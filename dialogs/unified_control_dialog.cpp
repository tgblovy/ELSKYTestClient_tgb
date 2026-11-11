#include "unified_control_dialog.h"
#include "../core/unified_control_manager.h"
#include "../common/protocol.h"

#include <QMessageBox>
#include <QDebug>
#include <QSettings>
#include <QFormLayout>

UnifiedControlDialog::UnifiedControlDialog(const QList<PeerInfo> &peers,
                                           const QString &currentTag,
                                           QWidget *parent)
    : QDialog(parent)
    , m_currentTag(currentTag)
{
    setWindowTitle(QString("统一控制面板 - 标签: %1").arg(currentTag));
    setMinimumSize(520, 550);
    setModal(true);
    setupUI(peers);
}

void UnifiedControlDialog::setupUI(const QList<PeerInfo> &peers)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(15, 15, 15, 15);

    // === 节点发现区域 ===
    QGroupBox *peerGroup = new QGroupBox("已发现节点 (同标签机器)", this);
    QVBoxLayout *peerGroupLayout = new QVBoxLayout(peerGroup);

    // 全选和刷新
    QHBoxLayout *peerControlLayout = new QHBoxLayout();
    selectAllCheckBox = new QCheckBox("全选", this);
    selectAllCheckBox->setChecked(true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(selectAllCheckBox, &QCheckBox::checkStateChanged, this, &UnifiedControlDialog::onSelectAllPeers);
#else
    connect(selectAllCheckBox, &QCheckBox::stateChanged, this, &UnifiedControlDialog::onSelectAllPeers);
#endif

    peerCountLabel = new QLabel(this);
    peerCountLabel->setStyleSheet("color: #666; font-size: 12px;");

    QPushButton *refreshButton = new QPushButton("刷新", this);
    refreshButton->setFixedWidth(60);
    refreshButton->setStyleSheet("QPushButton { background-color: #2196F3; color: white; border: none; padding: 4px 10px; border-radius: 3px; font-size: 12px; }");
    connect(refreshButton, &QPushButton::clicked, this, &UnifiedControlDialog::onRefreshPeers);

    peerControlLayout->addWidget(selectAllCheckBox);
    peerControlLayout->addStretch();
    peerControlLayout->addWidget(peerCountLabel);
    peerControlLayout->addWidget(refreshButton);
    peerGroupLayout->addLayout(peerControlLayout);

    // 节点列表
    peerListWidget = new QListWidget(this);
    peerListWidget->setMaximumHeight(120);
    peerListWidget->setStyleSheet("QListWidget { font-size: 12px; border: 1px solid #ddd; border-radius: 3px; }");

    for (const PeerInfo &peer : peers) {
        QListWidgetItem *item = new QListWidgetItem(
            QString("%1  (%2)  [标签: %3]")
            .arg(peer.ip)
            .arg(peer.hostname)
            .arg(peer.tag));
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, peer.ip);
        peerListWidget->addItem(item);
    }

    int count = peerListWidget->count();
    peerCountLabel->setText(QString("共 %1 台机器").arg(count));

    if (count == 0) {
        QListWidgetItem *emptyItem = new QListWidgetItem("未发现同标签的机器");
        emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsUserCheckable);
        emptyItem->setForeground(Qt::gray);
        peerListWidget->addItem(emptyItem);
        selectAllCheckBox->setEnabled(false);
    }

    peerGroupLayout->addWidget(peerListWidget);
    mainLayout->addWidget(peerGroup);

    // === 操作选项区域 ===
    QGroupBox *opGroup = new QGroupBox("执行操作 (可多选)", this);
    QVBoxLayout *opLayout = new QVBoxLayout(opGroup);
    opLayout->setSpacing(8);

    // 电源操作行
    QHBoxLayout *powerLayout = new QHBoxLayout();
    shutdownCheck = new QCheckBox("关机", this);
    rebootCheck = new QCheckBox("重启", this);
    suspendCheck = new QCheckBox("睡眠", this);
    hibernateCheck = new QCheckBox("休眠", this);
    powerLayout->addWidget(shutdownCheck);
    powerLayout->addWidget(rebootCheck);
    powerLayout->addWidget(suspendCheck);
    powerLayout->addWidget(hibernateCheck);
    powerLayout->addStretch();
    opLayout->addLayout(powerLayout);

    // 基础操作行
    QHBoxLayout *basicLayout = new QHBoxLayout();
    connectServerCheck = new QCheckBox("连接服务器", this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(connectServerCheck, &QCheckBox::checkStateChanged, this, &UnifiedControlDialog::onConnectServerCheckChanged);
#else
    connect(connectServerCheck, &QCheckBox::stateChanged, this, &UnifiedControlDialog::onConnectServerCheckChanged);
#endif

    serverIPEdit = new QLineEdit(this);
    serverIPEdit->setPlaceholderText("服务器IP地址");
    serverIPEdit->setEnabled(false);
    serverIPEdit->setFixedWidth(140);
    serverIPEdit->setFixedHeight(24);
    // 加载已保存的服务器IP
    {
        QSettings settings("ELSKY", "TestClient");
        QString savedIP = settings.value("server/ip", "192.168.1.100").toString();
        serverIPEdit->setText(savedIP);
    }

    autoStartCheck = new QCheckBox("开机启动", this);
    alwaysOnTopCheck = new QCheckBox("窗口置顶", this);
    basicLayout->addWidget(connectServerCheck);
    basicLayout->addWidget(serverIPEdit);
    basicLayout->addWidget(autoStartCheck);
    basicLayout->addWidget(alwaysOnTopCheck);
    basicLayout->addStretch();
    opLayout->addLayout(basicLayout);

    // 测试操作行
    QHBoxLayout *testLayout = new QHBoxLayout();
    networkSpeedCheck = new QCheckBox("网络测速", this);
    comTestCheck = new QCheckBox("COM测试", this);
    agingTestCheck = new QCheckBox("老化测试", this);
    stopAgingTestCheck = new QCheckBox("停止老化", this);
    startWakeOnLanCheck = new QCheckBox("启动唤醒", this);
    stopWakeOnLanCheck = new QCheckBox("取消唤醒", this);
    testLayout->addWidget(startWakeOnLanCheck);
    testLayout->addWidget(stopWakeOnLanCheck);
    testLayout->addWidget(comTestCheck);
    testLayout->addWidget(agingTestCheck);
    testLayout->addWidget(stopAgingTestCheck);
    testLayout->addWidget(networkSpeedCheck);
    testLayout->addStretch();
    opLayout->addLayout(testLayout);

    // 其他操作行
    QHBoxLayout *otherLayout = new QHBoxLayout();
    deviceDetectCheck = new QCheckBox("设备检测", this);
    clearLogsCheck = new QCheckBox("清除日志", this);
    reloadDeviceCheck = new QCheckBox("重新导入", this);
    clearIllegalShutdownCheck = new QCheckBox("清除非法关机", this);
    otherLayout->addWidget(deviceDetectCheck);
    otherLayout->addWidget(clearLogsCheck);
    otherLayout->addWidget(reloadDeviceCheck);
    otherLayout->addWidget(clearIllegalShutdownCheck);
    otherLayout->addStretch();
    opLayout->addLayout(otherLayout);

    // 循环控制行
    QHBoxLayout *cycleRowLayout = new QHBoxLayout();
    startCycleCheck = new QCheckBox("开始循环", this);
    stopCycleCheck = new QCheckBox("停止循环", this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(startCycleCheck, &QCheckBox::checkStateChanged, this, &UnifiedControlDialog::onCycleCheckChanged);
#else
    connect(startCycleCheck, &QCheckBox::stateChanged, this, &UnifiedControlDialog::onCycleCheckChanged);
#endif

    cycleOperationCombo = new QComboBox(this);
    cycleOperationCombo->setEnabled(false);
    cycleOperationCombo->addItem("关机", CYCLE_SHUTDOWN);
    cycleOperationCombo->addItem("重启", CYCLE_REBOOT);
    cycleOperationCombo->addItem("睡眠", CYCLE_SUSPEND);
    cycleOperationCombo->addItem("休眠", CYCLE_HIBERNATE);
    cycleOperationCombo->setFixedWidth(80);

    QLabel *intervalLabel = new QLabel("间隔:", this);
    cycleIntervalSpin = new QSpinBox(this);
    cycleIntervalSpin->setEnabled(false);
    cycleIntervalSpin->setRange(10, 86400);
    cycleIntervalSpin->setValue(40);
    cycleIntervalSpin->setSuffix("秒");
    cycleIntervalSpin->setFixedWidth(100);

    QLabel *countLabel = new QLabel("次数:", this);
    cycleCountSpin = new QSpinBox(this);
    cycleCountSpin->setEnabled(false);
    cycleCountSpin->setRange(1, 9999);
    cycleCountSpin->setValue(1000);
    cycleCountSpin->setSuffix("次");
    cycleCountSpin->setFixedWidth(100);

    cycleRowLayout->addWidget(startCycleCheck);
    cycleRowLayout->addWidget(cycleOperationCombo);
    cycleRowLayout->addWidget(intervalLabel);
    cycleRowLayout->addWidget(cycleIntervalSpin);
    cycleRowLayout->addWidget(countLabel);
    cycleRowLayout->addWidget(cycleCountSpin);
    cycleRowLayout->addStretch();
    cycleRowLayout->addWidget(stopCycleCheck);
    opLayout->addLayout(cycleRowLayout);

    // 设置各checkbox的样式
    QString checkStyle = "QCheckBox { font-size: 12px; spacing: 4px; }";
    shutdownCheck->setStyleSheet(checkStyle);
    rebootCheck->setStyleSheet(checkStyle);
    suspendCheck->setStyleSheet(checkStyle);
    hibernateCheck->setStyleSheet(checkStyle);
    networkSpeedCheck->setStyleSheet(checkStyle);
    comTestCheck->setStyleSheet(checkStyle);
    agingTestCheck->setStyleSheet(checkStyle);
    stopAgingTestCheck->setStyleSheet(checkStyle);
    startWakeOnLanCheck->setStyleSheet(checkStyle);
    stopWakeOnLanCheck->setStyleSheet(checkStyle);
    connectServerCheck->setStyleSheet(checkStyle);
    autoStartCheck->setStyleSheet(checkStyle);
    alwaysOnTopCheck->setStyleSheet(checkStyle);
    startCycleCheck->setStyleSheet(checkStyle);
    stopCycleCheck->setStyleSheet(checkStyle);
    deviceDetectCheck->setStyleSheet(checkStyle);
    clearLogsCheck->setStyleSheet(checkStyle);
    reloadDeviceCheck->setStyleSheet(checkStyle);
    clearIllegalShutdownCheck->setStyleSheet(checkStyle);

    mainLayout->addWidget(opGroup);

    // === 按钮区域 ===
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    QPushButton *cancelButton = new QPushButton("关闭", this);
    cancelButton->setFixedWidth(80);
    cancelButton->setStyleSheet("QPushButton { background-color: #607D8B; color: white; border: none; padding: 6px 16px; border-radius: 4px; font-size: 13px; } QPushButton:hover { background-color: #78909C; }");
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    QPushButton *executeButton = new QPushButton("执行", this);
    executeButton->setFixedWidth(120);
    executeButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; border: none; padding: 6px 16px; border-radius: 4px; font-size: 13px; font-weight: bold; } QPushButton:hover { background-color: #66BB6A; } QPushButton:pressed { background-color: #388E3C; }");
    connect(executeButton, &QPushButton::clicked, this, &UnifiedControlDialog::onExecute);

    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(executeButton);
    mainLayout->addLayout(buttonLayout);
}

void UnifiedControlDialog::onCycleCheckChanged(int state)
{
    bool enabled = (state == Qt::Checked);
    cycleOperationCombo->setEnabled(enabled);
    cycleIntervalSpin->setEnabled(enabled);
    cycleCountSpin->setEnabled(enabled);
}

void UnifiedControlDialog::onConnectServerCheckChanged(int state)
{
    serverIPEdit->setEnabled(state == Qt::Checked);
}

void UnifiedControlDialog::onSelectAllPeers(int state)
{
    bool checked = (state == Qt::Checked);
    for (int i = 0; i < peerListWidget->count(); ++i) {
        QListWidgetItem *item = peerListWidget->item(i);
        if (item->flags() & Qt::ItemIsUserCheckable) {
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
        }
    }
}

void UnifiedControlDialog::onRefreshPeers()
{
    peerCountLabel->setText("请等待重新发现...");
    QTimer::singleShot(500, this, [this]() {
        // 触发重新发现由外部完成，这里只是UI提示
        peerCountLabel->setText(QString("共 %1 台机器 (已刷新)").arg(peerListWidget->count()));
    });
}

void UnifiedControlDialog::onExecute()
{
    // 检查是否有选中的操作
    bool hasOperation = shutdownCheck->isChecked() || rebootCheck->isChecked() ||
                        suspendCheck->isChecked() || hibernateCheck->isChecked() ||
                        networkSpeedCheck->isChecked() || startCycleCheck->isChecked() ||
                        stopCycleCheck->isChecked() || connectServerCheck->isChecked() ||
                        autoStartCheck->isChecked() || alwaysOnTopCheck->isChecked() ||
                        comTestCheck->isChecked() || agingTestCheck->isChecked() ||
                        stopAgingTestCheck->isChecked() ||
                        startWakeOnLanCheck->isChecked() || stopWakeOnLanCheck->isChecked() ||
                        deviceDetectCheck->isChecked() || clearLogsCheck->isChecked() ||
                        reloadDeviceCheck->isChecked() || clearIllegalShutdownCheck->isChecked();

    if (!hasOperation) {
        QMessageBox::warning(this, "提示", "请至少选择一个操作！");
        return;
    }

    // 检查是否有选中的节点
    int selectedPeers = 0;
    for (int i = 0; i < peerListWidget->count(); ++i) {
        if (peerListWidget->item(i)->checkState() == Qt::Checked) {
            selectedPeers++;
        }
    }

    if (selectedPeers == 0) {
        QMessageBox::warning(this, "提示", "请至少选择一台目标机器！");
        return;
    }

    // 确认执行（对危险操作需要二次确认）
    bool hasDangerousOp = shutdownCheck->isChecked() || rebootCheck->isChecked();
    if (hasDangerousOp) {
        QMessageBox::StandardButton result = QMessageBox::warning(
            this, "确认执行",
            QString("您即将向 %1 台机器发送危险操作（关机/重启）！\n\n确定要继续吗？").arg(selectedPeers),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (result != QMessageBox::Yes) {
            return;
        }
    }

    // 停止老化确认（显示已老化时长）
    if (stopAgingTestCheck->isChecked()) {
        QSettings settings("ELSKY", "TestClient");
        QDateTime startTime = settings.value("aging/startTime").toDateTime();
        QString durationText;
        if (startTime.isValid()) {
            qint64 seconds = startTime.secsTo(QDateTime::currentDateTime());
            int days = seconds / 86400;
            int hours = (seconds % 86400) / 3600;
            int minutes = (seconds % 3600) / 60;
            if (days > 0) {
                durationText = QString("%1天%2小时%3分钟").arg(days).arg(hours).arg(minutes);
            } else if (hours > 0) {
                durationText = QString("%1小时%2分钟").arg(hours).arg(minutes);
            } else {
                durationText = QString("%1分钟").arg(minutes);
            }
        } else {
            durationText = "未知";
        }
        QMessageBox::StandardButton result = QMessageBox::question(
            this, "确认停止老化",
            QString("当前已老化：%1\n\n是否确认停止 %2 台机器的老化测试？").arg(durationText).arg(selectedPeers),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (result != QMessageBox::Yes) {
            return;
        }
    }

    // 启动唤醒确认（需要连接服务器）
    if (startWakeOnLanCheck->isChecked()) {
        if (!connectServerCheck->isChecked()) {
            QMessageBox::warning(this, "提示", "启动唤醒需要连接服务器，请选择连接服务器！");
            return;
        }
        QMessageBox::StandardButton result = QMessageBox::question(
            this, "确认启动唤醒",
            "请确定被控制端已连接服务器，执行操作后被控制端设置LAN唤醒为Magic模式\n\n是否确认启动唤醒？",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (result != QMessageBox::Yes) {
            return;
        }
    }

    // 取消唤醒确认（需要连接服务器）
    if (stopWakeOnLanCheck->isChecked()) {
        if (!connectServerCheck->isChecked()) {
            QMessageBox::warning(this, "提示", "取消唤醒需要连接服务器，请选择连接服务器！");
            return;
        }
        QMessageBox::StandardButton result = QMessageBox::question(
            this, "确认取消唤醒",
            "请确定被控制端已连接服务器\n\n是否确认取消唤醒？",
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (result != QMessageBox::Yes) {
            return;
        }
    }

    buildCommandAndSend();

    QMessageBox::information(this, "执行成功",
        QString("已向 %1 台机器发送命令。\n请查看日志了解执行结果。").arg(selectedPeers));
    accept();
}

void UnifiedControlDialog::buildCommandAndSend()
{
    if (shutdownCheck->isChecked()) {
        emit executeCommand("SHUTDOWN", QStringList());
    }
    if (rebootCheck->isChecked()) {
        emit executeCommand("REBOOT", QStringList());
    }
    if (suspendCheck->isChecked()) {
        emit executeCommand("SUSPEND", QStringList());
    }
    if (hibernateCheck->isChecked()) {
        emit executeCommand("HIBERNATE", QStringList());
    }
    if (startWakeOnLanCheck->isChecked()) {
        emit executeCommand("START_WAKE_ON_LAN", QStringList());
    }
    if (stopWakeOnLanCheck->isChecked()) {
        emit executeCommand("STOP_WAKE_ON_LAN", QStringList());
    }
    if (connectServerCheck->isChecked()) {
        QString ip = serverIPEdit->text().trimmed();
        if (ip.isEmpty()) {
            QMessageBox::warning(this, "提示", "请输入服务器IP地址！");
            return;
        }
        emit executeCommand("CONNECT_SERVER", QStringList() << ip);
    }
    if (autoStartCheck->isChecked()) {
        emit executeCommand("AUTO_START", QStringList());
    }
    if (alwaysOnTopCheck->isChecked()) {
        emit executeCommand("ALWAYS_ON_TOP", QStringList());
    }
    if (networkSpeedCheck->isChecked()) {
        emit executeCommand("NETWORK_SPEED_TEST", QStringList());
    }
    if (comTestCheck->isChecked()) {
        emit executeCommand("COM_TEST", QStringList());
    }
    if (agingTestCheck->isChecked()) {
        QSettings settings("ELSKY", "TestClient");
        settings.setValue("aging/startTime", QDateTime::currentDateTime());
        emit executeCommand("AGING_TEST", QStringList());
    }
    if (stopAgingTestCheck->isChecked()) {
        QSettings settings("ELSKY", "TestClient");
        settings.remove("aging/startTime");
        emit executeCommand("STOP_AGING_TEST", QStringList());
    }
    if (deviceDetectCheck->isChecked()) {
        emit executeCommand("DEVICE_DETECT", QStringList());
    }
    if (clearLogsCheck->isChecked()) {
        emit executeCommand("CLEAR_LOGS", QStringList());
    }
    if (reloadDeviceCheck->isChecked()) {
        emit executeCommand("RELOAD_DEVICE", QStringList());
    }
    if (clearIllegalShutdownCheck->isChecked()) {
        emit executeCommand("CLEAR_ILLEGAL_SHUTDOWN", QStringList());
    }
    if (startCycleCheck->isChecked()) {
        QStringList params;
        params << QString::number(cycleOperationCombo->currentData().toInt())
               << QString::number(cycleIntervalSpin->value())
               << QString::number(cycleCountSpin->value());
        emit executeCommand("START_CYCLE", params);
    }
    if (stopCycleCheck->isChecked()) {
        emit executeCommand("STOP_CYCLE", QStringList());
    }
}
