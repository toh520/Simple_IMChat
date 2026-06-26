#pragma once
#include <vector>
#include <string>
#include <utility> // for std::pair
#include "server/model/friendrequest.hpp"

// 好友申请表数据操作类
class FriendRequestModel {
public:
    // 插入一条新的好友申请记录 (默认状态为 'pending')，成功返回生成的记录 ID，失败返回 -1
    int insert(int from_id, int to_id);

    // 更新申请记录的状态 (例如 'accepted' | 'rejected')
    bool updateStatus(int apply_id, std::string status);

    // 查询所有针对该用户的待处理好友申请记录 (带回申请人的用户名)
    std::vector<std::pair<FriendRequest, std::string>> queryPending(int to_id);
};
