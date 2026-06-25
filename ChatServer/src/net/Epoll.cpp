#include "net/Epoll.h"
#include <unistd.h>     // close
#include <cstring>      // bzero (清空内存)
#include <stdexcept>    // 异常处理

// 设定每次最多处理多少个事件，1024 是个经验值，够用了
#define MAX_EVENTS 1024

Epoll::Epoll()
{
    // 创建 epoll 实例
    // epoll_create1(0) 是较新的写法，比老版 epoll_create 更推荐
    epollFd = epoll_create1(0);
    if (epollFd == -1) {
        throw std::runtime_error("Epoll 创建失败！");
    }

    // 2. 申请一段内存，用来存放操作系统告诉我们的“活跃事件”
    events = new epoll_event[MAX_EVENTS];
    bzero(events, sizeof(*events) * MAX_EVENTS);
}

Epoll::~Epoll()
{
    // 关闭 epoll 文件描述符
    if (epollFd != -1) {
        close(epollFd); // 关掉句柄
        delete[] events; // 释放内存，防止内存泄露
    }
}

void Epoll::updateChannel(int fd, int op, uint32_t events_flag) {
    struct epoll_event ev;
    bzero(&ev, sizeof(ev));
    
    ev.data.fd = fd;      // 记录是哪个 Socket
    ev.events = events_flag; // 记录我们关心什么事件 (读? 写? 边缘触发?)

    // epoll_ctl 是“控制”函数，用来增删改监听列表
    if (epoll_ctl(epollFd, op, fd, &ev) == -1) {
        throw std::runtime_error("Epoll control error");
    }
}

std::vector<epoll_event> Epoll::poll(int timeout) {
    std::vector<epoll_event> activeEvents;
    
    // 3. 核心等待函数
    // epollFd: 谁在等？
    // events: 发生的事件存哪？
    // MAX_EVENTS: 最多存多少个？
    // timeout: 等多久？
    // 返回值 nfds: 实际发生了多少个事件
    int nfds = epoll_wait(epollFd, events, MAX_EVENTS, timeout);
    
    if (nfds == -1) {
        throw std::runtime_error("Epoll wait error");
    }

    // 4. 把数组里的数据转移到 vector 中返回给上层
    for (int i = 0; i < nfds; ++i) {
        activeEvents.push_back(events[i]);
    }
    
    return activeEvents;
}