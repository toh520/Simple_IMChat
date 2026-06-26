#ifndef FRIENDREQUESTDIALOG_H
#define FRIENDREQUESTDIALOG_H

#include <QDialog>
#include <QVariantMap>
#include <QList>

namespace Ui {
class FriendRequestDialog;
}

// 好友申请处理对话框类
class FriendRequestDialog : public QDialog
{
    Q_OBJECT

public:
    // 构造函数，传入待处理的申请列表
    explicit FriendRequestDialog(const QList<QVariantMap> &applies, QWidget *parent = nullptr);
    ~FriendRequestDialog();

private slots:
    // 同意按钮点击槽函数
    void on_btn_accept_clicked();
    // 拒绝按钮点击槽函数
    void on_btn_reject_clicked();
    // 收到处理好友申请结果的网络回调信号槽函数
    void onProcessFriendResult(bool success, int applyId, bool accept, int friendId, const QString &friendName, const QString &state);

private:
    // 刷新界面列表显示
    void refreshList();

private:
    Ui::FriendRequestDialog *ui;
    QList<QVariantMap> applies_; // 缓存本地的申请列表
};

#endif // FRIENDREQUESTDIALOG_H
