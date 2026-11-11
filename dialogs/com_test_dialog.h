#ifndef COM_TEST_DIALOG_H
#define COM_TEST_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QTimer>
#include <QLabel>
#include <QList>
#include <QString>

#ifdef QT_SERIALPORT_LIB
#include <QSerialPort>
#include <QSerialPortInfo>
#endif

class ComTestDialog : public QDialog
{
    Q_OBJECT

public:
    ComTestDialog(QWidget *parent = nullptr);
    ~ComTestDialog();

private slots:
    void onConnectDisconnect();
    void onSendData();
    void onClearData();
    void onOpenFile();          // 新增：打开文件
    void onSendFile();          // 新增：发送文件
    void onSaveWindow();         // 新增：保存窗口
    void onClearWindow();        // 新增：清除窗口
    void onHelp();               // 新增：帮助
    void onHexDisplayChanged(int state);    // 新增：HEX显示
    void onHexSendChanged(int state);       // 新增：HEX发送
    void onDTRChanged(int state);           // 新增：DTR控制
    void onRTSChanged(int state);           // 新增：RTS控制
    void onAutoSendChanged(int state);      // 新增：定时发送
    void onAutoSendTimer();                 // 新增：定时发送触发

#ifdef QT_SERIALPORT_LIB
    void onReadyRead();
    void onSerialPortError(QSerialPort::SerialPortError error);
#endif

private:
    void setupUI();
    void onSendDataSilent();
    void checkPermissions();
    void setupSerialPortPermissions(const QStringList &portPaths);
    void initializeSerialPort();

    // 新增：HEX转换函数
    static QString bytesToHex(const QByteArray &data);
    static QByteArray hexToBytes(const QString &hex);
    static QString textToHexDisplay(const QString &text);

#ifdef QT_SERIALPORT_LIB
    void checkAndRefreshPortList(const QList<QSerialPortInfo> &ports);
    void refreshSerialPortList(const QList<QSerialPortInfo> &ports, const QStringList &problemPorts);
#endif

    QComboBox *comPortCombo;
    QComboBox *baudRateCombo;
    QComboBox *dataBitsCombo;
    QComboBox *stopBitsCombo;
    QComboBox *parityCombo;
    QComboBox *flowControlCombo;

    QLineEdit *sendDataEdit;
    QTextEdit *receiveDataEdit;

    QPushButton *connectButton;
    QPushButton *sendButton;
    QPushButton *clearButton;

    // 新增控件
    QCheckBox *hexDisplayCheck;
    QCheckBox *hexSendCheck;
    QCheckBox *dtrCheck;
    QCheckBox *rtsCheck;
    QCheckBox *autoSendCheck;
    QSpinBox *autoSendIntervalSpin;
    QPushButton *autoSendButton;
    QPushButton *openFileButton;
    QPushButton *sendFileButton;
    QPushButton *saveWindowButton;
    QPushButton *clearWindowButton;
    QPushButton *helpButton;
    QTimer *autoSendTimer;

    // 新增：收发计数显示
    QLabel *txCountLabel;       // 发送字符数标签
    QLabel *rxCountLabel;       // 接收字符数标签
    void updateCountDisplay();  // 更新计数显示

    // 顺序必须与构造函数初始化列表一致（避免 -Wreorder 警告）
    quint64 txByteCount;        // 累计发送字节数
    quint64 rxByteCount;        // 累计接收字节数

#ifdef QT_SERIALPORT_LIB
    QSerialPort *serialPort;
#endif
    bool isConnected;
    bool hexDisplayMode;
    bool hexSendMode;
    QByteArray receiveBuffer;
};

#endif // COM_TEST_DIALOG_H