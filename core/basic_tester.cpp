#include "basic_tester.h"
#include "system_controller.h"
#include "../dialogs/com_test_dialog.h"
#include "../dialogs/usb_speed_dialog.h"
#include "network_speed_tester.h"
#include "privilege_helper.h"
#include "benchmark_tester.h"

#include <QMessageBox>
#include <QApplication>
#include <QProgressDialog>
#include <QCoreApplication>
#include <QDebug>
#include <QProcess>
#include <QTimer>
#include <QThread>
#include <QFile>
#include <QSysInfo>
#include <QDir>        // 添加这行
#include <QFileInfo>   // 添加这行
#include <QTextStream> // 添加这行
#include <QProcessEnvironment>  // 新增：环境变量管理
#include <QInputDialog>
#include <QLineEdit>
#include <QLibrary>
#include <QDialog>
#include <QCheckBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <utime.h>

BasicTester::BasicTester(QObject *parent)
    : QObject(parent)
    , basicTestCombo(nullptr)
    , executeButton(nullptr)
{
}

void BasicTester::setupUI(QComboBox *combo, QPushButton *button)
{
    basicTestCombo = combo;
    executeButton = button;

    connect(executeButton, &QPushButton::clicked, this, &BasicTester::executeTest);
}

void BasicTester::executeTest()
{
    if (!basicTestCombo) return;

    QString selectedTest = basicTestCombo->currentText();
    emit logMessage(QString("执行基础测试: %1").arg(selectedTest));

    executeTestByName(selectedTest);
}

void BasicTester::executeTestByName(const QString &testName)
{
    emit logMessage(QString("执行基础测试: %1").arg(testName));

    if (testName == "关机") {
        onShutdown();
    } else if (testName == "重启") {
        onReboot();
    } else if (testName == "睡眠") {
        onSuspend();
    } else if (testName == "休眠") {
        onHibernate();
    } else if (testName == "COM测试") {
        onComTest();
    } else if (testName == "U盘测速") {
        onUsbSpeedTest();
    } else if (testName == "网络测速") {
        onNetworkSpeedTest();
    } else if (testName == "老化测试") {
        onAgingTest();
    } else if (testName == "跑分测试") {
        onBenchmarkTest();
    } else if (testName == "老化测试(自动)") {
        onAgingTestAutoStart();
    } else if (testName == "统一控制") {
        emit unifiedControlTriggered();
    }
}

// ---- [电源四项无反应修复] 2026-10-14 ----
// 根因：V2.2/免密提权改造后，shutdown/reboot/suspend/hibernate 走
//   SystemController::executePowerCommandWithFallback() → runPrivilegedSilent()
//   （免密 sudo → 记忆密码）→ 回退普通执行（polkit 拒绝）。
//   当目标机 sudoers 免密未配置且无记忆密码时整条链静默失败，
//   而旧的槽函数不检查返回值 → UI 无任何反馈，表现为"点击执行没反应"。
// 修复：失败时先交互建立提权凭据（密码弹窗，成功后记忆，仅手动点击场景；
//       循环控制路径直接调 SystemController 静态函数，不经此处，行为不变），
//       再重发一次原操作（此时静默提权链可用）；仍失败则日志 + 弹窗明确告知。
void BasicTester::onShutdown()
{
    emit logMessage("执行关机操作");
    if (SystemController::shutdown())
        return;
    emit logMessage("关机命令未生效（权限不足），尝试交互提权重试...");
    QString err;
    if (PrivilegeHelper::ensureAuthForPrivilege(&err)) {
        if (SystemController::shutdown())
            return;
        err = "提权后命令仍被拒绝";
    }
    const QString msg = QString(
        "关机操作执行失败：%1\n\n"
        "请配置免密 sudo（/etc/sudoers 中为本程序加 NOPASSWD）或以管理员身份运行。").arg(err);
    emit logMessage("关机失败: " + msg);
    QMessageBox::warning(nullptr, "关机失败", msg);
}

void BasicTester::onReboot()
{
    emit logMessage("执行重启操作");
    if (SystemController::reboot())
        return;
    emit logMessage("重启命令未生效（权限不足），尝试交互提权重试...");
    QString err;
    if (PrivilegeHelper::ensureAuthForPrivilege(&err)) {
        if (SystemController::reboot())
            return;
        err = "提权后命令仍被拒绝";
    }
    const QString msg = QString(
        "重启操作执行失败：%1\n\n"
        "请配置免密 sudo（/etc/sudoers 中为本程序加 NOPASSWD）或以管理员身份运行。").arg(err);
    emit logMessage("重启失败: " + msg);
    QMessageBox::warning(nullptr, "重启失败", msg);
}

void BasicTester::onSuspend()
{
    emit logMessage("执行睡眠操作");
    if (SystemController::suspend())
        return;
    emit logMessage("睡眠命令未生效（权限不足），尝试交互提权重试...");
    QString err;
    if (PrivilegeHelper::ensureAuthForPrivilege(&err)) {
        if (SystemController::suspend())
            return;
        err = "提权后命令仍被拒绝";
    }
    const QString msg = QString(
        "睡眠操作执行失败：%1\n\n"
        "请配置免密 sudo 或以管理员身份运行。").arg(err);
    emit logMessage("睡眠失败: " + msg);
    QMessageBox::warning(nullptr, "睡眠失败", msg);
}

void BasicTester::onHibernate()
{
    emit logMessage("执行休眠操作");
    if (SystemController::hibernate())
        return;
    emit logMessage("休眠命令未生效（权限不足），尝试交互提权重试...");
    QString err;
    if (PrivilegeHelper::ensureAuthForPrivilege(&err)) {
        if (SystemController::hibernate())
            return;
        err = "提权后命令仍被拒绝";
    }
    const QString msg = QString(
        "休眠操作执行失败：%1\n\n"
        "休眠还需要系统交换分区（swap）大于物理内存，请确认 swap 已配置。").arg(err);
    emit logMessage("休眠失败: " + msg);
    QMessageBox::warning(nullptr, "休眠失败", msg);
}

void BasicTester::onComTest()
{
    // 第一步：检查程序是否编译了串口支持
    static bool firstTime = true;

    if (firstTime) {
        firstTime = false;

        // 尝试直接创建COM测试对话框，这会检查编译时是否支持
        #ifdef QT_SERIALPORT_LIB
        emit logMessage("程序已编译串口支持");
        #else
        emit logMessage("警告: 程序未编译串口支持");

        // 询问用户是否重新配置和编译
        QMessageBox msgBox;
        msgBox.setWindowTitle("需要重新配置");
        msgBox.setText("此程序编译时未包含串口支持。\n\n是否要重新配置并编译程序？");
        msgBox.setInformativeText("这将修改项目文件并重新编译程序。");
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);
        msgBox.setWindowModality(Qt::ApplicationModal);

        if (msgBox.exec() == QMessageBox::Yes) {
            // 重新配置项目文件并编译
            reconfigureAndCompile();
            return;
        }
        #endif
    }

    // 第二步：检查运行时库是否安装
    bool hasRuntimeLibrary = checkSerialPortRuntimeLibrary();

    if (!hasRuntimeLibrary) {
        // 库未安装，询问用户
        QMessageBox msgBox;
        msgBox.setWindowTitle("安装库");
        msgBox.setText("需要安装QtSerialPort运行时库。\n是否立即安装？");
        msgBox.setInformativeText("安装后即可使用COM测试功能。");
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);

        if (msgBox.exec() == QMessageBox::Yes) {
            installQtSerialPortLibrary();
        }
        return;
    }

    // 第三步：一切正常，打开COM测试对话框
    #ifdef QT_SERIALPORT_LIB
    emit logMessage("打开COM端口测试对话框");
    ComTestDialog *dialog = new ComTestDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    // 改为非模态窗口，支持同时打开多个串口测试窗口
    dialog->setWindowModality(Qt::NonModal);
    // [2026-11-05] 窗口标志统一由 ComTestDialog 构造函数设为 Qt::Window
    //（独立顶层窗口）。此处不能再 OR 上 Qt::Dialog——那会把窗口类型改回对话框，
    // Qt 又会写入 WM_TRANSIENT_FOR，麒麟/统信窗口管理器在主界面最小化时
    // 仍会把这一组 COM 窗口一起最小化。
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    #endif
}

// 检查运行时库的函数
bool BasicTester::checkSerialPortRuntimeLibrary()
{
    // 优先：实际加载测试（LD_LIBRARY_PATH 生效时，程序自带 lib/ 目录中的库也能加载成功）
    QLibrary serialLib("Qt5SerialPort");
    if (serialLib.load()) {
        serialLib.unload();
        emit logMessage("串口库可正常加载");
        return true;
    }

    // 检查QtSerialPort库文件是否存在（系统路径 + 程序自带 lib/ 目录）
    QStringList possiblePaths = {
        "/usr/lib/x86_64-linux-gnu/libQt5SerialPort.so",
        "/usr/lib/x86_64-linux-gnu/libQt5SerialPort.so.5",
        "/usr/lib/x86_64-linux-gnu/libQt5SerialPort.so.5.15",
        "/usr/lib/aarch64-linux-gnu/libQt5SerialPort.so",
        "/usr/lib/aarch64-linux-gnu/libQt5SerialPort.so.5",
        "/usr/lib/libQt5SerialPort.so",
        "/usr/local/lib/libQt5SerialPort.so",
        QCoreApplication::applicationDirPath() + "/libQt5SerialPort.so",
        // deb 安装自带库目录（启动脚本已设 LD_LIBRARY_PATH 指向这里）
        QCoreApplication::applicationDirPath() + "/lib/libQt5SerialPort.so.5",
        QCoreApplication::applicationDirPath() + "/lib/libQt5SerialPort.so"
    };

    for (const QString &path : possiblePaths) {
        if (QFile::exists(path)) {
            emit logMessage(QString("找到串口库: %1").arg(path));
            return true;
        }
    }

    // 尝试使用ldconfig检查
    QProcess checkProcess;
    checkProcess.start("bash", QStringList() << "-c" << "ldconfig -p | grep -i qt5serialport 2>/dev/null || echo ''");
    checkProcess.waitForFinished(2000);
    QString output = checkProcess.readAllStandardOutput();

    if (!output.trimmed().isEmpty()) {
        emit logMessage("通过ldconfig找到串口库");
        return true;
    }

    emit logMessage("未找到串口运行时库");
    return false;
}

// 重新配置和编译的函数
void BasicTester::reconfigureAndCompile()
{
    emit logMessage("开始重新配置和编译程序...");

    QProgressDialog *progressDialog = new QProgressDialog();
    progressDialog->setWindowTitle("重新配置和编译");
    progressDialog->setLabelText("正在修改项目配置并重新编译程序...");
    progressDialog->setCancelButtonText("取消");
    progressDialog->setRange(0, 0);
    progressDialog->setModal(true);
    progressDialog->show();

    QCoreApplication::processEvents();

    // 在新线程中执行
    QThread *thread = QThread::create([this, progressDialog]() {
        bool success = false;
        QString errorMsg;

        try {
            // 1. 查找项目文件
            QString proFile = findProjectFile();
            if (proFile.isEmpty()) {
                errorMsg = "未找到项目文件(.pro)";
                emit logMessage(errorMsg);
                success = false;
            } else {
                emit logMessage(QString("找到项目文件: %1").arg(proFile));

                // 2. 修改项目文件，添加serialport模块
                if (addSerialPortToProjectFile(proFile)) {
                    emit logMessage("项目文件修改成功");

                    // 3. 重新编译
                    if (recompileProject(proFile)) {
                        emit logMessage("重新编译成功");
                        success = true;
                    } else {
                        errorMsg = "重新编译失败";
                        emit logMessage(errorMsg);
                    }
                } else {
                    errorMsg = "修改项目文件失败";
                    emit logMessage(errorMsg);
                }
            }
        } catch (const std::exception &e) {
            errorMsg = QString("异常: %1").arg(e.what());
            emit logMessage(errorMsg);
        }

        // 关闭进度对话框
        QMetaObject::invokeMethod(progressDialog, "close", Qt::QueuedConnection);
        QMetaObject::invokeMethod(progressDialog, "deleteLater", Qt::QueuedConnection);

        // 显示结果
        QMetaObject::invokeMethod(qApp, [success, errorMsg]() {
            if (success) {
                QMessageBox::information(nullptr, "成功",
                    "程序已重新配置并编译成功。\n\n"
                    "请重新启动程序以使用COM测试功能。");
            } else {
                QMessageBox::warning(nullptr, "失败",
                    QString("重新配置编译失败: %1\n\n"
                    "请手动修改项目文件(.pro)，添加: QT += serialport\n"
                    "然后重新编译程序。").arg(errorMsg));
            }
        }, Qt::QueuedConnection);
    });

    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();

    connect(progressDialog, &QProgressDialog::canceled, [thread, progressDialog]() {
        if (thread && thread->isRunning()) {
            thread->terminate();
            thread->wait();
        }
        if (progressDialog) {
            progressDialog->deleteLater();
        }
    });
}

// 查找项目文件
QString BasicTester::findProjectFile()
{
    // 在当前目录和上级目录中查找.pro文件
    QDir currentDir = QDir::current();

    // 检查当前目录
    QStringList proFiles = currentDir.entryList(QStringList() << "*.pro", QDir::Files);
    if (!proFiles.isEmpty()) {
        return currentDir.absoluteFilePath(proFiles.first());
    }

    // 检查上级目录（常见于build目录）
    currentDir.cdUp();
    proFiles = currentDir.entryList(QStringList() << "*.pro", QDir::Files);
    if (!proFiles.isEmpty()) {
        return currentDir.absoluteFilePath(proFiles.first());
    }

    // 检查程序所在目录
    QString appPath = QCoreApplication::applicationDirPath();
    QDir appDir(appPath);
    proFiles = appDir.entryList(QStringList() << "*.pro", QDir::Files);
    if (!proFiles.isEmpty()) {
        return appDir.absoluteFilePath(proFiles.first());
    }

    return "";
}

// 向项目文件添加serialport模块
bool BasicTester::addSerialPortToProjectFile(const QString &proFile)
{
    QFile file(proFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit logMessage(QString("无法打开项目文件: %1").arg(proFile));
        return false;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    // 检查是否已有serialport
    if (content.contains("QT +=") && content.contains("serialport")) {
        emit logMessage("项目文件已包含serialport模块");
        return true;
    }

    // 查找QT +=行
    QStringList lines = content.split("\n");
    for (int i = 0; i < lines.size(); i++) {
        if (lines[i].trimmed().startsWith("QT +=")) {
            // 检查是否已有serialport
            if (!lines[i].contains("serialport")) {
                lines[i] += " serialport";
                emit logMessage(QString("修改行: %1").arg(lines[i]));
            }
            break;
        }
    }

    // 如果没有找到QT +=行，添加一行
    bool found = false;
    for (int i = 0; i < lines.size(); i++) {
        if (lines[i].trimmed().startsWith("QT +=")) {
            found = true;
            break;
        }
    }

    if (!found) {
        // 在文件开头添加QT行
        lines.prepend("QT += core gui serialport");
    }

    // 保存修改
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        emit logMessage("无法写入项目文件");
        return false;
    }

    QTextStream out(&file);
    out << lines.join("\n");
    file.close();

    emit logMessage("项目文件修改完成");
    return true;
}

// 重新编译项目
bool BasicTester::recompileProject(const QString &proFile)
{
    QDir proDir = QFileInfo(proFile).absoluteDir();
    QString dirPath = proDir.absolutePath();

    // 1. qmake
    QProcess qmakeProcess;
    qmakeProcess.setWorkingDirectory(dirPath);
    qmakeProcess.start("qmake", QStringList() << proFile);

    emit logMessage("运行qmake...");

    if (!qmakeProcess.waitForStarted(5000) || !qmakeProcess.waitForFinished(30000)) {
        emit logMessage("qmake执行失败");
        return false;
    }

    if (qmakeProcess.exitCode() != 0) {
        emit logMessage(QString("qmake失败: %1").arg(QString::fromUtf8(qmakeProcess.readAllStandardError())));
        return false;
    }

    emit logMessage("qmake成功");

    // 2. make
    QProcess makeProcess;
    makeProcess.setWorkingDirectory(dirPath);
    makeProcess.start("make", QStringList());

    emit logMessage("运行make...");

    if (!makeProcess.waitForStarted(5000) || !makeProcess.waitForFinished(300000)) { // 5分钟超时
        emit logMessage("make执行失败");
        return false;
    }

    if (makeProcess.exitCode() != 0) {
        emit logMessage(QString("make失败: %1").arg(QString::fromUtf8(makeProcess.readAllStandardError())));
        return false;
    }

    emit logMessage("make成功");
    return true;
}

void BasicTester::installQtSerialPortLibrary()
{
    emit logMessage("开始安装QtSerialPort库...");

    // 创建安装进度对话框
    QProgressDialog *progressDialog = new QProgressDialog();
    progressDialog->setWindowTitle("安装库");
    progressDialog->setLabelText("正在检测系统类型并安装QtSerialPort库...");
    progressDialog->setCancelButtonText("取消");
    progressDialog->setRange(0, 0);
    progressDialog->setMinimumDuration(0);
    progressDialog->setModal(true);
    progressDialog->show();

    QCoreApplication::processEvents();

    // 在新线程中执行安装
    QThread *installThread = QThread::create([this, progressDialog]() {
        QString osType = QSysInfo::productType().toLower();
        QString installCommand;
        bool success = false;
        QString errorMsg;

        qDebug() << "检测到操作系统类型:" << osType;

        // 根据操作系统类型选择安装命令
        if (osType.contains("ubuntu") || osType.contains("debian")) {
            installCommand = "apt-get update && apt-get install -y libqt5serialport5 libqt5serialport5-dev";
        } else if (osType.contains("centos") || osType.contains("rhel") || osType.contains("redhat")) {
            installCommand = "yum install -y qt5-qtserialport qt5-qtserialport-devel";
        } else if (osType.contains("fedora")) {
            installCommand = "dnf install -y qt5-qtserialport qt5-qtserialport-devel";
        } else if (osType.contains("arch") || osType.contains("manjaro")) {
            installCommand = "pacman -S --noconfirm qt5-serialport";
        } else if (osType.contains("opensuse") || osType.contains("suse")) {
            installCommand = "zypper install -y libQt5SerialPort5 libQt5SerialPort5-devel";
        } else {
            installCommand = "apt-get update && apt-get install -y libqt5serialport5 libqt5serialport5-dev";
        }

        qDebug() << "执行安装命令:" << installCommand;

        // 使用统一权限模块执行：优先免密，其次记忆密码（均非交互，线程安全）
        QString errOut;
        success = PrivilegeHelper::runPrivilegedSilent(installCommand, &errOut);
        if (!success) {
            errorMsg = errOut.isEmpty() ? "无免密权限且无记忆密码" : errOut;
            qDebug() << errorMsg;
        }

        // 发送安装完成信号
        emit logMessage(success ? "QtSerialPort库安装成功！" : QString("安装失败: %1").arg(errorMsg));

        // 关闭进度对话框
        QMetaObject::invokeMethod(progressDialog, "close", Qt::QueuedConnection);
        QMetaObject::invokeMethod(progressDialog, "deleteLater", Qt::QueuedConnection);

        // 显示安装结果（失败时回到主线程弹密码框重试）
        QMetaObject::invokeMethod(qApp, [this, installCommand, success, errorMsg]() {
            if (success) {
                QMessageBox::information(nullptr, "安装成功",
                    "QtSerialPort库已成功安装。\n\n"
                    "注意：您需要重新编译并运行此程序才能使用COM测试功能。");
            } else {
                // 无免密/无记忆密码时，主线程弹密码框重试一次
                QString retryErr;
                bool retryOk = PrivilegeHelper::runPrivileged(installCommand, &retryErr);
                if (retryOk) {
                    QMessageBox::information(nullptr, "安装成功",
                        "QtSerialPort库已成功安装。\n\n"
                        "注意：您需要重新编译并运行此程序才能使用COM测试功能。");
                } else {
                    QMessageBox::warning(nullptr, "安装失败",
                        QString("安装QtSerialPort库失败。\n\n错误信息：%1\n\n"
                        "您可以尝试手动安装：\n"
                        "Ubuntu/Debian: sudo apt-get install libqt5serialport5 libqt5serialport5-dev\n"
                        "然后重新编译程序。").arg(retryErr.isEmpty() ? errorMsg : retryErr));
                }
            }
        }, Qt::QueuedConnection);
    });

    // 连接线程完成信号
    connect(installThread, &QThread::finished, installThread, &QThread::deleteLater);

    // 启动线程
    installThread->start();

    // 连接取消按钮
    connect(progressDialog, &QProgressDialog::canceled, [installThread, progressDialog]() {
        if (installThread && installThread->isRunning()) {
            installThread->terminate();
            installThread->wait();
        }
        if (progressDialog) {
            progressDialog->deleteLater();
        }
    });
}

void BasicTester::onUsbSpeedTest()
{
    emit logMessage("打开U盘速度测试对话框");
    UsbSpeedTestDialog *dialog = new UsbSpeedTestDialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->setWindowFlags(dialog->windowFlags() | Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void BasicTester::onNetworkSpeedTest()
{
    emit logMessage("开始网络速度测试...");

    // 创建进度对话框
    QProgressDialog *progressDialog = new QProgressDialog();
    progressDialog->setWindowTitle("网络测速");
    progressDialog->setLabelText("正在准备网络测速...");
    progressDialog->setCancelButtonText("取消测试");
    progressDialog->setRange(0, 0); // 无限进度
    progressDialog->setMinimumDuration(0);
    progressDialog->setModal(true);
    progressDialog->show();

    QCoreApplication::processEvents();

    // 创建网络测速器
    NetworkSpeedTester *speedTester = new NetworkSpeedTester();

    // 连接信号槽
    connect(speedTester, &NetworkSpeedTester::testFinished, this,
        [this, progressDialog, speedTester](double downloadSpeed, double uploadSpeed) {

            qDebug() << "网络测速完成，下载:" << downloadSpeed << "MB/s, 上传:" << uploadSpeed << "MB/s";

            QString result = QString(
                "🎯 网络速度测试完成！\n\n"
                "📥 下载速度 (下行): %1 MB/s\n"
                "📤 上传速度 (上行): %2 MB/s\n\n"
                "💡 单位换算:\n"
                "   1 MB/s = 8 Mbps\n"
                "   下载速度: %3 Mbps\n"
                "   上传速度: %4 Mbps"
            ).arg(downloadSpeed, 0, 'f', 2)
             .arg(uploadSpeed, 0, 'f', 2)
             .arg(downloadSpeed * 8, 0, 'f', 2)
             .arg(uploadSpeed * 8, 0, 'f', 2);

            emit logMessage("网络测速完成");
            emit logMessage(QString("下载速度: %1 MB/s (%2 Mbps)").arg(downloadSpeed, 0, 'f', 2).arg(downloadSpeed * 8, 0, 'f', 2));
            emit logMessage(QString("上传速度: %1 MB/s (%2 Mbps)").arg(uploadSpeed, 0, 'f', 2).arg(uploadSpeed * 8, 0, 'f', 2));

            if (progressDialog && progressDialog->isVisible()) {
                progressDialog->close();
                progressDialog->deleteLater();
            }

            QMessageBox::information(nullptr, "🎯 网络测速结果", result);

            speedTester->deleteLater();
        });

    connect(speedTester, &NetworkSpeedTester::testProgress, this,
        [this, progressDialog](const QString &message) {
            qDebug() << "网络测速进度:" << message;
            emit logMessage("网络测速: " + message);

            if (progressDialog && progressDialog->isVisible()) {
                progressDialog->setLabelText(message);
                QCoreApplication::processEvents();
            }
        });

    connect(speedTester, &NetworkSpeedTester::testError, this,
        [this, progressDialog, speedTester](const QString &error) {
            qDebug() << "网络测速错误:" << error;
            emit logMessage("网络测速错误: " + error);

            if (progressDialog && progressDialog->isVisible()) {
                progressDialog->close();
                progressDialog->deleteLater();
            }

            QMessageBox::warning(nullptr, "网络测速错误",
                QString("网络测速失败:\n%1\n\n请检查网络连接后重试。").arg(error));

            speedTester->deleteLater();
        });

    connect(progressDialog, &QProgressDialog::canceled, this,
        [progressDialog, speedTester]() {
            qDebug() << "用户取消网络测速";
            speedTester->stopTest();

            if (progressDialog) {
                progressDialog->deleteLater();
            }
            speedTester->deleteLater();
        });

    // 延迟开始测试（QOverload 消解 startTest 的重载歧义）
    QTimer::singleShot(100, speedTester, QOverload<>::of(&NetworkSpeedTester::startTest));
}

void BasicTester::onAgingTest()
{
    emit logMessage("检查老化测试依赖环境...");
    
    // 关键修复: 设置 PYTHONPATH 指向打包的 Python 模块目录
    // 这样无论主程序如何被启动,子进程 python3 都能找到打包的模块
    QString appDir = QCoreApplication::applicationDirPath();
    QString pythonModulesDir = appDir + "/python_modules";

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // 剥离系统/启动脚本注入的 python_modules/PyQt5 路径后按受控方式重建：
    // 打包脚本已把同发行版 apt deb 解包出的 PyQt5/psutil/OpenGL 放进 python_modules/
    // （"开箱即用"模式，ABI 与编译机一致），同发行版目标机直接 import 即可，
    // 零安装零密码零联网；跨发行版/跨 Python 版本目标机 import 失败时，
    // 下方依赖检查会回退到"离线 dpkg -i → 联网安装"兜底链（保留）。
    {
        QStringList stripPrefixes = {
            pythonModulesDir,                       // 当前程序的 python_modules
            "/usr/lib/elsky-test-client/python_modules"  // deb 安装的标准路径
        };
        auto stripPathList = [&](const QString &key) {
            QString value = env.value(key);
            if (value.isEmpty()) return;
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
            QStringList items = value.split(':', Qt::SkipEmptyParts);
#else
            QStringList items = value.split(':', QString::SkipEmptyParts);
#endif
            QStringList filtered;
            for (const QString &it : items) {
                bool match = false;
                for (const QString &p : stripPrefixes) {
                    if (it == p || it.startsWith(p + "/")) { match = true; break; }
                }
                if (!match) filtered << it;
            }
            if (filtered.isEmpty()) env.remove(key);
            else env.insert(key, filtered.join(':'));
        };
        stripPathList("PYTHONPATH");
        stripPathList("LD_LIBRARY_PATH");
    }

    if (QDir(pythonModulesDir + "/PyQt5").exists()) {
        // 开箱即用模式：使用打包脚本从同发行版 apt deb 解包的 Python 模块
        QString existingPythonPath = env.value("PYTHONPATH", "");
        if (existingPythonPath.isEmpty()) {
            env.insert("PYTHONPATH", pythonModulesDir);
        } else {
            env.insert("PYTHONPATH", pythonModulesDir + ":" + existingPythonPath);
        }

        // ★ PyQt5 的 .so 链接的 Qt 库必须可寻址：
        // apt 版 PyQt5 不带 Qt5/ 子目录，其 .so 链接编译机发行版的 libQt5*；
        // 应用 lib/ 携带了同一套 Qt 库 → 加入 LD_LIBRARY_PATH。
        // （即使从终端直接启动主程序、无启动脚本环境，这里也能兜底）
        QString appLibDir = appDir + "/lib";
        if (QDir(appLibDir).exists()) {
            QString existingLdPath = env.value("LD_LIBRARY_PATH", "");
            if (!existingLdPath.split(':').contains(appLibDir)) {
                env.insert("LD_LIBRARY_PATH",
                           existingLdPath.isEmpty() ? appLibDir : appLibDir + ":" + existingLdPath);
            }
        }

        // pip 版 PyQt5 自带 Qt 库目录（兼容旧打包形态）
        QString qt5LibDir = pythonModulesDir + "/PyQt5/Qt5/lib";
        if (QDir(qt5LibDir).exists()) {
            QString existingLdPath = env.value("LD_LIBRARY_PATH", "");
            if (existingLdPath.isEmpty()) {
                env.insert("LD_LIBRARY_PATH", qt5LibDir);
            } else {
                env.insert("LD_LIBRARY_PATH", qt5LibDir + ":" + existingLdPath);
            }
        }
        // python_modules 目录下的 .so 文件
        if (QDir(pythonModulesDir).entryInfoList(QStringList() << "*.so", QDir::Files).size() > 0) {
            QString existingLdPath = env.value("LD_LIBRARY_PATH", "");
            if (existingLdPath.isEmpty()) {
                env.insert("LD_LIBRARY_PATH", pythonModulesDir);
            } else {
                env.insert("LD_LIBRARY_PATH", pythonModulesDir + ":" + existingLdPath);
            }
        }

        emit logMessage(QString("使用打包的 Python 模块: %1").arg(pythonModulesDir));
    }

    // 检查依赖是否可用（使用打包模块或系统模块）
    QProcess checkProcess;
    checkProcess.setProcessEnvironment(env);
    checkProcess.start("python3", QStringList() << "-c" << "import PyQt5, PyQt5.QtOpenGL, psutil, OpenGL");
    checkProcess.waitForFinished(10000);

    // 修复（A2）：依赖安装成功后必须继续向下执行启动逻辑，不再提前 return。
    // 旧实现：离线/联网安装成功后直接 return，导致首次运行老化测试"点击无反应"，
    // 用户必须再点一次才启动。用 depsReady 标志控制流程落到下方的脚本查找与启动段。
    bool depsReady = false;

    if (checkProcess.exitCode() != 0) {
        QString checkError = QString::fromUtf8(checkProcess.readAllStandardError()).trimmed();
        emit logMessage(QString("Python 依赖检查失败: %1").arg(checkError));

        // 优先级1: 如果有打包的 .deb 包,自动离线安装(无需联网)
        QString aptPackagesDir = pythonModulesDir + "/apt_packages";
        if (QDir(aptPackagesDir).exists()) {
            QFileInfoList debFiles = QDir(aptPackagesDir).entryInfoList(QStringList() << "*.deb", QDir::Files);
            if (!debFiles.isEmpty()) {
                emit logMessage(QString("检测到打包的 .deb 包 (%1 个)，开始离线安装...")
                                .arg(debFiles.size()));

                // 使用统一权限模块 dpkg 离线安装（优先免密/记忆密码，兜底弹窗）
                QStringList debPaths;
                for (const QFileInfo &fi : debFiles) {
                    debPaths << fi.absoluteFilePath();
                }
                QString dpkgCmd = "dpkg -i " + debPaths.join(" ");
                QString dpkgErr;
                bool dpkgOk = PrivilegeHelper::runPrivileged(dpkgCmd, &dpkgErr);

                if (dpkgOk) {
                    emit logMessage("✓ .deb 包离线安装成功");

                    // 修复可能的依赖问题
                    PrivilegeHelper::runPrivileged("apt-get install -f -y");

                    // 重新检查（关键：必须用干净的 env，否则 PYTHONPATH 仍指向打包路径会再次失败）
                    QProcess recheck;
                    recheck.start("python3", QStringList() << "-c"
                                  << "import PyQt5, PyQt5.QtOpenGL, psutil, OpenGL");
                    recheck.waitForFinished(10000);

                    if (recheck.exitCode() == 0) {
                        emit logMessage("✓ Python 依赖现已可用（通过离线 .deb 安装）");
                        // 修复（A2）：不再 return，标记依赖就绪后继续启动老化测试
                        depsReady = true;
                    } else {
                        emit logMessage("⚠ 离线安装后仍无法导入依赖");
                    }
                } else {
                    emit logMessage(QString("✗ .deb 安装失败: %1").arg(dpkgErr));
                }
            }
        }

        // 优先级2: 打包的 .deb 不存在/已安装但仍失败 —— 回退联网安装
        // 修复（A2）：离线安装已成功时跳过联网安装
        if (!depsReady) {
        emit logMessage("依赖环境不满足，正在尝试联网安装所需依赖...");

        // 使用统一权限模块执行（优先免密，其次记忆密码，兜底弹窗）
        QString installErr;
        bool installOk = PrivilegeHelper::runPrivileged(
            "apt-get update && apt-get install -y python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl",
            &installErr);

        if (!installOk) {
            emit logMessage(QString("依赖安装失败: %1").arg(installErr));
            QString hint;
            // 检查是否打包了离线 deb 但没生效（提示用户从编译机同步）
            int debCount = 0;
            if (QDir(aptPackagesDir).exists()) {
                debCount = QDir(aptPackagesDir).entryInfoList(QStringList() << "*.deb", QDir::Files).size();
            }
            if (debCount == 0) {
                hint = QString(
                    "\n\n可能原因: 本 deb 包未包含离线依赖（编译机未执行 apt-get download 或 apt 源未配置）。\n"
                    "请按以下步骤离线安装:\n"
                    "  1. 在能联网的同架构 Linux 机器上执行:\n"
                    "     apt-get download python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl\n"
                    "  2. 将下载的 .deb 全部复制到本机:\n"
                    "     /usr/lib/elsky-test-client/python_modules/apt_packages/\n"
                    "  3. 在本机执行:\n"
                    "     sudo dpkg -i /usr/lib/elsky-test-client/python_modules/apt_packages/*.deb\n"
                    "     sudo apt-get install -f -y");
            } else {
                hint = "\n\n请手动执行以下命令安装:\n"
                       "  sudo apt install python3-pyqt5 python3-pyqt5.qtopengl python3-psutil python3-opengl";
            }
            QMessageBox::warning(nullptr, "依赖安装错误",
                QString("老化测试需要安装以下Python依赖:\n"
                "  python3-pyqt5\n"
                "  python3-pyqt5.qtopengl\n"
                "  python3-psutil\n"
                "  python3-opengl%1").arg(hint));
            return;
        }

        emit logMessage("依赖安装成功");

        // 重新检查系统模块
        QProcess verify;
        verify.start("python3", QStringList() << "-c"
                    << "import PyQt5, PyQt5.QtOpenGL, psutil, OpenGL");
        verify.waitForFinished(10000);
        if (verify.exitCode() != 0) {
            emit logMessage("⚠ 联网安装后仍无法导入依赖");
            return;
        }
        // 修复（A2）：联网安装成功后不再 return，继续启动老化测试
        depsReady = true;
        }
    }

    // 查找脚本文件 - 按优先级依次查找
    QStringList searchPaths;
    // 1. 程序运行目录
    searchPaths << QCoreApplication::applicationDirPath() + "/aging_test_gui.py";
    // 2. 当前工作目录
    searchPaths << "aging_test_gui.py";
    // 3. 上级目录（开发环境）
    searchPaths << "../aging_test_gui.py";
    // 4. 项目根目录
    searchPaths << QCoreApplication::applicationDirPath() + "/../aging_test_gui.py";
    
    QString scriptPath;
    for (const QString &path : searchPaths) {
        if (QFile::exists(path)) {
            scriptPath = path;
            break;
        }
    }

    // 检查脚本是否存在
    if (scriptPath.isEmpty()) {
        QString error = "无法找到老化测试脚本\n请确保aging_test_gui.py文件位于:\n"
                       "1. 程序运行目录\n"
                       "2. 程序上级目录\n"
                       "3. 当前工作目录";
        emit logMessage(error);
        QMessageBox::warning(nullptr, "老化测试错误", error);
        return;
    }

    // 执行老化测试 - 使用 startDetached 避免阻塞主线程
    // 这样主窗口的系统监控等定时器才能继续正常运行
    // 关键: 通过包装脚本设置环境变量，确保 startDetached 也能使用打包的 Python 模块
    
    emit logMessage(QString("正在执行脚本: %1").arg(scriptPath));
    
    QString workingDir = QFileInfo(scriptPath).absolutePath();
    
    // 创建临时启动脚本，确保环境变量被正确设置
    // startDetached 无法直接接受 QProcessEnvironment，所以用脚本包装
    // 修复（A3）：PYTHONPATH 仅在 python_modules 内确实带有 PyQt5 时才注入——
    // python_modules 目录里若只有含 C 扩展(.so)的模块（如 psutil，其 ABI 绑定
    // 编译机的 Python 版本），无条件前置到 PYTHONPATH 会在目标机 Python 版本
    // 不一致时 import 失败，且遮蔽 dpkg 安装的系统正确版本。
    QString launcherScript = QString(
        "#!/bin/bash\n"
        "if [ -d \"%1/PyQt5\" ]; then\n"
        "    export PYTHONPATH=\"%1:${PYTHONPATH}\"\n"
        "    if [ -d \"%1/PyQt5/Qt5/lib\" ]; then\n"
        "        export LD_LIBRARY_PATH=\"%1/PyQt5/Qt5/lib:${LD_LIBRARY_PATH}\"\n"
        "    fi\n"
        "fi\n"
        "cd \"%2\"\n"
        "exec python3 \"%3\" \"$@\"\n"
    ).arg(pythonModulesDir).arg(workingDir).arg(scriptPath);
    
    // 关键修复：固定路径持久化 launcher 脚本（不每次 truncate）
    // 根因：/tmp/ 临时文件每次被 polkit/安全中心识别为"新程序"，
    // 首次"始终允许"的授权不能复用到第二次；统信系统会出现"首次要授权、
    // 第二次启动后无反应"的诡异问题。固定路径 + 固定 mtime 后授权稳定复用。
    QString tempScript = QDir::homePath() + "/.config/ELSKY/elsky_aging_launcher.sh";
    QDir().mkpath(QFileInfo(tempScript).absolutePath());
    QFile scriptFile(tempScript);
    // 修复：按内容差异决定是否重写（旧逻辑"存在即不重写"导致升级后
    // 目标机仍执行旧版 launcher，新逻辑/修复无法下发）。重写后 mtime 仍固定，
    // 不影响 polkit 授权复用。
    bool needWrite = true;
    if (scriptFile.exists() && scriptFile.open(QIODevice::ReadOnly)) {
        QByteArray existing = scriptFile.readAll();
        scriptFile.close();
        needWrite = (existing != launcherScript.toUtf8());
    }
    if (needWrite && scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QTextStream out(&scriptFile);
        out << launcherScript;
        scriptFile.close();
    }
    if (scriptFile.exists()) {
        scriptFile.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                   QFile::ReadGroup | QFile::ExeGroup |
                                   QFile::ReadOther | QFile::ExeOther);
        // 固定 mtime，避免 polkit 每次重新校验（部分安全软件对比 mtime 决定是否复用授权）
        struct utimbuf ub;
        ub.actime = 1234567890;
        ub.modtime = 1234567890;
        utime(tempScript.toLocal8Bit().constData(), &ub);
    }

    qint64 pid = 0;
    // 关键：直接以用户身份 startDetached 启动 launcher 脚本
    // 原因：
    //   1) aging_test_gui.py 本身是 PyQt5 用户级应用，不需要 root；用 sudo 启动会触发
    //      麒麟/统信"安全中心"polkit 弹窗，首次"始终允许"无法稳定复用，第二次执行被拦截。
    //   2) QProcess 析构会 SIGKILL 子进程，导致 launcher 启动后立刻被父进程析构杀掉。
    //   3) startDetached 让子进程完全独立，父进程退出不影响老化测试运行。
    //   4) 显式通过 /bin/bash 执行 launcher 脚本，避免 execvp 在不同 glibc 发行版上
    //      对 shebang 行的差异处理。
    //
    // 兜底：如果 startDetached 失败，再尝试走 PrivilegeHelper（适用某些需要 root 的子命令）
    QString program = QStringLiteral("/bin/bash");
    QStringList detachedArgs;
    detachedArgs << tempScript;
    qint64 detachedPid = 0;
    bool started = false;
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    started = QProcess::startDetached(program, detachedArgs, workingDir, &detachedPid);
#else
    started = QProcess::startDetached(program, detachedArgs, workingDir);
#endif
    QProcess *proc = nullptr;
    if (started) {
        pid = detachedPid;
    } else {
        // 兜底路径：使用统一权限模块启动——免密/记忆密码时不弹 polkit 框
        proc = PrivilegeHelper::startPrivileged(
            QStringList() << tempScript);
        started = (proc != nullptr);
        if (started) {
            // 拿到主进程 pid 后立即分离（QProcess 由事件循环管理，父进程退出不影响子进程）
            pid = proc->processId();
            // 不再持有 QProcess，让其通过 Qt 事件循环自动 delete
            proc->setProcessChannelMode(QProcess::SeparateChannels);
            proc->disconnect();  // 断开所有信号防止本对象析构时回调
            // 注意：不能 deleteLater()，否则会 SIGKILL 子进程。
            // 让 QProcess 由主事件循环在程序退出时自然释放，期间只断开信号即可。
        }
    }
    if (started) {
        emit logMessage(QString("老化测试已启动！PID=%1").arg(pid));
        emit logMessage(QString("脚本路径: %1").arg(scriptPath));
        emit logMessage(QString("Python 模块: %1").arg(pythonModulesDir));
    } else {
        emit logMessage("无法启动老化测试程序");
        QMessageBox::warning(nullptr, "老化测试错误", "无法启动老化测试程序");
    }
}

void BasicTester::onBenchmarkTest()
{
    // 跑分测试：双模式（内置离线 / 专业工具联网）
    // 1) 离线：内置算法评分，报告标注"内置测评"
    // 2) 联网：可选下载 glmark2/fio/iozone/x11perf（业内权威工具），
    //    已装 UnixBench 可跑官方 CPU 分
    emit logMessage("[跑分] 启动系统跑分测试（CPU/GPU/内存/磁盘）...");

    // 懒加载跑分模块（首次使用时创建，之后复用）
    if (!m_benchmark) {
        m_benchmark = new BenchmarkTester(this);
        connect(m_benchmark, &BenchmarkTester::logMessage,
                this, &BasicTester::logMessage);
        connect(m_benchmark, &BenchmarkTester::progress,
                this, [this](const QString &item, int percent) {
            emit logMessage(QString("[跑分] 进度 %1%%: %2").arg(percent).arg(item));
        });
        connect(m_benchmark, &BenchmarkTester::finished,
                this, [this](const QString &report) {
            QMessageBox *box = new QMessageBox(nullptr);
            box->setIcon(QMessageBox::Information);
            box->setWindowTitle("系统综合跑分报告");
            box->setText(report);
            box->setStandardButtons(QMessageBox::Ok);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->setModal(false);          // 非模态：不影响软件其它功能操作
            box->show();
        });
        connect(m_benchmark, &BenchmarkTester::failed,
                this, [this](const QString &reason) {
            emit logMessage(QString("[跑分] 测试失败/中断: %1").arg(reason));
        });
    }

    if (m_benchmark->isRunning()) {
        emit logMessage("[跑分] 上一次跑分尚未结束，请稍候...");
        QMessageBox::information(nullptr, "跑分测试",
                                  "上一次跑分测试尚未结束，请稍候再试。\n"
                                  "（测试在后台进行，不影响其它功能使用）");
        return;
    }

    // ---- 模式选择（主线程 UI 交互，测试仍在后台线程跑）----
    ProToolsConfig cfg;

#ifdef Q_OS_LINUX
    emit logMessage("[跑分] 检测网络与已安装的测评工具...");
    bool online = BenchmarkTester::checkOnline();
    QStringList installed = BenchmarkTester::detectInstalledTools();
    QString unixbench = BenchmarkTester::findUnixBench();
    emit logMessage(QString("[跑分] 网络: %1, 已装工具: %2")
                    .arg(online ? "在线" : "离线")
                    .arg(installed.isEmpty() ? "无" : installed.join(",")));

    // 缺失且可 apt 安装的专业工具（包名 -> 说明）
    struct PkgInfo { const char *pkg; const char *name; const char *size; const char *runTime; const char *cover; };
    static const PkgInfo pkgList[] = {
        { "glmark2", "glmark2",   "~15MB", "~5分钟",  "GPU 3D 渲染" },
        { "fio",     "fio",       "~2MB",  "~3分钟",  "磁盘随机读写" },
        { "iozone3", "iozone",    "~1MB",  "~3分钟",  "磁盘顺序读写" },
        { "x11perf", "x11perf",   "~0.5MB", "~2分钟", "2D 图形" },
    };
    QStringList missingPkgs;
    for (const PkgInfo &pi : pkgList) {
        QString toolName = pi.name;
        if (!installed.contains(toolName))
            missingPkgs << QString("%1(%2, 下载约%3, 运行约%4)")
                           .arg(pi.pkg, pi.cover, pi.size, pi.runTime);
    }

    if (online && (!missingPkgs.isEmpty() || !unixbench.isEmpty())) {
        // 联网且有可补工具：弹模式选择对话框
        QDialog dlg(nullptr);
        dlg.setWindowTitle("跑分测试模式选择");
        dlg.setModal(true);
        QVBoxLayout *lay = new QVBoxLayout(&dlg);

        QString info;
        info += QString("网络状态: 在线\n\n");
        if (!installed.isEmpty())
            info += QString("已安装的专业工具: %1\n\n").arg(installed.join(", "));
        if (!missingPkgs.isEmpty())
            info += QString("可下载安装的专业工具:\n  %1\n\n")
                        .arg(missingPkgs.join("\n  "));

        QLabel *lbl = new QLabel(info, &dlg);
        lbl->setWordWrap(true);
        lay->addWidget(lbl);

        QCheckBox *proChk = new QCheckBox(
            "使用专业测评工具（下载+安装约 2~5 分钟，测评运行约 10~15 分钟，\n"
            "结果为业内权威工具原生分数，可直接与公开评测数据对比）\n"
            "（通过本软件 deb 包安装后已免密授权，无需输密码）", &dlg);
        proChk->setChecked(!missingPkgs.isEmpty());
        lay->addWidget(proChk);

        QCheckBox *ubChk = new QCheckBox(
            "同时运行 UnixBench CPU 测评（已检测到安装；CPU 子集约 10 分钟，\n"
            "业内最权威开源 CPU benchmark，输出官方 Index 分）", &dlg);
        ubChk->setEnabled(!unixbench.isEmpty());
        ubChk->setChecked(false);
        lay->addWidget(ubChk);

        QDialogButtonBox *btns = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dlg);
        connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        lay->addWidget(btns);

        if (dlg.exec() == QDialog::Accepted && proChk->isChecked()) {
            cfg.proMode = true;
            // 需下载安装的缺失包
            for (const PkgInfo &pi : pkgList) {
                if (!installed.contains(pi.name))
                    cfg.packagesToInstall << pi.pkg;
            }
            cfg.runUnixBench = ubChk->isChecked();
            emit logMessage(QString("[跑分] 专业工具模式：将安装 [%1]%2")
                            .arg(cfg.packagesToInstall.join(","))
                            .arg(cfg.runUnixBench ? " 并运行 UnixBench" : ""));
        } else {
            emit logMessage("[跑分] 用户选择内置测评模式");
        }
    } else if (!online) {
        emit logMessage("[跑分] 离线环境：使用内置测评模式");
        QMessageBox::information(nullptr, "跑分测试",
            "当前处于离线环境，将使用内置测评机制。\n"
            "（结果为内置算法评分，标注为\"内置测评\"；联网后重测可使用专业工具）");
    }
#endif

    if (!m_benchmark->start(QString(), cfg)) {
        emit logMessage("[跑分] 启动失败");
        QMessageBox::warning(nullptr, "跑分测试", "跑分测试启动失败");
        return;
    }
    QString estTime = cfg.proMode ? "专业模式约 15~30 分钟" : "内置模式约 1~2 分钟";
    emit logMessage(QString("[跑分] 测试已在后台启动（%1），期间可正常使用其它功能")
                    .arg(estTime));
}


void BasicTester::onAgingTestAutoStart()
{
    emit logMessage("[统一控制] 准备启动自动老化...");

    // 查找自动老化入口脚本（优先使用 aging_auto_start.py）
    QStringList searchPaths;
    searchPaths << QCoreApplication::applicationDirPath() + "/aging_auto_start.py";
    searchPaths << "aging_auto_start.py";
    searchPaths << "../aging_auto_start.py";
    searchPaths << QCoreApplication::applicationDirPath() + "/../aging_auto_start.py";

    QString scriptPath;
    for (const QString &path : searchPaths) {
        if (QFile::exists(path)) {
            scriptPath = path;
            break;
        }
    }

    // 如果找不到自动老化脚本，回退到主脚本
    if (scriptPath.isEmpty()) {
        QStringList fallbackPaths;
        fallbackPaths << QCoreApplication::applicationDirPath() + "/aging_test_gui.py";
        fallbackPaths << "aging_test_gui.py";
        fallbackPaths << "../aging_test_gui.py";
        fallbackPaths << QCoreApplication::applicationDirPath() + "/../aging_test_gui.py";
        for (const QString &path : fallbackPaths) {
            if (QFile::exists(path)) {
                scriptPath = path;
                break;
            }
        }
    }

    if (scriptPath.isEmpty()) {
        emit logMessage("[统一控制] 错误：找不到老化测试脚本！");
        emit logMessage(QString("[统一控制] 搜索路径: %1")
                        .arg(searchPaths.join(", ")));
        return;
    }

    emit logMessage(QString("[统一控制] 找到脚本: %1").arg(scriptPath));

    // 关键修复: 设置 PYTHONPATH 指向打包的 Python 模块目录
    QString appDir = QCoreApplication::applicationDirPath();
    QString pythonModulesDir = appDir + "/python_modules";
    
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    bool hasPackagedModules = QDir(pythonModulesDir + "/PyQt5").exists();
    
    if (hasPackagedModules) {
        QString existingPythonPath = env.value("PYTHONPATH", "");
        if (existingPythonPath.isEmpty()) {
            env.insert("PYTHONPATH", pythonModulesDir);
        } else {
            env.insert("PYTHONPATH", pythonModulesDir + ":" + existingPythonPath);
        }
        
        QString qt5LibDir = pythonModulesDir + "/PyQt5/Qt5/lib";
        if (QDir(qt5LibDir).exists()) {
            QString existingLdPath = env.value("LD_LIBRARY_PATH", "");
            if (existingLdPath.isEmpty()) {
                env.insert("LD_LIBRARY_PATH", qt5LibDir);
            } else {
                env.insert("LD_LIBRARY_PATH", qt5LibDir + ":" + existingLdPath);
            }
        }
        
        emit logMessage(QString("[统一控制] 使用打包的 Python 模块: %1").arg(pythonModulesDir));
    }

    // 先用 QProcess 测试脚本能否正常启动，捕获错误信息
    QProcess testProcess;
    testProcess.setProcessEnvironment(env);
    testProcess.setWorkingDirectory(QFileInfo(scriptPath).absolutePath());
    testProcess.start("python3", QStringList() << "-c"
        << "import PyQt5, PyQt5.QtOpenGL, psutil, OpenGL; print('OK')");
    testProcess.waitForFinished(5000);

    if (testProcess.exitCode() != 0) {
        QString err = QString::fromUtf8(testProcess.readAllStandardError());
        emit logMessage(QString("[统一控制] Python依赖检查失败: %1").arg(err));
        
        // 优先级1: 如果有打包的 .deb 包,自动离线安装
        QString aptPackagesDir = pythonModulesDir + "/apt_packages";
        if (QDir(aptPackagesDir).exists()) {
            QFileInfoList debFiles = QDir(aptPackagesDir).entryInfoList(QStringList() << "*.deb", QDir::Files);
            if (!debFiles.isEmpty()) {
                emit logMessage(QString("[统一控制] 检测到打包的 .deb 包 (%1 个)，开始离线安装...")
                                .arg(debFiles.size()));
                
                QStringList debPaths;
                for (const QFileInfo &fi : debFiles) {
                    debPaths << fi.absoluteFilePath();
                }
                
                QProcess dpkgProcess;
                dpkgProcess.start("pkexec", QStringList() << "dpkg" << "-i" << debPaths);
                dpkgProcess.waitForFinished(120000);
                
                if (dpkgProcess.exitCode() == 0) {
                    emit logMessage("[统一控制] ✓ .deb 包离线安装成功");
                    QProcess aptFix;
                    aptFix.start("pkexec", QStringList() << "apt-get" << "install" << "-f" << "-y");
                    aptFix.waitForFinished(60000);
                }
            }
        }
        
        // 优先级2: 如果有打包模块但导入失败，不再尝试 apt 安装
        if (hasPackagedModules) {
            emit logMessage("[统一控制] 打包模块加载失败，可能架构不兼容");
            emit logMessage("[统一控制] 尝试继续启动...");
        } else {
            emit logMessage("[统一控制] 正在尝试安装依赖...");
            // 尝试安装
            QProcess installProcess;
            installProcess.start("pkexec", QStringList() << "apt" << "install" << "-y"
                << "python3-pyqt5" << "python3-pyqt5.qtopengl" << "python3-psutil" << "python3-opengl");
            if (!installProcess.waitForStarted(5000)) {
                emit logMessage("[统一控制] pkexec无法启动，跳过依赖安装");
            } else {
                installProcess.waitForFinished(120000);
                if (installProcess.exitCode() == 0) {
                    emit logMessage("[统一控制] 依赖安装成功");
                } else {
                    emit logMessage("[统一控制] 依赖安装失败，尝试继续启动...");
                }
            }
        }
    }

    // 使用 startDetached 完全独立启动老化测试进程，不阻塞主线程
    // 关键: 通过包装脚本设置环境变量（startDetached 不支持 QProcessEnvironment）
    QString workingDir = QFileInfo(scriptPath).absolutePath();
    qint64 pid = 0;
    bool started = false;
    
    if (hasPackagedModules) {
        // 关键修复：固定路径持久化 launcher 脚本（不每次 truncate）
        // 根因：/tmp/ 临时文件每次被 polkit/安全中心识别为"新程序"，
        // 首次"始终允许"的授权不能复用到第二次；统信系统会出现"首次要授权、
        // 第二次启动后无反应"的诡异问题。固定路径 + 固定 mtime 后授权稳定复用。
        QString launcherScript = QString(
            "#!/bin/bash\n"
            "export PYTHONPATH=\"%1:${PYTHONPATH}\"\n"
            "if [ -d \"%1/PyQt5/Qt5/lib\" ]; then\n"
            "    export LD_LIBRARY_PATH=\"%1/PyQt5/Qt5/lib:${LD_LIBRARY_PATH}\"\n"
            "fi\n"
            "cd \"%2\"\n"
            "exec python3 \"%3\" \"$@\"\n"
        ).arg(pythonModulesDir).arg(workingDir).arg(scriptPath);

        QString tempScript = QDir::homePath() + "/.config/ELSKY/elsky_auto_aging_launcher.sh";
        QDir().mkpath(QFileInfo(tempScript).absolutePath());
        QFile scriptFile(tempScript);
        // 修复：按内容差异决定是否重写（同 onAgingTest 的 launcher 策略）
        bool needWrite = true;
        if (scriptFile.exists() && scriptFile.open(QIODevice::ReadOnly)) {
            QByteArray existing = scriptFile.readAll();
            scriptFile.close();
            needWrite = (existing != launcherScript.toUtf8());
        }
        if (needWrite && scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream out(&scriptFile);
            out << launcherScript;
            scriptFile.close();
        }
        if (scriptFile.exists()) {
            scriptFile.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                       QFile::ReadGroup | QFile::ExeGroup |
                                       QFile::ReadOther | QFile::ExeOther);
            // 固定 mtime，避免 polkit 每次重新校验
            struct utimbuf ub;
            ub.actime = 1234567890;
            ub.modtime = 1234567890;
            utime(tempScript.toLocal8Bit().constData(), &ub);
        }

        // 关键：使用统一权限模块启动（已配置 sudoers 免密/记忆密码时不经 polkit 弹框）
        QProcess *proc = PrivilegeHelper::startPrivileged(QStringList() << tempScript);
        started = (proc != nullptr);
        if (started) {
            pid = proc->processId();
            proc->setProcessChannelMode(QProcess::SeparateChannels);
            proc->disconnect();
            // 修复：此处曾误用 deleteLater()——事件循环触发 delete 时 ~QProcess
            // 会 SIGKILL 刚启动的老化 GUI（与本文件 startAgingTest 中 944 行的
            // 注释结论一致：不能 deleteLater，否则会杀死子进程）。
            // 正确做法：断开信号后让其存活到主程序退出，由事件循环自然释放。
        }
    } else {
        // 无打包模块，直接启动
        started = QProcess::startDetached("python3",
            QStringList() << scriptPath,
            workingDir,
            &pid);
    }

    if (started) {
        m_agingPid = pid;
        emit logMessage(QString("[统一控制] 自动老化已启动！PID=%1 (CPU/内存/硬盘/2D显卡/3D显卡, 7天)").arg(pid));
        emit logMessage(QString("[统一控制] 脚本路径: %1").arg(scriptPath));
        emit logMessage(QString("[统一控制] 工作目录: %1").arg(workingDir));
    } else {
        emit logMessage("[统一控制] 错误：老化测试启动失败！");
        // 尝试用 bash 启动
        started = QProcess::startDetached("/bin/bash",
            QStringList() << "-c" << QString("cd '%1' && python3 '%2'").arg(workingDir).arg(scriptPath),
            workingDir,
            &pid);
        if (started) {
            m_agingPid = pid;
            emit logMessage(QString("[统一控制] 通过bash启动成功，PID=%1").arg(pid));
        } else {
            emit logMessage("[统一控制] 通过bash启动也失败了");
        }
    }
}

void BasicTester::startAgingTestAuto()
{
    onAgingTestAutoStart();
}

void BasicTester::stopAgingTest()
{
    emit logMessage("[统一控制] 正在停止老化测试...");

    // 使用 pkill -9 -f 直接匹配进程名强制终止，确保杀死整个进程及其子线程
    QProcess killProcess;
    killProcess.start("pkill", QStringList() << "-9" << "-f" << "aging_auto_start.py");
    killProcess.waitForFinished(5000);

    killProcess.start("pkill", QStringList() << "-9" << "-f" << "aging_test_gui.py");
    killProcess.waitForFinished(5000);

    // 再通过 wmctrl 关闭可能残留的"系统硬件老化测试工具"窗口
    QProcess wmctrl;
    wmctrl.start("wmctrl", QStringList() << "-c" << "系统硬件老化测试工具");
    wmctrl.waitForFinished(3000);

    m_agingPid = 0;
    emit logMessage("[统一控制] 老化测试已停止");
}