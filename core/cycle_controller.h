#ifndef CYCLE_CONTROLLER_H
#define CYCLE_CONTROLLER_H

#include <QObject>
#include <QTimer>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QString>
#include <QDialog>        // 添加这行
#include <QVBoxLayout>    // 添加这行
#include <QMessageBox>    // 添加这行
#include "../common/protocol.h"

class AutoStartManager;

class CycleController : public QObject
{
    Q_OBJECT

public:
    explicit CycleController(QObject *parent = nullptr);
    ~CycleController();

    void setupUI(QPushButton *toggleButton, QLabel *statusLabel,
                 QComboBox *operationCombo, QSpinBox *intervalSpin, QSpinBox *countSpin);
    void setAutoStartManager(AutoStartManager *manager) { autoStartManager = manager; }

    void startCycleWithConfig(CycleOperation operation, int intervalSeconds, int cycleCount);
    void stopCycle();
    void restoreCycle();
    void saveState();

    bool isRunning() const { return isCycleRunning; }

    // 新增公共方法用于处理开机启动状态变化
    void handleAutoStartStatusChanged(bool enabled);

signals:
    void cycleStatusChanged(const QString &status);
    void logMessage(const QString &message);
    void operationExecuted();
    void cycleCompleted(); // 新增完成信号

public slots:  // 改为 public slots
    void onCycleToggle();
    void onCycleTimer();
    void onCountdownTimer();
    void updateCycleStatus();

private:
    void executeCycleOperation();
    void saveCycleState();
    void loadCycleState();
    void performRestore();
    void clearCycleState();
    QString getOperationName(CycleOperation operation);
    bool isAutoStartEnabled();

private:
    QTimer *cycleTimer;
    QTimer *countdownTimer;
    QPushButton *toggleButton;
    QLabel *statusLabel;
    QComboBox *operationCombo;
    QSpinBox *intervalSpin;
    QSpinBox *countSpin;
    AutoStartManager *autoStartManager;

    bool isCycleRunning;
    int currentCycleCount;
    int countdownSeconds;
    CycleConfig currentCycleConfig;
    bool shouldAutoStartCycle;
    bool isFirstStartAfterReboot;
    bool isRestoringCycle;
};

#endif // CYCLE_CONTROLLER_H
