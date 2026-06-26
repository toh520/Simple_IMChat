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

// 注册消息以及对应的Handler回调操作
ChatService::ChatService() : _running(true) {
    // 启动线程池 (例如 4 个 worker 线程)
    // 根据机器 CPU 核心数或者业务负载调整，这里默认给 4 个
    _threadPool = std::make_unique<ThreadPool>(4);

    // 用户注册业务管理
    // 当收到 REG_MSG (注册) 消息时，绑定到 ChatService::reg 方法
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, std::placeholders::_1, std::placeholders::_2)});
    
    // 用户登录业务管理
    // 当收到 LOGIN_MSG (登录) 消息时，绑定到 ChatService::login 方法
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, std::placeholders::_1, std::placeholders::_2)});

    // 一对一聊天业务管理
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, std::placeholders::_1, std::placeholders::_2)});

    // [新增] 注册接收确认消息处理
    _msgHandlerMap.insert({MSG_RECV_ACK, std::bind(&ChatService::recvAck, this, std::placeholders::_1, std::placeholders::_2)});

    // [新增] 注册心跳消息处理
    _msgHandlerMap.insert({HEART_BEAT_MSG, std::bind(&ChatService::clientHeartBeat, this, std::placeholders::_1, std::placeholders::_2)});

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
}

ChatService::~ChatService() {
    _running = false;
    if (_retransmitThread.joinable()) {
        _retransmitThread.join();
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

                // 2. 更新数据库状态为 online
                user.setState("online");
                _userModel.updateState(user);

                // 3. 返回成功
                resp.set_success(true);
                resp.set_uid(user.getId());
                resp.set_msg("登录成功");
                LOG_INFO("用户登录成功: UID=" + std::to_string(user.getId()));
            }
        } else {
            // 登录失败：用户不存在 或 密码错误
            resp.set_success(false);
            resp.set_msg("用户名或密码错误");
            LOG_WARN("用户登录失败: 输入 ID=" + std::to_string(id) + " (密码错误或用户不存在)");
        }
        
        resp.SerializeToString(&send_str);
        conn->send(LOGIN_MSG_ACK, send_str);

        // 4. 如果登录成功，再推送离线消息
        if (resp.success()) {
            vector<string> vec = _offlineMsgModel.query(id);
            if (!vec.empty()) {
                for (const string& msg : vec) {
                    conn->send(ONE_CHAT_MSG, msg);
                }
                _offlineMsgModel.remove(id);
            }
        }
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
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end()) {
        it->second->send(ONE_CHAT_MSG, msg);
        
        // 反序列化取出 msg_id，以便加入本服务器的重传确认队列中
        OneChatRequest req;
        if (req.ParseFromString(msg)) {
            PendingRecvMsg pMsg;
            pMsg.msgId = req.msg_id();
            pMsg.fromId = req.from_id();
            pMsg.toId = userid;
            pMsg.msgData = msg;
            pMsg.lastSendTime = time(nullptr);
            pMsg.retryCount = 0;
            pMsg.conn = it->second;
            
            lock_guard<mutex> pLock(_pendingMutex);
            _pendingRecvAckMap.insert({req.msg_id(), pMsg});
        }
        return;
    }

    // 理论上如果订阅了该用户，意味着用户肯定在线。
    // 但可能正好用户下线了，消息刚到，这时候可以选择存离线，或者丢弃
    // 这里简单起见，存储离线消息
    _offlineMsgModel.insert(userid, msg);
}

// 一对一聊天业务
void ChatService::oneChat(const std::shared_ptr<TcpConnection>& conn, std::string& data) {
    OneChatRequest req;
    if (req.ParseFromString(data)) {
        int toid = req.to_id();
        int fromid = req.from_id();

        // 1. 立即给发送端回发 MSG_SEND_ACK 告诉发送方服务器已接管该消息
        MsgSendAck ack;
        ack.set_msg_id(req.msg_id());
        ack.set_success(true);
        std::string ackStr;
        ack.SerializeToString(&ackStr);
        conn->send(MSG_SEND_ACK, ackStr);

        // 2. 查找接收端是否在线
        {
            lock_guard<mutex> lock(_connMutex);
            auto it = _userConnMap.find(toid);
            if (it != _userConnMap.end()) {
                // 用户在线，转发消息
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
                return;
            } 
        } // 锁在这里释放

        // 查询数据库：用户虽然不在本服务器，但可能在其他服务器
        // 这一步是分布式聊天的关键！
        User user = _userModel.query(toid);
        if (user.getState() == "online") {
            // 用户状态是 online，但不在我的 _userConnMap 里
            // 说明用户在别的服务器上 -> 发布消息到 Redis
            _redis.publish(toid, data);
            return;
        }

        // 用户不在线 -> 存储离线消息
        _offlineMsgModel.insert(toid, data);
    }
}

// 处理接收端确认接收业务
void ChatService::recvAck(const std::shared_ptr<TcpConnection>& conn, std::string& data) {
    (void)conn;
    MsgRecvAck ack;
    if (ack.ParseFromString(data)) {
        int64_t msgId = ack.msg_id();
        lock_guard<mutex> lock(_pendingMutex);
        _pendingRecvAckMap.erase(msgId);
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
    // printf("Heartbeat from fd=%d\n", conn->fd());
    // 可以在这里回复一个 HEART_BEAT_ACK，也可以不回复（单向心跳）
}
