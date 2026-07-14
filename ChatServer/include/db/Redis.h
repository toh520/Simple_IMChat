#ifndef REDIS_H
#define REDIS_H

#include <hiredis/hiredis.h>
#include <thread>
#include <functional>
#include <string>
#include <vector>
#include <mutex>

class Redis {
public:
    Redis();
    ~Redis();

    // 连接redis
    bool connect();

    // 向redis指定的通道channel发布消息
    bool publish(int channel, std::string message);

    // 向redis指定的通道subscribe订阅消息
    bool subscribe(int channel);

    // 向redis指定的通道unsubscribe取消订阅消息
    bool unsubscribe(int channel);

    // 在独立线程中接收订阅通道中的消息
    void observer_channel_message();

    // 初始化向业务层上报通道消息的回调对象
    void init_notify_handler(std::function<void(int, std::string)> fn);

    // 写入 Hash 键值对
    bool hset(const std::string &key, const std::string &field, const std::string &value);
    
    // 读取 Hash 字段值
    std::string hget(const std::string &key, const std::string &field);
    
    // 删除 Hash 字段
    bool hdel(const std::string &key, const std::string &field);

    // 清理指定 Redis Key 中，所有 Value 匹配 nodeVal 的字段 (用于宕机自清理)
    bool cleanNodeRoutes(const std::string &key, const std::string &nodeVal);

    // 向 Redis 集合中添加成员
    bool sadd(const std::string &key, const std::string &member);

    // 判断成员是否在 Redis 集合中
    bool sismember(const std::string &key, const std::string &member);

    // --- 新增 ZSet 时间线缓存操作 ---
    bool zadd(const std::string &key, long long score, const std::string &member);
    bool zremrangebyrank(const std::string &key, int start, int stop);
    long long zminscore(const std::string &key);
    std::vector<std::string> zrangebyscore(const std::string &key, long long minScore);

private:
    // hiredis同步上下文对象，负责reply
    redisContext *_publish_context;

    // hiredis同步上下文对象，负责subscribe
    redisContext *_subcribe_context;

    // 保护 _publish_context 的互斥锁，确保并发指令调用安全
    std::mutex _mutex;

    // 回调操作，拿到订阅的消息后，给service层上报
    std::function<void(int, std::string)> _notify_message_handler;
};

#endif