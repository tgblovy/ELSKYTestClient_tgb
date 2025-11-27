#ifndef AUTO_START_MANAGER_H
#define AUTO_START_MANAGER_H

#include <QObject>
#include <QCheckBox>
#include <QString>  // 确保包含QString

class AutoStartManager : public QObject
{
    Q_OBJECT

public:
    explicit AutoStartManager(QObject *parent = nullptr);

    void setupUI(QCheckBox *checkBox);
    bool setAutoStart(bool enable);
    bool isAutoStartEnabled();

signals:
    void logMessage(const QString &message);
    void autoStartStatusChanged(bool enabled);

private slots:
    void onAutoStartToggled(bool checked);

private:
    bool createSystemdService();
    void safestRemoveSystemdService();
    bool removeSystemdService();
    bool checkServiceStatus();
    void verifyAutoStart();

private:
    QCheckBox *autoStartCheckBox;
    bool isCancellingAutoStart;
};

#endif // AUTO_START_MANAGER_H
