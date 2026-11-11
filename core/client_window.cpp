#include "client_window.h"
#include "basic_tester.h"
#include "server_connector.h"
#include "auto_start_manager.h"
#include "wake_on_lan.h"
#include "cycle_controller.h"
#include "system_monitor.h"
#include "device_manager.h"
#include "logger.h"
#include "illegal_shutdown_detector.h"
#include "unified_control_manager.h"
#include "network_speed_tester.h"
#include "../dialogs/unified_control_dialog.h"
#include "../common/protocol.h"
#include "system_controller.h"

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
#include <QScreen>
#include <QGuiApplication>
#include <QDateTime>
#include <QMessageBox>
#include <QInputDialog>
#include <QDialog>
#include <QTextBrowser>
#include <QDialogButtonBox>
#include <QScrollBar>
#include <QListWidgetItem>
#include <QListWidget>
#include <QFileInfo>
#include <QSettings>
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QDir>
#include <QRegularExpression>

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
    , illegalShutdownButton(nullptr)
    , cpuTempLabel(nullptr)
    , diskTempLabel(nullptr)
    , cpuFanLabel(nullptr)
    , cpuUsageBar(nullptr)
    , memoryUsageBar(nullptr)
    , gpuMemoryBar(nullptr)
    , lastBootLabel(nullptr)
    , uptimeLabel(nullptr)
    , bootCountLabel(nullptr)  // 开机计数
    , basicTester(nullptr)
    , serverConnector(nullptr)
    , autoStartManager(nullptr)
    , wakeOnLan(nullptr)
    , cycleController(nullptr)
    , systemMonitor(nullptr)
    , deviceManager(nullptr)
    , logger(nullptr)
    , illegalShutdownDetector(nullptr)
    , unifiedControlManager(nullptr)
    , lanSpeedService(nullptr)
    , tagComboBox(nullptr)
    , addTagButton(nullptr)
    , manageTagsButton(nullptr)
    , tagStatusLabel(nullptr)
{
    try {
        setupUI();
        setupFunctionModules();
        connectModules();
        setupWindowProperties();

        // 延迟启动系统监控
        QTimer::singleShot(300, this, [this]() {
            if (systemMonitor) {
                systemMonitor->startMonitoring();
            }
        });

        // 延迟检测非法关机 - 缩短启动延迟
        QTimer::singleShot(800, this, [this]() {
            qDebug() << "开始延迟非法关机检测...";
            if (illegalShutdownDetector) {
                // 执行启动检测
                illegalShutdownDetector->checkOnStartup();
            } else {
                qDebug() << "错误: 非法关机检测器未初始化";
            }
        });

    } catch (const std::exception &e) {
        qDebug() << "初始化异常:" << e.what();
        QMessageBox::critical(this, "初始化错误",
                             QString("应用程序初始化失败:\n%1").arg(e.what()));
    }
}

ClientWindow::~ClientWindow()
{
    // 析构函数会自动删除子对象
    if (wakeOnLan) {
        wakeOnLan->saveState();
    }
}

void ClientWindow::setupUI()
{
    qDebug() << "开始设置UI...";

    setWindowTitle("研盛测试客户端V2.1");
    setMinimumSize(850, 580);

    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

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
    basicTestCombo->addItem("网络测速");
    basicTestCombo->addItem("跑分测试");
    basicTestCombo->addItem("老化测试");
    basicTestCombo->addItem("统一控制");
    basicTestCombo->setMinimumWidth(100);
    basicTestCombo->setMaximumHeight(25);

    executeBasicTestButton = new QPushButton("执行", this);
    executeBasicTestButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; border: none; padding: 4px 10px; border-radius: 3px; font-size: 12px; }");
    executeBasicTestButton->setFixedWidth(55);
    executeBasicTestButton->setFixedHeight(25);

    basicTestLayout->addWidget(basicTestLabel);
    basicTestLayout->addWidget(basicTestCombo);
    basicTestLayout->addWidget(executeBasicTestButton);

    // 连接控制
    QHBoxLayout *connectLayout = new QHBoxLayout();
    connectLayout->setSpacing(8);

    serverCheckBox = new QCheckBox("连接服务器", this);
    serverCheckBox->setFixedWidth(90);

    autoStartCheckBox = new QCheckBox("开机启动", this);
    autoStartCheckBox->setFixedWidth(80);

    QCheckBox *alwaysOnTopCheckBox = new QCheckBox("窗口置顶", this);
    alwaysOnTopCheckBox->setFixedWidth(80);

    // 开机次数显示 - 放在窗口置顶右侧（与执行按钮风格统一：绿色）
    bootCountLabel = new QLabel("开机: 0", this);
    bootCountLabel->setAlignment(Qt::AlignCenter);
    bootCountLabel->setStyleSheet(
        "QLabel { background-color: #4CAF50; color: white; border: none; "
        "padding: 4px 10px; border-radius: 3px; font-size: 12px; }");
    bootCountLabel->setFixedHeight(25);
    bootCountLabel->setToolTip("系统累计启动次数（双击清零）");
    bootCountLabel->setCursor(Qt::PointingHandCursor);
    bootCountLabel->installEventFilter(this);  // 安装事件过滤器以支持双击清零

    connectLayout->addWidget(serverCheckBox);
    connectLayout->addWidget(autoStartCheckBox);
    connectLayout->addWidget(alwaysOnTopCheckBox);
    connectLayout->addWidget(bootCountLabel);

    // 新增：非法关机提示按钮（初始隐藏）
    illegalShutdownButton = new QPushButton(this);
    illegalShutdownButton->setStyleSheet("QPushButton { background-color: #FF5722; color: white; border: none; padding: 4px 12px; border-radius: 3px; font-size: 12px; font-weight: bold; }"
                                         "QPushButton:hover { background-color: #FF7043; }");
    illegalShutdownButton->setText("⚠ 非法关机");
    illegalShutdownButton->setFixedWidth(250);
    illegalShutdownButton->setFixedHeight(25);
    illegalShutdownButton->setVisible(false);  // 初始隐藏
    illegalShutdownButton->setCursor(Qt::PointingHandCursor);

    // 连接点击事件
    connect(illegalShutdownButton, &QPushButton::clicked, this, &ClientWindow::showIllegalShutdownLog);

    navRow1->addLayout(basicTestLayout);
    navRow1->addSpacing(10);
    navRow1->addLayout(connectLayout);
    navRow1->addStretch();
    navRow1->addWidget(illegalShutdownButton);  // 非法关机按钮在最右侧

    // 第二行：网络唤醒、循环控制、设备检测
    QHBoxLayout *navRow2 = new QHBoxLayout();

    // 网络唤醒（保持不变）
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

    // 设备检测（保持不变）
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

    navRow2->addLayout(wolLayout);
    navRow2->addSpacing(10);
    navRow2->addLayout(cycleLayout);
    navRow2->addSpacing(10);
    navRow2->addLayout(deviceLayout);
    navRow2->addStretch();

    navLayout->addLayout(navRow1);
    navLayout->addLayout(navRow2);

    // 内容区域（保持不变）
    QSplitter *contentSplitter = new QSplitter(Qt::Horizontal, this);
    contentSplitter->setChildrenCollapsible(false);

    // 左边区域
    QWidget *leftWidget = new QWidget(this);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 3, 0);
    leftLayout->setSpacing(6);

    // 实时系统监控（保持不变）
    QGroupBox *monitorGroup = new QGroupBox("实时系统监控", this);
    monitorGroup->setMinimumHeight(200);
    monitorGroup->setMaximumHeight(220);
    QVBoxLayout *monitorLayout = new QVBoxLayout(monitorGroup);
    monitorLayout->setSpacing(6);
    monitorLayout->setContentsMargins(10, 10, 10, 10);

    // 温度信息
    QHBoxLayout *tempLayout = new QHBoxLayout();
    tempLayout->setSpacing(15);
    tempLayout->setContentsMargins(0, 0, 0, 4);

    cpuTempLabel = new QLabel("CPU温度: --°C", this);
    cpuTempLabel->setAlignment(Qt::AlignCenter);
    cpuTempLabel->setStyleSheet("font-size: 12px; color: #2196F3; font-weight: bold; background: #f8f9fa; padding: 6px 10px; border-radius: 4px; border: 1px solid #e0e0e0;");
    cpuTempLabel->setMinimumWidth(120);
    cpuTempLabel->setMinimumHeight(28);

    diskTempLabel = new QLabel("硬盘温度: --°C", this);
    diskTempLabel->setAlignment(Qt::AlignCenter);
    diskTempLabel->setStyleSheet("font-size: 12px; color: #4CAF50; font-weight: bold; background: #f8f9fa; padding: 6px 10px; border-radius: 4px; border: 1px solid #e0e0e0;");
    diskTempLabel->setMinimumWidth(120);
    diskTempLabel->setMinimumHeight(28);

    cpuFanLabel = new QLabel("CPU风扇: -- RPM", this);
    cpuFanLabel->setAlignment(Qt::AlignCenter);
    cpuFanLabel->setStyleSheet("font-size: 12px; color: #FF9800; font-weight: bold; background: #f8f9fa; padding: 6px 10px; border-radius: 4px; border: 1px solid #e0e0e0;");
    cpuFanLabel->setMinimumWidth(120);
    cpuFanLabel->setMinimumHeight(28);

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

    cpuUsageBar->setStyleSheet("QProgressBar { border: 1px solid #ccc; border-radius: 3px; background: #f0f0f0; padding: 1px; font-size: 10px; min-width: 200px; } QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4CAF50, stop:1 #8BC34A); border-radius: 2px; }");

    cpuUsageLayout->addWidget(cpuUsageTitle);
    cpuUsageLayout->addWidget(cpuUsageBar, 1);
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
    memoryUsageLayout->addWidget(memoryUsageBar, 1);
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
    gpuUsageLayout->addWidget(gpuMemoryBar, 1);
    gpuUsageLayout->addStretch();

    // 系统信息
    QHBoxLayout *systemLayout = new QHBoxLayout();
    systemLayout->setSpacing(12);
    systemLayout->setContentsMargins(0, 4, 0, 0);

    // 上次开机
    QHBoxLayout *bootLayout = new QHBoxLayout();
    bootLayout->setSpacing(2);
    QLabel *bootTitle = new QLabel("上次开机:", this);
    bootTitle->setStyleSheet("font-weight: bold; font-size: 12px;");
    lastBootLabel = new QLabel("--", this);
    lastBootLabel->setStyleSheet("font-size: 13px; color: #666; font-weight: bold; background: #f8f9fa; padding: 4px 8px; border-radius: 3px; border: 1px solid #e0e0e0;");
    lastBootLabel->setMinimumWidth(140);
    lastBootLabel->setMinimumHeight(24);

    bootLayout->addWidget(bootTitle);
    bootLayout->addWidget(lastBootLabel);

    // 系统运行时间
    QHBoxLayout *uptimeLayout = new QHBoxLayout();
    uptimeLayout->setSpacing(4);
    QLabel *uptimeTitle = new QLabel("系统运行:", this);
    uptimeTitle->setStyleSheet("font-weight: bold; font-size: 12px;");
    uptimeLabel = new QLabel("--", this);
    uptimeLabel->setStyleSheet("font-size: 13px; color: #666; font-weight: bold; background: #f8f9fa; padding: 4px 8px; border-radius: 3px; border: 1px solid #e0e0e0;");
    uptimeLabel->setMinimumWidth(120);
    uptimeLabel->setMinimumHeight(24);

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

    // 操作日志（保持不变）
    QGroupBox *logGroup = new QGroupBox("操作日志", this);
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    logLayout->setContentsMargins(3, 3, 3, 3);

    logTextEdit = new QTextEdit(this);
    logTextEdit->setReadOnly(true);
    logTextEdit->setStyleSheet("font-size: 11px;");

    QHBoxLayout *logButtonLayout = new QHBoxLayout();
    clearLogsButton = new QPushButton("清除日志", this);
    clearLogsButton->setFixedWidth(70);
    clearLogsButton->setFixedHeight(22);
    clearLogsButton->setStyleSheet("QPushButton { background-color: #607D8B; color: white; border: none; padding: 2px 6px; border-radius: 2px; font-size: 11px; }");
    logButtonLayout->addWidget(clearLogsButton);
    logButtonLayout->addStretch();

    logLayout->addWidget(logTextEdit);
    logLayout->addLayout(logButtonLayout);

    leftLayout->addWidget(monitorGroup);
    leftLayout->addWidget(logGroup, 1);

    // 右边区域 - 设备实时信息（保持不变）
    QWidget *rightWidget = new QWidget(this);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(3, 0, 0, 0);

    QGroupBox *deviceInfoGroup = new QGroupBox("设备实时信息", this);
    QVBoxLayout *deviceInfoLayout = new QVBoxLayout(deviceInfoGroup);
    deviceInfoLayout->setContentsMargins(3, 3, 3, 3);

    deviceInfoEdit = new QTextEdit(this);
    deviceInfoEdit->setReadOnly(true);
    deviceInfoEdit->setStyleSheet("font-size: 11px;");
    deviceInfoLayout->addWidget(deviceInfoEdit);

    QHBoxLayout *deviceButtonLayout = new QHBoxLayout();
    deviceButtonLayout->setContentsMargins(0, 4, 0, 0);
    deviceButtonLayout->addStretch();

    QPushButton *viewLogButton = new QPushButton("查看日志", this);
    viewLogButton->setFixedWidth(70);
    viewLogButton->setFixedHeight(22);
    viewLogButton->setStyleSheet("QPushButton { background-color: #2196F3; color: white; border: none; padding: 2px 6px; border-radius: 2px; font-size: 11px; }");

    connect(viewLogButton, &QPushButton::clicked, this, [this]() {
        showDeviceChangeLog();
    });

    deviceButtonLayout->addWidget(viewLogButton);

    QPushButton *reloadDeviceButton = new QPushButton("重新导入", this);
    reloadDeviceButton->setFixedWidth(70);
    reloadDeviceButton->setFixedHeight(22);
    reloadDeviceButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; border: none; padding: 2px 6px; border-radius: 2px; font-size: 11px; }");

    connect(reloadDeviceButton, &QPushButton::clicked, this, [this]() {
        // 清空丢失设备记录
        if (deviceManager) {
            deviceManager->clearLostDeviceRecord();
            deviceManager->performQuickScan();
        }

        if (logger) {
            logger->addMessage("重新导入设备列表，已清空丢失设备记录");
        }
    });

    deviceButtonLayout->addWidget(reloadDeviceButton);
    deviceInfoLayout->addLayout(deviceButtonLayout);
    rightLayout->addWidget(deviceInfoGroup);

    // === 标签管理与统一控制区域 ===
    setupTagUI(rightWidget);

    contentSplitter->addWidget(leftWidget);
    contentSplitter->addWidget(rightWidget);
    contentSplitter->setStretchFactor(0, 1);
    contentSplitter->setStretchFactor(1, 1);
    contentSplitter->setSizes(QList<int>() << 1 << 1);

    mainLayout->addWidget(navGroup);
    mainLayout->addWidget(contentSplitter, 1);

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
    illegalShutdownDetector = new IllegalShutdownDetector(this);
    unifiedControlManager = new UnifiedControlManager(this);

    // 添加详细的调试输出
    qDebug() << "=== 功能模块初始化状态 ===";
    qDebug() << "非法关机检测器:" << (illegalShutdownDetector ? "成功创建" : "创建失败");
    qDebug() << "非法关机按钮:" << (illegalShutdownButton ? "已创建" : "未创建");

    // 确保按钮存在
    if (!illegalShutdownButton) {
        qDebug() << "警告: 非法关机按钮未在UI中创建";
    }

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

    // 循环控制器 UI（只调用一次，避免重复信号连接）
    cycleController->setupUI(cycleToggleButton, cycleStatusLabel,
                           cycleOperationCombo, cycleIntervalSpinBox, cycleCountSpinBox);

    // 检查系统监控相关的UI组件
    if (cpuTempLabel && diskTempLabel && cpuFanLabel) {
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

    // 设置模块间的依赖关系（只在此处设置一次，connectModules 不再重复）
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
    connect(cycleController, &CycleController::cycleCompleted, this, &ClientWindow::onCycleCompleted);
    connect(systemMonitor, &SystemMonitor::logMessage, logger, &Logger::addMessage);
    connect(deviceManager, &DeviceManager::logMessage, logger, &Logger::addMessage);
    connect(deviceManager, &DeviceManager::clearDeviceInfo, this, &ClientWindow::clearDeviceInfoDisplay);

    // 关键修复：连接开机启动状态变化信号
    connect(autoStartManager, &AutoStartManager::autoStartStatusChanged,
            cycleController, &CycleController::handleAutoStartStatusChanged);

    // 关键修复：连接循环运行状态变化到AutoStartManager（循环保护机制）
    connect(cycleController, &CycleController::cycleRunningChanged,
            autoStartManager, &AutoStartManager::setCycleRunning);

    // 注: wakeOnLan->setServerConnector 和 cycleController->setAutoStartManager
    // 已在 setupFunctionModules 中设置，此处不再重复调用

    // 连接设备管理器的信号（设备变化事件显示到"设备实时信息"日志栏，而非操作日志）
    connect(deviceManager, &DeviceManager::deviceChanged, this, &ClientWindow::onDeviceChanged);
    connect(deviceManager, &DeviceManager::usbDeviceChanged, this, &ClientWindow::onUsbDeviceChanged);
    connect(deviceManager, &DeviceManager::inputDeviceChanged, this, &ClientWindow::onInputDeviceChanged);
    connect(deviceManager, &DeviceManager::displayChanged, this, &ClientWindow::onDisplayChanged);

    // 连接非法关机检测器的信号
    bool signal1 = connect(illegalShutdownDetector, &IllegalShutdownDetector::illegalShutdownDetected,
                          this, &ClientWindow::onIllegalShutdownDetected,
                          Qt::QueuedConnection);

    bool signal2 = connect(illegalShutdownDetector, &IllegalShutdownDetector::illegalShutdownStateRestored,
                          this, &ClientWindow::onIllegalShutdownStateRestored,
                          Qt::QueuedConnection);

    // 新增：连接非法关机次数变化信号 → 仅日志输出（不再有标签显示）
    connect(illegalShutdownDetector, &IllegalShutdownDetector::illegalShutdownCountChanged,
            this, [this](int count) {
        qDebug() << "非法关机次数更新为:" << count;
    });

    // 新增：程序异常退出事件 → 输出到日志（区别于系统非法关机）
    connect(illegalShutdownDetector, &IllegalShutdownDetector::appCrashDetected,
            logger, &Logger::addMessage);

    qDebug() << "非法关机检测信号连接状态:";
    qDebug() << "  illegalShutdownDetected:" << (signal1 ? "成功" : "失败");
    qDebug() << "  illegalShutdownStateRestored:" << (signal2 ? "成功" : "失败");

    // 初始化开机计数（以系统启动次数为依据，使用 btmp 日志）
    if (bootCountLabel) {
        int bootCount = getSystemBootCount();
        bootCountLabel->setText(QString("开机: %1").arg(bootCount));
    }

    // 连接统一控制管理器的信号
    connect(unifiedControlManager, &UnifiedControlManager::logMessage, logger, &Logger::addMessage);
    connect(unifiedControlManager, &UnifiedControlManager::peersUpdated, this, [this]() {
        if (unifiedControlManager && tagStatusLabel) {
            int peerCount = unifiedControlManager->getPeerList().count();
            tagStatusLabel->setText(QString("同标签节点: %1台").arg(peerCount));
            tagStatusLabel->setStyleSheet(
                "QLabel { color: #2196F3; font-size: 11px; background: #E3F2FD; "
                "padding: 2px 8px; border-radius: 2px; border: 1px solid #90CAF9; }");
        }
    });
    connect(unifiedControlManager, &UnifiedControlManager::commandReceived,
            this, &ClientWindow::onUnifiedCommandReceived);

    // 启动节点发现
    unifiedControlManager->startDiscovery();

    // 启动内网测速服务（修复 B12：网络测速离线化）
    // 常驻监听 TCP 18964 / UDP 18965，被动响应局域网内其他机器发起的测速，
    // 对端无需任何操作；本机点"网络测速"时会自动发现并连接这些对端。
    // 启动失败不致命（端口被占用时仅日志提示，本机仍可作为发起方测速）。
    lanSpeedService = new NetworkSpeedTester(this);
    if (lanSpeedService->startLanServer()) {
        qDebug() << "[内网测速] 服务已随主程序启动（对端可自动发现本机）";
    }

    // 连接统一控制按钮（基础测试中的"统一控制"选项由BasicTester处理转发）
    connect(basicTester, &BasicTester::unifiedControlTriggered,
            this, &ClientWindow::onUnifiedControl);

    // 设置统一控制管理器的标签
    if (tagComboBox) {
        unifiedControlManager->setCurrentTag(tagComboBox->currentText());
    }
}

// 新增：非法关机状态恢复处理函数
void ClientWindow::onIllegalShutdownStateRestored(const QDateTime &shutdownTime, const QString &logInfo)
{
    qDebug() << "=== 收到非法关机状态恢复信号 ===";
    qDebug() << "关机时间:" << shutdownTime.toString("yyyy-MM-dd HH:mm:ss");
    qDebug() << "日志长度:" << logInfo.length();

    // 更新UI显示，但不记录日志（因为之前已经记录过了）
    updateIllegalShutdownButton(shutdownTime, logInfo);

    // 记录到日志系统（可选）
    if (logger) {
        logger->addMessage("恢复未处理的非法关机事件，发生时间: " +
                          shutdownTime.toString("yyyy-MM-dd HH:mm:ss"));
    }
}

void ClientWindow::onIllegalShutdownDetected(const QDateTime &shutdownTime, const QString &logInfo)
{
    qDebug() << "=== 收到非法关机检测信号 ===";
    qDebug() << "关机时间:" << shutdownTime.toString("yyyy-MM-dd HH:mm:ss");
    qDebug() << "日志长度:" << logInfo.length();

    if (logInfo.isEmpty() || logInfo.trimmed().isEmpty()) {
        qDebug() << "警告: 收到的日志为空或只有空格";
        return;
    }

    // 获取当前非法关机次数
    int count = illegalShutdownDetector ? illegalShutdownDetector->getIllegalShutdownCount() : 0;

    // 只有在非法关机次数大于0时才显示按钮
    if (count <= 0) {
        qDebug() << "非法关机次数为0，不显示按钮";
        return;
    }

    // 保存日志信息和关机时间
    lastIllegalShutdownLog = logInfo;
    lastShutdownTime = shutdownTime;
    lastCheckTime = QDateTime::currentDateTime();

    // 记录到日志系统
    if (logger) {
        logger->addMessage("检测到非法关机事件，发生时间: " +
                          lastShutdownTime.toString("yyyy-MM-dd HH:mm:ss") +
                          "，累计次数: " + QString::number(count));
    }

    // 检查按钮是否存在
    if (!illegalShutdownButton) {
        qDebug() << "错误: 非法关机按钮指针为空";
        return;
    }

    // 确保在主线程中更新UI
    illegalShutdownButton->setVisible(true);

    // 更新按钮文本 - 显示非法关机发生时间和次数
    QString timeStr = lastShutdownTime.toString("yyyy-MM-dd HH:mm");
    illegalShutdownButton->setText(QString("⚠ 非法关机(%1次)").arg(count));

    // 更新工具提示，显示详细信息
    updateIllegalShutdownTooltip();

    // 设置醒目的样式
    illegalShutdownButton->setStyleSheet(
        "QPushButton {"
        "    background-color: #FF5722;"
        "    color: white;"
        "    border: 2px solid #FF9800;"
        "    padding: 4px 12px;"
        "    border-radius: 4px;"
        "    font-size: 12px;"
        "    font-weight: bold;"
        "    min-width: 180px;"  // 稍微加宽以容纳次数信息
        "}"
        "QPushButton:hover {"
        "    background-color: #FF7043;"
        "    border-color: #FFB74D;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #D84315;"
        "}"
    );

    qDebug() << "按钮已更新，文本:" << illegalShutdownButton->text();

    // 添加闪烁动画效果
    startButtonBlinkAnimation();

    // 强制更新UI
    illegalShutdownButton->update();
}

void ClientWindow::updateIllegalShutdownTooltip()
{
    if (!illegalShutdownDetector || !illegalShutdownButton) return;

    int count = illegalShutdownDetector->getIllegalShutdownCount();
    QString tooltip = QString("点击查看详细日志\n"
                             "累计非法关机次数: %1 次\n"
                             "最近发生时间: %2\n"
                             "检测时间: %3\n"
                             "日志长度: %4 字符")
                             .arg(count)
                             .arg(lastShutdownTime.toString("yyyy-MM-dd HH:mm:ss"))
                             .arg(lastCheckTime.toString("yyyy-MM-dd HH:mm:ss"))
                             .arg(lastIllegalShutdownLog.length());

    illegalShutdownButton->setToolTip(tooltip);
}

// 新增：更新非法关机按钮显示（公共处理函数）
void ClientWindow::updateIllegalShutdownButton(const QDateTime &shutdownTime, const QString &logInfo)
{
    // 获取当前非法关机次数
    int count = illegalShutdownDetector ? illegalShutdownDetector->getIllegalShutdownCount() : 0;

    // 只有在非法关机次数大于0时才显示按钮
    if (count <= 0) {
        qDebug() << "非法关机次数为0，隐藏按钮";
        if (illegalShutdownButton) {
            illegalShutdownButton->setVisible(false);
        }
        return;
    }

    // 保存日志信息和关机时间
    lastIllegalShutdownLog = logInfo;
    lastShutdownTime = shutdownTime;
    lastCheckTime = QDateTime::currentDateTime();

    // 检查按钮是否存在
    if (!illegalShutdownButton) {
        qDebug() << "错误: 非法关机按钮指针为空";
        return;
    }

    // 确保在主线程中更新UI
    illegalShutdownButton->setVisible(true);

    // 更新按钮文本 - 显示非法关机发生时间和次数
    QString timeStr = lastShutdownTime.toString("yyyy-MM-dd HH:mm");
    illegalShutdownButton->setText(QString("⚠ 非法关机(%1次)").arg(count));

    // 更新工具提示
    updateIllegalShutdownTooltip();

    // 设置醒目的样式
    illegalShutdownButton->setStyleSheet(
        "QPushButton {"
        "    background-color: #FF5722;"
        "    color: white;"
        "    border: 2px solid #FF9800;"
        "    padding: 4px 12px;"
        "    border-radius: 4px;"
        "    font-size: 12px;"
        "    font-weight: bold;"
        "    min-width: 180px;"
        "}"
        "QPushButton:hover {"
        "    background-color: #FF7043;"
        "    border-color: #FFB74D;"
        "}"
        "QPushButton:pressed {"
        "    background-color: #D84315;"
        "}"
    );

    qDebug() << "按钮已更新，文本:" << illegalShutdownButton->text();

    // 添加闪烁动画效果
    startButtonBlinkAnimation();

    // 强制更新UI
    illegalShutdownButton->update();
}

// 新增：按钮闪烁动画
void ClientWindow::startButtonBlinkAnimation()
{
    if (!illegalShutdownButton) return;

    // 创建简单的时间动画
    for (int i = 0; i < 3; i++) {
        QTimer::singleShot(i * 300, this, [this, i]() {
            if (illegalShutdownButton) {
                if (i % 2 == 0) {
                    illegalShutdownButton->setStyleSheet(
                        "QPushButton {"
                        "    background-color: #FF9800;"
                        "    color: white;"
                        "    border: 2px solid #FF5722;"
                        "    padding: 4px 12px;"
                        "    border-radius: 4px;"
                        "    font-size: 12px;"
                        "    font-weight: bold;"
                        "    min-width: 140px;"
                        "}"
                    );
                } else {
                    illegalShutdownButton->setStyleSheet(
                        "QPushButton {"
                        "    background-color: #FF5722;"
                        "    color: white;"
                        "    border: 2px solid #FF9800;"
                        "    padding: 4px 12px;"
                        "    border-radius: 4px;"
                        "    font-size: 12px;"
                        "    font-weight: bold;"
                        "    min-width: 140px;"
                        "}"
                    );
                }
            }
        });
    }

    // 最终样式
    QTimer::singleShot(300, this, [this]() {
        if (illegalShutdownButton) {
            illegalShutdownButton->setStyleSheet(
                "QPushButton {"
                "    background-color: #FF5722;"
                "    color: white;"
                "    border: 2px solid #FF9800;"
                "    padding: 4px 12px;"
                "    border-radius: 4px;"
                "    font-size: 12px;"
                "    font-weight: bold;"
                "    min-width: 140px;"
                "}"
                "QPushButton:hover {"
                "    background-color: #FF7043;"
                "    border-color: #FFB74D;"
                "}"
                "QPushButton:pressed {"
                "    background-color: #D84315;"
                "}"
            );
        }
    });
}

// 新增完成提示槽函数
void ClientWindow::onCycleCompleted()
{
    QMessageBox::information(this, "完成", "循环任务已完成所有操作！");
    
    // 立即重置循环状态
    if (cycleController) {
        cycleController->stopCycle();
        cycleController->resetToggleButtonText();
        cycleController->updateStatusLabel("循环未开始", "QLabel { color: #666; font-size: 12px; }");
    }
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

// 设置标签管理UI
void ClientWindow::setupTagUI(QWidget *parentWidget)
{
    QVBoxLayout *parentLayout = qobject_cast<QVBoxLayout*>(parentWidget->layout());
    if (!parentLayout) return;

    QGroupBox *tagGroup = new QGroupBox("标签与统一控制", this);
    QVBoxLayout *tagLayout = new QVBoxLayout(tagGroup);
    tagLayout->setSpacing(6);
    tagLayout->setContentsMargins(10, 8, 10, 8);

    // 第一行：标签选择
    QHBoxLayout *tagSelectLayout = new QHBoxLayout();

    QLabel *tagLabel = new QLabel("机器标签:", this);
    tagLabel->setStyleSheet("font-weight: bold; font-size: 12px;");
    tagLabel->setFixedWidth(65);

    tagComboBox = new QComboBox(this);
    tagComboBox->setMinimumWidth(100);
    tagComboBox->setFixedHeight(25);
    tagComboBox->setEditable(false);

    loadTagsFromSettings();

    // 当前选择的标签
    QSettings settings("ELSKY", "TestClient");
    QString savedTag = settings.value("unified_control/tag", "唐贵宝1").toString();
    int savedIndex = tagComboBox->findText(savedTag);
    if (savedIndex >= 0) {
        tagComboBox->setCurrentIndex(savedIndex);
    }

    connect(tagComboBox, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &ClientWindow::onTagChanged);

    tagSelectLayout->addWidget(tagLabel);
    tagSelectLayout->addWidget(tagComboBox, 1);
    tagLayout->addLayout(tagSelectLayout);

    // 第二行：标签管理按钮 + 状态
    QHBoxLayout *tagButtonLayout = new QHBoxLayout();

    addTagButton = new QPushButton("添加标签", this);
    addTagButton->setFixedHeight(24);
    addTagButton->setStyleSheet(
        "QPushButton { background-color: #2196F3; color: white; border: none; padding: 3px 10px; border-radius: 3px; font-size: 11px; }"
        "QPushButton:hover { background-color: #42A5F5; }"
        "QPushButton:pressed { background-color: #1E88E5; }");
    connect(addTagButton, &QPushButton::clicked, this, &ClientWindow::onAddTag);

    manageTagsButton = new QPushButton("管理标签", this);
    manageTagsButton->setFixedHeight(24);
    manageTagsButton->setStyleSheet(
        "QPushButton { background-color: #FF9800; color: white; border: none; padding: 3px 10px; border-radius: 3px; font-size: 11px; }"
        "QPushButton:hover { background-color: #FFB74D; }"
        "QPushButton:pressed { background-color: #F57C00; }");
    connect(manageTagsButton, &QPushButton::clicked, this, &ClientWindow::onManageTags);

    tagStatusLabel = new QLabel("未启动", this);
    tagStatusLabel->setStyleSheet(
        "QLabel { color: #666; font-size: 11px; background: #f5f5f5; padding: 2px 8px; border-radius: 2px; border: 1px solid #ddd; }");
    tagStatusLabel->setFixedHeight(24);
    tagStatusLabel->setAlignment(Qt::AlignCenter);

    tagButtonLayout->addWidget(addTagButton);
    tagButtonLayout->addWidget(manageTagsButton);
    tagButtonLayout->addStretch();
    tagButtonLayout->addWidget(tagStatusLabel);
    tagLayout->addLayout(tagButtonLayout);

    parentLayout->addWidget(tagGroup);
}

void ClientWindow::loadTagsFromSettings()
{
    if (!tagComboBox) return;

    tagComboBox->clear();

    QSettings settings("ELSKY", "TestClient");
    QStringList tags = settings.value("tags/list", QStringList()
        << "唐贵宝1" << "唐贵宝2" << "唐贵宝3"
        << "万消1" << "万消2" << "万消3"
        << "胡毅1" << "胡毅2" << "胡毅3"
        << "李胜梅1" << "李胜梅2" << "李胜梅3"
        << "赖玲玲1" << "赖玲玲2" << "赖玲玲3"
        << "廖爱玲1" << "廖爱玲2" << "廖爱玲3"
        << "曾周1" << "曾周2" << "曾周3"
        << "付苏宁1" << "付苏宁2" << "付苏宁3"
        << "刘谷丰1" << "刘谷丰2" << "刘谷丰3"
        << "刘伯强1" << "刘伯强2" << "刘伯强3").toStringList();

    for (const QString &tag : tags) {
        tagComboBox->addItem(tag);
    }
}

void ClientWindow::saveTagsToSettings()
{
    if (!tagComboBox) return;

    QStringList tags;
    for (int i = 0; i < tagComboBox->count(); ++i) {
        tags.append(tagComboBox->itemText(i));
    }

    QSettings settings("ELSKY", "TestClient");
    settings.setValue("tags/list", tags);
    settings.sync();
}

void ClientWindow::onTagChanged(int index)
{
    if (!tagComboBox || index < 0) return;

    QString newTag = tagComboBox->currentText();
    if (unifiedControlManager) {
        unifiedControlManager->setCurrentTag(newTag);
    }

    if (logger) {
        logger->addMessage(QString("切换标签: %1").arg(newTag));
    }
}

void ClientWindow::onAddTag()
{
    if (!tagComboBox) return;

    bool ok;
    QString newTag = QInputDialog::getText(this, "添加标签",
                                            "请输入新标签名称:",
                                            QLineEdit::Normal, "", &ok);
    if (!ok || newTag.trimmed().isEmpty()) return;

    newTag = newTag.trimmed();

    // 检查重复
    for (int i = 0; i < tagComboBox->count(); ++i) {
        if (tagComboBox->itemText(i) == newTag) {
            QMessageBox::warning(this, "标签已存在",
                                 QString("标签 \"%1\" 已存在，请使用其他名称。").arg(newTag));
            return;
        }
    }

    tagComboBox->addItem(newTag);
    tagComboBox->setCurrentIndex(tagComboBox->count() - 1);
    saveTagsToSettings();

    if (logger) {
        logger->addMessage(QString("添加新标签: %1").arg(newTag));
    }
}

void ClientWindow::onManageTags()
{
    if (!tagComboBox) return;

    QDialog dialog(this);
    dialog.setWindowTitle("标签管理");
    dialog.setFixedSize(350, 400);
    dialog.setModal(true);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QLabel *tipLabel = new QLabel("双击标签可编辑名称，选中后点击\"删除\"移除", &dialog);
    tipLabel->setStyleSheet("color: #666; font-size: 11px;");
    layout->addWidget(tipLabel);

    QListWidget *listWidget = new QListWidget(&dialog);
    for (int i = 0; i < tagComboBox->count(); ++i) {
        listWidget->addItem(tagComboBox->itemText(i));
    }
    layout->addWidget(listWidget);

    // 双击编辑
    listWidget->connect(listWidget, &QListWidget::itemDoubleClicked, [listWidget](QListWidgetItem *item) {
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        listWidget->editItem(item);
    });

    QHBoxLayout *buttonLayout = new QHBoxLayout();

    QPushButton *deleteButton = new QPushButton("删除选中", &dialog);
    deleteButton->setStyleSheet("QPushButton { background-color: #f44336; color: white; border: none; padding: 6px 12px; border-radius: 4px; font-size: 12px; } QPushButton:hover { background-color: #EF5350; }");
    connect(deleteButton, &QPushButton::clicked, this, [this, &dialog, listWidget]() {
        QListWidgetItem *current = listWidget->currentItem();
        if (!current) {
            QMessageBox::information(&dialog, "提示", "请先选择要删除的标签。");
            return;
        }
        QString tagToDelete = current->text();

        if (listWidget->count() <= 1) {
            QMessageBox::warning(&dialog, "无法删除", "至少需要保留一个标签。");
            return;
        }

        QMessageBox::StandardButton result = QMessageBox::question(
            &dialog, "确认删除",
            QString("确定要删除标签 \"%1\" 吗？").arg(tagToDelete),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (result == QMessageBox::Yes) {
            delete listWidget->takeItem(listWidget->row(current));
            if (logger) {
                logger->addMessage(QString("删除标签: %1").arg(tagToDelete));
            }
        }
    });

    QPushButton *saveButton = new QPushButton("保存", &dialog);
    saveButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; border: none; padding: 6px 12px; border-radius: 4px; font-size: 12px; } QPushButton:hover { background-color: #66BB6A; }");
    connect(saveButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    QPushButton *cancelBtn = new QPushButton("取消", &dialog);
    cancelBtn->setStyleSheet("QPushButton { background-color: #607D8B; color: white; border: none; padding: 6px 12px; border-radius: 4px; font-size: 12px; }");
    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    buttonLayout->addWidget(deleteButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(cancelBtn);
    buttonLayout->addWidget(saveButton);
    layout->addLayout(buttonLayout);

    if (dialog.exec() == QDialog::Accepted) {
        // 更新下拉框
        tagComboBox->clear();
        for (int i = 0; i < listWidget->count(); ++i) {
            QString tag = listWidget->item(i)->text().trimmed();
            if (!tag.isEmpty()) {
                tagComboBox->addItem(tag);
            }
        }

        if (tagComboBox->count() == 0) {
            tagComboBox->addItem("唐贵宝1");
        }

        saveTagsToSettings();

        if (logger) {
            logger->addMessage("标签列表已更新");
        }
    }
}

// 统一控制
void ClientWindow::onUnifiedControl()
{
    if (!unifiedControlManager) return;

    QList<PeerInfo> peers = unifiedControlManager->getPeerList();

    UnifiedControlDialog dialog(peers, unifiedControlManager->currentTag(), this);
    connect(&dialog, &UnifiedControlDialog::executeCommand,
            this, [this](const QString &command, const QStringList &params) {
        if (unifiedControlManager) {
            unifiedControlManager->sendCommand(command, params);
        }
    });

    dialog.exec();
}

// 处理收到的统一控制命令
void ClientWindow::onUnifiedCommandReceived(const QString &command, const QStringList &params, const QString &senderIP)
{
    if (logger) {
        logger->addMessage(QString("[统一控制] 收到来自 %1 的命令: %2").arg(senderIP).arg(command));
    }

    if (command == "SHUTDOWN") {
        if (logger) logger->addMessage("[统一控制] 执行关机...");
        SystemController::shutdown();
    } else if (command == "REBOOT") {
        if (logger) logger->addMessage("[统一控制] 执行重启...");
        SystemController::reboot();
    } else if (command == "SUSPEND") {
        if (logger) logger->addMessage("[统一控制] 执行睡眠...");
        SystemController::suspend();
    } else if (command == "HIBERNATE") {
        if (logger) logger->addMessage("[统一控制] 执行休眠...");
        SystemController::hibernate();
    } else if (command == "CONNECT_SERVER") {
        if (serverConnector && serverCheckBox) {
            if (serverConnector->isConnected()) {
                if (logger) logger->addMessage("[统一控制] 已连接服务器，跳过");
            } else {
                if (logger) logger->addMessage("[统一控制] 连接服务器...");
                QString serverIP = params.isEmpty() ? "192.168.1.100" : params[0];
                serverCheckBox->blockSignals(true);
                serverCheckBox->setChecked(true);
                serverCheckBox->blockSignals(false);
                serverConnector->connectToServer(serverIP);
            }
        }
    } else if (command == "AUTO_START") {
        if (logger) logger->addMessage("[统一控制] 设置开机启动...");
        if (autoStartManager) {
            if (autoStartCheckBox && !autoStartCheckBox->isChecked()) {
                autoStartCheckBox->setChecked(true);
            } else if (autoStartCheckBox) {
                // 已经开启则跳过
                if (logger) logger->addMessage("[统一控制] 开机启动已开启，跳过");
            }
        }
    } else if (command == "ALWAYS_ON_TOP") {
        if (logger) logger->addMessage("[统一控制] 设置窗口置顶...");
        toggleAlwaysOnTop(true);
        show();
    } else if (command == "NETWORK_SPEED_TEST") {
        if (logger) logger->addMessage("[统一控制] 执行网络测速...");
        if (basicTester) {
            basicTester->executeTestByName("网络测速");
        }
    } else if (command == "COM_TEST") {
        if (logger) logger->addMessage("[统一控制] 执行COM测试...");
        if (basicTester) {
            basicTester->executeTestByName("COM测试");
        }
    } else if (command == "AGING_TEST") {
        if (logger) logger->addMessage("[统一控制] 执行老化测试(自动模式)...");
        if (basicTester) {
            basicTester->startAgingTestAuto();
        }
    } else if (command == "STOP_AGING_TEST") {
        if (logger) logger->addMessage("[统一控制] 停止老化测试...");
        if (basicTester) {
            basicTester->stopAgingTest();
        }
    } else if (command == "START_WAKE_ON_LAN") {
        if (logger) logger->addMessage("[统一控制] 启动网络唤醒...");
        if (wakeOnLan && serverConnector) {
            if (serverConnector->isConnected()) {
                wakeOnLan->startWakeOnLanAuto();
            } else {
                if (logger) logger->addMessage("[统一控制] 等待服务器连接...");
                QTimer *timer = new QTimer(this);
                timer->setSingleShot(false);
                timer->setInterval(500);
                int retries = 0;
                const int maxRetries = 20; // 最多等10秒
                connect(timer, &QTimer::timeout, this, [=]() mutable {
                    retries++;
                    if (serverConnector->isConnected()) {
                        timer->stop();
                        timer->deleteLater();
                        wakeOnLan->startWakeOnLanAuto();
                    } else if (retries >= maxRetries) {
                        timer->stop();
                        timer->deleteLater();
                        if (logger) logger->addMessage("[统一控制] 等待服务器连接超时，启动唤醒失败");
                    }
                });
                timer->start();
            }
        }
    } else if (command == "STOP_WAKE_ON_LAN") {
        if (logger) logger->addMessage("[统一控制] 取消网络唤醒...");
        if (wakeOnLan && serverConnector) {
            if (serverConnector->isConnected()) {
                wakeOnLan->cancelWakeOnLanAuto();
            } else {
                if (logger) logger->addMessage("[统一控制] 等待服务器连接...");
                QTimer *timer = new QTimer(this);
                timer->setSingleShot(false);
                timer->setInterval(500);
                int retries = 0;
                const int maxRetries = 20;
                connect(timer, &QTimer::timeout, this, [=]() mutable {
                    retries++;
                    if (serverConnector->isConnected()) {
                        timer->stop();
                        timer->deleteLater();
                        wakeOnLan->cancelWakeOnLanAuto();
                    } else if (retries >= maxRetries) {
                        timer->stop();
                        timer->deleteLater();
                        if (logger) logger->addMessage("[统一控制] 等待服务器连接超时，取消唤醒失败");
                    }
                });
                timer->start();
            }
        }
    } else if (command == "DEVICE_DETECT") {
        if (logger) logger->addMessage("[统一控制] 执行设备检测...");
        if (deviceManager) {
            deviceManager->startDetection();
        }
    } else if (command == "CLEAR_LOGS") {
        if (logger) logger->addMessage("[统一控制] 清除日志...");
        if (logger && logTextEdit) {
            logTextEdit->clear();
        }
    } else if (command == "RELOAD_DEVICE") {
        if (logger) logger->addMessage("[统一控制] 重新导入设备...");
        if (deviceInfoEdit) {
            deviceInfoEdit->clear();
            QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
            deviceInfoEdit->append(QString("[%1] 重新导入设备信息...").arg(timestamp));
        }
        if (deviceManager) {
            deviceManager->startDetection();
        }
    } else if (command == "CLEAR_ILLEGAL_SHUTDOWN") {
        if (logger) logger->addMessage("[统一控制] 清除非法关机提示...");
        onClearIllegalShutdown();
    } else if (command == "START_CYCLE") {
        if (params.size() >= 3 && cycleController) {
            CycleOperation op = static_cast<CycleOperation>(params[0].toInt());
            int interval = params[1].toInt();
            int count = params[2].toInt();
            if (logger) {
                logger->addMessage(QString("[统一控制] 开始循环: 操作=%1, 间隔=%2秒, 次数=%3")
                                  .arg(static_cast<int>(op)).arg(interval).arg(count));
            }
            cycleController->startCycleWithConfig(op, interval, count);
        }
    } else if (command == "STOP_CYCLE") {
        if (logger) logger->addMessage("[统一控制] 停止循环...");
        if (cycleController && cycleController->isRunning()) {
            cycleController->stopCycle();
        }
    }
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

        show();
        activateWindow();
        raise();

        // 利用延时器窗体打开后0.5s开始设备监控
        QTimer::singleShot(500,deviceManager,&DeviceManager::startDetection);

        // 确保分割器对半分
        QSplitter *splitter = findChild<QSplitter*>();
        if (splitter) {
            int totalWidth = splitter->width();
            QList<int> sizes;
            sizes << totalWidth / 2 << totalWidth / 2;
            splitter->setSizes(sizes);
        }

        // 额外的检查：如果非法关机次数为0，确保按钮隐藏
        if (illegalShutdownDetector && illegalShutdownDetector->getIllegalShutdownCount() <= 0) {
            if (illegalShutdownButton) {
                illegalShutdownButton->setVisible(false);
                qDebug() << "启动时检查：非法关机次数为0，隐藏按钮";
            }
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

    // 标记正常关机 - 删除运行标志文件（下次启动时检测此文件是否存在）
    IllegalShutdownDetector::markCleanShutdown();

    // 同步文件系统，确保日志和状态文件已写入磁盘
    QProcess::execute("sync");

    QMainWindow::closeEvent(event);
}

// 设备相关槽函数
void ClientWindow::onDeviceChanged(const QString &message)
{
    // 设备变化事件显示到"设备实时信息"日志栏（而非操作日志）
    if (!deviceInfoEdit) return;

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    deviceInfoEdit->append(QString("<font color='#1976D2'>[%1]</font> %2").arg(timestamp, message));
}

void ClientWindow::onUsbDeviceChanged(const QString &action, const QString &deviceInfo)
{
    Q_UNUSED(action);
    Q_UNUSED(deviceInfo);
    // USB设备变化处理（已注释）
}

void ClientWindow::onInputDeviceChanged(const QString &action, const QString &deviceInfo)
{
    Q_UNUSED(action);
    Q_UNUSED(deviceInfo);
    // 输入设备变化处理（已注释）
}

void ClientWindow::onDisplayChanged(const QString &action, const QString &displayInfo)
{
    Q_UNUSED(action);
    Q_UNUSED(displayInfo);
    // 显示设备变化处理（已注释）
}

// 事件过滤器：处理 bootCountLabel 的双击事件（清零开机计数）
bool ClientWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == bootCountLabel && event->type() == QEvent::MouseButtonDblClick) {
        // 双击开机次数标签 - 直接清零（无确认对话框）
        // 关键: 记录当前系统真实启动次数作为基准，后续显示 = 真实次数 - 基准
        int currentActualCount = 0;

        // 获取当前系统真实启动次数
        QProcess process;
        process.start("sh", QStringList() << "-c" <<
                      "last reboot 2>/dev/null | grep -c 'reboot' || echo 0");
        if (process.waitForFinished(3000)) {
            QString output = process.readAllStandardOutput().trimmed();
            bool ok = false;
            int count = output.toInt(&ok);
            if (ok && count > 0) {
                currentActualCount = count;
            }
        }

        // 如果 last reboot 失败，尝试 journalctl
        if (currentActualCount == 0) {
            process.start("sh", QStringList() << "-c" <<
                          "journalctl --list-boots 2>/dev/null | wc -l");
            if (process.waitForFinished(3000)) {
                QString output = process.readAllStandardOutput().trimmed();
                bool ok = false;
                int count = output.toInt(&ok);
                if (ok && count > 0) {
                    currentActualCount = count;
                }
            }
        }

        // 如果都失败，使用当前 QSettings 中的值 + 基准作为当前真实次数
        if (currentActualCount == 0) {
            QSettings settings("YanSheng", "TestClient");
            int baseline = settings.value("boot_count_baseline", 0).toInt();
            int recorded = settings.value("system_boot_count", 0).toInt();
            currentActualCount = baseline + recorded;
        }

        // 记录当前真实次数作为清零基准
        QSettings settings("YanSheng", "TestClient");
        settings.setValue("boot_count_baseline", currentActualCount);
        settings.setValue("system_boot_count", 0);

        // 更新显示
        if (bootCountLabel) {
            bootCountLabel->setText("开机: 0");
        }

        if (logger) {
            logger->addMessage(QString("✓ 开机计数已清零（双击操作，基准: %1）").arg(currentActualCount));
        }

        qDebug() << "开机计数已清零，基准:" << currentActualCount;
        return true;  // 事件已处理
    }

    return QMainWindow::eventFilter(watched, event);
}

// 新增：获取系统累计启动次数（基于 btmp 日志）
// 使用 last reboot 命令统计系统启动次数，比"软件打开次数"更准确
// 支持双击清零：清零时记录当前真实次数作为基准，显示 = 真实次数 - 基准
int ClientWindow::getSystemBootCount()
{
    int actualBootCount = 0;  // 系统真实启动次数
    bool gotActualCount = false;

    // 方法1: 通过 last reboot 命令统计（最可靠）
    QProcess process;
    process.start("sh", QStringList() << "-c" <<
                  "last reboot 2>/dev/null | grep -c 'reboot' || echo 0");
    if (process.waitForFinished(3000)) {
        QString output = process.readAllStandardOutput().trimmed();
        bool ok = false;
        int count = output.toInt(&ok);
        if (ok && count > 0) {
            actualBootCount = count;
            gotActualCount = true;
            qDebug() << "通过 last reboot 获取系统启动次数:" << actualBootCount;
        }
    }

    // 方法2: 通过 journalctl 统计启动次数
    if (!gotActualCount) {
        process.start("sh", QStringList() << "-c" <<
                      "journalctl --list-boots 2>/dev/null | wc -l");
        if (process.waitForFinished(3000)) {
            QString output = process.readAllStandardOutput().trimmed();
            bool ok = false;
            int count = output.toInt(&ok);
            if (ok && count > 0) {
                actualBootCount = count;
                gotActualCount = true;
                qDebug() << "通过 journalctl --list-boots 获取系统启动次数:" << actualBootCount;
            }
        }
    }

    // 读取清零基准（双击清零时记录的真实次数）
    QSettings settings("YanSheng", "TestClient");
    int bootCountBaseline = settings.value("boot_count_baseline", 0).toInt();

    if (gotActualCount) {
        // 计算显示值 = 真实次数 - 基准
        int displayCount = actualBootCount - bootCountBaseline;
        if (displayCount < 0) displayCount = 0;  // 防止负数

        // 同时更新 system_boot_count 用于兼容
        settings.setValue("system_boot_count", displayCount);

        qDebug() << "系统真实启动次数:" << actualBootCount
                 << "清零基准:" << bootCountBaseline
                 << "显示次数:" << displayCount;
        return displayCount;
    }

    // 方法3: 回退方案 - 使用 QSettings 记录的次数 + 1
    int recordedCount = settings.value("system_boot_count", 0).toInt() + 1;
    settings.setValue("system_boot_count", recordedCount);
    qDebug() << "使用回退方案记录系统启动次数:" << recordedCount;
    return recordedCount;
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

void ClientWindow::showDeviceChangeLog()
{
    QDialog *dialog = new QDialog(this);
    dialog->setWindowTitle("设备变化日志");
    dialog->setMinimumSize(750, 500);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout *mainLayout = new QVBoxLayout(dialog);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    // === 查询区域 ===
    QGroupBox *filterGroup = new QGroupBox("日志查询", dialog);
    QHBoxLayout *filterLayout = new QHBoxLayout(filterGroup);
    filterLayout->setContentsMargins(8, 6, 8, 6);
    filterLayout->setSpacing(8);

    // 开始时间
    QLabel *startLabel = new QLabel("开始时间:", dialog);
    startLabel->setStyleSheet("font-size: 12px; font-weight: bold;");
    QDateTimeEdit *startEdit = new QDateTimeEdit(dialog);
    startEdit->setCalendarPopup(true);
    startEdit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    startEdit->setDateTime(QDateTime::currentDateTime().addDays(-1));
    startEdit->setFixedWidth(170);

    // 结束时间
    QLabel *endLabel = new QLabel("结束时间:", dialog);
    endLabel->setStyleSheet("font-size: 12px; font-weight: bold;");
    QDateTimeEdit *endEdit = new QDateTimeEdit(dialog);
    endEdit->setCalendarPopup(true);
    endEdit->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    endEdit->setDateTime(QDateTime::currentDateTime());
    endEdit->setFixedWidth(170);

    // 关键词
    QLabel *keywordLabel = new QLabel("关键词:", dialog);
    keywordLabel->setStyleSheet("font-size: 12px; font-weight: bold;");
    QLineEdit *keywordEdit = new QLineEdit(dialog);
    keywordEdit->setPlaceholderText("输入关键词过滤日志");
    keywordEdit->setFixedWidth(150);

    // 查询按钮
    QPushButton *searchBtn = new QPushButton("查询", dialog);
    searchBtn->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; border: none; padding: 4px 16px; border-radius: 3px; font-size: 12px; }");
    searchBtn->setFixedWidth(60);
    searchBtn->setFixedHeight(26);

    // 刷新按钮
    QPushButton *refreshBtn = new QPushButton("刷新", dialog);
    refreshBtn->setStyleSheet("QPushButton { background-color: #2196F3; color: white; border: none; padding: 4px 16px; border-radius: 3px; font-size: 12px; }");
    refreshBtn->setFixedWidth(60);
    refreshBtn->setFixedHeight(26);

    filterLayout->addWidget(startLabel);
    filterLayout->addWidget(startEdit);
    filterLayout->addWidget(endLabel);
    filterLayout->addWidget(endEdit);
    filterLayout->addWidget(keywordLabel);
    filterLayout->addWidget(keywordEdit);
    filterLayout->addStretch();
    filterLayout->addWidget(searchBtn);
    filterLayout->addWidget(refreshBtn);

    mainLayout->addWidget(filterGroup);

    // === 日志显示区域 ===
    QTextEdit *logBrowser = new QTextEdit(dialog);
    logBrowser->setReadOnly(true);
    logBrowser->setStyleSheet("QTextEdit { font-size: 12px; font-family: 'Courier New', monospace; background: #FAFAFA; border: 1px solid #ddd; border-radius: 3px; }");

    mainLayout->addWidget(logBrowser, 1);

    // === 底部状态栏 ===
    QLabel *statusLabel = new QLabel(dialog);
    statusLabel->setStyleSheet("QLabel { color: #666; font-size: 11px; background: #f5f5f5; padding: 3px 8px; border-radius: 2px; border: 1px solid #e0e0e0; }");
    mainLayout->addWidget(statusLabel);

    // 日志文件路径
    QString logFilePath = QDir::homePath() + "/.monitor_log/deviceChange.log";

    // 加载日志内容的lambda
    auto loadLog = [=]() {
        QFile file(logFilePath);
        if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            logBrowser->setHtml("<font color='gray'>暂无日志记录</font>");
            statusLabel->setText("日志文件不存在");
            return;
        }

        QDateTime startTime = startEdit->dateTime();
        QDateTime endTime = endEdit->dateTime();
        QString keyword = keywordEdit->text().trimmed();

        QTextStream in(&file);
        int matchCount = 0;
        int totalCount = 0;
        QString htmlOutput;

        while (!in.atEnd()) {
            QString line = in.readLine();
            totalCount++;

            if (line.trimmed().isEmpty()) continue;

            // 提取日志中的时间戳: [2026-03-31 00:00:00]
            QDateTime lineTime;
            QRegularExpression timeRe(R"(\[(\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}:\d{2})\])");
            QRegularExpressionMatch match = timeRe.match(line);
            if (match.hasMatch()) {
                lineTime = QDateTime::fromString(match.captured(1), "yyyy-MM-dd hh:mm:ss");
            }

            // 时间范围过滤
            if (lineTime.isValid()) {
                if (lineTime < startTime || lineTime > endTime) continue;
            }

            // 关键词过滤
            if (!keyword.isEmpty() && !line.contains(keyword, Qt::CaseInsensitive)) continue;

            matchCount++;

            // 根据内容着色
            QString color = "#333";
            if (line.contains("移除设备")) color = "#f44336";
            else if (line.contains("设备恢复")) color = "#4CAF50";
            else if (line.contains("新增设备") || line.contains("设备插入")) color = "#2196F3";
            else if (line.contains("设备扫描完成")) color = "#9C27B0";
            else if (line.contains("===")) color = "#FF9800";

            htmlOutput += QString("<span style='color:%1;'>%2</span><br>").arg(color).arg(line.toHtmlEscaped());
        }

        file.close();

        if (htmlOutput.isEmpty()) {
            logBrowser->setHtml("<font color='gray'>未找到匹配的日志记录</font>");
        } else {
            logBrowser->setHtml(htmlOutput);
        }

        // 滚动到底部
        QScrollBar *scrollBar = logBrowser->verticalScrollBar();
        scrollBar->setValue(scrollBar->maximum());

        statusLabel->setText(QString("共 %1 条日志，匹配 %2 条").arg(totalCount).arg(matchCount));
    };

    // 连接查询按钮
    QObject::connect(searchBtn, &QPushButton::clicked, loadLog);

    // 连接刷新按钮（清除过滤条件后加载全部日志）
    QObject::connect(refreshBtn, &QPushButton::clicked, [=]() {
        keywordEdit->clear();
        startEdit->setDateTime(QDateTime::currentDateTime().addDays(-7));
        endEdit->setDateTime(QDateTime::currentDateTime());
        loadLog();
    });

    // 回车键查询
    QObject::connect(keywordEdit, &QLineEdit::returnPressed, loadLog);

    // 初始加载
    loadLog();

    dialog->exec();
}

// 新增：显示非法关机日志对话框
void ClientWindow::showIllegalShutdownLog()
{
    // 获取当前非法关机次数
    int count = illegalShutdownDetector ? illegalShutdownDetector->getIllegalShutdownCount() : 0;

    // 获取所有历史日志文件（按时间排序，从旧到新）
    QStringList logFiles;
    if (illegalShutdownDetector) {
        logFiles = illegalShutdownDetector->getAllShutdownLogFiles();
    }

    // 如果没有记录，不显示对话框
    if (count <= 0 && logFiles.isEmpty()) {
        QMessageBox::information(this, "提示", "当前没有非法关机记录。");
        return;
    }

    // 创建对话框
    QDialog *dialog = new QDialog(this);
    dialog->setWindowTitle("非法关机日志信息");
    dialog->setMinimumSize(750, 500);

    QVBoxLayout *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(6);

    // 顶部：标题信息（紧凑设计：图标 + 文字，高度小）
    QFrame *titleFrame = new QFrame(dialog);
    titleFrame->setFrameShape(QFrame::StyledPanel);
    titleFrame->setFixedHeight(32);  // 固定高度，使标题区域更紧凑
    titleFrame->setStyleSheet(
        "QFrame { background-color: #fff3e0; border: 1px solid #ff9800; "
        "border-radius: 4px; }"
    );

    QHBoxLayout *titleLayout = new QHBoxLayout(titleFrame);
    titleLayout->setContentsMargins(8, 0, 8, 0);
    titleLayout->setSpacing(6);

    QLabel *iconLabel = new QLabel("⚠", titleFrame);
    iconLabel->setStyleSheet("font-size: 13pt; color: #ff5722; border: none;");
    iconLabel->setFixedWidth(20);
    titleLayout->addWidget(iconLabel);

    QLabel *titleText = new QLabel(titleFrame);
    titleText->setText(QString("非法关机日志信息 - 累计 <b style='color:#FF5722;'>%1</b> 次").arg(count));
    titleText->setStyleSheet("font-size: 10pt; border: none;");
    titleLayout->addWidget(titleText);

    titleLayout->addStretch();

    layout->addWidget(titleFrame);

    // 中间：左侧历史列表 + 右侧详情（使用 QSplitter）
    QSplitter *splitter = new QSplitter(Qt::Horizontal, dialog);

    // 左侧：历史记录列表
    QListWidget *logListWidget = new QListWidget(splitter);
    logListWidget->setMinimumWidth(200);
    logListWidget->setMaximumWidth(300);
    logListWidget->setStyleSheet("QListWidget { font-size: 10pt; }");

    // 填充历史记录列表
    for (int i = 0; i < logFiles.size(); ++i) {
        QFileInfo fi(logFiles[i]);
        QString baseName = fi.baseName(); // illegal_shutdown_yyyyMMdd_HHmmss

        // 从文件名提取时间戳并格式化
        QString timeStr = baseName;
        timeStr.remove("illegal_shutdown_");
        // 格式: yyyyMMdd_HHmmss -> yyyy-MM-dd HH:mm:ss
        if (timeStr.length() >= 15) {
            timeStr = timeStr.mid(0, 4) + "-" + timeStr.mid(4, 2) + "-" + timeStr.mid(6, 2) + " " +
                      timeStr.mid(9, 2) + ":" + timeStr.mid(11, 2) + ":" + timeStr.mid(13, 2);
        }

        // 列表项显示：第N次 - 时间
        QString displayText = QString("第%1次 - %2").arg(i + 1).arg(timeStr);

        QListWidgetItem *item = new QListWidgetItem(displayText, logListWidget);
        item->setData(Qt::UserRole, logFiles[i]); // 存储文件路径
    }

    // 右侧：日志详情
    QTextBrowser *logBrowser = new QTextBrowser(splitter);
    logBrowser->setFont(QFont("Monospace", 10));
    logBrowser->setStyleSheet("background-color: #f8f9fa; border: 1px solid #dee2e6; border-radius: 4px;");

    splitter->addWidget(logListWidget);
    splitter->addWidget(logBrowser);
    splitter->setStretchFactor(0, 1); // 列表占比小
    splitter->setStretchFactor(1, 3); // 详情占比大

    layout->addWidget(splitter);

    // 点击列表项时加载对应文件内容
    connect(logListWidget, &QListWidget::currentRowChanged, [logBrowser, logListWidget](int row) {
        if (row < 0 || row >= logListWidget->count()) return;

        QListWidgetItem *item = logListWidget->item(row);
        QString filePath = item->data(Qt::UserRole).toString();

        QFile file(filePath);
        if (!file.exists()) {
            logBrowser->setPlainText("日志文件不存在: " + filePath +
                                    "\n\n可能该记录来自旧版本测试，文件已被删除或损坏。" +
                                    "\n建议点击「清除记录」清理历史数据后重新测试。");
            return;
        }

        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            // 直接用 QString::fromUtf8() 读取，与写入端 toUtf8() 对应
            QByteArray rawData = file.readAll();
            file.close();

            QString content = QString::fromUtf8(rawData);

            if (content.trimmed().isEmpty()) {
                logBrowser->setPlainText("日志文件为空: " + filePath +
                                        "\n\n可能该记录来自旧版本测试。\n建议点击「清除记录」清理历史数据后重新测试。");
            } else {
                logBrowser->setPlainText(content);
            }
        } else {
            logBrowser->setPlainText("无法读取日志文件: " + filePath +
                                    "\n错误: " + file.errorString());
        }
    });

    // 默认选中最新的一条（列表最后一项）
    if (logListWidget->count() > 0) {
        logListWidget->setCurrentRow(logListWidget->count() - 1);
    } else {
        logBrowser->setPlainText("未找到非法关机日志文件。");
    }

    // 按钮区域
    QDialogButtonBox *buttonBox = new QDialogButtonBox(dialog);
    QPushButton *recheckButton = new QPushButton("重新检测", dialog);
    QPushButton *clearButton = new QPushButton("清除记录", dialog);
    QPushButton *closeButton = new QPushButton("关闭", dialog);

    recheckButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; padding: 8px 16px; border-radius: 4px; }");
    clearButton->setStyleSheet("QPushButton { background-color: #FF9800; color: white; padding: 8px 16px; border-radius: 4px; }");
    closeButton->setStyleSheet("QPushButton { background-color: #607D8B; color: white; padding: 8px 16px; border-radius: 4px; }");

    buttonBox->addButton(recheckButton, QDialogButtonBox::ActionRole);
    buttonBox->addButton(clearButton, QDialogButtonBox::ActionRole);
    buttonBox->addButton(closeButton, QDialogButtonBox::RejectRole);

    layout->addWidget(buttonBox);

    // 重新检测按钮
    connect(recheckButton, &QPushButton::clicked, this, [this, dialog, logListWidget, logBrowser, titleText]() {
        if (illegalShutdownDetector) {
            illegalShutdownDetector->clearIllegalShutdownState();
            illegalShutdownDetector->resetIllegalShutdownCount();

            QDateTime now = QDateTime::currentDateTime();
            illegalShutdownDetector->setLastCheckTime(now);
            illegalShutdownDetector->saveCheckTime();

            qDebug() << "重新检测：设置检测时间为" << now.toString("yyyy-MM-dd HH:mm:ss");
        }

        lastIllegalShutdownLog.clear();
        lastShutdownTime = QDateTime();
        lastCheckTime = QDateTime::currentDateTime();

        if (illegalShutdownButton) {
            illegalShutdownButton->setVisible(false);
        }

        // 清空列表和详情
        logListWidget->clear();
        logBrowser->setPlainText("已清除所有非法关机记录。\n\n"
                                "非法关机次数已清零：0 次\n"
                                "新的检测时间点: " + lastCheckTime.toString("yyyy-MM-dd HH:mm:ss") + "\n\n"
                                "在此时间点之后检测到的非法关机才会显示。");
        titleText->setText(QString("非法关机日志信息 - 累计 <b style='color:#FF5722;'>0</b> 次"));

        QString message = QString("重新检测完成！\n\n"
                                 "已清除所有非法关机记录，重新开始计数。\n"
                                 "新的检测时间点: %1\n"
                                 "在此时间点之后的非法关机事件才会被检测。")
                                 .arg(lastCheckTime.toString("yyyy-MM-dd HH:mm:ss"));

        QMessageBox::information(dialog, "重新检测", message);

        if (logger) {
            logger->addMessage("重新检测非法关机，时间点: " +
                              lastCheckTime.toString("yyyy-MM-dd HH:mm:ss") +
                              "，所有记录已清除");
        }
    });

    // 清除记录按钮
    connect(clearButton, &QPushButton::clicked, this, [this, dialog, logListWidget, logBrowser, titleText]() {
        lastIllegalShutdownLog.clear();
        lastShutdownTime = QDateTime();
        lastCheckTime = QDateTime::currentDateTime();

        if (illegalShutdownDetector) {
            illegalShutdownDetector->clearIllegalShutdownState();
            illegalShutdownDetector->resetIllegalShutdownCount();
            illegalShutdownDetector->setLastCheckTime(lastCheckTime);
            illegalShutdownDetector->saveCheckTime();
        }

        if (illegalShutdownButton) {
            illegalShutdownButton->setVisible(false);
        }

        // 清空列表和详情
        logListWidget->clear();
        logBrowser->setPlainText("已清除所有非法关机记录。\n\n"
                                "非法关机次数已清零：0 次\n"
                                "检测时间点已重置为: " + lastCheckTime.toString("yyyy-MM-dd HH:mm:ss"));
        titleText->setText(QString("非法关机日志信息 - 累计 <b style='color:#FF5722;'>0</b> 次"));

        if (logger) {
            logger->addMessage("清除所有非法关机记录，次数已清零");
        }
    });

    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);

    dialog->exec();
    dialog->deleteLater();
}

// 新增：重新检测非法关机
void ClientWindow::onRecheckIllegalShutdown()
{
    if (!illegalShutdownDetector) {
        return;
    }

    // 执行重新检测
    illegalShutdownDetector->checkOnStartup();

    // 记录日志
    if (logger) {
        logger->addMessage("重新检测非法关机");
    }
}

// 新增：清除非法关机记录
void ClientWindow::onClearIllegalShutdown()
{
    if (!illegalShutdownDetector) {
        return;
    }

    // 清除持久化状态
    illegalShutdownDetector->clearIllegalShutdownState();

    // 重置非法关机次数
    illegalShutdownDetector->resetIllegalShutdownCount();

    // 隐藏提示按钮
    if (illegalShutdownButton) {
        illegalShutdownButton->setVisible(false);
    }

    // 清除本地记录
    lastIllegalShutdownLog.clear();
    lastShutdownTime = QDateTime();

    // 记录日志
    if (logger) {
        logger->addMessage("清除所有非法关机记录，次数已清零");
    }

    // 显示提示信息
    QMessageBox::information(this, "清除成功", "已清除所有非法关机记录，非法关机次数已清零。");
}
