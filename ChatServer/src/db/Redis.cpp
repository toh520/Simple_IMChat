#include "db/Redis.h"
#include <iostream>
#include <vector>

Redis::Redis() : _publish_context(nullptr), _subcribe_context(nullptr) {
}

Redis::~Redis() {
    if (_publish_context != nullptr) {
        redisFree(_publish_context);
    }
    if (_subcribe_context != nullptr) {
        redisFree(_subcribe_context);
    }
}

bool Redis::connect() {
    // 负责publish发布消息的上下文连接
    _publish_context = redisConnect("redis", 6379);
    if (_publish_context == nullptr || _publish_context->err) {
        if (_publish_context != nullptr) {
            redisFree(_publish_context);
        }
        // Fallback to local connection
        _publish_context = redisConnect("127.0.0.1", 6379);
        if (_publish_context == nullptr || _publish_context->err) {
            std::cerr << "connect redis failed!" << std::endl;
            return false;
        }
    }

    // 负责subscribe订阅消息的上下文连接
    _subcribe_context = redisConnect("redis", 6379);
    if (_subcribe_context == nullptr || _subcribe_context->err) {
        if (_subcribe_context != nullptr) {
            redisFree(_subcribe_context);
        }
        // Fallback to local connection
        _subcribe_context = redisConnect("127.0.0.1", 6379);
        if (_subcribe_context == nullptr || _subcribe_context->err) {
            std::cerr << "connect redis failed!" << std::endl;
            return false;
        }
    }

    // 在单独的线程中，监听通道上的事件，有消息给业务层进行上报
    std::thread t([&]() {
        observer_channel_message();
    });
    t.detach();

    std::cout << "connect redis-server success!" << std::endl;
    return true;
}

// 向redis指定的通道channel发布消息
bool Redis::publish(int channel, std::string message) {
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "PUBLISH %d %s", channel, message.c_str());
    if (nullptr == reply) {
        std::cerr << "publish command failed!" << std::endl;
        return false;
    }
    freeReplyObject(reply);
    return true;
}

// 向redis指定的通道subscribe订阅消息
bool Redis::subscribe(int channel) {
    // SUBSCRIBE命令本身会造成线程阻塞等待通道里面发生消息，这里只做订阅通道，不接收通道消息
    // 通道消息的接收专门在observer_channel_message函数中的独立线程中进行
    // 只负责发送命令，不阻塞接收redis server响应消息，否则和notifyMsg线程抢占响应资源
    if (REDIS_ERR == redisAppendCommand(_subcribe_context, "SUBSCRIBE %d", channel)) {
        std::cerr << "subscribe command failed!" << std::endl;
        return false;
    }
    // redisBufferWrite可以循环发送缓冲区，直到缓冲区数据发送完毕（done被置为1）
    int done = 0;
    while (!done) {
        if (REDIS_ERR == redisBufferWrite(_subcribe_context, &done)) {
            std::cerr << "subscribe command failed!" << std::endl;
            return false;
        }
    }
    return true;
}

// 向redis指定的通道unsubscribe取消订阅消息
bool Redis::unsubscribe(int channel) {
    if (REDIS_ERR == redisAppendCommand(_subcribe_context, "UNSUBSCRIBE %d", channel)) {
        std::cerr << "unsubscribe command failed!" << std::endl;
        return false;
    }
    int done = 0;
    while (!done) {
        if (REDIS_ERR == redisBufferWrite(_subcribe_context, &done)) {
            std::cerr << "unsubscribe command failed!" << std::endl;
            return false;
        }
    }
    return true;
}

// 在独立线程中接收订阅通道中的消息
void Redis::observer_channel_message() {
    redisReply *reply = nullptr;
    while (REDIS_OK == redisGetReply(_subcribe_context, (void **)&reply)) {
        // 订阅收到的消息是一个带三元素的数组
        if (reply != nullptr && reply->element != nullptr && reply->element[2] != nullptr && reply->element[2]->str != nullptr) {
            // 给业务层上报通道上发生的消息
            _notify_message_handler(atoi(reply->element[1]->str), reply->element[2]->str);
        }

        freeReplyObject(reply);
    }
    std::cerr << ">>>>>>>>>>>>> observer_channel_message quit <<<<<<<<<<<<<" << std::endl;
}

void Redis::init_notify_handler(std::function<void(int, std::string)> fn) {
    this->_notify_message_handler = fn;
}

bool Redis::hset(const std::string &key, const std::string &field, const std::string &value) {
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "HSET %s %s %s", key.c_str(), field.c_str(), value.c_str());
    if (nullptr == reply) {
        std::cerr << "hset command failed!" << std::endl;
        return false;
    }
    freeReplyObject(reply);
    return true;
}

std::string Redis::hget(const std::string &key, const std::string &field) {
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "HGET %s %s", key.c_str(), field.c_str());
    if (nullptr == reply) {
        std::cerr << "hget command failed!" << std::endl;
        return "";
    }
    std::string val = "";
    if (reply->type == REDIS_REPLY_STRING && reply->str != nullptr) {
        val = reply->str;
    }
    freeReplyObject(reply);
    return val;
}

bool Redis::hdel(const std::string &key, const std::string &field) {
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "HDEL %s %s", key.c_str(), field.c_str());
    if (nullptr == reply) {
        std::cerr << "hdel command failed!" << std::endl;
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool Redis::cleanNodeRoutes(const std::string &key, const std::string &nodeVal) {
    // 1. 获取 Hash 表中的所有键值对
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "HGETALL %s", key.c_str());
    if (nullptr == reply) {
        std::cerr << "hgetall failed in cleanNodeRoutes!" << std::endl;
        return false;
    }
    
    if (reply->type == REDIS_REPLY_ARRAY) {
        std::vector<std::string> fields_to_del;
        // HGETALL 返回的元素按 [field1, value1, field2, value2...] 排列
        for (size_t i = 0; i < reply->elements; i += 2) {
            if (reply->element[i] && reply->element[i+1] && reply->element[i+1]->str) {
                std::string field = reply->element[i]->str;
                std::string val = reply->element[i+1]->str;
                if (val == nodeVal) {
                    fields_to_del.push_back(field);
                }
            }
        }
        freeReplyObject(reply);

        // 2. 批量删除属于当前宕机节点的字段
        for (const auto &f : fields_to_del) {
            redisReply *del_reply = (redisReply *)redisCommand(_publish_context, "HDEL %s %s", key.c_str(), f.c_str());
            if (del_reply) {
                freeReplyObject(del_reply);
            }
        }
        return true;
    }
    freeReplyObject(reply);
    return false;
}

bool Redis::sadd(const std::string &key, const std::string &member) {
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "SADD %s %s", key.c_str(), member.c_str());
    if (nullptr == reply) {
        std::cerr << "sadd command failed!" << std::endl;
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool Redis::sismember(const std::string &key, const std::string &member) {
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "SISMEMBER %s %s", key.c_str(), member.c_str());
    if (nullptr == reply) {
        std::cerr << "sismember command failed!" << std::endl;
        return false;
    }
    bool is_member = false;
    if (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1) {
        is_member = true;
    }
    freeReplyObject(reply);
    return is_member;
}

bool Redis::zadd(const std::string &key, long long score, const std::string &member) {
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "ZADD %s %lld %b", key.c_str(), score, member.data(), member.size());
    if (nullptr == reply) {
        std::cerr << "zadd command failed!" << std::endl;
        return false;
    }
    freeReplyObject(reply);
    return true;
}

bool Redis::zremrangebyrank(const std::string &key, int start, int stop) {
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "ZREMRANGEBYRANK %s %d %d", key.c_str(), start, stop);
    if (nullptr == reply) {
        std::cerr << "zremrangebyrank command failed!" << std::endl;
        return false;
    }
    freeReplyObject(reply);
    return true;
}

long long Redis::zminscore(const std::string &key) {
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "ZRANGE %s 0 0 WITHSCORES", key.c_str());
    if (nullptr == reply) {
        std::cerr << "zminscore command failed!" << std::endl;
        return -1;
    }
    
    long long min_score = -1;
    if (reply->type == REDIS_REPLY_ARRAY && reply->elements >= 2) {
        if (reply->element[1] && reply->element[1]->str) {
            min_score = atoll(reply->element[1]->str);
        }
    }
    freeReplyObject(reply);
    return min_score;
}

std::vector<std::string> Redis::zrangebyscore(const std::string &key, long long minScore) {
    std::string minScoreStr = "(" + std::to_string(minScore);
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "ZRANGEBYSCORE %s %s +inf", key.c_str(), minScoreStr.c_str());
    
    std::vector<std::string> vec;
    if (nullptr == reply) {
        std::cerr << "zrangebyscore command failed!" << std::endl;
        return vec;
    }

    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            if (reply->element[i] && reply->element[i]->str) {
                vec.push_back(std::string(reply->element[i]->str, reply->element[i]->len));
            }
        }
    }
    freeReplyObject(reply);
    return vec;
}