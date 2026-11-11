#ifndef NETWORK_SPEED_TESTER_H
#define NETWORK_SPEED_TESTER_H

#include <QObject>
#include <QTimer>
#include <QProcess>
#include <QElapsedTimer>

class QTcpServer;
class QTcpSocket;
class QUdpSocket;

// 网络测速器
//
// 三种测速能力（按优先级自动选择，startTest() 零参数版保持旧接口不变）：
//   1. 内网 TCP 双机对测（首选，完全离线可用，不依赖任何外部工具/公网）：
//      局域网内两台机器都运行本软件，其中一台点"网络测速"即可自动发现
//      对端并测出上下行吞吐；对端无需任何操作（本软件启动时即常驻测速服务）。
//   2. speedtest-cli 公网测速（系统已安装该工具时的兼容路径，保留旧逻辑）。
//   3. 都不可用时给出明确指引（不再输出伪造数据）。
//
// 内网对测协议（修复 B12：speedtest-cli 不在统信/Debian 官方源且依赖公网
// speedtest.net 服务器，内网/离线环境"装不上+用不了"，网络测速功能整体不可用；
// 改为 Qt 原生 QTcpServer/QTcpSocket 实现，零外部依赖、完全离线）：
//   TCP 18964（吞吐测试通道）：
//     客户端 → 服务端: "ELSKY_SPEEDTEST_REQ\n"
//     服务端 → 客户端: 16MB 测试数据块（固定 0x5A 填充，不含换行/ASCII）
//     服务端 → 客户端: "ELSKY_SPEEDTEST_DL_DONE\n"        （客户端计时算下行）
//     客户端 → 服务端: 16MB 测试数据块
//     客户端 → 服务端: "ELSKY_SPEEDTEST_UL_DONE\n"        （客户端计时算上行）
//     服务端 → 客户端: "ELSKY_SPEEDTEST_BYE\n"            （结束）
//   UDP 18965（对端发现）：
//     客户端广播: "ELSKY_SPEEDTEST_DISCOVER\n"
//     服务端应答: "ELSKY_SPEEDTEST_HERE <本机IP> <主机名>\n"
//
// 单位约定：testFinished(downloadSpeed, uploadSpeed) 单位为 MB/s，
// 与旧 speedtest-cli 路径保持一致，上层 UI（BasicTester）无需任何改动。
class NetworkSpeedTester : public QObject
{
    Q_OBJECT

public:
    explicit NetworkSpeedTester(QObject *parent = nullptr);
    ~NetworkSpeedTester();

    // 智能模式测速（旧接口，所有既有调用点零改动）：
    //   自动发现内网对端 → TCP 对测；无对端 → speedtest-cli（若已安装）；
    //   都不可用 → 明确报错。内部先做网关连通性检查。
    void startTest();

    // 指定对端 IP 直接进行内网 TCP 对测（统一控制等场景可指定中心机）。
    void startTest(const QString &peerIp);

    void stopTest();

    // ---- 内网测速服务（服务端角色，常驻被动响应）----
    // ClientWindow 构造时调用：监听 TCP 18964 + UDP 18965，
    // 响应局域网内其他机器发起的测速。失败不致命（端口被占用时仅降级，
    // 本机仍可作为客户端去测别人）。
    bool startLanServer();
    void stopLanServer();
    bool isLanServerRunning() const { return lanServer != nullptr; }

    static const quint16 LanTcpPort = 18964;   // TCP 吞吐对测端口
    static const quint16 LanUdpPort = 18965;   // UDP 对端发现端口
    static const qint64  TestBytes  = 16 * 1024 * 1024;  // 单向测试数据量 16MB

signals:
    void testProgress(const QString &message);
    void testFinished(double downloadSpeed, double uploadSpeed);
    void testError(const QString &error);

private slots:
    void onPingFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onSpeedTestFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onTestTimeout();

    // ---- 客户端（发起方）----
    void onDiscoverReadyRead();
    void onDiscoverTimeout();
    void onTestSocketConnected();
    void onTestSocketReadyRead();
    void onTestSocketBytesWritten(qint64 bytes);
    void onTestSocketDisconnected();

    // ---- 服务端（被动方）----
    void onServerDiscoverReadyRead();
    void onLanServerNewConnection();
    void onServerClientReadyRead();
    void onServerClientBytesWritten(qint64 bytes);
    void onServerClientDisconnected();

private:
    QTimer *timeoutTimer;
    QProcess *pingProcess;
    QProcess *speedTestProcess;
    QString pingTarget;   // 连通性测试目标（默认网关，缺省退回 8.8.8.8）

    bool isTesting;

    void runSpeedTest();                    // speedtest-cli 兼容路径（保留）
    void parseSpeedTestResult(const QString &output);

    // ---- 内网对测（客户端）----
    QUdpSocket *discoverSocket;      // UDP 发现（客户端角色）
    QTimer *discoverTimer;           // 发现窗口定时器（2 秒）
    QString discoveredPeerIp;
    QString discoveredPeerName;
    QString forcedPeerIp;            // 指定对端模式（startTest(peerIp)）
    QTcpSocket *testSocket;
    QElapsedTimer transferTimer;     // 传输计时器（值语义，非 QObject）
    qint64 downBytes;                // 下行累计字节数（不含协议标记）
    qint64 upBytesWritten;           // 上行已写出字节累计（含 UL_DONE 标记）
    double downloadSpeedMBs;         // 已测得的下行速度
    double uploadSpeedMBs;           // 已测得上行速度
    QByteArray uploadBlock;          // 上行测试数据块（复用避免反复构造）
    QByteArray tailBuf;              // 协议标记尾缓冲（TCP 分包可能把标记切碎）

    void discoverPeers();                                  // UDP 广播发现对端
    void startLanClientTest(const QString &peerIp, const QString &peerName);
    void finishLanTest();                                  // 汇总上报 testFinished
    void failLanTest(const QString &reason);               // 内网测速失败处理
    static bool isLocalAddress(const QString &ip);         // 判断 IP 是否本机地址

    // ---- 内网对测（服务端）----
    QTcpServer *lanServer;
    QUdpSocket *serverDiscoverSocket;   // UDP 发现应答（服务端角色）
    QTcpSocket *serverClient;           // 当前服务的连接（一次仅一个）
    qint64 serverSentBytes;             // 下行已发送字节
    qint64 serverRecvBytes;             // 上行已接收字节
    int serverPhase;                    // 0=等REQ 1=发下行 2=收上行 3=完成
    QByteArray serverBlock;             // 下行测试数据块（复用）
    QByteArray serverReqBuf;            // REQ 标记重组缓冲
    QByteArray serverTailBuf;           // UL_DONE 标记尾缓冲
};

#endif // NETWORK_SPEED_TESTER_H
