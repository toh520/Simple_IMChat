#include "server/model/friendrequestmodel.hpp"
#include "db/ConnectionPool.h"

using namespace std;

// 插入申请，返回产生的自增ID，已存在 pending 申请则直接返回已有的 ID
int FriendRequestModel::insert(int from_id, int to_id) {
    char checkSql[1024] = {0};
    sprintf(checkSql, "SELECT id FROM FriendRequest WHERE from_id = %d AND to_id = %d AND status = 'pending'", from_id, to_id);
    
    ConnectionPool* cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();
    if (sp) {
        MYSQL_RES* res = sp->query(checkSql);
        if (res != nullptr) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr) {
                int existId = atoi(row[0]);
                mysql_free_result(res);
                return existId; // 避免重复插入
            }
            mysql_free_result(res);
        }

        char sql[1024] = {0};
        sprintf(sql, "INSERT INTO FriendRequest(from_id, to_id, status) VALUES(%d, %d, 'pending')", from_id, to_id);
        if (sp->update(sql)) {
            return sp->getInsertId();
        }
    }
    return -1;
}

// 更新好友申请状态
bool FriendRequestModel::updateStatus(int apply_id, string status) {
    char sql[1024] = {0};
    sprintf(sql, "UPDATE FriendRequest SET status = '%s' WHERE id = %d", status.c_str(), apply_id);

    ConnectionPool* cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();
    if (sp) {
        return sp->update(sql);
    }
    return false;
}

// 批量抓取被申请人 pending 状态的申请记录并带回申请方昵称
vector<pair<FriendRequest, string>> FriendRequestModel::queryPending(int to_id) {
    char sql[1024] = {0};
    sprintf(sql, "SELECT r.id, r.from_id, r.to_id, r.status, u.name FROM FriendRequest r INNER JOIN User u ON r.from_id = u.id WHERE r.to_id = %d AND r.status = 'pending'", to_id);

    vector<pair<FriendRequest, string>> vec;
    ConnectionPool* cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();
    if (sp) {
        MYSQL_RES* res = sp->query(sql);
        if (res != nullptr) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr) {
                FriendRequest req;
                req.setId(atoi(row[0]));
                req.setFromId(atoi(row[1]));
                req.setToId(atoi(row[2]));
                req.setStatus(row[3]);
                string fromName = row[4];
                vec.push_back({req, fromName});
            }
            mysql_free_result(res);
        }
    }
    return vec;
}
