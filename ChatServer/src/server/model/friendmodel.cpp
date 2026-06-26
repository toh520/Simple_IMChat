#include "server/model/friendmodel.hpp"
#include "db/ConnectionPool.h"

using namespace std;

// 添加好友关系 (双向写入)
bool FriendModel::insert(int userid, int friendid) {
    char sql1[1024] = {0};
    sprintf(sql1, "INSERT INTO Friend(userid, friendid) VALUES(%d, %d)", userid, friendid);
    char sql2[1024] = {0};
    sprintf(sql2, "INSERT INTO Friend(userid, friendid) VALUES(%d, %d)", friendid, userid);

    ConnectionPool* cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();
    if (sp) {
        // 双写落盘
        sp->update(sql1);
        sp->update(sql2);
        return true;
    }
    return false;
}

// 主动查询用户的好友列表 (联查 User 表以直接拿回好友的昵称和状态)
vector<User> FriendModel::query(int userid) {
    char sql[1024] = {0};
    sprintf(sql, "SELECT u.id, u.name, u.state FROM User u INNER JOIN Friend f ON u.id = f.friendid WHERE f.userid = %d", userid);

    vector<User> vec;
    ConnectionPool* cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();
    if (sp) {
        MYSQL_RES* res = sp->query(sql);
        if (res != nullptr) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr) {
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setState(row[2]);
                vec.push_back(user);
            }
            mysql_free_result(res);
        }
    }
    return vec;
}
