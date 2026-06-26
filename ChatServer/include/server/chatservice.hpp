#pragma once

#include <unordered_map>
#include <functional>
#include <mutex>
#include <string>
#include <memory>
#include <thread>
#include <atomic>

#include "msg.pb.h"
#include "server/model/UserModel.hpp"
#include "server/model/offlinemessagemodel.hpp"
#include "net/TcpConnection.h"
#include "server/ThreadPool.hpp"
#include "db/Redis.h"

// 业务回调函数类型
// conn: 连接对象 (用于回发数据)
// data: 序列化后的 protobuf 数据字符串 (去掉 header 和 msgid 后的纯数据)
using MsgHandler = std::function<void(const std::shared_ptr<TcpConnection>& conn, std::string& data)>;


// 聊天服务器业务类 (单例模式)
class ChatService {
public:
    // 获取单例对象的接口
    static ChatService* instance();

    ~ChatService();

    // 设置节点物理地址并执行路由自清理
    void setHostInfo(std::string ip, int port);

    // 处理登录业务
    void login(const std::shared_ptr<TcpConnection>& conn, std::string& data);

    // 处理注册业务
    void reg(const std::shared_ptr<TcpConnection>& conn, std::string& data);

    // 处理一对一聊天业务
    void oneChat(const std::shared_ptr<TcpConnection>& conn, std::string& data);

    // 处理接收端确认接收业务
    void recvAck(const std::shared_ptr<TcpConnection>& conn, std::string& data);

    // [新增] 处理心跳业务
    void clientHeartBeat(const std::shared_ptr<TcpConnection>& conn, std::string& data);

    // 处理客户端异常退出
    void clientCloseException(const std::shared_ptr<TcpConnection>& conn);
    
    // 从 Redis 消息队列中获取订阅的消息
    void handleRedisSubscribeMessage(int userid, std::string msg);

    // 获取消息对应的处理器
    MsgHandler getHandler(int msgid);

private:
    ChatService();

    // 守护重传循环函数
    void retransmitLoop();

    // 存储消息id和其对应的业务处理方法
    std::unordered_map<int, MsgHandler> _msgHandlerMap;

    // 线程池
    std::unique_ptr<ThreadPool> _threadPool;

    // Redis 对象
    Redis _redis;

    // 数据操作对象
    UserModel _userModel;
    OfflineMsgModel _offlineMsgModel;

    // 存储在线用户的通信连接
    std::mutex _connMutex;
    std::unordered_map<int, std::shared_ptr<TcpConnection>> _userConnMap;

    // --- 服务端可靠重传组件 ---
    struct PendingRecvMsg {
        int64_t msgId;
        int fromId;
        int toId;
        std::string msgData; // OneChatRequest 序列化包体
        time_t lastSendTime;
        int retryCount;
        std::shared_ptr<TcpConnection> conn;
    };

    std::atomic_bool _running;
    std::thread _retransmitThread;
    std::mutex _pendingMutex;
    std::unordered_map<int64_t, PendingRecvMsg> _pendingRecvAckMap;

    // 本物理节点地址
    std::string _ip;
    int _port;
};