#include "network_speed_tester.h"

#include <QCoreApplication>
#include <QDebug>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QElapsedTimer>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QNetworkAddressEntry>
#include <QSysInfo>

// ---- 内网对测协议标记（\n 结尾；数据块用 0x5A 填充，绝不与 ASCII 标记混淆）----
static const QByteArray kProtoReq       = QByteArray("ELSKY_SPEEDTEST_REQ\n");
static const QByteArray kProtoDlDone    = QByteArray("ELSKY_SPEEDTEST_DL_DONE\n");
static const QByteArray kProtoUlDone    = QByteArray("ELSKY_SPEEDTEST_UL_DONE\n");
static const QByteArray kProtoBye       = QByteArray("ELSKY_SPEEDTEST_BYE\n");
static const QByteArray kProtoDiscover  = QByteArray("ELSKY_SPEEDTEST_DISCOVER");
static const QByteArray kProtoHere      = QByteArray("ELSKY_SPEEDTEST_HERE");

// 获取默认网关地址（离线/内网环境下公网不可达，ping 网关才能真实反映连通性）
static QString defaultGatewayIp()
{
    QProcess p;
    p.start("ip", QStringList() << "route");
    if (!p.waitForStarted(2000)) {
        return QString();
    }
    p.waitForFinished(3000);
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    // 匹配 "default via 192.168.1.1 dev eth0 ..."
    QRegularExpression re("default\\s+via\\s+(\\d+\\.\\d+\\.\\d+\\.\\d+)");
    QRegularExpressionMatch m = re.match(out);
    return m.hasMatch() ? m.captured(1) : QString();
}

// 16MB 固定填充测试块（0x5A 不含换行，与协议标记天然可区分）
static QByteArray makeTestBlock()
{
    QByteArray block(NetworkSpeedTester::TestBytes, '\x5A');
    return block;
}

NetworkSpeedTester::NetworkSpeedTester(QObject *parent)
    : QObject(parent)
    , timeoutTimer(nullptr)
    , pingProcess(nullptr)
    , speedTestProcess(nullptr)
    , isTesting(false)
    // 客户端
    , discoverSocket(nullptr)
    , discoverTimer(nullptr)
    , testSocket(nullptr)

    , downBytes(0)
    , upBytesWritten(0)
    , downloadSpeedMBs(0.0)
    , uploadSpeedMBs(0.0)
    // 服务端
    , lanServer(nullptr)
    , serverDiscoverSocket(nullptr)
    , serverClient(nullptr)
    , serverSentBytes(0)
    , serverRecvBytes(0)
    , serverPhase(0)
{
    timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, this, &NetworkSpeedTester::onTestTimeout);


}

NetworkSpeedTester::~NetworkSpeedTester()
{
    stopTest();
}

// ============================ 测速入口 ============================

void NetworkSpeedTester::startTest(const QString &peerIp)
{
    if (isTesting) {
        return;
    }
    forcedPeerIp = peerIp.trimmed();
    startTest();
}

void NetworkSpeedTester::startTest()
{
    if (isTesting) {
        return;
    }

    isTesting = true;
    downloadSpeedMBs = 0.0;
    uploadSpeedMBs = 0.0;
    emit testProgress("开始网络速度测试...");

    // 连通性提示与测速并行（修复：旧实现 ping 网关串行前置，禁 ICMP/无网关的
    // 内网直连场景会被误杀；ping 结果只作提示，TCP 对测本身才是连通性验证）
    pingProcess = new QProcess(this);
    connect(pingProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &NetworkSpeedTester::onPingFinished);

    // 连通性测试目标（内网/离线环境公网不可达，优先 ping 默认网关）
    // 顺序：默认网关（ip route 解析）→ 缺省退回 8.8.8.8
    pingTarget = defaultGatewayIp();
    if (pingTarget.isEmpty()) {
        pingTarget = QStringLiteral("8.8.8.8");
    }
    pingProcess->start("ping", QStringList() << "-c" << "2" << "-W" << "2" << pingTarget);

    // 不等待 ping 结果，立即进入测速主流程
    if (!forcedPeerIp.isEmpty()) {
        startLanClientTest(forcedPeerIp, forcedPeerIp);
    } else {
        discoverPeers();
    }
}

void NetworkSpeedTester::stopTest()
{
    if (timeoutTimer && timeoutTimer->isActive()) {
        timeoutTimer->stop();
    }

    if (pingProcess && pingProcess->state() == QProcess::Running) {
        pingProcess->kill();
        pingProcess->deleteLater();
        pingProcess = nullptr;
    }

    if (speedTestProcess && speedTestProcess->state() == QProcess::Running) {
        speedTestProcess->kill();
        speedTestProcess->deleteLater();
        speedTestProcess = nullptr;
    }

    // 客户端资源（服务端常驻资源不动）
    if (discoverTimer) {
        discoverTimer->stop();
        discoverTimer->deleteLater();
        discoverTimer = nullptr;
    }
    if (discoverSocket) {
        discoverSocket->deleteLater();
        discoverSocket = nullptr;
    }
    if (testSocket) {
        testSocket->abort();
        testSocket->deleteLater();
        testSocket = nullptr;
    }

    isTesting = false;
}

void NetworkSpeedTester::onPingFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus)

    // ping 结果仅作连通性提示（异步、不阻塞测速主流程）；
    // 流程结束后迟到的 ping 回调只做清理。
    if (exitCode == 0) {
        emit testProgress(QString("✓ 网关连通性正常（%1）").arg(pingTarget));
    } else if (isTesting) {
        emit testProgress(QString("⚠ 网关 ping 不通（%1）——不影响内网对测").arg(pingTarget));
    }

    if (pingProcess) {
        pingProcess->deleteLater();
        pingProcess = nullptr;
    }
}

// ============================ UDP 对端发现 ============================

void NetworkSpeedTester::discoverPeers()
{
    emit testProgress("正在发现局域网测速对端（其他运行本软件的机器）...");

    discoverSocket = new QUdpSocket(this);
    connect(discoverSocket, &QUdpSocket::readyRead, this, &NetworkSpeedTester::onDiscoverReadyRead);

    if (!discoverSocket->bind(QHostAddress(QHostAddress::Any), quint16(0))) {
        emit testProgress("⚠ UDP 绑定失败，跳过内网对端发现");
        runSpeedTest();   // 回退 speedtest-cli 路径
        return;
    }

    // 广播发现包：全局广播 + 各网卡的子网定向广播（部分交换机过滤全局广播）
    QByteArray discover = kProtoDiscover;
    discoverSocket->writeDatagram(discover, QHostAddress::Broadcast, LanUdpPort);
    const QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : ifaces) {
        if (iface.flags() & QNetworkInterface::CanBroadcast) {
            const QList<QNetworkAddressEntry> entries = iface.addressEntries();
            for (const QNetworkAddressEntry &entry : entries) {
                if (!entry.broadcast().isNull() && entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    discoverSocket->writeDatagram(discover, entry.broadcast(), LanUdpPort);
                }
            }
        }
    }

    // 2 秒发现窗口
    discoverTimer = new QTimer(this);
    discoverTimer->setSingleShot(true);
    connect(discoverTimer, &QTimer::timeout, this, &NetworkSpeedTester::onDiscoverTimeout);
    discoverTimer->start(2000);
    timeoutTimer->start(15000); // 总超时兜底（含发现窗口）
}

void NetworkSpeedTester::onDiscoverReadyRead()
{
    if (!discoverSocket) {
        return;
    }
    while (discoverSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(discoverSocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        qint64 read = discoverSocket->readDatagram(datagram.data(), datagram.size(),
                                                   &sender, &senderPort);
        if (read < 0) {
            continue;
        }
        const QString senderIp = sender.toString();
        QString payload = QString::fromUtf8(datagram);
        if (!payload.startsWith(QString::fromUtf8(kProtoHere))) {
            continue;
        }
        // 格式: ELSKY_SPEEDTEST_HERE <ip> <hostname>
        const QStringList parts = payload.trimmed().split(' ');
        if (parts.size() < 2) {
            continue;
        }
        QString peerIp = parts.at(1);
        QString peerName = parts.size() >= 3 ? parts.at(2) : peerIp;
        // 排除本机（自己的服务也会应答广播）
        if (isLocalAddress(peerIp) || isLocalAddress(senderIp)) {
            continue;
        }
        // 第一个外来应答胜出
        if (discoverTimer) {
            discoverTimer->stop();
            discoverTimer->deleteLater();
            discoverTimer = nullptr;
        }
        if (discoverSocket) {
            discoverSocket->deleteLater();
            discoverSocket = nullptr;
        }
        emit testProgress(QString("✓ 发现测速对端: %1 (%2)").arg(peerName, peerIp));
        startLanClientTest(peerIp, peerName);
        return;
    }
}

void NetworkSpeedTester::onDiscoverTimeout()
{
    if (discoverTimer) {
        discoverTimer->stop();
        discoverTimer->deleteLater();
        discoverTimer = nullptr;
    }
    if (discoverSocket) {
        discoverSocket->deleteLater();
        discoverSocket = nullptr;
    }
    emit testProgress("未发现内网测速对端，尝试 speedtest-cli 公网测速...");
    runSpeedTest();   // 回退 speedtest-cli 路径（未安装时其内部会给出明确指引）
}

bool NetworkSpeedTester::isLocalAddress(const QString &ip)
{
    if (ip.isEmpty() || ip == "::" || ip == "0.0.0.0") {
        return false;
    }
    const QList<QHostAddress> all = QNetworkInterface::allAddresses();
    for (const QHostAddress &addr : all) {
        if (addr.toString() == ip) {
            return true;
        }
    }
    return false;
}

// ============================ 客户端：TCP 对测 ============================

void NetworkSpeedTester::startLanClientTest(const QString &peerIp, const QString &peerName)
{
    Q_UNUSED(peerName);

    if (!isTesting) {
        return;   // 流程已死（如 ping 超时后残留回调），不再发起
    }

    emit testProgress(QString("连接对端测速服务 %1:%2 ...").arg(peerIp).arg(LanTcpPort));

    downBytes = 0;
    upBytesWritten = 0;
    if (uploadBlock.isEmpty()) {
        uploadBlock = makeTestBlock();
    }

    testSocket = new QTcpSocket(this);
    connect(testSocket, &QTcpSocket::connected, this, &NetworkSpeedTester::onTestSocketConnected);
    connect(testSocket, &QTcpSocket::readyRead, this, &NetworkSpeedTester::onTestSocketReadyRead);
    connect(testSocket, &QTcpSocket::bytesWritten, this, &NetworkSpeedTester::onTestSocketBytesWritten);
    connect(testSocket, &QTcpSocket::disconnected, this, &NetworkSpeedTester::onTestSocketDisconnected);
    // 兼容 Qt5.11 的 error(QAbstractSocket::SocketError) 信号形式
    connect(testSocket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, [this, peerIp](QAbstractSocket::SocketError) {
        if (isTesting) {
            failLanTest(QString("连接对端 %1 失败（对方未运行本软件或端口 %2 不通）")
                            .arg(peerIp).arg(LanTcpPort));
        }
    });

    testSocket->connectToHost(QHostAddress(peerIp), LanTcpPort);
    timeoutTimer->start(60000); // 60 秒总超时
}

void NetworkSpeedTester::onTestSocketConnected()
{
    if (!testSocket) {
        return;
    }
    emit testProgress("✓ 已连接对端，请求开始下行测试（16MB）...");
    testSocket->write(kProtoReq);
}

void NetworkSpeedTester::onTestSocketReadyRead()
{
    if (!testSocket) {
        return;
    }
    const QByteArray data = testSocket->readAll();

    // ---- 上行阶段：收到的只可能是 BYE 标记 ----
    if (downloadSpeedMBs > 0.0) {
        tailBuf.append(data);
        if (tailBuf.endsWith(kProtoBye)) {
            tailBuf.clear();
            finishLanTest();
        } else if (tailBuf.size() > 128) {
            tailBuf = tailBuf.right(128);
        }
        return;
    }

    // ---- 下行阶段：累计字节数，尾缓冲重组 DL_DONE 标记 ----
    // TCP 流式传输可能把 27 字节的标记切碎到多次 readyRead（10+17 等），
    // 用尾缓冲拼接后用 endsWith 判定，命中时把此前误计为数据的前半段扣回。
    tailBuf.append(data);
    if (tailBuf.endsWith(kProtoDlDone)) {
        downBytes += data.size() - kProtoDlDone.size();
        const qint64 elapsedMs = transferTimer.elapsed();
        downloadSpeedMBs = elapsedMs > 0
            ? double(downBytes) / (1024.0 * 1024.0) / (double(elapsedMs) / 1000.0)
            : 0.0;
        emit testProgress(QString("✓ 下行完成: %1 MB/s（%2 MB / %3 ms）")
                              .arg(downloadSpeedMBs, 0, 'f', 2)
                              .arg(downBytes / (1024 * 1024)).arg(elapsedMs));
        tailBuf.clear();

        // ---- 切换到上行阶段：数据块与完成标记一并写出（Qt 保序发送）----
        emit testProgress("开始上行测试（16MB）...");
        upBytesWritten = 0;
        transferTimer.restart();
        testSocket->write(uploadBlock);
        testSocket->write(kProtoUlDone);
        return;
    }

    // 下行普通数据块（首个数据块启动计时）
    if (downBytes == 0) {
        transferTimer.restart();
    }
    downBytes += data.size();
    if (tailBuf.size() > 128) {
        tailBuf = tailBuf.right(128);
    }
}

void NetworkSpeedTester::onTestSocketBytesWritten(qint64 bytes)
{
    if (!testSocket) {
        return;
    }
    // 下行未完成前的写出（REQ 标记等）不计入上行统计
    if (downloadSpeedMBs <= 0.0) {
        return;
    }
    upBytesWritten += bytes;
    // 数据块 + UL_DONE 标记全部写出完成 → 上行测速结束
    if (upBytesWritten >= TestBytes + kProtoUlDone.size()) {
        const qint64 elapsedMs = transferTimer.elapsed();
        uploadSpeedMBs = elapsedMs > 0
            ? double(TestBytes) / (1024.0 * 1024.0) / (double(elapsedMs) / 1000.0)
            : 0.0;
        emit testProgress(QString("✓ 上行完成: %1 MB/s（16MB / %2 ms）")
                              .arg(uploadSpeedMBs, 0, 'f', 2).arg(elapsedMs));
        // 服务端会回 BYE，但标记已全部发出、速度已测得，直接收尾
        // （等 BYE 仅为协议完整性，不阻塞结果上报）
        finishLanTest();
    }
}

void NetworkSpeedTester::onTestSocketDisconnected()
{
    if (isTesting && testSocket) {
        // 非正常收尾的断开（正常收尾在 finishLanTest 里先停标志）
        failLanTest("对端连接中断");
    }
}

void NetworkSpeedTester::finishLanTest()
{
    if (!isTesting) {
        return;
    }
    if (timeoutTimer && timeoutTimer->isActive()) {
        timeoutTimer->stop();
    }
    if (testSocket) {
        testSocket->disconnect(this);   // 防止析构路径再触发槽
        testSocket->abort();
        testSocket->deleteLater();
        testSocket = nullptr;
    }
    isTesting = false;
    emit testProgress("✓ 内网对测完成");
    emit testFinished(downloadSpeedMBs, uploadSpeedMBs);
}

void NetworkSpeedTester::failLanTest(const QString &reason)
{
    if (!isTesting) {
        return;
    }
    if (timeoutTimer && timeoutTimer->isActive()) {
        timeoutTimer->stop();
    }
    if (testSocket) {
        testSocket->disconnect(this);
        testSocket->abort();
        testSocket->deleteLater();
        testSocket = nullptr;
    }
    isTesting = false;
    emit testError(reason);
}

// ============================ 服务端：常驻测速服务 ============================

bool NetworkSpeedTester::startLanServer()
{
    if (lanServer) {
        return true;   // 已在运行
    }

    // UDP 发现应答
    serverDiscoverSocket = new QUdpSocket(this);
    connect(serverDiscoverSocket, &QUdpSocket::readyRead, this, &NetworkSpeedTester::onServerDiscoverReadyRead);
    if (!serverDiscoverSocket->bind(QHostAddress(QHostAddress::Any), LanUdpPort,
                                    QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning() << "[内网测速] UDP" << LanUdpPort << "绑定失败（可能被占用），"
                   << "本机无法被其他机器自动发现，但仍可作为发起方测速";
        serverDiscoverSocket->deleteLater();
        serverDiscoverSocket = nullptr;
    }

    // TCP 吞吐服务
    lanServer = new QTcpServer(this);
    connect(lanServer, &QTcpServer::newConnection, this, &NetworkSpeedTester::onLanServerNewConnection);
    if (!lanServer->listen(QHostAddress(QHostAddress::Any), LanTcpPort)) {
        qWarning() << "[内网测速] TCP" << LanTcpPort << "监听失败（可能被占用），"
                   << "本机无法作为被测端，但仍可作为发起方测速";
        lanServer->deleteLater();
        lanServer = nullptr;
        return false;
    }

    if (serverBlock.isEmpty()) {
        serverBlock = makeTestBlock();
    }
    qDebug() << "[内网测速] 服务已启动: TCP" << LanTcpPort << "/ UDP" << LanUdpPort
             << "（局域网内其他机器可对本机发起测速）";
    return true;
}

void NetworkSpeedTester::stopLanServer()
{
    if (lanServer) {
        lanServer->close();
        lanServer->deleteLater();
        lanServer = nullptr;
    }
    if (serverDiscoverSocket) {
        serverDiscoverSocket->deleteLater();
        serverDiscoverSocket = nullptr;
    }
    if (serverClient) {
        serverClient->disconnect(this);
        serverClient->abort();
        serverClient->deleteLater();
        serverClient = nullptr;
    }
}

void NetworkSpeedTester::onServerDiscoverReadyRead()
{
    if (!serverDiscoverSocket) {
        return;
    }
    while (serverDiscoverSocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(serverDiscoverSocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        qint64 read = serverDiscoverSocket->readDatagram(datagram.data(), datagram.size(),
                                                         &sender, &senderPort);
        if (read < 0) {
            continue;
        }
        if (!datagram.startsWith(kProtoDiscover)) {
            continue;
        }
        if (sender.isNull()) {
            continue;
        }
        // 选择与发送方同网段的本机 IPv4 应答（多网卡时保证对端能连回来）
        QString localIp;
        const QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
        for (const QNetworkInterface &iface : ifaces) {
            const QList<QNetworkAddressEntry> entries = iface.addressEntries();
            for (const QNetworkAddressEntry &entry : entries) {
                const QHostAddress ip = entry.ip();
                if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isLoopback()) {
                    continue;
                }
                if (localIp.isEmpty()) {
                    localIp = ip.toString();   // 兜底：任意非环回 IPv4
                }
                // 同 /24 网段优先（绝大多数测试局域网为 C 段）
                quint32 ip4 = ip.toIPv4Address();
                quint32 peer4 = sender.toIPv4Address();
                if (peer4 != 0 && (ip4 >> 24) == (peer4 >> 24)) {
                    localIp = ip.toString();
                }
            }
        }
        if (localIp.isEmpty()) {
            continue;
        }
        QByteArray here = kProtoHere;
        here += ' ';
        here += localIp.toUtf8();
        here += ' ';
        here += QSysInfo::machineHostName().toUtf8();
        serverDiscoverSocket->writeDatagram(here, sender, senderPort);
    }
}

void NetworkSpeedTester::onLanServerNewConnection()
{
    if (!lanServer) {
        return;
    }
    while (lanServer->hasPendingConnections()) {
        QTcpSocket *next = lanServer->nextPendingConnection();
        if (!next) {
            continue;
        }
        if (serverClient && serverClient->state() == QAbstractSocket::ConnectedState) {
            // 一次仅服务一个测速连接（并发测速会互相干扰结果）
            next->disconnectFromHost();
            continue;
        }
        serverClient = next;
        serverSentBytes = 0;
        serverRecvBytes = 0;
        serverPhase = 0;   // 0=等待 REQ
        connect(serverClient, &QTcpSocket::readyRead, this, &NetworkSpeedTester::onServerClientReadyRead);
        connect(serverClient, &QTcpSocket::bytesWritten, this, &NetworkSpeedTester::onServerClientBytesWritten);
        connect(serverClient, &QTcpSocket::disconnected, this, &NetworkSpeedTester::onServerClientDisconnected);
        qDebug() << "[内网测速] 接受测速连接:" << serverClient->peerAddress().toString();
    }
}

void NetworkSpeedTester::onServerClientReadyRead()
{
    if (!serverClient) {
        return;
    }
    const QByteArray data = serverClient->readAll();

    if (serverPhase == 0) {
        // 等待 REQ（标记可能被 TCP 分包切碎，用缓冲重组，上限防垃圾连接撑爆）
        serverReqBuf.append(data);
        if (serverReqBuf.contains(kProtoReq)) {
            serverPhase = 1;   // 下行发送阶段
            serverSentBytes = 0;
            serverReqBuf.clear();
            serverClient->write(serverBlock);
        } else if (serverReqBuf.size() > 128) {
            serverReqBuf.clear();   // 超长仍无 REQ：非本协议客户端，忽略数据
        }
        return;
    }

    if (serverPhase == 1) {
        // 下行发送阶段：TCP 保序，客户端上行数据必然在 DL_DONE 之后到达，
        // 此阶段收到的一切数据按协议不可能出现（违规客户端），直接丢弃。
        return;
    }

    if (serverPhase == 2) {
        // 上行接收阶段：累计字节数，尾缓冲重组 UL_DONE 标记（防分包切碎）
        serverTailBuf.append(data);
        if (serverTailBuf.endsWith(kProtoUlDone)) {
            serverRecvBytes += data.size() - kProtoUlDone.size();
            serverClient->write(kProtoBye);   // 回 BYE，写完由 disconnected 收尾
            serverPhase = 3;
            serverTailBuf.clear();
            qDebug() << "[内网测速] 服务完成: 下行" << (serverSentBytes / 1024 / 1024)
                     << "MB / 上行" << (serverRecvBytes / 1024 / 1024) << "MB";
        } else {
            serverRecvBytes += data.size();
            if (serverTailBuf.size() > 128) {
                serverTailBuf = serverTailBuf.right(128);
            }
        }
    }
}

void NetworkSpeedTester::onServerClientBytesWritten(qint64 bytes)
{
    if (!serverClient) {
        return;
    }
    if (serverPhase != 1) {
        return;
    }
    serverSentBytes += bytes;
    if (serverSentBytes >= TestBytes) {
        // 数据块发完，补发下行完成标记，切到接收上行阶段
        serverClient->write(kProtoDlDone);
        serverPhase = 2;
    }
}

void NetworkSpeedTester::onServerClientDisconnected()
{
    if (serverClient) {
        serverClient->disconnect(this);
        serverClient->deleteLater();
        serverClient = nullptr;
    }
}

// ============================ speedtest-cli 兼容路径（保留） ============================

void NetworkSpeedTester::runSpeedTest()
{
    emit testProgress("开始测速...");

    // 检查是否安装了speedtest-cli
    QProcess checkProcess;
    checkProcess.start("which", QStringList() << "speedtest-cli");
    checkProcess.waitForFinished(3000);

    if (checkProcess.exitCode() != 0) {
        // 未安装 speedtest-cli：内网对测（首选）不可用 + 公网工具缺失，给出明确指引
        // （修复：旧实现在无 speedtest-cli 时返回随机伪造数据（5~15MB/s），
        //  测试工具展示编造的网速会严重误导测试结论。改为明确报错并指引。）
        emit testError("网络测速不可用：\n"
                       "① 内网对测：需要局域网内至少另一台机器运行本软件（自动发现，无需操作）；\n"
                       "② speedtest-cli：未安装且需要公网（统信/麒麟官方源均无此包）。\n"
                       "离线环境请采用方式①，联网环境可 pip 安装 speedtest-cli 后重试。");
        isTesting = false;
    } else {
        // 如果安装了speedtest-cli，使用它进行测速
        emit testProgress("使用speedtest-cli进行测速...");

        speedTestProcess = new QProcess(this);
        connect(speedTestProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &NetworkSpeedTester::onSpeedTestFinished);

        speedTestProcess->start("speedtest-cli", QStringList() << "--simple");
        timeoutTimer->start(60000); // 60秒超时
    }
}

void NetworkSpeedTester::onSpeedTestFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus)

    if (timeoutTimer->isActive()) {
        timeoutTimer->stop();
    }

    if (exitCode == 0 && speedTestProcess) {
        QString output = speedTestProcess->readAllStandardOutput();
        emit testProgress("✓ 测速完成");
        parseSpeedTestResult(output);
    } else {
        // 修复：测速失败时只报错，不再上报伪造网速（旧实现回填 8.5/3.2 假数据，
        // 会污染测试报告，与 runSpeedTest 中"明确报错"的策略自相矛盾）
        emit testError(QString("测速失败（speedtest-cli 异常退出，退出码 %1）").arg(exitCode));
    }

    if (speedTestProcess) {
        speedTestProcess->deleteLater();
        speedTestProcess = nullptr;
    }

    stopTest();
}

void NetworkSpeedTester::parseSpeedTestResult(const QString &output)
{
    double downloadSpeed = 0.0;
    double uploadSpeed = 0.0;

    // 解析speedtest-cli的输出
    QRegularExpression downloadRegex("Download:\\s+([\\d.]+)\\s+([KM]?bit/s)");
    QRegularExpression uploadRegex("Upload:\\s+([\\d.]+)\\s+([KM]?bit/s)");

    QRegularExpressionMatch downloadMatch = downloadRegex.match(output);
    QRegularExpressionMatch uploadMatch = uploadRegex.match(output);

    if (downloadMatch.hasMatch()) {
        QString speedStr = downloadMatch.captured(1);
        QString unit = downloadMatch.captured(2);
        downloadSpeed = speedStr.toDouble();

        // 转换为MB/s
        if (unit == "Kbit/s") {
            downloadSpeed = downloadSpeed / 8000.0; // Kbit/s to MB/s
        } else if (unit == "Mbit/s") {
            downloadSpeed = downloadSpeed / 8.0; // Mbit/s to MB/s
        } else if (unit == "Gbit/s") {
            downloadSpeed = downloadSpeed * 125.0; // Gbit/s to MB/s
        }
    }

    if (uploadMatch.hasMatch()) {
        QString speedStr = uploadMatch.captured(1);
        QString unit = uploadMatch.captured(2);
        uploadSpeed = speedStr.toDouble();

        // 转换为MB/s
        if (unit == "Kbit/s") {
            uploadSpeed = uploadSpeed / 8000.0; // Kbit/s to MB/s
        } else if (unit == "Mbit/s") {
            uploadSpeed = uploadSpeed / 8.0; // Mbit/s to MB/s
        } else if (unit == "Gbit/s") {
            uploadSpeed = uploadSpeed * 125.0; // Gbit/s to MB/s
        }
    }

    // 修复：旧实现在解析失败时回填伪造值 8.5/3.2，掩盖真实失败。
    // 解析不到有效数据时明确报错，不输出假网速。
    if (downloadSpeed < 0.01 && uploadSpeed < 0.01) {
        emit testError("测速结果解析失败，未获取到有效网速数据");
        return;
    }
    if (downloadSpeed < 0.01) downloadSpeed = 0.0;
    if (uploadSpeed < 0.01) uploadSpeed = 0.0;

    emit testFinished(downloadSpeed, uploadSpeed);
}

void NetworkSpeedTester::onTestTimeout()
{
    if (!isTesting) {
        return;   // 已结束的测试忽略迟到超时（防竞态重复报错）
    }
    emit testError("测速超时");
    stopTest();
}
