#include "cycle_controller.h"
#include "auto_start_manager.h"
#include "system_controller.h"
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QMessageBox>
#include <QApplication>
#include <QSettings>
#include <QDataStream>
#include <QDateTime>  // 添加这行

CycleController::CycleController(QObject *parent)
    : QObject(parent)
    , cycleTimer(nullptr)
    , countdownTimer(nullptr)
    , toggleButton(nullptr)
    , statusLabel(nullptr)
    , operationCombo(nullptr)
    , intervalSpin(nullptr)
    , countSpin(nullptr)
    , autoStartManager(nullptr)
    , isCycleRunning(false)
    , currentCycleCount(0)
    , countdownSeconds(0)
    , shouldAutoStartCycle(false)
    , isFirstStartAfterReboot(false)
    , isRestoringCycle(false)
{
    cycleTimer = new QTimer(this);
    connect(cycleTimer, &QTimer::timeout, this, &CycleController::onCycleTimer);

    countdownTimer = new QTimer(this);
    connect(countdownTimer, &QTimer::timeout, this, &CycleController::onCountdownTimer);

    // 延迟加载状态
    QTimer::singleShot(100, this, &CycleController::loadCycleState);
}

CycleController::~CycleController()
{
    if (isCycleRunning) {
        stopCycle();
    }
    saveState();
}

void CycleController::setupUI(QPushButton *toggleBtn, QLabel *statusLbl,
                            QComboBox *opCombo, QSpinBox *intervalSpn, QSpinBox *countSpn)
{
    toggleButton = toggleBtn;
    statusLabel = statusLbl;
    operationCombo = opCombo;      // 可能为 nullptr
    intervalSpin = intervalSpn;    // 可能为 nullptr
    countSpin = countSpn;          // 可能为 nullptr

    // 连接按钮点击事件
    if (toggleButton) {
        connect(toggleButton, &QPushButton::clicked, this, &CycleController::onCycleToggle);
    }

    // 即使组件为 nullptr 也设置默认值
    if (operationCombo) {
        operationCombo->addItem("关机", CYCLE_SHUTDOWN);
        operationCombo->addItem("重启", CYCLE_REBOOT);
        operationCombo->addItem("睡眠", CYCLE_SUSPEND);
        operationCombo->addItem("休眠", CYCLE_HIBERNATE);
    }

    if (intervalSpin) {
        intervalSpin->setRange(10, 86400);
        intervalSpin->setValue(60);
        intervalSpin->setSuffix("秒");
    }

    if (countSpin) {
        countSpin->setRange(1, 9999);
        countSpin->setValue(1);
        countSpin->setSuffix("次");
    }

    updateCycleStatus();
}

void CycleController::startCycleWithConfig(CycleOperation operation, int intervalSeconds, int cycleCount)
{
    // 检查开机启动状态
    if (!isAutoStartEnabled()) {
        emit logMessage("错误: 开机启动服务未启用，无法启动循环控制");
        QMessageBox::warning(nullptr, "警告",
            "开机启动设置未生效，无法启动循环控制。\n\n"
            "请确保开机启动服务已正确设置并启用。");
        return;
    }

    // 设置循环参数
    currentCycleConfig.operation = operation;
    currentCycleConfig.intervalSeconds = intervalSeconds;
    currentCycleConfig.cycleCount = cycleCount;
    currentCycleConfig.enabled = true;

    currentCycleCount = 0;
    countdownSeconds = currentCycleConfig.intervalSeconds;
    isFirstStartAfterReboot = false;

    // 更新UI状态
    if (toggleButton) {
        toggleButton->setText("停止循环");
        toggleButton->setStyleSheet("QPushButton { background-color: #f44336; color: white; }");
    }

    isCycleRunning = true;
    shouldAutoStartCycle = true;

    // 启动定时器
    countdownTimer->start(1000);
    updateCycleStatus();
    saveCycleState();

    QString operationName = getOperationName(operation);
    emit logMessage(QString("开始循环 - 操作: %1, 间隔: %2秒, 次数: %3次")
                  .arg(operationName)
                  .arg(intervalSeconds)
                  .arg(cycleCount));

    // 重要提示
    if (operation == CYCLE_SHUTDOWN || operation == CYCLE_REBOOT) {
        emit logMessage("重要：系统将执行" + operationName + "操作，重启后程序将自动恢复循环任务");
        emit logMessage("请勿手动关闭程序，确保开机启动服务正常工作");
    }
}

void CycleController::stopCycle()
{
    // 只有在循环运行时才显示停止日志
    if (isCycleRunning) {
        emit logMessage("正在安全停止循环任务...");
    }

    // 先停止所有定时器
    if (countdownTimer && countdownTimer->isActive()) {
        countdownTimer->stop();
    }
    if (cycleTimer && cycleTimer->isActive()) {
        cycleTimer->stop();
    }

    // 关键修复：更新UI状态
    if (toggleButton) {
        toggleButton->setText("开始循环");
        toggleButton->setStyleSheet("QPushButton { background-color: #9C27B0; color: white; }");
        toggleButton->setEnabled(true);
    }

    if (statusLabel) {
        // 根据是否完成来显示不同的状态
        if (currentCycleCount >= currentCycleConfig.cycleCount && currentCycleConfig.cycleCount > 0) {
            statusLabel->setText("循环已完成");
            statusLabel->setStyleSheet("QLabel { color: #4CAF50; font-size: 12px; }");
        } else {
            statusLabel->setText("循环已停止");
            statusLabel->setStyleSheet("QLabel { color: #666; font-size: 12px; }");
        }
    }

    // 更新状态变量
    bool wasRunning = isCycleRunning;
    isCycleRunning = false;
    shouldAutoStartCycle = false;
    isFirstStartAfterReboot = false;

    // 只有在主动停止时才记录日志
    if (wasRunning) {
        emit logMessage("循环已安全停止 - 已完成 " + QString::number(currentCycleCount) + " 次操作");
    }

    // 关键修复：如果是完成状态，立即清除状态
    if (currentCycleCount >= currentCycleConfig.cycleCount) {
        clearCycleState();
    } else {
        // 否则延迟清除状态
        QTimer::singleShot(1000, this, &CycleController::clearCycleState);
    }
}

void CycleController::restoreCycle()
{
    if (isRestoringCycle) {
        emit logMessage("恢复操作正在进行中，直接调用performRestore");
        performRestore();
        return;
    }

    isRestoringCycle = true;
    performRestore();
    isRestoringCycle = false;
}

void CycleController::saveState()
{
    saveCycleState();
}

void CycleController::onCycleToggle()
{
    if (!isCycleRunning) {
        // 启动循环 - 检查开机启动状态
        if (!isAutoStartEnabled()) {
            QMessageBox::warning(nullptr, "警告",
                "请先设置开机启动，然后再设置循环控制功能。\n\n"
                "循环控制中的关机/重启操作需要客户端开机自启动才能继续执行后续循环。\n"
                "请点击\"开机启动\"按钮进行设置。");
            return;
        }

        // 弹出设置对话框
        QDialog dialog;
        dialog.setWindowTitle("设置循环参数");
        dialog.setFixedSize(300, 200);

        QVBoxLayout *layout = new QVBoxLayout(&dialog);

        // 操作选择
        QHBoxLayout *opLayout = new QHBoxLayout();
        QLabel *opLabel = new QLabel("操作:", &dialog);
        QComboBox *opCombo = new QComboBox(&dialog);
        opCombo->addItem("关机", CYCLE_SHUTDOWN);
        opCombo->addItem("重启", CYCLE_REBOOT);
        opCombo->addItem("睡眠", CYCLE_SUSPEND);
        opCombo->addItem("休眠", CYCLE_HIBERNATE);
        opLayout->addWidget(opLabel);
        opLayout->addWidget(opCombo);

        // 等待时间
        QHBoxLayout *intervalLayout = new QHBoxLayout();
        QLabel *intervalLabel = new QLabel("等待时间:", &dialog);
        QSpinBox *intervalSpin = new QSpinBox(&dialog);
        intervalSpin->setRange(10, 86400);
        intervalSpin->setValue(60);
        intervalSpin->setSuffix("秒");
        intervalLayout->addWidget(intervalLabel);
        intervalLayout->addWidget(intervalSpin);

        // 循环次数
        QHBoxLayout *countLayout = new QHBoxLayout();
        QLabel *countLabel = new QLabel("循环次数:", &dialog);
        QSpinBox *countSpin = new QSpinBox(&dialog);
        countSpin->setRange(1, 9999);
        countSpin->setValue(1);
        countSpin->setSuffix("次");
        countLayout->addWidget(countLabel);
        countLayout->addWidget(countSpin);

        // 按钮
        QHBoxLayout *buttonLayout = new QHBoxLayout();
        QPushButton *okButton = new QPushButton("确定", &dialog);
        QPushButton *cancelButton = new QPushButton("取消", &dialog);
        buttonLayout->addWidget(okButton);
        buttonLayout->addWidget(cancelButton);

        layout->addLayout(opLayout);
        layout->addLayout(intervalLayout);
        layout->addLayout(countLayout);
        layout->addLayout(buttonLayout);

        connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
        connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

        if (dialog.exec() == QDialog::Accepted) {
            startCycleWithConfig(static_cast<CycleOperation>(opCombo->currentData().toInt()),
                               intervalSpin->value(), countSpin->value());
        }
    } else {
        // 停止循环 - 添加确认对话框
        QMessageBox::StandardButton result = QMessageBox::question(
            nullptr,
            "确认停止",
            "确定要停止当前循环任务吗？\n\n已完成 " + QString::number(currentCycleCount) + " 次操作",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );

        if (result == QMessageBox::Yes) {
            stopCycle();
        }
    }
}

void CycleController::handleAutoStartStatusChanged(bool enabled)
{
    if (!enabled && isCycleRunning) {
        emit logMessage("警告: 开机启动已被取消，但循环任务仍在运行");
        emit logMessage("系统重启后将无法自动恢复循环任务");

        // 询问用户是否停止循环
        QMessageBox::StandardButton result = QMessageBox::question(
            nullptr,
            "开机启动已取消",
            "检测到开机启动已被取消，但循环任务包含关机/重启操作。\n\n"
            "系统重启后程序将无法自动恢复循环任务。\n\n"
            "是否停止当前循环任务？",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes
        );

        if (result == QMessageBox::Yes) {
            stopCycle();
        }
    }
}

void CycleController::onCycleTimer()
{
    executeCycleOperation();
}

void CycleController::onCountdownTimer()
{
    if (!isCycleRunning) {
        countdownTimer->stop();
        return;
    }

    countdownSeconds--;

    // 减少状态保存频率：每30秒保存一次，或者在倒计时结束时保存
    static int saveCounter = 0;
    if (++saveCounter >= 30 || countdownSeconds <= 0) {
        saveCycleState();
        saveCounter = 0;
    }

    if (countdownSeconds <= 0) {
        emit logMessage("倒计时结束，执行循环操作...");
        countdownTimer->stop();
        executeCycleOperation();
        return;
    }

    updateCycleStatus();
}

void CycleController::executeCycleOperation()
{
    // 检查是否完成所有循环
    if (currentCycleCount >= currentCycleConfig.cycleCount) {
        emit logMessage("所有循环已完成，正在清理状态...");

        // 安全地停止循环
        isCycleRunning = false;
        shouldAutoStartCycle = false;

        // 更新UI状态
        if (toggleButton) {
            toggleButton->setText("开始循环");
            toggleButton->setStyleSheet("QPushButton { background-color: #9C27B0; color: white; }");
            toggleButton->setEnabled(true);
        }

        if (statusLabel) {
            statusLabel->setText("循环已完成");
            statusLabel->setStyleSheet("QLabel { color: #4CAF50; font-size: 12px; }");
        }

        // 清除状态
        clearCycleState();

        // 发射完成信号
        emit logMessage("循环任务已完成所有操作");
        emit cycleCompleted();

        return;
    }

    // 增加当前计数
    currentCycleCount++;

    QString operationName;
    bool willTerminateClient = false;
    bool isSuspendOperation = false; // 新增：标记是否为睡眠/休眠操作

    switch (currentCycleConfig.operation) {
    case CYCLE_SHUTDOWN:
        operationName = "关机";
        willTerminateClient = true;
        break;
    case CYCLE_REBOOT:
        operationName = "重启";
        willTerminateClient = true;
        break;
    case CYCLE_SUSPEND:
        operationName = "睡眠";
        isSuspendOperation = true;
        break;
    case CYCLE_HIBERNATE:
        operationName = "休眠";
        isSuspendOperation = true;
        break;
    }

    emit logMessage(QString("执行循环操作 %1/%2: %3")
                  .arg(currentCycleCount)
                  .arg(currentCycleConfig.cycleCount)
                  .arg(operationName));

    // 关键修复：对于会导致客户端终止的操作，设置重启标记
    if (willTerminateClient && currentCycleCount < currentCycleConfig.cycleCount) {
        isFirstStartAfterReboot = true;
        emit logMessage("设置重启标记，下次启动时将重置倒计时");
    } else if (willTerminateClient && currentCycleCount >= currentCycleConfig.cycleCount) {
        // 最后一次操作，设置完成标记
        QSettings settings("ELSKY", "TestClient");
        settings.setValue("cycle/showCompletionDialog", true);
        settings.sync();
        emit logMessage("设置完成提示标记");
    }

    // 保存状态（在可能终止客户端的操作之前）
    saveCycleState();
    emit logMessage("状态已保存，准备执行操作");

    // 对于会导致客户端终止的操作，延迟执行系统命令
    if (willTerminateClient) {
        emit logMessage("系统将在3秒后执行" + operationName + "操作...");
        if (currentCycleCount < currentCycleConfig.cycleCount) {
            emit logMessage("客户端重启后将自动恢复循环任务");
        } else {
            emit logMessage("这是最后一次操作，完成后将显示完成提示");
        }

        countdownTimer->stop();
        cycleTimer->stop();

        // 更新UI状态显示准备中
        if (statusLabel) {
            QString statusText = QString("正在准备%1...").arg(operationName);
            statusLabel->setText(statusText);
            statusLabel->setStyleSheet("QLabel { color: #f44336; font-weight: bold; }");
        }

        QApplication::processEvents();

        QTimer::singleShot(3000, this, [this, operationName]() {
            emit logMessage("正在执行" + operationName + "操作...");

            if (currentCycleConfig.operation == CYCLE_SHUTDOWN) {
                SystemController::shutdown();
            } else if (currentCycleConfig.operation == CYCLE_REBOOT) {
                SystemController::reboot();
            }
        });
    } else if (isSuspendOperation) {
        // 关键修复：改进睡眠/休眠操作的处理
        emit logMessage("正在执行" + operationName + "操作...");

        // 检查是否是最后一次操作
        bool isLastOperation = (currentCycleCount >= currentCycleConfig.cycleCount);

        if (isLastOperation) {
            emit logMessage("这是最后一次" + operationName + "操作，完成后将显示完成提示");
        }

        QApplication::processEvents();

        // 执行睡眠/休眠操作
        if (currentCycleConfig.operation == CYCLE_SUSPEND) {
            SystemController::suspend();
        } else if (currentCycleConfig.operation == CYCLE_HIBERNATE) {
            SystemController::hibernate();
        }

        // 关键修复：改进恢复后的处理逻辑
        QTimer::singleShot(3000, this, [this, isLastOperation, operationName]() {
            emit logMessage("系统已从" + operationName + "状态恢复");

            if (isCycleRunning) {
                if (isLastOperation) {
                    // 最后一次操作完成，停止循环
                    emit logMessage("最后一次" + operationName + "操作已完成，停止循环");
                    stopCycle();
                    emit cycleCompleted();
                } else {
                    // 继续下一次循环
                    emit logMessage("继续下一次循环");
                    countdownSeconds = currentCycleConfig.intervalSeconds;
                    countdownTimer->start(1000);
                    updateCycleStatus();
                    saveCycleState();
                }
            } else {
                emit logMessage("循环已停止，不再继续");
            }
        });
    }
}

void CycleController::updateCycleStatus()
{
    if (isCycleRunning && statusLabel) {
        QString operationName = getOperationName(currentCycleConfig.operation);
        QString statusText;

        if (countdownSeconds > 0) {
            statusText = QString("%1 - 第%2/%3次 - 倒计时: %4秒")
                        .arg(operationName)
                        .arg(currentCycleCount + 1)
                        .arg(currentCycleConfig.cycleCount)
                        .arg(countdownSeconds);
            statusLabel->setStyleSheet("QLabel { color: #FF9800; font-size: 12px; }");
        } else {
            statusText = QString("%1 - 第%2/%3次 - 执行中...")
                        .arg(operationName)
                        .arg(currentCycleCount + 1)
                        .arg(currentCycleConfig.cycleCount);
            statusLabel->setStyleSheet("QLabel { color: #f44336; font-size: 12px; }");
        }

        statusLabel->setText(statusText);
    } else if (statusLabel) {
        // 关键修复：区分完成和停止状态
        if (currentCycleCount >= currentCycleConfig.cycleCount && currentCycleConfig.cycleCount > 0) {
            statusLabel->setText("循环已完成");
            statusLabel->setStyleSheet("QLabel { color: #4CAF50; font-size: 12px; }");
        } else if (shouldAutoStartCycle) {
            statusLabel->setText("等待恢复循环...");
            statusLabel->setStyleSheet("QLabel { color: #FF9800; font-size: 12px; }");
        } else {
            statusLabel->setText("循环未开始");
            statusLabel->setStyleSheet("QLabel { color: #666; font-size: 12px; }");
        }
    }
}

void CycleController::saveCycleState()
{
    QSettings settings("ELSKY", "TestClient");

    settings.setValue("cycle/isRunning", isCycleRunning);
    settings.setValue("cycle/currentCount", currentCycleCount);
    settings.setValue("cycle/countdownSeconds", countdownSeconds);
    settings.setValue("cycle/shouldAutoStartCycle", shouldAutoStartCycle && isAutoStartEnabled());
    settings.setValue("cycle/isFirstStartAfterReboot", isFirstStartAfterReboot);

    settings.setValue("cycleConfig/operation", static_cast<int>(currentCycleConfig.operation));
    settings.setValue("cycleConfig/intervalSeconds", currentCycleConfig.intervalSeconds);
    settings.setValue("cycleConfig/cycleCount", currentCycleConfig.cycleCount);
    settings.setValue("cycleConfig/enabled", currentCycleConfig.enabled);

    settings.setValue("cycle/lastSaveTime", QDateTime::currentDateTime().toString(Qt::ISODate));

    settings.sync();

    // 只在重要状态变化时记录日志
    static int saveCount = 0;
    if (++saveCount % 10 == 0 || !isCycleRunning) { // 每10次保存或停止时记录一次
        emit logMessage("循环状态已保存");
    }
}

void CycleController::loadCycleState()
{
    QSettings settings("ELSKY", "TestClient");

    isCycleRunning = settings.value("cycle/isRunning", false).toBool();
    currentCycleCount = settings.value("cycle/currentCount", 0).toInt();
    countdownSeconds = settings.value("cycle/countdownSeconds", 0).toInt();
    shouldAutoStartCycle = settings.value("cycle/shouldAutoStartCycle", false).toBool();
    isFirstStartAfterReboot = settings.value("cycle/isFirstStartAfterReboot", false).toBool();

    currentCycleConfig.operation = static_cast<CycleOperation>(settings.value("cycleConfig/operation", CYCLE_SHUTDOWN).toInt());
    currentCycleConfig.intervalSeconds = settings.value("cycleConfig/intervalSeconds", 60).toInt();
    currentCycleConfig.cycleCount = settings.value("cycleConfig/cycleCount", 1).toInt();
    currentCycleConfig.enabled = settings.value("cycleConfig/enabled", false).toBool();

    // 关键修复：检查是否需要显示完成提示
    bool showCompletionDialog = settings.value("cycle/showCompletionDialog", false).toBool();
    if (showCompletionDialog) {
        settings.setValue("cycle/showCompletionDialog", false);
        settings.sync();

        // 延迟显示完成对话框，确保程序完全启动
        QTimer::singleShot(3000, this, [this]() {
            emit logMessage("循环任务已完成所有操作");
            emit cycleCompleted();

            // 关键修复：确保UI状态正确更新
            if (toggleButton) {
                toggleButton->setText("开始循环");
                toggleButton->setStyleSheet("QPushButton { background-color: #9C27B0; color: white; }");
            }
            if (statusLabel) {
                statusLabel->setText("循环已完成");
                statusLabel->setStyleSheet("QLabel { color: #4CAF50; font-size: 12px; }");
            }
        });
    }

    emit logMessage(QString("加载循环状态 - 运行: %1, 计数: %2/%3, 自动启动: %4")
                  .arg(isCycleRunning)
                  .arg(currentCycleCount)
                  .arg(currentCycleConfig.cycleCount)
                  .arg(shouldAutoStartCycle));

    // 关键修复：简化恢复逻辑，避免竞争条件
    if (shouldAutoStartCycle && isCycleRunning && currentCycleCount < currentCycleConfig.cycleCount) {
        emit logMessage("检测到需要恢复的循环任务");

        // 立即设置恢复标志，防止重复调用
        isRestoringCycle = true;

        // 关键修复：更短的延迟，确保只调用一次
        QTimer::singleShot(2000, this, [this]() {
            emit logMessage("执行延迟恢复检查...");

            // 重新验证条件
            if (this->isAutoStartEnabled() && shouldAutoStartCycle &&
                isCycleRunning && currentCycleCount < currentCycleConfig.cycleCount) {

                emit logMessage("恢复条件满足，开始恢复循环");
                performRestore();
            } else {
                emit logMessage("恢复条件不满足，清理状态");
                if (!this->isAutoStartEnabled())
                    emit logMessage("  - 开机启动未启用");
                if (!shouldAutoStartCycle)
                    emit logMessage("  - 自动启动已取消");
                if (!isCycleRunning)
                    emit logMessage("  - 循环未运行");
                if (currentCycleCount >= currentCycleConfig.cycleCount)
                    emit logMessage("  - 循环已完成");

                clearCycleState();
                stopCycle();
            }
            isRestoringCycle = false;
        });
    } else {
        emit logMessage("不需要恢复循环状态");
        if (!shouldAutoStartCycle)
            emit logMessage("  - 自动启动未启用");
        if (!isCycleRunning)
            emit logMessage("  - 循环未运行");
        if (currentCycleCount >= currentCycleConfig.cycleCount)
            emit logMessage("  - 循环已完成");
    }
}

// 新增专用恢复方法
void CycleController::performRestore()
{
    emit logMessage("=== 开始执行循环恢复 ===");

    // 验证开机启动状态
    if (!isAutoStartEnabled()) {
        emit logMessage("错误: 开机启动服务未启用，无法恢复循环任务");
        clearCycleState();
        stopCycle();
        return;
    }

    if (currentCycleCount >= currentCycleConfig.cycleCount) {
        emit logMessage("循环已完成所有次数，停止恢复");
        stopCycle();
        return;
    }

    // 关键修复：确保状态正确同步
    isCycleRunning = true;
    shouldAutoStartCycle = true;

    emit logMessage(QString("恢复状态: 操作=%1, 已完成=%2/%3, 倒计时=%4")
                  .arg(getOperationName(currentCycleConfig.operation))
                  .arg(currentCycleCount)
                  .arg(currentCycleConfig.cycleCount)
                  .arg(countdownSeconds));

    // 更新UI状态
    if (toggleButton) {
        toggleButton->setText("停止循环");
        toggleButton->setStyleSheet("QPushButton { background-color: #f44336; color: white; }");
        emit logMessage("UI按钮状态已更新为: 停止循环");
    }

    // 处理倒计时
    if (isFirstStartAfterReboot || countdownSeconds <= 0) {
        countdownSeconds = currentCycleConfig.intervalSeconds;
        isFirstStartAfterReboot = false;
        emit logMessage("重置倒计时为: " + QString::number(countdownSeconds) + "秒");
    } else {
        emit logMessage("使用现有倒计时: " + QString::number(countdownSeconds) + "秒");
    }

    // 启动定时器
    if (!countdownTimer->isActive()) {
        countdownTimer->start(1000);
        emit logMessage("倒计时定时器已启动");
    }

    // 更新状态显示
    updateCycleStatus();

    // 保存状态
    saveCycleState();

    emit logMessage(QString("循环恢复完成: 第%1/%2次 %3, 倒计时%4秒")
                  .arg(currentCycleCount + 1)
                  .arg(currentCycleConfig.cycleCount)
                  .arg(getOperationName(currentCycleConfig.operation))
                  .arg(countdownSeconds));

    emit logMessage("=== 循环恢复完成 ===");
}

void CycleController::clearCycleState()
{
    // 只有在真正需要清除时才记录日志
    if (currentCycleCount > 0) {
        emit logMessage("正在清除循环状态...");
    }

    QSettings settings("ELSKY", "TestClient");

    // 只清除循环相关的状态
    settings.remove("cycle");
    settings.remove("cycleConfig");

    // 重置内部状态
    shouldAutoStartCycle = false;
    isFirstStartAfterReboot = false;

    // 关键修复：只有在循环完成时才重置计数
    if (currentCycleCount >= currentCycleConfig.cycleCount) {
        currentCycleCount = 0;
        // 清除完成提示标记
        settings.remove("cycle/showCompletionDialog");
    }

    settings.sync();

    if (currentCycleCount > 0) {
        emit logMessage("循环状态已清除");
    }
}

QString CycleController::getOperationName(CycleOperation operation)
{
    switch (operation) {
    case CYCLE_SHUTDOWN: return "关机";
    case CYCLE_REBOOT: return "重启";
    case CYCLE_SUSPEND: return "睡眠";
    case CYCLE_HIBERNATE: return "休眠";
    default: return "未知";
    }
}

bool CycleController::isAutoStartEnabled()
{
    if (autoStartManager) {
        return autoStartManager->isAutoStartEnabled();
    }
    return false;
}
