#pragma once
#include <mysql/mysql.h>
#include <string>
#include <ctime>

class Connection {
public:
    Connection();
    ~Connection();

    // 连接数据库
    bool connect(std::string ip, unsigned short port, std::string user, std::string password, std::string dbname);

    // 执行更新操作 (Insert, Update, Delete)
    bool update(std::string sql);

    // [新增] 获取上一次插入操作生成的自增主键ID
    int getInsertId() const { return mysql_insert_id(conn_); }

    // 执行查询操作 (Select)
    MYSQL_RES* query(std::string sql);

    // 刷新一下连接的起始空闲时间点
    void refreshAliveTime() { alivetime_ = clock(); }
    // 返回存活的时间
    clock_t getAliveTime() const { return clock() - alivetime_; }

private:
    MYSQL* conn_; // MySQL 原生句柄
    clock_t alivetime_; // 记录进入空闲状态后的起始时间
};