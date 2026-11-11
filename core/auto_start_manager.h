#ifndef AUTO_START_MANAGER_H
#define AUTO_START_MANAGER_H

#include <QObject>
#include <QCheckBox>
#include <QString>  // 确保包含QString

class AutoStartManager : public QObject
{
    Q_OBJECT

public:
    explicit AutoStartManager(QObject *parent = nullptr);

    void setupUI(QCheckBox *checkBox);
    bool setAutoStart(bool enable);
    bool isAutoStartEnabled();

    // 新增：循环状态感知接口
    void setCycleRunning(bool running);      // 设置循环运行状态
    bool isCycleRunning() const;             // 获取循环是否正在运行

signals:
    void logMessage(const QString &message);
    void autoStartStatusChanged(bool enabled);

private slots:
    void onAutoStartToggled(bool checked);

private:
    bool createSystemdService();
    QString createLauncherScript(const QString &appPath);  // 动态环境 launcher（Ubuntu/GNOME Wayland 修复）
    void safestRemoveSystemdService();
    bool removeSystemdService();
    bool checkServiceStatus();
    void verifyAutoStart();
    void checkUserIntentOnStartup();   // 新增：启动时检查用户意图标识

    // 用户意图标识管理
    bool getUserIntentFlag();          // 获取用户意图标识（是否曾主动勾选过开机自启）
    void setUserIntentFlag(bool set);  // 设置/清除用户意图标识

private:
    QCheckBox *autoStartCheckBox;
    bool isCancellingAutoStart;
    int verifyRetryCount;              // 验证重试计数器
    static const int MAX_VERIFY_RETRIES = 3;  // 最大重试次数
    bool isSystemVerification;         // 标记是否为系统自动验证（非用户操作）
    bool m_cycleRunning;              // 循环任务是否正在运行
};

#endif // AUTO_START_MANAGER_H
