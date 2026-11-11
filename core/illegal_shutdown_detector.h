#ifndef ILLEGAL_SHUTDOWN_DETECTOR_H
#define ILLEGAL_SHUTDOWN_DETECTOR_H

#include <QObject>
#include <QDateTime>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTimer>

// 关机类型枚举
enum ShutdownType {
    SHUTDOWN_NORMAL,      // 正常关机/重启
    SHUTDOWN_ILLEGAL,     // 非法关机（断电、强制关机等）
    SHUTDOWN_APP_CRASH,   // 程序异常退出（崩溃/被强制结束，区别于系统非法关机）
    SHUTDOWN_NETWORK_WAKE,// 网络唤醒(WoL)
    SHUTDOWN_SUSPEND_RESUME, // 休眠/恢复
    SHUTDOWN_UNKNOWN      // 未知类型
};

class IllegalShutdownDetector : public QObject
{
    Q_OBJECT

public:
    explicit IllegalShutdownDetector(QObject *parent = nullptr);
    virtual ~IllegalShutdownDetector();

    // 检测非法关机
    QPair<QDateTime, QString> detectIllegalShutdown(const QDateTime &sinceTime = QDateTime());

    // 设置上次检测时间
    void setLastCheckTime(const QDateTime &time);
    QDateTime getLastCheckTime() const;

    // 获取最后检测到的日志
    QString getLastDetectedLog() const;

    // 保存/加载检测时间
    void saveCheckTime();
    void loadCheckTime();

    // 保存检测到的日志到文件
    void saveDetectedLogToFile(const QString &log);

    // 保存和加载非法关机状态
    void saveIllegalShutdownState(const QDateTime &time, const QString &log);
    void loadIllegalShutdownState();
    void clearIllegalShutdownState();  // 清除非法关机状态（用户手动清除时调用）

    // 检查是否有未处理的非法关机记录
    bool hasPendingIllegalShutdown() const;

    // 非法关机计数相关函数
    int getIllegalShutdownCount() const;  // 获取非法关机次数
    void incrementIllegalShutdownCount(); // 增加非法关机次数（只有当非法关机时间变化时调用）
    void resetIllegalShutdownCount();     // 重置非法关机次数
    void saveIllegalShutdownCount();      // 保存非法关机次数
    void loadIllegalShutdownCount();      // 加载非法关机次数

    // 获取最近记录的非法关机时间
    QDateTime getLastRecordedShutdownTime() const;

    // ========== 标志文件管理（静态方法，供 SystemController 等模块调用） ==========
    static QString getRunningFlagPath();     // 获取运行标志文件路径
    static void markCleanShutdown();         // 标记正常关机（删除运行标志文件）

    // ========== 计划内电源操作标记（2026-09-22 循环重启误判修复） ==========
    // 背景：循环重启场景下，markCleanShutdown() 删除 running.flag 与真正掉电之间
    //       存在 10~60 秒窗口（sudo 提权等待 + systemd 关机流程）。窗口内标志若被
    //       二次创建/删除失败残留，下次启动将误判非法关机；且"标志存在"分支的
    //       journalctl/last -x 仲裁在统信/麒麟上常因无权限/日志易失/wtmp 不全而
    //       降级到"宁可误报"兜底 → 概率性误判。
    // 机制：reboot()/shutdown() 发命令前写 planned_power.marker（跨启动周期凭据，
    //       双 fsync 落盘）；下次启动 determineShutdownType() 最高优先级消费该凭据
    //       → 确定性判定"计划内重启/关机"，与标志文件删除成败、系统证据可用性解耦。
    // 防残留：boot_id 属当前周期（命令未实际执行/失败）时不清除（可能是另一实例
    //       挂起中的操作），仅年龄 > 600 秒才按失败残留清除。
    static QString getPlannedPowerMarkerPath();   // 标记文件路径（与 running.flag 同目录）
    static void markPlannedPower(const QString &intent);  // 写标记（intent: "reboot"/"shutdown"）
    static bool checkPlannedPowerMarker(QString *intent = nullptr,
                                        QDateTime *markerTime = nullptr,
                                        QString *bootId = nullptr);   // 读标记
    static void clearPlannedPowerMarker();        // 清除标记
    static void restoreRunningFlagAfterPowerAbort(); // 电源命令失败后重建运行标志（消除漏报窗口）

    // ========== 休眠标记管理（供 SystemController 在 hibernate 前调用） ==========
    // 背景：hibernate（休眠到磁盘）= 内存镜像写入 swap 后直接断电，
    //       journalctl 上个启动周期不会有正常关机流程（无 Stopping Session 等），
    //       唤醒后即便标志文件已删除，"系统证据仲裁"仍会误判为非法关机。
    // 机制：hibernate() 发命令前调用 markHibernate() 写入 marker 文件；
    //       唤醒后 determineShutdownType() 第零层检测到 marker（跨启动周期）
    //       即判定"软件主动休眠恢复"，不计入非法关机。
    // 注意：suspend（待机到内存）**不写** marker —— 待机中意外断电
    //       属于应当检出的非法关机场景，不能被 marker 掩盖。
    static void markHibernate();        // 写休眠标记（hibernate 命令发出前调用）
    static bool checkHibernateMarker(QDateTime *markerTime = nullptr);  // 读休眠标记（存在返回true）
    static void clearHibernateMarker(); // 清除休眠标记（检测消费后/残留清理）

    // ========== suspend 断电检测标记（2026-09-11 飞腾真机新增）==========
    // 背景：用户在 suspend 期间直接断电 → boot_id 变 + running.flag 已被 markCleanShutdown 删
    //       → 现有检测看不到任何"软件曾运行"痕迹,会漏判"非法关机"。
    // 机制：suspend() 写 suspend.marker(记录 suspend 命令发出时刻);
    //       启动时若 marker 存在 + bootTime - markerTime < 阈值(60s) → "suspend 期间被断电"
    //       → 判定非法关机。若 bootTime - markerTime ≥ 阈值,说明用户正常唤醒后正常关机,清除 marker。
    // 阈值依据:BIOS POST + GRUB + kernel init 一般 20~40s,留 60s 缓冲避免误报。
    static void markSuspend();                // 写 suspend 标记(suspend 命令发出前调用)
    static bool checkSuspendMarker(QDateTime *markerTime = nullptr);  // 读 suspend 标记
    static void clearSuspendMarker();         // 清除 suspend 标记

    // ========== 跨架构稳定性增强 ==========
    static QString getCurrentBootId();       // 获取当前启动周期标识（/proc/sys/kernel/random/boot_id，每次开机唯一）
#ifdef Q_OS_LINUX
    static bool installTerminationHandlers(); // 安装SIGTERM/SIGINT处理器（防止桌面环境直接发信号杀进程导致误报）
#endif

    // ========== 历史日志管理 ==========
    QStringList getAllShutdownLogFiles();    // 获取所有非法关机日志文件路径（按时间排序）
    void clearAllShutdownLogFiles();         // 删除所有非法关机日志文件（清除记录时调用）

signals:
    void illegalShutdownDetected(const QDateTime &shutdownTime, const QString &logInfo);
    void illegalShutdownStateRestored(const QDateTime &shutdownTime, const QString &logInfo);
    void illegalShutdownCountChanged(int count); // 非法关机次数变化信号
    void appCrashDetected(const QString &logInfo); // 程序异常退出信号（崩溃/被强制结束，非系统非法关机）

public slots:
    void checkOnStartup();

private:
    QDateTime lastCheckTime;
    QString lastDetectedLog;

    // 持久化存储的非法关机状态
    QDateTime persistentShutdownTime;
    QString persistentShutdownLog;

    // 非法关机计数器
    int illegalShutdownCount;

    // 最近记录的非法关机时间（用于判断是否为同一个事件）
    QDateTime lastRecordedShutdownTime;

    // 检测结果缓存
    bool m_cachedHasRecovery = false;
    QString m_cachedRecoveryLogs;
    ShutdownType m_cachedShutdownType = SHUTDOWN_UNKNOWN;
    bool m_flagBasedDetection = false;   // 本次判定是否以标志文件为主证据

    // 私有方法声明
    // 注意：getSystemBootTime() 不依赖任何成员变量（纯读 uptime 命令 / /proc/uptime），
    //       声明为 static 以便被 restoreRunningFlagAfterPowerAbort() 等静态方法调用；
    //       静态成员函数同样可在实例方法内直接调用（现有调用点均不受影响）。
    static QDateTime getSystemBootTime();
    QDateTime parseShutdownTimeFromRecoveryLogs(const QString &logs);
    ShutdownType determineShutdownType();    // 综合判断关机类型（标志文件+boot_id+系统证据仲裁）
    QString getShutdownReasonLog();          // 获取关机原因的详细日志

    // ========== 标志文件机制（主要检测方法） ==========
    QString m_runningFlagPath;                       // 运行标志文件路径
    bool checkRunningFlag(QString *info = nullptr);  // 检查运行标志文件（解析boot_id/最后活跃时间）
    void createRunningFlag();                        // 创建/原位重写运行标志文件（保持inode，无删除-重建间隙）
    void refreshRunningFlag();                       // 定期刷新标志文件的last_alive_time（缩小断电时间估算窗口）

    // 从标志文件解析出的信息
    QString m_flagBootId;       // 标志文件记录的boot_id（判断是否属于当前启动周期）
    QDateTime m_flagLastAlive;  // 标志文件记录的最后活跃时间（断电时间的最近下界）
    QDateTime m_flagBootTime;   // 标志文件记录的系统启动时间
    QString m_currentBootId;    // 当前启动周期的boot_id（缓存）
    QTimer *m_aliveTimer;       // 定期刷新标志文件时间戳的定时器（60秒）

    // ========== 系统级检测方法（辅助验证/仲裁） ==========
    // journalctl 检查指定启动周期是否正常关机（bootId为空时检查上一次启动）
    // reliable=true表示journalctl有输出且做出了可靠判断（false=无权限/日志易失/超时，能力降级）
    bool checkLastBootCleanShutdown(QString *logInfo = nullptr, bool *reliable = nullptr, const QString &bootId = QString());
    // last -x 检查当前启动前是否有shutdown记录（reliable=true表示做出了可靠判断）
    // shutdownEndTime：输出最近一条shutdown记录的结束时间（供陈旧性校验）
    // flagBootTime非空时：精确定位该启动周期在wtmp中的记录并检查其结束方式
    bool checkNormalShutdownByLast(bool *reliable = nullptr, QDateTime *shutdownEndTime = nullptr, const QDateTime &flagBootTime = QDateTime());
    // dmesg 检查本次启动是否存在文件系统恢复记录（无journalctl/wtmp环境的兜底证据）
    bool checkDmesgFilesystemRecovery(QString *logInfo = nullptr);

    // 工具函数
    void saveAppCrashLog(const QString &log);                        // 保存程序崩溃日志（不计入非法关机）
    static QString parseFlagValue(const QString &content, const QString &key);  // 解析key=value
    static void fsyncParentDir(const QString &filePath);             // 同步文件所在目录（保证目录项落盘）
    static QString getHibernateMarkerPath();                         // 获取休眠标记文件路径（与running.flag同目录）
    static QString getSuspendMarkerPath();                           // 获取 suspend 标记文件路径（同上目录）
};

#endif // ILLEGAL_SHUTDOWN_DETECTOR_H
