#ifndef BENCHMARK_TESTER_H
#define BENCHMARK_TESTER_H

#include <QObject>
#include <QString>
#include <QStringList>

class QThread;

/**
 * @brief 跑分模式配置（由主线程 UI 询问用户后填充）
 *
 * 双模式设计：
 *  - 内置模式（离线）：全部使用内置算法，报告标注"内置测评"
 *  - 专业模式（联网）：自动 apt 下载安装缺失工具后运行官方工具评分
 */
struct ProToolsConfig
{
    bool proMode = false;           // 是否专业工具模式
    QStringList packagesToInstall;  // 需 apt 下载安装的包（fio/iozone3/x11perf/glmark2...）
    bool runUnixBench = false;      // 运行 UnixBench（仅当已安装时有效，CPU 子集约10分钟）
};

/**
 * @brief 系统跑分测试模块（独立后台线程运行，不阻塞主界面）
 */
class BenchmarkTester : public QObject
{
    Q_OBJECT

public:
    explicit BenchmarkTester(QObject *parent = nullptr);
    ~BenchmarkTester() override;

    // 启动一次完整跑分（CPU/GPU/内存/磁盘 + 综合分）
    // cfg: 双模式配置（内置模式传默认值即可）
    bool start(const QString &diskPath = QString(),
               const ProToolsConfig &cfg = ProToolsConfig());

    // 是否正在运行
    bool isRunning() const;

    // 请求中断（尽快结束当前子项）
    void requestStop();

    // ---- 工具探测静态接口（供主线程 UI 询问前使用）----
    // 检测网络连通性（ping 公共 DNS，2秒超时）
    static bool checkOnline();
    // 检测已安装的专业工具（返回存在的工具名列表）
    static QStringList detectInstalledTools();
    // UnixBench 安装位置探测（空串=未安装）
    static QString findUnixBench();

signals:
    void progress(const QString &item, int percent);
    void logMessage(const QString &message);
    void finished(const QString &report);
    void failed(const QString &reason);

private:
    void runAll();   // 私有成员函数（不是 slot），在工作线程中执行
    bool installProTools();   // 专业模式：后台安装缺失工具（apt-get，免密sudo）

    ProToolsConfig m_cfg;
    volatile bool m_stopFlag;
    QString m_diskPath;
    QThread *m_thread = nullptr;
};

#endif // BENCHMARK_TESTER_H