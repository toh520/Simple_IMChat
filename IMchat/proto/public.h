#pragma once

// client/server shared message IDs
// keep this file consistent with server-side protocol IDs
enum EnMsgType {
    LOGIN_MSG = 1,
    LOGIN_MSG_ACK,

    REG_MSG,
    REG_MSG_ACK,

    ONE_CHAT_MSG,

    HEART_BEAT_MSG,

    MSG_SEND_ACK,   // 发送端发送确认消息 (服务端 -> 发送端)
    MSG_RECV_ACK,   // 接收端接收确认消息 (接收端 -> 服务端)
};
