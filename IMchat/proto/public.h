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
};
