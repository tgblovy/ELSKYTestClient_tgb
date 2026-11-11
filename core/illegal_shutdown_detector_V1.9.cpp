#include "illegal_shutdown_detector.h"
#include <QProcess>
#include <QFile>
#include <QTextStream>
#include <QSettings>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QMap>
#include <QCoreApplication>

IllegalShutdownDetector::IllegalShutdownDetector(QObject *parent)
    : QObject(parent), lastDetectedLog(""), illegalShutdownCount(0)
{
    qDebug() << "[DEBUG] IllegalShutdownDetector 构造函数开始";

    loadCheckTime();
    loadIllegalShutdownState();
    loadIllegalShutdownCount();  // 加载非法关机次数

    // 如果次数为0但有持久化状态，自动清除
    if (illegalShutdownCount <= 0 && hasPendingIllegalShutdown()) {
        qDebug() << "[DEBUG] 次数为0但有持久化状态，自动清除";
        clearIllegalShutdownState();
    }

    // 加载最近记录的非法关机时间
    QSettings settings("YanSheng", "TestClient");
    lastRecordedShutdownTime = settings.value("last_recorded_shutdown_time", QDateTime()).toDateTime();

    qDebug() << "[DEBUG] 构造函数结束，计数器:" << illegalShutdownCount;
    qDebug() << "[DEBUG] 是否有持久化状态:" << hasPendingIllegalShutdown();
}

IllegalShutdownDetector::~IllegalShutdownDetector()
{
    saveCheckTime();
}

// 新增：判断是否为同一个关机事件
bool IllegalShutdownDetector::isSameShutdownEvent(const QDateTime &newShutdownTime)
{
    if (!lastRecordedShutdownTime.isValid() || !newShutdownTime.isValid()) {
        return false;
    }

    // 如果两个时间在5分钟内，认为是同一个事件
    qint64 timeDiff = qAbs(lastRecordedShutdownTime.secsTo(newShutdownTime));
    return timeDiff < 300; // 5分钟内的认为是同一个事件
}

void IllegalShutdownDetector::checkOnStartup()
{
    qDebug() << "=== 开始检测非法关机 ===";
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

    // 检测非法关机
    QPair<QDateTime, QString> result = detectIllegalShutdown(lastCheckTime);
    QDateTime shutdownTime = result.first;
    QString illegalLog = result.second;

    if (!illegalLog.isEmpty()) {
        qDebug() << "检测到新的非法关机，关机时间：" << shutdownTime.toString("yyyy-MM-dd HH:mm:ss");
        qDebug() << "日志长度：" << illegalLog.length();

        // 获取当前检测时间
        QDateTime currentDetectionTime = QDateTime::currentDateTime();

        // 判断是否为新的非法关机事件
        bool isNewEvent = true;

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
                saveIllegalShutdownState(shutdownTime, illegalLog);
                emit illegalShutdownDetected(shutdownTime, illegalLog);
                saveDetectedLogToFile(illegalLog);
            }
        } else {
            qDebug() << "可能是重复检测，非法关机次数保持不变:" << illegalShutdownCount;
        }
    } else {
        qDebug() << "未检测到新的非法关机事件（可能已处理过或正常关机）";
    }

    // 更新检测时间
    lastCheckTime = QDateTime::currentDateTime();
    saveCheckTime();

    qDebug() << "更新检测时间到:" << lastCheckTime.toString("yyyy-MM-dd HH:mm:ss");
    qDebug() << "当前非法关机次数:" << illegalShutdownCount;
}

// 新增：获取最近记录的非法关机时间
QDateTime IllegalShutdownDetector::getLastRecordedShutdownTime() const
{
    return lastRecordedShutdownTime;
}

// 新增：保存非法关机状态到持久化存储
void IllegalShutdownDetector::saveIllegalShutdownState(const QDateTime &time, const QString &log)
{
    QSettings settings("YanSheng", "TestClient");
    settings.setValue("illegal_shutdown_persistent_time", time);
    settings.setValue("illegal_shutdown_persistent_log", log);

    persistentShutdownTime = time;
    persistentShutdownLog = log;

    qDebug() << "[DEBUG] 保存非法关机状态，关机时间:" << time.toString("yyyy-MM-dd HH:mm:ss");
    qDebug() << "[DEBUG] 当前非法关机次数:" << illegalShutdownCount;

    // 记录调用栈信息
    qDebug() << "[DEBUG] 保存状态的调用来源:";
    #ifdef QT_DEBUG
    qDebug() << "  " << Q_FUNC_INFO;
    #endif
}
// 新增：从持久化存储加载非法关机状态
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

// 新增：清除非法关机状态（用户手动清除时调用）
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

    persistentShutdownTime = QDateTime();
    persistentShutdownLog = QString();
    lastDetectedLog = QString();
    lastRecordedShutdownTime = QDateTime();
    illegalShutdownCount = 0;  // 确保计数器也清零

    qDebug() << "[DEBUG] 已彻底清除非法关机状态、记录的时间和计数器";
}

// 新增：检查是否有未处理的非法关机记录
bool IllegalShutdownDetector::hasPendingIllegalShutdown() const
{
    return persistentShutdownTime.isValid() && !persistentShutdownLog.isEmpty();
}

// 修改 getLastDetectedLog 函数，优先返回持久化存储的日志
QString IllegalShutdownDetector::getLastDetectedLog() const
{
    if (!lastDetectedLog.isEmpty()) {
        return lastDetectedLog;
    } else if (hasPendingIllegalShutdown()) {
        return persistentShutdownLog;
    }
    return QString();
}

// 新增：获取非法关机次数
int IllegalShutdownDetector::getIllegalShutdownCount() const
{
    return illegalShutdownCount;
}

// 新增：增加非法关机次数
void IllegalShutdownDetector::incrementIllegalShutdownCount()
{
    illegalShutdownCount++;
    saveIllegalShutdownCount();
    emit illegalShutdownCountChanged(illegalShutdownCount);
    qDebug() << "非法关机次数增加为:" << illegalShutdownCount;

    // 如果这是第一次非法关机（从0到1），需要确保有持久化状态
    if (illegalShutdownCount == 1 && !hasPendingIllegalShutdown()) {
        qDebug() << "首次非法关机，创建初始状态";
        // 这里需要重新检测并保存状态
        // 实际上，应该在 checkOnStartup 中处理
    }
}

// 新增：重置非法关机次数
void IllegalShutdownDetector::resetIllegalShutdownCount()
{
    illegalShutdownCount = 0;
    saveIllegalShutdownCount();
    emit illegalShutdownCountChanged(illegalShutdownCount);
    qDebug() << "非法关机次数已重置为0";
}

// 新增：保存非法关机次数
void IllegalShutdownDetector::saveIllegalShutdownCount()
{
    QSettings settings("YanSheng", "TestClient");
    settings.setValue("illegal_shutdown_count", illegalShutdownCount);
    qDebug() << "保存非法关机次数:" << illegalShutdownCount;
}

// 新增：加载非法关机次数
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
    process.start("uptime", QStringList() << "-s");
    if (process.waitForFinished(1000)) {
        QString output = process.readAllStandardOutput().trimmed();
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

    // 方法2: 通过/proc/uptime计算启动时间
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

    QString fileName = QString("illegal_shutdown_%1.log")
                      .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QString filePath = logDir.filePath(fileName);

    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "=== 非法关机检测日志 ===\n";
        out << "检测时间: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";
        out << "累计非法关机次数: " << illegalShutdownCount << " 次\n";
        out << "关机时间: " << (lastRecordedShutdownTime.isValid() ?
                               lastRecordedShutdownTime.toString("yyyy-MM-dd HH:mm:ss") : "未知") << "\n";
        out << "日志长度: " << log.length() << " 字符\n";
        out << "========================\n\n";
        out << log;
        file.close();
        qDebug() << "非法关机日志已保存到:" << filePath;
    }
}

QString IllegalShutdownDetector::filterSelfLogs(const QString &logs)
{
    QStringList lines = logs.split('\n');
    QStringList filteredLines;

    // 程序标识关键词，用于识别并过滤掉程序自身的日志
    QStringList selfKeywords;
    selfKeywords << "ELSKY_test_client_ft[";  // 程序进程名
    selfKeywords << "执行命令:";
    selfKeywords << "命令输出内容:";
    selfKeywords << "开始检测非法关机";
    selfKeywords << "=== 开始检测非法关机 ===";
    selfKeywords << "检测到非法关机";
    selfKeywords << "未检测到非法关机";
    selfKeywords << "更新检测时间到:";

    for (const QString &line : lines) {
        bool isSelfLog = false;

        // 检查是否包含自身程序的关键词
        for (const QString &keyword : selfKeywords) {
            if (line.contains(keyword)) {
                isSelfLog = true;
                break;
            }
        }

        // 如果不是自身程序的日志，则保留
        if (!isSelfLog) {
            filteredLines.append(line);
        }
    }

    return filteredLines.join('\n');
}

bool IllegalShutdownDetector::checkForFilesystemRecovery(QString *recoveryLogs)
{
    qDebug() << "=== 检查文件系统恢复记录 ===";

    bool foundRecovery = false;
    QString allLogs;

    // 方法1: 尝试使用journalctl（不需要sudo，但可能需要用户有journal访问权限）
    QStringList commands;
    commands << "journalctl -b 0 --no-pager | grep -iE \"unmounted.cleanly|unchecked|recovery|filesystem.*error|marking.*dirty\" | head -20";

    for (const QString &command : commands) {
        qDebug() << "执行命令:" << command;

        QProcess process;
        process.start("sh", QStringList() << "-c" << command);

        if (process.waitForFinished(3000)) {
            QString output = process.readAllStandardOutput().trimmed();
            QString errorOutput = process.readAllStandardError().trimmed();

            qDebug() << "命令输出长度:" << output.length();
            if (!errorOutput.isEmpty()) {
                qDebug() << "错误输出:" << errorOutput;
            }

            if (!output.isEmpty()) {
                // 过滤掉程序自身的日志
                QString filteredOutput = filterSelfLogs(output);

                // 只添加非空的过滤后日志
                if (!filteredOutput.isEmpty()) {
                    allLogs += "命令: " + command + "\n";
                    allLogs += filteredOutput + "\n\n";

                    qDebug() << "过滤后命令输出长度:" << filteredOutput.length();

                    // 检查是否包含"recovery complete"
                    if (filteredOutput.contains("recovery complete", Qt::CaseInsensitive)) {
                        qDebug() << "发现文件系统恢复完成记录，确认是非法关机";
                        foundRecovery = true;
                    }
                } else {
                    qDebug() << "命令输出全部被过滤（均为程序自身日志）";
                }
            }
        }
    }

    if (recoveryLogs) {
        *recoveryLogs = allLogs;
    }

    return foundRecovery;
}

QDateTime IllegalShutdownDetector::parseShutdownTimeFromRecoveryLogs(const QString &logs)
{
    // 尝试从恢复日志中解析时间
    QStringList lines = logs.split('\n');

    // 正则表达式匹配常见日志时间格式
    QRegularExpression timePattern(R"((\d{1,2}月\s+\d{1,2}\s+\d{2}:\d{2}:\d{2}))");
    QRegularExpression timePattern2(R"((\w{3}\s+\d{1,2}\s+\d{2}:\d{2}:\d{2}))");

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
                        QDateTime result(date, time);
                        qDebug() << "从恢复日志解析到时间:" << result.toString("yyyy-MM-dd HH:mm:ss");
                        return result;
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
                        QDateTime result(date, time);
                        qDebug() << "从恢复日志解析到时间(英文):" << result.toString("yyyy-MM-dd HH:mm:ss");
                        return result;
                    }
                }
            }
        }
    }

    qDebug() << "无法从恢复日志解析时间";
    return QDateTime();
}

QPair<QDateTime, QString> IllegalShutdownDetector::detectIllegalShutdown(const QDateTime &sinceTime)
{
    qDebug() << "=== 开始检测非法关机（简化版） ===";
    qDebug() << "上次检测时间:" << (sinceTime.isValid() ? sinceTime.toString("yyyy-MM-dd HH:mm:ss") : "无效");

    QString recoveryLogs;
    bool isIllegal = checkForFilesystemRecovery(&recoveryLogs);

    if (!isIllegal) {
        qDebug() << "文件系统检查：未发现recovery complete记录，正常关机";
        return qMakePair(QDateTime(), QString());
    }

    // 检测到非法关机，确定关机时间
    QDateTime shutdownTime;

    // 首先尝试从恢复日志中解析时间
    shutdownTime = parseShutdownTimeFromRecoveryLogs(recoveryLogs);

    if (!shutdownTime.isValid()) {
        // 如果无法解析，使用系统启动时间
        shutdownTime = getSystemBootTime();
        if (!shutdownTime.isValid()) {
            // 如果还是无法获取，使用当前时间减去300秒（5分钟）作为近似值
            shutdownTime = QDateTime::currentDateTime().addSecs(-300);
            qDebug() << "无法获取准确时间，使用估计关机时间:" << shutdownTime.toString("yyyy-MM-dd HH:mm:ss");
        } else {
            qDebug() << "使用系统启动时间作为关机时间:" << shutdownTime.toString("yyyy-MM-dd HH:mm:ss");
        }
    }

    qDebug() << "解析到的关机时间:" << shutdownTime.toString("yyyy-MM-dd HH:mm:ss");

    // 关键修复：检查关机时间是否在上次检测时间之后
    if (sinceTime.isValid()) {
        // 如果关机时间在上次检测时间之前，说明这个非法关机已经被处理过，忽略它
        if (shutdownTime <= sinceTime) {
            qDebug() << "非法关机时间(" << shutdownTime.toString("yyyy-MM-dd HH:mm:ss")
                     << ")在上次检测时间(" << sinceTime.toString("yyyy-MM-dd HH:mm:ss")
                     << ")之前或相同，忽略此事件";
            return qMakePair(QDateTime(), QString());
        }
    }

    // 构建日志内容
    QString shutdownLog;
    shutdownLog = "=== 非法关机检测结果 ===\n";
    shutdownLog += "检测方法: 文件系统恢复记录检查\n";
    shutdownLog += "关机时间: " + shutdownTime.toString("yyyy-MM-dd HH:mm:ss") + "\n";
    shutdownLog += "检测时间: " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") + "\n";
    shutdownLog += "上次检测时间: " + (sinceTime.isValid() ? sinceTime.toString("yyyy-MM-dd HH:mm:ss") : "从未检测") + "\n";
    shutdownLog += "=====================\n\n";

    if (!recoveryLogs.isEmpty()) {
        shutdownLog += "文件系统恢复日志（已过滤程序自身日志）:\n";
        shutdownLog += recoveryLogs + "\n";
    }

    // 添加判断依据说明
    shutdownLog += "\n判断依据:\n";
    shutdownLog += "1. 检测到包含 'recovery complete' 的记录\n";
    shutdownLog += "2. 这表明文件系统在启动时进行了恢复操作\n";
    shutdownLog += "3. 通常是由于非正常关机（如断电）导致\n";

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
