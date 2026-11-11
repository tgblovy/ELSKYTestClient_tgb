#include "core/client_window.h"
#include "core/illegal_shutdown_detector.h"
#include <QApplication>
#include <QStyleFactory>
#include <QDebug>
#include <QProcess>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    qDebug() << "应用程序启动...";

    // 设置应用程序属性
    app.setApplicationName("ELSKYTestClient");
    app.setApplicationVersion("2.1");   // 与 deb 包 PK_VERSION 统一（原 1.5 落后于实际版本）
    app.setOrganizationName("ELSKY");

    // 设置样式
    app.setStyle(QStyleFactory::create("Fusion"));

    // ========== 退出路径兜底清理 ==========
    // 注册 aboutToQuit 信号处理，确保在事件循环正常退出时清理运行标志：
    // 1. 正常关闭窗口（closeEvent 触发后事件循环退出）
    // 2. 调用 app.quit() 主动退出
    // 注：SIGTERM/SIGINT 场景（统信DDE等桌面环境关机时直接发信号杀进程）
    // 已由 IllegalShutdownDetector 构造时安装的Unix信号处理器覆盖
    // （信号处理器内直接unlink标志文件，async-signal-safe，不依赖Qt事件循环）
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        qDebug() << "[MAIN] 应用程序即将退出，清理运行标志文件";
        IllegalShutdownDetector::markCleanShutdown();
        // 同步文件系统缓冲区，确保删除操作真正写入磁盘
        QProcess::execute("sync");
    });
    // ================================

    qDebug() << "创建主窗口...";
    ClientWindow window;

    qDebug() << "显示主窗口...";
    window.show();

    qDebug() << "进入事件循环...";
    return app.exec();
}
