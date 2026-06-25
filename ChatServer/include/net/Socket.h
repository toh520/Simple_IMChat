#pragma once

#include <arpa/inet.h>  // inet_addr// 需要用到 uint16_t 等类型

class Socket
{
private:
    int fd;// 文件描述符 (File Descriptor)，在 Linux 里就是一个整数

public:
    Socket();
    // [新增] 允许传入一个已有的 fd 来创建对象
    // explicit 关键字防止隐式类型转换，是 C++ 规范写法
    explicit Socket(int fd);
    ~Socket();

    void bind(const char* ip, uint16_t port); // 绑定 IP 和端口
    void listen();                            // 开始监听
    int accept(struct sockaddr_in* addr);                            // 接受连接 

    void setNonBlocking(); // 设置为非阻塞模式
    static void setNonBlocking(int fd);

    int getFd() const;
};

