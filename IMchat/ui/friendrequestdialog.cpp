#include "friendrequestdialog.h"
#include "ui_friendrequestdialog.h"
#include "imclient.h"

#include <QMessageBox>
#include <QListWidgetItem>

FriendRequestDialog::FriendRequestDialog(const QList<QVariantMap> &applies, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::FriendRequestDialog)
    , applies_(applies)
{
    ui->setupUi(this);

    // 渲染初始申请列表
    refreshList();

    // 绑定处理好友申请的网络回调信号
    connect(&ImClient::instance(), &ImClient::processFriendResult, this, &FriendRequestDialog::onProcessFriendResult);
}

FriendRequestDialog::~FriendRequestDialog()
{
    delete ui;
}

void FriendRequestDialog::refreshList()
{
    ui->list_requests->clear();
    
    for (const auto &app : applies_) {
        int applyId = app["apply_id"].toInt();
        int fromId = app["from_id"].toInt();
        QString fromName = app["from_name"].toString();

        QListWidgetItem *item = new QListWidgetItem(ui->list_requests);
        // 显示文本格式：用户名 (ID) 申请加为好友
        item->setText(QString("%1 (UID: %2) 申请加您为好友").arg(fromName).arg(fromId));
        // 将 apply_id 和 from_id 存入 Item 的 UserRole 中
        item->setData(Qt::UserRole, applyId);
        item->setData(Qt::UserRole + 1, fromId);
    }

    // 如果列表为空，禁用按钮
    bool hasItems = ui->list_requests->count() > 0;
    ui->btn_accept->setEnabled(hasItems);
    ui->btn_reject->setEnabled(hasItems);
}

void FriendRequestDialog::on_btn_accept_clicked()
{
    QListWidgetItem *item = ui->list_requests->currentItem();
    if (!item) {
        QMessageBox::warning(this, "提示", "请先选择一条好友申请");
        return;
    }

    int applyId = item->data(Qt::UserRole).toInt();
    int fromId = item->data(Qt::UserRole + 1).toInt();

    // 禁用按钮防重复点击
    ui->btn_accept->setEnabled(false);
    ui->btn_reject->setEnabled(false);

    // 发送同意请求
    ImClient::instance().processFriend(applyId, fromId, true);
}

void FriendRequestDialog::on_btn_reject_clicked()
{
    QListWidgetItem *item = ui->list_requests->currentItem();
    if (!item) {
        QMessageBox::warning(this, "提示", "请先选择一条好友申请");
        return;
    }

    int applyId = item->data(Qt::UserRole).toInt();
    int fromId = item->data(Qt::UserRole + 1).toInt();

    // 禁用按钮防重复点击
    ui->btn_accept->setEnabled(false);
    ui->btn_reject->setEnabled(false);

    // 发送拒绝请求
    ImClient::instance().processFriend(applyId, fromId, false);
}

void FriendRequestDialog::onProcessFriendResult(bool success, int applyId, bool accept, int friendId, const QString &friendName, const QString &state)
{
    Q_UNUSED(friendId);
    Q_UNUSED(friendName);
    Q_UNUSED(state);

    if (!success) {
        QMessageBox::warning(this, "操作失败", "处理好友申请时发生错误");
        // 恢复按钮状态
        refreshList();
        return;
    }

    // 从本地列表中移除已处理的项
    for (int i = 0; i < applies_.size(); ++i) {
        if (applies_[i]["apply_id"].toInt() == applyId) {
            applies_.removeAt(i);
            break;
        }
    }

    // 提示用户
    QMessageBox::information(this, "操作成功", accept ? "已同意好友申请" : "已拒绝好友申请");

    // 刷新显示
    refreshList();
}
