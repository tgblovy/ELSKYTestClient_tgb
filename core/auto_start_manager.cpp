#include "auto_start_manager.h"
#include <QProcess>
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QMessageBox>
#include <QTimer>
#include <QSettings>
#include <QCoreApplication>  // 添加这行
#include <QDebug>  // 添加这行
#include <unistd.h>          // 添加这行，用于 getuid()

AutoStartManager::AutoStartManager(QObject *parent)
    : QObject(parent)
    , autoStartCheckBox(nullptr)
    , isCancellingAutoStart(false)
{
}

void AutoStartManager::setupUI(QCheckBox *checkBox)
{
    autoStartCheckBox = checkBox;
    connect(autoStartCheckBox, &QCheckBox::toggled, this, &AutoStartManager::onAutoStartToggled);

    // 加载保存的设置
    QSettings settings("ELSKY", "TestClient");
    bool autoStart = settings.value("client/autoStart", false).toBool();
    autoStartCheckBox->setChecked(autoStart);

    // 延迟验证服务状态
    QTimer::singleShot(2000, this, &AutoStartManager::verifyAutoStart);
}

bool AutoStartManager::setAutoStart(bool enable)
{
    emit logMessage(enable ? "执行启用开机启动操作" : "执行取消开机启动操作");

    bool result = false;

    if (enable) {
        result = createSystemdService();
    } else {
        // 设置取消保护
        isCancellingAutoStart = true;

        emit logMessage("安全取消开机启动流程...");
        result = removeSystemdService();

        if (result) {
            emit logMessage("✓ 开机自启动已安全取消 - 程序实例保持运行");
        }

        isCancellingAutoStart = false;
    }

    // 保存设置
    QSettings settings("ELSKY", "TestClient");
    settings.setValue("client/autoStart", enable);

    return result;
}

bool AutoStartManager::isAutoStartEnabled()
{
    QProcess process;
    process.start("systemctl", QStringList() << "--user" << "is-enabled" << "elsky-test-client.service");

    if (process.waitForFinished(3000)) {
        return process.exitCode() == 0;
    }
    return false;
}

void AutoStartManager::onAutoStartToggled(bool checked)
{
    // 防止重复取消操作
    if (!checked && isCancellingAutoStart) {
        emit logMessage("警告: 取消开机启动操作正在进行中，跳过重复操作");
        QTimer::singleShot(100, this, [this]() {
            if (autoStartCheckBox) {
                autoStartCheckBox->setChecked(true);
            }
        });
        return;
    }

    emit logMessage(checked ? "用户启用开机启动" : "用户取消开机启动");

    if (checked) {
        emit logMessage("正在设置开机自启动...");
        if (setAutoStart(true)) {
            emit logMessage("开机自启动设置成功");
            emit logMessage("✓ 开机自启动设置成功，重启系统后程序将自动运行");
            emit autoStartStatusChanged(true);  // 触发状态变化信号
        } else {
            emit logMessage("设置开机自启动失败");
            QTimer::singleShot(100, this, [this]() {
                if (autoStartCheckBox) {
                    autoStartCheckBox->setChecked(false);
                }
            });
        }
    } else {
        // 关键修复：使用最安全的取消方法
        safestRemoveSystemdService();
        emit autoStartStatusChanged(false);  // 触发状态变化信号
    }
}

bool AutoStartManager::createSystemdService()
{
    emit logMessage("正在创建开机启动服务...");
    QString appPath = QCoreApplication::applicationFilePath();

    if (appPath.isEmpty() || !QFile::exists(appPath)) {
        emit logMessage("错误: 应用程序路径无效或不存在: " + appPath);
        return false;
    }

    QString homeDir = QDir::homePath();
    QString userName = qgetenv("USER");
    if (userName.isEmpty()) {
        userName = qgetenv("USERNAME");
        if (userName.isEmpty()) {
            userName = QDir::home().dirName();
        }
    }

    // 创建用户 systemd 目录
    QString userSystemdDir = homeDir + "/.config/systemd/user";
    QDir dir;
    if (!dir.mkpath(userSystemdDir)) {
        emit logMessage("错误: 无法创建用户systemd目录: " + userSystemdDir);
        return false;
    }

    QString serviceFilePath = userSystemdDir + "/elsky-test-client.service";

    QString serviceContent = QString(
        "[Unit]\n"
        "Description=ELSKY Test Client\n"
        "After=graphical-session.target\n"
        "After=network.target\n"
        "Wants=graphical-session.target\n"
        "\n"
        "[Service]\n"
        "Type=simple\n"
        "ExecStartPre=/bin/sleep 15\n"
        "ExecStart=%1\n"
        "WorkingDirectory=%2\n"
        "Environment=DISPLAY=:0\n"
        "Environment=XAUTHORITY=%3/.Xauthority\n"
        "Environment=XDG_RUNTIME_DIR=/run/user/%4\n"
        "Restart=no\n"
        "TimeoutSec=30\n"
        "\n"
        "[Install]\n"
        "WantedBy=default.target\n"
    ).arg(appPath, homeDir, homeDir, QString::number(getuid()));

    // 写入服务文件
    QFile serviceFile(serviceFilePath);
    if (!serviceFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit logMessage("错误: 无法创建服务文件: " + serviceFilePath);
        return false;
    }

    QTextStream out(&serviceFile);
    out << serviceContent;
    serviceFile.close();

    emit logMessage("服务文件创建成功: " + serviceFilePath);

    // 重新加载并启用服务
    QProcess process;

    // 重新加载systemd配置
    process.start("systemctl", QStringList() << "--user" << "daemon-reload");
    if (!process.waitForFinished(5000)) {
        emit logMessage("警告: systemctl --user daemon-reload 超时");
    }

    // 启用服务
    process.start("systemctl", QStringList() << "--user" << "enable" << "elsky-test-client.service");
    if (!process.waitForFinished(5000)) {
        emit logMessage("错误: systemctl --user enable 执行超时");
        return false;
    }

    if (process.exitCode() != 0) {
        emit logMessage("错误: 启用服务失败，退出码: " + QString::number(process.exitCode()));
        return false;
    }

    emit logMessage("✓ 开机启动服务设置完成");

    // 验证服务状态
    QTimer::singleShot(2000, this, &AutoStartManager::verifyAutoStart);

    return true;
}

// 新增最安全的服务移除方法
void AutoStartManager::safestRemoveSystemdService()
{
    emit logMessage("执行最安全的服务移除...");

    // 使用独立的进程来执行服务移除，完全隔离
    QString scriptContent = R"(
#!/bin/bash
sleep 2  # 延迟执行，确保主程序完全继续运行

# 禁用服务（不停止当前实例）
systemctl --user disable elsky-test-client.service 2>/dev/null

# 删除服务文件
USER_HOME=$HOME
SERVICE_FILE="$USER_HOME/.config/systemd/user/elsky-test-client.service"
if [ -f "$SERVICE_FILE" ]; then
    rm -f "$SERVICE_FILE"
    echo "服务文件已删除"
fi

# 重新加载配置
systemctl --user daemon-reload 2>/dev/null

echo "服务移除完成"
)";

    // 创建临时脚本
    QString tempScript = "/tmp/safe_service_remove.sh";
    QFile scriptFile(tempScript);
    if (scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&scriptFile);
        out << scriptContent;
        scriptFile.close();

        // 设置执行权限
        QFile::setPermissions(tempScript,
            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
            QFile::ReadGroup | QFile::ExeGroup |
            QFile::ReadOther | QFile::ExeOther);

        // 启动独立进程执行移除
        QProcess::startDetached("bash", QStringList() << tempScript);
        emit logMessage("服务移除已在独立进程中启动，不影响当前程序");

        // 定时清理临时脚本 - 修复 lambda 函数中的 qDebug
        QTimer::singleShot(10000, [tempScript]() {
            if (QFile::exists(tempScript)) {
                QFile::remove(tempScript);
                // 使用标准输出替代 qDebug
                // qDebug() << "临时脚本已清理";
            }
        });

        // 更新设置状态
        QSettings settings("ELSKY", "TestClient");
        settings.setValue("client/autoStart", false);

        emit logMessage("✓ 开机自启动已安全取消 - 程序实例保持运行");
    } else {
        emit logMessage("无法创建安全移除脚本，使用传统方法");
        setAutoStart(false);
    }
}

bool AutoStartManager::removeSystemdService()
{
    emit logMessage("安全移除开机启动服务开始 - 使用非侵入式方法");

    QProcess process;
    QString homeDir = QDir::homePath();

    // 方法1: 仅禁用服务，不停止当前实例
    process.start("systemctl", QStringList() << "--user" << "disable" << "elsky-test-client.service");
    process.waitForFinished(2000);

    if (process.exitCode() == 0) {
        emit logMessage("✓ 服务已禁用，当前实例继续运行");
    } else {
        emit logMessage("禁用服务失败，尝试其他方法");
    }

    // 方法2: 异步删除服务文件，避免阻塞 - 修复 lambda 函数
    QTimer::singleShot(0, [homeDir, this]() {  // 捕获 this 指针以使用 emit
        QString serviceFilePath = homeDir + "/.config/systemd/user/elsky-test-client.service";
        if (QFile::exists(serviceFilePath)) {
            if (QFile::remove(serviceFilePath)) {
                emit logMessage("服务文件已删除");
            }
        }

        // 异步重新加载systemd配置
        QProcess::startDetached("systemctl", QStringList() << "--user" << "daemon-reload");
    });

    emit logMessage("开机启动服务已安全处理 - 当前实例不受影响");
    return true;
}

bool AutoStartManager::checkServiceStatus()
{
    QProcess process;
    process.start("systemctl", QStringList() << "--user" << "is-active" << "elsky-test-client.service");

    if (process.waitForFinished(3000)) {
        return process.exitCode() == 0;
    }
    return false;
}

void AutoStartManager::verifyAutoStart()
{
    emit logMessage("验证开机启动设置...");

    QProcess process;
    process.start("systemctl", QStringList() << "--user" << "is-enabled" << "elsky-test-client.service");

    if (process.waitForFinished(3000)) {
        if (process.exitCode() == 0) {
            emit logMessage("✓ 开机启动服务已启用");
            if (autoStartCheckBox) {
                autoStartCheckBox->setChecked(true);
            }

            // 检查服务是否运行
            process.start("systemctl", QStringList() << "--user" << "is-active" << "elsky-test-client.service");
            if (process.waitForFinished(3000)) {
                if (process.exitCode() == 0) {
                    emit logMessage("✓ 开机启动服务运行正常");
                } else {
                    emit logMessage("⚠ 开机启动服务已启用但未运行");
                }
            }
        } else {
            emit logMessage("✗ 开机启动服务未启用");
            if (autoStartCheckBox) {
                autoStartCheckBox->setChecked(false);
            }
        }
    } else {
        emit logMessage("✗ 无法检查开机启动服务状态");
        if (autoStartCheckBox) {
            autoStartCheckBox->setChecked(false);
        }
    }
}
