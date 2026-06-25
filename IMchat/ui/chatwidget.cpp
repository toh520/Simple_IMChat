#include "chatwidget.h"
#include "ui_chatwidget.h"
#include "imclient.h"

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

    // 设置分裂器两栏的初始伸缩因子比例 (左栏 1/4 宽度，右栏 3/4 宽度)
    ui->splitter->setStretchFactor(0, 1);
    ui->splitter->setStretchFactor(1, 4);

    // 默认未选择会话时，禁用右侧消息输入区与发送按钮
    ui->edit_input->setEnabled(false);
    ui->btn_send->setEnabled(false);

    // 给输入编辑框安装事件过滤器，拦截并实现 Enter 键快捷发送
    ui->edit_input->installEventFilter(this);

    // 绑定网络库收到单聊消息的信号
    connect(&ImClient::instance(), &ImClient::oneChatReceived, this, &ChatWidget::onOneChatReceived);
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
void ChatWidget::onOneChatReceived(int fromId, int toId, const QString &msg)
{
    Q_UNUSED(toId);

    // 构造聊天消息结构
    ChatMessage chatMsg;
    chatMsg.fromId = fromId;
    chatMsg.toId = ImClient::instance().getMyUid(); // 接收者即为当前用户自己
    chatMsg.content = msg;
    chatMsg.timestamp = QDateTime::currentDateTime();

    // 存入当前会话的历史数据队列中
    chatHistory_[fromId].append(chatMsg);

    // 检查会话列表中是否已经存在该发送者的会话项
    bool sessionExists = false;
    for (int i = 0; i < ui->list_sessions->count(); ++i) {
        QListWidgetItem *item = ui->list_sessions->item(i);
        if (item->data(Qt::UserRole).toInt() == fromId) {
            sessionExists = true;
            // 如果不是当前正在进行的会话，在名字后方加上未读提示
            if (activeUserId_ != fromId) {
                item->setText(QString("用户 ID: %1 (有新消息)").arg(fromId));
            }
            break;
        }
    }

    // 若会话不存在，则自动将该发送者加入到左侧列表中
    if (!sessionExists) {
        addSession(fromId);
        // 对于刚创建的非常规会话，且当前没有切过去，也附加上未读消息文字
        if (activeUserId_ != fromId) {
            for (int i = 0; i < ui->list_sessions->count(); ++i) {
                QListWidgetItem *item = ui->list_sessions->item(i);
                if (item->data(Qt::UserRole).toInt() == fromId) {
                    item->setText(QString("用户 ID: %1 (有新消息)").arg(fromId));
                    break;
                }
            }
        }
    }

    // 如果当前打开的会话窗口正是发信人，则直接将其呈现在界面消息列表，并滚动至底部
    if (activeUserId_ == fromId) {
        appendMessageToView(chatMsg);
        ui->list_messages->scrollToBottom();
    }
}

// “发起新聊天” 按钮点击槽函数
void ChatWidget::on_btn_add_session_clicked()
{
    bool ok = false;
    int targetUid = QInputDialog::getInt(this, "发起新聊天", "请输入聊天对象的数字 UID:", 
                                         0, 0, 2147483647, 1, &ok);
    if (!ok) return;

    // 基础安全检查：不允许自己与自己聊天
    if (targetUid == ImClient::instance().getMyUid()) {
        QMessageBox::warning(this, "提示", "不能与自己聊天哦！");
        return;
    }

    // 检查此 UID 的会话项是否已在列表
    int existingRow = -1;
    for (int i = 0; i < ui->list_sessions->count(); ++i) {
        if (ui->list_sessions->item(i)->data(Qt::UserRole).toInt() == targetUid) {
            existingRow = i;
            break;
        }
    }

    // 若不存在，则创建新会话并初置其历史消息队列
    if (existingRow == -1) {
        addSession(targetUid);
        if (!chatHistory_.contains(targetUid)) {
            chatHistory_[targetUid] = QList<ChatMessage>();
        }
        existingRow = ui->list_sessions->count() - 1;
    }

    // 自动高亮并切换到这个会话项
    ui->list_sessions->setCurrentRow(existingRow);
}

// “发送” 按钮点击槽函数
void ChatWidget::on_btn_send_clicked()
{
    if (activeUserId_ == -1) return;

    QString text = ui->edit_input->toPlainText().trimmed();
    if (text.isEmpty()) return;

    // 1. 调用网络组件发送数据包
    ImClient::instance().sendOneChat(activeUserId_, text);

    // 2. 将消息构造并追加到本地缓存的历史聊天记录中
    ChatMessage chatMsg;
    chatMsg.fromId = ImClient::instance().getMyUid(); // 发送方是自己
    chatMsg.toId = activeUserId_;                     // 接收方是对方
    chatMsg.content = text;
    chatMsg.timestamp = QDateTime::currentDateTime();
    chatHistory_[activeUserId_].append(chatMsg);

    // 3. 将消息渲染到界面的消息列表中并清空输入区
    appendMessageToView(chatMsg);
    ui->edit_input->clear();
    ui->list_messages->scrollToBottom();
}

// 选中会话列表某行发生变更的槽函数
void ChatWidget::on_list_sessions_currentRowChanged(int currentRow)
{
    if (currentRow == -1) {
        activeUserId_ = -1;
        ui->lbl_title->setText("未选择聊天对象");
        ui->list_messages->clear();
        ui->edit_input->setEnabled(false);
        ui->btn_send->setEnabled(false);
        return;
    }

    QListWidgetItem *item = ui->list_sessions->item(currentRow);
    int uid = item->data(Qt::UserRole).toInt();
    activeUserId_ = uid;

    // 清除会话项名字后附带的“有新消息”未读标记
    item->setText(QString("用户 ID: %1").arg(uid));

    // 更新右侧标题与可用状态
    ui->lbl_title->setText(QString("当前与用户 %1 聊天").arg(uid));
    ui->edit_input->setEnabled(true);
    ui->btn_send->setEnabled(true);
    ui->edit_input->setFocus();

    // 重新加载渲染当前会话的所有历史消息
    loadChatHistory(uid);
}

// 私有辅助方法：将会话项加入左侧列表
void ChatWidget::addSession(int uid)
{
    QListWidgetItem *item = new QListWidgetItem(ui->list_sessions);
    item->setText(QString("用户 ID: %1").arg(uid));
    item->setData(Qt::UserRole, uid);
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

    // 判断发送者是否为自己，选择靠右或靠左的消息气泡排版
    if (msg.fromId == ImClient::instance().getMyUid()) {
        displayHtml = QString(
            "<div align='right' style='margin-bottom: 8px;'>"
            "  <span style='color: #666666; font-size: 9pt;'>我  (%1)</span><br/>"
            "  <span style='display: inline-block; background-color: #d2e3fc; color: #1a0dab; "
            "               padding: 6px 12px; border-radius: 8px; font-size: 11pt; "
            "               margin-top: 3px; max-width: 70%; word-wrap: break-word; text-align: left;'>"
            "    %2"
            "  </span>"
            "</div>"
        ).arg(timeStr).arg(msg.content.toHtmlEscaped().replace("\n", "<br/>"));
    } else {
        displayHtml = QString(
            "<div align='left' style='margin-bottom: 8px;'>"
            "  <span style='color: #666666; font-size: 9pt;'>用户 %1  (%2)</span><br/>"
            "  <span style='display: inline-block; background-color: #f1f3f4; color: #202124; "
            "               padding: 6px 12px; border-radius: 8px; font-size: 11pt; "
            "               margin-top: 3px; max-width: 70%; word-wrap: break-word; text-align: left;'>"
            "    %3"
            "  </span>"
            "</div>"
        ).arg(msg.fromId).arg(timeStr).arg(msg.content.toHtmlEscaped().replace("\n", "<br/>"));
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
