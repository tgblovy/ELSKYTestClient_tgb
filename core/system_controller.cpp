#include "system_controller.h"
#include <QProcess>
#include <QDebug>

bool SystemController::executeSystemCommand(const QString &command) {
    QProcess process;
    process.start("bash", QStringList() << "-c" << command);

    if (!process.waitForStarted()) {
        qDebug() << "无法启动进程:" << command;
        return false;
    }

    if (!process.waitForFinished(10000)) {
        qDebug() << "进程执行超时:" << command;
        process.kill();
        return false;
    }

    return (process.exitCode() == 0);
}

bool SystemController::shutdown() {
    QString command = "poweroff";
    QProcess checkProcess;
    checkProcess.start("which", QStringList() << "poweroff");
    checkProcess.waitForFinished();
    if (checkProcess.exitCode() != 0) {
        command = "shutdown -h now";
    }
    return executeSystemCommand(command);
}

bool SystemController::reboot() {
    QString command = "reboot";
    QProcess checkProcess;
    checkProcess.start("which", QStringList() << "reboot");
    checkProcess.waitForFinished();
    if (checkProcess.exitCode() != 0) {
        command = "shutdown -r now";
    }
    return executeSystemCommand(command);
}

bool SystemController::suspend() {
    QStringList suspendCommands = {
        "systemctl suspend",
        "pm-suspend",
        "echo mem > /sys/power/state"
    };

    for (const QString &command : suspendCommands) {
        if (executeSystemCommand(command)) {
            return true;
        }
    }
    return false;
}

bool SystemController::hibernate() {
    QStringList hibernateCommands = {
        "systemctl hibernate",
        "pm-hibernate",
        "echo disk > /sys/power/state"
    };

    for (const QString &command : hibernateCommands) {
        if (executeSystemCommand(command)) {
            return true;
        }
    }
    return false;
}
