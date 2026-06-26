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

    ADD_FRIEND_REQ,             // 发送加好友申请 (客户端 -> 服务端)
    ADD_FRIEND_RESP,            // 发送加好友申请响应 (服务端 -> 客户端)
    FRIEND_REQUEST_NOTIFY,      // 被申请人收到好友申请实时推送 (服务端 -> 客户端)
    PROCESS_FRIEND_REQ,         // 被申请人处理加好友申请 (客户端 -> 服务端)
    PROCESS_FRIEND_RESP,        // 被申请人处理加好友申请响应 (服务端 -> 客户端)
    USER_STATUS_NOTIFY_MSG,      // 好友状态变动通知 (上线/下线)
    ADD_FRIEND_SUCCESS_NOTIFY,   // 申请人收到好友绑定成功实时通知
    SYNC_REQ,                   // 消息同步请求 (客户端 -> 服务端)
    SYNC_RESP,                  // 消息同步响应 (服务端 -> 客户端)
};
