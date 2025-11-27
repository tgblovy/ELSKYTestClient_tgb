#include "com_test_dialog.h"
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

#ifdef QT_SERIALPORT_LIB
#include <QSerialPortInfo>
#endif

ComTestDialog::ComTestDialog(QWidget *parent)
    : QDialog(parent), isConnected(false)
{
    setupUI();

    // 设置对话框属性
    setWindowModality(Qt::ApplicationModal); // 应用程序模态
    setWindowFlags(windowFlags() | Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

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

    setWindowTitle("COM端口测试");
    setMinimumSize(600, 500);
    receiveBuffer.clear();

    // 在对话框初始化时检查权限
    checkPermissions();
}

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

    // 串口配置区域
    QGroupBox *configGroup = new QGroupBox("串口配置", this);
    QGridLayout *configLayout = new QGridLayout(configGroup);

    configLayout->addWidget(new QLabel("COM端口:"), 0, 0);
    comPortCombo = new QComboBox(this);

#ifdef QT_SERIALPORT_LIB
    // 获取可用串口
    QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    if (ports.isEmpty()) {
        comPortCombo->addItem("无可用端口");
    } else {
        for (const QSerialPortInfo &port : ports) {
            comPortCombo->addItem(port.portName());
        }
    }
#else
    comPortCombo->addItem("串口功能不可用");
    comPortCombo->setEnabled(false);
#endif
    configLayout->addWidget(comPortCombo, 0, 1);

    configLayout->addWidget(new QLabel("波特率:"), 0, 2);
    baudRateCombo = new QComboBox(this);
    baudRateCombo->addItems({"9600", "19200", "38400", "57600", "115200"});
    baudRateCombo->setCurrentText("115200");
    configLayout->addWidget(baudRateCombo, 0, 3);

    configLayout->addWidget(new QLabel("数据位:"), 1, 0);
    dataBitsCombo = new QComboBox(this);
    dataBitsCombo->addItems({"5", "6", "7", "8"});
    dataBitsCombo->setCurrentText("8");
    configLayout->addWidget(dataBitsCombo, 1, 1);

    configLayout->addWidget(new QLabel("停止位:"), 1, 2);
    stopBitsCombo = new QComboBox(this);
    stopBitsCombo->addItems({"1", "1.5", "2"});
    configLayout->addWidget(stopBitsCombo, 1, 3);

    configLayout->addWidget(new QLabel("校验位:"), 2, 0);
    parityCombo = new QComboBox(this);
    parityCombo->addItems({"无", "奇校验", "偶校验"});
    configLayout->addWidget(parityCombo, 2, 1);

    configLayout->addWidget(new QLabel("流控制:"), 2, 2);
    flowControlCombo = new QComboBox(this);
    flowControlCombo->addItems({"无", "硬件", "软件"});
    configLayout->addWidget(flowControlCombo, 2, 3);

    // 连接按钮
    connectButton = new QPushButton("连接", this);
    connectButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; }");
    configLayout->addWidget(connectButton, 3, 0, 1, 4);

    // 数据收发区域
    QGroupBox *dataGroup = new QGroupBox("数据收发", this);
    QVBoxLayout *dataLayout = new QVBoxLayout(dataGroup);

    // 发送区域
    QHBoxLayout *sendLayout = new QHBoxLayout();
    sendDataEdit = new QLineEdit(this);
    sendDataEdit->setPlaceholderText("输入要发送的数据...");
    sendButton = new QPushButton("发送", this);

    // 关键修复：禁用按钮的默认行为
    sendButton->setAutoDefault(false);
    sendButton->setDefault(false);

    sendLayout->addWidget(new QLabel("发送:"));
    sendLayout->addWidget(sendDataEdit);
    sendLayout->addWidget(sendButton);

    // 接收区域
    receiveDataEdit = new QTextEdit(this);
    receiveDataEdit->setReadOnly(true);

    // 清除按钮
    clearButton = new QPushButton("清除接收", this);

    dataLayout->addLayout(sendLayout);
    dataLayout->addWidget(new QLabel("接收:"));
    dataLayout->addWidget(receiveDataEdit);
    dataLayout->addWidget(clearButton);

    mainLayout->addWidget(configGroup);
    mainLayout->addWidget(dataGroup);

    // 连接信号和槽
    connect(connectButton, &QPushButton::clicked, this, &ComTestDialog::onConnectDisconnect);
    connect(sendButton, &QPushButton::clicked, this, &ComTestDialog::onSendDataSilent);
    connect(clearButton, &QPushButton::clicked, this, &ComTestDialog::onClearData);

    // 关键修复：完全接管回车键处理
    connect(sendDataEdit, &QLineEdit::returnPressed, this, &ComTestDialog::onSendDataSilent);

    // 确保连接按钮不会捕获回车键
    connectButton->setAutoDefault(false);
    connectButton->setDefault(false);

    // 确保清除按钮不会捕获回车键
    clearButton->setAutoDefault(false);
    clearButton->setDefault(false);
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

    // 创建临时脚本
    QString tempScriptPath = "/tmp/fix_serial_permissions.sh";
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

    // 使用 pkexec 执行修复脚本
    QProcess process;
    process.start("pkexec", QStringList() << "bash" << tempScriptPath);

    receiveDataEdit->append("正在执行串口权限修复，请在弹出的窗口中输入管理员密码...");

    if (process.waitForFinished(30000)) {
        if (process.exitCode() == 0) {
            QString output = QString::fromUtf8(process.readAllStandardOutput());
            receiveDataEdit->append("串口权限修复成功！");
            receiveDataEdit->append("修复输出: " + output);
        } else {
            QString error = QString::fromUtf8(process.readAllStandardError());
            receiveDataEdit->append("修复串口权限失败: " + error);
        }
    } else {
        receiveDataEdit->append("权限修复执行超时");
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
        QString portName = comPortCombo->currentText();
        if (portName == "无可用端口") {
            QMessageBox::warning(this, "错误", "没有可用的串口设备");
            return;
        }

        serialPort->setPortName(portName);
        serialPort->setBaudRate(baudRateCombo->currentText().toInt());
        serialPort->setDataBits(static_cast<QSerialPort::DataBits>(dataBitsCombo->currentText().toInt()));
        serialPort->setStopBits(static_cast<QSerialPort::StopBits>(stopBitsCombo->currentIndex() + 1));

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

        if (serialPort->open(QIODevice::ReadWrite)) {
            isConnected = true;
            connectButton->setText("断开");
            connectButton->setStyleSheet("QPushButton { background-color: #f44336; color: white; }");

            // 禁用所有配置控件
            comPortCombo->setEnabled(false);
            baudRateCombo->setEnabled(false);
            dataBitsCombo->setEnabled(false);
            stopBitsCombo->setEnabled(false);
            parityCombo->setEnabled(false);
            flowControlCombo->setEnabled(false);

            receiveDataEdit->append(QString("[%1] 已连接到串口: %2")
                                   .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                                   .arg(portName));

            // 清空接收缓冲区
            receiveBuffer.clear();

        } else {
            QString errorString = serialPort->errorString();
            QMessageBox::warning(this, "错误", "无法打开串口: " + errorString);

            // 提供权限修复建议
            if (errorString.contains("denied") || errorString.contains("permission")) {
                QMessageBox::information(this, "权限提示",
                    "串口权限不足，建议执行以下命令：\n\n"
                    "sudo usermod -a -G dialout $USER\n\n"
                    "然后重新登录系统。");
            }
        }
    } else {
        // 断开串口
        serialPort->close();
        isConnected = false;
        connectButton->setText("连接");
        connectButton->setStyleSheet("QPushButton { background-color: #4CAF50; color: white; }");

        // 启用所有配置控件
        comPortCombo->setEnabled(true);
        baudRateCombo->setEnabled(true);
        dataBitsCombo->setEnabled(true);
        stopBitsCombo->setEnabled(true);
        parityCombo->setEnabled(true);
        flowControlCombo->setEnabled(true);

        receiveDataEdit->append(QString("[%1] 已断开串口连接")
                               .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));

        // 清空接收缓冲区
        receiveBuffer.clear();
    }
#else
    QMessageBox::information(this, "功能不可用", "COM测试功能在当前系统中不可用。");
#endif
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

    // 先显示发送的数据
    receiveDataEdit->append(QString("[%1] 发送: %2")
                           .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                           .arg(data));

    // 可选：自动添加换行符
    QString dataToSend = data;
    if (!dataToSend.endsWith('\n') && !dataToSend.endsWith('\r')) {
        dataToSend += "\r\n";
    }

    QByteArray sendBytes = dataToSend.toUtf8();
    qint64 bytesWritten = serialPort->write(sendBytes);

    if (bytesWritten == -1) {
        // 只在日志中记录错误，不显示对话框
        receiveDataEdit->append(QString("[%1] 发送失败: %2")
                               .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                               .arg(serialPort->errorString()));
    } else {
        // 确保数据发送完成
        if (!serialPort->waitForBytesWritten(1000)) {
            receiveDataEdit->append(QString("[%1] 发送超时")
                                   .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
        }
        // 成功发送后清空输入框
        sendDataEdit->clear();
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
}

#ifdef QT_SERIALPORT_LIB
void ComTestDialog::onReadyRead()
{
    if (isConnected && serialPort->isOpen()) {
        // 读取所有可用数据
        QByteArray data = serialPort->readAll();

        if (!data.isEmpty()) {
            // 将数据添加到缓冲区
            receiveBuffer.append(data);

            // 修复：使用 QByteArray 的分割方法
            if (receiveBuffer.contains('\n')) {
                // 修复：正确分割 QByteArray
                QList<QByteArray> lines = receiveBuffer.split('\n');

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
                        receiveDataEdit->append(QString("[%1] 接收: %2")
                                               .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
                                               .arg(line));
                    }
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
