#pragma once

/*
server和client的公共文件
定义消息类型，用于在传输数据时区分业务
*/
enum EnMsgType {
    LOGIN_MSG = 1, // 登录消息
    LOGIN_MSG_ACK, // 登录响应消息
    
    REG_MSG,       // 注册消息
    REG_MSG_ACK,   // 注册响应消息
    
    ONE_CHAT_MSG,  // 聊天消息

    // [新增] 
    HEART_BEAT_MSG, // 心跳消息

    MSG_SEND_ACK,   // 发送端发送确认消息 (服务端 -> 发送端)
    MSG_RECV_ACK,   // 接收端接收确认消息 (接收端 -> 服务端)
};

#include <iostream>
#include <time.h>
#include <string>

inline void LOG_INFO(const std::string& msg) {
    time_t now = time(nullptr);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    std::cout << "[" << time_str << "] [信息] " << msg << std::endl;
}

inline void LOG_DEBUG(const std::string& msg) {
    time_t now = time(nullptr);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    std::cout << "[" << time_str << "] [调试] " << msg << std::endl;
}

inline void LOG_WARN(const std::string& msg) {
    time_t now = time(nullptr);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    std::cout << "\033[1;33m[" << time_str << "] [警告] " << msg << "\033[0m" << std::endl;
}

inline void LOG_ERROR(const std::string& msg) {
    time_t now = time(nullptr);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    std::cout << "\033[1;31m[" << time_str << "] [错误] " << msg << "\033[0m" << std::endl;
}