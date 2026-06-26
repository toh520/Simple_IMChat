#include "imclient.h"

#include "msg.pb.h"
#include "public.h"

#include <QAbstractSocket>
#include <QtEndian>
#include <QTimer> // 引入定时器头文件
#include <QSettings>

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

    // 初始化重发定时器
    retransmitTimer_ = new QTimer(this);
    connect(retransmitTimer_, &QTimer::timeout, this, &ImClient::checkRetransmit);

    connect(&socket_, &QTcpSocket::connected, this, &ImClient::onConnected);
    connect(&socket_, &QTcpSocket::disconnected, this, &ImClient::onDisconnected);
    connect(&socket_, &QTcpSocket::readyRead, this, &ImClient::onReadyRead);
    connect(&socket_, &QTcpSocket::errorOccurred, this, &ImClient::onSocketError);
}

bool ImClient::isConnected() const
{
    return socket_.state() == QAbstractSocket::ConnectedState;
}

void ImClient::setServerPort(quint16 port)
{
    serverPort_ = port;
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

void ImClient::logout()
{
    myUid_ = -1;
    lastSyncKey_ = 0;
    socket_.disconnectFromHost();
}

void ImClient::addFriend(int friendId)
{
    chat::AddFriendReq req;
    req.set_from_id(myUid_);
    req.set_to_id(friendId);

    std::string payload;
    req.SerializeToString(&payload);
    sendPacket(ADD_FRIEND_REQ, payload);
}

void ImClient::processFriend(int applyId, int fromId, bool accept)
{
    chat::ProcessFriendReq req;
    req.set_apply_id(applyId);
    req.set_from_id(fromId);
    req.set_to_id(myUid_);
    req.set_accept(accept);

    std::string payload;
    req.SerializeToString(&payload);
    sendPacket(PROCESS_FRIEND_REQ, payload);
}

void ImClient::sendOneChat(int toId, const QString &msg, qint64 msgId)
{
    chat::OneChatRequest req;
    req.set_from_id(myUid_); // 设置发送者 ID
    req.set_to_id(toId);     // 设置接收者 ID
    req.set_msg(msg.toStdString()); // 设置消息内容
    req.set_msg_id(msgId);   // 设置消息唯一 ID

    std::string payload;
    req.SerializeToString(&payload);

    // 存入待确认队列
    PendingSendMsg pMsg;
    pMsg.msgId = msgId;
    pMsg.toId = toId;
    pMsg.payload = payload;
    pMsg.sendTime = QDateTime::currentMSecsSinceEpoch();
    pMsg.retryCount = 0;
    pendingSendAckMap_.insert(msgId, pMsg);

    sendPacket(ONE_CHAT_MSG, payload); // 按照协议发送 ONE_CHAT_MSG 类型的消息
}

void ImClient::syncMessages(qint64 lastSyncKey)
{
    chat::SyncReq req;
    req.set_uid(myUid_);
    req.set_last_sync_key(lastSyncKey);

    std::string payload;
    req.SerializeToString(&payload);
    sendPacket(SYNC_REQ, payload);
}

void ImClient::ensureConnected()
{
    if (socket_.state() == QAbstractSocket::ConnectedState ||
        socket_.state() == QAbstractSocket::ConnectingState) {
        return;
    }

    socket_.connectToHost(QString::fromUtf8(kServerHost), serverPort_);
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
            QSettings settings("Simple_IMChat", "Client");
            lastSyncKey_ = settings.value(QString("last_sync_key_%1").arg(myUid_), 0).toLongLong();
        }

        // 解析好友列表和状态
        QList<QVariantMap> friendsList;
        for (int i = 0; i < resp.friends_size(); ++i) {
            const auto& frnd = resp.friends(i);
            QVariantMap f;
            f["id"] = frnd.id();
            f["name"] = QString::fromStdString(frnd.name());
            f["state"] = QString::fromStdString(frnd.state());
            friendsList.append(f);
        }

        // 解析未处理好友申请
        QList<QVariantMap> appliesList;
        for (int i = 0; i < resp.pending_applies_size(); ++i) {
            const auto& app = resp.pending_applies(i);
            QVariantMap a;
            a["apply_id"] = app.apply_id();
            a["from_id"] = app.from_id();
            a["from_name"] = QString::fromStdString(app.from_name());
            appliesList.append(a);
        }

        emit loginResult(resp.success(), resp.uid(), QString::fromStdString(resp.msg()));
        
        if (resp.success()) {
            emit socialDataLoaded(friendsList, appliesList);
            syncMessages(lastSyncKey_);
        }
        return;
    }

    if (msgId == MSG_SEND_ACK) {
        chat::MsgSendAck resp;
        if (!resp.ParseFromString(data)) {
            return;
        }
        qint64 mId = resp.msg_id();
        pendingSendAckMap_.remove(mId);
        emit oneChatSendAck(mId, resp.success(), QString::fromStdString(resp.err_msg()));
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

        // 1. 收到消息立即回发 MSG_RECV_ACK
        chat::MsgRecvAck ack;
        ack.set_msg_id(req.msg_id());
        ack.set_from_id(req.from_id());
        ack.set_to_id(req.to_id());
        std::string ackPayload;
        ack.SerializeToString(&ackPayload);
        sendPacket(MSG_RECV_ACK, ackPayload);

        // 2. 滑动窗口去重
        if (recvMsgWindow_.contains(req.msg_id())) {
            // 重复的消息，直接忽略，防止重复渲染，但 ACK 已经发送
            return;
        }
        recvMsgWindow_.append(req.msg_id());
        if (recvMsgWindow_.size() > 1000) {
            recvMsgWindow_.removeFirst();
        }

        // 触发单聊消息接收信号，通知 UI 层 (带上 msgId)
        emit oneChatReceived(req.from_id(), req.to_id(), QString::fromStdString(req.msg()), req.msg_id());
        return;
    }

    // [新增] 处理好友申请响应
    if (msgId == ADD_FRIEND_RESP) {
        chat::AddFriendResp resp;
        if (resp.ParseFromString(data)) {
            emit addFriendResult(resp.success(), QString::fromStdString(resp.msg()));
        }
        return;
    }

    // [新增] 收到好友申请实时通知
    if (msgId == FRIEND_REQUEST_NOTIFY) {
        chat::FriendRequestNotify notify;
        if (notify.ParseFromString(data)) {
            emit friendRequestReceived(notify.apply_id(), notify.from_id(), QString::fromStdString(notify.from_name()));
        }
        return;
    }

    // [新增] 收到处理好友申请响应 (同意/拒绝成功反馈)
    if (msgId == PROCESS_FRIEND_RESP) {
        chat::ProcessFriendResp resp;
        if (resp.ParseFromString(data)) {
            emit processFriendResult(resp.success(), resp.apply_id(), resp.accept(), 
                                     resp.friend_info().id(), 
                                     QString::fromStdString(resp.friend_info().name()), 
                                     QString::fromStdString(resp.friend_info().state()));
        }
        return;
    }

    // [新增] 收到好友上线/下线实时状态通知
    if (msgId == USER_STATUS_NOTIFY_MSG) {
        chat::UserStatusNotify notify;
        if (notify.ParseFromString(data)) {
            emit friendStatusChanged(notify.uid(), QString::fromStdString(notify.state()));
        }
        return;
    }

    // [新增] 收到好友关系建立实时通知 (A收到B同意的事件)
    if (msgId == ADD_FRIEND_SUCCESS_NOTIFY) {
        chat::AddFriendSuccessNotify notify;
        if (notify.ParseFromString(data)) {
            emit friendBindSuccess(notify.friend_info().id(), 
                                   QString::fromStdString(notify.friend_info().name()), 
                                   QString::fromStdString(notify.friend_info().state()));
        }
        return;
    }

    // [新增] 处理服务端返回的同步消息响应 (SYNC_RESP)
    if (msgId == SYNC_RESP) {
        chat::SyncResp resp;
        if (!resp.ParseFromString(data)) {
            emit networkError(QString::fromUtf8("同步消息响应解析失败"));
            return;
        }

        if (resp.success()) {
            // 遍历拉取到的历史消息
            for (int i = 0; i < resp.messages_size(); ++i) {
                const auto& reqMsg = resp.messages(i);

                // 滑动窗口去重
                if (recvMsgWindow_.contains(reqMsg.msg_id())) {
                    continue;
                }
                recvMsgWindow_.append(reqMsg.msg_id());
                if (recvMsgWindow_.size() > 1000) {
                    recvMsgWindow_.removeFirst();
                }

                // 触发单聊消息接收信号，通知 UI 层
                emit oneChatReceived(reqMsg.from_id(), reqMsg.to_id(), QString::fromStdString(reqMsg.msg()), reqMsg.msg_id());
            }

            // 更新 SyncKey 并持久化
            if (resp.new_sync_key() > lastSyncKey_) {
                lastSyncKey_ = resp.new_sync_key();
                QSettings settings("Simple_IMChat", "Client");
                settings.setValue(QString("last_sync_key_%1").arg(myUid_), lastSyncKey_);
            }
        }
        return;
    }
}

void ImClient::onConnected()
{
    // 连接成功后，启动心跳定时器，每 15 秒发送一次心跳包维持长连接活性
    if (heartbeatTimer_) {
        heartbeatTimer_->start(15000);
    }
    // 启动重发定时器，每 2 秒扫描一次
    if (retransmitTimer_) {
        retransmitTimer_->start(2000);
    }
    flushPendingPackets();
}

void ImClient::onDisconnected()
{
    // 连接断开后，停止心跳与重发定时器并重置自身 UID
    if (heartbeatTimer_) {
        heartbeatTimer_->stop();
    }
    if (retransmitTimer_) {
        retransmitTimer_->stop();
    }
    myUid_ = -1;

    // 清理发送待确认队列，所有未确认消息触发发送失败信号
    for (auto it = pendingSendAckMap_.begin(); it != pendingSendAckMap_.end(); ++it) {
        emit oneChatSendAck(it->msgId, false, QString::fromUtf8("连接已断开"));
    }
    pendingSendAckMap_.clear();
}

void ImClient::checkRetransmit()
{
    qint64 curr = QDateTime::currentMSecsSinceEpoch();
    for (auto it = pendingSendAckMap_.begin(); it != pendingSendAckMap_.end(); ) {
        if (curr - it->sendTime >= 3000) { // 3秒超时重传
            if (it->retryCount < 3) {
                it->retryCount++;
                it->sendTime = curr;
                sendPacket(ONE_CHAT_MSG, it->payload); // 重新发送
                ++it;
            } else {
                qint64 mId = it->msgId;
                it = pendingSendAckMap_.erase(it); // 移出重传队列
                emit oneChatSendAck(mId, false, QString::fromUtf8("发送超时"));
            }
        } else {
            ++it;
        }
    }
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
