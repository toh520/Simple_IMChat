#include "server/model/messagehistorymodel.hpp"
#include "db/ConnectionPool.h"
#include <sstream>
#include <iomanip>
#include <iostream>
#include <cstdlib>

// 辅助函数：Binary -> Hex 转换
static std::string toHex(const std::string& input) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned char c : input) {
        ss << std::setw(2) << static_cast<int>(c);
    }
    return ss.str();
}

// 辅助函数：Hex -> Binary 转换
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

// 批量插入历史消息
bool MessageHistoryModel::insertBatch(const std::vector<MessageHistory> &vec) {
    if (vec.empty()) {
        return true;
    }

    // 拼装批量插入 SQL: INSERT INTO MessageHistory(...) VALUES (...), (...), ...
    std::stringstream ss;
    ss << "INSERT INTO MessageHistory(msg_id, from_id, to_id, content) VALUES ";
    for (size_t i = 0; i < vec.size(); ++i) {
        std::string hexContent = toHex(vec[i].getContent());
        ss << "(" << vec[i].getMsgId() << ", " 
           << vec[i].getFromId() << ", " 
           << vec[i].getToId() << ", '" 
           << hexContent << "')";
        if (i < vec.size() - 1) {
            ss << ", ";
        }
    }

    std::string sql = ss.str();

    ConnectionPool* cp = ConnectionPool::getInstance();
    std::shared_ptr<Connection> sp = cp->getConnection();
    if (sp) {
        return sp->update(sql.c_str());
    }
    return false;
}

// 基于 last_sync_key 增量查询未读消息
std::vector<MessageHistory> MessageHistoryModel::query(int userid, long long last_sync_key) {
    char sql[1024] = {0};
    // 按 msg_id 升序排列，保证聊天时序正确
    sprintf(sql, "SELECT msg_id, from_id, to_id, content, create_time FROM MessageHistory WHERE to_id = %d AND msg_id > %lld ORDER BY msg_id ASC", 
            userid, last_sync_key);

    std::vector<MessageHistory> vec;
    ConnectionPool* cp = ConnectionPool::getInstance();
    std::shared_ptr<Connection> sp = cp->getConnection();

    if (sp) {
        MYSQL_RES* res = sp->query(sql);
        if (res != nullptr) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr) {
                MessageHistory msg;
                msg.setMsgId(atoll(row[0]));
                msg.setFromId(atoi(row[1]));
                msg.setToId(atoi(row[2]));
                msg.setContent(fromHex(row[3])); // 还原二进制
                if (row[4] != nullptr) {
                    msg.setCreateTime(row[4]);
                }
                vec.push_back(msg);
            }
            mysql_free_result(res);
        }
    }
    return vec;
}
