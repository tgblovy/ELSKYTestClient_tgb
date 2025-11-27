#include "core/client_window.h"
#include <QApplication>
#include <QStyleFactory>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    qDebug() << "应用程序启动...";

    // 设置应用程序属性
    app.setApplicationName("ELSKYTestClient");
    app.setApplicationVersion("1.5");
    app.setOrganizationName("ELSKY");

    // 设置样式
    app.setStyle(QStyleFactory::create("Fusion"));

    qDebug() << "创建主窗口...";
    ClientWindow window;

    qDebug() << "显示主窗口...";
    window.show();

    qDebug() << "进入事件循环...";
    return app.exec();
}
