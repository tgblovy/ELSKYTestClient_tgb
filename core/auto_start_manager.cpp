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
#ifdef Q_OS_WIN
#include <windows.h>  // Windows平台使用 GetCurrentProcessId 等
#else
#include <unistd.h>   // Linux/Unix 平台用于 getuid()
#endif

AutoStartManager::AutoStartManager(QObject *parent)
    : QObject(parent)
    , autoStartCheckBox(nullptr)
    , isCancellingAutoStart(false)
    , verifyRetryCount(0)
    , isSystemVerification(true)  // 默认为系统验证
    , m_cycleRunning(false)       // 默认循环未运行
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

    // 延迟启动时检查用户意图标识（核心新增逻辑）
    QTimer::singleShot(500, this, [this]() {
        checkUserIntentOnStartup();
    });
}

// 新增：获取用户意图标识
// 该标识独立于 client/autoStart，用于记录用户"曾主动勾选过"开机自启
// 只有用户手动取消才会清除，重启不会清除
bool AutoStartManager::getUserIntentFlag()
{
    QSettings settings("ELSKY", "TestClient");
    return settings.value("autoStart_userIntent", false).toBool();
}

// 新增：设置/清除用户意图标识
void AutoStartManager::setUserIntentFlag(bool set)
{
    QSettings settings("ELSKY", "TestClient");
    settings.setValue("autoStart_userIntent", set);
    emit logMessage(QString("用户意图标识已%1").arg(set ? "设置（开机自启主动启用）" : "清除（用户手动取消）"));
}

// 新增：启动时检查用户意图标识
// 逻辑：
//   1. 如果用户意图标识为 true（曾主动勾选过）
//      a. 但界面复选框未勾选 → 自动勾选（这会触发 onAutoStartToggled）
//      b. 复选框已勾选但服务文件缺失 → 自动重建服务
//   2. 如果用户意图标识为 false → 尊重用户选择，不做任何操作
void AutoStartManager::checkUserIntentOnStartup()
{
    bool userIntent = getUserIntentFlag();
    emit logMessage(QString("=== 启动时检查用户意图标识: %1 ===")
                   .arg(userIntent ? "存在（曾主动启用）" : "不存在（用户未启用或已取消）"));

    if (!userIntent) {
        // 用户从未主动启用过，或已主动取消 → 不做任何操作
        emit logMessage("用户意图标识不存在，跳过自动检查");
        return;
    }

    // 用户意图存在，检查界面复选框状态
    bool checkBoxChecked = autoStartCheckBox ? autoStartCheckBox->isChecked() : false;
    emit logMessage(QString("界面复选框状态: %1").arg(checkBoxChecked ? "已勾选" : "未勾选"));

    if (!checkBoxChecked) {
        // 场景A: 用户意图存在但界面未勾选 → 自动勾选
        // 这可能是因为配置文件被部分清除，或系统重置了 UI 状态
        emit logMessage("⚠ 检测到: 用户意图标识存在但界面未勾选");
        emit logMessage("→ 自动勾选开机启动并验证服务...");

        // 设置标记为系统自动验证，避免触发"用户操作"的逻辑
        isSystemVerification = true;
        verifyRetryCount = 0;

        // 静默勾选复选框（这会触发 onAutoStartToggled → setAutoStart(true)）
        if (autoStartCheckBox) {
            autoStartCheckBox->blockSignals(false);  // 确保信号能触发
            autoStartCheckBox->setChecked(true);
        }

        // onAutoStartToggled 会处理服务创建和状态保存
        return;
    }

    // 场景B: 用户意图存在且界面已勾选 → 检查服务文件是否存在
    emit logMessage("用户意图存在且界面已勾选，验证服务文件...");
    isSystemVerification = true;
    verifyRetryCount = 0;
    verifyAutoStart();
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
    // 标记为用户主动操作（非系统验证）
    isSystemVerification = false;

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

            // 关键新增: 设置用户意图标识（持久化，重启不会清除）
            setUserIntentFlag(true);

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
        // 关键新增: 用户手动取消 → 清除用户意图标识
        setUserIntentFlag(false);

        // 关键修复：使用最安全的取消方法
        safestRemoveSystemdService();
        emit autoStartStatusChanged(false);  // 触发状态变化信号
    }
}

// 生成动态环境 launcher 脚本（见 createSystemdService 内注释）
// 返回空串表示生成失败（调用方回退为直接 ExecStart 二进制，保持旧行为）
QString AutoStartManager::createLauncherScript(const QString &appPath)
{
    QString configDir = QDir::homePath() + "/.config/ELSKY";
    QDir().mkpath(configDir);
    QString scriptPath = configDir + "/elsky-autostart.sh";

    // 脚本逻辑（关键设计：所有"未找到"的分支都保持原环境不变 →
    // 麒麟/统信等已验证平台行为与旧版完全一致；只有发现更准确的值才覆盖）
    QString script = QString(
        "#!/bin/bash\n"
        "# ELSKY 开机自启 launcher（自动生成，请勿手工编辑）\n"
        "# 等待桌面会话导入显示环境后启动主程序\n"
        "APP=\"%1\"\n"
        "DISP=\"\"\n"
        "WL=\"\"\n"
        "# 最长等待 60 秒：桌面会话(gnome-session/UKUI/DDE)登录后会把\n"
        "# DISPLAY/WAYLAND_DISPLAY import 进 systemd 用户环境管理器\n"
        "for i in $(seq 1 60); do\n"
        "    SYSENV=$(systemctl --user show-environment 2>/dev/null)\n"
        "    DISP=$(echo \"$SYSENV\" | sed -n 's/^DISPLAY=//p' | head -n1)\n"
        "    WL=$(echo \"$SYSENV\" | sed -n 's/^WAYLAND_DISPLAY=//p' | head -n1)\n"
        "    if [ -n \"$DISP\" ] || [ -n \"$WL\" ]; then break; fi\n"
        "    sleep 1\n"
        "done\n"
        "# 用会话真实值覆盖（仅非空时覆盖；空则保留 service 传入的旧值）\n"
        "if [ -n \"$DISP\" ]; then export DISPLAY=\"$DISP\"; fi\n"
        "if [ -n \"$WL\" ]; then export WAYLAND_DISPLAY=\"$WL\"; fi\n"
        "# 关键：与桌面启动脚本 elsky-test-client.sh 完全一致，强制 xcb 平台。\n"
        "# 桌面图标启动（强制 xcb）已验证可用；systemd 路径之前没强制，Qt 可能\n"
        "# 选中打包内来自编译机的 wayland 插件——其协议版本不被 Ubuntu 24.04\n"
        "# GNOME 支持 → 窗口创建成功但永远不被显示（进程活着、循环继续）。\n"
        "# 刻意不导出 XDG_SESSION_TYPE，避免任何引导 Qt 选 wayland 的提示。\n"
        "export QT_QPA_PLATFORM=xcb\n"
        "# XAUTHORITY 解析（带重试）：\n"
        "#   传统 X11 桌面(麒麟/统信) → ~/.Xauthority（立即命中，零等待）\n"
        "#   GNOME Wayland 的 XWayland → $XDG_RUNTIME_DIR/.mutter-Xwaylandauth.*\n"
        "#     （XWayland 按需启动时鉴权文件晚几秒出现 → 轮询最长 30 秒）\n"
        "if [ -z \"$XAUTHORITY\" ] || [ ! -f \"$XAUTHORITY\" ]; then\n"
        "    for i in $(seq 1 30); do\n"
        "        for f in \"$HOME\"/.Xauthority \\\n"
        "                 \"$XDG_RUNTIME_DIR\"/.mutter-Xwaylandauth.* \\\n"
        "                 \"$XDG_RUNTIME_DIR\"/xauth_*; do\n"
        "            if [ -f \"$f\" ]; then export XAUTHORITY=\"$f\"; break 2; fi\n"
        "        done\n"
        "        sleep 1\n"
        "    done\n"
        "fi\n"
        "# 记录最终启动环境（journalctl --user -u elsky-test-client.service -b 可查）\n"
        "echo \"[elsky-launcher] DISPLAY=$DISPLAY WAYLAND_DISPLAY=$WAYLAND_DISPLAY QT_QPA_PLATFORM=$QT_QPA_PLATFORM\"\n"
        "echo \"[elsky-launcher] XAUTHORITY=$XAUTHORITY\"\n"
        "# 注入程序自带库/插件路径（与桌面图标启动的 elsky-test-client.sh 行为一致：\n"
        "# 自带 Qt 库与插件版本完全匹配，避免连到系统 Qt 插件导致 ABI 不匹配）\n"
        "APPDIR=$(dirname \"$APP\")\n"
        "if [ -d \"$APPDIR/lib\" ]; then\n"
        "    export LD_LIBRARY_PATH=\"$APPDIR/lib:$LD_LIBRARY_PATH\"\n"
        "    export QT_PLUGIN_PATH=\"$APPDIR/lib:${QT_PLUGIN_PATH}\"\n"
        "fi\n"
        "exec \"$APP\"\n"
    ).arg(appPath);

    QFile f(scriptPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit logMessage("警告: 无法生成 launcher 脚本: " + scriptPath);
        return QString();
    }
    f.write(script.toUtf8());
    f.close();
    // 755 权限（所有者可执行）
    QFile::setPermissions(scriptPath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
        QFileDevice::ReadGroup | QFileDevice::ExeGroup |
        QFileDevice::ReadOther | QFileDevice::ExeOther);
    emit logMessage("已生成自启 launcher: " + scriptPath);
    return scriptPath;
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

    // 检测程序目录下是否带打包的动态库（deb 安装到 /usr/lib/elsky-test-client/lib）
    // systemd 服务启动没有 LD_LIBRARY_PATH，必须显式注入，否则开机自启时找不到
    // Qt/GL/串口等库直接崩溃，表现为"开机启动失效"
    QString appDir = QCoreApplication::applicationDirPath();
    QString libDir = appDir + "/lib";
    QString libEnvLine;
    if (QDir(libDir).exists()) {
        libEnvLine = QString("Environment=LD_LIBRARY_PATH=%1\n").arg(libDir);
        emit logMessage("检测到程序自带动态库目录，开机启动将注入: LD_LIBRARY_PATH=" + libDir);
    }

    // 设计说明（避免回归）：
    //   - 5 秒 sleep + launcher 轮询等待（环境 60s + XAUTHORITY 30s）已解决
    //     启动时序问题；Restart 必须为 no：
    //     on-failure 会把"用户手动关闭软件"（信号/非零退出码）判定为失败，
    //     10 秒后自动重启 → 界面又弹出（用户关闭后被"复活"的 bug）
    // ---- 生成动态环境 launcher（等待桌面会话导入显示环境 + 定位 XAUTHORITY）----
    // Ubuntu/GNOME 下 systemd --user 服务早于桌面会话导入显示环境启动；
    // launcher 轮询真实 DISPLAY/WAYLAND_DISPLAY 并动态定位 X11 鉴权文件。
    // 麒麟/统信（X11）第 1 次轮询即命中，行为与旧版一致。
    QString launcherPath = createLauncherScript(appPath);
    QString execTarget = launcherPath.isEmpty() ? appPath : launcherPath;

    QString serviceContent = QString(
        "[Unit]\n"
        "Description=ELSKY Test Client\n"
        "After=graphical-session.target\n"
        "After=network.target\n"
        "Wants=graphical-session.target\n"
        "\n"
        "[Service]\n"
        "Type=simple\n"
        "ExecStartPre=/bin/sleep 5\n"
        "ExecStart=%1\n"
        "WorkingDirectory=%2\n"
        "Environment=DISPLAY=:0\n"
        "Environment=XAUTHORITY=%4/.Xauthority\n"
        "Environment=XDG_RUNTIME_DIR=/run/user/%3\n"
        "%5"
        "Restart=no\n"
        "TimeoutSec=30\n"
        "\n"
        "[Install]\n"
        "WantedBy=default.target\n"
    ).arg(execTarget, homeDir,
#ifdef Q_OS_WIN
          QString::number(1000),  // Windows 下使用默认值（systemd服务仅Linux）
#else
          QString::number(getuid()),
#endif
          homeDir,
          libEnvLine
         );

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

    // 关键修复: 不再在 createSystemdService 内部触发 verifyAutoStart
    // 原因: 这会导致 verifyAutoStart -> createSystemdService -> verifyAutoStart 的无限递归
    // 验证由调用者自行决定是否触发
    if (m_cycleRunning) {
        emit logMessage("[循环保护] 检测到循环正在运行，跳过后续验证");
    } else {
        emit logMessage("服务已创建,验证将由调用方触发(避免递归)");
    }

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

# 清理 launcher 脚本（开机启动的辅助脚本，随服务一并移除）
LAUNCHER_FILE="$USER_HOME/.config/ELSKY/elsky-autostart.sh"
if [ -f "$LAUNCHER_FILE" ]; then
    rm -f "$LAUNCHER_FILE"
    echo "launcher 脚本已清理"
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

    // ========== 检查服务文件是否存在 ==========
    QString homeDir = QDir::homePath();
    QString serviceFilePath = homeDir + "/.config/systemd/user/elsky-test-client.service";

    bool fileExists = QFile::exists(serviceFilePath);
    emit logMessage(QString("诊断-文件检查: %1 %2")
                   .arg(serviceFilePath)
                   .arg(fileExists ? "✓ 存在" : "✗ 不存在"));

    if (!fileExists) {
        emit logMessage("⚠ 服务文件不存在！");

        if (isSystemVerification) {
            // 检查用户是否真的想要启用开机自启
            QSettings settings("ELSKY", "TestClient");
            bool userWantsAutoStart = settings.value("client/autoStart", false).toBool();

            if (!userWantsAutoStart) {
                // 用户未勾选,直接返回,不重试不重建
                emit logMessage("用户设置: 未启用开机自启，跳过验证");
                if (autoStartCheckBox) {
                    bool wasBlocked = autoStartCheckBox->signalsBlocked();
                    autoStartCheckBox->blockSignals(true);
                    autoStartCheckBox->setChecked(false);
                    autoStartCheckBox->blockSignals(wasBlocked);
                }
                isSystemVerification = false;
                return;
            }

            // 用户已勾选,直接尝试重建服务（不重试,只做一次）
            emit logMessage("用户已启用开机自启，尝试重建服务...");
            bool recreateSuccess = createSystemdService();

            if (recreateSuccess) {
                emit logMessage("✅ 开机启动服务已重建");
                if (autoStartCheckBox) {
                    bool wasBlocked = autoStartCheckBox->signalsBlocked();
                    autoStartCheckBox->blockSignals(true);
                    autoStartCheckBox->setChecked(true);
                    autoStartCheckBox->blockSignals(wasBlocked);
                }
            } else {
                emit logMessage("❌ 服务重建失败，请手动检查");
                if (autoStartCheckBox) {
                    bool wasBlocked = autoStartCheckBox->signalsBlocked();
                    autoStartCheckBox->blockSignals(true);
                    autoStartCheckBox->setChecked(false);
                    autoStartCheckBox->blockSignals(wasBlocked);
                }
            }
            isSystemVerification = false;
            return;
        }
    }

    // ========== 检查服务是否已启用（单次检测，不重试） ==========
    QProcess process;
    process.start("systemctl", QStringList() << "--user" << "is-enabled" << "elsky-test-client.service");

    if (process.waitForFinished(3000)) {
        QString error = process.readAllStandardError().trimmed();

        if (process.exitCode() == 0) {
            // 验证成功
            emit logMessage("✓ 开机启动服务已启用");
            if (autoStartCheckBox) {
                bool wasBlocked = autoStartCheckBox->signalsBlocked();
                autoStartCheckBox->blockSignals(true);
                autoStartCheckBox->setChecked(true);
                autoStartCheckBox->blockSignals(wasBlocked);
            }
        } else {
            // 验证失败 - 尝试修复（单次,不重试）
            emit logMessage(QString("⚠ 服务未启用: %1，尝试修复...").arg(error));

            QProcess fixProcess;
            fixProcess.start("systemctl", QStringList() << "--user" << "daemon-reload");
            fixProcess.waitForFinished(3000);

            fixProcess.start("systemctl", QStringList() << "--user" << "enable" << "elsky-test-client.service");
            fixProcess.waitForFinished(5000);

            if (fixProcess.exitCode() == 0) {
                emit logMessage("✅ 服务已重新启用");
                if (autoStartCheckBox) {
                    bool wasBlocked = autoStartCheckBox->signalsBlocked();
                    autoStartCheckBox->blockSignals(true);
                    autoStartCheckBox->setChecked(true);
                    autoStartCheckBox->blockSignals(wasBlocked);
                }
            } else {
                emit logMessage("❌ 服务启用失败: " + fixProcess.readAllStandardError().trimmed());
                if (autoStartCheckBox) {
                    bool wasBlocked = autoStartCheckBox->signalsBlocked();
                    autoStartCheckBox->blockSignals(true);
                    autoStartCheckBox->setChecked(false);
                    autoStartCheckBox->blockSignals(wasBlocked);
                }
            }
        }
    } else {
        emit logMessage("✗ 无法检查开机启动服务状态（命令超时）");
    }

    isSystemVerification = false;
}

// ========== 新增：循环状态感知方法 ==========

void AutoStartManager::setCycleRunning(bool running)
{
    bool changed = (m_cycleRunning != running);
    m_cycleRunning = running;

    if (changed) {
        emit logMessage(QString("[循环保护] 循环状态变更: %1")
                       .arg(running ? "▶ 运行中" : "■ 已停止"));

        if (running) {
            emit logMessage("[循环保护] 进入静默模式：所有服务操作将不会中断循环任务");
        } else {
            emit logMessage("[循环保护] 退出静默模式：恢复正常服务管理模式");
        }
    }
}

bool AutoStartManager::isCycleRunning() const
{
    return m_cycleRunning;
}
