#ifndef CHATWIDGET_H
#define CHATWIDGET_H

#include <QWidget>
#include <QMap>
#include <QList>
#include <QDateTime>

namespace Ui {
class ChatWidget;
}

// 聊天消息实体结构
struct ChatMessage {
    int fromId;          // 发送者 UID
    int toId;            // 接收者 UID
    QString content;     // 消息内容
    QDateTime timestamp; // 发送时间戳
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
    // 处理收到单聊消息的网络回调信号
    void onOneChatReceived(int fromId, int toId, const QString &msg);

    // 界面按钮与列表的槽函数 (配合 Qt 自带的自动关联机制)
    void on_btn_add_session_clicked();
    void on_btn_send_clicked();
    void on_list_sessions_currentRowChanged(int currentRow);

private:
    // 将指定 UID 添加到左侧会话列表中
    void addSession(int uid);
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
};

#endif // CHATWIDGET_H
