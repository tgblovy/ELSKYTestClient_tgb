#include "client_window.h"
#include "basic_tester.h"
#include "server_connector.h"
#include "auto_start_manager.h"
#include "wake_on_lan.h"
#include "cycle_controller.h"
#include "system_monitor.h"
#include "device_manager.h"
#include "logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QApplication>
#include <QTimer>
#include <QDebug>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QScreen>           // 添加这行
#include <QGuiApplication>   // 添加这行
#include <QDateTime>         // 添加这行 - 解决 QDateTime 错误

ClientWindow::ClientWindow(QWidget *parent)
    : QMainWindow(parent)
    , serverCheckBox(nullptr)
    , autoStartCheckBox(nullptr)
    , basicTestCombo(nullptr)
    , executeBasicTestButton(nullptr)
    , wolToggleButton(nullptr)
    , cycleToggleButton(nullptr)
    , deviceDetectButton(nullptr)
    , clearLogsButton(nullptr)
    , cycleOperationCombo(nullptr)
    , cycleIntervalSpinBox(nullptr)
    , cycleCountSpinBox(nullptr)
    , cycleStatusLabel(nullptr)
    , logTextEdit(nullptr)
    , deviceInfoEdit(nullptr)
    , cpuTempLabel(nullptr)
    , diskTempLabel(nullptr)
    , cpuFanLabel(nullptr)
    , cpuUsageBar(nullptr)
    , memoryUsageBar(nullptr)
    , gpuMemoryBar(nullptr)
    , lastBootLabel(nullptr)
    , uptimeLabel(nullptr)
    , basicTester(nullptr)
    , serverConnector(nullptr)
    , autoStartManager(nullptr)
    , wakeOnLan(nullptr)
    , cycleController(nullptr)
    , systemMonitor(nullptr)
    , deviceManager(nullptr)
    , logger(nullptr)
    // 删除 alwaysOnTopAction 的初始化
{
    setupUI();
    setupFunctionModules();
    connectModules();
    setupWindowProperties();

    QTimer::singleShot(1000, this, [this]() {
        if (systemMonitor) {
            systemMonitor->startMonitoring();
        }
    });
}

ClientWindow::~ClientWindow()
{
    // 析构函数会自动删除子对象
    // 保存各个模块的状态
    if (wakeOnLan) {
        wakeOnLan->saveState();
    }
}

void ClientWindow::setupUI()
{
    qDebug() << "开始设置UI...";

    // 动态设置窗口标题
    setWindowTitle("研盛测试客户端V1.7");
    setMinimumSize(850, 580);

    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    // 使用垂直布局作为主布局
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(6, 6, 6, 6);

    // 操作导航栏
    QGroupBox *navGroup = new QGroupBox("操作导航", this);
    QVBoxLayout *navLayout = new QVBoxLayout(navGroup);
    navLayout->setSpacing(6);
    navLayout->setContentsMargins(10, 10, 10, 10);

    // 第一行：基础测试、连接服务器、开机启动、窗口置顶
    QHBoxLayout *navRow1 = new QHBoxLayout();
    // 删除这行：navRow1->setSpacing(20);

    // 基础测试
    QHBoxLayout *basicTestLayout = new QHBoxLayout();
    basicTestLayout->setSpacing(8);
    QLabel *basicTestLabel = new QLabel("基础测试:", this);
    basicTestLabel->setFixedWidth(60);

    basicTestCombo = new QComboBox(this);
    basicTestCombo->addItem("关机");
    basicTestCombo->addItem("重启");
    basicTestCombo->addItem("睡眠");
    basicTestCombo->addItem("休眠");
    basicTestCombo->addItem("COM测试");
    basicTestCombo->addItem("U盘测速");
    basicTestCombo->addItem("网络测速");  // 新增网络测速选项
    basicTestCombo->setMinimumWidth(100);
    basicTestCombo->setMaximumHeight(25);

    executeBasicTestButton = new QPushButton("执行", this);
    executeBasicTestButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; border: none; padding: 4px 10px; border-radius: 3px; font-size: 12px; }");
    executeBasicTestButton->setFixedWidth(55);
    executeBasicTestButton->setFixedHeight(25);

    basicTestLayout->addWidget(basicTestLabel);
    basicTestLayout->addWidget(basicTestCombo);
    basicTestLayout->addWidget(executeBasicTestButton);

    // 连接控制 - 使用弹性空间来控制间距
    QHBoxLayout *connectLayout = new QHBoxLayout();
    connectLayout->setSpacing(8);  // 减小复选框间距

    serverCheckBox = new QCheckBox("连接服务器", this);
    serverCheckBox->setFixedWidth(90);

    autoStartCheckBox = new QCheckBox("开机启动", this);
    autoStartCheckBox->setFixedWidth(80);

    QCheckBox *alwaysOnTopCheckBox = new QCheckBox("窗口置顶", this);
    alwaysOnTopCheckBox->setFixedWidth(80);

    connectLayout->addWidget(serverCheckBox);
    connectLayout->addWidget(autoStartCheckBox);
    connectLayout->addWidget(alwaysOnTopCheckBox);

    // 使用弹性空间来控制第一行的整体布局
    navRow1->addLayout(basicTestLayout);
    navRow1->addSpacing(10);  // 直接添加固定间距
    navRow1->addLayout(connectLayout);
    navRow1->addStretch();

    // 第二行：网络唤醒、循环控制、设备检测
    QHBoxLayout *navRow2 = new QHBoxLayout();
    //navRow2->setSpacing(20);

    // 网络唤醒
    QHBoxLayout *wolLayout = new QHBoxLayout();
    wolLayout->setSpacing(8);
    QLabel *wolLabel = new QLabel("网络唤醒:", this);
    wolLabel->setFixedWidth(65);

    wolToggleButton = new QPushButton("启动唤醒", this);
    wolToggleButton->setStyleSheet("QPushButton { background-color: #2196F3; color: white; border: none; padding: 4px 10px; border-radius: 3px; font-size: 12px; }");
    wolToggleButton->setFixedWidth(70);
    wolToggleButton->setFixedHeight(25);

    wolLayout->addWidget(wolLabel);
    wolLayout->addWidget(wolToggleButton);
    //wolLayout->addStretch();

    // 循环控制
    QHBoxLayout *cycleLayout = new QHBoxLayout();
    cycleLayout->setSpacing(8);
    QLabel *cycleLabel = new QLabel("循环控制:", this);
    cycleLabel->setFixedWidth(60);

    cycleToggleButton = new QPushButton("开始循环", this);
    cycleToggleButton->setStyleSheet("QPushButton { background-color: #9C27B0; color: white; border: none; padding: 4px 10px; border-radius: 3px; font-size: 12px; }");
    cycleToggleButton->setFixedWidth(70);
    cycleToggleButton->setFixedHeight(25);

    cycleStatusLabel = new QLabel("未开始", this);
    cycleStatusLabel->setStyleSheet("QLabel { color: #666; font-size: 13px; background: #f5f5f5; padding: 3px 6px; border-radius: 2px; border: 1px solid #ddd; }");
    cycleStatusLabel->setFixedWidth(170);
    cycleStatusLabel->setFixedHeight(25);
    cycleStatusLabel->setAlignment(Qt::AlignCenter);

    cycleLayout->addWidget(cycleLabel);
    cycleLayout->addWidget(cycleToggleButton);
    cycleLayout->addWidget(cycleStatusLabel);
    //cycleLayout->addStretch();

    // 设备检测
    QHBoxLayout *deviceLayout = new QHBoxLayout();
    deviceLayout->setSpacing(8);
    QLabel *deviceLabel = new QLabel("设备检测:", this);
    deviceLabel->setFixedWidth(65);

    deviceDetectButton = new QPushButton("开始检测", this);
    deviceDetectButton->setStyleSheet("QPushButton { background-color: #FF9800; color: white; border: none; padding: 4px 10px; border-radius: 3px; font-size: 12px; }");
    deviceDetectButton->setFixedWidth(70);
    deviceDetectButton->setFixedHeight(25);

    deviceLayout->addWidget(deviceLabel);
    deviceLayout->addWidget(deviceDetectButton);
    deviceLayout->addStretch();

    // 使用固定间距来控制第二行的布局
    navRow2->addLayout(wolLayout);
    navRow2->addSpacing(2);  // 网络唤醒和循环控制之间的间距
    navRow2->addLayout(cycleLayout);
    navRow2->addSpacing(2);  // 循环控制和设备检测之间的间距
    navRow2->addLayout(deviceLayout);
    navRow2->addStretch();

    navLayout->addLayout(navRow1);
    navLayout->addLayout(navRow2);

    // 内容区域 - 左右等分
    QSplitter *contentSplitter = new QSplitter(Qt::Horizontal, this);
    contentSplitter->setChildrenCollapsible(false);

    // 左边区域 - 实时系统监控和操作日志
    QWidget *leftWidget = new QWidget(this);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 3, 0);  // 减小边距
    leftLayout->setSpacing(6);  // 减小间距

    // 实时系统监控
    QGroupBox *monitorGroup = new QGroupBox("实时系统监控", this);
    monitorGroup->setMinimumHeight(200);  // 缩小高度
    monitorGroup->setMaximumHeight(220);  // 缩小高度
    QVBoxLayout *monitorLayout = new QVBoxLayout(monitorGroup);
    monitorLayout->setSpacing(6);  // 减小间距
    monitorLayout->setContentsMargins(10, 10, 10, 10);  // 减小边距

    // 温度信息
    QHBoxLayout *tempLayout = new QHBoxLayout();
    tempLayout->setSpacing(15);  // 减小间距
    tempLayout->setContentsMargins(0, 0, 0, 4);  // 减小边距

    cpuTempLabel = new QLabel("CPU温度: --°C", this);
    cpuTempLabel->setAlignment(Qt::AlignCenter);
    cpuTempLabel->setStyleSheet("font-size: 12px; color: #2196F3; font-weight: bold; background: #f8f9fa; padding: 6px 10px; border-radius: 4px; border: 1px solid #e0e0e0;");  // 缩小字体和padding
    cpuTempLabel->setMinimumWidth(120);  // 缩小宽度
    cpuTempLabel->setMinimumHeight(28);  // 缩小高度

    diskTempLabel = new QLabel("硬盘温度: --°C", this);
    diskTempLabel->setAlignment(Qt::AlignCenter);
    diskTempLabel->setStyleSheet("font-size: 12px; color: #4CAF50; font-weight: bold; background: #f8f9fa; padding: 6px 10px; border-radius: 4px; border: 1px solid #e0e0e0;");  // 缩小字体和padding
    diskTempLabel->setMinimumWidth(120);  // 缩小宽度
    diskTempLabel->setMinimumHeight(28);  // 缩小高度

    cpuFanLabel = new QLabel("CPU风扇: -- RPM", this);
    cpuFanLabel->setAlignment(Qt::AlignCenter);
    cpuFanLabel->setStyleSheet("font-size: 12px; color: #FF9800; font-weight: bold; background: #f8f9fa; padding: 6px 10px; border-radius: 4px; border: 1px solid #e0e0e0;");  // 缩小字体和padding
    cpuFanLabel->setMinimumWidth(120);  // 缩小宽度
    cpuFanLabel->setMinimumHeight(28);  // 缩小高度

    tempLayout->addWidget(cpuTempLabel);
    tempLayout->addWidget(diskTempLabel);
    tempLayout->addWidget(cpuFanLabel);
    tempLayout->addStretch();

    // CPU占用
    QHBoxLayout *cpuUsageLayout = new QHBoxLayout();
    cpuUsageLayout->setSpacing(4);
    cpuUsageLayout->setContentsMargins(0, 1, 0, 0);

    QLabel *cpuUsageTitle = new QLabel("CPU占用:", this);
    cpuUsageTitle->setFixedWidth(60);
    cpuUsageTitle->setStyleSheet("font-weight: bold; font-size: 11px;");

    cpuUsageBar = new QProgressBar(this);
    cpuUsageBar->setRange(0, 100);
    cpuUsageBar->setTextVisible(true);
    cpuUsageBar->setAlignment(Qt::AlignCenter);
    cpuUsageBar->setFixedHeight(18);
    // 移除最小宽度设置，改用布局权重
    // cpuUsageBar->setMinimumWidth(200);

    cpuUsageBar->setStyleSheet("QProgressBar { border: 1px solid #ccc; border-radius: 3px; background: #f0f0f0; padding: 1px; font-size: 10px; min-width: 200px; } QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4CAF50, stop:1 #8BC34A); border-radius: 2px; }");

    cpuUsageLayout->addWidget(cpuUsageTitle);
    cpuUsageLayout->addWidget(cpuUsageBar, 1);  // 添加权重1，让进度条扩展
    cpuUsageLayout->addStretch();

    // 内存占用
    QHBoxLayout *memoryUsageLayout = new QHBoxLayout();
    memoryUsageLayout->setSpacing(4);
    memoryUsageLayout->setContentsMargins(0, 1, 0, 0);

    QLabel *memoryUsageTitle = new QLabel("内存占用:", this);
    memoryUsageTitle->setFixedWidth(60);
    memoryUsageTitle->setStyleSheet("font-weight: bold; font-size: 11px;");

    memoryUsageBar = new QProgressBar(this);
    memoryUsageBar->setRange(0, 100);
    memoryUsageBar->setTextVisible(true);
    memoryUsageBar->setAlignment(Qt::AlignCenter);
    memoryUsageBar->setFixedHeight(18);
    memoryUsageBar->setStyleSheet("QProgressBar { border: 1px solid #ccc; border-radius: 3px; background: #f0f0f0; padding: 1px; font-size: 10px; min-width: 200px; } QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #2196F3, stop:1 #03A9F4); border-radius: 2px; }");

    memoryUsageLayout->addWidget(memoryUsageTitle);
    memoryUsageLayout->addWidget(memoryUsageBar, 1);  // 添加权重1
    memoryUsageLayout->addStretch();

    // 显存占用
    QHBoxLayout *gpuUsageLayout = new QHBoxLayout();
    gpuUsageLayout->setSpacing(4);
    gpuUsageLayout->setContentsMargins(0, 1, 0, 0);

    QLabel *gpuUsageTitle = new QLabel("显存占用:", this);
    gpuUsageTitle->setFixedWidth(60);
    gpuUsageTitle->setStyleSheet("font-weight: bold; font-size: 11px;");

    gpuMemoryBar = new QProgressBar(this);
    gpuMemoryBar->setRange(0, 100);
    gpuMemoryBar->setTextVisible(true);
    gpuMemoryBar->setAlignment(Qt::AlignCenter);
    gpuMemoryBar->setFixedHeight(18);
    gpuMemoryBar->setStyleSheet("QProgressBar { border: 1px solid #ccc; border-radius: 3px; background: #f0f0f0; padding: 1px; font-size: 10px; min-width: 200px; } QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #FF9800, stop:1 #FFC107); border-radius: 2px; }");

    gpuUsageLayout->addWidget(gpuUsageTitle);
    gpuUsageLayout->addWidget(gpuMemoryBar, 1);  // 添加权重1
    gpuUsageLayout->addStretch();

    // 系统信息
    QHBoxLayout *systemLayout = new QHBoxLayout();
    systemLayout->setSpacing(12);  // 减小间距
    systemLayout->setContentsMargins(0, 4, 0, 0);  // 减小边距

    // 上次开机
    QHBoxLayout *bootLayout = new QHBoxLayout();
    bootLayout->setSpacing(2);  // 减小间距
    QLabel *bootTitle = new QLabel("上次开机:", this);
    bootTitle->setStyleSheet("font-weight: bold; font-size: 12px;");  // 缩小字体
    lastBootLabel = new QLabel("--", this);
    lastBootLabel->setStyleSheet("font-size: 13px; color: #666; font-weight: bold; background: #f8f9fa; padding: 4px 8px; border-radius: 3px; border: 1px solid #e0e0e0;");  // 缩小字体和padding
    lastBootLabel->setMinimumWidth(140);  // 缩小宽度
    lastBootLabel->setMinimumHeight(24);  // 缩小高度

    bootLayout->addWidget(bootTitle);
    bootLayout->addWidget(lastBootLabel);

    // 系统运行时间
    QHBoxLayout *uptimeLayout = new QHBoxLayout();
    uptimeLayout->setSpacing(4);  // 减小间距
    QLabel *uptimeTitle = new QLabel("系统运行:", this);
    uptimeTitle->setStyleSheet("font-weight: bold; font-size: 12px;");  // 缩小字体
    uptimeLabel = new QLabel("--", this);
    uptimeLabel->setStyleSheet("font-size: 13px; color: #666; font-weight: bold; background: #f8f9fa; padding: 4px 8px; border-radius: 3px; border: 1px solid #e0e0e0;");  // 缩小字体和padding
    uptimeLabel->setMinimumWidth(120);  // 缩小宽度
    uptimeLabel->setMinimumHeight(24);  // 缩小高度

    uptimeLayout->addWidget(uptimeTitle);
    uptimeLayout->addWidget(uptimeLabel);

    systemLayout->addLayout(bootLayout);
    systemLayout->addLayout(uptimeLayout);
    systemLayout->addStretch();

    // 将所有布局添加到主监控布局
    monitorLayout->addLayout(tempLayout);
    monitorLayout->addLayout(cpuUsageLayout);
    monitorLayout->addLayout(memoryUsageLayout);
    monitorLayout->addLayout(gpuUsageLayout);
    monitorLayout->addLayout(systemLayout);

    // 操作日志
    QGroupBox *logGroup = new QGroupBox("操作日志", this);
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    logLayout->setContentsMargins(3, 3, 3, 3);  // 减小边距

    logTextEdit = new QTextEdit(this);
    logTextEdit->setReadOnly(true);
    logTextEdit->setStyleSheet("font-size: 11px;");  // 缩小字体

    QHBoxLayout *logButtonLayout = new QHBoxLayout();
    clearLogsButton = new QPushButton("清除日志", this);
    clearLogsButton->setFixedWidth(70);  // 缩小宽度
    clearLogsButton->setFixedHeight(22);  // 设置高度
    clearLogsButton->setStyleSheet("QPushButton { background-color: #607D8B; color: white; border: none; padding: 2px 6px; border-radius: 2px; font-size: 11px; }");  // 缩小padding和字体
    logButtonLayout->addWidget(clearLogsButton);
    logButtonLayout->addStretch();

    logLayout->addWidget(logTextEdit);
    logLayout->addLayout(logButtonLayout);

    leftLayout->addWidget(monitorGroup);
    leftLayout->addWidget(logGroup, 1);

    // 右边区域 - 设备实时信息
    QWidget *rightWidget = new QWidget(this);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(3, 0, 0, 0);  // 减小边距

    QGroupBox *deviceInfoGroup = new QGroupBox("设备实时信息", this);
    QVBoxLayout *deviceInfoLayout = new QVBoxLayout(deviceInfoGroup);
    deviceInfoLayout->setContentsMargins(3, 3, 3, 3);  // 减小边距

    deviceInfoEdit = new QTextEdit(this);
    deviceInfoEdit->setReadOnly(true);
    deviceInfoEdit->setStyleSheet("font-size: 11px;");  // 缩小字体
    deviceInfoLayout->addWidget(deviceInfoEdit);

    // 重新导入按钮布局
    QHBoxLayout *deviceButtonLayout = new QHBoxLayout();
    deviceButtonLayout->setContentsMargins(0, 4, 0, 0);  // 减小边距
    deviceButtonLayout->addStretch();

    QPushButton *reloadDeviceButton = new QPushButton("重新导入", this);
    reloadDeviceButton->setFixedWidth(70);  // 缩小宽度
    reloadDeviceButton->setFixedHeight(22);  // 设置高度
    reloadDeviceButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; border: none; padding: 2px 6px; border-radius: 2px; font-size: 11px; }");  // 缩小padding和字体

    // 修改重新导入功能 - 只清除设备信息栏，不执行设备扫描
    connect(reloadDeviceButton, &QPushButton::clicked, this, [this]() {
        if (deviceInfoEdit) {
            // 清除设备信息栏的旧内容
            deviceInfoEdit->clear();

            // 添加重新导入提示
            QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
            deviceInfoEdit->append(QString("<font color='blue'>=== 设备信息已清除 (%1) ===</font>").arg(timestamp));
            deviceInfoEdit->append("点击\"开始检测\"按钮重新扫描设备");
        }

        if (logger) {
            logger->addMessage("清除设备信息栏内容");
        }
    });

    deviceButtonLayout->addWidget(reloadDeviceButton);

    deviceInfoLayout->addLayout(deviceButtonLayout);

    rightLayout->addWidget(deviceInfoGroup);

    // 将左右部件添加到分割器
    contentSplitter->addWidget(leftWidget);
    contentSplitter->addWidget(rightWidget);

    // 设置分割器比例为1:1
    contentSplitter->setStretchFactor(0, 1);
    contentSplitter->setStretchFactor(1, 1);
    contentSplitter->setSizes(QList<int>() << 1 << 1);

    // 将导航栏和内容分割器添加到主布局
    mainLayout->addWidget(navGroup);
    mainLayout->addWidget(contentSplitter, 1);

    // 连接置顶复选框
    connect(alwaysOnTopCheckBox, &QCheckBox::toggled, this, &ClientWindow::toggleAlwaysOnTop);

    qDebug() << "UI设置完成";
}

void ClientWindow::setupFunctionModules()
{
    // 初始化各个功能模块
    basicTester = new BasicTester(this);
    serverConnector = new ServerConnector(this);
    autoStartManager = new AutoStartManager(this);
    wakeOnLan = new WakeOnLan(this);
    cycleController = new CycleController(this);
    systemMonitor = new SystemMonitor(this);
    deviceManager = new DeviceManager(this);
    logger = new Logger(this);

    // 检查UI组件是否已创建
    if (!basicTestCombo || !executeBasicTestButton) {
        qDebug() << "错误: UI组件未正确初始化";
        return;
    }

    // 设置各个功能模块的UI组件
    basicTester->setupUI(basicTestCombo, executeBasicTestButton);
    serverConnector->setupUI(serverCheckBox);
    autoStartManager->setupUI(autoStartCheckBox);
    wakeOnLan->setupUI(wolToggleButton);

    // 直接调用 setupUI，即使部分组件为 nullptr
    cycleController->setupUI(cycleToggleButton, cycleStatusLabel,
                           cycleOperationCombo, cycleIntervalSpinBox, cycleCountSpinBox);

    // 检查循环控制相关的UI组件
    if (cycleToggleButton && cycleStatusLabel && cycleOperationCombo &&
        cycleIntervalSpinBox && cycleCountSpinBox) {
        cycleController->setupUI(cycleToggleButton, cycleStatusLabel,
                               cycleOperationCombo, cycleIntervalSpinBox, cycleCountSpinBox);
    }

    // 检查系统监控相关的UI组件
    if (cpuTempLabel && diskTempLabel && cpuFanLabel &&
        cpuUsageBar && memoryUsageBar && gpuMemoryBar &&
        lastBootLabel && uptimeLabel) {
        systemMonitor->setupUI(cpuTempLabel, diskTempLabel, cpuFanLabel,
                              cpuUsageBar, memoryUsageBar, gpuMemoryBar,
                              lastBootLabel, uptimeLabel);
    }

    // 检查设备管理相关的UI组件
    if (deviceDetectButton && deviceInfoEdit) {
        deviceManager->setupUI(deviceDetectButton, deviceInfoEdit);
    }

    // 检查日志相关的UI组件
    if (logTextEdit && clearLogsButton) {
        logger->setupUI(logTextEdit, clearLogsButton);
    }

    // 设置模块间的依赖关系
    wakeOnLan->setServerConnector(serverConnector);
    cycleController->setAutoStartManager(autoStartManager);
}

void ClientWindow::connectModules()
{
    // 连接各个模块的信号
    connect(serverConnector, &ServerConnector::logMessage, logger, &Logger::addMessage);
    connect(autoStartManager, &AutoStartManager::logMessage, logger, &Logger::addMessage);
    connect(wakeOnLan, &WakeOnLan::logMessage, logger, &Logger::addMessage);
    connect(cycleController, &CycleController::logMessage, logger, &Logger::addMessage);
    // 关键修复：连接循环完成信号
    connect(cycleController, &CycleController::cycleCompleted, this, &ClientWindow::onCycleCompleted);
    connect(systemMonitor, &SystemMonitor::logMessage, logger, &Logger::addMessage);
    connect(deviceManager, &DeviceManager::logMessage, logger, &Logger::addMessage);
    connect(deviceManager, &DeviceManager::logMessage, logger, &Logger::addMessage);
    //清除设备列表
    connect(deviceManager, &DeviceManager::clearDeviceInfo, this, &ClientWindow::clearDeviceInfoDisplay);

    // 关键修复：连接开机启动状态变化信号
    connect(autoStartManager, &AutoStartManager::autoStartStatusChanged,
            cycleController, &CycleController::handleAutoStartStatusChanged);

    // 设置模块间的依赖关系
    wakeOnLan->setServerConnector(serverConnector);
    cycleController->setAutoStartManager(autoStartManager);

    // 连接设备管理器的信号
    connect(deviceManager, &DeviceManager::deviceChanged, this, &ClientWindow::onDeviceChanged);
    connect(deviceManager, &DeviceManager::usbDeviceChanged, this, &ClientWindow::onUsbDeviceChanged);
    connect(deviceManager, &DeviceManager::inputDeviceChanged, this, &ClientWindow::onInputDeviceChanged);
    connect(deviceManager, &DeviceManager::displayChanged, this, &ClientWindow::onDisplayChanged);
}

// 新增完成提示槽函数
void ClientWindow::onCycleCompleted()
{
    QMessageBox::information(this, "完成", "循环任务已完成所有操作！");
}

// 添加置顶切换函数
void ClientWindow::toggleAlwaysOnTop(bool on)
{
    if (on) {
        setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    } else {
        setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);
    }
    show(); // 需要重新显示窗口使标志生效
    activateWindow(); // 激活窗口
}

void ClientWindow::setupWindowProperties()
{
    // 设置窗口图标（如果有的话）
    // setWindowIcon(QIcon(":/icons/app_icon.png"));

    // 设置窗口大小和位置 - 使用更小的默认大小
    resize(850, 580);

    // 居中显示
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect screenGeometry = screen->geometry();
        int x = (screenGeometry.width() - width()) / 2;
        int y = (screenGeometry.height() - height()) / 2;
        move(x, y);
    }

    // 设置窗口属性
    setAttribute(Qt::WA_DeleteOnClose, false);

    qDebug() << "窗口属性设置完成";
}

void ClientWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    QTimer::singleShot(100, this, [this]() {
        QPoint globalPos = mapToGlobal(QPoint(0, 0));

        if (globalPos.x() < 0 || globalPos.y() < 0 ||
            globalPos.x() > 2000 || globalPos.y() > 2000) {
            move(400, 250);
        }

        // 不再设置置顶属性
        // setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);

        show();
        activateWindow();
        raise();

        // 确保分割器对半分
        QSplitter *splitter = findChild<QSplitter*>();
        if (splitter) {
            int totalWidth = splitter->width();
            QList<int> sizes;
            sizes << totalWidth / 2 << totalWidth / 2;
            splitter->setSizes(sizes);
        }
    });
}

void ClientWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);

    // 确保分割器保持对半分
    QSplitter *splitter = findChild<QSplitter*>();
    if (splitter) {
        int totalWidth = splitter->width();
        QList<int> sizes;
        sizes << totalWidth / 2 << totalWidth / 2;
        splitter->setSizes(sizes);
    }
}

void ClientWindow::closeEvent(QCloseEvent *event)
{
    // 保存状态
    if (cycleController) {
        cycleController->saveState();
    }
    if (wakeOnLan) {
        wakeOnLan->saveState();  // 新增：保存网络唤醒状态
    }
    if (logger) {
        logger->saveLogToFile();
    }

    QMainWindow::closeEvent(event);
}

// 设备相关槽函数
void ClientWindow::onDeviceChanged(const QString &message)
{
//    deviceInfoEdit->append(message);

//    QTextCursor cursor = deviceInfoEdit->textCursor();
//    cursor.movePosition(QTextCursor::End);
//    deviceInfoEdit->setTextCursor(cursor);
}

void ClientWindow::onUsbDeviceChanged(const QString &action, const QString &deviceInfo)
{
//    QString message;
//    if (action == "add") {
//        message = QString("🎯 USB设备插入: %1").arg(deviceInfo);
//        deviceInfoEdit->append("<font color='green'>" + message + "</font>");
//    } else if (action == "remove") {
//        message = QString("🚫 USB设备移除: %1").arg(deviceInfo);
//        deviceInfoEdit->append("<font color='red'>" + message + "</font>");
//    }

//    QTextCursor cursor = deviceInfoEdit->textCursor();
//    cursor.movePosition(QTextCursor::End);
//    deviceInfoEdit->setTextCursor(cursor);
}

void ClientWindow::onInputDeviceChanged(const QString &action, const QString &deviceInfo)
{
//    QString message;
//    if (action == "add") {
//        message = QString("🖱️  输入设备插入: %1").arg(deviceInfo);
//        deviceInfoEdit->append("<font color='green'>" + message + "</font>");
//    } else if (action == "remove") {
//        message = QString("⌨️  输入设备移除: %1").arg(deviceInfo);
//        deviceInfoEdit->append("<font color='red'>" + message + "</font>");
//    }

//    QTextCursor cursor = deviceInfoEdit->textCursor();
//    cursor.movePosition(QTextCursor::End);
//    deviceInfoEdit->setTextCursor(cursor);
}

void ClientWindow::onDisplayChanged(const QString &action, const QString &displayInfo)
{
//    QString message = QString("🖥️  显示设备%1: %2").arg(action).arg(displayInfo);
//    deviceInfoEdit->append("<font color='blue'>" + message + "</font>");

//    QTextCursor cursor = deviceInfoEdit->textCursor();
//    cursor.movePosition(QTextCursor::End);
//    deviceInfoEdit->setTextCursor(cursor);
}

// 清空设备实时信息窗口
void ClientWindow::clearDeviceInfoDisplay()
{
    if (deviceInfoEdit) {
        deviceInfoEdit->clear();  // 清空设备实时信息窗口

        // 可以添加一个状态提示
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        deviceInfoEdit->append(QString("<font color='gray'>设备信息已更新于 %1</font>").arg(timestamp));

        qDebug() << "设备实时信息列表已清空";
    }
}
