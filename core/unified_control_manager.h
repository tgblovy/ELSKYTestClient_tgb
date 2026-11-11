#ifndef UNIFIED_CONTROL_MANAGER_H
#define UNIFIED_CONTROL_MANAGER_H

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QHostAddress>
#include <QMap>
#include <QStringList>
#include <QDateTime>

// 对等节点信息
struct PeerInfo {
    QString ip;
    QString hostname;
    QString tag;
    QDateTime lastSeen;
};

class UnifiedControlManager : public QObject
{
    Q_OBJECT

public:
    explicit UnifiedControlManager(QObject *parent = nullptr);
    ~UnifiedControlManager();

    void setCurrentTag(const QString &tag);
    QString currentTag() const { return m_currentTag; }

    // 获取同标签的节点列表
    QStringList getPeers() const;
    QList<PeerInfo> getPeerList() const;

    // 获取所有发现的节点
    QList<PeerInfo> getAllPeers() const;

    // 发送命令到同标签的所有节点
    void sendCommand(const QString &command, const QStringList &params = QStringList());

    // 获取本机IP
    QString getLocalIP() const;
    // 获取本机主机名
    QString getLocalHostname() const;

    void startDiscovery();
    void stopDiscovery();

signals:
    void peersUpdated();
    void commandReceived(const QString &command, const QStringList &params, const QString &senderIP);
    void logMessage(const QString &message);

private slots:
    void onDatagramReceived();
    void sendHeartbeat();
    void cleanupStalePeers();

private:
    void parseMessage(const QString &message, const QHostAddress &senderAddr);
    void sendBroadcast(const QString &message);

    QUdpSocket *udpSocket;
    QTimer *heartbeatTimer;
    QTimer *cleanupTimer;

    QString m_currentTag;
    QString m_localIP;
    QString m_localHostname;
    QMap<QString, PeerInfo> m_peers;  // IP -> PeerInfo

    static const quint16 BROADCAST_PORT = 19998;
    static const int HEARTBEAT_INTERVAL = 3000;   // 3秒
    static const int PEER_TIMEOUT = 10000;        // 10秒超时
};

#endif // UNIFIED_CONTROL_MANAGER_H
