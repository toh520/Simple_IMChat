#pragma once
#include <string>

// FriendRequest 申请关系实体类
class FriendRequest {
public:
    FriendRequest(int id = -1, int from_id = -1, int to_id = -1, std::string status = "pending")
        : id_(id), from_id_(from_id), to_id_(to_id), status_(status) {}

    void setId(int id) { id_ = id; }
    void setFromId(int from_id) { from_id_ = from_id; }
    void setToId(int to_id) { to_id_ = to_id; }
    void setStatus(std::string status) { status_ = status; }

    int getId() const { return id_; }
    int getFromId() const { return from_id_; }
    int getToId() const { return to_id_; }
    std::string getStatus() const { return status_; }

private:
    int id_;
    int from_id_;
    int to_id_;
    std::string status_;
};
