#pragma once
#include <vector>
#include <string>
#include <algorithm> // for std::copy

class Buffer {
public:
    // [Modern C++] 使用默认参数，初始化 buffer 大小为 1024 字节
    explicit Buffer(size_t initialSize = 1024);
    ~Buffer() = default;

    // 1. 还可以读出多少字节的数据？ (写位置 - 读位置)
    size_t readableBytes() const;

    // 2. 还可以写入多少字节的数据？ (总大小 - 写位置)
    size_t writableBytes() const;

    // 3. 返回可读数据的起始指针 (像数组一样使用)
    const char* peek() const;

    // 4. 核心功能：取走数据
    // 也就是把读指针往后移动 len 个字节，表示这部分数据处理完了
    void retrieve(size_t len);

    // [新增] 取出指定长度的数据转为 string
    std::string retrieveAsString(size_t len);

    // 5. 取出所有数据转成 string (方便打印调试)
    std::string retrieveAllAsString();

    // 6. 核心功能：追加数据 (写数据进来)
    void append(const std::string& data);
    void append(const char* data, size_t len);

    // 7. 核心功能：从 Socket 读数据进 Buffer
    // 这一点至关重要，封装了 read 系统调用
    ssize_t readFd(int fd, int* saveErrno);

    void retrieveAll();

private:
    // 内部存储容器
    std::vector<char> buffer_;
    // 读位置索引
    size_t readIndex_;
    // 写位置索引
    size_t writeIndex_;

    // 获取可写位置的指针
    char* beginWrite();
    // 获取 buffer 起始地址
    char* begin();
    const char* begin() const;
    
    // 扩容函数
    void makeSpace(size_t len);
};