#include "net/TcpConnection.h"
#include "net/Socket.h" 
// [修正] 因为 CMake 包含了 proto 目录，所以直接引用文件名即可，不要加 proto/ 前缀
#include "msg.pb.h" 
#include "server/chatservice.hpp"
#include <iostream>
#include <unistd.h>
#include <cerrno>
#include <cstring>      // memcpy
#include <arpa/inet.h>  // ntohl

TcpConnection::TcpConnection(Epoll* epoll, int fd) 
    : epoll_(epoll),
      socket_(std::make_unique<Socket>(fd)),
      readBuffer_(),
      writeEventEnabled_(false),
      closed_(false),
      lastActiveTime_(time(nullptr)) // [Initialize] 初始化活跃时间
{
    socket_->setNonBlocking();
}

void TcpConnection::refreshAliveTime() {
    lastActiveTime_ = time(nullptr);
}

TcpConnection::~TcpConnection() {
    std::cout << "TcpConnection 资源释放，关闭 fd=" << socket_->getFd() << std::endl;
}

void TcpConnection::onRead() {
    int saveErrno = 0;

    // ET 模式下必须持续读取直到 EAGAIN/EWOULDBLOCK
    while (true) {
        ssize_t n = readBuffer_.readFd(socket_->getFd(), &saveErrno);

        if (n > 0) {
            // [新增] 只要读到数据，就更新活跃时间
            refreshAliveTime();

            // [DEBUG] 打印接收到的数据长度
            std::cout << "[DEBUG] Received " << n << " bytes. Buffer readable: " << readBuffer_.readableBytes() << std::endl;

            // [核心逻辑] 循环处理 Buffer 中的数据，解决粘包
            while (true) {
                // 第一步：检查 Buffer 里的数据够不够解析出一个包头 (4字节)
                // 包头存放整个包的长度
                if (readBuffer_.readableBytes() < 4) {
                    std::cout << "[DEBUG] Buffer data < 4 bytes, waiting..." << std::endl;
                    break; // 数据不够，等待下次读取
                }

                // 第二步：读取包头，获取包体长度
                // peek() 只是看一眼数据，不会移动读指针
                int32_t len;
                // 使用 memcpy 避免字节对齐问题
                memcpy(&len, readBuffer_.peek(), 4);
                // 网络字节序(大端) 转 主机字节序(小端)
                int32_t net_len = len; // 保存网络序以便调试
                len = ntohl(len);

                // [DEBUG] 打印头部信息
                std::cout << "[DEBUG] Header raw: " << std::hex << net_len << std::dec
                          << ", Parsed len: " << len
                          << ", Needed total: " << (4 + len)
                          << ", Readable: " << readBuffer_.readableBytes() << std::endl;

                // 安全检查：如果长度非常离谱（比如过大），可能是恶意攻击
                if (len < 4 || len > 65536) { // 最小长度是4 (只有MsgID，没有包体)
                    std::cout << "错误：非法的数据包长度 " << len << "，关闭连接" << std::endl;
                    closed_.store(true);
                    epoll_->updateChannel(socket_->getFd(), EPOLL_CTL_DEL, 0);
                    if (closeCallback_) closeCallback_(socket_->getFd());
                    return;
                }

                // 第三步：检查 Buffer 剩下的数据够不够一个完整的包体
                // 4 是包头长度，len 是包体长度
                if (readBuffer_.readableBytes() < 4 + len) {
                    break; // 数据不够完整，等待下次数据到来
                }

                // --- 数据完整，开始拆包 ---

                // 1. 先移除 4 字节的包头 (Length)
                readBuffer_.retrieve(4);

                // 2. 再解析 4 字节的 MsgID (业务类型)
                int32_t msgid;
                std::string msgidStr = readBuffer_.retrieveAsString(4);
                memcpy(&msgid, msgidStr.data(), 4);
                msgid = ntohl(msgid);

                // 3. 最后取出剩下的数据 (Protobuf 序列化后的数据)
                // 包体总长度 len - 4 (MsgID占用的长度)
                std::string data = readBuffer_.retrieveAsString(len - 4);

                std::cout << "收到数据: MsgID=" << msgid << " DataLen=" << data.size() << std::endl;

                // 4. [关键] 调用业务层进行分发处理
                // 获取对应消息id的处理器
                auto handler = ChatService::instance()->getHandler(msgid);

                // 把当前连接对象(shared_ptr)和数据传给业务层
                handler(shared_from_this(), data);
            }
            continue;
        }

        if (n == 0) {
            std::cout << "客户端断开连接 fd=" << socket_->getFd() << std::endl;
            closed_.store(true);
            epoll_->updateChannel(socket_->getFd(), EPOLL_CTL_DEL, 0);
            if (closeCallback_) {
                closeCallback_(socket_->getFd());
            }
            return;
        }

        if (saveErrno == EAGAIN || saveErrno == EWOULDBLOCK) {
            break; // ET 下已经读空了内核接收缓冲区
        }
        if (saveErrno == EINTR) {
            continue; // 被信号打断，重试读取
        }

        std::cout << "TcpConnection 读取数据出错！errno=" << saveErrno << std::endl;
        closed_.store(true);
        epoll_->updateChannel(socket_->getFd(), EPOLL_CTL_DEL, 0);
        if (closeCallback_) {
            closeCallback_(socket_->getFd());
        }
        return;
    }
}

void TcpConnection::onWrite() {
    std::lock_guard<std::mutex> lock(sendMutex_);

    if (closed_.load() || socket_->getFd() == -1) {
        return;
    }

    while (!writeBuffer_.empty()) {
        ssize_t n = ::write(socket_->getFd(), writeBuffer_.data(), writeBuffer_.size());
        if (n > 0) {
            writeBuffer_.erase(0, static_cast<size_t>(n));
            continue;
        }

        if (n == -1 && errno == EINTR) {
            continue;
        }

        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return; // 当前不可写，等待下一次 EPOLLOUT
        }

        std::cout << "TcpConnection onWrite 发送失败 errno=" << errno << std::endl;
        return;
    }

    if (writeEventEnabled_) {
        epoll_->updateChannel(socket_->getFd(), EPOLL_CTL_MOD, EPOLLIN | EPOLLET | EPOLLRDHUP);
        writeEventEnabled_ = false;
    }
}

// [新增] 按照自定义协议发送数据: 4字节长度 + 4字节MsgID + Data
void TcpConnection::send(int msgid, std::string data) {
    if (closed_.load() || socket_->getFd() == -1) return;

    // 1. 计算总长度: MsgID(4字节) + Data长度
    int32_t len = 4 + data.size();
    
    // 2. 将整数转为网络字节序 (大端)
    int32_t len_net = htonl(len);
    int32_t msgid_net = htonl(msgid);

    // 3. 组装发送缓冲区
    std::string sendBuf;
    sendBuf.resize(4 + 4 + data.size());

    // 填入长度
    memcpy(sendBuf.data(), &len_net, 4);
    // 填入MsgID
    memcpy(sendBuf.data() + 4, &msgid_net, 4);
    // 填入数据
    memcpy(sendBuf.data() + 8, data.data(), data.size());

    // 4. 发送 
    this->send(sendBuf);
}

// 发送数据的方法
void TcpConnection::send(std::string msg) {
    // [新增] 加锁保护，防止多线程同时 write 导致数据错乱
    std::lock_guard<std::mutex> lock(sendMutex_);

    if (closed_.load() || socket_->getFd() == -1) {
        return;
    }

    // 如果已经有待发送数据，直接入队，等待 EPOLLOUT flush
    if (!writeBuffer_.empty()) {
        writeBuffer_.append(msg);
        if (!writeEventEnabled_) {
            epoll_->updateChannel(socket_->getFd(), EPOLL_CTL_MOD, EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP);
            writeEventEnabled_ = true;
        }
        return;
    }

    size_t total = msg.size();
    size_t sent = 0;

    while (sent < total) {
        ssize_t n = ::write(socket_->getFd(), msg.data() + sent, total - sent);

        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }

        if (n == -1 && errno == EINTR) {
            continue;
        }

        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }

        std::cout << "TcpConnection 发送数据失败 errno=" << errno << std::endl;
        return;
    }

    // 还有没发完的数据，放入发送缓冲并开启 EPOLLOUT 事件续传
    if (sent < total) {
        writeBuffer_.append(msg.data() + sent, total - sent);
        if (!writeEventEnabled_) {
            epoll_->updateChannel(socket_->getFd(), EPOLL_CTL_MOD, EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP);
            writeEventEnabled_ = true;
        }
    }
}