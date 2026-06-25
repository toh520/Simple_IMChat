#pragma once
#include <memory>
#include <functional>
#include <string>
#include <atomic>
#include <mutex> // [修复] 补上 mutex 头文件
#include "net/Socket.h" // 确保这些头文件里没有循环引用
#include "net/Epoll.h"
#include "net/Buffer.h"

// [关键修改] 继承 std::enable_shared_from_this
// 这样我们在成员函数里就能通过 shared_from_this() 拿到管理自己的那个智能指针
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    using ptr = std::shared_ptr<TcpConnection>;
    using CloseCallback = std::function<void(int)>;

    TcpConnection(Epoll* epoll, int fd);
    ~TcpConnection();

    void onRead();
    void onWrite();

    // [新增] 刷新最后活跃时间
    void refreshAliveTime();
    // [新增] 获取最后活跃时间
    time_t getAliveTime() const { return lastActiveTime_; }

    // [新增] 发送数据的方法 (业务层会调用这个)
    // 直接发送 string 数据
    void send(std::string msg);
    // [新增] 按照协议发送 Header + MsgID + Data
    void send(int msgid, std::string data);

    void setCloseCallback(const CloseCallback& cb) { closeCallback_ = cb; }

private:
    // 保护发送操作的互斥锁（因为多线程业务可能同时调用 send）
    std::mutex sendMutex_;

    // 发送缓冲区：当非阻塞 write 发生 EAGAIN/部分写时，剩余数据先入队
    std::string writeBuffer_;
    bool writeEventEnabled_;
    std::atomic_bool closed_;

    Epoll* epoll_;
    std::unique_ptr<Socket> socket_;
    Buffer readBuffer_;
    
    // [新增] 记录最后活跃时间戳
    time_t lastActiveTime_;

    CloseCallback closeCallback_;
};