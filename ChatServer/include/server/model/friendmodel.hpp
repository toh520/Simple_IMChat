#pragma once
#include <vector>
#include "server/model/User.hpp"

// Friend 表的数据操作类
class FriendModel {
public:
    // 添加好友关系 (双向写入)
    bool insert(int userid, int friendid);

    // 主动查询用户的好友列表 (联合查询 User 表以获取好友的名字和当前在线状态)
    std::vector<User> query(int userid);
};
