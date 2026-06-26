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