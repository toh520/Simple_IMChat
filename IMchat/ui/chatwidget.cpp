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
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpacerItem>

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
    // 构造聊天消息结构
    ChatMessage chatMsg;
    chatMsg.msgId = msgId;
    chatMsg.fromId = fromId;
    chatMsg.toId = toId;
    chatMsg.content = msg;
    
    // 从雪花 ID 中解析出精确的时间戳
    if (msgId > 0) {
        qint64 timestampMS = (msgId >> 22) + 1704067200000ULL;
        chatMsg.timestamp = QDateTime::fromMSecsSinceEpoch(timestampMS);
    } else {
        chatMsg.timestamp = QDateTime::currentDateTime();
    }
    chatMsg.status = MSG_STATUS_SUCCESS; // 接收到的消息默认为成功

    int sessionUserId = (fromId == ImClient::instance().getMyUid()) ? toId : fromId;

    // 存入当前会话的历史数据队列中
    chatHistory_[sessionUserId].append(chatMsg);

    // 检查是否为好友，若是，当非当前活跃会话时，设为未读状态
    if (friendsMap_.contains(sessionUserId)) {
        if (activeUserId_ != sessionUserId) {
            unreadUsers_.insert(sessionUserId);
            refreshFriendList();
        }
    }

    // 如果当前打开的会话窗口正是发信人，则直接将其呈呈现界面消息列表，并滚动至底部
    if (activeUserId_ == sessionUserId) {
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
    chatMsg.timestamp = QDateTime::fromMSecsSinceEpoch((msgId >> 22) + 1704067200000ULL);
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

// 私有辅助方法：使用独立的 QWidget 容器与布局动态构建气泡，并通过 QFontMetrics 精确计算文本宽高，彻底解决折叠与省略号问题
void ChatWidget::appendMessageToView(const ChatMessage &msg)
{
    // 1. 创建消息条目的主容器 Widget
    QWidget *container = new QWidget(ui->list_messages);
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(10, 5, 10, 5); // 设置上下左右边距
    layout->setSpacing(4);                   // 头部信息与聊天气泡之间的间距

    // 2. 创建头部信息 Label (显示发送者、时间及状态)
    QLabel *headerLabel = new QLabel(container);
    QString timeStr = msg.timestamp.toString("yyyy-MM-dd hh:mm:ss");

    // 3. 创建消息内容气泡 Label
    QLabel *contentLabel = new QLabel(container);
    contentLabel->setText(msg.content);
    contentLabel->setWordWrap(true);
    contentLabel->setMargin(0); // 关键：消除 QLabel 默认的内边距，确保像素计算完全由我们掌控
    contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse); // 支持鼠标选中复制文本

    // 限制气泡最大宽度为聊天列表宽度的 70%
    int maxW = ui->list_messages->width() * 0.7;
    if (maxW < 250) maxW = 380; // 设置保底最大宽度

    // 4. 创建气泡水平布局以实现靠左或靠右对齐
    QHBoxLayout *bubbleLayout = new QHBoxLayout();
    bubbleLayout->setContentsMargins(0, 0, 0, 0);

    if (msg.fromId == 0) {
        // 系统通知消息：居中展示，灰色小字卡片样式
        headerLabel->hide();
        contentLabel->setAlignment(Qt::AlignCenter);
        
        // 设置精确的系统字体与大小
        QFont sysFont("Microsoft YaHei");
        sysFont.setPointSizeF(8.5);
        contentLabel->setFont(sysFont);
        
        // 计算系统消息的精确宽高
        // 左右 padding 共 20px (10px * 2)
        // 极重要修复：将宽度安全缓冲提高至 24px，高度缓冲提高至 8px。
        // 这是因为系统消息包含特殊字符（如“●”和“○”），这类字符在不同系统和字型下会被渲染得非常宽（触发字体回退机制），
        // 必须留出足够的横向余量，否则最后一个字/符号必定会被挤压折行导致显示不全。
        QFontMetrics fm(sysFont);
        int textWidthLimit = maxW - 20 - 24; 
        QRect rect = fm.boundingRect(0, 0, textWidthLimit, 10000, Qt::TextWordWrap, msg.content);
        
        int contentHeight = rect.height() + 8 + 8; // 上下 padding (8px) + 高度安全缓冲 (8px)
        int contentWidth = rect.width() + 20 + 24; // 左右 padding (20px) + 宽度安全缓冲 (24px)
        if (contentWidth > maxW) contentWidth = maxW;
        
        contentLabel->setFixedSize(contentWidth, contentHeight);

        contentLabel->setStyleSheet(
            "QLabel {"
            "  background-color: #f8fafc;"
            "  color: #64748b;"
            "  border: 1px solid #e2e8f0;"
            "  border-radius: 10px;"
            "  padding: 4px 10px;"
            "}"
        );
        bubbleLayout->addStretch();
        bubbleLayout->addWidget(contentLabel);
        bubbleLayout->addStretch();
        
        layout->addLayout(bubbleLayout);
    } else {
        // 用户消息：设置与 QSS 匹配的字体大小（10pt）进行精确高度计算
        QFont font("Microsoft YaHei");
        font.setPointSize(10);
        contentLabel->setFont(font);

        // 计算用户消息的精确宽高
        // 左右 padding 共 28px (14px * 2)
        // 极重要修复：同样将宽度安全缓冲提高至 24px，高度缓冲提高至 8px，以兼容用户发送表情符号（Emoji）或特殊标点时的回退渲染。
        QFontMetrics fm(font);
        int textWidthLimit = maxW - 28 - 24; 
        QRect rect = fm.boundingRect(0, 0, textWidthLimit, 10000, Qt::TextWordWrap, msg.content);
        
        int contentHeight = rect.height() + 16 + 8; // 上下 padding (16px) + 高度安全缓冲 (8px)
        int contentWidth = rect.width() + 28 + 24;  // 左右 padding (28px) + 宽度安全缓冲 (24px)
        if (contentWidth > maxW) contentWidth = maxW;
        
        // 显式固定 Label 大小，防止 Qt 布局引擎在未完全显示前将其压缩
        contentLabel->setFixedSize(contentWidth, contentHeight);

        if (msg.fromId == ImClient::instance().getMyUid()) {
            // 我发送的消息：右对齐，蓝色气泡，右上角为微直角
            QString statusStr;
            if (msg.status == MSG_STATUS_SENDING) {
                statusStr = " <span style='color: #f59e0b;'>[发送中...]</span>";
            } else if (msg.status == MSG_STATUS_FAILED) {
                statusStr = " <span style='color: #ef4444;'>[失败 ⚠️]</span>";
            } else {
                statusStr = " <span style='color: #10b981; font-weight: bold;'>✓</span>";
            }

            headerLabel->setText(QString("我  (%1)%2").arg(timeStr).arg(statusStr));
            headerLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            headerLabel->setStyleSheet("color: #94a3b8; font-size: 8.5pt; background: transparent;");

            contentLabel->setStyleSheet(
                "QLabel {"
                "  background-color: #6366f1;"
                "  color: #ffffff;"
                "  border-radius: 12px;"
                "  border-top-right-radius: 2px;"
                "  padding: 8px 14px;"
                "}"
            );

            bubbleLayout->addStretch();
            bubbleLayout->addWidget(contentLabel);
            
            layout->addWidget(headerLabel);
            layout->addLayout(bubbleLayout);
        } else {
            // 对方发送的消息：左对齐，灰白色气泡，左上角为微直角
            QString name = friendsMap_.contains(msg.fromId) ? friendsMap_[msg.fromId]["name"].toString() : QString::number(msg.fromId);
            headerLabel->setText(QString("%1  (UID: %2)  %3").arg(name).arg(msg.fromId).arg(timeStr));
            headerLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            headerLabel->setStyleSheet("color: #64748b; font-size: 8.5pt; background: transparent;");

            contentLabel->setStyleSheet(
                "QLabel {"
                "  background-color: #f1f5f9;"
                "  color: #0f172a;"
                "  border-radius: 12px;"
                "  border-top-left-radius: 2px;"
                "  padding: 8px 14px;"
                "}"
            );

            bubbleLayout->addWidget(contentLabel);
            bubbleLayout->addStretch();

            layout->addWidget(headerLabel);
            layout->addLayout(bubbleLayout);
        }
    }

    container->setLayout(layout);
    container->setStyleSheet("background: transparent;");

    // 5. 激活布局并计算主容器的精确大小
    layout->activate();

    // 6. 将容器设置到 QListWidget 中并动态设置项高度
    QListWidgetItem *item = new QListWidgetItem(ui->list_messages);
    ui->list_messages->setItemWidget(item, container);
    
    // 设置安全高度，留出外边距，防止底部被截断
    item->setSizeHint(QSize(0, container->sizeHint().height() + 4));
}
