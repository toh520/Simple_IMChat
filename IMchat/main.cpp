#include "mainwindow.h"
#include "loginwidget.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    LoginWidget loginWnd;
    loginWnd.show();

    return a.exec();
}
