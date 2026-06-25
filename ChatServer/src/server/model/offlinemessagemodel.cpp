#include "server/model/offlinemessagemodel.hpp"
#include "db/ConnectionPool.h"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

// 辅助函数：Binary -> Hex
static std::string toHex(const std::string& input) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned char c : input) {
        ss << std::setw(2) << static_cast<int>(c);
    }
    return ss.str();
}

// 辅助函数：Hex -> Binary
static std::string fromHex(const std::string& input) {
    if (input.length() % 2 != 0) return "";
    std::string output;
    output.reserve(input.length() / 2);
    for (size_t i = 0; i < input.length(); i += 2) {
        std::string byteString = input.substr(i, 2);
        char byte = (char)strtol(byteString.c_str(), nullptr, 16);
        output.push_back(byte);
    }
    return output;
}

void OfflineMsgModel::insert(int userid, std::string msg) {
    // 1. 将二进制 msg 转为 Hex 字符串，防止特殊字符破坏 SQL 且不受 \0 影响
    std::string hexMsg = toHex(msg);

    // 2. 组装 SQL
    char sql[10240] = {0}; // 扩大缓冲区，防止 overflow
    sprintf(sql, "INSERT INTO OfflineMessage(userid, message) VALUES(%d, '%s')", 
            userid, hexMsg.c_str());

    ConnectionPool* cp = ConnectionPool::getInstance();
    std::shared_ptr<Connection> sp = cp->getConnection();
    if (sp) {
        sp->update(sql);
    }
}

void OfflineMsgModel::remove(int userid) {
    char sql[1024] = {0};
    sprintf(sql, "DELETE FROM OfflineMessage WHERE userid=%d", userid);

    ConnectionPool* cp = ConnectionPool::getInstance();
    std::shared_ptr<Connection> sp = cp->getConnection();
    if (sp) {
        sp->update(sql);
    }
}

std::vector<std::string> OfflineMsgModel::query(int userid) {
    char sql[1024] = {0};
    sprintf(sql, "SELECT message FROM OfflineMessage WHERE userid = %d", userid);

    std::vector<std::string> vec;
    ConnectionPool* cp = ConnectionPool::getInstance();
    std::shared_ptr<Connection> sp = cp->getConnection();

    if (sp) {
        MYSQL_RES* res = sp->query(sql);
        if (res != nullptr) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr) {
                 // 数据库存的是 Hex，需要转回 Binary
                std::string hexMsg = row[0];
                vec.push_back(fromHex(hexMsg));
            }
            mysql_free_result(res);
        }
    }
    return vec;
}