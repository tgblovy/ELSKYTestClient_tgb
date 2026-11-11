#include "illegal_shutdown_detector.h"
#include <QProcess>
#include <QFile>
#include <QTextStream>
#include <QSettings>
#include <QFileInfo>
#ifdef Q_OS_LINUX
#include <unistd.h>  // for fsync()
#include <signal.h>  // for sigaction
#include <fcntl.h>   // for open()
#endif
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QMap>
#include <QCoreApplication>
#include <QTimer>

// =================================================================
// ========== SIGTERM/SIGINT 信号处理（防止桌面环境直杀进程误报） ==========
// =================================================================
// 背景：统信DDE等桌面环境关机时可能直接向进程发送SIGTERM终止进程
// （不触发closeEvent，aboutToQuit也不会执行——Qt默认不捕获Unix信号），
// 导致运行标志文件残留，下次启动被误判为非法关机。
// 方案：信号处理器内直接 unlink 标志文件（unlink 是 POSIX
// async-signal-safe 函数，不经过Qt事件循环，各架构/发行版行为一致），
// 随后恢复默认处理并重新触发信号让进程终止。
// 注意：::sync() 并非 POSIX async-signal-safe（2026-09-22 修正，旧版误用），
//       慢 IO/NFS 上可能在信号处理器内阻塞，导致关机被 systemd 90 秒
//       超时后 SIGKILL；正常系统关机流程最终会 flush 磁盘，此处只需 unlink。
#ifdef Q_OS_LINUX
namespace {
// 缓存的标志文件路径（信号处理器中禁止QString堆分配，预先缓存为C字符串）
QByteArray g_flagPathBytes;

void terminationSignalHandler(int sig)
{
    // unlink 是 POSIX async-signal-safe 函数；::sync() 不是（旧版误用）。
    if (!g_flagPathBytes.isEmpty()) {
        ::unlink(g_flagPathBytes.constData());
    }
    // 恢复默认处理并重新触发信号，让进程以正常语义终止
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}
} // namespace
#endif

// 辅助函数：以C locale执行shell命令并返回标准输出
// （避免系统本地化导致 last/journalctl 的时间戳、月份名称解析失败）
#ifdef Q_OS_LINUX
static QString runShellCommand(const QString &command, int timeoutMs = 3000)
{
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LANG", "C");
    env.insert("LC_ALL", "C");
    process.setProcessEnvironment(env);
    process.start("sh", QStringList() << "-c" << command);
    if (process.waitForFinished(timeoutMs)) {
        return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    }
    return QString();
}

// 辅助函数：从 last -F 输出行中解析完整时间戳（"Mon Aug 21 09:00:52 2026"）
// takeFirst=true取第一个（记录开始时间），否则取最后一个（记录结束时间）
static QDateTime parseLastFullTimestamp(const QString &line, bool takeFirst)
{
    static QMap<QString, int> monthMap;
    if (monthMap.isEmpty()) {
        monthMap["Jan"] = 1; monthMap["Feb"] = 2; monthMap["Mar"] = 3;
        monthMap["Apr"] = 4; monthMap["May"] = 5; monthMap["Jun"] = 6;
        monthMap["Jul"] = 7; monthMap["Aug"] = 8; monthMap["Sep"] = 9;
        monthMap["Oct"] = 10; monthMap["Nov"] = 11; monthMap["Dec"] = 12;
    }

    QRegularExpression re(R"((\w{3})\s+(\w{3})\s+(\d{1,2})\s+(\d{2}:\d{2}:\d{2})\s+(\d{4}))");
    QRegularExpressionMatchIterator it = re.globalMatch(line);

    QDateTime result;
    bool gotAny = false;
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        int month = monthMap.value(match.captured(2), 0);
        int day = match.captured(3).toInt();
        QTime time = QTime::fromString(match.captured(4), "HH:mm:ss");
        int year = match.captured(5).toInt();
        if (month > 0 && day > 0 && time.isValid() && year > 1970) {
            QDate date(year, month, day);
            if (date.isValid()) {
                QDateTime dt(date, time);
                if (!gotAny || !takeFirst) {
                    result = dt;  // takeFirst时保留第一个，否则持续覆盖为最后一个
                }
                gotAny = true;
            }
        }
    }
    return result;
}
#endif // Q_OS_LINUX

IllegalShutdownDetector::IllegalShutdownDetector(QObject *parent)
    : QObject(parent), lastDetectedLog(""), illegalShutdownCount(0)
    , m_aliveTimer(nullptr)
{
    qDebug() << "[DEBUG] IllegalShutdownDetector 构造函数开始";

    loadCheckTime();
    loadIllegalShutdownState();
    loadIllegalShutdownCount();

    // 如果次数为0但有持久化状态，自动清除
    if (illegalShutdownCount <= 0 && hasPendingIllegalShutdown()) {
        qDebug() << "[DEBUG] 次数为0但有持久化状态，自动清除";
        clearIllegalShutdownState();
    }

    // 加载最近记录的非法关机时间
    QSettings settings("YanSheng", "TestClient");
    lastRecordedShutdownTime = settings.value("last_recorded_shutdown_time", QDateTime()).toDateTime();

    // 初始化运行标志文件路径
    m_runningFlagPath = getRunningFlagPath();
    qDebug() << "[DEBUG] 运行标志文件路径:" << m_runningFlagPath;

    // 缓存当前启动周期标识（boot_id：每次开机唯一，Linux 2.6+内核均支持，与CPU架构无关）
    m_currentBootId = getCurrentBootId();
    qDebug() << "[DEBUG] 当前启动周期 boot_id:" << m_currentBootId;

    // 定期刷新标志文件的 last_alive_time（每60秒fsync一次）
    // 作用：断电时间估算窗口从"上次软件启动~本次开机"缩小到1分钟内
    m_aliveTimer = new QTimer(this);
    m_aliveTimer->setInterval(60 * 1000);
    connect(m_aliveTimer, &QTimer::timeout, this, &IllegalShutdownDetector::refreshRunningFlag);

#ifdef Q_OS_LINUX
    // 安装 SIGTERM/SIGINT 处理器（各架构通用，防止桌面环境/系统服务直杀进程导致误报）
    installTerminationHandlers();
#endif

    qDebug() << "[DEBUG] 构造函数结束，计数器:" << illegalShutdownCount;
    qDebug() << "[DEBUG] 是否有持久化状态:" << hasPendingIllegalShutdown();
}

IllegalShutdownDetector::~IllegalShutdownDetector()
{
    saveCheckTime();
}

void IllegalShutdownDetector::checkOnStartup()
{
    qDebug() << "=== 开始检测非法关机（标志文件+boot_id+系统证据仲裁） ===";
    qDebug() << "当前非法关机次数:" << illegalShutdownCount;
    qDebug() << "是否有持久化状态:" << hasPendingIllegalShutdown();

    // 1. 首先检查是否有持久化的未处理非法关机记录
    if (hasPendingIllegalShutdown()) {
        qDebug() << "发现未处理的非法关机记录，恢复状态";
        qDebug() << "关机时间:" << persistentShutdownTime.toString("yyyy-MM-dd HH:mm:ss");
        qDebug() << "日志长度:" << persistentShutdownLog.length();
        qDebug() << "非法关机次数:" << illegalShutdownCount;

        // 只在非法关机次数大于0时才恢复状态
        if (illegalShutdownCount > 0) {
            lastDetectedLog = persistentShutdownLog;
            emit illegalShutdownStateRestored(persistentShutdownTime, persistentShutdownLog);
        } else {
            qDebug() << "非法关机次数为0，清除持久化状态";
            clearIllegalShutdownState();
            lastDetectedLog = QString();
        }
    }

    qDebug() << "上次检测时间:" << (lastCheckTime.isValid() ?
                                   lastCheckTime.toString("yyyy-MM-dd HH:mm:ss") : "无效");

    // 如果上次检测时间无效，设置为当前启动时间前几分钟
    if (!lastCheckTime.isValid()) {
        QDateTime bootTime = getSystemBootTime();
        if (bootTime.isValid()) {
            lastCheckTime = bootTime.addSecs(-60);
            qDebug() << "使用系统启动时间前1分钟:" << lastCheckTime.toString("yyyy-MM-dd HH:mm:ss");
        } else {
            lastCheckTime = QDateTime::currentDateTime().addSecs(-300);
            qDebug() << "重置检测时间为5分钟前:" << lastCheckTime.toString("yyyy-MM-dd HH:mm:ss");
        }
    }

    // 2. 执行非法关机检测（内部：标志文件+boot_id判定，系统证据仲裁）
    QPair<QDateTime, QString> result = detectIllegalShutdown(lastCheckTime);
    QDateTime shutdownTime = result.first;
    QString illegalLog = result.second;

    if (!illegalLog.isEmpty()) {
        qDebug() << "检测到非法关机，关机时间：" << shutdownTime.toString("yyyy-MM-dd HH:mm:ss");
        qDebug() << "日志长度：" << illegalLog.length();

        // 判断是否为新的非法关机事件（防重复检测）
        bool isNewEvent = true;

        // 检查：是否与上次检测时间太近（可能是同一次检测的重复触发，如软件快速重启）
        QDateTime currentDetectionTime = QDateTime::currentDateTime();
        if (lastCheckTime.isValid()) {
            qint64 secondsSinceLastCheck = lastCheckTime.secsTo(currentDetectionTime);
            if (secondsSinceLastCheck < 10) {
                qDebug() << "距离上次检测时间太短(" << secondsSinceLastCheck << "秒)，可能是重复检测，不计数";
                isNewEvent = false;
            }
        }

        if (isNewEvent) {
            // 是新的非法关机事件，增加次数
            incrementIllegalShutdownCount();
            qDebug() << "检测到新的非法关机事件，次数增加为:" << illegalShutdownCount;

            // 只在非法关机次数大于0时保存状态
            if (illegalShutdownCount > 0) {
                lastDetectedLog = illegalLog;
                lastRecordedShutdownTime = shutdownTime;

                // 保存最近记录的非法关机时间
                QSettings settings("YanSheng", "TestClient");
                settings.setValue("last_recorded_shutdown_time", shutdownTime);
                settings.sync();

                saveIllegalShutdownState(shutdownTime, illegalLog);
                emit illegalShutdownDetected(shutdownTime, illegalLog);
                saveDetectedLogToFile(illegalLog);
            }
        } else {
            qDebug() << "可能是重复检测，非法关机次数保持不变:" << illegalShutdownCount;
        }
    } else {
        qDebug() << "未检测到非法关机事件（正常关机/重启/程序异常退出已单独记录或已处理过）";
    }

    // 3. 原位重写运行标志文件（保持inode不变，避免"先删除再创建"间隙断电导致漏报）
    // 关键逻辑：检测完成后用当前启动周期信息重写标志文件，供下次启动检测使用
    // （标志文件中的boot_id用于区分"系统非法关机"与"程序崩溃"）
    createRunningFlag();

    // 启动 last_alive_time 定期刷新
    if (m_aliveTimer && !m_aliveTimer->isActive()) {
        m_aliveTimer->start();
    }

    // 4. 更新检测时间
    lastCheckTime = QDateTime::currentDateTime();
    saveCheckTime();

    qDebug() << "更新检测时间到:" << lastCheckTime.toString("yyyy-MM-dd HH:mm:ss");
    qDebug() << "当前非法关机次数:" << illegalShutdownCount;
}

QDateTime IllegalShutdownDetector::getLastRecordedShutdownTime() const
{
    return lastRecordedShutdownTime;
}

void IllegalShutdownDetector::saveIllegalShutdownState(const QDateTime &time, const QString &log)
{
    QSettings settings("YanSheng", "TestClient");
    settings.setValue("illegal_shutdown_persistent_time", time);
    settings.setValue("illegal_shutdown_persistent_log", log);
    settings.sync();  // 关键状态立即落盘

    persistentShutdownTime = time;
    persistentShutdownLog = log;

    qDebug() << "[DEBUG] 保存非法关机状态，关机时间:" << time.toString("yyyy-MM-dd HH:mm:ss");
    qDebug() << "[DEBUG] 当前非法关机次数:" << illegalShutdownCount;
}

void IllegalShutdownDetector::loadIllegalShutdownState()
{
    QSettings settings("YanSheng", "TestClient");
    persistentShutdownTime = settings.value("illegal_shutdown_persistent_time", QDateTime()).toDateTime();
    persistentShutdownLog = settings.value("illegal_shutdown_persistent_log", QString()).toString();

    if (hasPendingIllegalShutdown()) {
        qDebug() << "加载到未处理的非法关机记录，时间:" << persistentShutdownTime.toString("yyyy-MM-dd HH:mm:ss");
    } else {
        qDebug() << "没有未处理的非法关机记录";
    }
}

void IllegalShutdownDetector::clearIllegalShutdownState()
{
    QSettings settings("YanSheng", "TestClient");

    // 先打印清除前的状态
    qDebug() << "[DEBUG] 清除前的非法关机状态:";
    qDebug() << "  时间:" << settings.value("illegal_shutdown_persistent_time").toDateTime().toString("yyyy-MM-dd HH:mm:ss");
    qDebug() << "  日志长度:" << settings.value("illegal_shutdown_persistent_log").toString().length();
    qDebug() << "  记录时间:" << settings.value("last_recorded_shutdown_time").toDateTime().toString("yyyy-MM-dd HH:mm:ss");
    qDebug() << "  计数器:" << settings.value("illegal_shutdown_count", 0).toInt();

    settings.remove("illegal_shutdown_persistent_time");
    settings.remove("illegal_shutdown_persistent_log");
    settings.remove("last_recorded_shutdown_time");

    // 同时清除非法关机次数
    settings.remove("illegal_shutdown_count");
    settings.sync();

    persistentShutdownTime = QDateTime();
    persistentShutdownLog = QString();
    lastDetectedLog = QString();
    lastRecordedShutdownTime = QDateTime();
    illegalShutdownCount = 0;

    // 同时删除所有历史日志文件
    clearAllShutdownLogFiles();

    qDebug() << "[DEBUG] 已彻底清除非法关机状态、记录的时间、计数器和历史日志文件";
}

bool IllegalShutdownDetector::hasPendingIllegalShutdown() const
{
    return persistentShutdownTime.isValid() && !persistentShutdownLog.isEmpty();
}

QString IllegalShutdownDetector::getLastDetectedLog() const
{
    if (!lastDetectedLog.isEmpty()) {
        return lastDetectedLog;
    } else if (hasPendingIllegalShutdown()) {
        return persistentShutdownLog;
    }
    return QString();
}

int IllegalShutdownDetector::getIllegalShutdownCount() const
{
    return illegalShutdownCount;
}

void IllegalShutdownDetector::incrementIllegalShutdownCount()
{
    illegalShutdownCount++;
    saveIllegalShutdownCount();
    emit illegalShutdownCountChanged(illegalShutdownCount);
    qDebug() << "非法关机次数增加为:" << illegalShutdownCount;
}

void IllegalShutdownDetector::resetIllegalShutdownCount()
{
    illegalShutdownCount = 0;
    saveIllegalShutdownCount();
    emit illegalShutdownCountChanged(illegalShutdownCount);
    qDebug() << "非法关机次数已重置为0";
}

void IllegalShutdownDetector::saveIllegalShutdownCount()
{
    QSettings settings("YanSheng", "TestClient");
    settings.setValue("illegal_shutdown_count", illegalShutdownCount);
    settings.sync();  // 计数器立即落盘
    qDebug() << "保存非法关机次数:" << illegalShutdownCount;
}

void IllegalShutdownDetector::loadIllegalShutdownCount()
{
    QSettings settings("YanSheng", "TestClient");
    illegalShutdownCount = settings.value("illegal_shutdown_count", 0).toInt();
    qDebug() << "加载非法关机次数:" << illegalShutdownCount;
}

QDateTime IllegalShutdownDetector::getSystemBootTime()
{
    qDebug() << "=== 获取系统启动时间 ===";

    QDateTime bootTime;

    // 方法1: 通过uptime命令获取启动时间
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("LANG", "C");
    env.insert("LC_ALL", "C");
    process.setProcessEnvironment(env);
    process.start("uptime", QStringList() << "-s");
    if (process.waitForFinished(1000)) {
        QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        qDebug() << "uptime命令输出:" << output;

        if (!output.isEmpty()) {
            // 格式: 2025-12-16 13:41:10
            bootTime = QDateTime::fromString(output, "yyyy-MM-dd HH:mm:ss");
            if (bootTime.isValid()) {
                qDebug() << "通过uptime获取启动时间:" << bootTime.toString("yyyy-MM-dd HH:mm:ss");
                return bootTime;
            }
        }
    }

    // 方法2: 通过/proc/uptime计算启动时间（不依赖外部命令，各架构通用）
    QFile uptimeFile("/proc/uptime");
    if (uptimeFile.open(QIODevice::ReadOnly)) {
        QString uptimeStr = uptimeFile.readLine().trimmed();
        qDebug() << "/proc/uptime内容:" << uptimeStr;

        QStringList parts = uptimeStr.split(' ');
        if (!parts.isEmpty()) {
            double uptimeSeconds = parts[0].toDouble();
            bootTime = QDateTime::currentDateTime().addSecs(-uptimeSeconds);
            qDebug() << "通过/proc/uptime计算启动时间:" << bootTime.toString("yyyy-MM-dd HH:mm:ss");
            uptimeFile.close();
            return bootTime;
        }
        uptimeFile.close();
    }

    qDebug() << "无法获取系统启动时间";
    return QDateTime();
}

void IllegalShutdownDetector::saveDetectedLogToFile(const QString &log)
{
    QString logDirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir logDir(logDirPath);
    if (!logDir.exists()) {
        logDir.mkpath(".");
    }

    // 使用毫秒级时间戳，避免快速连续测试时文件名冲突导致文件覆盖
    QString fileName = QString("illegal_shutdown_%1.log")
                      .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz"));
    QString filePath = logDir.filePath(fileName);

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        // 直接使用 file.write() + toUtf8() 写入 UTF-8 字节
        // 避免 QTextStream 默认编码（中文系统是 GBK）导致的乱码
        QString content;
        content += "=== 非法关机检测日志 ===\n";
        content += "检测时间: " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz") + "\n";
        content += "累计非法关机次数: " + QString::number(illegalShutdownCount) + " 次\n";
        content += "关机时间: " + (lastRecordedShutdownTime.isValid() ?
                                   lastRecordedShutdownTime.toString("yyyy-MM-dd HH:mm:ss") : "未知") + "\n";
        content += "日志长度: " + QString::number(log.length()) + " 字符\n";
        content += "========================\n\n";
        content += log;

        qint64 bytesWritten = file.write(content.toUtf8());
        file.flush();  // 刷新 Qt 内部缓冲区

#ifdef Q_OS_LINUX
        // 强制将文件系统缓冲区写入物理磁盘
        // 防止非法关机时数据停留在内存缓冲区中导致文件丢失/截断
        if (file.handle() != -1) {
            ::fsync(file.handle());
        }
#endif

        file.close();

        if (bytesWritten == -1) {
            qWarning() << "写入日志文件失败:" << filePath << "错误:" << file.errorString();
        } else if (bytesWritten == 0 && !content.isEmpty()) {
            qWarning() << "写入日志文件但字节数为0:" << filePath << "内容长度:" << content.length();
        } else {
            qDebug() << "非法关机日志已保存到:" << filePath << "写入字节:" << bytesWritten;
        }
    } else {
        qWarning() << "无法打开日志文件:" << filePath << "错误:" << file.errorString();
    }
}

// 保存程序崩溃日志（崩溃/被强制结束，与系统非法关机分开记录）
void IllegalShutdownDetector::saveAppCrashLog(const QString &log)
{
    QString logDirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir logDir(logDirPath);
    if (!logDir.exists()) {
        logDir.mkpath(".");
    }

    QString fileName = QString("app_crash_%1.log")
                      .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz"));
    QString filePath = logDir.filePath(fileName);

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qint64 bytesWritten = file.write(log.toUtf8());
        file.flush();
#ifdef Q_OS_LINUX
        if (file.handle() != -1) {
            ::fsync(file.handle());
        }
#endif
        file.close();
        qDebug() << "程序崩溃日志已保存到:" << filePath << "写入字节:" << bytesWritten;
    } else {
        qWarning() << "无法保存程序崩溃日志:" << filePath << "错误:" << file.errorString();
    }
}

// 获取所有非法关机日志文件路径（按时间排序，从旧到新）
QStringList IllegalShutdownDetector::getAllShutdownLogFiles()
{
    QString logDirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir logDir(logDirPath);

    QStringList filters;
    filters << "illegal_shutdown_*.log";

    // 按文件名排序（文件名中包含时间戳，等同于按时间排序）
    QFileInfoList files = logDir.entryInfoList(filters, QDir::Files, QDir::Name);

    QStringList paths;
    int skipped = 0;
    for (const QFileInfo &fi : files) {
        // 过滤掉空文件（可能是写入失败或被截断的文件）
        if (fi.size() <= 0) {
            qWarning() << "跳过空日志文件:" << fi.absoluteFilePath();
            skipped++;
            continue;
        }
        paths << fi.absoluteFilePath();
    }

    qDebug() << "找到" << paths.size() << "个非法关机日志文件（跳过" << skipped << "个空文件）";
    return paths;
}

// 删除所有非法关机日志文件
void IllegalShutdownDetector::clearAllShutdownLogFiles()
{
    QStringList logFiles = getAllShutdownLogFiles();
    int deletedCount = 0;

    for (const QString &filePath : logFiles) {
        QFile file(filePath);
        if (file.remove()) {
            deletedCount++;
        }
    }

    qDebug() << "已删除" << deletedCount << "个非法关机日志文件";
}

// 从恢复日志中解析关机时间
// 优化1：取最后一个时间戳（最接近断电/关机时刻，第一个距断电可能相差数分钟）
// 优化2：跨年修正（12月31日的日志在次年1月解析时年份错一年）
QDateTime IllegalShutdownDetector::parseShutdownTimeFromRecoveryLogs(const QString &logs)
{
    QStringList lines = logs.split('\n');

    // 正则表达式匹配常见日志时间格式
    QRegularExpression timePattern(R"((\d{1,2}月\s+\d{1,2}\s+\d{2}:\d{2}:\d{2}))");
    QRegularExpression timePattern2(R"((\w{3}\s+\d{1,2}\s+\d{2}:\d{2}:\d{2}))");

    QDateTime result;

    // 跨年修正：若解析结果在未来（超过2天），说明年份推断错误，回退一年
    auto applyYearFix = [](QDateTime dt) -> QDateTime {
        if (dt.isValid() && dt > QDateTime::currentDateTime().addDays(2)) {
            dt = dt.addYears(-1);
        }
        return dt;
    };

    for (const QString &line : lines) {
        QRegularExpressionMatch match;

        if ((match = timePattern.match(line)).hasMatch()) {
            QString timeStr = match.captured(1);
            // 解析格式: "12月 16 13:58:38"
            QStringList parts = timeStr.split(QRegularExpression("\\s+"));
            if (parts.size() >= 3) {
                QString monthStr = parts[0].replace("月", "");
                QString dayStr = parts[1];
                QString timePart = parts[2];

                int month = monthStr.toInt();
                int day = dayStr.toInt();

                if (month > 0 && day > 0) {
                    int year = QDate::currentDate().year();
                    QDate date(year, month, day);
                    QTime time = QTime::fromString(timePart, "HH:mm:ss");

                    if (date.isValid() && time.isValid()) {
                        result = applyYearFix(QDateTime(date, time));
                    }
                }
            }
        } else if ((match = timePattern2.match(line)).hasMatch()) {
            // 英文月份格式: "Dec 16 13:58:38"
            QString timeStr = match.captured(1);

            QMap<QString, int> monthMap;
            monthMap["Jan"] = 1; monthMap["Feb"] = 2; monthMap["Mar"] = 3;
            monthMap["Apr"] = 4; monthMap["May"] = 5; monthMap["Jun"] = 6;
            monthMap["Jul"] = 7; monthMap["Aug"] = 8; monthMap["Sep"] = 9;
            monthMap["Oct"] = 10; monthMap["Nov"] = 11; monthMap["Dec"] = 12;

            QStringList parts = timeStr.split(QRegularExpression("\\s+"));
            if (parts.size() >= 3) {
                QString monthStr = parts[0];
                QString dayStr = parts[1];
                QString timePart = parts[2];

                int month = monthMap.value(monthStr, 0);
                int day = dayStr.toInt();

                if (month > 0 && day > 0) {
                    int year = QDate::currentDate().year();
                    QDate date(year, month, day);
                    QTime time = QTime::fromString(timePart, "HH:mm:ss");

                    if (date.isValid() && time.isValid()) {
                        result = applyYearFix(QDateTime(date, time));
                    }
                }
            }
        }
    }

    if (result.isValid()) {
        qDebug() << "从恢复日志解析到时间(取最后一条):" << result.toString("yyyy-MM-dd HH:mm:ss");
    } else {
        qDebug() << "无法从恢复日志解析时间";
    }
    return result;
}

// =================================================================
// ========== 综合判断关机类型（仲裁版） ==========
// =================================================================
// 检测策略（各架构通用，不依赖单一外部命令）：
// 1) 标志文件 + boot_id 判定"程序视角"的退出方式（最可靠，纯文件操作无外部依赖）：
//    - 标志存在且boot_id与当前一致 → 程序在上次运行中异常退出（崩溃/被杀）
//      → SHUTDOWN_APP_CRASH（不计入非法关机，避免错报）
//    - 标志存在且boot_id不同 → 程序运行期间系统关机且未走正常退出流程
//      → 用系统证据仲裁：区分"断电/强制关机"与"正常关机但程序被漏杀"
// 2) 系统证据仲裁优先级：journalctl（精确到标志文件对应的启动周期）
//    > last -x（wtmp） > dmesg文件系统恢复记录 > 无证据（以标志文件为准）
// 3) 标志不存在时，仅依赖系统证据判定系统级非法关机（覆盖软件未运行期间的断电，避免漏报）
ShutdownType IllegalShutdownDetector::determineShutdownType()
{
    qDebug() << "=== 综合判断关机类型（标志文件+boot_id+系统证据仲裁） ===";

    // ========== 第负一层：计划内电源操作标记（软件主动 reboot/shutdown，最高优先级） ==========
    // 比 running.flag 更权威的跨启动周期凭据：只要上次开机周期内软件主动发过
    // 重启/关机命令，本次开机即判定为正常（与标志文件删除成败、journalctl/wtmp
    // 证据可用性完全解耦）——这是消除"循环重启概率性误判"的确定性判据。
    {
        QString pIntent; QDateTime pTime; QString pBootId;
        if (checkPlannedPowerMarker(&pIntent, &pTime, &pBootId)) {
            bool prevBoot = !pBootId.isEmpty() && !m_currentBootId.isEmpty()
                            && (pBootId != m_currentBootId);
            if (prevBoot) {
                QDateTime bootTime = getSystemBootTime();
                qint64 gap = (pTime.isValid() && bootTime.isValid())
                             ? pTime.secsTo(bootTime) : -1;
                // gap<0（时钟跳变无法判定）时从宽认定为计划内操作
                if (gap < 0 || gap <= 1800) {
                    qDebug() << "第负一层：检测到计划内电源操作标记（跨启动周期，"
                             << gap << "秒前）→ 判定为正常" << pIntent << "，非非法关机";
                    clearPlannedPowerMarker();
                    m_cachedShutdownType = SHUTDOWN_NORMAL;
                    m_cachedHasRecovery = false;
                    m_flagBasedDetection = false;
                    m_cachedRecoveryLogs = QString(
                        "计划内%1判定：上次启动周期内软件主动发起%1命令（planned_power.marker 凭据），\n"
                        "本次开机为计划内%1后的正常启动，非非法关机。\n"
                        "  标记时间: %2  本次boot时间: %3  intent=%4")
                        .arg(pIntent)
                        .arg(pTime.isValid() ? pTime.toString("yyyy-MM-dd HH:mm:ss") : QStringLiteral("未知"))
                        .arg(bootTime.isValid() ? bootTime.toString("yyyy-MM-dd HH:mm:ss") : QStringLiteral("未知"))
                        .arg(pIntent);
                    return SHUTDOWN_NORMAL;
                }
                // 距本次开机太久（>1800秒）→ 陈旧残留（如计划操作后机器长时间未再开机，
                // 期间发生过其他真实非法断电）→ 清除并继续正常检测
                qDebug() << "第负一层：计划内电源操作标记为陈旧残留(" << gap << "秒) → 清除后继续正常检测";
                clearPlannedPowerMarker();
            } else {
                // boot_id 属当前启动周期：命令可能正在执行（另一实例挂起中）或已失败。
                // 年龄 > 600 秒视为失败残留清除；否则保留，继续走正常检测流程。
                QDateTime now = QDateTime::currentDateTime();
                qint64 age = pTime.isValid() ? pTime.secsTo(now) : -1;
                if (age > 600) {
                    qDebug() << "第负一层：计划内电源操作标记为本周期失败残留(" << age
                             << "秒) → 清除后继续正常检测";
                    clearPlannedPowerMarker();
                } else {
                    qDebug() << "第负一层：计划内电源操作标记属当前周期且为挂起状态("
                             << age << "秒) → 保留，继续正常检测";
                }
            }
        }
    }

    // ========== 第零层：休眠标记检测（软件主动休眠，最高优先级） ==========
    // 背景：hibernate（休眠到磁盘）= 内存镜像写入 swap 后直接断电，
    //       对系统日志而言与"非法断电"完全无法区分（journalctl 上个
    //       启动周期没有 Stopping Session / Reached target Shutdown 等
    //       正常关机流程）。因此即使标志文件已通过 markCleanShutdown()
    //       删除，"标志不存在→系统证据仲裁"分支仍会把休眠唤醒误判为
    //       非法关机（用户实测：两次休眠两次误报）。
    // 机制：SystemController::hibernate() 发命令前调用 markHibernate()
    //       写入 marker；唤醒后本层检测到 marker 即判定"软件主动休眠恢复"。
    // 防残留：marker 记录写入时刻 —— 仅当 marker 时间早于本次系统启动
    //        时间（跨启动周期）才认定有效；若 marker 是本启动周期写的
    //        （说明上次休眠命令未成功执行，机器没断电），清除后继续
    //        走正常检测流程，避免污染后续判断。
    {
        QDateTime markerTime;
        if (checkHibernateMarker(&markerTime)) {
            QDateTime bootTime = getSystemBootTime();
            bool crossBoot = !bootTime.isValid() || !markerTime.isValid()
                             || (markerTime < bootTime);
            if (crossBoot) {
                // marker 写于上个启动周期（时间无法判定时从宽处理）→ 本次开机为休眠唤醒
                qDebug() << "第零层：检测到休眠标记（跨启动周期）→ 判定为休眠唤醒，非非法关机"
                         << "marker时间:" << (markerTime.isValid() ? markerTime.toString("yyyy-MM-dd HH:mm:ss") : "未知")
                         << "本次boot时间:" << (bootTime.isValid() ? bootTime.toString("yyyy-MM-dd HH:mm:ss") : "未知");
                clearHibernateMarker();
                m_cachedShutdownType = SHUTDOWN_NORMAL;
                m_cachedHasRecovery = false;
                m_flagBasedDetection = false;
                m_cachedRecoveryLogs.clear();
                return SHUTDOWN_NORMAL;
            }
            // marker 是本启动周期写的（休眠命令未实际执行/被取消）→ 清除残留
            qDebug() << "第零层：休眠标记为本启动周期残留（休眠未实际发生），清除后继续正常检测";
            clearHibernateMarker();
        }
    }

    // ========== 第零点五层：suspend 期间断电检测（2026-09-11 飞腾真机新增）==========
    // 背景：用户在 suspend 状态直接断电（拔电源/长按电源键）→ boot_id 变 + running.flag
    //       已被 markCleanShutdown 删 → 看不到任何"软件曾运行"痕迹,会被漏判为"正常"。
    // 机制：SystemController::suspend() 发命令前调用 markSuspend() 写入 marker(带 RTC 时间)。
    //       启动时若 marker 存在 + bootTime - markerTime < 60s → "suspend 期间被立即断电"
    //       (BIOS POST + GRUB + kernel init 一般 20~40s,留 60s 缓冲)→ 判定**非法关机**。
    //       bootTime - markerTime >= 60s → 用户从 suspend 正常唤醒→使用→正常关机 → 正常。
    // 防止"suspend → 正常唤醒(本 boot_id)"漏报:本启动周期 boot_id 不变 → bootTime < markerTime
    //   的判断不会命中非法分支(我们用 secsTo >= 0 检查),继续走常规检测;
    //   marker 在 warm-resume 后由本启动周期的"检测"流程清除(否则会污染下次冷启动)。
    {
        QDateTime markerTime;
        if (checkSuspendMarker(&markerTime)) {
            QDateTime bootTime = getSystemBootTime();
            if (!bootTime.isValid() || !markerTime.isValid()) {
                qWarning() << "第零点五层：suspend marker 时间解析失败,清除后继续正常检测"
                           << "bootTime 有效:" << bootTime.isValid()
                           << "markerTime 有效:" << markerTime.isValid();
                clearSuspendMarker();
            } else {
                qint64 gapSecs = markerTime.secsTo(bootTime);
                if (gapSecs < 0) {
                    // marker 写于"未来"(跨时区/时钟漂移)→ 异常,清除后继续
                    qWarning() << "第零点五层：suspend marker 时间异常(写于未来),清除后继续"
                               << "markerTime:" << markerTime.toString("yyyy-MM-dd HH:mm:ss")
                               << "bootTime:" << bootTime.toString("yyyy-MM-dd HH:mm:ss");
                    clearSuspendMarker();
                } else if (gapSecs < 60) {
                    // ===== 核心修复:suspend 期间被立即断电 =====
                    qDebug() << "第零点五层：suspend marker 存在 + bootTime-markerTime=" << gapSecs
                             << "秒 < 60秒 → 判定为 suspend 期间断电,非法关机"
                             << "marker时间:" << markerTime.toString("yyyy-MM-dd HH:mm:ss")
                             << "本次boot时间:" << bootTime.toString("yyyy-MM-dd HH:mm:ss");
                    clearSuspendMarker();
                    m_cachedShutdownType = SHUTDOWN_ILLEGAL;
                    m_cachedHasRecovery = true;
                    m_flagBasedDetection = false;
                    m_cachedRecoveryLogs = QString(
                        "suspend 期间被断电:软件发 suspend 命令后用户直接断电(无正常关机流程)\n"
                        "  suspend 命令发出时间: %1\n"
                        "  本次系统启动时间:   %2\n"
                        "  时间差: %3 秒(< 60秒阈值,极可能是断电而非正常关机)\n"
                        "  检测依据:markSuspend() 写入 suspend.marker 记录 suspend 命令时刻,\n"
                        "          与系统启动时间差 < 60s 是 suspend 期间被立即断电的强信号\n"
                        "          (BIOS POST + GRUB + kernel init 一般 20~40s,留 60s 缓冲)\n"
                        "  注:boot_id 跨周期 + flag 不存在 + 时间差极小 → 三重证据交叉验证")
                        .arg(markerTime.toString("yyyy-MM-dd HH:mm:ss"))
                        .arg(bootTime.toString("yyyy-MM-dd HH:mm:ss"))
                        .arg(gapSecs);
                    return SHUTDOWN_ILLEGAL;
                } else {
                    // 长时间间隔 → 用户正常唤醒→使用→正常关机
                    qDebug() << "第零点五层：suspend marker 存在 + bootTime-markerTime=" << gapSecs
                             << "秒 >= 60秒 → 判定为 suspend 后正常关机,清除 marker";
                    clearSuspendMarker();
                }
            }
        }
    }

    // ========== 第一层：运行标志文件检测（主判据） ==========
    QString flagInfo;
    bool flagExists = checkRunningFlag(&flagInfo);

    qDebug() << "运行标志文件存在:" << (flagExists ? "是" : "否");

    if (flagExists) {
        // 判断标志文件是否属于当前启动周期（boot_id机制）
        bool sameBoot = false;
        if (!m_currentBootId.isEmpty() && !m_flagBootId.isEmpty()) {
            sameBoot = (m_currentBootId == m_flagBootId);
        } else if (m_flagLastAlive.isValid()) {
            // 回退方案1（旧版标志文件无boot_id）：
            // 同一启动周期内，最后活跃时间必然不早于系统启动时间
            QDateTime bootTime = getSystemBootTime();
            if (bootTime.isValid()) {
                sameBoot = (m_flagLastAlive >= bootTime.addSecs(-5));
            }
        } else if (m_flagBootTime.isValid()) {
            // 回退方案2（旧版标志文件仅有boot_time）：
            // 与当前系统启动时间比较（快速重启场景下可能不准，仅作回退）
            QDateTime bootTime = getSystemBootTime();
            if (bootTime.isValid()) {
                sameBoot = (qAbs(m_flagBootTime.secsTo(bootTime)) < 30);
            }
        }

        if (sameBoot) {
            // 标志文件属于当前启动周期 → 程序异常退出（崩溃/被杀），不是系统非法关机
            qDebug() << "判定为：程序异常退出（当前启动周期内，崩溃或被强制结束）";
            m_cachedShutdownType = SHUTDOWN_APP_CRASH;
            m_cachedHasRecovery = false;
            m_flagBasedDetection = true;
            m_cachedRecoveryLogs = "标志文件boot_id与当前启动周期一致：程序上次运行中异常退出（崩溃/被强制结束）\n" + flagInfo;
            return SHUTDOWN_APP_CRASH;
        }

        // 标志文件属于之前的启动周期 → 程序运行期间系统关机且未正常退出
        // 用系统级证据仲裁：区分"断电/强制关机"与"正常关机但程序未及清理标志"
        qDebug() << "标志文件属于之前的启动周期，启用系统证据仲裁...";

        // 证据1: journalctl（精确检查标志文件对应的启动周期）
        bool jReliable = false;
        QString journalInfo;
        bool jClean = checkLastBootCleanShutdown(&journalInfo, &jReliable, m_flagBootId);
        if (jReliable) {
            if (jClean) {
                // journalctl明确显示该启动周期存在正常关机流程：
                // 系统是正常关机的，标志残留是因为程序在关机序列中被强杀（漏杀）
                qDebug() << "仲裁结果：journalctl显示标志文件所在启动周期为正常关机（程序未及清理标志）";
                m_cachedShutdownType = SHUTDOWN_NORMAL;
                m_cachedHasRecovery = false;
                m_flagBasedDetection = true;
                m_cachedRecoveryLogs = "journalctl仲裁：标志文件所在启动周期存在正常关机流程，标志文件未及清理（如关机前进程被强杀）\n" + journalInfo;
                return SHUTDOWN_NORMAL;
            }
            qDebug() << "仲裁结果：journalctl显示标志文件所在启动周期无正常关机流程 → 非法关机";
            m_cachedShutdownType = SHUTDOWN_ILLEGAL;
            m_cachedHasRecovery = true;
            m_flagBasedDetection = true;
            m_cachedRecoveryLogs = "运行标志文件存在（程序运行期间系统关机未走正常退出流程）\njournalctl证据：该启动周期未发现正常关机流程\n" + journalInfo;
            return SHUTDOWN_ILLEGAL;
        }

        // 证据2: last -x（wtmp）——精确定位标志文件对应的启动记录
        bool lReliable = false;
        QDateTime shutdownEndTime;
        bool lHasShutdown = checkNormalShutdownByLast(&lReliable, &shutdownEndTime, m_flagBootTime);
        if (lReliable) {
            if (lHasShutdown) {
                // 校验：该shutdown记录必须晚于标志文件的最后活跃时间，
                // 否则它是更早启动周期的关机记录，不能证明标志所在的启动周期正常关机
                if (m_flagLastAlive.isValid() && shutdownEndTime.isValid()
                    && shutdownEndTime < m_flagLastAlive) {
                    qDebug() << "仲裁结果：last -x的shutdown记录早于程序最后活跃时间（陈旧证据）→ 非法关机";
                    m_cachedShutdownType = SHUTDOWN_ILLEGAL;
                    m_cachedHasRecovery = true;
                    m_flagBasedDetection = true;
                    m_cachedRecoveryLogs = "运行标志文件存在 + last -x中最近的shutdown记录早于程序最后活跃时间（陈旧证据，不能证明正常关机）";
                    return SHUTDOWN_ILLEGAL;
                }
                qDebug() << "仲裁结果：last -x存在标志所在启动周期的shutdown记录 → 正常关机";
                m_cachedShutdownType = SHUTDOWN_NORMAL;
                m_cachedHasRecovery = false;
                m_flagBasedDetection = true;
                m_cachedRecoveryLogs = "last -x仲裁：标志文件所在启动周期存在正常关机记录，标志文件未及清理";
                return SHUTDOWN_NORMAL;
            }
            qDebug() << "仲裁结果：last -x显示标志所在启动周期无shutdown记录 → 非法关机";
            m_cachedShutdownType = SHUTDOWN_ILLEGAL;
            m_cachedHasRecovery = true;
            m_flagBasedDetection = true;
            m_cachedRecoveryLogs = "运行标志文件存在 + last -x显示标志所在启动周期无shutdown记录（wtmp证据）";
            return SHUTDOWN_ILLEGAL;
        }

        // 证据3: dmesg 文件系统恢复记录（无journalctl/wtmp环境的兜底证据）
        QString dmesgInfo;
        if (checkDmesgFilesystemRecovery(&dmesgInfo)) {
            qDebug() << "仲裁结果：dmesg存在文件系统恢复记录 → 非法关机";
            m_cachedShutdownType = SHUTDOWN_ILLEGAL;
            m_cachedHasRecovery = true;
            m_flagBasedDetection = true;
            m_cachedRecoveryLogs = "运行标志文件存在 + 本次启动存在文件系统恢复记录（上次关机未正常卸载文件系统）\n" + dmesgInfo;
            return SHUTDOWN_ILLEGAL;
        }

        // 无任何系统级证据可用（嵌入式/最小系统）：
        // 标志文件是唯一证据——程序运行期间系统未走正常退出流程 → 判定非法关机
        // （保持与原版一致的行为：宁可上报，不漏报）
        qDebug() << "仲裁结果：无系统级证据可用，以标志文件为准 → 非法关机";
        m_cachedShutdownType = SHUTDOWN_ILLEGAL;
        m_cachedHasRecovery = true;
        m_flagBasedDetection = true;
        m_cachedRecoveryLogs = "运行标志文件存在（上次启动周期，系统级检测工具均不可用，以标志文件为唯一证据）\n" + flagInfo;
        return SHUTDOWN_ILLEGAL;
    }

    // ========== 标志文件不存在：程序上次正常退出（或未运行过） ==========
    // 系统级证据判定软件未运行期间的断电（避免漏报）
    {
        bool jReliable = false;
        QString journalInfo;
        bool jClean = checkLastBootCleanShutdown(&journalInfo, &jReliable);
        if (jReliable) {
            if (jClean) {
                qDebug() << "判定为：正常关机（journalctl存在关机流程）";
                m_cachedShutdownType = SHUTDOWN_NORMAL;
                m_cachedHasRecovery = false;
                m_flagBasedDetection = false;
                m_cachedRecoveryLogs.clear();
                return SHUTDOWN_NORMAL;
            }
            qDebug() << "判定为：非法关机（journalctl无关机流程，软件未运行期间断电）";
            m_cachedShutdownType = SHUTDOWN_ILLEGAL;
            m_cachedHasRecovery = true;
            m_flagBasedDetection = false;
            m_cachedRecoveryLogs = "journalctl检测：上次启动未发现正常关机流程（软件未运行期间发生断电/强制关机）\n" + journalInfo;
            return SHUTDOWN_ILLEGAL;
        }

        // last -x 仅作参考，不作为定罪依据：
        // 不少国产系统（麒麟/统信）wtmp 对 shutdown 记录维护不完整
        // （fastboot/记录轮转/halt 语义），"只有 reboot 无 shutdown"在该类
        // 系统上是常态。程序标志文件已正常删除（软件视角正常退出），
        // 仅凭 wtmp 缺失就判非法关机会造成大量误报。
        // 定罪仍依赖 journalctl（内核日志，可靠）与 dmesg（文件系统恢复，强证据）。
        bool lReliable = false;
        QDateTime shutdownEndTime;
        bool lHasShutdown = checkNormalShutdownByLast(&lReliable, &shutdownEndTime);
        if (lReliable && !lHasShutdown) {
            qDebug() << "last -x未找到shutdown记录，但标志文件已正常删除（wtmp记录缺失常见），"
                     << "不作为非法关机定罪依据，继续检查dmesg";
        }

        QString dmesgInfo;
        if (checkDmesgFilesystemRecovery(&dmesgInfo)) {
            qDebug() << "判定为：非法关机（dmesg文件系统恢复记录）";
            m_cachedShutdownType = SHUTDOWN_ILLEGAL;
            m_cachedHasRecovery = true;
            m_flagBasedDetection = false;
            m_cachedRecoveryLogs = "dmesg检测：本次启动存在文件系统恢复记录，上次关机未正常卸载文件系统\n" + dmesgInfo;
            return SHUTDOWN_ILLEGAL;
        }

        // 无证据 → 不定罪（避免错报）
        qDebug() << "判定为：正常关机/重启（未发现异常证据）";
        m_cachedShutdownType = SHUTDOWN_NORMAL;
        m_cachedHasRecovery = false;
        m_flagBasedDetection = false;
        m_cachedRecoveryLogs.clear();
        return SHUTDOWN_NORMAL;
    }
}

// 获取关机原因的详细日志
QString IllegalShutdownDetector::getShutdownReasonLog()
{
    qDebug() << "=== 生成关机原因详细日志 ===";

    QString detailedLog;
    detailedLog += "=== 关机类型分析详细报告 ===\n\n";

    detailedLog += "--- 检测证据 ---\n";
    detailedLog += "结果: " + QString(m_cachedHasRecovery ? "发现异常证据" : "未发现异常证据") + "\n";
    if (!m_cachedRecoveryLogs.isEmpty()) {
        detailedLog += "证据内容:\n" + m_cachedRecoveryLogs + "\n";
    }

    detailedLog += "\n--- 最终结论 ---\n";

    switch (m_cachedShutdownType) {
    case SHUTDOWN_NORMAL:
        detailedLog += "关机类型: 正常关机/重启\n";
        break;
    case SHUTDOWN_ILLEGAL:
        detailedLog += "关机类型: 非法关机 ⚠️\n";
        break;
    case SHUTDOWN_APP_CRASH:
        detailedLog += "关机类型: 程序异常退出（崩溃/被强制结束，非系统非法关机）\n";
        break;
    default:
        detailedLog += "关机类型: 未知\n";
        break;
    }

    detailedLog += "\n--- 系统状态 ---\n";
    QDateTime bootTime = getSystemBootTime();
    detailedLog += "系统启动时间: " + (bootTime.isValid() ? bootTime.toString("yyyy-MM-dd HH:mm:ss") : "未知") + "\n";
    detailedLog += "当前启动周期boot_id: " + (m_currentBootId.isEmpty() ? "未知" : m_currentBootId) + "\n";

    return detailedLog;
}

QPair<QDateTime, QString> IllegalShutdownDetector::detectIllegalShutdown(const QDateTime &sinceTime)
{
    qDebug() << "=== 开始检测非法关机（标志文件+boot_id+系统证据仲裁） ===";
    qDebug() << "上次检测时间:" << (sinceTime.isValid() ? sinceTime.toString("yyyy-MM-dd HH:mm:ss") : "无效");

    // 使用综合判断逻辑（内部：标志文件+boot_id判定，系统证据仲裁）
    ShutdownType shutdownType = determineShutdownType();

    qDebug() << "关机类型判断结果:" << static_cast<int>(shutdownType);

    // 程序异常退出（崩溃/被强制结束）：不是系统非法关机，单独记录但不计入非法关机次数
    if (shutdownType == SHUTDOWN_APP_CRASH) {
        QString crashLog;
        crashLog += "=== 程序异常退出检测 ===\n";
        crashLog += "检测时间: " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") + "\n";
        crashLog += "程序上次启动的系统时间: " + (m_flagBootTime.isValid() ? m_flagBootTime.toString("yyyy-MM-dd HH:mm:ss") : "未知") + "\n";
        crashLog += "程序最后活跃时间: " + (m_flagLastAlive.isValid() ? m_flagLastAlive.toString("yyyy-MM-dd HH:mm:ss") : "未知") + "\n";
        crashLog += "判定依据: 运行标志文件中的boot_id与当前启动周期一致，\n";
        crashLog += "          表明程序在上次运行中崩溃或被强制结束（如 kill -9、OOM），\n";
        crashLog += "          这不是系统非法关机，不计入非法关机次数。\n";
        if (!m_cachedRecoveryLogs.isEmpty()) {
            crashLog += "\n检测证据:\n" + m_cachedRecoveryLogs + "\n";
        }
        saveAppCrashLog(crashLog);
        emit appCrashDetected(QString("检测到程序上次异常退出（崩溃或被强制结束，boot_id一致），已记录到崩溃日志，不计入非法关机"));
        qDebug() << "程序异常退出已记录（不计入非法关机）";
        return qMakePair(QDateTime(), QString());
    }

    // 只有明确判定为非法关机时才继续处理
    if (shutdownType != SHUTDOWN_ILLEGAL) {
        qDebug() << "判定为正常关机/重启，不记录非法关机";
        return qMakePair(QDateTime(), QString());
    }

    // 到达此处说明确实是非法关机，获取详细信息
    QString detectionLog = m_cachedRecoveryLogs;

    // 确定关机时间
    QDateTime shutdownTime;

    // 优先使用标志文件中的最后活跃时间（仅标志文件为主证据时有效）：
    // 断电发生在 last_alive_time 之后、本次开机之前，
    // last_alive_time 是距断电时刻最近的已知时间（每60秒刷新一次），
    // 比使用系统启动时间更接近真实断电时刻
    if (m_flagBasedDetection && m_flagLastAlive.isValid()) {
        shutdownTime = m_flagLastAlive;
        qDebug() << "使用标志文件最后活跃时间作为断电时间近似:" << shutdownTime.toString("yyyy-MM-dd HH:mm:ss");
    } else {
        // 尝试从恢复日志中解析时间（取最后一个时间戳，最接近断电时刻）
        shutdownTime = parseShutdownTimeFromRecoveryLogs(detectionLog);
    }

    if (!shutdownTime.isValid()) {
        // 使用系统启动时间作为关机时间的近似值
        shutdownTime = getSystemBootTime();
        if (shutdownTime.isValid()) {
            qDebug() << "使用系统启动时间作为关机时间:" << shutdownTime.toString("yyyy-MM-dd HH:mm:ss");
        }
    }

    if (!shutdownTime.isValid()) {
        // 最后备选：当前时间减去300秒
        shutdownTime = QDateTime::currentDateTime().addSecs(-300);
        qDebug() << "无法获取准确时间，使用估计关机时间:" << shutdownTime.toString("yyyy-MM-dd HH:mm:ss");
    }

    qDebug() << "解析到的关机时间:" << shutdownTime.toString("yyyy-MM-dd HH:mm:ss");

    // 检查关机时间是否在上次检测时间之后（防止重复计数已处理过的事件）
    // 注意：标志文件为主证据时跳过此检查——标志文件本身具有天然去重性
    // （每次检测后会用当前启动周期信息重写标志文件，同一非法事件只会被检测到一次）
    if (sinceTime.isValid() && !m_flagBasedDetection) {
        if (shutdownTime <= sinceTime) {
            qDebug() << "非法关机时间(" << shutdownTime.toString("yyyy-MM-dd HH:mm:ss")
                     << ")在上次检测时间(" << sinceTime.toString("yyyy-MM-dd HH:mm:ss")
                     << ")之前或相同，忽略此事件";
            return qMakePair(QDateTime(), QString());
        }
    }

    // 构建日志内容
    QString shutdownLog;
    shutdownLog += "=== 非法关机检测结果 ===\n";
    shutdownLog += "检测方法: 标志文件+boot_id+系统证据仲裁\n";
    shutdownLog += "关机时间: " + shutdownTime.toString("yyyy-MM-dd HH:mm:ss") + "\n";
    if (m_flagBasedDetection && m_flagLastAlive.isValid()) {
        shutdownLog += "时间说明: 断电发生在程序最后活跃时间(" + m_flagLastAlive.toString("yyyy-MM-dd HH:mm:ss")
                     + ")之后、本次系统启动之前，为近似估算\n";
    }
    shutdownLog += "检测时间: " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") + "\n";
    shutdownLog += "上次检测时间: " + (sinceTime.isValid() ? sinceTime.toString("yyyy-MM-dd HH:mm:ss") : "从未检测") + "\n";
    shutdownLog += "=====================\n\n";

    // 添加详细的判断过程
    shutdownLog += getShutdownReasonLog() + "\n";

    if (!detectionLog.isEmpty()) {
        shutdownLog += "检测证据:\n";
        shutdownLog += detectionLog + "\n";
    }

    // 判断依据说明
    shutdownLog += "\n最终判断依据:\n";
    shutdownLog += "1. 运行标志文件机制：软件运行时创建标志文件（含boot_id），正常退出时删除\n";
    shutdownLog += "2. 标志文件存在且boot_id与当前启动周期不同 → 程序运行期间系统未走正常退出流程\n";
    shutdownLog += "3. 系统证据仲裁：journalctl/last -x/dmesg 区分断电与正常关机但程序被漏杀\n";
    shutdownLog += "4. 标志文件不存在时：由系统级证据（journalctl/last -x/dmesg）判定软件未运行期间的断电\n";
    shutdownLog += "结论: 确认非法关机（断电/长按电源/强制关机导致系统未正常关机）\n";

    return qMakePair(shutdownTime, shutdownLog.trimmed());
}

void IllegalShutdownDetector::setLastCheckTime(const QDateTime &time)
{
    lastCheckTime = time;
}

QDateTime IllegalShutdownDetector::getLastCheckTime() const
{
    return lastCheckTime;
}

void IllegalShutdownDetector::saveCheckTime()
{
    QSettings settings("YanSheng", "TestClient");
    settings.setValue("illegal_shutdown_last_check", lastCheckTime);
    settings.sync();
    qDebug() << "保存检测时间:" << lastCheckTime.toString("yyyy-MM-dd HH:mm:ss");
}

void IllegalShutdownDetector::loadCheckTime()
{
    QSettings settings("YanSheng", "TestClient");
    lastCheckTime = settings.value("illegal_shutdown_last_check", QDateTime()).toDateTime();

    if (!lastCheckTime.isValid()) {
        qDebug() << "首次运行或未找到上次检测时间";
    } else {
        qDebug() << "加载上次检测时间:" << lastCheckTime.toString("yyyy-MM-dd HH:mm:ss");
    }
}

// =================================================================
// ========== 标志文件机制实现（主要检测方法） ==========
// =================================================================

// 获取运行标志文件路径（静态方法）
QString IllegalShutdownDetector::getRunningFlagPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dir).filePath("running.flag");
}

// 获取当前启动周期标识（boot_id：每次开机唯一，Linux 2.6+内核均支持，与CPU架构无关）
QString IllegalShutdownDetector::getCurrentBootId()
{
#ifdef Q_OS_LINUX
    QFile f("/proc/sys/kernel/random/boot_id");
    if (f.open(QIODevice::ReadOnly)) {
        QString id = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        if (!id.isEmpty()) return id;
    }
#endif
    return QString();
}

#ifdef Q_OS_LINUX
// 安装 SIGTERM/SIGINT 处理器
bool IllegalShutdownDetector::installTerminationHandlers()
{
    if (g_flagPathBytes.isEmpty()) {
        g_flagPathBytes = QFile::encodeName(getRunningFlagPath());
    }

    struct sigaction sa = {};
    sa.sa_handler = terminationSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (::sigaction(SIGTERM, &sa, nullptr) != 0) {
        qWarning() << "安装SIGTERM处理器失败";
        return false;
    }
    if (::sigaction(SIGINT, &sa, nullptr) != 0) {
        qWarning() << "安装SIGINT处理器失败";
        return false;
    }

    qDebug() << "已安装SIGTERM/SIGINT信号处理器（防止桌面环境直杀进程导致非法关机误报）";
    return true;
}
#endif

// 解析标志文件内容中的 key=value
QString IllegalShutdownDetector::parseFlagValue(const QString &content, const QString &key)
{
    const QStringList lines = content.split('\n');
    for (const QString &line : lines) {
        QString t = line.trimmed();
        if (t.startsWith(key + "=")) {
            return t.mid(key.length() + 1).trimmed();
        }
    }
    return QString();
}

// 同步文件所在目录（确保文件的创建/删除目录项落盘，防止断电后目录元数据丢失）
void IllegalShutdownDetector::fsyncParentDir(const QString &filePath)
{
#ifdef Q_OS_LINUX
    QDir dir = QFileInfo(filePath).absoluteDir();
    int fd = ::open(QFile::encodeName(dir.absolutePath()).constData(), O_RDONLY);
    if (fd != -1) {
        ::fsync(fd);
        ::close(fd);
    }
#else
    Q_UNUSED(filePath);
#endif
}

// 标记正常关机 - 删除运行标志文件（静态方法，供 SystemController 等模块调用）
void IllegalShutdownDetector::markCleanShutdown()
{
    QString flagPath = getRunningFlagPath();
    QFile flagFile(flagPath);
    if (flagFile.exists()) {
        bool removed = false;
        for (int attempt = 0; attempt < 2 && !removed; ++attempt) {
            removed = flagFile.remove();
            if (!removed) {
                qWarning() << "警告: 删除运行标志文件失败(第" << (attempt + 1) << "次):" << flagPath
                           << "错误:" << flagFile.errorString();
            }
        }
        if (removed) {
            fsyncParentDir(flagPath);  // 确保删除操作真正落盘
            qDebug() << "已删除运行标志文件（标记正常关机）:" << flagPath;
        } else {
            // 删除失败必须显式报告：标志文件残留会导致下次启动误报非法关机
            qWarning() << "*** 严重警告: 运行标志文件删除失败，下次启动可能误报非法关机! 路径:" << flagPath << "***";
        }
    } else {
        qDebug() << "运行标志文件不存在，无需删除:" << flagPath;
    }
}

// 获取休眠标记文件路径（静态方法，与 running.flag 同目录）
QString IllegalShutdownDetector::getHibernateMarkerPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dir).filePath("hibernate.marker");
}

// 获取 suspend 标记文件路径(2026-09-11 新增,飞腾真机修复)
QString IllegalShutdownDetector::getSuspendMarkerPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dir).filePath("suspend.marker");
}

// 写休眠标记（静态方法，SystemController::hibernate() 在发命令前调用）
// 关键要求：marker 必须在断电前真正落盘（文件内容 + 目录项都 fsync），
// 否则休眠唤醒后 marker 丢失，仍会被误判为非法关机
void IllegalShutdownDetector::markHibernate()
{
    QString markerPath = getHibernateMarkerPath();

    // 确保目录存在（与 running.flag 同目录，一般已存在）
    QDir dir = QFileInfo(markerPath).absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QFile markerFile(markerPath);
    if (markerFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QString content;
        content += "hibernate_start_time=" + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") + "\n";
        content += "pid=" + QString::number(QCoreApplication::applicationPid()) + "\n";

        markerFile.write(content.toUtf8());
        markerFile.flush();
#ifdef Q_OS_LINUX
        // fsync 文件内容（机器马上要断电，必须确保数据真正写入磁盘）
        if (markerFile.handle() != -1) {
            ::fsync(markerFile.handle());
        }
#endif
        markerFile.close();
        // fsync 父目录（确保目录项落盘：文件"存在"这件事本身也要持久化）
        fsyncParentDir(markerPath);
        qDebug() << "已写入休眠标记（软件主动休眠，唤醒后不计非法关机）:" << markerPath;
    } else {
        qWarning() << "*** 警告: 休眠标记写入失败，休眠唤醒后可能被误判为非法关机! 路径:" << markerPath
                   << "错误:" << markerFile.errorString();
    }
}

// 检查休眠标记是否存在（存在返回 true，并输出 marker 记录的写入时间）
bool IllegalShutdownDetector::checkHibernateMarker(QDateTime *markerTime)
{
    QString markerPath = getHibernateMarkerPath();
    QFile markerFile(markerPath);

    if (!markerFile.exists()) {
        return false;
    }
    if (!markerFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // 文件存在但读不了 —— 按存在处理（从宽：宁可漏报不误报）
        return true;
    }
    QString content = QString::fromUtf8(markerFile.readAll());
    markerFile.close();

    if (markerTime) {
        QString timeStr = parseFlagValue(content, "hibernate_start_time");
        if (!timeStr.isEmpty()) {
            *markerTime = QDateTime::fromString(timeStr, "yyyy-MM-dd HH:mm:ss");
        }
    }
    return true;
}

// 清除休眠标记（静态方法）
void IllegalShutdownDetector::clearHibernateMarker()
{
    QString markerPath = getHibernateMarkerPath();
    QFile markerFile(markerPath);
    if (markerFile.exists()) {
        if (markerFile.remove()) {
            fsyncParentDir(markerPath);
            qDebug() << "已清除休眠标记:" << markerPath;
        } else {
            qWarning() << "警告: 清除休眠标记失败:" << markerPath << markerFile.errorString();
        }
    }
}

// ---- 计划内电源操作标记（2026-09-22 循环重启误判修复） ----
// 设计目标:软件主动 reboot/shutdown 后,下次启动确定性判定"正常",
//          与 running.flag 删除成败、journalctl/wtmp 证据可用性完全解耦。
// 关键:marker 必须在电源命令发出前落盘(双 fsync),跨启动周期保留,
//      启动时以"marker.boot_id != 当前boot_id 且 距开机<=1800秒"为判据。
QString IllegalShutdownDetector::getPlannedPowerMarkerPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dir).filePath("planned_power.marker");
}

void IllegalShutdownDetector::markPlannedPower(const QString &intent)
{
    QString markerPath = getPlannedPowerMarkerPath();
    QDir dir = QFileInfo(markerPath).absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    QFile markerFile(markerPath);
    if (markerFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QString content;
        content += "intent=" + intent + "\n";
        content += "mark_time=" + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") + "\n";
        content += "boot_id=" + getCurrentBootId() + "\n";
        content += "pid=" + QString::number(QCoreApplication::applicationPid()) + "\n";
        markerFile.write(content.toUtf8());
        markerFile.flush();
#ifdef Q_OS_LINUX
        if (markerFile.handle() != -1) {
            ::fsync(markerFile.handle());
        }
#endif
        markerFile.close();
        fsyncParentDir(markerPath);
        qDebug() << "已写入计划内电源操作标记:" << markerPath << "intent:" << intent;
    } else {
        qWarning() << "*** 警告: 计划内电源操作标记写入失败! 路径:" << markerPath
                   << "错误:" << markerFile.errorString();
    }
}

bool IllegalShutdownDetector::checkPlannedPowerMarker(QString *intent,
                                                       QDateTime *markerTime,
                                                       QString *bootId)
{
    QString markerPath = getPlannedPowerMarkerPath();
    QFile markerFile(markerPath);
    if (!markerFile.exists()) {
        return false;
    }
    if (!markerFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return true;  // 存在但读不了 → 按存在处理（从宽）
    }
    QString content = QString::fromUtf8(markerFile.readAll());
    markerFile.close();

    if (intent)     *intent = parseFlagValue(content, "intent");
    if (bootId)     *bootId = parseFlagValue(content, "boot_id");
    if (markerTime) {
        QString t = parseFlagValue(content, "mark_time");
        if (!t.isEmpty()) {
            *markerTime = QDateTime::fromString(t, "yyyy-MM-dd HH:mm:ss");
        }
    }
    return true;
}

void IllegalShutdownDetector::clearPlannedPowerMarker()
{
    QString markerPath = getPlannedPowerMarkerPath();
    QFile markerFile(markerPath);
    if (markerFile.exists()) {
        if (markerFile.remove()) {
            fsyncParentDir(markerPath);
            qDebug() << "已清除计划内电源操作标记:" << markerPath;
        } else {
            qWarning() << "警告: 清除计划内电源操作标记失败:" << markerPath
                       << markerFile.errorString();
        }
    }
}

// 电源命令全部失败后重建运行标志：
// markCleanShutdown() 已删标志但机器没有关机 → 程序继续运行期间真实断电会被漏报，
// 必须重建标志（原位写，含当前 boot_id，双 fsync）
void IllegalShutdownDetector::restoreRunningFlagAfterPowerAbort()
{
    QString flagPath = getRunningFlagPath();
    QDir dir = QFileInfo(flagPath).absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    QFile flagFile(flagPath);
    if (flagFile.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)) {
        QDateTime now = QDateTime::currentDateTime();
        QDateTime bootTime = getSystemBootTime();
        QString bootId = getCurrentBootId();
        QString content;
        content += "last_alive_time=" + now.toString("yyyy-MM-dd HH:mm:ss") + "\n";
        content += "boot_time=" + (bootTime.isValid() ? bootTime.toString("yyyy-MM-dd HH:mm:ss") : "unknown") + "\n";
        content += "boot_id=" + bootId + "\n";
        content += "pid=" + QString::number(QCoreApplication::applicationPid()) + "\n";
        flagFile.write(content.toUtf8());
        flagFile.flush();
#ifdef Q_OS_LINUX
        if (flagFile.handle() != -1) {
            ::fsync(flagFile.handle());
        }
#endif
        flagFile.close();
        fsyncParentDir(flagPath);
        qDebug() << "电源命令失败，已重建运行标志文件:" << flagPath;
    }
}

// ---- suspend 期间断电检测标记（2026-09-11 新增） ----
// 设计目标:用户对挂起的系统直接断电,软件应当能识别为非法关机。
// 关键:marker 必须在 suspend 命令发出前落盘,断电后保留;
//      启动时通过 marker 时间 vs 系统启动时间 的差值推断"是否被立即断电"。
void IllegalShutdownDetector::markSuspend()
{
    QString markerPath = getSuspendMarkerPath();
    QDir dir = QFileInfo(markerPath).absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    QFile markerFile(markerPath);
    if (markerFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QString content;
        // 关键:写入当前 RTC 时间,启动时与 bootTime 比较判断"立即断电"还是"正常关机"
        content += "suspend_start_time=" + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") + "\n";
        content += "pid=" + QString::number(QCoreApplication::applicationPid()) + "\n";
        markerFile.write(content.toUtf8());
        markerFile.flush();
#ifdef Q_OS_LINUX
        if (markerFile.handle() != -1) {
            ::fsync(markerFile.handle());
        }
#endif
        markerFile.close();
        fsyncParentDir(markerPath);
        qDebug() << "已写入 suspend 标记:" << markerPath;
    } else {
        qWarning() << "*** 警告: suspend 标记写入失败,断电可能漏判 *** 路径:" << markerPath
                   << "错误:" << markerFile.errorString();
    }
}

bool IllegalShutdownDetector::checkSuspendMarker(QDateTime *markerTime)
{
    QString markerPath = getSuspendMarkerPath();
    QFile markerFile(markerPath);
    if (!markerFile.exists()) {
        return false;
    }
    if (!markerFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // 文件存在但读不了 → 按存在处理(从宽)
        return true;
    }
    QString content = QString::fromUtf8(markerFile.readAll());
    markerFile.close();
    if (markerTime) {
        QString timeStr = parseFlagValue(content, "suspend_start_time");
        if (!timeStr.isEmpty()) {
            *markerTime = QDateTime::fromString(timeStr, "yyyy-MM-dd HH:mm:ss");
        }
    }
    return true;
}

void IllegalShutdownDetector::clearSuspendMarker()
{
    QString markerPath = getSuspendMarkerPath();
    QFile markerFile(markerPath);
    if (markerFile.exists()) {
        if (markerFile.remove()) {
            fsyncParentDir(markerPath);
            qDebug() << "已清除 suspend 标记:" << markerPath;
        } else {
            qWarning() << "警告: 清除 suspend 标记失败:" << markerPath << markerFile.errorString();
        }
    }
}

// 检查运行标志文件是否存在（存在则解析其中的boot_id/最后活跃时间/启动时间）
bool IllegalShutdownDetector::checkRunningFlag(QString *info)
{
    m_runningFlagPath = getRunningFlagPath();
    QFile flagFile(m_runningFlagPath);

    // 重置解析缓存
    m_flagBootId.clear();
    m_flagLastAlive = QDateTime();
    m_flagBootTime = QDateTime();

    if (!flagFile.exists()) {
        qDebug() << "运行标志文件不存在，上次正常退出:" << m_runningFlagPath;
        return false;
    }

    qDebug() << "*** 运行标志文件存在！上次可能发生异常 ***" << m_runningFlagPath;

    QString content;
    if (flagFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        content = QString::fromUtf8(flagFile.readAll());
        flagFile.close();
        qDebug() << "标志文件内容:\n" << content;
    } else {
        content = QString();
    }

    // 解析标志文件中记录的信息（新版格式含boot_id）
    m_flagBootId = parseFlagValue(content, "boot_id");
    QString lastAliveStr = parseFlagValue(content, "last_alive_time");
    if (!lastAliveStr.isEmpty()) {
        m_flagLastAlive = QDateTime::fromString(lastAliveStr, "yyyy-MM-dd HH:mm:ss");
    }
    QString bootTimeStr = parseFlagValue(content, "boot_time");
    if (!bootTimeStr.isEmpty() && bootTimeStr != "unknown") {
        m_flagBootTime = QDateTime::fromString(bootTimeStr, "yyyy-MM-dd HH:mm:ss");
    }

    if (info) {
        if (!content.isEmpty()) {
            *info = content;
        } else {
            *info = "（无法读取标志文件内容）";
        }
    }

    return true;
}

// 创建/原位重写运行标志文件
// 关键改进：使用 ReadWrite|Truncate 原位截断重写（文件已存在时保持inode不变），
// 消除旧实现"先删除再创建"间隙断电导致的漏报窗口
void IllegalShutdownDetector::createRunningFlag()
{
    m_runningFlagPath = getRunningFlagPath();

    // 确保目录存在
    QDir dir = QFileInfo(m_runningFlagPath).absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QFile flagFile(m_runningFlagPath);
    if (flagFile.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)) {
        QDateTime now = QDateTime::currentDateTime();
        QDateTime bootTime = getSystemBootTime();
        QString bootId = m_currentBootId.isEmpty() ? getCurrentBootId() : m_currentBootId;

        // 直接用 write + toUtf8 写入，避免 QTextStream 编码问题
        QString content;
        content += "last_alive_time=" + now.toString("yyyy-MM-dd HH:mm:ss") + "\n";
        content += "boot_time=" + (bootTime.isValid() ? bootTime.toString("yyyy-MM-dd HH:mm:ss") : "unknown") + "\n";
        content += "boot_id=" + bootId + "\n";
        content += "pid=" + QString::number(QCoreApplication::applicationPid()) + "\n";

        flagFile.write(content.toUtf8());
        flagFile.flush();

#ifdef Q_OS_LINUX
        // 强制写入物理磁盘，防止非法关机时标志文件丢失
        if (flagFile.handle() != -1) {
            ::fsync(flagFile.handle());
        }
#endif

        flagFile.close();

        // 同步父目录，确保新建文件的目录项落盘（新建文件时inode/目录项需持久化）
        fsyncParentDir(m_runningFlagPath);

        // 更新缓存
        m_flagBootId = bootId;
        m_flagLastAlive = now;
        m_flagBootTime = bootTime;

        qDebug() << "已创建运行标志文件:" << m_runningFlagPath;
    } else {
        qDebug() << "警告: 无法创建运行标志文件:" << m_runningFlagPath
                 << "错误:" << flagFile.errorString();
    }
}

// 定期刷新标志文件的 last_alive_time（每60秒，fsync落盘）
// 作用1：将断电时间估算窗口从"上次软件启动~本次开机"缩小到1分钟内
// 注意：仅在文件已存在时刷新，绝不重新创建——
// 正常退出流程（markCleanShutdown）删除标志后到进程真正退出之间定时器可能触发，
// 此时重建标志会导致下次启动误报非法关机
void IllegalShutdownDetector::refreshRunningFlag()
{
    if (m_runningFlagPath.isEmpty()) {
        m_runningFlagPath = getRunningFlagPath();
    }

    QFileInfo fi(m_runningFlagPath);
    if (!fi.exists()) {
        return;  // 标志已被正常退出流程删除，不重建
    }

    QFile flagFile(m_runningFlagPath);
    if (flagFile.open(QIODevice::ReadWrite | QIODevice::Truncate | QIODevice::Text)) {
        QDateTime now = QDateTime::currentDateTime();
        QString bootId = m_currentBootId.isEmpty() ? getCurrentBootId() : m_currentBootId;

        QString content;
        content += "last_alive_time=" + now.toString("yyyy-MM-dd HH:mm:ss") + "\n";
        content += "boot_time=" + (m_flagBootTime.isValid() ? m_flagBootTime.toString("yyyy-MM-dd HH:mm:ss") : "unknown") + "\n";
        content += "boot_id=" + bootId + "\n";
        content += "pid=" + QString::number(QCoreApplication::applicationPid()) + "\n";

        flagFile.write(content.toUtf8());
        flagFile.flush();
#ifdef Q_OS_LINUX
        if (flagFile.handle() != -1) {
            ::fsync(flagFile.handle());
        }
#endif
        flagFile.close();

        m_flagLastAlive = now;
    }
}

// =================================================================
// ========== 系统级检测方法实现（辅助验证/仲裁） ==========
// =================================================================

// 通过 journalctl 检查指定启动周期是否正常关机
// 原理：检查该启动周期最后几条日志是否包含正常关机流程
// 正常关机日志特征：Reached target Shutdown / Powering off / System halting 等
// 非法关机特征：日志突然中断，没有关机流程记录
// reliable语义：true=journalctl有输出且做出了可靠判断；false=能力不足（无权限/
// 日志易失/超时/指定启动周期不存在），此时上层应降级到其他证据
bool IllegalShutdownDetector::checkLastBootCleanShutdown(QString *logInfo, bool *reliable, const QString &bootId)
{
    if (reliable) *reliable = false;
    qDebug() << "=== 检查上次启动是否正常关机（journalctl） ===";
    // 在非Linux编译路径下也"使用"参数，避免 [-Wunused-parameter] 错误（参数签名需保留）
    (void)bootId;

#ifdef Q_OS_LINUX
    // bootId非空时精确检查该启动周期（用于标志文件仲裁）；否则检查上一次启动(-b -1)
    // 注意：取最近150行并在全部范围内查找关机关键词——部分系统关机序列长，
    // 关键词可能不在末尾25行内（ ACPI 噪声/固件消息垫在最后），窗口过小会误判为非法关机
    QString command;
    if (!bootId.isEmpty()) {
        command = QString("journalctl -b %1 --no-pager -n 150 2>/dev/null").arg(bootId);
    } else {
        command = "journalctl -b -1 --no-pager -n 150 2>/dev/null";
    }

    QString output = runShellCommand(command, 5000);

    if (output.isEmpty()) {
        // 可能为：指定的启动周期不在日志中（日志轮转/易失存储）、无权限、无上次启动记录
        qDebug() << "journalctl 无输出（可能无权限/日志易失/启动记录不存在），跳过此检测";
        if (logInfo) *logInfo = "journalctl 无输出（无权限或日志未持久化，检测能力降级）";
        return true;  // 不阻断
    }

    if (logInfo) {
        *logInfo = output;
    }

    // 在全部输出范围内查找正常关机关键词（关键词仅在关机流程出现，全文匹配无误报风险）
    QString recentText = output;

    // 正常关机/重启的关键词（系统关机流程中会出现的日志）
    QStringList cleanShutdownKeywords;
    cleanShutdownKeywords << "Reached target Shutdown"
                          << "Starting Power-Off"
                          << "Starting Reboot"
                          << "System halting"
                          << "System powering down"
                          << "reboot: Power down"
                          << "reboot: Restarting system"
                          << "Powering off"
                          << "powering down"
                          << "acpid.*power button"
                          << "Shutting down"
                          << "Stopping target"
                          << "Reached target.*Shutdown"
                          << "Reached target Final"
                          << "systemd-logind.*Power"
                          << "Requesting system poweroff"
                          << "Requesting system reboot"
                          << "Unmounted.*file system";

    for (const QString &keyword : cleanShutdownKeywords) {
        QRegularExpression re(keyword, QRegularExpression::CaseInsensitiveOption);
        if (re.match(recentText).hasMatch()) {
            qDebug() << "发现正常关机关键词:" << keyword;
            if (reliable) *reliable = true;
            return true;  // 正常关机（可靠判断）
        }
    }

    qDebug() << "*** 未发现正常关机关键词，可能是非法关机 ***";
    if (reliable) *reliable = true;  // 有输出且无关机流程 → 可靠判断为异常
    return false;

#else
    qDebug() << "非Linux系统，跳过journalctl检测";
    if (logInfo) *logInfo = "非Linux系统";
    return true;
#endif
}

// 通过 last -x 检查正常关机记录
// 原理：last -x 按时间倒序排列，当前启动在前
// 正常关机后重启：当前reboot(still running) → shutdown记录 → 上次reboot
// 非法关机后重启：当前reboot(still running) → 上次reboot（无对应shutdown）
// flagBootTime非空时：精确定位该启动周期在wtmp中的记录，检查其上方相邻记录
// shutdownEndTime输出参数：最近一条shutdown记录的结束时间（供调用方做陈旧性校验）
bool IllegalShutdownDetector::checkNormalShutdownByLast(bool *reliable, QDateTime *shutdownEndTime, const QDateTime &flagBootTime)
{
    if (reliable) *reliable = false;
    if (shutdownEndTime) *shutdownEndTime = QDateTime();
    qDebug() << "=== 检查正常关机记录（last -x） ===";
    // 在非Linux编译路径下也"使用"参数，避免 [-Wunused-parameter] 错误
    (void)flagBootTime;

#ifdef Q_OS_LINUX
    // 使用 -F 输出完整时间戳（含年份），C locale 避免本地化
    QString output = runShellCommand("last -x -F 2>/dev/null | head -12", 2000);
    qDebug() << "last -x 输出:\n" << output;

    if (output.isEmpty()) {
        qDebug() << "last -x 无输出（命令不存在或wtmp缺失），跳过此检测";
        return true;  // 不阻断
    }

    // 兼容 Qt 5.0 - 5.13（使用 QString::SkipEmptyParts）和 Qt 5.14+（使用 Qt::SkipEmptyParts）
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
#else
    QStringList lines = output.split('\n', QString::SkipEmptyParts);
#endif

    // ---------- 模式A：指定了标志文件对应的启动时间（标志文件仲裁路径） ----------
    // 在wtmp记录中定位该启动周期的reboot记录，检查其上方（时间上更新）的
    // 第一条关机/重启记录：shutdown → 该周期正常关机；reboot → 该周期异常结束
    if (flagBootTime.isValid()) {
        struct LastEntry {
            bool isShutdown;
            bool isReboot;
            QDateTime startTime;
        };
        QList<LastEntry> entries;  // 按时间倒序（最新在前）

        for (const QString &line : lines) {
            QString t = line.trimmed();
            if (t.isEmpty()) continue;
            LastEntry e;
            e.isShutdown = t.startsWith("shutdown") || t.contains("system down");
            e.isReboot = t.startsWith("reboot") || t.contains("system boot");
            e.startTime = parseLastFullTimestamp(t, true);  // 记录开始时间
            if (e.isShutdown || e.isReboot) {
                entries.append(e);
            }
        }

        // 定位标志文件对应的启动记录（时间容差±120秒，兼容时钟误差）
        int flagIdx = -1;
        for (int i = 0; i < entries.size(); ++i) {
            if (entries[i].isReboot && entries[i].startTime.isValid()) {
                if (qAbs(entries[i].startTime.secsTo(flagBootTime)) <= 120) {
                    flagIdx = i;
                    break;
                }
            }
        }

        if (flagIdx >= 0) {
            // 检查该记录上方（时间更新）的第一条关机/重启记录
            for (int j = flagIdx - 1; j >= 0; --j) {
                if (entries[j].isShutdown) {
                    qDebug() << "定位到标志所在启动周期，其上方为shutdown记录 → 该周期正常关机";
                    // 找到对应的原始行以解析结束时间
                    for (const QString &line : lines) {
                        QString t = line.trimmed();
                        if ((t.startsWith("shutdown") || t.contains("system down"))) {
                            QDateTime end = parseLastFullTimestamp(t, false);
                            if (shutdownEndTime) *shutdownEndTime = end;
                            break;
                        }
                    }
                    if (reliable) *reliable = true;
                    return true;
                }
                if (entries[j].isReboot) {
                    qDebug() << "定位到标志所在启动周期，其上方为下一次reboot（无shutdown）→ 该周期异常结束";
                    if (reliable) *reliable = true;
                    return false;
                }
            }
            // 上方没有更多关机/重启记录（理论上是当前启动），无法判断
            qDebug() << "标志所在启动周期上方无有效记录，跳过此检测";
            return true;
        }

        // 未在最近的wtmp记录中定位到该启动周期（记录过旧或时间不匹配）
        qDebug() << "未在last -x最近记录中定位到标志对应的启动周期，降级为通用判定";
    }

    // ---------- 模式B：通用判定（未指定启动周期 / 定位失败） ----------
    // 查找当前启动后第一条有效记录：shutdown → 正常；reboot → 非法
    QString shutdownLine;
    for (const QString &line : lines) {
        QString trimmedLine = line.trimmed();
        if (trimmedLine.isEmpty()) continue;
        if (trimmedLine.contains("still running")) continue;

        if (trimmedLine.startsWith("shutdown") || trimmedLine.contains("system down")) {
            shutdownLine = trimmedLine;
            break;
        }

        if (trimmedLine.startsWith("reboot") || trimmedLine.contains("system boot")) {
            // 当前启动后的第一个非still running记录是reboot而不是shutdown
            // 说明上次没有正常关机（wtmp有维护的系统中此判定可靠）
            qDebug() << "*** 当前启动后第一条记录是reboot而非shutdown，可能是非法关机:" << trimmedLine.left(80);
            if (reliable) *reliable = true;
            return false;
        }

        if (trimmedLine.contains("runlevel")) continue;
        // 用户登录等其他记录，继续检查下一条
    }

    if (shutdownLine.isEmpty()) {
        // 无有效关机/重启记录（wtmp不完整），无法判断
        qDebug() << "last -x 无法确定关机方式，跳过此检测";
        return true;  // 不阻断
    }

    if (shutdownEndTime) {
        *shutdownEndTime = parseLastFullTimestamp(shutdownLine, false);  // shutdown记录的结束时间
    }

    // 关键校验：该shutdown记录必须对应"本次开机前"的最后一次关机。
    // 正常流程：shutdown结束时间紧邻本次系统启动时间（POST仅需数秒到数分钟）。
    // 若最近一条shutdown记录远早于本次系统启动时间（超过10分钟），
    // 说明上一次启动周期是异常结束的（未写入shutdown记录）→ 非法关机。
    QDateTime bootTime = getSystemBootTime();
    QDateTime sEnd = shutdownEndTime ? *shutdownEndTime : QDateTime();
    if (bootTime.isValid() && sEnd.isValid()) {
        qint64 gap = sEnd.secsTo(bootTime);
        if (gap >= 0 && gap > 600) {
            qDebug() << "*** 最近shutdown记录距本次启动" << (gap / 60) << "分钟（陈旧记录），"
                     << "上次启动周期未正常关机 → 非法关机";
            if (reliable) *reliable = true;
            return false;
        }
    }

    qDebug() << "找到shutdown记录，判定为正常关机:" << shutdownLine.left(80);
    if (reliable) *reliable = true;
    return true;

#else
    qDebug() << "非Linux系统，跳过last -x检测";
    return true;
#endif
}

// 检查本次启动的内核日志中是否存在文件系统恢复/日志重放记录
// 原理：ext4/xfs在上次未正常卸载后，本次挂载时会执行日志恢复并在内核日志中
// 留下记录。该证据不依赖journalctl持久化/wtmp，是无systemd环境的重要兜底。
// 注：正常关机会干净卸载文件系统，不会产生恢复记录（无误报）
bool IllegalShutdownDetector::checkDmesgFilesystemRecovery(QString *logInfo)
{
    qDebug() << "=== 检查文件系统恢复记录（dmesg） ===";

#ifdef Q_OS_LINUX
    // 匹配ext2/3/4和XFS的恢复类消息（带文件系统前缀，避免误匹配RAID重建等消息）
    QString output = runShellCommand(
        "dmesg 2>/dev/null | grep -iE \"(EXT[234]-fs|XFS) \\(.*\\):.*(recover|replay)\" | head -10",
        2000);

    if (output.isEmpty()) {
        qDebug() << "dmesg 无文件系统恢复记录（或无权限读取）";
        return false;
    }

    qDebug() << "★ dmesg 发现文件系统恢复记录:";
    qDebug() << output;
    if (logInfo) *logInfo = output;
    return true;
#else
    Q_UNUSED(logInfo);
    return false;
#endif
}
