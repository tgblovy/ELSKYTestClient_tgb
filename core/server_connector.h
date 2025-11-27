#ifndef SERVER_CONNECTOR_H
#define SERVER_CONNECTOR_H

#include <QObject>
#include <QTcpSocket>
#include <QCheckBox>
#include <QString>  // 确保包含QString
#include "../common/protocol.h"

class ServerConnector : public QObject
{
    Q_OBJECT

public:
    explicit ServerConnector(QObject *parent = nullptr);
    ~ServerConnector();

    void setupUI(QCheckBox *checkBox);
    void connectToServer(const QString &ip);
    void disconnectFromServer();
    void sendMessage(const NetworkMessage &msg);

    bool isConnected() const { return m_isConnected; }

signals:
    void connectionStatusChanged(bool connected);
    void messageReceived(const QString &message);
    void logMessage(const QString &message);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onErrorOccurred(QAbstractSocket::SocketError error);
    void onServerConnectToggled(bool checked);

private:
    QTcpSocket *tcpSocket;
    QCheckBox *serverCheckBox;
    QString serverIP;
    bool m_isConnected;
};

#endif // SERVER_CONNECTOR_H
