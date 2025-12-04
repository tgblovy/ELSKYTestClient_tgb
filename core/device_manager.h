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

signals:
    void deviceChanged(const QString &message);
    void usbDeviceChanged(const QString &action, const QString &deviceInfo);
    void inputDeviceChanged(const QString &action, const QString &deviceInfo);
    void displayChanged(const QString &action, const QString &displayInfo);
    void logMessage(const QString &message);
    void clearDeviceInfo();  // 新增：清除设备信息信号

private slots:
    void checkDevices();
    void onUsbDeviceChanged();
    void onInputDeviceChanged();
    void onDisplayChanged();

private:
    void checkUsbDevices();
    void checkInputDevices();
    void checkDisplayDevices();
    void checkPciDevices();
    void checkNetworkDevices();

    QString getUsbDeviceInfo(const QString &devicePath);
    QString getInputDeviceInfo(const QString &devicePath);
    QString getDisplayInfo();

    void saveDeviceLog();
    void compareAndNotify();

    QTimer *detectionTimer;
    QFileSystemWatcher *fileWatcher;
    QPushButton *detectButton;
    QTextEdit *deviceInfoEdit;

    // 设备状态缓存
    QSet<QString> previousUsbDevices;
    QSet<QString> previousInputDevices;
    QString previousDisplayInfo;

    // 当前设备信息
    QList<QString> currentDeviceList;
    // 上次保存的设备日志
    QList<QString> previousDeviceLog;

    bool isMonitoring;
};

#endif // DEVICE_MANAGER_H
