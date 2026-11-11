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
#include <QDateTime>
#include <QListWidget>

// 前向声明
class BasicTester;
class ServerConnector;
class AutoStartManager;
class WakeOnLan;
class CycleController;
class SystemMonitor;
class DeviceManager;
class Logger;
class IllegalShutdownDetector;
class UnifiedControlManager;
class NetworkSpeedTester;

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
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onDeviceChanged(const QString &message);
    void onUsbDeviceChanged(const QString &action, const QString &deviceInfo);
    void onInputDeviceChanged(const QString &action, const QString &deviceInfo);
    void onDisplayChanged(const QString &action, const QString &displayInfo);
    void toggleAlwaysOnTop(bool on);
    void onCycleCompleted(); // 新增完成提示槽函数
    void clearDeviceInfoDisplay();  // 新增：清除设备信息显示
    void onIllegalShutdownDetected(const QDateTime &shutdownTime, const QString &logInfo);  // 新增：非法关机检测信号
    void onIllegalShutdownStateRestored(const QDateTime &shutdownTime, const QString &logInfo);  // 新增：非法关机状态恢复信号
    void showIllegalShutdownLog();  // 新增：显示非法关机日志
    void onRecheckIllegalShutdown();  // 新增：重新检测非法关机
    void onClearIllegalShutdown();    // 新增：清除非法关机记录
    void onUnifiedControl();           // 新增：统一控制
    void onUnifiedCommandReceived(const QString &command, const QStringList &params, const QString &senderIP);  // 新增：收到统一控制命令
    void onTagChanged(int index);      // 新增：标签切换
    void onAddTag();                   // 新增：添加标签
    void onManageTags();               // 新增：管理标签(删除等)
    void showDeviceChangeLog();        // 新增：查看设备变化日志

private:
    void setupUI();
    void setupFunctionModules();
    void connectModules();
    void setupWindowProperties();
    void startButtonBlinkAnimation();  // 新增：按钮闪烁动画
    void updateIllegalShutdownButton(const QDateTime &shutdownTime, const QString &logInfo);  // 新增：更新按钮显示
    void updateIllegalShutdownTooltip();  // 新增：更新工具提示信息
    void setupTagUI(QWidget *parentWidget);  // 新增：设置标签UI
    void loadTagsFromSettings();  // 新增：从设置加载标签列表
    void saveTagsToSettings();    // 新增：保存标签列表到设置
    int getSystemBootCount();     // 新增：获取系统累计启动次数（基于 btmp 日志）

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
    QPushButton *illegalShutdownButton;  // 新增：非法关机提示按钮

    // 系统监控UI
    QLabel *cpuTempLabel;
    QLabel *diskTempLabel;
    QLabel *cpuFanLabel;
    QProgressBar *cpuUsageBar;
    QProgressBar *memoryUsageBar;
    QProgressBar *gpuMemoryBar;
    QLabel *lastBootLabel;
    QLabel *uptimeLabel;
    
    // 开机/非法关机计数显示（分开显示）
    QLabel *bootCountLabel;      // 开机次数显示

    // 功能模块
    BasicTester *basicTester;
    ServerConnector *serverConnector;
    AutoStartManager *autoStartManager;
    WakeOnLan *wakeOnLan;
    CycleController *cycleController;
    SystemMonitor *systemMonitor;
    DeviceManager *deviceManager;
    Logger *logger;
    IllegalShutdownDetector *illegalShutdownDetector;  // 新增：非法关机检测器
    UnifiedControlManager *unifiedControlManager;  // 新增：统一控制管理器
    NetworkSpeedTester *lanSpeedService;  // 新增：内网测速服务（常驻，响应局域网内其他机器的测速请求）

    // 标签管理UI
    QComboBox *tagComboBox;         // 标签下拉框
    QPushButton *addTagButton;      // 添加标签按钮
    QPushButton *manageTagsButton;  // 管理标签按钮
    QLabel *tagStatusLabel;         // 统一控制状态标签

    // 非法关机检测相关
    QString lastIllegalShutdownLog;  // 保存最近的非法关机日志
    QDateTime lastShutdownTime;      // 非法关机发生时间
    QDateTime lastCheckTime;         // 上次检测时间
};

#endif // CLIENT_WINDOW_H
