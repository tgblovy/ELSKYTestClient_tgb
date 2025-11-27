#include "usb_speed_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QStorageInfo>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QRegularExpression>
#include <QApplication>

UsbSpeedTestDialog::UsbSpeedTestDialog(QWidget *parent)
    : QDialog(parent), progressValue(0)
{
    setupUI();

    // 设置对话框属性
    setWindowModality(Qt::ApplicationModal); // 应用程序模态
    setWindowFlags(windowFlags() | Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

    setWindowTitle("U盘速度测试");
    setMinimumSize(600, 500);

    // 初始化进度条定时器
    progressTimer = new QTimer(this);
    connect(progressTimer, &QTimer::timeout, this, &UsbSpeedTestDialog::updateProgress);

    // 初始检测驱动器
    detectUsbDrives();
}

UsbSpeedTestDialog::~UsbSpeedTestDialog()
{
    if (progressTimer && progressTimer->isActive()) {
        progressTimer->stop();
    }
}

void UsbSpeedTestDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 驱动器选择区域
    QGroupBox *driveGroup = new QGroupBox("选择测试驱动器", this);
    QHBoxLayout *driveLayout = new QHBoxLayout(driveGroup);

    driveLayout->addWidget(new QLabel("驱动器:"));
    driveCombo = new QComboBox(this);
    driveCombo->setMinimumWidth(300);
    driveLayout->addWidget(driveCombo);

    refreshButton = new QPushButton("刷新", this);
    driveLayout->addWidget(refreshButton);

    driveLayout->addStretch();

    // 测试设置区域
    QGroupBox *testGroup = new QGroupBox("测试设置", this);
    QHBoxLayout *testLayout = new QHBoxLayout(testGroup);

    testLayout->addWidget(new QLabel("测试文件大小:"));
    testFileSizeCombo = new QComboBox(this);
    testFileSizeCombo->addItem("100 MB", 100);
    testFileSizeCombo->addItem("200 MB", 200);
    testFileSizeCombo->addItem("500 MB", 500);
    testFileSizeCombo->addItem("1 GB", 1024);
    testFileSizeCombo->setCurrentIndex(1); // 默认200MB
    testLayout->addWidget(testFileSizeCombo);

    startTestButton = new QPushButton("开始测试", this);
    startTestButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; }");
    testLayout->addWidget(startTestButton);

    testLayout->addStretch();

    // 进度条
    progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->setVisible(false);

    // 测试结果区域
    QGroupBox *resultGroup = new QGroupBox("测试结果", this);
    QVBoxLayout *resultLayout = new QVBoxLayout(resultGroup);

    resultEdit = new QTextEdit(this);
    resultEdit->setReadOnly(true);
    resultEdit->setMinimumHeight(200);
    resultLayout->addWidget(resultEdit);

    mainLayout->addWidget(driveGroup);
    mainLayout->addWidget(testGroup);
    mainLayout->addWidget(progressBar);
    mainLayout->addWidget(resultGroup);

    // 连接信号和槽
    connect(refreshButton, &QPushButton::clicked, this, &UsbSpeedTestDialog::onRefreshDrives);
    connect(startTestButton, &QPushButton::clicked, this, &UsbSpeedTestDialog::onStartTest);
    connect(testFileSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &UsbSpeedTestDialog::onTestFileSizeChanged);
}

void UsbSpeedTestDialog::onRefreshDrives()
{
    detectUsbDrives();
    resultEdit->append("已刷新驱动器列表");
}

void UsbSpeedTestDialog::detectUsbDrives()
{
    driveCombo->clear();

    QList<QStorageInfo> drives = QStorageInfo::mountedVolumes();

    for (const QStorageInfo &drive : drives) {
        // 检查是否为可移动设备或网络驱动器
        bool isRemovable = false;
        QString fileSystemType = QString::fromUtf8(drive.fileSystemType());

        // 通过多种方式判断是否为可移动设备
        // 1. 检查文件系统类型
        if (fileSystemType.contains("fuseblk") ||
            fileSystemType.contains("vfat") ||
            fileSystemType.contains("ntfs") ||
            fileSystemType.contains("exfat")) {
            isRemovable = true;
        }

        // 2. 检查挂载点是否在/media或/mnt目录下
        QString rootPath = drive.rootPath();
        if (rootPath.startsWith("/media/") || rootPath.startsWith("/mnt/") || rootPath.startsWith("/run/media/")) {
            isRemovable = true;
        }

        // 3. 排除系统根目录和其他系统分区
        if (rootPath == "/" || rootPath == "/boot" || rootPath == "/home" ||
            rootPath == "/var" || rootPath == "/tmp") {
            isRemovable = false;
        }

        if (drive.isValid() && drive.isReady() && isRemovable) {
            QString displayName = QString("%1 (%2) - %3 %4 GB")
                .arg(drive.name())
                .arg(drive.rootPath())
                .arg(fileSystemType)
                .arg(drive.bytesTotal() / (1024 * 1024 * 1024));

            // 判断设备类型
            QString deviceType = "U盘";
            if (drive.bytesTotal() > 32L * 1024 * 1024 * 1024) {
                deviceType = "移动硬盘";
            }

            QString toolTip = QString("设备类型: %1\n总空间: %2 GB\n可用空间: %3 GB\n文件系统: %4")
                .arg(deviceType)
                .arg(drive.bytesTotal() / (1024 * 1024 * 1024))
                .arg(drive.bytesFree() / (1024 * 1024 * 1024))
                .arg(fileSystemType);

            driveCombo->addItem(displayName, drive.rootPath());
            driveCombo->setItemData(driveCombo->count() - 1, toolTip, Qt::ToolTipRole);
        }
    }

    if (driveCombo->count() == 0) {
        driveCombo->addItem("未检测到可移动驱动器");
        startTestButton->setEnabled(false);
    } else {
        startTestButton->setEnabled(true);
    }
}

void UsbSpeedTestDialog::onTestFileSizeChanged(int index)
{
    Q_UNUSED(index);
    // 可以在这里添加文件大小改变的处理逻辑
}

void UsbSpeedTestDialog::updateProgress()
{
    progressValue += 2;
    if (progressValue > 100) {
        progressValue = 100;
        if (progressTimer && progressTimer->isActive()) {
            progressTimer->stop();
        }
    }
    progressBar->setValue(progressValue);
}

void UsbSpeedTestDialog::onStartTest()
{
    if (driveCombo->count() == 0 || !startTestButton->isEnabled()) {
        QMessageBox::warning(this, "错误", "没有可用的驱动器进行测试");
        return;
    }

    QString selectedDrive = driveCombo->currentData().toString();
    if (selectedDrive.isEmpty()) {
        QMessageBox::warning(this, "错误", "请选择有效的驱动器");
        return;
    }

    // 检查驱动器是否有足够的空间
    QStorageInfo driveInfo(selectedDrive);
    int testFileSizeMB = testFileSizeCombo->currentData().toInt();
    qint64 requiredSpace = testFileSizeMB * 1024 * 1024;

    if (driveInfo.bytesFree() < requiredSpace) {
        QMessageBox::warning(this, "空间不足",
            QString("驱动器空间不足！\n需要: %1 MB\n可用: %2 MB")
                .arg(testFileSizeMB)
                .arg(driveInfo.bytesFree() / (1024 * 1024)));
        return;
    }

    // 准备测试
    startTestButton->setEnabled(false);
    refreshButton->setEnabled(false);
    progressBar->setVisible(true);
    progressBar->setValue(0);
    progressValue = 0;

    resultEdit->clear();
    resultEdit->append("测试开始...");
    resultEdit->append(QString("测试驱动器: %1").arg(driveCombo->currentText()));
    resultEdit->append(QString("测试文件大小: %2 MB").arg(testFileSizeMB));
    resultEdit->append("正在执行速度测试，请稍候...");

    // 停止进度条定时器
    if (progressTimer && progressTimer->isActive()) {
        progressTimer->stop();
    }

    // 在新线程中执行测试，避免界面冻结
    QTimer::singleShot(100, this, [this, selectedDrive, testFileSizeMB]() {
        QString result = performSpeedTest();
        resultEdit->append("\n测试完成\n");
        resultEdit->append("测试结果\n");
        resultEdit->append(result);

        progressBar->setValue(100);
        startTestButton->setEnabled(true);
        refreshButton->setEnabled(true);

        QTimer::singleShot(1000, this, [this]() {
            progressBar->setVisible(false);
        });
    });
}

QString UsbSpeedTestDialog::performSpeedTest()
{
    QString selectedDrive = driveCombo->currentData().toString();
    int testFileSizeMB = testFileSizeCombo->currentData().toInt();

    double writeSpeed = 0.0;
    double readSpeed = 0.0;

    resultEdit->append("使用精确模式(dd命令)进行测试...");

    if (runDDTestAsRoot(selectedDrive, testFileSizeMB, writeSpeed, readSpeed)) {
        resultEdit->append("✓ 精确测试完成");
    } else {
        resultEdit->append("✗ 精确测试失败，请检查权限和驱动器状态");
        return "错误: 精确测试失败，请确保有管理员权限且驱动器可用";
    }

    // 构建结果字符串
    QString result;
    result += QString("驱动器: %1\n").arg(driveCombo->currentText());
    result += QString("测试文件大小: %1 MB\n").arg(testFileSizeMB);
    result += QString("写入速度: %1 MB/s\n").arg(QString::number(writeSpeed, 'f', 2));
    result += QString("读取速度: %1 MB/s\n").arg(QString::number(readSpeed, 'f', 2));
    result += QString("性能评价: %1").arg(evaluatePerformance(writeSpeed, readSpeed));
    result += "\n(使用精确测试模式 - 绕过系统缓存)";

    return result;
}

QString UsbSpeedTestDialog::createTestScript(const QString &drivePath, int testFileSizeMB)
{
    QString scriptContent = QString(
        "#!/bin/bash\n"
        "set -e\n"
        "\n"
        "DRIVE_PATH=\"%1\"\n"
        "TEST_FILE=\"$DRIVE_PATH/usb_speed_test.dat\"\n"
        "TEST_SIZE=%2\n"
        "\n"
        "echo \"开始USB速度测试...\"\n"
        "echo \"驱动器: $DRIVE_PATH\"\n"
        "echo \"测试文件大小: ${TEST_SIZE}MB\"\n"
        "\n"
        "# 清理缓存\n"
        "echo \"清理系统缓存...\"\n"
        "sync\n"
        "echo 3 > /proc/sys/vm/drop_caches\n"
        "\n"
        "# 写入测试\n"
        "echo \"执行写入测试...\"\n"
        "WRITE_RESULT=$(dd if=/dev/zero of=\"$TEST_FILE\" bs=1M count=$TEST_SIZE oflag=dsync 2>&1)\n"
        "echo \"$WRITE_RESULT\"\n"
        "WRITE_TIME=$(echo \"$WRITE_RESULT\" | grep -o '[0-9]\\+\\.[0-9]\\+ seconds' | grep -o '[0-9]\\+\\.[0-9]\\+' | head -1)\n"
        "WRITE_SPEED=$(echo \"$WRITE_RESULT\" | grep -o '[0-9]\\+\\.[0-9]\\+ [KMGT]B/s' | grep -o '[0-9]\\+\\.[0-9]\\+' | head -1)\n"
        "WRITE_UNIT=$(echo \"$WRITE_RESULT\" | grep -o '[0-9]\\+\\.[0-9]\\+ \\+[KMGT]B/s' | grep -o '[KMGT]B/s' | head -1)\n"
        "\n"
        "# 如果解析失败，使用时间计算\n"
        "if [ -z \"$WRITE_SPEED\" ] && [ -n \"$WRITE_TIME\" ]; then\n"
        "    WRITE_SPEED=$(echo \"scale=2; $TEST_SIZE / $WRITE_TIME\" | bc)\n"
        "    WRITE_UNIT=\"MB/s\"\n"
        "    echo \"使用时间计算写入速度: $WRITE_SPEED MB/s\"\n"
        "fi\n"
        "\n"
        "# 清理缓存\n"
        "echo \"清理缓存准备读取测试...\"\n"
        "sync\n"
        "echo 3 > /proc/sys/vm/drop_caches\n"
        "sleep 2\n"
        "\n"
        "# 读取测试\n"
        "echo \"执行读取测试...\"\n"
        "READ_RESULT=$(dd if=\"$TEST_FILE\" of=/dev/null bs=1M count=$TEST_SIZE iflag=direct 2>&1)\n"
        "echo \"$READ_RESULT\"\n"
        "READ_TIME=$(echo \"$READ_RESULT\" | grep -o '[0-9]\\+\\.[0-9]\\+ seconds' | grep -o '[0-9]\\+\\.[0-9]\\+' | head -1)\n"
        "READ_SPEED=$(echo \"$READ_RESULT\" | grep -o '[0-9]\\+\\.[0-9]\\+ [KMGT]B/s' | grep -o '[0-9]\\+\\.[0-9]\\+' | head -1)\n"
        "READ_UNIT=$(echo \"$READ_RESULT\" | grep -o '[0-9]\\+\\.[0-9]\\+ \\+[KMGT]B/s' | grep -o '[KMGT]B/s' | head -1)\n"
        "\n"
        "# 如果解析失败，使用时间计算\n"
        "if [ -z \"$READ_SPEED\" ] && [ -n \"$READ_TIME\" ]; then\n"
        "    READ_SPEED=$(echo \"scale=2; $TEST_SIZE / $READ_TIME\" | bc)\n"
        "    READ_UNIT=\"MB/s\"\n"
        "    echo \"使用时间计算读取速度: $READ_SPEED MB/s\"\n"
        "fi\n"
        "\n"
        "# 清理测试文件\n"
        "rm -f \"$TEST_FILE\"\n"
        "\n"
        "# 输出结果\n"
        "echo \"WRITE_SPEED=$WRITE_SPEED\"\n"
        "echo \"WRITE_UNIT=$WRITE_UNIT\"\n"
        "echo \"READ_SPEED=$READ_SPEED\"\n"
        "echo \"READ_UNIT=$READ_UNIT\"\n"
        "\n"
        "echo \"测试完成\"\n"
    ).arg(drivePath).arg(testFileSizeMB);

    return scriptContent;
}

bool UsbSpeedTestDialog::runDDTestAsRoot(const QString &drivePath, int testFileSizeMB, double &writeSpeed, double &readSpeed)
{
    // 创建临时脚本文件
    QString tempScriptPath = "/tmp/usb_speed_test.sh";
    QFile scriptFile(tempScriptPath);

    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        resultEdit->append("错误: 无法创建测试脚本");
        return false;
    }

    QTextStream out(&scriptFile);
    out << createTestScript(drivePath, testFileSizeMB);
    scriptFile.close();

    // 设置脚本可执行权限
    QFile::setPermissions(tempScriptPath,
                         QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                         QFile::ReadGroup | QFile::ExeGroup |
                         QFile::ReadOther | QFile::ExeOther);

    // 使用pkexec以root权限运行脚本
    resultEdit->append("正在请求管理员权限...");
    QApplication::processEvents();

    QProcess process;
    process.start("pkexec", QStringList() << "bash" << tempScriptPath);

    if (!process.waitForStarted(1000)) {
        resultEdit->append("错误: 无法启动测试进程");
        QFile::remove(tempScriptPath);
        return false;
    }

    // 读取输出并显示进度
    QString output;
    QTimer progressTimer;
    int progressValue = 10;
    progressTimer.start(200);

    QObject::connect(&progressTimer, &QTimer::timeout, [this, &process, &output, &progressValue]() {
        if (process.state() == QProcess::Running) {
            output += process.readAllStandardOutput();

            // 更新进度显示
            if (output.contains("写入测试")) {
                progressValue = 30;
            } else if (output.contains("读取测试")) {
                progressValue = 70;
            } else if (progressValue < 90) {
                progressValue += 1;
            }

            progressBar->setValue(progressValue);
            QApplication::processEvents();
        }
    });

    // 等待测试完成
    if (!process.waitForFinished(300000)) {
        progressTimer.stop();
        resultEdit->append("错误: 测试超时");
        QFile::remove(tempScriptPath);
        return false;
    }

    progressTimer.stop();

    // 获取完整输出
    output += process.readAllStandardOutput();
    QString errorOutput = process.readAllStandardError();

    if (!errorOutput.isEmpty()) {
        resultEdit->append("错误输出: " + errorOutput);
    }

    // 显示测试过程
    resultEdit->append("测试过程:");
    resultEdit->append(output);

    // 解析结果
    QRegularExpression writeRe("WRITE_SPEED=([0-9]+(?:\\.[0-9]+)?)");
    QRegularExpression writeUnitRe("WRITE_UNIT=([KMGT]B/s)");
    QRegularExpression readRe("READ_SPEED=([0-9]+(?:\\.[0-9]+)?)");
    QRegularExpression readUnitRe("READ_UNIT=([KMGT]B/s)");

    QRegularExpressionMatch writeMatch = writeRe.match(output);
    QRegularExpressionMatch writeUnitMatch = writeUnitRe.match(output);
    QRegularExpressionMatch readMatch = readRe.match(output);
    QRegularExpressionMatch readUnitMatch = readUnitRe.match(output);

    if (writeMatch.hasMatch() && readMatch.hasMatch()) {
        writeSpeed = writeMatch.captured(1).toDouble();
        readSpeed = readMatch.captured(1).toDouble();

        // 单位转换
        if (writeUnitMatch.hasMatch()) {
            QString unit = writeUnitMatch.captured(1);
            if (unit == "KB/s") writeSpeed /= 1024.0;
            else if (unit == "GB/s") writeSpeed *= 1024.0;
        }

        if (readUnitMatch.hasMatch()) {
            QString unit = readUnitMatch.captured(1);
            if (unit == "KB/s") readSpeed /= 1024.0;
            else if (unit == "GB/s") readSpeed *= 1024.0;
        }

        resultEdit->append(QString("解析结果 - 写入: %1 MB/s, 读取: %2 MB/s").arg(writeSpeed, 0, 'f', 2).arg(readSpeed, 0, 'f', 2));
    } else {
        // 如果解析失败，尝试从输出中提取速度
        QRegularExpression speedRe("([0-9]+(?:\\.[0-9]+)?)\\s*([KMGT]?B/s)");
        QRegularExpressionMatchIterator matches = speedRe.globalMatch(output);

        QList<double> speeds;
        while (matches.hasNext()) {
            QRegularExpressionMatch match = matches.next();
            double speed = match.captured(1).toDouble();
            QString unit = match.captured(2);

            // 转换为MB/s
            if (unit == "KB/s") speed /= 1024.0;
            else if (unit == "GB/s") speed *= 1024.0;

            speeds.append(speed);
        }

        if (speeds.size() >= 2) {
            writeSpeed = speeds[0];
            readSpeed = speeds[1];
            resultEdit->append(QString("备选解析 - 写入: %1 MB/s, 读取: %2 MB/s").arg(writeSpeed, 0, 'f', 2).arg(readSpeed, 0, 'f', 2));
        } else {
            resultEdit->append("错误: 无法解析测试结果");
            QFile::remove(tempScriptPath);
            return false;
        }
    }

    // 清理临时脚本
    QFile::remove(tempScriptPath);

    return true;
}

QString UsbSpeedTestDialog::evaluatePerformance(double writeSpeed, double readSpeed)
{
    if (writeSpeed <= 0 || readSpeed <= 0) {
        return "✗ 测试失败 - 无法完成速度测试";
    } else if (writeSpeed >= 100.0 && readSpeed >= 150.0) {
        return "✓ 优秀 - USB 3.0或更高版本设备";
    } else if (writeSpeed >= 50.0 && readSpeed >= 80.0) {
        return "✓ 良好 - USB 3.0设备";
    } else if (writeSpeed >= 20.0 && readSpeed >= 30.0) {
        return "○ 一般 - USB 2.0设备";
    } else if (writeSpeed >= 5.0 && readSpeed >= 10.0) {
        return "✗ 较差 - USB 2.0设备性能较低";
    } else {
        return "✗ 极差 - 设备可能存在问题";
    }
}

void UsbSpeedTestDialog::clearCache()
{
    QProcess process;

    // 先同步文件系统
    process.start("sync");
    process.waitForFinished(3000);

    // 尝试以root权限清理缓存
    process.start("sh", QStringList() << "-c" << "sudo -n echo 'test' 2>/dev/null");
    process.waitForFinished(2000);

    if (process.exitCode() == 0) {
        // 有root权限，清理缓存
        process.start("sh", QStringList() << "-c" << "sudo sync && echo 3 | sudo tee /proc/sys/vm/drop_caches");
        process.waitForFinished(5000);
        resultEdit->append("✓ 已清理系统缓存");
    } else {
        // 没有root权限，只做sync
        process.start("sync");
        process.waitForFinished(2000);
        resultEdit->append("⚠ 无root权限，缓存清理受限");
    }
}
