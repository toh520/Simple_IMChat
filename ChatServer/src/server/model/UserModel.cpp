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
    char sql[1024] = "UPDATE User SET state = 'offline' WHERE state = 'online'";
    ConnectionPool* cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();
    if (sp) {
        sp->update(sql);
    }
}