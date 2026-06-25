#include "net/Buffer.h"
#include <sys/uio.h> // readv 需要这个头文件
#include <unistd.h>  // read, close
#include <errno.h>
#include "net/Buffer.h"

Buffer::Buffer(size_t initialSize)
    : buffer_(initialSize),
      readIndex_(0),
      writeIndex_(0) 
{
}

size_t Buffer::readableBytes() const {
    return writeIndex_ - readIndex_;
}

size_t Buffer::writableBytes() const {
    return buffer_.size() - writeIndex_;
}

const char* Buffer::peek() const {
    return begin() + readIndex_;
}

void Buffer::retrieve(size_t len) {
    if (len < readableBytes()) {
        // 只取走了一部分，读指针后移
        readIndex_ += len;
    } else {
        // 全部取走了，重置指针，复用空间
        retrieveAll();
    }
}

std::string Buffer::retrieveAsString(size_t len) {
    // 确保要取的长度不超过现有数据长度，防止越界
    if (len > readableBytes()) {
        len = readableBytes(); 
    }
    
    // 1. 构造字符串：从当前读位置开始，拷贝 len 个字节
    std::string result(peek(), len);
    
    // 2. 移动读指针：表示这 len 个字节已经处理完了
    retrieve(len);
    
    return result;
}

// 辅助私有函数：重置
void Buffer::retrieveAll() {
    readIndex_ = 0;
    writeIndex_ = 0;
}

std::string Buffer::retrieveAllAsString() {
    std::string str(peek(), readableBytes());
    retrieveAll();
    return str;
}

void Buffer::append(const std::string& data) {
    append(data.c_str(), data.size());
}

void Buffer::append(const char* data, size_t len) {
    // 如果空间不够，先扩容
    if (writableBytes() < len) {
        makeSpace(len);
    }
    // [Modern C++] 使用 std::copy 替代 memcpy
    std::copy(data, data + len, beginWrite());
    writeIndex_ += len;
}

ssize_t Buffer::readFd(int fd, int* saveErrno) {
    // 栈上的临时空间，64K，通常足够读完 Socket 缓冲区
    char extrabuf[65536]; 
    
    struct iovec vec[2];
    const size_t writable = writableBytes();
    
    // 第一块缓冲区：Buffer 内部剩余的可写空间
    vec[0].iov_base = begin() + writeIndex_;
    vec[0].iov_len = writable;
    
    // 第二块缓冲区：栈上的临时空间
    vec[1].iov_base = extrabuf;
    vec[1].iov_len = sizeof(extrabuf);
    
    // 如果 Buffer 够写，就只用一块；不够就借用栈空间
    const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
    
    // readv 可以分散读，自动填满 vec[0] 后填 vec[1]
    const ssize_t n = ::readv(fd, vec, iovcnt);
    
    if (n < 0) {
        *saveErrno = errno;
    } else if (static_cast<size_t>(n) <= writable) {
        // 还没填满 Buffer，只需移动写指针
        writeIndex_ += n;
    } else {
        // Buffer 填满了，剩下的在 extrabuf 里
        writeIndex_ = buffer_.size();
        // 把 extrabuf 里的数据追加到 Buffer 后面 (会自动扩容)
        append(extrabuf, n - writable);
    }
    
    return n;
}

// --- 私有辅助函数 ---

char* Buffer::begin() { 
    return &*buffer_.begin(); 
}
const char* Buffer::begin() const {
     return &*buffer_.begin(); 
}

char* Buffer::beginWrite() {
     return begin() + writeIndex_; 
}

void Buffer::makeSpace(size_t len) {
    // 如果总空闲空间都不够，只能扩容
    if (writableBytes() + readIndex_ < len) {
        buffer_.resize(writeIndex_ + len);
    } else {
        // 如果前面有空闲（因为读走了一部分），把数据挪到最前面去，腾出后面
        // 这是一个空间换时间的优化，避免 resize 分配内存
        size_t readable = readableBytes();
        std::copy(begin() + readIndex_, begin() + writeIndex_, begin());
        readIndex_ = 0;
        writeIndex_ = readIndex_ + readable;
    }
}