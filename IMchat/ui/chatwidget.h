#ifndef CHATWIDGET_H
#define CHATWIDGET_H

#include <QWidget>
#include <QMap>
#include <QList>
#include <QSet>
#include <QDateTime>

namespace Ui {
class ChatWidget;
}

// 消息状态枚举
enum MessageStatus {
    MSG_STATUS_SENDING, // 发送中
    MSG_STATUS_SUCCESS, // 发送成功
    MSG_STATUS_FAILED   // 发送失败
};

// 聊天消息实体结构
struct ChatMessage {
    qint64 msgId;        // 消息ID
    int fromId;          // 发送者 UID
    int toId;            // 接收者 UID
    QString content;     // 消息内容
    QDateTime timestamp; // 发送时间戳
    MessageStatus status; // 消息状态
};

class ChatWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChatWidget(QWidget *parent = nullptr);
    ~ChatWidget();

protected:
    // 事件过滤器，用于捕获输入框的 Enter / Ctrl+Enter 按键事件以实现快捷发送
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    // 处理收到单聊消息的网络回调信号 (包含 msgId)
    void onOneChatReceived(int fromId, int toId, const QString &msg, qint64 msgId);

    // 处理收到消息发送确认 of 信号
    void onOneChatSendAck(qint64 msgId, bool success, const QString &errMsg);

    // 界面按钮与列表的槽函数 (配合 Qt 自带的自动关联机制)
    void on_btn_add_session_clicked();
    void on_btn_send_clicked();
    void on_btn_logout_clicked();
    void on_list_sessions_currentRowChanged(int currentRow);

    // [新增] 社交相关槽函数
    void onSocialDataLoaded(const QList<QVariantMap> &friends, const QList<QVariantMap> &applies);
    void onAddFriendResult(bool success, const QString &msg);
    void onFriendRequestReceived(int applyId, int fromId, const QString &fromName);
    void onProcessFriendResult(bool success, int applyId, bool accept, int friendId, const QString &friendName, const QString &state);
    void onFriendStatusChanged(int uid, const QString &state);
    void onFriendBindSuccess(int friendId, const QString &friendName, const QString &state);

private:
    // [新增] 刷新好友与会话列表
    void refreshFriendList();
    // 加载并渲染与指定 UID 的所有历史聊天消息
    void loadChatHistory(int uid);
    // 辅助函数：格式化并渲染一条聊天消息到 list_messages
    void appendMessageToView(const ChatMessage &msg);

private:
    Ui::ChatWidget *ui;

    // 当前正在与之聊天的对方 UID (-1 表示未选择)
    int activeUserId_{-1};

    // 内存中的聊天历史记录映射表，Key 为对方的 UID
    QMap<int, QList<ChatMessage>> chatHistory_;

    // [新增] 社交状态与申请缓存数据
    QList<QVariantMap> pendingApplies_;       // 未处理好友申请列表
    QMap<int, QVariantMap> friendsMap_;       // 好友信息映射表 (Key: friendId, Value: { "name": QString, "state": QString })
    QSet<int> unreadUsers_;                  // 存在未读消息的用户的 UID 集合
};

#endif // CHATWIDGET_H
