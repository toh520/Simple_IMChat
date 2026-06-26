#include "server/chatservice.hpp"
#include "public.hpp"
#include "msg.pb.h"
#include <iostream>

using namespace std;
using namespace chat; // protobuf 命名空间

ChatService* ChatService::instance() {
    static ChatService service;
    return &service;
}

void ChatService::setHostInfo(std::string ip, int port) {
    _ip = ip;
    _port = port;
    // 清理上次本物理节点异常残留的路由缓存
    std::string nodeVal = _ip + ":" + std::to_string(_port);
    _redis.cleanNodeRoutes("user:route", nodeVal);
    LOG_INFO("清理节点 [" + nodeVal + "] 在 Redis 中的历史残留路由完成");
}

// 注册消息以及对应的Handler回调操作
ChatService::ChatService() : _running(true), _saveThreadRunning(true) {
    // 启动线程池 (例如 4 个 worker 线程)
    // 根据机器 CPU 核心数或者业务负载调整，这里默认给 4 个
    _threadPool = std::make_unique<ThreadPool>(4);

    // 用户注册业务管理
    // 当收到 REG_MSG (注册) 消息时，绑定 to ChatService::reg 方法
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, std::placeholders::_1, std::placeholders::_2)});
    
    // 用户登录业务管理
    // 当收到 LOGIN_MSG (登录) 消息时，绑定 to ChatService::login 方法
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, std::placeholders::_1, std::placeholders::_2)});

    // 一对一聊天业务管理
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, std::placeholders::_1, std::placeholders::_2)});

    // [新增] 注册接收确认消息处理
    _msgHandlerMap.insert({MSG_RECV_ACK, std::bind(&ChatService::recvAck, this, std::placeholders::_1, std::placeholders::_2)});

    // [新增] 注册添加好友申请消息处理
    _msgHandlerMap.insert({ADD_FRIEND_REQ, std::bind(&ChatService::sendApply, this, std::placeholders::_1, std::placeholders::_2)});

    // [新增] 注册处理好友申请消息处理 (同意/拒绝)
    _msgHandlerMap.insert({PROCESS_FRIEND_REQ, std::bind(&ChatService::processApply, this, std::placeholders::_1, std::placeholders::_2)});

    // [新增] 注册心跳消息处理
    _msgHandlerMap.insert({HEART_BEAT_MSG, std::bind(&ChatService::clientHeartBeat, this, std::placeholders::_1, std::placeholders::_2)});

    // [新增] 注册消息同步处理 (Timeline Sync)
    _msgHandlerMap.insert({SYNC_REQ, std::bind(&ChatService::syncMessages, this, std::placeholders::_1, std::placeholders::_2)});

    // [新增] 只有在构造时重置一次所有用户状态为 offline
    // 防止服务器崩溃重启后，状态仍为 online 导致无法登录
    _userModel.resetState();

    // 连接 Redis
    if (_redis.connect()) {
        // 设置上报消息的回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, std::placeholders::_1, std::placeholders::_2));
    }

    // 启动重传扫描守护线程
    _retransmitThread = std::thread(&ChatService::retransmitLoop, this);

    // [新增] 启动后台存盘守护线程
    _saveThread = std::thread(&ChatService::backgroundSaveThread, this);
}

ChatService::~ChatService() {
    _running = false;
    if (_retransmitThread.joinable()) {
        _retransmitThread.join();
    }

    // [新增] 安全停止后台存盘线程
    _saveThreadRunning = false;
    _queueCond.notify_all();
    if (_saveThread.joinable()) {
        _saveThread.join();
    }
}

// 获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid) {
    auto it = _msgHandlerMap.find(msgid);
    if (it == _msgHandlerMap.end()) {
        // 返回一个默认的空处理器，防止崩溃，并打印错误日志
        return [=](const std::shared_ptr<TcpConnection>& conn, std::string& data) {
            (void)conn;
            (void)data;
            LOG_ERROR("未找到消息类型对应的业务处理器: msgid=" + std::to_string(msgid));
        };
    } else {
        // [新增] 异步解耦核心：
        // 返回一个 Lambda，这个 Lambda 并不直接执行业务，而是把任务提交到 ThreadPool
        return [this, it](const std::shared_ptr<TcpConnection>& conn, std::string data) {
            // 将 (handler, conn, data) 投递给线程池
            _threadPool->enqueue([=]() {
                // 这个 lambda 会在子线程中执行 -> 真正的业务逻辑
                // 注意 data 是按值捕获的，虽然有一次拷贝，但保证了数据在子线程有效
                // conn 是 shared_ptr，安全
                // it->second 就是真正的 login/reg/chat 方法
                std::string d = data; // 只是为了解 const
                it->second(conn, d);
            });
        };
    }
}

// 处理注册业务
void ChatService::reg(const std::shared_ptr<TcpConnection>& conn, std::string& data) {
    RegRequest req;
    // 1. 反序列化: 把网络层传来的 string 数据转成 RegRequest 对象
    if (req.ParseFromString(data)) {
        string name = req.username();
        string pwd = req.password();

        User user;
        user.setName(name);
        user.setPwd(pwd);
        
        // 2. 真正操作数据库 (Model层)
        bool state = _userModel.insert(user);
        
        // 3. 构建响应
        RegResponse resp;
        if (state) {
            // 注册成功
            resp.set_success(true);
            resp.set_uid(user.getId()); // 这是一个亮点，注册成功直接返回ID
            resp.set_msg("注册成功");
            LOG_INFO("用户注册成功: 用户名=" + name + ", 分配的 UID=" + std::to_string(user.getId()));
        } else {
            // 注册失败
            resp.set_success(false);
            resp.set_msg("注册失败，用户名可能已存在");
            LOG_WARN("用户注册失败: 用户名=" + name + " (可能已存在)");
        }

        // 4. 序列化并发送回去
        string send_str;
        resp.SerializeToString(&send_str);
        
        conn->send(REG_MSG_ACK, send_str);
    }
}

// 处理登录业务
void ChatService::login(const std::shared_ptr<TcpConnection>& conn, std::string& data) {
    LoginRequest req;
    if (req.ParseFromString(data)) {
        int id = 0;
        string pwd = req.password();
        // 兼容 username 字段存放 id（简化设计，或者你需要完善协议增加 id 字段）
        // 这里假设协议设计早期，暂时暂定 username 字段如果有纯数字则当作 id 登录，或者后续修改 LoginRequest
        // 为了严谨，建议修改 LoginRequest 增加 int32 id 字段，但在现有 protobuf 基础上，我们先解析 username
        
        // 实际上现有代码 LoginRequest 只有 username 和 password
        // 我们先假设 username 就是用户的唯一标识（名字），但数据库 UserModel::query 是按 id 查的
        // 这是一个逻辑断层。通常登录是 ID + Pwd 或者 Name + Pwd
        // 让我们先暂时用 Name + Pwd 查库，需要给 UserModel 加一个按 Name 查询的方法
        // 或者简单起见，这里先做一个模拟：如果 username 是数字字符串，转成 ID
        
        // 为了项目含金量，我们假设用户输入的是 ID
        try {
            id = std::stoi(req.username()); 
        } catch (...) {
            // 如果不是 ID，理论上应该支持名字登录，这里暂时简化，如果转换失败则 id=0
            id = 0;
        }

        User user = _userModel.query(id);

        LoginResponse resp;
        string send_str;

        if (user.getId() == id && user.getPwd() == pwd) {
            // 登录成功
            if (user.getState() == "online") {
                // 用户已经在线，不允许重复登录
                resp.set_success(false);
                resp.set_msg("该账号已在线，请勿重复登录");
            } else {
                // 1. 记录用户连接
                {
                    lock_guard<mutex> lock(_connMutex);
                    _userConnMap.insert({id, conn});
                }
                
                // [新增] 登录成功后，向 Redis 订阅该用户的 Channel
                _redis.subscribe(id);

                // 注册路由网关：userid -> server_ip:port
                std::string nodeVal = _ip + ":" + std::to_string(_port);
                _redis.hset("user:route", std::to_string(id), nodeVal);
                LOG_INFO("用户在线状态已同步至 Redis 状态网关, UID=" + std::to_string(id) + " -> " + nodeVal);

                // 2. 更新数据库状态为 online
                user.setState("online");
                _userModel.updateState(user);

                // [新增] 3. 查询好友关系并缓存 Redis Set，同时带回其在线状态快照
                vector<User> friendVec = _friendModel.query(id);
                for (const auto& frnd : friendVec) {
                    _redis.sadd("user:friends:" + to_string(id), to_string(frnd.getId()));
                    
                    FriendInfo* fi = resp.add_friends();
                    fi->set_id(frnd.getId());
                    fi->set_name(frnd.getName());
                    
                    // 比对路由网关以得出最新的在线状态
                    string routeVal = _redis.hget("user:route", to_string(frnd.getId()));
                    if (!routeVal.empty()) {
                        fi->set_state("online");
                    } else {
                        fi->set_state("offline");
                    }
                }

                // [新增] 4. 查询待处理的好友申请列表并塞入响应中
                auto pendingApplies = _friendRequestModel.queryPending(id);
                for (const auto& item : pendingApplies) {
                    FriendRequestNotify* frn = resp.add_pending_applies();
                    frn->set_apply_id(item.first.getId());
                    frn->set_from_id(item.first.getFromId());
                    frn->set_from_name(item.second);
                }

                // 3. 返回成功
                resp.set_success(true);
                resp.set_uid(user.getId());
                resp.set_msg("登录成功");
                LOG_INFO("用户登录成功: UID=" + std::to_string(user.getId()));

                // [新增] 5. 广播自身上线通知给所有在线好友
                for (const auto& frnd : friendVec) {
                    string routeVal = _redis.hget("user:route", to_string(frnd.getId()));
                    if (!routeVal.empty()) {
                        UserStatusNotify statusNotify;
                        statusNotify.set_uid(id);
                        statusNotify.set_state("online");
                        string notifyStr;
                        statusNotify.SerializeToString(&notifyStr);
                        
                        _redis.publish(frnd.getId(), "NTF:" + notifyStr);
                        LOG_DEBUG("向在线好友广播上线通知: 发送方 UID=" + to_string(id) + " -> 接收方 UID=" + to_string(frnd.getId()));
                    }
                }
            }
        } else {
            // 登录失败：用户不存在 或 密码错误
            resp.set_success(false);
            resp.set_msg("用户名或密码错误");
            LOG_WARN("用户登录失败: 输入 ID=" + std::to_string(id) + " (密码错误或用户不存在)");
        }
        
        resp.SerializeToString(&send_str);
        conn->send(LOGIN_MSG_ACK, send_str);
    }
}

// 处理客户端异常退出
void ChatService::clientCloseException(const std::shared_ptr<TcpConnection>& conn) {
    User user;
    {
        lock_guard<mutex> lock(_connMutex);
        
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); ++it) {
            if (it->second == conn) {
                // 找到了
                user.setId(it->first);
                // 从 map 移除
                _userConnMap.erase(it);
                break;
            }
        }
    }

    // 更新数据库状态
    if (user.getId() != -1) { 
        user.setState("offline");
        _userModel.updateState(user);
        
        // 用户下线，取消订阅
        _redis.unsubscribe(user.getId());

        // 从 Redis 状态网关注销路由
        _redis.hdel("user:route", std::to_string(user.getId()));
        LOG_INFO("用户已从 Redis 状态网关注销, UID=" + std::to_string(user.getId()));

        // [新增] 向所有在线的好友广播自身下线通知
        vector<User> friendVec = _friendModel.query(user.getId());
        for (const auto& frnd : friendVec) {
            string routeVal = _redis.hget("user:route", to_string(frnd.getId()));
            if (!routeVal.empty()) {
                UserStatusNotify statusNotify;
                statusNotify.set_uid(user.getId());
                statusNotify.set_state("offline");
                string notifyStr;
                statusNotify.SerializeToString(&notifyStr);
                
                _redis.publish(frnd.getId(), "NTF:" + notifyStr);
                LOG_DEBUG("向在线好友广播下线通知: 发送方 UID=" + to_string(user.getId()) + " -> 接收方 UID=" + to_string(frnd.getId()));
            }
        }

        // 清理该用户在重传队列中的所有待接收确认消息，并转存为离线消息
        {
            lock_guard<mutex> lock(_pendingMutex);
            for (auto it = _pendingRecvAckMap.begin(); it != _pendingRecvAckMap.end(); ) {
                if (it->second.toId == user.getId()) {
                    LOG_DEBUG("用户下线，转存待确认消息为离线消息, msgId=" + std::to_string(it->second.msgId));
                    _offlineMsgModel.insert(user.getId(), it->second.msgData);
                    it = _pendingRecvAckMap.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
}

// 从 Redis 收到消息：说明有别的服务器发消息给本服务器上的用户了
void ChatService::handleRedisSubscribeMessage(int userid, std::string msg) {
    if (msg.size() < 4) {
        LOG_ERROR("收到无效的跨服路由消息，长度不足 4 字节");
        return;
    }
    std::string prefix = msg.substr(0, 4);
    std::string payload = msg.substr(4);

    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end()) {
        if (prefix == "MSG:") {
            it->second->send(ONE_CHAT_MSG, payload);
            LOG_INFO("从 Redis 通道收到跨服单聊消息，转发给在线用户 UID=" + std::to_string(userid));
            
            // 反序列化取出 msg_id，以便加入本服务器的重传确认队列中
            OneChatRequest req;
            if (req.ParseFromString(payload)) {
                PendingRecvMsg pMsg;
                pMsg.msgId = req.msg_id();
                pMsg.fromId = req.from_id();
                pMsg.toId = userid;
                pMsg.msgData = payload;
                pMsg.lastSendTime = time(nullptr);
                pMsg.retryCount = 0;
                pMsg.conn = it->second;
                
                lock_guard<mutex> pLock(_pendingMutex);
                _pendingRecvAckMap.insert({req.msg_id(), pMsg});
            }
        }
        else if (prefix == "NTF:") {
            it->second->send(USER_STATUS_NOTIFY_MSG, payload);
            LOG_INFO("从 Redis 通道收到跨服状态变更通知，转发给在线用户 UID=" + std::to_string(userid));
        }
        else if (prefix == "APY:") {
            it->second->send(FRIEND_REQUEST_NOTIFY, payload);
            LOG_INFO("从 Redis 通道收到跨服好友申请通知，转发给在线用户 UID=" + std::to_string(userid));
        }
        else if (prefix == "BND:") {
            it->second->send(ADD_FRIEND_SUCCESS_NOTIFY, payload);
            LOG_INFO("从 Redis 通道收到跨服好友成功绑定通知，转发给在线用户 UID=" + std::to_string(userid));
        }
        return;
    }

    // 用户刚好下线，如果收到的是 MSG 单聊包，则需要落盘存入离线消息
    if (prefix == "MSG:") {
        _offlineMsgModel.insert(userid, payload);
        LOG_INFO("跨服单聊目标用户已下线，消息转存为离线消息, UID=" + std::to_string(userid));
    }
}

// 一对一聊天业务
void ChatService::oneChat(const std::shared_ptr<TcpConnection>& conn, std::string& data) {
    // 1. 立即给发送端回发 MSG_SEND_ACK 告诉发送方服务器已接管该消息
    OneChatRequest req;
    if (!req.ParseFromString(data)) {
        LOG_ERROR("反序列化 OneChatRequest 失败");
        return;
    }
    int toid = req.to_id();
    int fromid = req.from_id();

    // 好友关系鉴权 (防骚扰)：必须是好友才能发送消息
    bool isFriend = _redis.sismember("user:friends:" + to_string(fromid), to_string(toid));
    if (!isFriend) {
        MsgSendAck ack;
        ack.set_msg_id(req.msg_id());
        ack.set_success(false);
        ack.set_err_msg("对方还不是您的好友，请先添加好友");
        std::string ackStr;
        ack.SerializeToString(&ackStr);
        conn->send(MSG_SEND_ACK, ackStr);
        LOG_WARN("非好友单聊拦截: 发送方 UID=" + to_string(fromid) + " -> 接收方 UID=" + to_string(toid) + ", msgId=" + to_string(req.msg_id()));
        return;
    }

    LOG_INFO("收到单聊消息：发送方 UID=" + std::to_string(fromid) + " -> 接收方 UID=" + std::to_string(toid) + ", msgId=" + std::to_string(req.msg_id()));

    MsgSendAck ack;
    ack.set_msg_id(req.msg_id());
    ack.set_success(true);
    std::string ackStr;
    ack.SerializeToString(&ackStr);
    conn->send(MSG_SEND_ACK, ackStr);
    LOG_DEBUG("已向发送方 UID=" + std::to_string(fromid) + " 回发接管确认 ACK, msgId=" + std::to_string(req.msg_id()));

    // [新增] 2. 写入全局异步批量存盘队列，由后台线程批量合并 Insert
    MessageHistory histMsg(req.msg_id(), fromid, toid, data, "");
    {
        lock_guard<mutex> lock(_queueMutex);
        _saveMsgQueue.push(histMsg);
    }
    _queueCond.notify_one();

    // [新增] 3. 写入 Redis ZSet 时间线缓存中，限制保留最新 100 条
    std::string timelineKey = "user:timeline:" + to_string(toid);
    _redis.zadd(timelineKey, req.msg_id(), data);
    _redis.zremrangebyrank(timelineKey, 0, -101);

    // 4. 查找接收端是否在线，在线则进行转发推送
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end()) {
            // 用户在线，直接转发消息
            it->second->send(ONE_CHAT_MSG, data);

            // 加入待接收确认队列
            PendingRecvMsg pMsg;
            pMsg.msgId = req.msg_id();
            pMsg.fromId = fromid;
            pMsg.toId = toid;
            pMsg.msgData = data;
            pMsg.lastSendTime = time(nullptr);
            pMsg.retryCount = 0;
            pMsg.conn = it->second;

            lock_guard<mutex> pLock(_pendingMutex);
            _pendingRecvAckMap.insert({req.msg_id(), pMsg});
            LOG_INFO("接收方在线，本地直接转发消息，并将该消息加入待接收重传队列, msgId=" + std::to_string(req.msg_id()));
            return;
        } 
    } // 锁在这里释放

    // 5. 本地不在线，查询 Redis 状态网关
    std::string route = _redis.hget("user:route", std::to_string(toid));
    if (!route.empty()) {
        // 用户在其他节点在线 -> 通过 Redis 发布订阅，完成分布式跨节点路由转发，加 "MSG:" 前缀
        _redis.publish(toid, "MSG:" + data);
        LOG_INFO("接收方在节点 [" + route + "] 在线，通过 Redis 状态网关完成跨服路由转发, msgId=" + std::to_string(req.msg_id()));
        return;
    }

    // 6. 不在线且没有路由，退化为静默缓存状态
    LOG_INFO("接收方当前不在线，消息已存入后台存盘队列与 Redis ZSet, msgId=" + std::to_string(req.msg_id()));
}

// 处理接收端确认接收业务
void ChatService::recvAck(const std::shared_ptr<TcpConnection>& conn, std::string& data) {
    (void)conn;
    MsgRecvAck ack;
    if (ack.ParseFromString(data)) {
        int64_t msgId = ack.msg_id();
        lock_guard<mutex> lock(_pendingMutex);
        _pendingRecvAckMap.erase(msgId);
        LOG_INFO("收到接收方回复的已送达 ACK，中止重传并从队列移出该消息, msgId=" + std::to_string(msgId));
    }
}

// 守护重传循环函数
void ChatService::retransmitLoop() {
    while (_running) {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        lock_guard<mutex> lock(_pendingMutex);
        for (auto it = _pendingRecvAckMap.begin(); it != _pendingRecvAckMap.end(); ) {
            time_t now = time(nullptr);
            if (now - it->second.lastSendTime >= 3) { // 3秒超时重传
                if (it->second.retryCount < 3) {
                    it->second.retryCount++;
                    it->second.lastSendTime = now;
                    LOG_WARN("消息接收超时，重传给用户 " + std::to_string(it->second.toId) 
                             + ", msgId=" + std::to_string(it->second.msgId) 
                             + " (重试次数: " + std::to_string(it->second.retryCount) + ")");
                    it->second.conn->send(ONE_CHAT_MSG, it->second.msgData);
                    ++it;
                } else {
                    // 重传达上限，说明对方假死/断网，转为离线并强退连接
                    LOG_ERROR("消息投递最终超时失效：目标用户 UID=" + std::to_string(it->second.toId) 
                              + " 假死或断开，强切其长连接并转离线，msgId=" + std::to_string(it->second.msgId));

                    _offlineMsgModel.insert(it->second.toId, it->second.msgData);

                    // 强断套接字
                    clientCloseException(it->second.conn);
                    it->second.conn->shutdown();

                    it = _pendingRecvAckMap.erase(it);
                }
            } else {
                ++it;
            }
        }
    }
}

// [新增] 处理客户端心跳
// 实际上这里不需要做太多业务逻辑，因为 TcpConnection::onRead 每次读到数据都会刷新 lastActiveTime
// 这里只是为了响应 MSGID，避免 "can not find handler" 报错
void ChatService::clientHeartBeat(const std::shared_ptr<TcpConnection>& conn, std::string& data) {
    (void)conn;
    (void)data;
    LOG_DEBUG("收到客户端心跳包，刷新连接活性");
}

// 处理添加好友申请业务
void ChatService::sendApply(const std::shared_ptr<TcpConnection>& conn, std::string& data) {
    AddFriendReq req;
    AddFriendResp resp;
    string send_str;

    if (req.ParseFromString(data)) {
        int from_id = req.from_id();
        int to_id = req.to_id();

        LOG_INFO("收到加好友申请：发送方 UID=" + to_string(from_id) + " -> 接收方 UID=" + to_string(to_id));

        if (from_id == to_id) {
            resp.set_success(false);
            resp.set_msg("不能添加自己为好友");
        } else {
            // 1. 校验目标用户是否存在
            User target = _userModel.query(to_id);
            if (target.getId() == -1) {
                resp.set_success(false);
                resp.set_msg("目标用户不存在");
            } else {
                // 2. 校验是否已经是好友
                bool isFriend = _redis.sismember("user:friends:" + to_string(from_id), to_string(to_id));
                if (isFriend) {
                    resp.set_success(false);
                    resp.set_msg("对方已在您的好友列表中");
                } else {
                    // 3. 暂存到数据库中，状态为 pending
                    int apply_id = _friendRequestModel.insert(from_id, to_id);
                    if (apply_id == -1) {
                        resp.set_success(false);
                        resp.set_msg("发送好友申请失败，数据库更新异常");
                    } else {
                        resp.set_success(true);
                        resp.set_msg("好友申请已发送，等待对方同意");

                        // 4. 若目标在线，跨服发送申请实时通知
                        string routeVal = _redis.hget("user:route", to_string(to_id));
                        if (!routeVal.empty()) {
                            User fromUser = _userModel.query(from_id);
                            FriendRequestNotify applyNotify;
                            applyNotify.set_apply_id(apply_id);
                            applyNotify.set_from_id(from_id);
                            applyNotify.set_from_name(fromUser.getName());
                            
                            string notifyStr;
                            applyNotify.SerializeToString(&notifyStr);
                            
                            _redis.publish(to_id, "APY:" + notifyStr);
                            LOG_DEBUG("已跨服向目标用户实时推送好友申请: to_id=" + to_string(to_id));
                        }
                    }
                }
            }
        }

        resp.SerializeToString(&send_str);
        conn->send(ADD_FRIEND_RESP, send_str);
    }
}

// 处理同意/拒绝好友申请业务
void ChatService::processApply(const std::shared_ptr<TcpConnection>& conn, std::string& data) {
    ProcessFriendReq req;
    ProcessFriendResp resp;
    string send_str;

    if (req.ParseFromString(data)) {
        int apply_id = req.apply_id();
        int from_id = req.from_id();
        int to_id = req.to_id();
        bool accept = req.accept();

        LOG_INFO("收到处理好友申请：处理方 UID=" + to_string(to_id) + ", 申请方 UID=" + to_string(from_id) + ", 同意=" + (accept ? "是" : "否"));

        // 1. 更新申请表的状态
        string newStatus = accept ? "accepted" : "rejected";
        bool updateOk = _friendRequestModel.updateStatus(apply_id, newStatus);
        
        if (!updateOk) {
            resp.set_success(false);
            resp.set_msg("处理好友申请失败，数据库写入异常");
            resp.set_apply_id(apply_id);
            resp.set_accept(accept);
        } else {
            resp.set_success(true);
            resp.set_msg(accept ? "已同意好友申请" : "已拒绝好友申请");
            resp.set_apply_id(apply_id);
            resp.set_accept(accept);

            if (accept) {
                // 2. 关系双向落盘，写入 MySQL Friend 表
                _friendModel.insert(from_id, to_id);

                // 3. 缓存在线好友 Set 集合到 Redis 缓存中
                _redis.sadd("user:friends:" + to_string(from_id), to_string(to_id));
                _redis.sadd("user:friends:" + to_string(to_id), to_string(from_id));

                // 4. 将新好友的信息塞回给处理人 (B)
                User fromUser = _userModel.query(from_id);
                FriendInfo* fi = resp.mutable_friend_info();
                fi->set_id(fromUser.getId());
                fi->set_name(fromUser.getName());
                
                string fromRoute = _redis.hget("user:route", to_string(from_id));
                fi->set_state(!fromRoute.empty() ? "online" : "offline");

                // 5. 判定申请人 A 是否在线。若在线，跨服发送成功通知以亮起好友
                string A_route = _redis.hget("user:route", to_string(from_id));
                if (!A_route.empty()) {
                    User toUser = _userModel.query(to_id);
                    AddFriendSuccessNotify successNotify;
                    FriendInfo* sfi = successNotify.mutable_friend_info();
                    sfi->set_id(toUser.getId());
                    sfi->set_name(toUser.getName());
                    sfi->set_state("online"); // 处理人 B 此时肯定是在线的
                    
                    string successNotifyStr;
                    successNotify.SerializeToString(&successNotifyStr);

                    _redis.publish(from_id, "BND:" + successNotifyStr);
                    LOG_DEBUG("已跨服向申请人发送好友绑定成功通知: from_id=" + to_string(from_id));
                }
            }
        }

        resp.SerializeToString(&send_str);
        conn->send(PROCESS_FRIEND_RESP, send_str);
    }
}

// 后台异步合并存盘线程函数
void ChatService::backgroundSaveThread() {
    while (_saveThreadRunning) {
        std::vector<MessageHistory> msgs;
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            // 等待队列不为空，或者线程被终止，最多等待 1 秒
            _queueCond.wait_for(lock, std::chrono::milliseconds(1000), [this]() {
                return !_saveMsgQueue.empty() || !_saveThreadRunning;
            });

            // 批量从队列中提取消息，每次上限 100 条
            while (!_saveMsgQueue.empty() && msgs.size() < 100) {
                msgs.push_back(_saveMsgQueue.front());
                _saveMsgQueue.pop();
            }
        }

        // 执行批量落盘操作
        if (!msgs.empty()) {
            bool batchOk = _messageHistoryModel.insertBatch(msgs);
            if (batchOk) {
                LOG_INFO("[MySQL 异步批量落盘] 成功写入 " + std::to_string(msgs.size()) + " 条消息记录");
            } else {
                LOG_ERROR("[MySQL 异步批量落盘] 批量写入失败，数据条数: " + std::to_string(msgs.size()));
            }
        }
    }
}

// 处理客户端同步消息的请求 (Timeline Sync)
void ChatService::syncMessages(const std::shared_ptr<TcpConnection>& conn, std::string& data) {
    SyncReq req;
    SyncResp resp;
    std::string send_str;

    if (req.ParseFromString(data)) {
        int uid = req.uid();
        long long last_sync_key = req.last_sync_key();

        LOG_INFO("收到消息同步请求：UID=" + to_string(uid) + ", last_sync_key=" + to_string(last_sync_key));

        std::vector<MessageHistory> historyMsgs;
        bool cacheHit = false;

        // 1. 尝试从 Redis ZSet 时间线缓存中拉取
        std::string timelineKey = "user:timeline:" + to_string(uid);
        long long min_score = _redis.zminscore(timelineKey);

        // 如果缓存非空，且 last_sync_key 大于等于缓存中最早的消息 ID，说明缓存完全覆盖了未读数据
        if (min_score != -1 && last_sync_key >= min_score) {
            std::vector<std::string> cachedMsgs = _redis.zrangebyscore(timelineKey, last_sync_key);
            for (const auto &raw : cachedMsgs) {
                MessageHistory msg;
                // 从缓存中直接取出序列化后的 OneChatRequest
                OneChatRequest oneReq;
                if (oneReq.ParseFromString(raw)) {
                    msg.setMsgId(oneReq.msg_id());
                    msg.setFromId(oneReq.from_id());
                    msg.setToId(oneReq.to_id());
                    msg.setContent(oneReq.msg());
                    historyMsgs.push_back(msg);
                }
            }
            cacheHit = true;
            LOG_INFO("[Redis Timeline 缓存命中] 从内存快速同步 " + std::to_string(historyMsgs.size()) + " 条消息给 UID=" + to_string(uid));
        }

        // 2. 如果缓存未命中（说明离线积压超出了 100 条限制，或者缓存已失效），则穿透到 MySQL 历史表查询
        if (!cacheHit) {
            historyMsgs = _messageHistoryModel.query(uid, last_sync_key);
            LOG_INFO("[Redis 缓存未命中，查询 MySQL 消息历史表] 成功同步 " + std::to_string(historyMsgs.size()) + " 条消息给 UID=" + to_string(uid));
        }

        // 3. 组装响应包
        resp.set_success(true);
        long long max_sync_key = last_sync_key;
        for (const auto &item : historyMsgs) {
            OneChatRequest* singleMsg = resp.add_messages();
            singleMsg->set_msg_id(item.getMsgId());
            singleMsg->set_from_id(item.getFromId());
            singleMsg->set_to_id(item.getToId());
            singleMsg->set_msg(item.getContent());

            if (item.getMsgId() > max_sync_key) {
                max_sync_key = item.getMsgId();
            }
        }
        resp.set_new_sync_key(max_sync_key);

        resp.SerializeToString(&send_str);
        conn->send(SYNC_RESP, send_str);
    }
}
