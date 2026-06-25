#pragma once

#include <sys/epoll.h> //Linux 下 epoll 相关的头文件
#include <vector>

class Epoll{

public:
    Epoll();
    ~Epoll();

    // 核心功能 1：把一个 Socket (fd) 加入监听名单
    // op: 你想干什么？(EPOLL_CTL_ADD 添加 / EPOLL_CTL_DEL 删除)
    void updateChannel(int fd, int op, uint32_t events);

    // 核心功能 2：等待事件发生
    // timeout: 等待多久？(-1 表示死等，直到有事发生)
    // 返回值：发生的一组事件 (比如：Socket A 有数据读，Socket B 断开了)
    std::vector<epoll_event> poll(int timeout = -1);


private:
    int epollFd; // Epoll 的身份证号 (文件描述符)
    struct epoll_event* events; // 这是一个数组，用来暂存刚才发生的事件

};