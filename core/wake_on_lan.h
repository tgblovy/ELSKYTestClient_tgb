#ifndef WAKE_ON_LAN_H
#define WAKE_ON_LAN_H

#include <QObject>
#include <QPushButton>
#include <QString>
#include "../common/protocol.h"

class ServerConnector;

class WakeOnLan : public QObject
{
    Q_OBJECT

public:
    explicit WakeOnLan(QObject *parent = nullptr);

    void setupUI(QPushButton *button);
    void setServerConnector(ServerConnector *connector) { serverConnector = connector; }
    void loadState();
    void saveState();

signals:
    void logMessage(const QString &message);

private slots:
    void onWakeOnLanToggle();

private:
    QString getLocalMacAddress();
    void setupWakeOnLan();
    void cancelWakeOnLan();

private:
    QPushButton *wolToggleButton;
    ServerConnector *serverConnector;
    bool isWakeOnLanEnabled;
    QString currentMacAddress;
    int currentInterval;
};

#endif // WAKE_ON_LAN_H
