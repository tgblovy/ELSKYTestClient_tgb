#include "unified_control_manager.h"
#include <QNetworkInterface>
#include <QDateTime>
#include <QDebug>
#include <QSysInfo>
#include <QSettings>

UnifiedControlManager::UnifiedControlManager(QObject *parent)
    : QObject(parent)
    , udpSocket(nullptr)
    , heartbeatTimer(nullptr)
    , cleanupTimer(nullptr)
    , m_currentTag("唐贵宝1")
{
    m_localHostname = QSysInfo::machineHostName();
    m_localIP = getLocalIP();

    udpSocket = new QUdpSocket(this);
    udpSocket->bind(QHostAddress::Any, BROADCAST_PORT, QUdpSocket::ShareAddress);

    connect(udpSocket, &QUdpSocket::readyRead, this, &UnifiedControlManager::onDatagramReceived);

    heartbeatTimer = new QTimer(this);
    heartbeatTimer->setInterval(HEARTBEAT_INTERVAL);
    connect(heartbeatTimer, &QTimer::timeout, this, &UnifiedControlManager::sendHeartbeat);

    cleanupTimer = new QTimer(this);
    cleanupTimer->setInterval(5000);
    connect(cleanupTimer, &QTimer::timeout, this, &UnifiedControlManager::cleanupStalePeers);

    // 从设置中加载标签
    QSettings settings("ELSKY", "TestClient");
    m_currentTag = settings.value("unified_control/tag", "唐贵宝1").toString();

    emit logMessage(QString("统一控制管理器初始化完成, 本机: %1(%2), 标签: %3")
                    .arg(m_localHostname).arg(m_localIP).arg(m_currentTag));
}

UnifiedControlManager::~UnifiedControlManager()
{
    stopDiscovery();
}

void UnifiedControlManager::setCurrentTag(const QString &tag)
{
    if (m_currentTag != tag) {
        m_currentTag = tag;

        // 保存标签设置
        QSettings settings("ELSKY", "TestClient");
        settings.setValue("unified_control/tag", m_currentTag);
        settings.sync();

        // 清除旧的节点列表，因为标签变了
        m_peers.clear();
        emit peersUpdated();

        emit logMessage(QString("标签已更改为: %1").arg(m_currentTag));

        // 如果已经在广播，立即发送一次心跳以通知其他节点
        if (heartbeatTimer->isActive()) {
            sendHeartbeat();
        }
    }
}

QString UnifiedControlManager::getLocalIP() const
{
    // 优先获取非回文的IPv4地址
    QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &addr : addresses) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol &&
            addr != QHostAddress::LocalHost &&
            !addr.isLoopback()) {
            return addr.toString();
        }
    }
    return QHostAddress(QHostAddress::LocalHost).toString();
}

QString UnifiedControlManager::getLocalHostname() const
{
    return m_localHostname;
}

void UnifiedControlManager::startDiscovery()
{
    if (!heartbeatTimer->isActive()) {
        heartbeatTimer->start();
        cleanupTimer->start();
        sendHeartbeat();
        qDebug() << "开始节点发现...";
    }
}

void UnifiedControlManager::stopDiscovery()
{
    heartbeatTimer->stop();
    cleanupTimer->stop();
}

QStringList UnifiedControlManager::getPeers() const
{
    QStringList result;
    QDateTime now = QDateTime::currentDateTime();
    QMap<QString, PeerInfo>::const_iterator it;
    for (it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        if (it.value().tag == m_currentTag &&
            it.value().lastSeen.msecsTo(now) < PEER_TIMEOUT) {
            result.append(it.key());
        }
    }
    return result;
}

QList<PeerInfo> UnifiedControlManager::getPeerList() const
{
    QList<PeerInfo> result;
    QDateTime now = QDateTime::currentDateTime();
    QMap<QString, PeerInfo>::const_iterator it;
    for (it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        if (it.value().tag == m_currentTag &&
            it.value().lastSeen.msecsTo(now) < PEER_TIMEOUT) {
            result.append(it.value());
        }
    }
    return result;
}

QList<PeerInfo> UnifiedControlManager::getAllPeers() const
{
    QList<PeerInfo> result;
    QDateTime now = QDateTime::currentDateTime();
    QMap<QString, PeerInfo>::const_iterator it;
    for (it = m_peers.constBegin(); it != m_peers.constEnd(); ++it) {
        if (it.value().lastSeen.msecsTo(now) < PEER_TIMEOUT) {
            result.append(it.value());
        }
    }
    return result;
}

void UnifiedControlManager::sendCommand(const QString &command, const QStringList &params)
{
    QString message = "CMD|" + m_localIP + "|" + m_currentTag + "|" + command;
    for (const QString &p : params) {
        message += "|" + p;
    }
    sendBroadcast(message);

    emit logMessage(QString("发送统一控制命令: %1 (标签: %2)").arg(command).arg(m_currentTag));
}

void UnifiedControlManager::sendBroadcast(const QString &message)
{
    QByteArray datagram = message.toUtf8();
    udpSocket->writeDatagram(datagram, QHostAddress::Broadcast, BROADCAST_PORT);
}

void UnifiedControlManager::sendHeartbeat()
{
    QString message = QString("HEARTBEAT|%1|%2|%3|%4")
                      .arg(m_localIP)
                      .arg(m_localHostname)
                      .arg(m_currentTag)
                      .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    sendBroadcast(message);
}

void UnifiedControlManager::onDatagramReceived()
{
    while (udpSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(udpSocket->pendingDatagramSize());
        QHostAddress senderAddr;
        quint16 senderPort;

        udpSocket->readDatagram(datagram.data(), datagram.size(), &senderAddr, &senderPort);

        QString message = QString::fromUtf8(datagram).trimmed();
        if (!message.isEmpty()) {
            parseMessage(message, senderAddr);
        }
    }
}

void UnifiedControlManager::parseMessage(const QString &message, const QHostAddress &senderAddr)
{
    Q_UNUSED(senderAddr);
    QStringList parts = message.split("|");
    if (parts.isEmpty()) return;

    QString type = parts[0];

    if (type == "HEARTBEAT") {
        // 格式: HEARTBEAT|ip|hostname|tag|timestamp
        if (parts.size() >= 4) {
            QString peerIP = parts[1];
            // 忽略自己的心跳
            if (peerIP == m_localIP) return;

            PeerInfo info;
            info.ip = peerIP;
            info.hostname = parts.size() > 2 ? parts[2] : peerIP;
            info.tag = parts.size() > 3 ? parts[3] : "";
            info.lastSeen = QDateTime::currentDateTime();

            bool isNew = !m_peers.contains(peerIP) || (m_peers[peerIP].tag != info.tag);

            m_peers[peerIP] = info;

            // 发现新节点和标签变更时通过 qDebug 输出，不刷日志栏
            if (isNew) {
                qDebug() << "发现节点:" << info.hostname << "(" << info.ip << ") 标签:" << info.tag;
            }

            emit peersUpdated();
        }
    } else if (type == "CMD") {
        // 格式: CMD|sender_ip|tag|command|param1|param2|...
        if (parts.size() >= 4) {
            QString senderIP = parts[1];
            QString cmdTag = parts[2];

            // 忽略自己的命令
            if (senderIP == m_localIP) return;

            // 检查标签是否匹配
            if (cmdTag != m_currentTag) return;

            QString command = parts[3];
            QStringList params;
            for (int i = 4; i < parts.size(); ++i) {
                params.append(parts[i]);
            }

            qDebug() << "收到统一控制命令:" << command << "来自:" << senderIP;
            emit commandReceived(command, params, senderIP);
        }
    }
}

void UnifiedControlManager::cleanupStalePeers()
{
    QDateTime now = QDateTime::currentDateTime();
    bool changed = false;

    QMap<QString, PeerInfo>::iterator it = m_peers.begin();
    while (it != m_peers.end()) {
        if (it.value().lastSeen.msecsTo(now) > PEER_TIMEOUT) {
            qDebug() << "节点超时移除:" << it.value().hostname << "(" << it.value().ip << ")";
            it = m_peers.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    if (changed) {
        emit peersUpdated();
    }
}
