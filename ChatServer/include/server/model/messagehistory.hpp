#ifndef MESSAGEHISTORY_H
#define MESSAGEHISTORY_H

#include <string>

// 消息历史实体类，与数据库 MessageHistory 表映射
class MessageHistory {
public:
    MessageHistory() : msgId(0), fromId(0), toId(0) {}
    MessageHistory(long long msgId, int fromId, int toId, const std::string &content, const std::string &createTime)
        : msgId(msgId), fromId(fromId), toId(toId), content(content), createTime(createTime) {}

    void setMsgId(long long id) { msgId = id; }
    void setFromId(int id) { fromId = id; }
    void setToId(int id) { toId = id; }
    void setContent(const std::string &str) { content = str; }
    void setCreateTime(const std::string &str) { createTime = str; }

    long long getMsgId() const { return msgId; }
    int getFromId() const { return fromId; }
    int getToId() const { return toId; }
    std::string getContent() const { return content; }
    std::string getCreateTime() const { return createTime; }

private:
    long long msgId;          // 雪花算法消息唯一ID
    int fromId;               // 发送方ID
    int toId;                 // 接收方ID
    std::string content;      // 消息内容（在数据库中存储为 Hex 编码）
    std::string createTime;   // 创建时间
};

#endif // MESSAGEHISTORY_H
