#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <QObject>
#include <QTimer>
#include <QLabel>
#include <QProgressBar>

class SystemMonitor : public QObject
{
    Q_OBJECT

public:
    explicit SystemMonitor(QObject *parent = nullptr);
    ~SystemMonitor();

    void setupUI(QLabel *cpuTempLabel, QLabel *diskTempLabel, QLabel *cpuFanLabel,
                 QProgressBar *cpuUsageBar, QProgressBar *memoryUsageBar,
                 QProgressBar *gpuMemoryBar, QLabel *lastBootLabel, QLabel *uptimeLabel);
    void startMonitoring();
    void stopMonitoring();

signals:
    void logMessage(const QString &message);

private slots:
    void updateSystemMonitor();

private:
    // 平台检测方法
    QString getSystemArchitecture();
    bool isX86Architecture();
    bool isFTArchitecture();

    // 监控方法
    double getCpuTemperature();
    double getDiskTemperature();
    int getCpuFanSpeed();
    double getCpuUsage();
    double getMemoryUsage();
    double getGpuMemoryUsage();
    QDateTime getLastBootTime();
    qint64 getSystemUptime();
    QString formatUptime(qint64 seconds);

    void updateSystemMonitorX86();
    void updateSystemMonitorFT();
    void updateSystemMonitorGeneric();

private:
    QTimer *monitorTimer;
    QLabel *cpuTempLabel;
    QLabel *diskTempLabel;
    QLabel *cpuFanLabel;
    QProgressBar *cpuUsageBar;
    QProgressBar *memoryUsageBar;
    QProgressBar *gpuMemoryBar;
    QLabel *lastBootLabel;
    QLabel *uptimeLabel;
};

#endif // SYSTEM_MONITOR_H
