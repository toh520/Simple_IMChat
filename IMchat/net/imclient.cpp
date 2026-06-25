#include "imclient.h"

#include "msg.pb.h"
#include "public.h"

#include <QAbstractSocket>
#include <QtEndian>
#include <QTimer> // 引入定时器头文件

#include <cstring>

ImClient &ImClient::instance()
{
    static ImClient client;
    return client;
}

ImClient::ImClient(QObject *parent)
    : QObject(parent)
{
    // 初始化心跳定时器
    heartbeatTimer_ = new QTimer(this);
    connect(heartbeatTimer_, &QTimer::timeout, this, &ImClient::sendHeartBeat);

    connect(&socket_, &QTcpSocket::connected, this, &ImClient::onConnected);
    connect(&socket_, &QTcpSocket::disconnected, this, &ImClient::onDisconnected);
    connect(&socket_, &QTcpSocket::readyRead, this, &ImClient::onReadyRead);
    connect(&socket_, &QTcpSocket::errorOccurred, this, &ImClient::onSocketError);
}

bool ImClient::isConnected() const
{
    return socket_.state() == QAbstractSocket::ConnectedState;
}

void ImClient::login(const QString &userId, const QString &password)
{
    chat::LoginRequest req;
    req.set_username(userId.toStdString());
    req.set_password(password.toStdString());

    std::string payload;
    req.SerializeToString(&payload);
    sendPacket(LOGIN_MSG, payload);
}

void ImClient::reg(const QString &username, const QString &password)
{
    chat::RegRequest req;
    req.set_username(username.toStdString());
    req.set_password(password.toStdString());

    std::string payload;
    req.SerializeToString(&payload);
    sendPacket(REG_MSG, payload);
}

void ImClient::sendOneChat(int toId, const QString &msg)
{
    chat::OneChatRequest req;
    req.set_from_id(myUid_); // 设置发送者 ID
    req.set_to_id(toId);     // 设置接收者 ID
    req.set_msg(msg.toStdString()); // 设置消息内容

    std::string payload;
    req.SerializeToString(&payload);
    sendPacket(ONE_CHAT_MSG, payload); // 按照协议发送 ONE_CHAT_MSG 类型的消息
}

void ImClient::ensureConnected()
{
    if (socket_.state() == QAbstractSocket::ConnectedState ||
        socket_.state() == QAbstractSocket::ConnectingState) {
        return;
    }

    socket_.connectToHost(QString::fromUtf8(kServerHost), kServerPort);
}

void ImClient::sendPacket(int msgId, const std::string &payload)
{
    QByteArray packet;
    packet.resize(static_cast<int>(8 + payload.size()));

    const quint32 bodyLen = static_cast<quint32>(4 + payload.size());
    const quint32 msgIdU32 = static_cast<quint32>(msgId);

    qToBigEndian(bodyLen, reinterpret_cast<uchar *>(packet.data()));
    qToBigEndian(msgIdU32, reinterpret_cast<uchar *>(packet.data() + 4));

    if (!payload.empty()) {
        memcpy(packet.data() + 8, payload.data(), payload.size());
    }

    if (isConnected()) {
        socket_.write(packet);
        socket_.flush();
        return;
    }

    pendingPackets_.enqueue(packet);
    ensureConnected();
}

void ImClient::flushPendingPackets()
{
    while (!pendingPackets_.isEmpty() && isConnected()) {
        socket_.write(pendingPackets_.dequeue());
    }
    socket_.flush();
}

void ImClient::handleMessage(int msgId, const QByteArray &payload)
{
    const std::string data(payload.constData(), static_cast<size_t>(payload.size()));

    if (msgId == LOGIN_MSG_ACK) {
        chat::LoginResponse resp;
        if (!resp.ParseFromString(data)) {
            emit networkError(QString::fromUtf8("登录响应解析失败"));
            return;
        }
        
        // 登录成功时，在客户端保存自身的 UID
        if (resp.success()) {
            myUid_ = resp.uid();
        }

        emit loginResult(resp.success(), resp.uid(), QString::fromStdString(resp.msg()));
        return;
    }

    if (msgId == REG_MSG_ACK) {
        chat::RegResponse resp;
        if (!resp.ParseFromString(data)) {
            emit networkError(QString::fromUtf8("注册响应解析失败"));
            return;
        }
        emit regResult(resp.success(), resp.uid(), QString::fromStdString(resp.msg()));
        return;
    }

    // 处理服务端推送过来的一对一单聊消息
    if (msgId == ONE_CHAT_MSG) {
        chat::OneChatRequest req;
        if (!req.ParseFromString(data)) {
            emit networkError(QString::fromUtf8("收到单聊消息，但反序列化失败"));
            return;
        }
        // 触发单聊消息接收信号，通知 UI 层
        emit oneChatReceived(req.from_id(), req.to_id(), QString::fromStdString(req.msg()));
        return;
    }
}

void ImClient::onConnected()
{
    // 连接成功后，启动心跳定时器，每 15 秒发送一次心跳包维持长连接活性
    if (heartbeatTimer_) {
        heartbeatTimer_->start(15000);
    }
    flushPendingPackets();
}

void ImClient::onDisconnected()
{
    // 连接断开后，停止心跳定时器并重置自身 UID
    if (heartbeatTimer_) {
        heartbeatTimer_->stop();
    }
    myUid_ = -1;
}

void ImClient::sendHeartBeat()
{
    // 按照服务端心跳协议发送空负载心跳包
    sendPacket(HEART_BEAT_MSG, "");
}

void ImClient::onReadyRead()
{
    readBuffer_.append(socket_.readAll());

    while (true) {
        if (readBuffer_.size() < 4) {
            return;
        }

        const quint32 bodyLen = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(readBuffer_.constData()));

        if (bodyLen < 4 || bodyLen > kMaxPacketSize) {
            emit networkError(QString::fromUtf8("收到非法数据包，连接已断开"));
            socket_.disconnectFromHost();
            readBuffer_.clear();
            return;
        }

        const int totalPacketLen = static_cast<int>(4 + bodyLen);
        if (readBuffer_.size() < totalPacketLen) {
            return;
        }

        const quint32 msgId = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(readBuffer_.constData() + 4));

        const QByteArray payload = readBuffer_.mid(8, static_cast<int>(bodyLen - 4));
        readBuffer_.remove(0, totalPacketLen);

        handleMessage(static_cast<int>(msgId), payload);
    }
}

void ImClient::onSocketError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    emit networkError(socket_.errorString());
}
