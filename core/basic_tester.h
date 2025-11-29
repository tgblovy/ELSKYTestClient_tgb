#ifndef BASIC_TESTER_H
#define BASIC_TESTER_H

#include <QObject>
#include <QComboBox>
#include <QPushButton>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QElapsedTimer>
#include <QTime>

class BasicTester : public QObject
{
    Q_OBJECT

public:
    explicit BasicTester(QObject *parent = nullptr);
    // 移除析构函数声明，使用默认析构函数

    void setupUI(QComboBox *combo, QPushButton *button);

public slots:
    void executeTest();

private slots:
    void onShutdown();
    void onReboot();
    void onSuspend();
    void onHibernate();
    void onComTest();
    void onUsbSpeedTest();
    void onNetworkSpeedTest();

signals:
    void logMessage(const QString &message);

private:
    QComboBox *basicTestCombo;
    QPushButton *executeButton;
};

#endif // BASIC_TESTER_H
