#include "db/Connection.h"
#include <iostream>

Connection::Connection() {
    // 初始化数据库句柄
    conn_ = mysql_init(nullptr);
}

Connection::~Connection() {
    // 释放数据库连接资源
    if (conn_ != nullptr) {
        mysql_close(conn_);
    }
}

bool Connection::connect(std::string ip, unsigned short port, std::string user, std::string password, std::string dbname) {
    // 建立连接
    MYSQL* p = mysql_real_connect(conn_, ip.c_str(), user.c_str(), password.c_str(), dbname.c_str(), port, nullptr, 0);
    
    // --- 修改开始: 加上错误打印 ---
    if (p == nullptr) {
        // 这行代码会打印具体的错误原因，比如 "Access denied" 或 "Can't connect to server"
        std::cout << "连接失败原因: " << mysql_error(conn_) << std::endl; 
    }
    // --- 修改结束 ---

    return p != nullptr;
}

bool Connection::update(std::string sql) {
    // mysql_query 返回 0 表示成功
    if (mysql_query(conn_, sql.c_str())) {
        std::cout << "更新失败: " << sql << std::endl;
        std::cout << mysql_error(conn_) << std::endl; // 打印错误信息
        return false;
    }
    return true;
}

MYSQL_RES* Connection::query(std::string sql) {
    // 查询操作
    if (mysql_query(conn_, sql.c_str())) {
        std::cout << "查询失败: " << sql << std::endl;
        std::cout << mysql_error(conn_) << std::endl;
        return nullptr;
    }
    return mysql_use_result(conn_);
}