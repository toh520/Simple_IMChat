#include "server/ChatServer.h"
#include "server/chatservice.hpp" // [修复] 引入业务类头文件
#include <iostream>
#include <cstring>
#include <cerrno>
#include <functional> // for std::bind
#include <thread> // [新增]
#include <unistd.h>

ChatServer::ChatServer(int port) : port_(port) {
    // 1. 初始化监听 Socket
    listener_ = std::make_unique<Socket>();
    listener_->bind("0.0.0.0", port_);
    listener_->listen();
    listener_->setNonBlocking(); // Epoll 必须非阻塞

    // 2. 初始化 Epoll
    epoll_ = std::make_unique<Epoll>();
    
    // 3. 把监听 Socket 加入 Epoll
    // EPOLLIN: 读事件, EPOLLET: 边缘触发
    epoll_->updateChannel(listener_->getFd(), EPOLL_CTL_ADD, EPOLLIN | EPOLLET);

    std::cout << "ChatServer 初始化完成，监听端口: " << port_ << std::endl;
}

ChatServer::~ChatServer() {
    // 智能指针会自动释放 Socket 和 Epoll，不需要手动 delete
}

void ChatServer::start() {
    std::cout << "ChatServer 服务已启动..." << std::endl;

    // [新增] 启动后台心跳检测线程
    std::thread checkThread(std::bind(&ChatServer::checkConnectionTask, this));
    checkThread.detach();

    while (true) {
        // 等待事件
        auto events = epoll_->poll();


        for (auto& event : events) {
            int fd = event.data.fd;

            // 情况 A: 监听 Socket 有动静 -> 新用户连接
            if (fd == listener_->getFd()) {
                handleNewConnection();
            }
            else {
                // [加锁] 保护 connections_ 的读取
                TcpConnection::ptr conn = nullptr;
                {
                    std::lock_guard<std::mutex> lock(connMutex_);
                    auto it = connections_.find(fd);
                    if (it != connections_.end()) {
                        conn = it->second;
                    }
                }

                if (!conn) {
                    epoll_->updateChannel(fd, EPOLL_CTL_DEL, 0);
                    close(fd);
                    continue;
                }

                // 优先处理异常/对端半关闭事件
                if (event.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    conn->onRead();
                    continue;
                }

                if (event.events & EPOLLIN) {
                    conn->onRead();
                }

                if (event.events & EPOLLOUT) {
                    conn->onWrite();
                }
            }
        }
    }
}

void ChatServer::handleNewConnection() {
    // ET 模式下必须把 accept 队列“读空”，直到 EAGAIN/EWOULDBLOCK
    while (true) {
        struct sockaddr_in addr;
        int clnt_fd = listener_->accept(&addr);

        if (clnt_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // 已经没有待处理的新连接
            }
            if (errno == EINTR) {
                continue; // 被信号中断，重试 accept
            }
            std::cout << "accept error, errno=" << errno << std::endl;
            break;
        }

        // 创建连接对象
        auto conn = std::make_shared<TcpConnection>(epoll_.get(), clnt_fd);

        // [关键] 设置关闭回调
        // 当 TcpConnection 发现客户端断开时，会调用 ChatServer::handleClientDisconnect
        conn->setCloseCallback(std::bind(&ChatServer::handleClientDisconnect, this, std::placeholders::_1));

        // 存入 map
        {
            std::lock_guard<std::mutex> lock(connMutex_);
            connections_[clnt_fd] = conn;
        }

        // 加入 Epoll
        epoll_->updateChannel(clnt_fd, EPOLL_CTL_ADD, EPOLLIN | EPOLLET | EPOLLRDHUP);

        std::cout << "新连接建立 fd=" << clnt_fd << " 当前在线(Roughly): " << connections_.size() << std::endl;
    }
}

void ChatServer::handleClientDisconnect(int fd) {
    TcpConnection::ptr conn = nullptr;
    
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        auto it = connections_.find(fd);
        if (it != connections_.end()) {
            conn = it->second;
            connections_.erase(it);
        }
    }
    
    // [新增] 通知业务层处理客户端异常退出 (比如把用户状态改为 offline)
    // 这里需要 ChatService 提供一个处理客户端异常退出的接口
    if (conn) {
        ChatService::instance()->clientCloseException(conn);
    }

    std::cout << "客户端断开，已回收资源 fd=" << fd << " 当前在线: " << connections_.size() << std::endl;
}
// 定时任务：扫描所有超时连接并断开
void ChatServer::checkConnectionTask() {
    while (true) {
        // 每 5 秒检查一次
        sleep(5);
        
        time_t now = time(nullptr);
        
        // [加锁保护] - 这里的逻辑略复杂，因为我们不想一直长时间持有锁
        // 方案：快速拷贝所有 snapshot，或者就在得锁期间处理
        // 考虑到 demo，直接加锁遍历
        std::vector<int> timeout_fds;
        
        {
            std::lock_guard<std::mutex> lock(connMutex_);
            for (auto it = connections_.begin(); it != connections_.end(); ++it) {
                TcpConnection::ptr conn = it->second;
                if (now - conn->getAliveTime() > 30) {
                    timeout_fds.push_back(it->first);
                }
            }
        } // 释放锁
        
        for (int fd : timeout_fds) {
             std::cout << "[Heartbeat] Connection timeout fd=" << fd << ", kicking out..." << std::endl;
             // shutdown 会触发主 loop 的 onRead -> read 0 -> handleClientDisconnect
             shutdown(fd, SHUT_RDWR);
        }
    }
}
