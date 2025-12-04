#include "device_manager.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDebug>
#include <QPushButton>
#include <QTextEdit>

// 全局变量定义
QString homeDir = QDir::homePath();
QString logFilePath = homeDir + "/.monitor_log/device.log";

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

    // 程序启动时加载上次的设备日志
    loadPreviousDeviceLog();
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

void DeviceManager::loadPreviousDeviceLog()
{
    // 加载上次保存的设备日志
    QFile file(logFilePath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (!line.isEmpty()) {
                previousDeviceLog.append(line);
            }
        }
        file.close();
    }
}

void DeviceManager::saveDeviceLog()
{
    // 确保日志目录存在
    QFileInfo fileInfo(logFilePath);
    QDir logDir = fileInfo.absoluteDir();
    if (!logDir.exists()) {
        if (!logDir.mkpath(".")) {
            qWarning() << "无法创建日志目录:" << logDir.absolutePath();
            return;
        }
    }

    // 覆盖写入日志文件
    QFile file(logFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);

        // 写入时间戳
        out << "[" << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "] 设备扫描结果:\n";

        // 写入当前设备列表
        for (const QString &device : currentDeviceList) {
            out << device << "\n";
        }

        file.close();

        // 保存后更新上一次的日志记录
        previousDeviceLog = currentDeviceList;

        emit logMessage("设备日志已保存到: " + logFilePath);
    } else {
        qWarning() << "无法打开日志文件:" << logFilePath;
    }
}

void DeviceManager::compareAndNotify()
{
    // 比较当前设备列表和上次日志
    bool hasChanges = false;
    QString changesText;

    // 检查新增设备
//    for (const QString &device : currentDeviceList) {
//        if (!previousDeviceLog.contains(device)) {
//            changesText += "<font color='green'>[新增] " + device + "</font>\n";
//            hasChanges = true;
//        }
//    }

    // 检查移除设备
    QString ts=QDateTime::currentDateTime().toString("[hh:mm:ss] ");
    for (const QString &device : previousDeviceLog) {
        if (!currentDeviceList.contains(device)) {
            changesText += "<font color='red'>["+ts+"移除]" + device +"<br>"+"</font>";
            hasChanges = true;
        }
    }

    // 如果有变化，发送到设备信息窗口
    if (hasChanges && deviceInfoEdit) {
        // 先清空设备实时信息列表
        emit clearDeviceInfo();  // 发送清除信号

        // 添加时间戳和变化信息
        QString timestamp = QDateTime::currentDateTime().toString("[hh:mm:ss] ");
        deviceInfoEdit->append("<font color='blue'>"+timestamp + "设备状态变化检测:</font>");
        deviceInfoEdit->append("<font color='blue'>=========================</font>");
        deviceInfoEdit->append(changesText);
        deviceInfoEdit->append("<font color='blue'>=========================</font>");

        emit deviceChanged("<font color='blue'>检测到设备变化</font>");
    } else if (deviceInfoEdit) {
        // 如果没有变化，可以显示无变化信息
        deviceInfoEdit->clear();
        deviceInfoEdit->append("<font color='blue'>"+QDateTime::currentDateTime().toString("[hh:mm:ss] ") + "设备状态无变化</font>");
    }
}

void DeviceManager::startDetection()
{
    if (isMonitoring) return;

    isMonitoring = true;

    // 清空当前设备列表
    currentDeviceList.clear();

    // 设置文件系统监控
    fileWatcher->addPath("/dev/input");
    fileWatcher->addPath("/dev/bus/usb");
    fileWatcher->addPath("/sys/class/drm");

    // 清空设备信息显示
    if (deviceInfoEdit) {
        deviceInfoEdit->clear();
    }

    // 初始设备扫描
    checkDevices();

    // 保存初始设备日志（覆盖）
    saveDeviceLog();

    // 与上次日志比较（用于开机重启后的首次检测）
    compareAndNotify();

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
    // 清空设备信息显示
    if (deviceInfoEdit) {
        deviceInfoEdit->clear();
    }

    emit deviceChanged("=== 快速设备扫描开始 ===");
    emit deviceChanged("扫描时间: " + QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));

    // 清空当前设备列表
    currentDeviceList.clear();

    // 清除设备状态缓存
    previousUsbDevices.clear();
    previousInputDevices.clear();
    previousDisplayInfo.clear();

    checkUsbDevices();
    checkInputDevices();
    checkDisplayDevices();
    checkPciDevices();
    checkNetworkDevices();

    // 保存并比较
    saveDeviceLog();
    compareAndNotify();

    emit deviceChanged("=== 快速设备扫描结束 ===");
    emit logMessage("执行快速设备扫描");
}

// 注意：这里需要确保槽函数有正确的实现
void DeviceManager::checkDevices()
{
    // 清空当前设备列表
    currentDeviceList.clear();

    checkUsbDevices();
    checkInputDevices();
    checkDisplayDevices();
    checkPciDevices();
    checkNetworkDevices();

    // 每次检查后都与上次保存的日志比较
    compareAndNotify();
}

// 这里添加缺失的槽函数实现
void DeviceManager::onUsbDeviceChanged()
{
    // 当USB设备目录变化时触发
    checkUsbDevices();
    compareAndNotify();
}

void DeviceManager::onInputDeviceChanged()
{
    // 当输入设备目录变化时触发
    checkInputDevices();
    compareAndNotify();
}

void DeviceManager::onDisplayChanged()
{
    // 当显示设备目录变化时触发
    checkDisplayDevices();
    compareAndNotify();
}

void DeviceManager::checkUsbDevices()
{
    QSet<QString> currentUsbDevices;

    // 在设备列表中添加USB设备标题
    currentDeviceList.append("=== USB设备 ===");

    QProcess lsusbProcess;
    lsusbProcess.start("lsusb");
    if (lsusbProcess.waitForFinished(1000)) {
        QString output = lsusbProcess.readAllStandardOutput();
        QStringList lines = output.split('\n');

        for (const QString &line : lines) {
            if (!line.trimmed().isEmpty()) {
                QString deviceInfo = line.trimmed();
                currentUsbDevices.insert(deviceInfo);
                currentDeviceList.append(deviceInfo);

                if (!previousUsbDevices.contains(deviceInfo)) {
                    emit usbDeviceChanged("add", deviceInfo);
                    emit deviceChanged("USB设备插入: " + deviceInfo);
                }
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

    // 在设备列表中添加输入设备标题
    currentDeviceList.append("=== 输入设备 ===");

    for (const QString &inputFile : inputFiles) {
        QString devicePath = inputDir.absoluteFilePath(inputFile);
        QString deviceInfo = getInputDeviceInfo(devicePath);

        if (!deviceInfo.isEmpty()) {
            currentInputDevices.insert(devicePath);
            currentDeviceList.append(deviceInfo);

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

    // 在设备列表中添加显示设备标题和信息
    currentDeviceList.append("=== 显示设备 ===");
    currentDeviceList.append(currentDisplayInfo);

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
        QStringList lines = output.split('\n');

        // 在设备列表中添加PCI设备标题
        currentDeviceList.append("=== PCI设备 ===");

        for (const QString &line : lines) {
            if (!line.trimmed().isEmpty()) {
                currentDeviceList.append(line.trimmed());
                emit deviceChanged("PCI设备: " + line.trimmed());
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
        QStringList lines = output.split('\n');

        // 在设备列表中添加网络设备标题
        currentDeviceList.append("=== 网络设备 ===");

        for (const QString &line : lines) {
            if (!line.trimmed().isEmpty()) {
                currentDeviceList.append(line.trimmed());
                emit deviceChanged("网络设备: " + line.trimmed());
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

        QStringList lines = output.split('\n');
        for (const QString &line : lines) {
            if (!line.trimmed().isEmpty() && line.contains(" connected")) {
                displayInfo += "[" + line.trimmed() + "] ";
            }
        }
    }

    return displayInfo.isEmpty() ? "未检测到显示器" : displayInfo;
}
