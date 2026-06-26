#include "chatwidget.h"
#include "ui_chatwidget.h"
#include "imclient.h"
#include "snowflake.h"
#include "friendrequestdialog.h" // 引入好友申请对话框

#include <QInputDialog>
#include <QMessageBox>
#include <QKeyEvent>
#include <QLabel>
#include <QDateTime>

ChatWidget::ChatWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ChatWidget)
{
    ui->setupUi(this);

    // 将原“发起新聊天”按钮文字修改为“+ 添加好友”
    ui->btn_add_session->setText("+ 添加好友");

    // 设置分裂器两栏的初始伸缩因子比例 (左栏 1/4 宽度，右栏 3/4 宽度)
    ui->splitter->setStretchFactor(0, 1);
    ui->splitter->setStretchFactor(1, 4);

    // 默认未选择会话时，禁用右侧消息输入区与发送按钮
    ui->edit_input->setEnabled(false);
    ui->btn_send->setEnabled(false);

    // 给输入编辑框安装事件过滤器，拦截并实现 Enter 键快捷发送
    ui->edit_input->installEventFilter(this);

    // 绑定网络库收到单聊消息的信号 (包括 msgId)
    connect(&ImClient::instance(), &ImClient::oneChatReceived, this, &ChatWidget::onOneChatReceived);
    // 绑定网络库收到消息发送确认的信号
    connect(&ImClient::instance(), &ImClient::oneChatSendAck, this, &ChatWidget::onOneChatSendAck);

    // [新增] 绑定网络库社交相关信号
    connect(&ImClient::instance(), &ImClient::socialDataLoaded, this, &ChatWidget::onSocialDataLoaded);
    connect(&ImClient::instance(), &ImClient::addFriendResult, this, &ChatWidget::onAddFriendResult);
    connect(&ImClient::instance(), &ImClient::friendRequestReceived, this, &ChatWidget::onFriendRequestReceived);
    connect(&ImClient::instance(), &ImClient::processFriendResult, this, &ChatWidget::onProcessFriendResult);
    connect(&ImClient::instance(), &ImClient::friendStatusChanged, this, &ChatWidget::onFriendStatusChanged);
    connect(&ImClient::instance(), &ImClient::friendBindSuccess, this, &ChatWidget::onFriendBindSuccess);
}

ChatWidget::~ChatWidget()
{
    delete ui;
}

// 捕获并处理回车按键事件过滤器
bool ChatWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->edit_input && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        // 如果用户按下了 Return(Enter) 键，且没有同时按下 Ctrl 或 Shift
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) &&
            !(keyEvent->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
            // 执行发送消息槽函数
            on_btn_send_clicked();
            return true; // 拦截该事件，防止输入框内产生换行符
        }
    }
    return QWidget::eventFilter(watched, event);
}

// 接收单聊消息槽函数
void ChatWidget::onOneChatReceived(int fromId, int toId, const QString &msg, qint64 msgId)
{
    Q_UNUSED(toId);

    // 构造聊天消息结构
    ChatMessage chatMsg;
    chatMsg.msgId = msgId;
    chatMsg.fromId = fromId;
    chatMsg.toId = ImClient::instance().getMyUid(); // 接收者即为当前用户自己
    chatMsg.content = msg;
    chatMsg.timestamp = QDateTime::currentDateTime();
    chatMsg.status = MSG_STATUS_SUCCESS; // 接收到的消息默认为成功

    // 存入当前会话的历史数据队列中
    chatHistory_[fromId].append(chatMsg);

    // 检查是否为好友，若是，当非当前活跃会话时，设为未读状态
    if (friendsMap_.contains(fromId)) {
        if (activeUserId_ != fromId) {
            unreadUsers_.insert(fromId);
            refreshFriendList();
        }
    }

    // 如果当前打开的会话窗口正是发信人，则直接将其呈呈现界面消息列表，并滚动至底部
    if (activeUserId_ == fromId) {
        appendMessageToView(chatMsg);
        ui->list_messages->scrollToBottom();
    }
}

// “添加好友” 按钮点击槽函数 (原“发起新聊天”按钮)
void ChatWidget::on_btn_add_session_clicked()
{
    bool ok = false;
    int targetUid = QInputDialog::getInt(this, "添加好友", "请输入要添加好友的数字 UID:", 
                                         0, 0, 2147483647, 1, &ok);
    if (!ok) return;

    // 基础安全检查：不允许自己与自己聊天/加好友
    if (targetUid == ImClient::instance().getMyUid()) {
        QMessageBox::warning(this, "提示", "不能添加自己为好友！");
        return;
    }

    // 调用网络组件发送加好友申请
    ImClient::instance().addFriend(targetUid);
}

// “发送” 按钮点击槽函数
void ChatWidget::on_btn_send_clicked()
{
    if (activeUserId_ == -1) return;

    QString text = ui->edit_input->toPlainText().trimmed();
    if (text.isEmpty()) return;

    // 1. 生成雪花 ID
    qint64 msgId = Snowflake::instance().nextId();

    // 2. 调用网络组件发送数据包 (传入 msgId)
    ImClient::instance().sendOneChat(activeUserId_, text, msgId);

    // 3. 将消息构造并追加到本地缓存的历史聊天记录中 (初始状态为发送中)
    ChatMessage chatMsg;
    chatMsg.msgId = msgId;
    chatMsg.fromId = ImClient::instance().getMyUid(); // 发送方是自己
    chatMsg.toId = activeUserId_;                     // 接收方是对方
    chatMsg.content = text;
    chatMsg.timestamp = QDateTime::currentDateTime();
    chatMsg.status = MSG_STATUS_SENDING;
    chatHistory_[activeUserId_].append(chatMsg);

    // 4. 将消息渲染到界面的消息列表中并清空输入区
    appendMessageToView(chatMsg);
    ui->edit_input->clear();
    ui->list_messages->scrollToBottom();
}

// 处理收到消息发送确认的信号
void ChatWidget::onOneChatSendAck(qint64 msgId, bool success, const QString &errMsg)
{
    bool found = false;
    for (auto it = chatHistory_.begin(); it != chatHistory_.end(); ++it) {
        for (int i = 0; i < it->size(); ++i) {
            if (it->at(i).msgId == msgId) {
                (*it)[i].status = success ? MSG_STATUS_SUCCESS : MSG_STATUS_FAILED;
                
                // 如果发送失败且存在返回错误信息，则生成一条系统提示消息
                if (!success && !errMsg.isEmpty()) {
                    ChatMessage sysMsg;
                    sysMsg.msgId = 0;
                    sysMsg.fromId = 0; // 系统消息
                    sysMsg.toId = ImClient::instance().getMyUid();
                    sysMsg.content = QString("发送失败: %1").arg(errMsg);
                    sysMsg.timestamp = QDateTime::currentDateTime();
                    sysMsg.status = MSG_STATUS_SUCCESS;
                    
                    // 将系统消息追加到列表中
                    it->append(sysMsg);
                }
                
                found = true;
                break;
            }
        }
        if (found) {
            // 如果更新的消息正是当前打开的会话，重新加载渲染界面
            if (it.key() == activeUserId_) {
                loadChatHistory(activeUserId_);
            }
            break;
        }
    }
}

// “退出登录” 按钮点击槽函数
void ChatWidget::on_btn_logout_clicked()
{
    this->close();
}

// 选中会话列表某行发生变更的槽函数
void ChatWidget::on_list_sessions_currentRowChanged(int currentRow)
{
    if (currentRow == -1) {
        return;
    }

    QListWidgetItem *item = ui->list_sessions->item(currentRow);
    int uid = item->data(Qt::UserRole).toInt();

    // 如果选中的是第 0 行“新好友申请”
    if (uid == -2) {
        FriendRequestDialog dlg(pendingApplies_, this);
        dlg.exec();

        // 弹窗关闭后，清除选中高亮，防止下次点击无法触发
        ui->list_sessions->setCurrentRow(-1);
        return;
    }

    // 正常的聊天好友
    activeUserId_ = uid;
    unreadUsers_.remove(uid); // 移除未读标记

    // 重新刷新列表清除未读提示
    refreshFriendList();

    // 更新右侧标题与可用状态
    QString name = friendsMap_.contains(uid) ? friendsMap_[uid]["name"].toString() : QString::number(uid);
    ui->lbl_title->setText(QString("当前与好友 %1 (UID: %2) 聊天").arg(name).arg(uid));
    ui->edit_input->setEnabled(true);
    ui->btn_send->setEnabled(true);
    ui->edit_input->setFocus();

    // 重新加载渲染当前会话的所有历史消息
    loadChatHistory(uid);
}

// [新增] 登录初始化社交数据回调槽函数
void ChatWidget::onSocialDataLoaded(const QList<QVariantMap> &friends, const QList<QVariantMap> &applies)
{
    friendsMap_.clear();
    pendingApplies_ = applies;

    for (const auto &frnd : friends) {
        int friendId = frnd["id"].toInt();
        friendsMap_.insert(friendId, frnd);
    }

    refreshFriendList();
}

// [新增] 申请加好友返回结果
void ChatWidget::onAddFriendResult(bool success, const QString &msg)
{
    if (success) {
        QMessageBox::information(this, "申请加好友", msg);
    } else {
        QMessageBox::warning(this, "申请加好友失败", msg);
    }
}

// [新增] 收到好友申请实时通知
void ChatWidget::onFriendRequestReceived(int applyId, int fromId, const QString &fromName)
{
    QVariantMap app;
    app["apply_id"] = applyId;
    app["from_id"] = fromId;
    app["from_name"] = fromName;

    // 避免重复追加
    bool exists = false;
    for (const auto &existing : pendingApplies_) {
        if (existing["apply_id"].toInt() == applyId) {
            exists = true;
            break;
        }
    }
    if (!exists) {
        pendingApplies_.append(app);
    }

    refreshFriendList();
}

// [新增] 收到处理好友申请响应（同意/拒绝）
void ChatWidget::onProcessFriendResult(bool success, int applyId, bool accept, int friendId, const QString &friendName, const QString &state)
{
    if (!success) return;

    // 1. 从未处理申请列表中移除
    for (int i = 0; i < pendingApplies_.size(); ++i) {
        if (pendingApplies_[i]["apply_id"].toInt() == applyId) {
            pendingApplies_.removeAt(i);
            break;
        }
    }

    // 2. 若同意，则将新好友加入好友映射表
    if (accept) {
        QVariantMap frnd;
        frnd["id"] = friendId;
        frnd["name"] = friendName;
        frnd["state"] = state;
        friendsMap_.insert(friendId, frnd);

        // 创建系统提示消息
        ChatMessage sysMsg;
        sysMsg.msgId = 0;
        sysMsg.fromId = 0; // 系统消息
        sysMsg.toId = ImClient::instance().getMyUid();
        sysMsg.content = QString("您与 %1 (UID: %2) 已经成为好友，现在可以开始聊天了").arg(friendName).arg(friendId);
        sysMsg.timestamp = QDateTime::currentDateTime();
        sysMsg.status = MSG_STATUS_SUCCESS;
        chatHistory_[friendId].append(sysMsg);

        if (activeUserId_ == friendId) {
            appendMessageToView(sysMsg);
            ui->list_messages->scrollToBottom();
        }
    }

    refreshFriendList();
}

// [新增] 收到好友上线/下线实时状态通知
void ChatWidget::onFriendStatusChanged(int uid, const QString &state)
{
    if (!friendsMap_.contains(uid)) return;

    // 更新好友状态
    friendsMap_[uid]["state"] = state;
    refreshFriendList();

    // 在聊天窗口中增加系统提示气泡
    QString name = friendsMap_[uid]["name"].toString();
    ChatMessage sysMsg;
    sysMsg.msgId = 0;
    sysMsg.fromId = 0; // 系统消息
    sysMsg.toId = ImClient::instance().getMyUid();
    sysMsg.content = QString("好友 %1 (%2) 已%3").arg(name).arg(uid).arg(state == "online" ? "上线 ●" : "下线 ○");
    sysMsg.timestamp = QDateTime::currentDateTime();
    sysMsg.status = MSG_STATUS_SUCCESS;

    chatHistory_[uid].append(sysMsg);

    if (activeUserId_ == uid) {
        appendMessageToView(sysMsg);
        ui->list_messages->scrollToBottom();
    }
}

// [新增] 申请人收到好友关系建立实时通知 (A 收到 B 同意)
void ChatWidget::onFriendBindSuccess(int friendId, const QString &friendName, const QString &state)
{
    QVariantMap frnd;
    frnd["id"] = friendId;
    frnd["name"] = friendName;
    frnd["state"] = state;
    friendsMap_.insert(friendId, frnd);

    // 自动刷新好友列表
    refreshFriendList();

    // 插入一条系统消息提示
    ChatMessage sysMsg;
    sysMsg.msgId = 0;
    sysMsg.fromId = 0; // 系统消息
    sysMsg.toId = ImClient::instance().getMyUid();
    sysMsg.content = QString("您与 %1 (UID: %2) 已经成为好友，现在可以开始聊天了").arg(friendName).arg(friendId);
    sysMsg.timestamp = QDateTime::currentDateTime();
    sysMsg.status = MSG_STATUS_SUCCESS;
    chatHistory_[friendId].append(sysMsg);

    if (activeUserId_ == friendId) {
        appendMessageToView(sysMsg);
        ui->list_messages->scrollToBottom();
    }
}

// [新增] 刷新好友与申请列表
void ChatWidget::refreshFriendList()
{
    // 阻断信号，避免 clear() 触发 currentRowChanged(-1) 丢失当前聊天状态
    ui->list_sessions->blockSignals(true);

    ui->list_sessions->clear();

    // 1. 新好友申请行（固定的第 0 行）
    QListWidgetItem *applyItem = new QListWidgetItem(ui->list_sessions);
    int pendingCount = pendingApplies_.size();
    if (pendingCount > 0) {
        applyItem->setText(QString("★ 新好友申请 [%1] (有未处理)").arg(pendingCount));
        applyItem->setForeground(QBrush(Qt::red));
    } else {
        applyItem->setText("★ 新好友申请 [0]");
        applyItem->setForeground(QBrush(Qt::darkGray));
    }
    QFont applyFont = applyItem->font();
    applyFont.setBold(true);
    applyItem->setFont(applyFont);
    applyItem->setData(Qt::UserRole, -2); // 设为特殊的特殊ID -2

    // 2. 依次渲染好友列表行
    int selectRow = -1;
    int currentRowIndex = 1;
    for (auto it = friendsMap_.begin(); it != friendsMap_.end(); ++it) {
        int friendId = it.key();
        QString name = it.value()["name"].toString();
        QString state = it.value()["state"].toString();

        QListWidgetItem *item = new QListWidgetItem(ui->list_sessions);
        QString display;
        if (state == "online") {
            display = QString("● [在线] %1 (UID: %2)").arg(name).arg(friendId);
            item->setForeground(QBrush(QColor(24, 128, 56))); // 高雅绿色代表在线
        } else {
            display = QString("○ [离线] %1 (UID: %2)").arg(name).arg(friendId);
            item->setForeground(QBrush(Qt::gray)); // 灰色代表离线
        }

        // 若有未读消息，追加文字并加粗
        if (unreadUsers_.contains(friendId)) {
            display += " (有新消息)";
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }

        item->setText(display);
        item->setData(Qt::UserRole, friendId);

        if (friendId == activeUserId_) {
            selectRow = currentRowIndex;
        }
        currentRowIndex++;
    }

    // 恢复选中高亮
    if (selectRow != -1) {
        ui->list_sessions->setCurrentRow(selectRow);
    } else {
        ui->list_sessions->setCurrentRow(-1);
    }

    // 恢复信号槽
    ui->list_sessions->blockSignals(false);
}

// 私有辅助方法：将内存缓存的某个用户的聊天记录全部重新渲染到消息区
void ChatWidget::loadChatHistory(int uid)
{
    ui->list_messages->clear();
    if (chatHistory_.contains(uid)) {
        for (const ChatMessage &msg : chatHistory_[uid]) {
            appendMessageToView(msg);
        }
    }
    ui->list_messages->scrollToBottom();
}

// 私有辅助方法：渲染气泡样式的聊天消息富文本
void ChatWidget::appendMessageToView(const ChatMessage &msg)
{
    QString timeStr = msg.timestamp.toString("yyyy-MM-dd hh:mm:ss");
    QString displayHtml;
    
    QString statusStr;
    if (msg.fromId == ImClient::instance().getMyUid()) {
        if (msg.status == MSG_STATUS_SENDING) {
            statusStr = " <span style='color: #e37400; font-size: 8pt; font-weight: bold;'>[发送中...]</span>";
        } else if (msg.status == MSG_STATUS_FAILED) {
            statusStr = " <span style='color: #d93025; font-size: 8pt; font-weight: bold;'>[发送失败 ⚠️]</span>";
        } else {
            statusStr = " <span style='color: #188038; font-size: 8pt;'>[已送达]</span>";
        }
    }

    // 判断发送者类型：自己、系统、对方
    if (msg.fromId == ImClient::instance().getMyUid()) {
        displayHtml = QString(
            "<div align='right' style='margin-bottom: 8px;'>"
            "  <span style='color: #666666; font-size: 9pt;'>我  (%1)%2</span><br/>"
            "  <span style='display: inline-block; background-color: #d2e3fc; color: #1a0dab; "
            "               padding: 6px 12px; border-radius: 8px; font-size: 11pt; "
            "               margin-top: 3px; max-width: 70%; word-wrap: break-word; text-align: left;'>"
            "    %3"
            "  </span>"
            "</div>"
        ).arg(timeStr).arg(statusStr).arg(msg.content.toHtmlEscaped().replace("\n", "<br/>"));
    } else if (msg.fromId == 0) { // 系统通知消息气泡
        displayHtml = QString(
            "<div align='center' style='margin-top: 4px; margin-bottom: 12px;'>"
            "  <span style='display: inline-block; background-color: #f1f3f4; color: #5f6368; "
            "               padding: 4px 10px; border-radius: 12px; font-size: 9pt; "
            "               border: 1px solid #dadce0; max-width: 80%; word-wrap: break-word;'>"
            "    %1"
            "  </span>"
            "</div>"
        ).arg(msg.content.toHtmlEscaped());
    } else {
        // 获取对方名字
        QString name = friendsMap_.contains(msg.fromId) ? friendsMap_[msg.fromId]["name"].toString() : QString::number(msg.fromId);
        displayHtml = QString(
            "<div align='left' style='margin-bottom: 8px;'>"
            "  <span style='color: #666666; font-size: 9pt;'>%1  (UID: %2)  (%3)</span><br/>"
            "  <span style='display: inline-block; background-color: #f1f3f4; color: #202124; "
            "               padding: 6px 12px; border-radius: 8px; font-size: 11pt; "
            "               margin-top: 3px; max-width: 70%; word-wrap: break-word; text-align: left;'>"
            "    %4"
            "  </span>"
            "</div>"
        ).arg(name).arg(msg.fromId).arg(timeStr).arg(msg.content.toHtmlEscaped().replace("\n", "<br/>"));
    }

    QListWidgetItem *item = new QListWidgetItem(ui->list_messages);
    
    // 通过关联 QLabel 控件支持富文本 HTML 换行与气泡填充
    QLabel *label = new QLabel(this);
    label->setText(displayHtml);
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setStyleSheet("background: transparent;");

    ui->list_messages->setItemWidget(item, label);
    // 高度增加一些安全内衬值，防止文字重叠
    item->setSizeHint(QSize(0, label->sizeHint().height() + 10));
}
