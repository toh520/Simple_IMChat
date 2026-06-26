#pragma once
#include <string>

// Friend 关系实体类
class Friend {
public:
    Friend(int userid = -1, int friendid = -1) : userid_(userid), friendid_(friendid) {}

    void setUserId(int userid) { userid_ = userid; }
    void setFriendId(int friendid) { friendid_ = friendid; }

    int getUserId() const { return userid_; }
    int getFriendId() const { return friendid_; }

private:
    int userid_;
    int friendid_;
};
