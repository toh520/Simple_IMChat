#pragma once
#include <map>
#include <memory>
#include <mutex> // [修复] 补全 mutex 头文件
#include "net/Epoll.h"
#include "net/Socket.h"
#include "net/TcpConnection.h"

class ChatServer {
public:
    // 构造函数：指定监听端口
    ChatServer(int port);
    ~ChatServer();

    // 启动服务
    void start();

private:
    // [新增] 定时检测连接活性的后台任务
    void checkConnectionTask();

    // 处理新连接事件
    void handleNewConnection();
    // 处理客户端断开事件 (作为回调传给 TcpConnection)
    void handleClientDisconnect(int fd);

private:
    int port_;
    std::unique_ptr<Socket> listener_; // 监听 Socket
    std::unique_ptr<Epoll> epoll_;     // Epoll 实例
    
    // 连接管理 Map：key是fd，value是连接对象
    std::map<int, TcpConnection::ptr> connections_;
    // [新增] 保护 connections_ 的互斥锁
    std::mutex connMutex_;
};
