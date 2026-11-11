#ifndef DEVICE_MONITOR_H
#define DEVICE_MONITOR_H

#include <QObject>
#include <QStorageInfo>
#include <QSerialPortInfo>
#include <QNetworkInterface>
#include <QTimer>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>

class DeviceMonitor : public QObject
{
    Q_OBJECT
public:
    explicit DeviceMonitor(QObject *parent = nullptr);
    
    void startMonitoring(int intervalMs = 5000);
    void stopMonitoring();
    
    QVariantMap getCurrentDevices() const;
    QVariantMap getLastSessionDevices() const;
    
signals:
    void deviceChanged(const QString &changeType, const QVariantMap &deviceInfo);
    void logMessage(const QString &message);
    
private slots:
    void checkDevices();
    
public:
    void saveCurrentDevices();
    
private:
    void loadLastDevices();
    QVariantMap enumerateDevices() const;
    
    QTimer *m_timer;
    QVariantMap m_lastDevices;
    QSettings m_settings;
};

#endif // DEVICE_MONITOR_H