#include "loginwidget.h"
#include "ui_loginwidget.h"

#include "chatwidget.h"
#include "registerwidget.h"
#include "imclient.h"

#include <QMessageBox>

LoginWidget::LoginWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LoginWidget)
    , chatWidget_(nullptr)
    , registerWidget_(nullptr)
{
    ui->setupUi(this);

    connect(&ImClient::instance(), &ImClient::loginResult, this,
            [this](bool success, int uid, const QString &msg) {
                ui->pushButton->setEnabled(true);

                if (!success) {
                    QMessageBox::warning(this, "登录失败", msg);
                    return;
                }

                QMessageBox::information(this, "登录成功",
                                         QString("登录成功，用户ID：%1").arg(uid));

                if (chatWidget_ == nullptr) {
                    chatWidget_ = new ChatWidget(nullptr);
                    chatWidget_->setAttribute(Qt::WA_DeleteOnClose);

                    connect(chatWidget_, &QObject::destroyed, this, [this]() {
                        chatWidget_ = nullptr;
                        ImClient::instance().logout(); // 主动断开连接，下线用户
                        this->show();
                        this->activateWindow();
                    });
                }

                chatWidget_->setWindowTitle(QString("聊天界面 - UID %1").arg(uid));
                chatWidget_->show();
                chatWidget_->activateWindow();
                this->hide();
            });

    connect(&ImClient::instance(), &ImClient::networkError, this,
            [this](const QString &msg) {
                ui->pushButton->setEnabled(true);
                QMessageBox::warning(this, "网络错误", msg);
            });
}

LoginWidget::~LoginWidget()
{
    delete ui;
}

void LoginWidget::on_btn_register_clicked()
{
    // 同步当前端口给 ImClient，防止注册时连错端口
    QString portStr = ui->port->text();
    bool portOk = false;
    int port = portStr.toInt(&portOk);
    if (portOk && port > 0 && port < 65536) {
        ImClient::instance().setServerPort(static_cast<quint16>(port));
    }

    if (registerWidget_ == nullptr) {
        registerWidget_ = new RegisterWidget(nullptr);
        registerWidget_->setAttribute(Qt::WA_DeleteOnClose);

        connect(registerWidget_, &QObject::destroyed, this, [this]() {
            registerWidget_ = nullptr;
            this->show();
            this->activateWindow();
        });
    }

    registerWidget_->show();
    registerWidget_->activateWindow();
    this->hide();
}


void LoginWidget::on_pushButton_clicked()
{
    QString username = ui->id->text();
    QString password = ui->pwd->text();
    QString portStr = ui->port->text();

    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, "登录失败", "请输入用户ID和密码");
        return;
    }

    bool ok = false;
    username.toInt(&ok);
    if (!ok) {
        QMessageBox::warning(this, "登录失败", "服务端当前按用户ID登录，请输入数字ID");
        return;
    }

    bool portOk = false;
    int port = portStr.toInt(&portOk);
    if (!portOk || port <= 0 || port > 65535) {
        QMessageBox::warning(this, "登录失败", "请输入合法的端口号 (1-65535)");
        return;
    }

    ui->pushButton->setEnabled(false);
    ImClient::instance().setServerPort(static_cast<quint16>(port));
    ImClient::instance().login(username, password);

}

