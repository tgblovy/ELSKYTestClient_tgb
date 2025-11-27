#ifndef COM_TEST_DIALOG_H
#define COM_TEST_DIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>

#ifdef QT_SERIALPORT_LIB
#include <QSerialPort>
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
#ifdef QT_SERIALPORT_LIB
    void onReadyRead();
    void onSerialPortError(QSerialPort::SerialPortError error);
#endif

private:
    void setupUI();
    void onSendDataSilent();
    void checkPermissions();
    void setupSerialPortPermissions(const QStringList &portPaths);

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

#ifdef QT_SERIALPORT_LIB
    QSerialPort *serialPort;
#endif
    bool isConnected;
    QByteArray receiveBuffer;
};

#endif // COM_TEST_DIALOG_H
