#ifndef NETWORK_SPEED_TESTER_H
#define NETWORK_SPEED_TESTER_H

#include <QObject>
#include <QTimer>
#include <QProcess>

class NetworkSpeedTester : public QObject
{
    Q_OBJECT

public:
    explicit NetworkSpeedTester(QObject *parent = nullptr);
    ~NetworkSpeedTester();

    void startTest();
    void stopTest();

signals:
    void testProgress(const QString &message);
    void testFinished(double downloadSpeed, double uploadSpeed);
    void testError(const QString &error);

private slots:
    void onPingFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onSpeedTestFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onTestTimeout();

private:
    QTimer *timeoutTimer;
    QProcess *pingProcess;
    QProcess *speedTestProcess;

    bool isTesting;

    void runSpeedTest();
    void parseSpeedTestResult(const QString &output);
};

#endif // NETWORK_SPEED_TESTER_H
