#include "registerwidget.h"
#include "ui_registerwidget.h"

#include "imclient.h"

#include <QLineEdit>
#include <QMessageBox>

RegisterWidget::RegisterWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::RegisterWidget)
{
    ui->setupUi(this);

    setWindowTitle("注册");

    ui->edit_pwd->setEchoMode(QLineEdit::Password);
    ui->edit_pwd_confirm->setEchoMode(QLineEdit::Password);

    connect(&ImClient::instance(), &ImClient::regResult, this,
            [this](bool success, int uid, const QString &msg) {
                if (!isVisible()) {
                    return;
                }

                ui->btn_submit->setEnabled(true);

                if (success) {
                    QMessageBox::information(this, "注册成功",
                                             QString("注册成功，您的用户ID是：%1").arg(uid));
                    close();
                } else {
                    QMessageBox::warning(this, "注册失败", msg);
                }
            });

    connect(&ImClient::instance(), &ImClient::networkError, this,
            [this](const QString &msg) {
                if (!isVisible()) {
                    return;
                }

                ui->btn_submit->setEnabled(true);
                QMessageBox::warning(this, "网络错误", msg);
            });
}

RegisterWidget::~RegisterWidget()
{
    delete ui;
}

void RegisterWidget::on_btn_submit_clicked()
{
    const QString name = ui->edit_name->text().trimmed();
    const QString pwd = ui->edit_pwd->text();
    const QString confirmPwd = ui->edit_pwd_confirm->text();

    if (name.isEmpty() || pwd.isEmpty() || confirmPwd.isEmpty()) {
        QMessageBox::warning(this, "注册失败", "请完整填写用户名和密码");
        return;
    }

    if (pwd != confirmPwd) {
        QMessageBox::warning(this, "注册失败", "两次输入的密码不一致");
        return;
    }

    ui->btn_submit->setEnabled(false);
    ImClient::instance().reg(name, pwd);
}

void RegisterWidget::on_btn_back_clicked()
{
    close();
}
