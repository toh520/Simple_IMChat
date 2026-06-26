#ifndef MESSAGEHISTORYMODEL_H
#define MESSAGEHISTORYMODEL_H

#include "messagehistory.hpp"
#include <vector>

// 消息历史表的操作类，实现批量插入与拉取
class MessageHistoryModel {
public:
    // 批量插入历史消息 (由后台异步存盘线程调用)
    bool insertBatch(const std::vector<MessageHistory> &vec);

    // 根据 last_sync_key 增量查询用户接收到的未读消息
    std::vector<MessageHistory> query(int userid, long long last_sync_key);
};

#endif // MESSAGEHISTORYMODEL_H
