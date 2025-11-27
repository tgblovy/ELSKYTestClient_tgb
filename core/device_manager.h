#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <QObject>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QSet>
#include <QString>  // 确保包含QString

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

signals:
    void deviceChanged(const QString &message);
    void usbDeviceChanged(const QString &action, const QString &deviceInfo);
    void inputDeviceChanged(const QString &action, const QString &deviceInfo);
    void displayChanged(const QString &action, const QString &displayInfo);
    void logMessage(const QString &message);

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

    QTimer *detectionTimer;
    QFileSystemWatcher *fileWatcher;
    QPushButton *detectButton;
    QTextEdit *deviceInfoEdit;

    // 设备状态缓存
    QSet<QString> previousUsbDevices;
    QSet<QString> previousInputDevices;
    QString previousDisplayInfo;
    //设备信息
     QList<QString> deviceList;

    bool isMonitoring;
};

#endif // DEVICE_MANAGER_H
