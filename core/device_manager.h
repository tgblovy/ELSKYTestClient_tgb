#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <QObject>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QSet>
#include <QString>
#include <QList>
#include <QMap>

class DeviceManager : public QObject
{
    Q_OBJECT

public:
    explicit DeviceManager(QObject *parent = nullptr);
    ~DeviceManager();

    void setupUI(QPushButton *detectButton, QTextEdit *deviceInfoEdit);
    void startDetection();
    void stopDetection();
    void performQuickScan();
    void loadPreviousDeviceLog();

    // 丢失设备管理
    void loadLostDeviceRecord();
    void displayLostDevices();
    void clearLostDeviceRecord();
    void clearDeviceChangeLog();  // 清空设备变化日志文件

signals:
    void deviceChanged(const QString &message);
    void usbDeviceChanged(const QString &action, const QString &deviceInfo);
    void inputDeviceChanged(const QString &action, const QString &deviceInfo);
    void displayChanged(const QString &action, const QString &displayInfo);
    void logMessage(const QString &message);
    void clearDeviceInfo();  // 清除设备信息信号

private slots:
    void checkDevices();
    void onWatchedDirectoryChanged();  // 统一热插拔事件处理（带去抖）

private:
    void checkUsbDevices();
    void checkInputDevices();
    void checkDisplayDevices();
    void checkPciDevices();
    void checkNetworkDevices();
    void checkBlockDevices();   // 磁盘/存储设备检测（sysfs，各架构通用）
    void checkSerialDevices();  // 串口设备检测（sysfs，各架构通用）
    void checkBoardInfo();      // 主板/BIOS信息（DMI，各架构通用）

    // 丢失设备管理
    void saveLostDeviceToLog(const QString &deviceInfo);
    void removeLostDeviceFromLog(const QString &deviceInfo);
    void checkLostDeviceRecovery();
    void appendLostDevicesToDisplay();
    void appendAddedDevicesToDisplay();

    // 历史记录持久化（断开/新增历史，重启后恢复）
    void saveLostDeviceHistory();   // 持久化断开历史
    void loadLostDeviceHistory();   // 加载断开历史
    void saveAddedDeviceHistory();  // 持久化新增历史
    void loadAddedDeviceHistory();  // 加载新增历史

    QString getUsbDeviceInfo(const QString &devicePath);
    QString getInputDeviceInfo(const QString &devicePath);
    QString getDisplayInfo();

    void saveDeviceLog();
    void compareAndNotify();
    void logDeviceChange(const QString &changeType, const QString &deviceInfo);
    void cleanupOldDeviceChangeLogs(const int &maxLines);

    QTimer *detectionTimer;
    QFileSystemWatcher *fileWatcher;
    QTimer *rescanDebounceTimer;  // 热插拔事件去抖定时器
    QPushButton *detectButton;
    QTextEdit *deviceInfoEdit;

    // 设备状态缓存（用于增删事件检测）
    QSet<QString> previousUsbDevices;
    QSet<QString> previousInputDevices;
    QSet<QString> previousNetworkDevices;
    QSet<QString> previousDisplayNames;
    QSet<QString> previousPciDevices;      // PCI设备变化检测（增量上报，避免每轮全量刷屏）
    QSet<QString> previousBlockDevices;    // 磁盘设备变化检测
    QSet<QString> previousSerialDevices;   // 串口设备变化检测
    QMap<QString, QString> inputDeviceNames;  // 输入设备 sysfs名 → 显示名（移除事件上报友好名称）
    QString previousDisplayInfo;

    // 当前设备信息
    QList<QString> currentDeviceList;
    // 基准设备列表（来自device.log，丢失判断以此为基准）
    QList<QString> baselineDeviceLog;
    // 上次保存的设备日志
    QList<QString> previousDeviceLog;

    // 丢失设备管理
    QMap<QString, QString> lostDeviceMap;  // deviceInfo -> 移除时间戳（当前仍丢失的设备）
    QMap<QString, QString> lostDeviceHistoryMap;  // 断开历史：deviceInfo -> 最后一次断开时间（恢复后仍保留，直观展示哪些设备断开/丢失过）
    QMap<QString, QString> addedDeviceHistoryMap;  // 新增历史：deviceInfo -> 最后一次新增时间（即使后续消失也保留）

    bool isMonitoring;
    bool m_suppressEvents;  // 初始扫描/快速扫描时抑制增删事件上报（防刷屏）
};

#endif // DEVICE_MANAGER_H
