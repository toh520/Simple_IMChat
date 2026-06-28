#include "mainwindow.h"
#include "loginwidget.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 加载并应用全局 QSS 样式表以美化 UI
    QFile file(":/ui/style.qss");
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream ts(&file);
        a.setStyleSheet(ts.readAll());
    }

    LoginWidget loginWnd;
    loginWnd.show();

    return a.exec();
}
