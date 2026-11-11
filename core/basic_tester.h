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
#include <QThread>
#include <QProcess>

class BenchmarkTester;

class BasicTester : public QObject
{
    Q_OBJECT

public:
    explicit BasicTester(QObject *parent = nullptr);

    void setupUI(QComboBox *combo, QPushButton *button);

public slots:
    void executeTest();
    void executeTestByName(const QString &testName);  // 按名称执行测试
    void startAgingTestAuto();  // 统一控制：自动开始老化
    void stopAgingTest();       // 统一控制：停止老化

private slots:
    void onShutdown();
    void onReboot();
    void onSuspend();
    void onHibernate();
    void onComTest();
    void onUsbSpeedTest();
    void onNetworkSpeedTest();
    void onAgingTest();
    void onAgingTestAutoStart();  // 统一控制：自动老化（跳过设置直接开始）
    void onBenchmarkTest();       // 跑分测试（CPU/GPU/内存/磁盘综合评分）

private:
    bool checkSerialPortRuntimeLibrary();
    void installQtSerialPortLibrary();
    void reconfigureAndCompile();
    QString findProjectFile();
    bool addSerialPortToProjectFile(const QString &proFile);
    bool recompileProject(const QString &proFile);

signals:
    void logMessage(const QString &message);
    void unifiedControlTriggered();  // 统一控制信号

private:
    QComboBox *basicTestCombo;
    QPushButton *executeButton;
    qint64 m_agingPid = 0;  // 老化测试进程PID
    BenchmarkTester *m_benchmark = nullptr;  // 跑分测试模块（懒加载，独立后台线程）
};

#endif // BASIC_TESTER_H
