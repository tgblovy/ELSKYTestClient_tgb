#include "device_manager.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>

DeviceManager::DeviceManager(QObject *parent)
    : QObject(parent)
    , detectionTimer(nullptr)
    , fileWatcher(nullptr)
    , detectButton(nullptr)
    , deviceInfoEdit(nullptr)
    , isMonitoring(false)
{
    detectionTimer = new QTimer(this);
    connect(detectionTimer, &QTimer::timeout, this, &DeviceManager::checkDevices);

    fileWatcher = new QFileSystemWatcher(this);
    connect(fileWatcher, &QFileSystemWatcher::directoryChanged, this, &DeviceManager::onUsbDeviceChanged);
}

DeviceManager::~DeviceManager()
{
    stopDetection();
}

void DeviceManager::setupUI(QPushButton *button, QTextEdit *textEdit)
{
    detectButton = button;
    deviceInfoEdit = textEdit;

    connect(detectButton, &QPushButton::clicked, this, [this]() {
        if (!isMonitoring) {
            startDetection();
        } else {
            stopDetection();
        }
    });
}

void DeviceManager::startDetection()
{
    if (isMonitoring) return;

    isMonitoring = true;

    // 设置文件系统监控
    fileWatcher->addPath("/dev/input");
    fileWatcher->addPath("/dev/bus/usb");
    fileWatcher->addPath("/sys/class/drm");

    // 初始设备扫描
    checkDevices();

    // 启动定时器，每2秒检查一次
    detectionTimer->start(2000);

    if (detectButton) {
        detectButton->setText("停止检测");
        detectButton->setStyleSheet("QPushButton { background-color: #f44336; color: white; }");
    }

    emit deviceChanged("设备检测已启动 - 开始监控USB、鼠标、键盘、显示器等设备");
    emit logMessage("开始设备检测...");
}

void DeviceManager::stopDetection()
{
    if (!isMonitoring) return;

    isMonitoring = false;
    detectionTimer->stop();
    fileWatcher->removePaths(fileWatcher->directories());

    if (detectButton) {
        detectButton->setText("开始检测");
        detectButton->setStyleSheet("QPushButton { background-color: #FF9800; color: white; }");
    }

    emit deviceChanged("设备检测已停止");
    emit logMessage("停止设备检测");
}

void DeviceManager::performQuickScan()
{
    // 发送清除信号（如果需要）
    emit deviceChanged("=== 快速设备扫描开始 ===");
    emit deviceChanged("扫描时间: " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));

    // 清除设备状态缓存，强制重新检测所有设备
    previousUsbDevices.clear();
    previousInputDevices.clear();
    previousDisplayInfo.clear();

    checkUsbDevices();
    checkInputDevices();
    checkDisplayDevices();
    checkPciDevices();
    checkNetworkDevices();

    emit deviceChanged("=== 快速设备扫描结束 ===");
    emit logMessage("执行快速设备扫描");
}

void DeviceManager::checkDevices()
{
    checkUsbDevices();
    checkInputDevices();
    checkDisplayDevices();
}

void DeviceManager::onUsbDeviceChanged()
{
    checkUsbDevices();
}

void DeviceManager::onInputDeviceChanged()
{
    checkInputDevices();
}

void DeviceManager::onDisplayChanged()
{
    checkDisplayDevices();
}

void DeviceManager::checkUsbDevices()
{
    QSet<QString> currentUsbDevices;

    // 使用 lsusb 命令检查 USB 设备
    QProcess lsusbProcess;
    lsusbProcess.start("lsusb");
    if (lsusbProcess.waitForFinished(1000)) {
        QString output = lsusbProcess.readAllStandardOutput();

        // 修复：使用兼容的方式分割字符串
        QStringList lines = output.split('\n');
        QStringList nonEmptyLines;
        for (const QString &line : lines) {
            if (!line.trimmed().isEmpty()) {
                nonEmptyLines.append(line);
            }
        }

        for (const QString &line : nonEmptyLines) {
            QString deviceInfo = line.trimmed();
            currentUsbDevices.insert(deviceInfo);

            if (!previousUsbDevices.contains(deviceInfo)) {
                emit usbDeviceChanged("add", deviceInfo);
                emit deviceChanged("USB设备插入: " + deviceInfo);
            }
        }

        // 检查移除的设备
        for (const QString &device : previousUsbDevices) {
            if (!currentUsbDevices.contains(device)) {
                emit usbDeviceChanged("remove", device);
                emit deviceChanged("USB设备移除: " + device);
            }
        }

        previousUsbDevices = currentUsbDevices;
    }
}

void DeviceManager::checkInputDevices()
{
    QSet<QString> currentInputDevices;
    QDir inputDir("/dev/input");

    QStringList inputFiles = inputDir.entryList(QStringList() << "event*", QDir::System);

    for (const QString &inputFile : inputFiles) {
        QString devicePath = inputDir.absoluteFilePath(inputFile);
        QString deviceInfo = getInputDeviceInfo(devicePath);

        if (!deviceInfo.isEmpty()) {
            currentInputDevices.insert(devicePath);

            if (!previousInputDevices.contains(devicePath)) {
                emit inputDeviceChanged("add", deviceInfo);
                emit deviceChanged("输入设备插入: " + deviceInfo);
            }
        }
    }

    // 检查移除的设备
    for (const QString &device : previousInputDevices) {
        if (!currentInputDevices.contains(device)) {
            QString deviceInfo = getInputDeviceInfo(device);
            emit inputDeviceChanged("remove", deviceInfo);
            emit deviceChanged("输入设备移除: " + deviceInfo);
        }
    }

    previousInputDevices = currentInputDevices;
}

void DeviceManager::checkDisplayDevices()
{
    QString currentDisplayInfo = getDisplayInfo();

    if (currentDisplayInfo != previousDisplayInfo) {
        if (!previousDisplayInfo.isEmpty()) {
            emit displayChanged("change", currentDisplayInfo);
            emit deviceChanged("显示设备变化: " + currentDisplayInfo);
        }
        previousDisplayInfo = currentDisplayInfo;
    }
}

void DeviceManager::checkPciDevices()
{
    QProcess pciProcess;
    pciProcess.start("lspci", QStringList());

    if (pciProcess.waitForFinished(1000)) {
        QString output = pciProcess.readAllStandardOutput();

        // 修复：使用兼容的方式分割字符串
        QStringList lines = output.split('\n');
        QStringList nonEmptyLines;
        for (const QString &line : lines) {
            if (!line.trimmed().isEmpty()) {
                nonEmptyLines.append(line);
            }
        }

        emit deviceChanged("PCI设备列表:");
        for (const QString &device : nonEmptyLines) {
            if (device.contains("VGA") || device.contains("Display") ||
                device.contains("USB") || device.contains("Network")) {
                emit deviceChanged("  " + device.trimmed());
            }
        }
    }
}

void DeviceManager::checkNetworkDevices()
{
    QProcess ipProcess;
    ipProcess.start("ip", QStringList() << "link");

    if (ipProcess.waitForFinished(1000)) {
        QString output = ipProcess.readAllStandardOutput();

        // 修复：使用兼容的方式分割字符串
        QStringList lines = output.split('\n');
        QStringList nonEmptyLines;
        for (const QString &line : lines) {
            if (!line.trimmed().isEmpty()) {
                nonEmptyLines.append(line);
            }
        }

        emit deviceChanged("网络设备列表:");
        for (const QString &line : nonEmptyLines) {
            if (line.contains("state UP") || line.contains("state DOWN")) {
                emit deviceChanged("  " + line.trimmed());
            }
        }
    }
}

QString DeviceManager::getUsbDeviceInfo(const QString &devicePath)
{
    return "USB设备: " + devicePath;
}

QString DeviceManager::getInputDeviceInfo(const QString &devicePath)
{
    QString sysPath = devicePath;
    sysPath = sysPath.replace("/dev/input/", "/sys/class/input/") + "/device/name";

    QFile deviceNameFile(sysPath);
    if (deviceNameFile.open(QIODevice::ReadOnly)) {
        QString deviceName = deviceNameFile.readLine().trimmed();
        return QString("输入设备: %1 (%2)").arg(deviceName).arg(QFileInfo(devicePath).fileName());
    }

    return QString("输入设备: %1").arg(QFileInfo(devicePath).fileName());
}

QString DeviceManager::getDisplayInfo()
{
    QString displayInfo;

    // 检查DRM设备
    QDir drmDir("/sys/class/drm");
    QStringList cardDirs = drmDir.entryList(QStringList() << "card*", QDir::Dirs);

    for (const QString &cardDir : cardDirs) {
        QString cardPath = drmDir.absoluteFilePath(cardDir);

        // 检查连接状态
        QFile statusFile(cardPath + "/status");
        if (statusFile.open(QIODevice::ReadOnly)) {
            QString status = statusFile.readLine().trimmed();
            if (status == "connected") {
                displayInfo += QString("显示器: %1 ").arg(cardDir);
            }
            statusFile.close();
        }
    }

    // 使用xrandr获取显示信息（如果可用）
    QProcess xrandrProcess;
    xrandrProcess.start("xrandr", QStringList());
    if (xrandrProcess.waitForFinished(1000)) {
        QString output = xrandrProcess.readAllStandardOutput();

        // 修复：使用兼容的方式分割字符串
        QStringList lines = output.split('\n');
        QStringList nonEmptyLines;
        for (const QString &line : lines) {
            if (!line.trimmed().isEmpty()) {
                nonEmptyLines.append(line);
            }
        }

        for (const QString &line : nonEmptyLines) {
            if (line.contains(" connected")) {
                displayInfo += "[" + line.trimmed() + "] ";
            }
        }
    }

    return displayInfo.isEmpty() ? "未检测到显示器" : displayInfo;
}
