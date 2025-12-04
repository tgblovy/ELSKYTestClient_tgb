#ifndef CLIENT_WINDOW_H
#define CLIENT_WINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QProgressBar>
#include <QSpinBox>

// 前向声明
class BasicTester;
class ServerConnector;
class AutoStartManager;
class WakeOnLan;
class CycleController;
class SystemMonitor;
class DeviceManager;
class Logger;

class ClientWindow : public QMainWindow
{
    Q_OBJECT

public:
    ClientWindow(QWidget *parent = nullptr);
    ~ClientWindow();

protected:
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onDeviceChanged(const QString &message);
    void onUsbDeviceChanged(const QString &action, const QString &deviceInfo);
    void onInputDeviceChanged(const QString &action, const QString &deviceInfo);
    void onDisplayChanged(const QString &action, const QString &displayInfo);
    void toggleAlwaysOnTop(bool on);
    void onCycleCompleted(); // 新增完成提示槽函数

private:
    void setupUI();
    void setupFunctionModules();
    void connectModules();
    void setupWindowProperties();

    // UI组件
    QCheckBox *serverCheckBox;
    QCheckBox *autoStartCheckBox;
    QComboBox *basicTestCombo;
    QPushButton *executeBasicTestButton;
    QPushButton *wolToggleButton;
    QPushButton *cycleToggleButton;
    QPushButton *deviceDetectButton;
    QPushButton *clearLogsButton;
    QComboBox *cycleOperationCombo;
    QSpinBox *cycleIntervalSpinBox;
    QSpinBox *cycleCountSpinBox;
    QLabel *cycleStatusLabel;
    QTextEdit *logTextEdit;
    QTextEdit *deviceInfoEdit;

    // 系统监控UI
    QLabel *cpuTempLabel;
    QLabel *diskTempLabel;
    QLabel *cpuFanLabel;
    QProgressBar *cpuUsageBar;
    QProgressBar *memoryUsageBar;
    QProgressBar *gpuMemoryBar;
    QLabel *lastBootLabel;
    QLabel *uptimeLabel;

    // 功能模块
    BasicTester *basicTester;
    ServerConnector *serverConnector;
    AutoStartManager *autoStartManager;
    WakeOnLan *wakeOnLan;
    CycleController *cycleController;
    SystemMonitor *systemMonitor;
    DeviceManager *deviceManager;
    Logger *logger;
};

#endif // CLIENT_WINDOW_H
