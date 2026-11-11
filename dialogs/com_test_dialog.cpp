#include "com_test_dialog.h"
#include "../core/privilege_helper.h"
#include <QCoreApplication>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QDateTime>
#include <QTextStream>
#include <QProcess>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QRegularExpression>
#include <QFileDialog>
#include <QSettings>
#include <QStandardPaths>

#ifdef QT_SERIALPORT_LIB
#include <QSerialPortInfo>
#endif

// 统一按钮样式 - 与软件其他按钮一致
static const char* BUTTON_STYLE_GREEN =
    "QPushButton { background-color: #4CAF50; color: white; border: none; "
    "padding: 4px 12px; border-radius: 3px; font-size: 12px; font-weight: bold; }"
    "QPushButton:hover { background-color: #66BB6A; }"
    "QPushButton:disabled { background-color: #BDBDBD; }";

static const char* BUTTON_STYLE_BLUE =
    "QPushButton { background-color: #2196F3; color: white; border: none; "
    "padding: 4px 12px; border-radius: 3px; font-size: 12px; }"
    "QPushButton:hover { background-color: #42A5F5; }"
    "QPushButton:disabled { background-color: #BDBDBD; }";

static const char* BUTTON_STYLE_ORANGE =
    "QPushButton { background-color: #FF9800; color: white; border: none; "
    "padding: 4px 12px; border-radius: 3px; font-size: 12px; }"
    "QPushButton:hover { background-color: #FFB74D; }";

static const char* BUTTON_STYLE_GRAY =
    "QPushButton { background-color: #607D8B; color: white; border: none; "
    "padding: 4px 12px; border-radius: 3px; font-size: 12px; }"
    "QPushButton:hover { background-color: #78909C; }";

ComTestDialog::ComTestDialog(QWidget *parent)
    : QDialog(parent)
    , txByteCount(0), rxByteCount(0)
#ifdef QT_SERIALPORT_LIB
    , serialPort(nullptr)
#endif
    , isConnected(false), hexDisplayMode(false), hexSendMode(false)
{
    setupUI();

    // [2026-10-29] 修复:长时间循环收发后界面卡死(5窗口收发约28~29MB后全部冻结)
    // 根因:接收窗口(QTextEdit)文档无限增长——每收/发一次追加一行带时间戳
    // 的记录且从不裁剪,28MB 量级时每窗口已积累约 18 万行文本块,每次 append
    // +滚动到底都触发全文重布局,单次耗时几十~几百毫秒,UI 事件循环被拖死;
    // 5 个窗口共用同一 UI 线程,因此一起卡死(且内存暴涨触发换页)。
    // 修复:设 maximumBlockCount 环形上限——超过 2000 行自动丢弃最旧行,
    // 窗口文档恒定在有限大小,追加/滚动代价恒定,界面永久流畅。
    // 累计收发量由 TX/RX 计数标签(txByteCount/rxByteCount)独立统计,不受影响。
    receiveDataEdit->document()->setMaximumBlockCount(2000);

    // 设置窗口属性（支持多个实例同时运行，且各窗口互相独立）
    // [2026-11-05] 修复：主界面最小化时，所有 COM 测试窗口被一起最小化。
    // 根因：QDialog 默认是 Qt::Dialog 窗口类型，即使不传 parent，Qt 在 X11 下
    //   仍会为它写入 WM_TRANSIENT_FOR（指向应用的 WM_CLIENT_LEADER）并标记
    //   _NET_WM_WINDOW_TYPE_DIALOG；麒麟/统信的 KWin/UKUI 系窗口管理器把这类
    //   窗口当作主窗口的附属对话框组，最小化主窗口时整组一起最小化，
    //   于是 N 个 COM 界面全部消失（代码里并没有 hide 它们）。
    // 修复：改为 Qt::Window 普通独立顶层窗口——无 WM_TRANSIENT_FOR、
    //   窗口类型为 NORMAL，窗口管理器不再把它们与主窗口编组。
    // 注意：必须用覆盖式 setWindowFlags(...)，不能用 windowFlags() | ...，
    //   否则原有的 Qt::Dialog 位不会被清除，此处改动等于无效。
    setWindowModality(Qt::NonModal);
    setWindowFlags(Qt::Window | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                   Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);

#ifdef QT_SERIALPORT_LIB
    serialPort = new QSerialPort(this);
    serialPort->setReadBufferSize(4096);

    connect(serialPort, &QSerialPort::readyRead, this, &ComTestDialog::onReadyRead);

    #if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
    connect(serialPort, QOverload<QSerialPort::SerialPortError>::of(&QSerialPort::error),
            this, &ComTestDialog::onSerialPortError);
    #else
    connect(serialPort, SIGNAL(error(QSerialPort::SerialPortError)),
            this, SLOT(onSerialPortError(QSerialPort::SerialPortError)));
    #endif
#endif

    // 创建定时发送定时器
    autoSendTimer = new QTimer(this);
    connect(autoSendTimer, &QTimer::timeout, this, &ComTestDialog::onAutoSendTimer);

    setWindowTitle("COM端口测试 - 研盛测试客户端");
    setMinimumSize(750, 600);
    receiveBuffer.clear();

    // 延迟执行权限检查和串口检测
    QTimer::singleShot(100, this, &ComTestDialog::initializeSerialPort);
}

// 新增：初始化串口方法
void ComTestDialog::initializeSerialPort()
{
#ifdef QT_SERIALPORT_LIB
    // 首先检查是否有可用的串口设备
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();

    if (ports.isEmpty()) {
        // 没有检测到串口设备 - 延迟重试（可能需要时间识别）
        comPortCombo->clear();
        comPortCombo->addItem("正在扫描串口...");
        comPortCombo->setEnabled(false);
        connectButton->setEnabled(false);
        receiveDataEdit->append("正在扫描串口设备...");

        // 延迟2秒后再次尝试刷新串口列表
        QTimer::singleShot(2000, [this]() {
#ifdef QT_SERIALPORT_LIB
            QList<QSerialPortInfo> retryPorts = QSerialPortInfo::availablePorts();
            if (retryPorts.isEmpty()) {
                comPortCombo->clear();
                comPortCombo->addItem("无可用串口设备");
                receiveDataEdit->append("未检测到串口设备。");
                receiveDataEdit->append("请检查：");
                receiveDataEdit->append("1. 串口设备是否正确连接");
                receiveDataEdit->append("2. 系统是否识别到串口设备");
            } else {
                // 检查权限并刷新列表
                checkAndRefreshPortList(retryPorts);
            }
#endif
        });
        return;
    } else {
        // 有串口设备，检查权限并刷新列表
        checkAndRefreshPortList(ports);
    }
#else
    comPortCombo->addItem("串口功能不可用");
    comPortCombo->setEnabled(false);
    connectButton->setEnabled(false);
    receiveDataEdit->append("COM测试功能在当前系统中不可用。");
#endif
}

#ifdef QT_SERIALPORT_LIB
// 新增：检查权限并刷新串口列表
void ComTestDialog::checkAndRefreshPortList(const QList<QSerialPortInfo> &ports)
{
    // 检查串口权限
    bool hasPermissionIssue = false;
    QStringList problemPorts;

    for (const QSerialPortInfo &port : ports) {
        QFileInfo deviceInfo(port.systemLocation());
        if (!deviceInfo.isReadable() || !deviceInfo.isWritable()) {
            hasPermissionIssue = true;
            problemPorts << port.systemLocation();
        }
    }

    if (hasPermissionIssue) {
        // 显示权限问题提示
        receiveDataEdit->append("检测到串口权限问题，可能影响COM测试功能。");
        receiveDataEdit->append("正在检查权限...");

        // 询问用户是否修复权限
        QMessageBox::StandardButton result = QMessageBox::question(
            this,
            "串口权限问题",
            QString("检测到 %1 个串口设备存在权限限制。\n\n"
                   "这会影响COM测试功能，是否立即修复？")
                   .arg(problemPorts.size()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes
        );

        if (result == QMessageBox::Yes) {
            // 修复权限
            setupSerialPortPermissions(problemPorts);

            // 权限修复完成后，延迟刷新串口列表
            QTimer::singleShot(1000, [this, ports]() {
                refreshSerialPortList(ports, QStringList());
            });
        } else {
            receiveDataEdit->append("用户选择不修复权限，部分串口可能无法使用。");
            // 即使有权限问题，也显示串口列表（标记权限不足）
            refreshSerialPortList(ports, problemPorts);
        }
    } else {
        // 权限正常，直接刷新串口列表
        refreshSerialPortList(ports, QStringList());
    }
}

void ComTestDialog::refreshSerialPortList(const QList<QSerialPortInfo> &ports, const QStringList &problemPorts)
{
    comPortCombo->clear();

    if (ports.isEmpty()) {
        comPortCombo->addItem("无可用串口");
        comPortCombo->setEnabled(false);
        connectButton->setEnabled(false);
        return;
    }

    bool hasAvailablePort = false;

    for (const QSerialPortInfo &port : ports) {
        QString portName = port.portName();
        QString description = port.description();

        // 检查权限问题
        QFileInfo deviceInfo(port.systemLocation());
        bool hasPermission = deviceInfo.isReadable() && deviceInfo.isWritable();

        // 如果提供了problemPorts列表，使用它来标记
        bool isProblemPort = problemPorts.contains(port.systemLocation());

        QString displayText;
        if (hasPermission && !isProblemPort) {
            displayText = QString("%1 - %2").arg(portName).arg(description.isEmpty() ? "串口设备" : description);
            hasAvailablePort = true;
        } else {
            displayText = QString("%1 - %2 (权限不足)").arg(portName).arg(description.isEmpty() ? "串口设备" : description);
        }

        comPortCombo->addItem(displayText, portName);
    }

    comPortCombo->setEnabled(true);
    connectButton->setEnabled(hasAvailablePort);

    if (hasAvailablePort) {
        receiveDataEdit->append(QString("已检测到 %1 个串口设备").arg(ports.size()));
        receiveDataEdit->append("请选择COM端口进行连接...");
    } else {
        receiveDataEdit->append("所有串口设备都存在权限问题，无法使用。");
        receiveDataEdit->append("请修复权限或使用管理员权限运行程序。");
    }
}
#endif

ComTestDialog::~ComTestDialog()
{
#ifdef QT_SERIALPORT_LIB
    if (serialPort && serialPort->isOpen()) {
        serialPort->close();
    }
#endif
}

void ComTestDialog::setupUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(6);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    // ===================================================
    // 第一区域：串口参数配置（端口、波特率、数据位等）
    // ===================================================
    QGroupBox *configGroup = new QGroupBox("串口配置", this);
    QGridLayout *configLayout = new QGridLayout(configGroup);
    configLayout->setSpacing(6);

    // 第1行: 串口 | 波特率 | 数据位 | 停止位 | 校验位 | 流控制
    QLabel *portLabel = new QLabel("串口号:", this);
    comPortCombo = new QComboBox(this);
    comPortCombo->setMinimumWidth(90);
    comPortCombo->setMaximumHeight(25);

    QLabel *baudLabel = new QLabel("波特率:", this);
    baudRateCombo = new QComboBox(this);
    baudRateCombo->addItems({"1200", "2400", "4800", "9600", "14400", "19200",
                             "38400", "57600", "115200", "230400", "460800", "921600"});
    baudRateCombo->setCurrentText("9600");
    baudRateCombo->setMaximumHeight(25);

    QLabel *dataBitsLabel = new QLabel("数据位:", this);
    dataBitsCombo = new QComboBox(this);
    dataBitsCombo->addItems({"5", "6", "7", "8"});
    dataBitsCombo->setCurrentText("8");
    dataBitsCombo->setMaximumHeight(25);

    QLabel *stopBitsLabel = new QLabel("停止位:", this);
    stopBitsCombo = new QComboBox(this);
    stopBitsCombo->addItems({"1", "1.5", "2"});
    stopBitsCombo->setCurrentText("1");
    stopBitsCombo->setMaximumHeight(25);

    QLabel *parityLabel = new QLabel("校验位:", this);
    parityCombo = new QComboBox(this);
    parityCombo->addItems({"None", "Odd", "Even", "Mark", "Space"});
    parityCombo->setCurrentText("None");
    parityCombo->setMaximumHeight(25);

    QLabel *flowLabel = new QLabel("流控:", this);
    flowControlCombo = new QComboBox(this);
    flowControlCombo->addItems({"None", "RTS/CTS", "XON/XOFF"});
    flowControlCombo->setCurrentText("None");
    flowControlCombo->setMaximumHeight(25);

    configLayout->addWidget(portLabel, 0, 0);
    configLayout->addWidget(comPortCombo, 0, 1);
    configLayout->addWidget(baudLabel, 0, 2);
    configLayout->addWidget(baudRateCombo, 0, 3);
    configLayout->addWidget(dataBitsLabel, 0, 4);
    configLayout->addWidget(dataBitsCombo, 0, 5);
    configLayout->addWidget(stopBitsLabel, 0, 6);
    configLayout->addWidget(stopBitsCombo, 0, 7);
    configLayout->addWidget(parityLabel, 0, 8);
    configLayout->addWidget(parityCombo, 0, 9);
    configLayout->addWidget(flowLabel, 0, 10);
    configLayout->addWidget(flowControlCombo, 0, 11);

    // 第2行: DTR | RTS | HEX显示 | HEX发送 | 连接按钮(跨列)
    dtrCheck = new QCheckBox("DTR", this);
    dtrCheck->setMaximumHeight(25);
    dtrCheck->setEnabled(false);
    rtsCheck = new QCheckBox("RTS", this);
    rtsCheck->setMaximumHeight(25);
    rtsCheck->setEnabled(false);

    hexDisplayCheck = new QCheckBox("HEX显示", this);
    hexDisplayCheck->setMaximumHeight(25);
    hexSendCheck = new QCheckBox("HEX发送", this);
    hexSendCheck->setMaximumHeight(25);

    connectButton = new QPushButton("打开串口", this);
    connectButton->setStyleSheet(BUTTON_STYLE_GREEN);
    connectButton->setMaximumHeight(25);

    configLayout->addWidget(dtrCheck, 1, 0);
    configLayout->addWidget(rtsCheck, 1, 2);
    configLayout->addWidget(hexDisplayCheck, 1, 3, 1, 3);
    configLayout->addWidget(hexSendCheck, 1, 6, 1, 3);
    configLayout->addWidget(connectButton, 1, 9, 1, 3);

    mainLayout->addWidget(configGroup);

    // ===================================================
    // 第二区域：发送区域（输入框 + 定时发送）
    // ===================================================
    QGroupBox *sendGroup = new QGroupBox("发送数据", this);
    QVBoxLayout *sendLayout = new QVBoxLayout(sendGroup);
    sendLayout->setSpacing(6);

    // 输入框
    sendDataEdit = new QLineEdit(this);
    sendDataEdit->setPlaceholderText("输入要发送的数据（HEX发送模式时输入如：48 65 6C 6C 6F）");

    QHBoxLayout *sendRow = new QHBoxLayout();
    QLabel *sendInputLabel = new QLabel("字符串输入框:", this);
    sendRow->addWidget(sendInputLabel);
    sendRow->addWidget(sendDataEdit);

    sendLayout->addLayout(sendRow);

    // 定时发送区域
    QHBoxLayout *autoSendLayout = new QHBoxLayout();
    autoSendCheck = new QCheckBox("定时发送 ms", this);
    autoSendCheck->setMaximumHeight(25);
    autoSendCheck->setEnabled(false);

    autoSendIntervalSpin = new QSpinBox(this);
    autoSendIntervalSpin->setRange(50, 60000);
    autoSendIntervalSpin->setValue(1000);
    autoSendIntervalSpin->setSuffix(" ms");
    autoSendIntervalSpin->setMaximumHeight(25);
    autoSendIntervalSpin->setMaximumWidth(120);

    autoSendButton = new QPushButton("发送", this);
    autoSendButton->setStyleSheet(BUTTON_STYLE_BLUE);
    autoSendButton->setMaximumHeight(25);
    autoSendButton->setFixedWidth(70);
    autoSendButton->setEnabled(true);

    autoSendLayout->addWidget(autoSendCheck);
    autoSendLayout->addWidget(autoSendIntervalSpin);
    autoSendLayout->addWidget(autoSendButton);
    autoSendLayout->addStretch();

    sendLayout->addLayout(autoSendLayout);

    mainLayout->addWidget(sendGroup);

    // ===================================================
    // 第三区域：接收显示区
    // ===================================================
    QGroupBox *receiveGroup = new QGroupBox("接收数据", this);
    QVBoxLayout *receiveLayout = new QVBoxLayout(receiveGroup);
    receiveLayout->setSpacing(6);

    receiveDataEdit = new QTextEdit(this);
    receiveDataEdit->setReadOnly(true);
    receiveDataEdit->setMinimumHeight(200);
    receiveDataEdit->setStyleSheet("QTextEdit { background-color: #fafafa; border: 1px solid #ddd; }");

    receiveLayout->addWidget(receiveDataEdit);

    mainLayout->addWidget(receiveGroup);

    // ===================================================
    // 第四区域：收发计数显示（TX/RX 字节数）
    // ===================================================
    QHBoxLayout *countLayout = new QHBoxLayout();
    countLayout->setSpacing(10);

    txCountLabel = new QLabel("TX: 0 字节", this);
    txCountLabel->setStyleSheet(
        "QLabel { background-color: #E3F2FD; color: #1565C0; padding: 4px 12px; "
        "border-radius: 3px; font-size: 12px; font-weight: bold; min-width: 100px; }");
    txCountLabel->setAlignment(Qt::AlignCenter);
    txCountLabel->setFixedHeight(25);

    rxCountLabel = new QLabel("RX: 0 字节", this);
    rxCountLabel->setStyleSheet(
        "QLabel { background-color: #F3E5F5; color: #7B1FA2; padding: 4px 12px; "
        "border-radius: 3px; font-size: 12px; font-weight: bold; min-width: 100px; }");
    rxCountLabel->setAlignment(Qt::AlignCenter);
    rxCountLabel->setFixedHeight(25);

    countLayout->addWidget(txCountLabel);
    countLayout->addWidget(rxCountLabel);
    countLayout->addStretch();

    mainLayout->addLayout(countLayout);

    // ===================================================
    // 第五区域：底部工具栏
    // ===================================================
    QHBoxLayout *bottomLayout = new QHBoxLayout();

    openFileButton = new QPushButton("打开文件", this);
    openFileButton->setStyleSheet(BUTTON_STYLE_GRAY);
    openFileButton->setMaximumHeight(28);

    sendFileButton = new QPushButton("发送文件", this);
    sendFileButton->setStyleSheet(BUTTON_STYLE_ORANGE);
    sendFileButton->setMaximumHeight(28);

    saveWindowButton = new QPushButton("保存窗口", this);
    saveWindowButton->setStyleSheet(BUTTON_STYLE_GRAY);
    saveWindowButton->setMaximumHeight(28);

    clearWindowButton = new QPushButton("清除窗口", this);
    clearWindowButton->setStyleSheet(BUTTON_STYLE_GRAY);
    clearWindowButton->setMaximumHeight(28);

    helpButton = new QPushButton("帮助", this);
    helpButton->setStyleSheet(BUTTON_STYLE_GRAY);
    helpButton->setMaximumHeight(28);
    helpButton->setFixedWidth(60);

    clearButton = new QPushButton("清除接收", this);
    clearButton->setStyleSheet(BUTTON_STYLE_GRAY);
    clearButton->setMaximumHeight(28);

    bottomLayout->addWidget(openFileButton);
    bottomLayout->addWidget(sendFileButton);
    bottomLayout->addWidget(saveWindowButton);
    bottomLayout->addWidget(clearWindowButton);
    bottomLayout->addWidget(helpButton);
    bottomLayout->addWidget(clearButton);

    mainLayout->addLayout(bottomLayout);

    // ===================================================
    // 信号槽连接
    // ===================================================
    connect(connectButton, &QPushButton::clicked, this, &ComTestDialog::onConnectDisconnect);
    connect(autoSendButton, &QPushButton::clicked, this, &ComTestDialog::onSendDataSilent);
    connect(clearButton, &QPushButton::clicked, this, &ComTestDialog::onClearData);
    connect(openFileButton, &QPushButton::clicked, this, &ComTestDialog::onOpenFile);
    connect(sendFileButton, &QPushButton::clicked, this, &ComTestDialog::onSendFile);
    connect(saveWindowButton, &QPushButton::clicked, this, &ComTestDialog::onSaveWindow);
    connect(clearWindowButton, &QPushButton::clicked, this, &ComTestDialog::onClearData);
    connect(helpButton, &QPushButton::clicked, this, &ComTestDialog::onHelp);
    // Qt6 兼容: 使用 checkStateChanged（Qt6 新信号名，避免弃用警告）
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(hexDisplayCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState s){ onHexDisplayChanged(s == Qt::Checked ? 2 : 0); });
    connect(hexSendCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState s){ onHexSendChanged(s == Qt::Checked ? 2 : 0); });
    connect(dtrCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState s){ onDTRChanged(s == Qt::Checked ? 2 : 0); });
    connect(rtsCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState s){ onRTSChanged(s == Qt::Checked ? 2 : 0); });
    connect(autoSendCheck, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState s){ onAutoSendChanged(s == Qt::Checked ? 2 : 0); });
#else
    connect(hexDisplayCheck, &QCheckBox::stateChanged, this, &ComTestDialog::onHexDisplayChanged);
    connect(hexSendCheck, &QCheckBox::stateChanged, this, &ComTestDialog::onHexSendChanged);
    connect(dtrCheck, &QCheckBox::stateChanged, this, &ComTestDialog::onDTRChanged);
    connect(rtsCheck, &QCheckBox::stateChanged, this, &ComTestDialog::onRTSChanged);
    connect(autoSendCheck, &QCheckBox::stateChanged, this, &ComTestDialog::onAutoSendChanged);
#endif

    // 回车键发送
    connect(sendDataEdit, &QLineEdit::returnPressed, this, &ComTestDialog::onSendDataSilent);

    // 禁用按钮的默认行为（防止回车误触）
    connectButton->setAutoDefault(false);
    connectButton->setDefault(false);
    autoSendButton->setAutoDefault(false);
    autoSendButton->setDefault(false);
    clearButton->setAutoDefault(false);
    clearButton->setDefault(false);
    openFileButton->setAutoDefault(false);
    openFileButton->setDefault(false);
    sendFileButton->setAutoDefault(false);
    sendFileButton->setDefault(false);
    saveWindowButton->setAutoDefault(false);
    saveWindowButton->setDefault(false);
    clearWindowButton->setAutoDefault(false);
    clearWindowButton->setDefault(false);
    helpButton->setAutoDefault(false);
    helpButton->setDefault(false);
}

void ComTestDialog::checkPermissions()
{
#ifdef QT_SERIALPORT_LIB
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    bool hasPermissionIssue = false;
    QStringList problemPorts;

    for (const QSerialPortInfo &port : ports) {
        QFileInfo deviceInfo(port.systemLocation());
        if (!deviceInfo.isReadable() || !deviceInfo.isWritable()) {
            hasPermissionIssue = true;
            problemPorts << port.systemLocation();
        }
    }

    if (hasPermissionIssue) {
        int result = QMessageBox::question(this, "串口权限问题",
            QString("检测到 %1 个串口设备权限限制，这会影响COM测试功能。\n\n"
                   "是否立即一键修复所有串口权限？")
                   .arg(problemPorts.size()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);

        if (result == QMessageBox::Yes) {
            setupSerialPortPermissions(problemPorts);
        }
    }
#endif
}

void ComTestDialog::setupSerialPortPermissions(const QStringList &portPaths)
{
    Q_UNUSED(portPaths);
#ifdef QT_SERIALPORT_LIB
    if (portPaths.isEmpty()) {
        receiveDataEdit->append("没有需要修复权限的串口设备");
        return;
    }

    // 询问用户确认
    int result = QMessageBox::question(this, "确认修复",
        QString("将修复以下 %1 个串口设备的权限：\n\n%2\n\n"
               "此操作需要管理员权限，请在弹出的认证窗口中输入密码。\n"
               "确定要继续吗？")
               .arg(portPaths.size())
               .arg(portPaths.join("\n")),
        QMessageBox::Yes | QMessageBox::No);

    if (result != QMessageBox::Yes) {
        return;
    }

    // 构建修复脚本
    QStringList fixCommands;
    for (const QString &portPath : portPaths) {
        fixCommands << QString("chmod 666 %1").arg(portPath);
    }

    QString userName = qgetenv("USER");
    if (userName.isEmpty()) {
        userName = qgetenv("USERNAME");
    }
    if (!userName.isEmpty()) {
        fixCommands << QString("usermod -a -G dialout %1").arg(userName);
    }

    // 创建临时脚本（修复C8：带 PID 防多实例互相覆盖/误删）
    QString tempScriptPath = QString("/tmp/fix_serial_permissions_%1.sh").arg(QCoreApplication::applicationPid());
    QFile scriptFile(tempScriptPath);
    if (scriptFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&scriptFile);
        out << "#!/bin/bash\n";
        out << "echo '开始修复串口权限...'\n";
        for (const QString &cmd : fixCommands) {
            out << cmd << "\n";
        }

        // 创建 udev 规则
        out << "cat > /etc/udev/rules.d/99-serial-permissions.rules << 'EOF'\n";
        out << "# 串口设备权限规则\n";
        for (const QString &portPath : portPaths) {
            QString deviceName = QFileInfo(portPath).fileName();
            out << QString("SUBSYSTEM==\"tty\", KERNEL==\"%1\", MODE=\"0666\"\n").arg(deviceName);
        }
        out << "EOF\n";

        out << "udevadm control --reload-rules\n";
        out << "udevadm trigger\n";
        out << "echo '串口权限修复完成！'\n";
        scriptFile.close();

        QFile::setPermissions(tempScriptPath,
                            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                            QFile::ReadGroup | QFile::ExeGroup |
                            QFile::ReadOther | QFile::ExeOther);
    }

    // 使用统一权限模块执行修复脚本（优先免密/记忆密码，兜底弹窗）
    receiveDataEdit->append("正在执行串口权限修复...");

    QString fixErr;
    bool fixOk = PrivilegeHelper::runPrivileged(QString("bash '%1'").arg(tempScriptPath), &fixErr);

    if (fixOk) {
        receiveDataEdit->append("串口权限修复成功！");
    } else {
        receiveDataEdit->append("修复串口权限失败: " + fixErr);
    }

    // 清理临时脚本
    QFile::remove(tempScriptPath);
#endif
}

void ComTestDialog::onConnectDisconnect()
{
#ifdef QT_SERIALPORT_LIB
    if (!isConnected) {
        // 连接串口
        QString displayText = comPortCombo->currentText();
        QString portName = comPortCombo->currentData().toString();

        if (displayText.contains("无可用串口") || displayText.contains("串口功能不可用")) {
            QMessageBox::warning(this, "错误", "没有可用的串口设备");
            return;
        }

        if (displayText.contains("权限不足")) {
            QMessageBox::warning(this, "权限错误",
                QString("串口 %1 权限不足，无法访问。\n\n"
                       "请修复串口权限或使用管理员权限运行程序。")
                .arg(portName));
            return;
        }

        if (portName.isEmpty()) {
            // 尝试从显示文本中提取端口名
            QRegularExpression re("(COM\\d+|tty\\S+)", QRegularExpression::CaseInsensitiveOption);
            QRegularExpressionMatch match = re.match(displayText);
            if (match.hasMatch()) {
                portName = match.captured(1);
            } else {
                QMessageBox::warning(this, "错误", "无法确定串口设备名称");
                return;
            }
        }

        // 记录连接尝试
        receiveDataEdit->append(QString("[%1] 正在连接串口: %2")
                               .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                               .arg(portName));

        // 配置串口参数
        serialPort->setPortName(portName);
        serialPort->setBaudRate(baudRateCombo->currentText().toInt());
        serialPort->setDataBits(static_cast<QSerialPort::DataBits>(dataBitsCombo->currentText().toInt()));
        // 设置停止位
        // 修复（C4）：旧代码 setStopBits(currentIndex()+1) 把 QSerialPort 枚举值当档位数用，
        // 枚举为 OneStop=1, TwoStop=2, OneAndHalfStop=3 —— 选"1.5"得到 2(TwoStop)、
        // 选"2"得到 3(OneAndHalfStop)，1.5 与 2 恰好互换，导致串口通信失败/乱码。
        // 改为显式映射：索引 0("1")→OneStop、1("1.5")→OneAndHalfStop、2("2")→TwoStop
        switch (stopBitsCombo->currentIndex()) {
            case 0:  serialPort->setStopBits(QSerialPort::OneStop);        break;
            case 1:  serialPort->setStopBits(QSerialPort::OneAndHalfStop); break;
            case 2:  serialPort->setStopBits(QSerialPort::TwoStop);        break;
            default: serialPort->setStopBits(QSerialPort::OneStop);        break;
        }

        // 设置校验位
        switch (parityCombo->currentIndex()) {
            case 0: serialPort->setParity(QSerialPort::NoParity); break;
            case 1: serialPort->setParity(QSerialPort::OddParity); break;
            case 2: serialPort->setParity(QSerialPort::EvenParity); break;
        }

        // 设置流控制
        switch (flowControlCombo->currentIndex()) {
            case 0: serialPort->setFlowControl(QSerialPort::NoFlowControl); break;
            case 1: serialPort->setFlowControl(QSerialPort::HardwareControl); break;
            case 2: serialPort->setFlowControl(QSerialPort::SoftwareControl); break;
        }

        // 优化串口设置
        serialPort->setReadBufferSize(4096);
        serialPort->setFlowControl(QSerialPort::NoFlowControl);

        // 尝试打开串口
        if (serialPort->open(QIODevice::ReadWrite)) {
            isConnected = true;
            connectButton->setText("关闭串口");
            connectButton->setStyleSheet("QPushButton { background-color: #f44336; color: white; border: none; "
                                         "padding: 4px 12px; border-radius: 3px; font-size: 12px; font-weight: bold; }");

            // 禁用所有配置控件（除DTR/RTS外）
            comPortCombo->setEnabled(false);
            baudRateCombo->setEnabled(false);
            dataBitsCombo->setEnabled(false);
            stopBitsCombo->setEnabled(false);
            parityCombo->setEnabled(false);
            flowControlCombo->setEnabled(false);

            // 启用DTR/RTS控制
            dtrCheck->setEnabled(true);
            rtsCheck->setEnabled(true);

            // 启用定时发送控件和发送按钮
            autoSendCheck->setEnabled(true);
            autoSendButton->setEnabled(true);

            receiveDataEdit->append(QString("[%1] 已连接到串口: %2")
                                   .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                                   .arg(portName));
            receiveDataEdit->append(QString("[%1] 串口配置: %2波特, %3数据位, %4停止位")
                                   .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                                   .arg(baudRateCombo->currentText())
                                   .arg(dataBitsCombo->currentText())
                                   .arg(stopBitsCombo->currentText()));

            // 清空接收缓冲区
            receiveBuffer.clear();

        } else {
            QString errorString = serialPort->errorString();

            // 详细错误处理
            QString detailedError = "无法打开串口:\n";
            detailedError += "设备: " + portName + "\n";
            detailedError += "错误: " + errorString + "\n\n";

            if (errorString.contains("denied", Qt::CaseInsensitive) ||
                errorString.contains("permission", Qt::CaseInsensitive)) {
                detailedError += "可能的原因：\n";
                detailedError += "1. 权限不足，请修复串口权限\n";
                detailedError += "2. 当前用户不在dialout组\n";
                detailedError += "3. 设备文件权限不正确\n\n";
                detailedError += "解决方案：\n";
                detailedError += "• 点击对话框的确定按钮修复权限\n";
                detailedError += "• 或使用管理员权限运行程序";

                QMessageBox::StandardButton fixButton = QMessageBox::question(
                    this, "权限错误", detailedError,
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::Yes
                );

                if (fixButton == QMessageBox::Yes) {
                    // 重新检测并修复权限
                    QTimer::singleShot(0, this, &ComTestDialog::initializeSerialPort);
                }
            } else if (errorString.contains("busy", Qt::CaseInsensitive) ||
                       errorString.contains("in use", Qt::CaseInsensitive)) {
                detailedError += "可能的原因：\n";
                detailedError += "串口正被其他程序占用\n\n";
                detailedError += "解决方案：\n";
                detailedError += "• 关闭其他使用该串口的程序\n";
                detailedError += "• 或选择其他串口";
                QMessageBox::warning(this, "设备忙", detailedError);
            } else {
                QMessageBox::warning(this, "连接失败", detailedError);
            }

            receiveDataEdit->append(QString("[%1] 连接失败: %2")
                                   .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                                   .arg(errorString));
        }
    } else {
        // 断开串口
        serialPort->close();
        isConnected = false;
        connectButton->setText("打开串口");
        connectButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; border: none; "
                                      "padding: 4px 12px; border-radius: 3px; font-size: 12px; font-weight: bold; }");

        // 启用所有配置控件
        comPortCombo->setEnabled(true);
        baudRateCombo->setEnabled(true);
        dataBitsCombo->setEnabled(true);
        stopBitsCombo->setEnabled(true);
        parityCombo->setEnabled(true);
        flowControlCombo->setEnabled(true);

        // 禁用DTR/RTS控制
        dtrCheck->setEnabled(false);
        dtrCheck->setChecked(false);
        rtsCheck->setEnabled(false);
        rtsCheck->setChecked(false);

        // 关闭定时发送
        if (autoSendTimer->isActive()) {
            autoSendTimer->stop();
            autoSendCheck->setChecked(false);
        }
        autoSendCheck->setEnabled(false);
        autoSendButton->setEnabled(false);

        receiveDataEdit->append(QString("[%1] 已断开串口连接")
                               .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));

        // 清空接收缓冲区
        receiveBuffer.clear();
    }
#else
    QMessageBox::information(this, "功能不可用", "COM测试功能在当前系统中不可用。");
#endif
}

// ===================================================
// 新增功能实现
// ===================================================

// 字节转HEX字符串
QString ComTestDialog::bytesToHex(const QByteArray &data)
{
    QString hex;
    for (int i = 0; i < data.size(); ++i) {
        hex += QString("%1 ").arg(static_cast<quint8>(data.at(i)), 2, 16, QChar('0')).toUpper();
    }
    return hex.trimmed();
}

// HEX字符串转字节数组
QByteArray ComTestDialog::hexToBytes(const QString &hex)
{
    QByteArray bytes;
    QString cleanHex = hex;
    cleanHex.remove(QChar(' '));
    cleanHex.remove(QChar('\t'));
    cleanHex.remove(QChar('\n'));
    cleanHex.remove(QChar('\r'));

    for (int i = 0; i < cleanHex.length(); i += 2) {
        if (i + 1 < cleanHex.length()) {
            bool ok;
            quint8 byte = cleanHex.mid(i, 2).toUShort(&ok, 16);
            if (ok) {
                bytes.append(static_cast<char>(byte));
            }
        } else {
            bool ok;
            quint8 byte = cleanHex.mid(i, 1).toUShort(&ok, 16);
            if (ok) {
                bytes.append(static_cast<char>(byte));
            }
        }
    }
    return bytes;
}

// 文本转HEX显示
QString ComTestDialog::textToHexDisplay(const QString &text)
{
    QByteArray utf8 = text.toUtf8();
    return bytesToHex(utf8);
}

// 打开文件
void ComTestDialog::onOpenFile()
{
    QString fileName = QFileDialog::getOpenFileName(this, "选择串口数据文件",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "文本文件 (*.txt);;所有文件 (*.*)");

    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString content = QString::fromUtf8(file.readAll());
            sendDataEdit->setText(content);
            receiveDataEdit->append(QString("[%1] 已加载文件: %2 (%3 字节)")
                .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                .arg(QFileInfo(fileName).fileName())
                .arg(content.toUtf8().size()));
            file.close();
        } else {
            QMessageBox::warning(this, "打开失败", "无法读取文件: " + fileName);
        }
    }
}

// 发送文件
void ComTestDialog::onSendFile()
{
#ifdef QT_SERIALPORT_LIB
    if (!isConnected || !serialPort || !serialPort->isOpen()) {
        QMessageBox::warning(this, "发送失败", "请先打开串口");
        return;
    }

    QString fileName = QFileDialog::getOpenFileName(this, "选择要发送的文件",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "所有文件 (*.*)");

    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "打开失败", "无法打开文件: " + fileName);
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    qint64 bytesWritten = serialPort->write(data);
    serialPort->waitForBytesWritten(5000);

    // 累加发送字节数
    if (bytesWritten > 0) {
        txByteCount += static_cast<quint64>(bytesWritten);
        updateCountDisplay();
    }

    receiveDataEdit->append(QString("[%1] 发送文件: %2 (%3 字节)")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
        .arg(QFileInfo(fileName).fileName())
        .arg(bytesWritten));
#else
    QMessageBox::information(this, "功能不可用", "串口功能不可用");
#endif
}

// 保存窗口内容
void ComTestDialog::onSaveWindow()
{
    QString fileName = QFileDialog::getSaveFileName(this, "保存接收数据",
        QString("%1/com_receive_%2.txt")
            .arg(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        "文本文件 (*.txt);;所有文件 (*.*)");

    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << receiveDataEdit->toPlainText();
            file.close();
            QMessageBox::information(this, "保存成功", "数据已保存到: " + fileName);
        } else {
            QMessageBox::warning(this, "保存失败", "无法写入文件: " + fileName);
        }
    }
}

// 帮助
void ComTestDialog::onHelp()
{
    QMessageBox::information(this, "使用帮助",
        "<h3>串口测试工具使用说明</h3>"
        "<p><b>1. 配置串口:</b> 选择串口号、波特率、数据位、停止位、校验位和流控制</p>"
        "<p><b>2. 打开串口:</b> 点击\"打开串口\"按钮</p>"
        "<p><b>3. 发送数据:</b></p>"
        "<ul>"
        "<li>普通发送: 输入字符串直接发送</li>"
        "<li>HEX发送: 勾选\"HEX发送\"后输入十六进制(如 48 65 6C 6C 6F)</li>"
        "<li>定时发送: 勾选\"定时发送\"并设置间隔(ms),自动循环发送</li>"
        "</ul>"
        "<p><b>4. 接收数据:</b></p>"
        "<ul>"
        "<li>勾选\"HEX显示\"以十六进制格式查看数据</li>"
        "</ul>"
        "<p><b>5. 文件操作:</b></p>"
        "<ul>"
        "<li>打开文件: 加载文件内容到发送框</li>"
        "<li>发送文件: 直接发送整个文件</li>"
        "<li>保存窗口: 保存接收的数据到文件</li>"
        "</ul>"
        "<p><b>注意:</b> 海光3490等系统可能需要管理员权限才能访问串口</p>");
}

// HEX显示切换
void ComTestDialog::onHexDisplayChanged(int state)
{
    hexDisplayMode = (state == Qt::Checked);
    receiveDataEdit->clear();
    receiveDataEdit->append(QString("[%1] HEX显示模式: %2")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
        .arg(hexDisplayMode ? "开启" : "关闭"));
}

// HEX发送切换
void ComTestDialog::onHexSendChanged(int state)
{
    hexSendMode = (state == Qt::Checked);
    sendDataEdit->setPlaceholderText(hexSendMode ?
        "HEX模式：输入如 48 65 6C 6C 6F（十六进制，空格分隔）" :
        "输入要发送的数据...");
}

// DTR切换
void ComTestDialog::onDTRChanged(int state)
{
    Q_UNUSED(state);  // 防止未使用警告
#ifdef QT_SERIALPORT_LIB
    if (serialPort && serialPort->isOpen()) {
        serialPort->setDataTerminalReady(state == Qt::Checked);
        receiveDataEdit->append(QString("[%1] DTR信号: %2")
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
            .arg(state == Qt::Checked ? "开启" : "关闭"));
    }
#endif
}

// RTS切换
void ComTestDialog::onRTSChanged(int state)
{
    Q_UNUSED(state);  // 防止未使用警告
#ifdef QT_SERIALPORT_LIB
    if (serialPort && serialPort->isOpen()) {
        serialPort->setRequestToSend(state == Qt::Checked);
        receiveDataEdit->append(QString("[%1] RTS信号: %2")
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
            .arg(state == Qt::Checked ? "开启" : "关闭"));
    }
#endif
}

// 定时发送开关
void ComTestDialog::onAutoSendChanged(int state)
{
    Q_UNUSED(state);  // 防止未使用警告
#ifdef QT_SERIALPORT_LIB
    if (state == Qt::Checked) {
        if (!isConnected || !serialPort || !serialPort->isOpen()) {
            QMessageBox::warning(this, "无法启动", "请先打开串口");
            autoSendCheck->setChecked(false);
            return;
        }

        int interval = autoSendIntervalSpin->value();
        autoSendTimer->start(interval);
        autoSendIntervalSpin->setEnabled(false);
        autoSendButton->setEnabled(false);
        receiveDataEdit->append(QString("[%1] 定时发送已启动 (间隔: %2 ms)")
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
            .arg(interval));
    } else {
        autoSendTimer->stop();
        autoSendIntervalSpin->setEnabled(true);
        autoSendButton->setEnabled(true);
        receiveDataEdit->append(QString("[%1] 定时发送已停止")
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    }
#endif
}

// 定时发送触发
void ComTestDialog::onAutoSendTimer()
{
    // 自动复用onSendDataSilent的逻辑
    onSendDataSilent();
}

void ComTestDialog::onSendDataSilent()
{
    QString data = sendDataEdit->text().trimmed();
    if (data.isEmpty()) {
        return; // 空数据静默返回
    }

#ifdef QT_SERIALPORT_LIB
    if (!isConnected || !serialPort || !serialPort->isOpen()) {
        // 未连接时，只在接收区域显示提示，不弹出对话框
        receiveDataEdit->append(QString("[%1] 错误: 串口未连接")
                               .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
        return;
    }

    QByteArray sendBytes;

    if (hexSendMode) {
        // HEX发送模式：将输入解析为十六进制字节
        sendBytes = hexToBytes(data);

        // 显示发送的内容（HEX格式）
        QString hexDisplay = bytesToHex(sendBytes);
        receiveDataEdit->append(QString("[%1] 发送(HEX): %2 (%3 字节)")
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
            .arg(hexDisplay)
            .arg(sendBytes.size()));
    } else {
        // 普通文本模式：自动添加换行符
        QString dataToSend = data;
        if (!dataToSend.endsWith('\n') && !dataToSend.endsWith('\r')) {
            dataToSend += "\r\n";
        }

        sendBytes = dataToSend.toUtf8();

        // 显示发送的数据
        receiveDataEdit->append(QString("[%1] 发送: %2")
            .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
            .arg(data));
    }

    qint64 bytesWritten = serialPort->write(sendBytes);

    if (bytesWritten == -1) {
        // 只在日志中记录错误，不显示对话框
        receiveDataEdit->append(QString("[%1] 发送失败: %2")
                               .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                               .arg(serialPort->errorString()));
    } else {
        // 累加发送字节数
        txByteCount += static_cast<quint64>(bytesWritten);
        updateCountDisplay();

        // [2026-10-29] 修复:删除 waitForBytesWritten(1000) 同步阻塞等待。
        // 原代码在 UI 线程里每发一次最多阻塞 1 秒——串口写缓冲积压时(低波特率/
        // 硬件流控/对端未读),5 个窗口的定时发送叠加会把事件循环彻底卡死。
        // write() 已把数据交给 QSerialPort 的异步发送缓冲,无需在 UI 线程等待;
        // 发送失败仍由 bytesWritten==-1 与 error 信号上报。
        // 定时发送模式下不清空输入框（因为需要循环发送）
        if (!autoSendCheck->isChecked()) {
            // 成功发送后清空输入框（仅在非定时模式下）
            sendDataEdit->clear();
        }
    }

    // 自动滚动到底部
    QTextCursor cursor = receiveDataEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    receiveDataEdit->setTextCursor(cursor);
#else
    receiveDataEdit->append(QString("[%1] 错误: 串口功能不可用")
                           .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
#endif
}

void ComTestDialog::onSendData()
{
    // 这个方法现在只用于按钮点击，也使用静默发送
    onSendDataSilent();
}

void ComTestDialog::onClearData()
{
    receiveDataEdit->clear();
    // 同时重置计数器
    txByteCount = 0;
    rxByteCount = 0;
    updateCountDisplay();
}

// 新增：清除窗口（与onClearData相同）
void ComTestDialog::onClearWindow()
{
    onClearData();
}

// 新增：更新计数显示
void ComTestDialog::updateCountDisplay()
{
    if (txCountLabel) {
        if (txByteCount < 1024) {
            txCountLabel->setText(QString("TX: %1 字节").arg(txByteCount));
        } else if (txByteCount < 1024 * 1024) {
            txCountLabel->setText(QString("TX: %1 KB (%2 字节)")
                .arg(txByteCount / 1024.0, 0, 'f', 2).arg(txByteCount));
        } else {
            txCountLabel->setText(QString("TX: %1 MB (%2 字节)")
                .arg(txByteCount / (1024.0 * 1024.0), 0, 'f', 2).arg(txByteCount));
        }
    }
    if (rxCountLabel) {
        if (rxByteCount < 1024) {
            rxCountLabel->setText(QString("RX: %1 字节").arg(rxByteCount));
        } else if (rxByteCount < 1024 * 1024) {
            rxCountLabel->setText(QString("RX: %1 KB (%2 字节)")
                .arg(rxByteCount / 1024.0, 0, 'f', 2).arg(rxByteCount));
        } else {
            rxCountLabel->setText(QString("RX: %1 MB (%2 字节)")
                .arg(rxByteCount / (1024.0 * 1024.0), 0, 'f', 2).arg(rxByteCount));
        }
    }
}

#ifdef QT_SERIALPORT_LIB
void ComTestDialog::onReadyRead()
{
    if (isConnected && serialPort->isOpen()) {
        // 读取所有可用数据
        QByteArray data = serialPort->readAll();

        if (!data.isEmpty()) {
            // 累加接收字节数
            rxByteCount += static_cast<quint64>(data.size());
            updateCountDisplay();

            // 将数据添加到缓冲区
            receiveBuffer.append(data);

            if (hexDisplayMode) {
                // HEX显示模式：直接以十六进制显示
                QString hexStr = bytesToHex(data);
                receiveDataEdit->append(QString("[%1] 接收(HEX): %2 (%3 字节)")
                    .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                    .arg(hexStr)
                    .arg(data.size()));
                receiveBuffer.clear();
            } else {
                // 文本模式：按行处理
                // 修复：使用 QByteArray 的分割方法
                if (receiveBuffer.contains('\n')) {
                    // 修复：正确分割 QByteArray
                    QList<QByteArray> lines = receiveBuffer.split('\n');

                    // [2026-10-29] 修复:逐行 append 改为拼好后一次性 append,
                    // 一次 readyRead 里的多行只触发一次文档变更/重排,
                    // 高频收发时 UI 开销降为原来的 1/N
                    QStringList shown;
                    // 处理完整的行（最后一行可能不完整）
                    for (int i = 0; i < lines.size() - 1; i++) {
                        // 修复：正确处理 QByteArray 到 QString 的转换
                        QByteArray lineData = lines[i];
                        // 移除可能的回车符
                        if (lineData.endsWith('\r')) {
                            lineData.chop(1);
                        }

                        QString line = QString::fromUtf8(lineData).trimmed();
                        if (!line.isEmpty()) {
                            shown << QString("[%1] 接收: %2")
                                         .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                                         .arg(line);
                        }
                    }
                    if (!shown.isEmpty()) {
                        receiveDataEdit->append(shown.join('\n'));
                    }

                    // 修复：保留不完整的最后一行
                    receiveBuffer = lines.last();
                }

                // 如果缓冲区太大，直接显示（防止内存溢出）
                if (receiveBuffer.length() > 1024) {
                    QString receivedText = QString::fromUtf8(receiveBuffer);
                    receiveDataEdit->append(QString("[%1] 接收: %2")
                                           .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                                           .arg(receivedText));
                    receiveBuffer.clear();
                }
            }

            // 自动滚动到底部
            QTextCursor cursor = receiveDataEdit->textCursor();
            cursor.movePosition(QTextCursor::End);
            receiveDataEdit->setTextCursor(cursor);
        }
    }
}

void ComTestDialog::onSerialPortError(QSerialPort::SerialPortError error) {
    if (error == QSerialPort::NoError)
        return;

    QString errorMsg;
    switch (error) {
    case QSerialPort::PermissionError:
        errorMsg = "权限不足，无法访问串口";
        break;
    case QSerialPort::DeviceNotFoundError:
        errorMsg = "串口设备不存在";
        break;
    case QSerialPort::OpenError:
        errorMsg = "串口打开失败";
        break;
    default:
        errorMsg = QString("串口错误: %1").arg(serialPort->errorString());
        break;
    }

    receiveDataEdit->append(QString("[%1] %2")
                           .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                           .arg(errorMsg));
}
#endif