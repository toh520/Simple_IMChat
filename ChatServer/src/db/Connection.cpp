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
    // 缓存参数以便自动重连
    ip_ = ip;
    port_ = port;
    user_ = user;
    password_ = password;
    dbname_ = dbname;

    // 启用 MySQL C API 自动重连选项
    bool reconnect = true;
    mysql_options(conn_, MYSQL_OPT_RECONNECT, &reconnect);

    // 建立连接
    MYSQL* p = mysql_real_connect(conn_, ip.c_str(), user.c_str(), password.c_str(), dbname.c_str(), port, nullptr, 0);
    
    if (p == nullptr) {
        std::cout << "连接失败原因: " << mysql_error(conn_) << std::endl; 
    }

    return p != nullptr;
}

bool Connection::ping() {
    if (mysql_ping(conn_) == 0) {
        return true;
    }
    std::cout << "数据库连接断开，尝试自动重连..." << std::endl;
    // 释放旧句柄并重新初始化，确保重连干净
    if (conn_ != nullptr) {
        mysql_close(conn_);
    }
    conn_ = mysql_init(nullptr);
    return connect(ip_, port_, user_, password_, dbname_);
}

bool Connection::update(std::string sql) {
    // 每次更新前，通过 ping() 保证连接依然存活，否则自动拉起重连
    if (!ping()) {
        std::cout << "数据库连接已失效且重连失败，拒绝执行: " << sql << std::endl;
        return false;
    }

    // mysql_query 返回 0 表示成功
    if (mysql_query(conn_, sql.c_str())) {
        std::cout << "更新失败: " << sql << std::endl;
        std::cout << mysql_error(conn_) << std::endl; // 打印错误信息
        return false;
    }
    return true;
}

MYSQL_RES* Connection::query(std::string sql) {
    // 每次查询前，通过 ping() 保证连接依然存活，否则自动拉起重连
    if (!ping()) {
        std::cout << "数据库连接已失效且重连失败，拒绝执行: " << sql << std::endl;
        return nullptr;
    }

    // 查询操作
    if (mysql_query(conn_, sql.c_str())) {
        std::cout << "查询失败: " << sql << std::endl;
        std::cout << mysql_error(conn_) << std::endl;
        return nullptr;
    }
    return mysql_use_result(conn_);
}