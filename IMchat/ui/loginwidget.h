#ifndef LOGINWIDGET_H
#define LOGINWIDGET_H

#include <QWidget>

class ChatWidget;
class RegisterWidget;

namespace Ui {
class LoginWidget;
}

class LoginWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWidget(QWidget *parent = nullptr);
    ~LoginWidget();

private slots:
    void on_btn_register_clicked();

    void on_pushButton_clicked();

private:
    Ui::LoginWidget *ui;
    ChatWidget *chatWidget_;
    RegisterWidget *registerWidget_;
};

#endif // LOGINWIDGET_H
