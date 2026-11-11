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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#else
#include <QTextCodec>
#endif
#include <QProcessEnvironment>

// 全局变量定义
QString homeDir = QDir::homePath();
QString logFilePath = homeDir + "/.monitor_log/device.log";
QString deviceChangeLogFilePath = homeDir + "/.monitor_log/deviceChange.log";  // 新增
QString lostDeviceLogFilePath = homeDir + "/.monitor_log/device_lost_record.log";  // 丢失设备记录
QString lostDeviceHistoryFilePath = homeDir + "/.monitor_log/device_lost_history.log";  // 断开历史（含已恢复，持久化）
QString addedDeviceHistoryFilePath = homeDir + "/.monitor_log/device_added_history.log";  // 新增历史（含后续消失，持久化）

// 辅助函数：启动进程并读取UTF-8输出，避免乱码
static QString runProcessWithUtf8(const QString &program, const QStringList &args = QStringList())
{
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LANG", "C");
    env.insert("LC_ALL", "C");
    process.setProcessEnvironment(env);
    process.start(program, args);

    if (process.waitForFinished(3000)) {
        QByteArray rawOutput = process.readAllStandardOutput();
        // 使用 QString::fromUtf8 解码（Qt5/Qt6 兼容）
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        return QString::fromUtf8(rawOutput);
#else
        QTextCodec *codec = QTextCodec::codecForName("UTF-8");
        if (codec) {
            return codec->toUnicode(rawOutput);
        }
        return QString::fromLocal8Bit(rawOutput);
#endif
    }
    return QString();
}

// 辅助函数：读取sysfs属性文件（去除首尾空白；失败返回空串）
// sysfs是所有Linux架构的标准接口（2.6+内核），不依赖任何外部命令，
// 是各架构（x86/飞腾ARM64等）下最可靠的硬件信息来源
static QString readSysfsFile(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        QByteArray data = f.readAll();
        f.close();
        return QString::fromUtf8(data).trimmed();
    }
    return QString();
}

DeviceManager::DeviceManager(QObject *parent)
    : QObject(parent)
    , detectionTimer(nullptr)
    , fileWatcher(nullptr)
    , rescanDebounceTimer(nullptr)
    , detectButton(nullptr)
    , deviceInfoEdit(nullptr)
    , isMonitoring(false)
    , m_suppressEvents(false)
{
    detectionTimer = new QTimer(this);
    connect(detectionTimer, &QTimer::timeout, this, &DeviceManager::checkDevices);

    fileWatcher = new QFileSystemWatcher(this);
    connect(fileWatcher, &QFileSystemWatcher::directoryChanged, this, &DeviceManager::onWatchedDirectoryChanged);

    // 热插拔事件去抖定时器（一次插拔会在多个被监控目录产生多个inotify事件）
    rescanDebounceTimer = new QTimer(this);
    rescanDebounceTimer->setSingleShot(true);
    rescanDebounceTimer->setInterval(300);
    connect(rescanDebounceTimer, &QTimer::timeout, this, &DeviceManager::checkDevices);

    // 程序启动时加载上次的设备日志作为基准
    loadPreviousDeviceLog();
    baselineDeviceLog = previousDeviceLog;

    // 加载丢失设备记录
    loadLostDeviceRecord();

    // 加载断开/新增历史（重启后恢复实时信息栏的最后记录）
    loadLostDeviceHistory();
    loadAddedDeviceHistory();
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        in.setEncoding(QStringConverter::Utf8);
#else
        in.setCodec("UTF-8");
#endif
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            // 过滤非设备行：时间戳行和分区标题行
            if (!line.isEmpty() && !line.startsWith("[") && !line.startsWith("===")) {
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
    if (file.open(QIODevice::WriteOnly)) {
        // 写入时间戳（直接写入UTF-8字节，避免QTextStream编码问题）
        // QByteArray lineData = QString("[%1] 设备扫描结果:\n")
        //     .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"))
        //     .toUtf8();
        // file.write(lineData);

        // 写入当前设备列表
        for (const QString &device : currentDeviceList) {
            file.write(device.toUtf8());
            file.write("\n");
        }

        file.close();

        // logDeviceChange在设备变化日志中添加分隔符，标记一次完整的扫描
        logDeviceChange("=== 设备扫描完成 ===",
                       QString("扫描时间: %1, 发现设备: %2 个")
                       .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"))
                       .arg(currentDeviceList.count()));

        emit logMessage("设备日志已保存到: " + logFilePath);
    } else {
        qWarning() << "无法打开日志文件:" << logFilePath;
    }
}

// 新增：记录设备变化到 deviceChange.log
void DeviceManager::logDeviceChange(const QString &changeType, const QString &deviceInfo)
{
    // 确保日志目录存在
    QFileInfo fileInfo(deviceChangeLogFilePath);
    QDir logDir = fileInfo.absoluteDir();
    if (!logDir.exists()) {
        if (!logDir.mkpath(".")) {
            qWarning() << "无法创建设备变化日志目录:" << logDir.absolutePath();
            return;
        }
    }

    // 追加写入设备变化日志
    QFile file(deviceChangeLogFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        out.setEncoding(QStringConverter::Utf8);
#else
        out.setCodec("UTF-8");
#endif
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        out << "[" << timestamp << "] " << changeType << ": " << deviceInfo << "\n";
        file.close();
    } else {
        qWarning() << "无法打开设备变化日志文件:" << deviceChangeLogFilePath;
    }
}

void DeviceManager::compareAndNotify()
{
    bool hasNewChanges = false;
    QString changesText;
    QString currentTime = QDateTime::currentDateTime().toString("[hh:mm:ss] ");

    // 检查新增设备（与基准设备列表比较：
    // 只有重新导入/启动快照里不存在、现在才检测到的设备才报"新增"；
    // 拔掉再插上的键盘属于恢复，不会误判为新增）
    QString addTime = QDateTime::currentDateTime().toString("yyyy.M.d HH:mm:ss");
    for (const QString &device : currentDeviceList) {
        if (device.startsWith("===")) continue;  // 跳过分区标题行
        if (!baselineDeviceLog.contains(device)) {
            hasNewChanges = true;

            // 仅在首次检测到新增时记录时间、写入设备变化日志和新增历史
            if (!addedDeviceHistoryMap.contains(device)) {
                addedDeviceHistoryMap[device] = addTime;
                logDeviceChange("新增设备", device);
                saveAddedDeviceHistory();  // 持久化，重启后恢复
            }

            changesText += "<font color='blue'>【" + addTime + "】新增设备：" + device + "</font><br>";
        }
    }

    // 检查移除设备（与基准设备列表比较，只有基准中的设备丢失才记录）
    for (const QString &device : baselineDeviceLog) {
        if (!currentDeviceList.contains(device)) {
            // 跳过分区标题行
            if (device.startsWith("===")) continue;

            hasNewChanges = true;

            // 仅在首次检测到移除时记录时间、写入丢失日志和设备变化日志
            if (!lostDeviceMap.contains(device)) {
                QString removalTime = QDateTime::currentDateTime().toString("yyyy.M.d HH:mm:ss");
                lostDeviceMap[device] = removalTime;
                lostDeviceHistoryMap[device] = removalTime;  // 记录最后一次断开时间（恢复后仍保留）
                saveLostDeviceToLog(device);
                logDeviceChange("移除设备", device);
                saveLostDeviceHistory();  // 持久化，重启后恢复
            }

            changesText += "<font color='red'>【" + lostDeviceMap[device] + "】丢失：" + device + "</font><br>";
        }
    }

    // 检查丢失设备是否恢复
    checkLostDeviceRecovery();

    // 更新设备信息窗口
    if (deviceInfoEdit) {
        emit clearDeviceInfo();

        if (hasNewChanges) {
            deviceInfoEdit->append("<font color='blue'>" + currentTime + "设备状态变化检测:</font>");
            deviceInfoEdit->append(changesText);
            emit deviceChanged("<font color='blue'>检测到设备变化</font>");
        }

        // 始终显示新增历史（含后续消失的设备）与断开历史（含已恢复设备）
        if (!addedDeviceHistoryMap.isEmpty()) {
            appendAddedDevicesToDisplay();
        }
        if (!lostDeviceHistoryMap.isEmpty()) {
            appendLostDevicesToDisplay();
        }

        if (hasNewChanges || !addedDeviceHistoryMap.isEmpty() || !lostDeviceHistoryMap.isEmpty()) {
            deviceInfoEdit->append("<font color='blue'>=========================</font>");
        } else {
            deviceInfoEdit->append("<font color='blue'>" + currentTime + "设备状态无变化</font>");
        }
    }
}

void DeviceManager::startDetection()
{
    if (isMonitoring) return;

    isMonitoring = true;

    // 清空当前设备列表
    currentDeviceList.clear();
    //清空指定行数的设备变化日志
    cleanupOldDeviceChangeLogs(5000);

    // 设置文件系统监控（热插拔即时感知，sysfs/devtmpfs在各架构Linux上均支持inotify）
    // 覆盖：输入设备、USB、显示、网络、磁盘、串口的热插拔目录
    QStringList watchPaths;
    watchPaths << "/dev/input"          // 输入设备节点（键鼠拔插）
               << "/dev/bus/usb"        // USB设备节点（USB拔插）
               << "/sys/class/drm"      // 显示接口状态（显示器拔插）
               << "/sys/class/net"      // 网络接口（网卡/USB网卡拔插）
               << "/sys/block"          // 块设备（磁盘/U盘拔插）
               << "/sys/class/tty"      // 串口设备（USB转串口等拔插）
               << "/sys/bus/usb/devices"; // USB设备sysfs（备用触发源）
    for (const QString &path : watchPaths) {
        if (QDir(path).exists()) {
            if (!fileWatcher->addPath(path)) {
                qWarning() << "无法添加监控目录:" << path;
            }
        } else {
            qDebug() << "监控目录不存在，跳过:" << path;
        }
    }

    // 清空设备信息显示
    if (deviceInfoEdit) {
        deviceInfoEdit->clear();
    }

    // 如果有丢失设备记录，先在设备窗口加载显示
    if (!lostDeviceMap.isEmpty()) {
        displayLostDevices();
    }

    // 保存旧基准用于检测上次运行后的丢失设备
    QList<QString> oldBaseline = baselineDeviceLog;

    // 初始设备扫描（抑制增删事件上报，避免把所有已存在设备都报告为"插入"）
    m_suppressEvents = true;
    currentDeviceList.clear();
    checkUsbDevices();
    checkInputDevices();
    checkDisplayDevices();
    checkPciDevices();
    checkNetworkDevices();
    checkBlockDevices();
    checkSerialDevices();
    checkBoardInfo();
    m_suppressEvents = false;

    // 保存初始设备日志（覆盖），并更新基准设备列表
    saveDeviceLog();
    baselineDeviceLog = currentDeviceList;
    previousDeviceLog = currentDeviceList;

    // 与旧基准比较，检测上次运行后的丢失设备
    if (!oldBaseline.isEmpty()) {
        for (const QString &device : oldBaseline) {
            if (device.startsWith("===")) continue;

            // 检查该设备是否仍存在（按设备名称模糊匹配）
            bool stillExists = false;
            QString deviceName = device;

            // 提取输入设备的名称部分用于匹配
            if (device.startsWith("输入设备:")) {
                int parenPos = device.lastIndexOf('(');
                if (parenPos != -1) {
                    deviceName = "输入设备:" + device.mid(8, parenPos - 9).trimmed();
                }
            }

            for (const QString &current : currentDeviceList) {
                if (current == device || current.contains(deviceName) || deviceName.contains(current)) {
                    stillExists = true;
                    break;
                }
            }

            if (!stillExists) {
                if (!lostDeviceMap.contains(device)) {
                    QString removalTime = QDateTime::currentDateTime().toString("yyyy.M.d HH:mm:ss");
                    lostDeviceMap[device] = removalTime;
                    lostDeviceHistoryMap[device] = removalTime;  // 记录最后一次断开时间（恢复后仍保留）
                    saveLostDeviceToLog(device);
                    saveLostDeviceHistory();  // 持久化，重启后恢复
                }
                logDeviceChange("移除设备", device);

                if (deviceInfoEdit) {
                    deviceInfoEdit->append("<font color='red'>【" + lostDeviceMap[device] + "】丢失：" + device + "</font><br>");
                }
            }
        }

        // 检查丢失设备是否恢复
        checkLostDeviceRecovery();
    }

    // 启动定时器，每10秒检查一次
    detectionTimer->start(10000);

    if (detectButton) {
        detectButton->setText("停止检测");
        detectButton->setStyleSheet("QPushButton { background-color: #f44336; color: white; }");
    }

    emit deviceChanged("设备检测已启动 - 监控USB、鼠标、键盘、显示器、磁盘、串口、网络等设备");
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
    previousNetworkDevices.clear();
    previousDisplayNames.clear();
    previousPciDevices.clear();
    previousBlockDevices.clear();
    previousSerialDevices.clear();
    previousDisplayInfo.clear();

    // 快速扫描（快照式，抑制增删事件刷屏，变化由 compareAndNotify 统一报告）
    m_suppressEvents = true;
    checkUsbDevices();
    checkInputDevices();
    checkDisplayDevices();
    checkPciDevices();
    checkNetworkDevices();
    checkBlockDevices();
    checkSerialDevices();
    checkBoardInfo();
    m_suppressEvents = false;

    // 保存并比较，更新基准设备列表
    saveDeviceLog();
    baselineDeviceLog = currentDeviceList;
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
    checkBlockDevices();
    checkSerialDevices();
    checkBoardInfo();

    // 每次检查后都与上次保存的日志比较
    compareAndNotify();

    // 更新上一次检测的设备列表（用于检测新增设备，不影响丢失判断基准）
    previousDeviceLog = currentDeviceList;
}

// 统一热插拔事件处理：任意被监控目录（/dev/input、/dev/bus/usb、/sys/class/drm、
// /sys/class/net、/sys/block、/sys/class/tty、/sys/bus/usb/devices）变化都会触发。
// 一次插拔会在多个目录产生多个 inotify 事件，用去抖定时器合并为一次完整重扫，
// 避免重复上报、避免旧基准被部分扫描污染。
void DeviceManager::onWatchedDirectoryChanged()
{
    if (!isMonitoring) return;
    rescanDebounceTimer->start();
}

void DeviceManager::checkUsbDevices()
{
    QSet<QString> currentUsbDevices;

    // 在设备列表中添加USB设备标题
    currentDeviceList.append("=== USB设备 ===");

    QString output = runProcessWithUtf8("lsusb");
    if (!output.isEmpty()) {
        QStringList lines = output.split('\n');

        for (const QString &line : lines) {
            if (!line.trimmed().isEmpty()) {
                // 从lsusb输出中提取设备名称用于比较
                // 格式: "Bus 001 Device 002: ID 046d:c077 Logitech, Inc. M105 Optical Mouse"
                QString deviceName;
                int idPos = line.indexOf("ID ");
                if (idPos != -1) {
                    // 跳过 "ID xxxx:xxxx " 部分，取后面的设备描述名称
                    int nameStart = line.indexOf(" ", idPos + 4);
                    if (nameStart != -1) {
                        deviceName = line.mid(nameStart + 1).trimmed();
                    }
                }

                if (deviceName.isEmpty()) {
                    deviceName = line.trimmed();
                }

                // 过滤掉Linux Foundation根集线器等系统设备
                if (deviceName.contains("Linux Foundation") ||
                    deviceName.contains("root hub")) {
                    continue;
                }

                currentUsbDevices.insert(deviceName);
                currentDeviceList.append(deviceName);

                if (!previousUsbDevices.contains(deviceName) && !m_suppressEvents) {
                    emit usbDeviceChanged("add", deviceName);
                    emit deviceChanged("USB设备插入: " + deviceName);
                }
            }
        }

        // 检查移除的设备
        for (const QString &device : previousUsbDevices) {
            if (!currentUsbDevices.contains(device)) {
                if (!m_suppressEvents) {
                    emit usbDeviceChanged("remove", device);
                    emit deviceChanged("USB设备移除: " + device);
                }
            }
        }

        previousUsbDevices = currentUsbDevices;
    }
}

void DeviceManager::checkInputDevices()
{
    // 使用 /sys/class/input 检查输入设备（不监控具体的 event 设备）
    QDir inputDir("/sys/class/input");

    // 在设备列表中添加输入设备标题
    currentDeviceList.append("=== 输入设备 ===");

    // 只查找命名的输入设备，跳过具体的 event* 文件
    QStringList inputEntries = inputDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    
    inputDeviceNames.clear();  // 成员变量：供移除事件上报友好名称
    for (const QString &entry : inputEntries) {
        QString namePath = inputDir.absoluteFilePath(entry + "/device/name");
        QFile nameFile(namePath);
        if (nameFile.exists() && nameFile.open(QIODevice::ReadOnly)) {
            QString deviceName = nameFile.readLine().trimmed();
            if (!deviceName.isEmpty()) {
                // 排除纯数字的 event 设备名称
                bool isNumeric = true;
                for (int i = 0; i < deviceName.length(); i++) {
                    if (!deviceName[i].isDigit()) {
                        isNumeric = false;
                        break;
                    }
                }
                if (!isNumeric && !entry.startsWith("event")) {
                    inputDeviceNames[entry] = deviceName;
                }
            }
            nameFile.close();
        }
    }

    QSet<QString> currentInputDevices;
    for (auto it = inputDeviceNames.begin(); it != inputDeviceNames.end(); ++it) {
        QString sysfsName = it.key();
        QString deviceName = it.value();
        
        QString mergedInfo = QString("输入设备: %1").arg(deviceName);

        currentInputDevices.insert(sysfsName);
        currentDeviceList.append(mergedInfo);

        if (!previousInputDevices.contains(sysfsName) && !m_suppressEvents) {
            emit inputDeviceChanged("add", mergedInfo);
            emit deviceChanged("输入设备插入: " + mergedInfo);
        }
    }

    // 检查移除的设备（用成员映射解析友好名称，避免只显示 sysfs 内部名）
    for (const QString &sysfsName : previousInputDevices) {
        if (!currentInputDevices.contains(sysfsName)) {
            QString friendlyName = inputDeviceNames.value(sysfsName, sysfsName);
            QString deviceInfo = "输入设备: " + friendlyName;
            if (!m_suppressEvents) {
                emit inputDeviceChanged("remove", deviceInfo);
                emit deviceChanged("输入设备移除: " + deviceInfo);
            }
        }
    }

    previousInputDevices = currentInputDevices;
}

void DeviceManager::checkDisplayDevices()
{
    QSet<QString> currentDisplayNames;

    // 在设备列表中添加显示设备标题
    currentDeviceList.append("=== 显示设备 ===");

    // 通过DRM检查已连接的显示器，直接比较名称
    QDir drmDir("/sys/class/drm");
    QStringList entries = drmDir.entryList(QStringList() << "card*", QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &entry : entries) {
        // 只处理输出端口（cardN-CONN-NAME格式），跳过cardN本身
        if (!entry.contains("-")) continue;

        QString cardPath = drmDir.absoluteFilePath(entry);
        QFile statusFile(cardPath + "/status");
        if (statusFile.open(QIODevice::ReadOnly)) {
            QString status = statusFile.readLine().trimmed();
            if (status == "connected") {
                // 提取显示接口名称: "card0-HDMI-A-1" -> "HDMI-A-1"
                int dashPos = entry.indexOf('-');
                QString displayName = entry.mid(dashPos + 1);

                currentDisplayNames.insert(displayName);
                currentDeviceList.append(displayName);

                if (!previousDisplayNames.contains(displayName) && !m_suppressEvents) {
                    emit displayChanged("add", displayName);
                    emit deviceChanged("显示设备插入: " + displayName);
                }
            }
            statusFile.close();
        }
    }

    // 检查移除的显示器
    for (const QString &display : previousDisplayNames) {
        if (!currentDisplayNames.contains(display)) {
            if (!m_suppressEvents) {
                emit displayChanged("remove", display);
                emit deviceChanged("显示设备移除: " + display);
            }
        }
    }

    previousDisplayNames = currentDisplayNames;
    previousDisplayInfo = QStringList(currentDisplayNames.values()).join(" ");
}

void DeviceManager::checkPciDevices()
{
    QString output = runProcessWithUtf8("lspci");

    if (!output.isEmpty()) {
        QStringList lines = output.split('\n');

        // 在设备列表中添加PCI设备标题
        currentDeviceList.append("=== PCI设备 ===");

        QSet<QString> currentPciDevices;
        for (const QString &line : lines) {
            QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) continue;

            currentPciDevices.insert(trimmed);
            currentDeviceList.append(trimmed);

            // 增量上报：只有新出现的PCI设备才上报，避免每轮全量刷屏
            if (!previousPciDevices.contains(trimmed) && !m_suppressEvents) {
                emit deviceChanged("PCI设备变化(新增): " + trimmed);
            }
        }

        // 检查移除的PCI设备
        for (const QString &device : previousPciDevices) {
            if (!currentPciDevices.contains(device) && !m_suppressEvents) {
                emit deviceChanged("PCI设备变化(移除): " + device);
            }
        }

        previousPciDevices = currentPciDevices;
    }
}

void DeviceManager::checkNetworkDevices()
{
    QSet<QString> currentNetworkDevices;

    QString output = runProcessWithUtf8("ip", QStringList() << "link");

    if (!output.isEmpty()) {
        QStringList lines = output.split('\n');

        // 在设备列表中添加网络设备标题
        currentDeviceList.append("=== 网络设备 ===");

        for (const QString &line : lines) {
            QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) continue;

            // 修复（C5）：从 ip link 输出中提取网卡名，只认主行
            // 主行格式: "2: enp8s0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 ..."
            // 续行格式: "    link/ether 00:11:22:33:44:55 brd ..."（含 MAC，冒号切分会
            // 切出 "11" 之类的段，旧代码把它误报为"网络设备插入"虚报事件）
            QRegularExpression mainLineRe("^\\d+:\\s*(\\S+):");
            QRegularExpressionMatch m = mainLineRe.match(trimmed);
            if (m.hasMatch()) {
                QString ifaceName = m.captured(1).trimmed();

                // 过滤掉回环接口lo
                if (ifaceName.isEmpty() || ifaceName == "lo") continue;

                currentNetworkDevices.insert(ifaceName);
                currentDeviceList.append(ifaceName);

                if (!previousNetworkDevices.contains(ifaceName) && !m_suppressEvents) {
                    emit deviceChanged("网络设备插入: " + ifaceName);
                }
            }
            // 不匹配主行格式的一律跳过（续行/别名行等）
        }

        // 检查移除的网络设备
        for (const QString &device : previousNetworkDevices) {
            if (!currentNetworkDevices.contains(device)) {
                if (!m_suppressEvents) {
                    emit deviceChanged("网络设备移除: " + device);
                }
            }
        }

        previousNetworkDevices = currentNetworkDevices;
    }
}

// === 磁盘/存储设备检测（/sys/block，纯sysfs，各架构通用，不依赖lsblk/fdisk） ===
void DeviceManager::checkBlockDevices()
{
    QDir blockDir("/sys/block");
    if (!blockDir.exists()) return;

    QSet<QString> currentBlockDevices;
    currentDeviceList.append("=== 磁盘设备 ===");

    const QStringList entries = blockDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &devName : entries) {
        // 跳过虚拟设备：loop（回环）、ram（内存盘）、dm-（device mapper）、zram
        if (devName.startsWith("loop") || devName.startsWith("ram")
            || devName.startsWith("dm-") || devName.startsWith("zram")) {
            continue;
        }

        QString devPath = blockDir.absoluteFilePath(devName);

        // 容量：size 文件单位为 512 字节扇区
        qint64 sectors = readSysfsFile(devPath + "/size").toLongLong();
        double gb = sectors * 512.0 / (1024.0 * 1024.0 * 1024.0);

        // 型号/厂商（SATA/USB/NVMe 磁盘提供；部分架构为空则只报设备名）
        QString model = readSysfsFile(devPath + "/device/model").trimmed();
        QString vendor = readSysfsFile(devPath + "/device/vendor").trimmed();
        QString removable = readSysfsFile(devPath + "/removable");  // "1"=可移动(U盘)

        QString info = QString("磁盘: %1 (%2%3%4)")
            .arg(devName)
            .arg(gb > 0.1 ? QString::number(gb, 'f', 1) + "GB" : "未知容量")
            .arg(model.isEmpty() ? "" : ", " + model)
            .arg(removable == "1" ? ", 可移动" : "");

        currentBlockDevices.insert(info);
        currentDeviceList.append(info);

        if (!previousBlockDevices.contains(info) && !m_suppressEvents) {
            emit deviceChanged("磁盘设备插入: " + info);
        }
    }

    // 检查移除的磁盘（U盘拔出的即时感知）
    for (const QString &device : previousBlockDevices) {
        if (!currentBlockDevices.contains(device)) {
            if (!m_suppressEvents) {
                emit deviceChanged("磁盘设备移除: " + device);
            }
        }
    }

    previousBlockDevices = currentBlockDevices;
}

// === 串口设备检测（/sys/class/tty，纯sysfs，各架构通用） ===
// 覆盖 ttyS*（板载串口）、ttyUSB*（USB转串口）、ttyACM*（调制解调器）、
// ttyAMA*/ttymxc*/ttySC* 等各SoC平台串口命名
void DeviceManager::checkSerialDevices()
{
    QDir ttyDir("/sys/class/tty");
    if (!ttyDir.exists()) return;

    QSet<QString> currentSerialDevices;
    currentDeviceList.append("=== 串口设备 ===");

    const QStringList entries = ttyDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &ttyName : entries) {
        // 排除内核内部终端与虚拟控制台
        if (ttyName == "tty" || ttyName == "console" || ttyName == "ptmx"
            || ttyName == "printk") {
            continue;
        }
        if (ttyName.startsWith("tty")) {
            // tty0..tty63 等纯数字为虚拟控制台，跳过
            const QString numPart = ttyName.mid(3);
            bool isNum = !numPart.isEmpty();
            for (int i = 0; i < numPart.size(); ++i) {
                if (!numPart[i].isDigit()) { isNum = false; break; }
            }
            if (isNum) continue;
        }

        // 必须有硬件设备节点（/sys/class/tty/<name>/device 存在）
        if (!QFile::exists(ttyDir.absoluteFilePath(ttyName) + "/device")) continue;

        // 获取驱动名（symlink target 最后一段），如 usb-serial / 8250 / amba-pl011
        QString driver;
        QFileInfo linkInfo(ttyDir.absoluteFilePath(ttyName) + "/device/driver");
        if (linkInfo.isSymLink()) {
            driver = QFileInfo(linkInfo.symLinkTarget()).fileName();
        }
        if (driver.isEmpty()) driver = "serial";

        QString info = QString("串口: %1 (%2)").arg(ttyName).arg(driver);
        currentSerialDevices.insert(info);
        currentDeviceList.append(info);

        if (!previousSerialDevices.contains(info) && !m_suppressEvents) {
            emit deviceChanged("串口设备插入: " + info);
        }
    }

    // 检查移除的串口
    for (const QString &device : previousSerialDevices) {
        if (!currentSerialDevices.contains(device)) {
            if (!m_suppressEvents) {
                emit deviceChanged("串口设备移除: " + device);
            }
        }
    }

    previousSerialDevices = currentSerialDevices;
}

// === 主板/平台信息（DMI sysfs + /proc，纯sysfs，各架构通用） ===
// 覆盖飞腾/麒麟、兆芯、海光、Intel、ARM嵌入式等所有平台
void DeviceManager::checkBoardInfo()
{
    currentDeviceList.append("=== 主板/平台信息 ===");

    // DMI 数据（/sys/class/dmi/id 在2.6+内核各架构均有，root可读部分文件，
    // 产品名/主板名等通常普通用户也可读）
    QString sysVendor   = readSysfsFile("/sys/class/dmi/id/sys_vendor");
    QString productName = readSysfsFile("/sys/class/dmi/id/product_name");
    QString productVer  = readSysfsFile("/sys/class/dmi/id/product_version");
    QString boardVendor = readSysfsFile("/sys/class/dmi/id/board_vendor");
    QString boardName   = readSysfsFile("/sys/class/dmi/id/board_name");
    QString biosVendor  = readSysfsFile("/sys/class/dmi/id/bios_vendor");
    QString biosVersion = readSysfsFile("/sys/class/dmi/id/bios_version");
    QString biosDate    = readSysfsFile("/sys/class/dmi/id/bios_date");

    // 主板序列号对非法关机/设备更换审计有价值（root可读，普通用户可能为空，正常）
    QString boardSerial = readSysfsFile("/sys/class/dmi/id/board_serial");
    if (!boardSerial.isEmpty() && boardSerial != "None") {
        currentDeviceList.append("主板序列号: " + boardSerial);
    }

    if (!sysVendor.isEmpty() || !productName.isEmpty()) {
        QString line = "系统: " + sysVendor + " " + productName + " " + productVer;
        currentDeviceList.append(line.trimmed());
    }
    if (!boardVendor.isEmpty() || !boardName.isEmpty()) {
        currentDeviceList.append("主板: " + boardVendor + " " + boardName);
    }
    if (!biosVersion.isEmpty()) {
        currentDeviceList.append("BIOS: " + biosVendor + " " + biosVersion + " (" + biosDate + ")");
    }

    // CPU 信息（/proc/cpuinfo 各架构格式有差异：x86用"model name"，ARM用"Processor"/"model name"）
    QFile cpuInfo("/proc/cpuinfo");
    if (cpuInfo.open(QIODevice::ReadOnly)) {
        QByteArray data = cpuInfo.readAll();
        cpuInfo.close();

        QString modelName;
        int coreCount = 0;
        const QStringList lines = QString::fromUtf8(data).split('\n');
        for (const QString &line : lines) {
            if (line.contains("model name") || line.contains("Processor")) {
                int idx = line.indexOf(':');
                if (idx != -1) {
                    QString val = line.mid(idx + 1).trimmed();
                    if (modelName.isEmpty()) modelName = val;
                }
            }
            if (line.startsWith("processor")) coreCount++;
        }
        if (!modelName.isEmpty()) {
            currentDeviceList.append("CPU: " + modelName + " (核心数 " + QString::number(coreCount) + ")");
        } else {
            // ARM 部分平台无 model name，用 /proc/device-tree 的 compatible 作为回退
            QString compatible = readSysfsFile("/proc/device-tree/compatible").trimmed();
            if (!compatible.isEmpty()) {
                currentDeviceList.append("CPU平台: " + compatible.replace('\0', ' '));
            }
        }
    }

    // 内存总量（/proc/meminfo 各架构通用）
    QFile memInfo("/proc/meminfo");
    if (memInfo.open(QIODevice::ReadOnly)) {
        QByteArray data = memInfo.readAll();
        memInfo.close();
        const QStringList lines = QString::fromUtf8(data).split('\n');
        for (const QString &line : lines) {
            if (line.startsWith("MemTotal")) {
                // 格式: "MemTotal:       16299548 kB"
                int idx = line.indexOf(':');
                if (idx != -1) {
                    QString val = line.mid(idx + 1).trimmed();
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
                    QStringList parts = val.split(' ', Qt::SkipEmptyParts);
#else
                    QStringList parts = val.split(' ', QString::SkipEmptyParts);
#endif
                    if (!parts.isEmpty()) {
                        qint64 kb = parts[0].toLongLong();
                        currentDeviceList.append(QString("内存: %1 GB")
                            .arg(kb / 1024.0 / 1024.0, 0, 'f', 1));
                    }
                }
                break;
            }
        }
    }

    // 主板信息为静态快照，仅在启动/快速扫描时随设备日志保存，不参与热插拔事件上报
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
    QString output = runProcessWithUtf8("xrandr");
    if (!output.isEmpty()) {

        QStringList lines = output.split('\n');
        for (const QString &line : lines) {
            if (!line.trimmed().isEmpty() && line.contains(" connected")) {
                displayInfo += "[" + line.trimmed() + "] ";
            }
        }
    }

    return displayInfo.isEmpty() ? "未检测到显示器" : displayInfo;
}

// 避免 deviceChange.log 文件过大
void DeviceManager::cleanupOldDeviceChangeLogs(const int &maxLines)
{
    QFile file(deviceChangeLogFilePath);
    if (!file.exists() || file.size() < 10 * 1024 * 1024) { // 小于10MB不清理
        return;
    }

    QStringList lines;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        in.setEncoding(QStringConverter::Utf8);
#else
        in.setCodec("UTF-8");
#endif
        while (!in.atEnd()) {
            lines.append(in.readLine());
        }
        file.close();
    }

    // 保留最近 maxLines 行
    if (lines.size() > maxLines) {
        lines = lines.mid(lines.size() - maxLines);

        if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            out.setEncoding(QStringConverter::Utf8);
#else
            out.setCodec("UTF-8");
#endif
            for (const QString &line : lines) {
                out << line << "\n";
            }
            file.close();

            emit logMessage(QString("设备变化日志已清理，保留最近 %1 行").arg(maxLines));
        }
    }
}

// === 丢失设备管理方法 ===

void DeviceManager::loadLostDeviceRecord()
{
    QFile file(lostDeviceLogFilePath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        in.setEncoding(QStringConverter::Utf8);
#else
        in.setCodec("UTF-8");
#endif
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (!line.isEmpty()) {
                // 解析格式: [2025.3.30 14:30:22] deviceInfo 或 [2025-03-30 14:30:22] deviceInfo
                QRegularExpression re(R"(\[(\d{4}[.\-]\d{1,2}[.\-]\d{1,2}\s+\d{2}:\d{2}:\d{2})\]\s*(.*)$)");
                QRegularExpressionMatch match = re.match(line);
                if (match.hasMatch()) {
                    QString timestamp = match.captured(1);
                    QString deviceInfo = match.captured(2);
                    lostDeviceMap[deviceInfo] = timestamp;
                }
            }
        }
        file.close();

        if (!lostDeviceMap.isEmpty()) {
            // 加载的丢失设备同样记入断开历史，便于直观查看哪些设备断开/丢失过
            for (auto it = lostDeviceMap.begin(); it != lostDeviceMap.end(); ++it) {
                if (!lostDeviceHistoryMap.contains(it.key())) {
                    lostDeviceHistoryMap[it.key()] = it.value();
                }
            }
            qDebug() << "加载丢失设备记录:" << lostDeviceMap.size() << "台设备未恢复";
        }
    }
}

// 通用：解析形如 "[时间] deviceInfo" 的历史文件，填充到目标 map
static void parseHistoryFile(const QString &path, QMap<QString, QString> &targetMap)
{
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    QTextStream in(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    in.setEncoding(QStringConverter::Utf8);
#else
    in.setCodec("UTF-8");
#endif
    QRegularExpression re(R"(\[(\d{4}[.\-]\d{1,2}[.\-]\d{1,2}\s+\d{2}:\d{2}:\d{2})\]\s*(.*)$)");
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        QRegularExpressionMatch match = re.match(line);
        if (match.hasMatch()) {
            targetMap[match.captured(2)] = match.captured(1);
        }
    }
    file.close();
}

// 通用：将 map 内容以 "[时间] deviceInfo" 覆盖写入文件
static void writeHistoryFile(const QString &path, const QMap<QString, QString> &sourceMap)
{
    QFileInfo fileInfo(path);
    QDir logDir = fileInfo.absoluteDir();
    if (!logDir.exists()) {
        logDir.mkpath(".");
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "无法写入历史文件:" << path;
        return;
    }
    QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec("UTF-8");
#endif
    for (auto it = sourceMap.begin(); it != sourceMap.end(); ++it) {
        out << "[" << it.value() << "] " << it.key() << "\n";
    }
    file.close();
}

void DeviceManager::loadLostDeviceHistory()
{
    parseHistoryFile(lostDeviceHistoryFilePath, lostDeviceHistoryMap);
}

void DeviceManager::saveLostDeviceHistory()
{
    writeHistoryFile(lostDeviceHistoryFilePath, lostDeviceHistoryMap);
}

void DeviceManager::loadAddedDeviceHistory()
{
    parseHistoryFile(addedDeviceHistoryFilePath, addedDeviceHistoryMap);
}

void DeviceManager::saveAddedDeviceHistory()
{
    writeHistoryFile(addedDeviceHistoryFilePath, addedDeviceHistoryMap);
}

void DeviceManager::displayLostDevices()
{
    if (!deviceInfoEdit || lostDeviceMap.isEmpty()) return;

    deviceInfoEdit->append("<font color='orange'>===== 丢失设备记录(上次运行) =====</font>");
    for (auto it = lostDeviceMap.begin(); it != lostDeviceMap.end(); ++it) {
        deviceInfoEdit->append("<font color='red'>【" + it.value() + "】丢失：" + it.key() + "</font>");
    }
    deviceInfoEdit->append("<font color='orange'>================================</font>");
}

void DeviceManager::saveLostDeviceToLog(const QString &deviceInfo)
{
    QFileInfo fileInfo(lostDeviceLogFilePath);
    QDir logDir = fileInfo.absoluteDir();
    if (!logDir.exists()) {
        logDir.mkpath(".");
    }

    QFile file(lostDeviceLogFilePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        out.setEncoding(QStringConverter::Utf8);
#else
        out.setCodec("UTF-8");
#endif
        out << "[" << QDateTime::currentDateTime().toString("yyyy.M.d HH:mm:ss") << "] " << deviceInfo << "\n";
        file.close();
    } else {
        qWarning() << "无法打开丢失设备日志文件:" << lostDeviceLogFilePath;
    }
}

void DeviceManager::removeLostDeviceFromLog(const QString &deviceInfo)
{
    QStringList lines;
    QFile file(lostDeviceLogFilePath);
    if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        in.setEncoding(QStringConverter::Utf8);
#else
        in.setCodec("UTF-8");
#endif
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (!line.contains(deviceInfo)) {
                lines.append(line);
            }
        }
        file.close();
    }

    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream out(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        out.setEncoding(QStringConverter::Utf8);
#else
        out.setCodec("UTF-8");
#endif
        for (const QString &line : lines) {
            out << line << "\n";
        }
        file.close();
    }
}

void DeviceManager::checkLostDeviceRecovery()
{
    QList<QString> recoveredDevices;

    for (auto it = lostDeviceMap.begin(); it != lostDeviceMap.end(); ++it) {
        if (currentDeviceList.contains(it.key())) {
            recoveredDevices.append(it.key());
        }
    }

    for (const QString &device : recoveredDevices) {
        QString lostTime = lostDeviceMap[device];
        lostDeviceMap.remove(device);  // 从"当前丢失"中移除，但断开历史保留
        removeLostDeviceFromLog(device);

        logDeviceChange("设备恢复", device);
    }

    // 恢复后重新显示断开历史，确保最后一次断开记录仍可见
    if (deviceInfoEdit && !lostDeviceHistoryMap.isEmpty()) {
        appendLostDevicesToDisplay();
    }
}

void DeviceManager::appendLostDevicesToDisplay()
{
    if (!deviceInfoEdit || lostDeviceHistoryMap.isEmpty()) return;

    deviceInfoEdit->append("<font color='orange'>--- 断开设备记录 ---</font>");
    for (auto it = lostDeviceHistoryMap.begin(); it != lostDeviceHistoryMap.end(); ++it) {
        if (lostDeviceMap.contains(it.key())) {
            // 当前仍处于断开状态
            deviceInfoEdit->append("<font color='red'>【" + it.value() + "】断开（未恢复）：" + it.key() + "</font>");
        } else {
            // 曾断开但已恢复，保留最后一次断开时间供用户直观查看
            deviceInfoEdit->append("<font color='green'>【" + it.value() + "】断开后已恢复：" + it.key() + "</font>");
        }
    }
}

void DeviceManager::appendAddedDevicesToDisplay()
{
    if (!deviceInfoEdit || addedDeviceHistoryMap.isEmpty()) return;

    deviceInfoEdit->append("<font color='orange'>--- 新增设备记录 ---</font>");
    for (auto it = addedDeviceHistoryMap.begin(); it != addedDeviceHistoryMap.end(); ++it) {
        if (currentDeviceList.contains(it.key())) {
            // 当前仍在线
            deviceInfoEdit->append("<font color='blue'>【" + it.value() + "】新增（在线）：" + it.key() + "</font>");
        } else {
            // 后续已消失，保留最后一次新增时间供用户直观查看
            deviceInfoEdit->append("<font color='gray'>【" + it.value() + "】新增后已消失：" + it.key() + "</font>");
        }
    }
}

void DeviceManager::clearLostDeviceRecord()
{
    lostDeviceMap.clear();
    lostDeviceHistoryMap.clear();  // 断开历史一并清空，重新导入后从当前快照重新记录
    addedDeviceHistoryMap.clear(); // 新增历史一并清空，重新导入后从当前快照重新记录

    // 同步清空持久化历史文件
    saveLostDeviceHistory();
    saveAddedDeviceHistory();

    // 清空丢失设备记录文件
    QFile file(lostDeviceLogFilePath);
    if (file.exists()) {
        file.remove();
    }

    // 重新导入即重置设备基线，设备变化日志一并清空，从当前快照重新记录
    clearDeviceChangeLog();

    emit logMessage("已清空丢失设备记录");
}

void DeviceManager::clearDeviceChangeLog()
{
    QFile file(deviceChangeLogFilePath);
    if (file.exists()) {
        file.remove();
    }

    emit logMessage("已清空设备变化日志");
}
