#ifndef BASIC_TESTER_H
#define BASIC_TESTER_H

#include <QObject>
#include <QComboBox>
#include <QPushButton>
#include <QString>  // 确保包含QString

class BasicTester : public QObject
{
    Q_OBJECT

public:
    explicit BasicTester(QObject *parent = nullptr);

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

signals:
    void logMessage(const QString &message);

private:
    QComboBox *basicTestCombo;
    QPushButton *executeButton;
};

#endif // BASIC_TESTER_H
