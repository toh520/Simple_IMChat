#ifndef IMCLIENT_H
#define IMCLIENT_H

#include <QObject>
#include <QByteArray>
#include <QQueue>
#include <QTcpSocket>
#include <QMap>
#include <QList>
#include <QDateTime>

struct PendingSendMsg {
    qint64 msgId;
    int toId;
    std::string payload; // OneChatRequest 序列化包体
    qint64 sendTime;     // 发送时间戳（毫秒）
    int retryCount;      // 重试次数
};

class ImClient : public QObject
{
    Q_OBJECT

public:
    static ImClient &instance();

    void login(const QString &userId, const QString &password);
    void reg(const QString &username, const QString &password);
    // 发送单聊消息的接口
    void sendOneChat(int toId, const QString &msg, qint64 msgId);

    // 获取当前登录用户自身的 ID
    int getMyUid() const { return myUid_; }

    bool isConnected() const;

signals:
    void loginResult(bool success, int uid, const QString &msg);
    void regResult(bool success, int uid, const QString &msg);
    // 单聊消息接收信号 (包含 msgId)
    void oneChatReceived(int fromId, int toId, const QString &msg, qint64 msgId);
    // 发送确认接收信号
    void oneChatSendAck(qint64 msgId, bool success, const QString &errMsg);
    void networkError(const QString &msg);

private:
    explicit ImClient(QObject *parent = nullptr);
    ImClient(const ImClient &) = delete;
    ImClient &operator=(const ImClient &) = delete;

    void ensureConnected();
    void sendPacket(int msgId, const std::string &payload);
    void flushPendingPackets();
    void handleMessage(int msgId, const QByteArray &payload);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    // 定时发送心跳的槽函数
    void sendHeartBeat();
    // 定时检查待确认重发队列的槽函数
    void checkRetransmit();

private:
    QTcpSocket socket_;
    QByteArray readBuffer_;
    QQueue<QByteArray> pendingPackets_;
    
    // 心跳定时器指针，用于保持长连接活性
    class QTimer *heartbeatTimer_{nullptr};
    
    // 重发定时器指针
    class QTimer *retransmitTimer_{nullptr};

    // 发送待确认确认队列
    QMap<qint64, PendingSendMsg> pendingSendAckMap_;

    // 接收消息去重滑动窗口
    QList<qint64> recvMsgWindow_;

    // 当前登录成功的用户 UID
    int myUid_{-1};

    static constexpr const char *kServerHost = "127.0.0.1";
    static constexpr quint16 kServerPort = 8888;
    static constexpr quint32 kMaxPacketSize = 64 * 1024;
};

#endif // IMCLIENT_H
