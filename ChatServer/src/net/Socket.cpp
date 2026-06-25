#include "net/Socket.h"
#include <sys/socket.h>
#include <stdexcept>
#include <unistd.h>
#include <arpa/inet.h>  // inet_addr 需要这个
#include <cstring>      // memset 需要这个
#include <fcntl.h>     // fcntl 需要这个



Socket::Socket()
{
    fd = socket(AF_INET,SOCK_STREAM,0);

    if (fd==-1){
        throw std::runtime_error("创建 Socket 失败！");
    }
    

}

Socket::Socket(int _fd) : fd(_fd) {
    if (fd == -1) {
        throw std::runtime_error("Socket 创建失败: 无效的 fd");
    }
}

Socket::~Socket()
{
    if (fd != -1) {
        close(fd);
    }
}

void Socket::bind(const char *ip, uint16_t port)
{
    struct sockaddr_in addr;
    // 1. 初始化结构体，清空垃圾值
    memset(&addr, 0, sizeof(addr));
    
    // 2. 设置地址族为 IPv4
    addr.sin_family = AF_INET;
    
    // 3. 设置 IP 地址 (将字符串 "0.0.0.0" 转换为网络字节序)
    addr.sin_addr.s_addr = inet_addr(ip);
    
    // 4. 设置端口 (htons: Host to Network Short，处理大小端字节序问题)
    addr.sin_port = htons(port);

    // [核心重点] 设置端口复用 SO_REUSEADDR
    // 如果不加这行，服务器关闭后重启，端口会被系统锁定几分钟，无法立即重启
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
        throw std::runtime_error("Socket setsockopt error!");
    }

    // 5. 调用系统 bind 函数
    if (::bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        throw std::runtime_error("Socket bind error!");
    }
}

void Socket::listen()
{
    // SOMAXCONN 是系统允许的最大监听队列长度 (通常是 128 或 4096)
    if (::listen(fd, SOMAXCONN) == -1) {
        throw std::runtime_error("Socket listen error!");
    }
}

int Socket::accept(sockaddr_in *addr)
{
    //1. 定义一个变量存客户端地址长度
    socklen_t len = sizeof(*addr);

    // 2. 调用系统 accept
    // fd: 监听的 socket
    // addr: 用于存客户端的 IP 和端口信息
    // len: 传入结构体大小，返回实际大小
    int clnt_fd = ::accept(fd, (struct sockaddr*)addr, &len);

    //错误检查
    if (clnt_fd == -1) {
        // 在这一步我们先简单处理，只打印不抛异常，防止程序直接崩溃
        // 后面做 Epoll 非阻塞时，这里会有特殊的处理逻辑
        // perror("accept error"); 
        return -1;
    }

    //4. 返回一个新的 socket fd，专门用于和这个客户端通信
    return clnt_fd;



}

int Socket::getFd() const
{
    return fd;
}


void Socket::setNonBlocking(int fd)
{
    int opts = fcntl(fd, F_GETFL);
    if (opts < 0) {
        throw std::runtime_error("fcntl(F_GETFL) failed");
    }
    opts = opts | O_NONBLOCK;
    if (fcntl(fd, F_SETFL, opts) < 0) {
        throw std::runtime_error("fcntl(F_SETFL) failed");
    }
}
void Socket::setNonBlocking() {
    // 获取当前文件描述符的标志位
    int opts = fcntl(fd, F_GETFL);
    if (opts < 0) {
        // 如果获取失败，抛出异常
        throw std::runtime_error("fcntl(F_GETFL) failed");
    }
    // 设置非阻塞标志位
    opts = opts | O_NONBLOCK;
    if (fcntl(fd, F_SETFL, opts) < 0) {
        // 如果设置失败，抛出异常
        throw std::runtime_error("fcntl(F_SETFL) failed");
    }
}