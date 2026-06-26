#include "server/model/UserModel.hpp"
#include "db/ConnectionPool.h" // 引入连接池
#include <iostream>

using namespace std;

// 注册用户：即向 User 表插入一条数据
bool UserModel::insert(User& user) {
    // 1. 组装 SQL 语句
    char sql[1024] = {0};
    sprintf(sql, "INSERT INTO User(name, password, state) VALUES('%s', '%s', '%s')",
            user.getName().c_str(), user.getPwd().c_str(), user.getState().c_str());

    // 2. 从连接池获取连接
    ConnectionPool* cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection(); // 智能指针，用完自动归还

    if (sp) {
        // 3. 执行 SQL
        if (sp->update(sql)) {
            // [修复] 获取插入成功的用户主键ID，赋值给 user 对象
            user.setId(sp->getInsertId()); 
            return true;
        }
    }
    return false;
}

// 查询用户
User UserModel::query(int id) {
    char sql[1024] = {0};
    sprintf(sql, "SELECT * FROM User WHERE id = %d", id);

    ConnectionPool* cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();

    if (sp) {
        MYSQL_RES* res = sp->query(sql);
        if (res != nullptr) {
            // 解析查询结果
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr) {
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setPwd(row[2]);
                user.setState(row[3]);
                mysql_free_result(res); // 记得释放资源
                return user;
            }
            mysql_free_result(res);
        }
    }
    return User(); // 返回默认的无效用户
}

bool UserModel::updateState(User user) {
    char sql[1024] = {0};
    sprintf(sql, "UPDATE User SET state = '%s' WHERE id = %d", 
            user.getState().c_str(), user.getId());

    ConnectionPool* cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();

    if (sp) {
        if (sp->update(sql)) {
            return true;
        }
    }
    return false;
}

void UserModel::resetState() {
    ConnectionPool* cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();
    if (sp) {
        // 启动自检：如果不存在，自动创建 Friend 关系表
        sp->update("CREATE TABLE IF NOT EXISTS Friend (userid INT NOT NULL, friendid INT NOT NULL, PRIMARY KEY (userid, friendid))");
        
        // 启动自检：如果不存在，自动创建 FriendRequest 申请暂存表
        sp->update("CREATE TABLE IF NOT EXISTS FriendRequest (id INT AUTO_INCREMENT PRIMARY KEY, from_id INT NOT NULL, to_id INT NOT NULL, status VARCHAR(20) DEFAULT 'pending', create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP, KEY idx_to_status (to_id, status))");

        // 启动自检：如果不存在，自动创建 MessageHistory 消息历史表
        sp->update("CREATE TABLE IF NOT EXISTS MessageHistory (msg_id BIGINT PRIMARY KEY, from_id INT NOT NULL, to_id INT NOT NULL, content VARCHAR(1000) NOT NULL, create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP, KEY idx_to_msg (to_id, msg_id))");

        // 重置所有在线状态为离线
        char sql[1024] = "UPDATE User SET state = 'offline' WHERE state = 'online'";
        sp->update(sql);
    }
}