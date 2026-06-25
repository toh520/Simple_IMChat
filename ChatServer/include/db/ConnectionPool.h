#pragma once
#include <string>
#include <queue>
#include <mutex>
#include <iostream>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <memory>
#include <functional>

#include "db/Connection.h"

/*
实现连接池功能模块
*/
class ConnectionPool {
public:
    // 获取连接池单例对象的接口
    static ConnectionPool* getInstance();

    // 给外部提供接口，从连接池中获取一个可用的空闲连接
    // 智能指针自动管理生命周期，用完自动归还到队列，而不是 delete
    std::shared_ptr<Connection> getConnection();

private:
    // 单例模式：构造函数私有化
    ConnectionPool();
    
    // 从配置文件中加载配置项
    bool loadConfigFile(); 

    // 运行在独立的线程中，专门负责生产新连接
    void produceConnectionTask();

    // 扫描超过 maxIdleTime 时间的空闲连接，进行回收连接
    void scannerConnectionTask();

    std::string ip_;
    unsigned short port_;
    std::string username_;
    std::string password_;
    std::string dbname_;

    int initSize_;      // 连接池的初始连接量
    int maxSize_;       // 连接池的最大连接量
    int maxIdleTime_;   // 连接池最大空闲时间
    int connectionTimeout_; // 连接池获取连接的超时时间

    std::queue<Connection*> connectionQueue_; // 存储mysql连接的队列
    std::mutex queueMutex_; // 维护连接队列线程安全的互斥锁
    std::atomic_int connectionCnt_; // 记录连接所创建的connection连接的总数量 
    std::condition_variable cv_; // 设置条件变量，用于连接生产线程和消费线程的通信
};