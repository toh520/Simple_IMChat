#include "db/ConnectionPool.h"
#include <fstream>
#include <iostream>

// 线程安全的懒汉单例模式
ConnectionPool* ConnectionPool::getInstance() {
    static ConnectionPool pool;
    return &pool;
}

// 构造函数：解析配置、创建初始连接、启动维护线程
ConnectionPool::ConnectionPool() {
    // 1. 加载配置
    if (!loadConfigFile()) {
        return;
    }

    // 2. 创建初始数量的连接
    for (int i = 0; i < initSize_; ++i) {
        Connection* p = new Connection();
        p->connect(ip_, port_, username_, password_, dbname_);
        p->refreshAliveTime(); // 记录一下生辰八字（起始空闲时间）
        connectionQueue_.push(p);
        connectionCnt_++;
    }

    // 3. 启动一个新的线程，作为生产者
    // C++11 thread 需要绑定成员函数，必须传 this 指针
    std::thread produce(std::bind(&ConnectionPool::produceConnectionTask, this));
    produce.detach(); // 分离线程，让它自己在后台跑

    // 4. 启动一个新的线程，作为扫描者（回收超时空闲连接）
    std::thread scanner(std::bind(&ConnectionPool::scannerConnectionTask, this));
    scanner.detach();
}

// 解析配置文件 (简单粗暴的字符串解析)
bool ConnectionPool::loadConfigFile() {
    FILE* pf = fopen("mysql.conf", "r");
    if (pf == nullptr) {
        std::cout << "mysql.conf file is not exist!" << std::endl;
        return false;
    }

    while (!feof(pf)) {
        char line[1024] = {0};
        fgets(line, 1024, pf);
        std::string str = line;
        
        // 找到 '=' 的位置，分割 key 和 value
        int idx = str.find('=', 0);
        if (idx == -1) { // 无效行
            continue;
        }

        // 截取 key 和 value，并去掉末尾的换行符
        int endidx = str.find('\n', idx);
        std::string key = str.substr(0, idx);
        std::string value = str.substr(idx + 1, endidx - idx - 1);

        if (key == "ip") ip_ = value;
        else if (key == "port") port_ = atoi(value.c_str());
        else if (key == "username") username_ = value;
        else if (key == "password") password_ = value;
        else if (key == "dbname") dbname_ = value;
        else if (key == "initSize") initSize_ = atoi(value.c_str());
        else if (key == "maxSize") maxSize_ = atoi(value.c_str());
        else if (key == "maxIdleTime") maxIdleTime_ = atoi(value.c_str());
        else if (key == "connectionTimeout") connectionTimeout_ = atoi(value.c_str());
    }
    return true;
}

// 生产者线程：负责在队列空时生产新连接
void ConnectionPool::produceConnectionTask() {
    while (true) {
        std::unique_lock<std::mutex> lock(queueMutex_);
        while (!connectionQueue_.empty()) {
            cv_.wait(lock); // 队列不空，生产者就等待（进入睡眠，释放锁）
        }

        // 被唤醒后，如果连接数量还没到上限，就继续创建
        if (connectionCnt_ < maxSize_) {
            Connection* p = new Connection();
            p->connect(ip_, port_, username_, password_, dbname_);
            p->refreshAliveTime();
            connectionQueue_.push(p);
            connectionCnt_++;
        }

        // 通知消费者（getConnection 的线程）：有连接可用了！
        cv_.notify_all();
    }
}

// 核心功能：给外部提供一个可用连接
std::shared_ptr<Connection> ConnectionPool::getConnection() {
    std::unique_lock<std::mutex> lock(queueMutex_);
    
    // 如果队列空了
    while (connectionQueue_.empty()) {
        // 如果还没到最大连接数，叫醒生产者去干活
        if (connectionCnt_ < maxSize_) {
            cv_.notify_all(); 
        }
        
        // 等待指定时间 (connectionTimeout)，如果超时了还是空，就返回失败
        if (std::cv_status::timeout == cv_.wait_for(lock, std::chrono::milliseconds(connectionTimeout_))) {
            if (connectionQueue_.empty()) {
                std::cout << "获取连接超时...获取失败!" << std::endl;
                return nullptr;
            }
        }
    }

    // 🏆 这里是整个连接池最精髓的地方！
    // 自定义 shared_ptr 的删除器。当 shared_ptr 析构时（即用户用完了连接），
    // 不会执行 delete，而是执行这段 lambda 函数：把连接还回队列。
    std::shared_ptr<Connection> sp(connectionQueue_.front(), 
        [&](Connection* pconn) {
            // 这里是在服务器应用线程中调用的，所以也要考虑线程安全
            std::unique_lock<std::mutex> lock(queueMutex_);
            pconn->refreshAliveTime(); // 刷新最后活跃时间
            connectionQueue_.push(pconn); // 归还连接
        });

    connectionQueue_.pop();
    cv_.notify_all(); // 如果有人在等空队列（生产者），通知它队列变少了，可以生产了（如果需要）

    return sp;
}

// 扫描线程：定期检查并销毁长时间不用的连接
void ConnectionPool::scannerConnectionTask() {
    while (true) {
        // 模拟定时轮询
        std::this_thread::sleep_for(std::chrono::seconds(maxIdleTime_));

        std::unique_lock<std::mutex> lock(queueMutex_);
        while (connectionCnt_ > initSize_) {
            Connection* p = connectionQueue_.front();
            // 如果队头的连接空闲时间超过了设定值，就把它销毁
            if (p->getAliveTime() >= (maxIdleTime_ * 1000)) {
                connectionQueue_.pop();
                connectionCnt_--;
                delete p; // 真正的销毁物理连接
            } else {
                break; // 队头都没超时，后面的肯定也没超时（因为是先进先出的）
            }
        }
    }
}