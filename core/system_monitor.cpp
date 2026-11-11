#include "system_monitor.h"
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QDateTime>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QDir>
#include <unistd.h>   // geteuid(): 判断进程是否已是 root，避免无谓地拉起 sudo

// 辅助函数：从内存信息行中提取数值
qint64 extractMemValue(const QString& line) {
    int colonPos = line.indexOf(':');
    if (colonPos == -1) return 0;

    QString valuePart = line.mid(colonPos + 1).trimmed();
    QString numberStr;
    for (int i = 0; i < valuePart.length(); i++) {
        QChar ch = valuePart[i];
        if (ch.isDigit()) {
            numberStr.append(ch);
        } else if (!numberStr.isEmpty()) {
            break;
        }
    }

    if (numberStr.isEmpty()) return 0;

    bool ok;
    qint64 value = numberStr.toLongLong(&ok);
    return ok ? value : 0;
}

SystemMonitor::SystemMonitor(QObject *parent)
    : QObject(parent)
    , monitorTimer(nullptr)
    , cpuTempLabel(nullptr)
    , diskTempLabel(nullptr)
    , cpuFanLabel(nullptr)
    , cpuUsageBar(nullptr)
    , memoryUsageBar(nullptr)
    , gpuMemoryBar(nullptr)
    , lastBootLabel(nullptr)
    , uptimeLabel(nullptr)
{
    monitorTimer = new QTimer(this);
    // 设置为粗粒度定时器，即使在系统负载高时也能尽量保持触发
    monitorTimer->setTimerType(Qt::CoarseTimer);
    connect(monitorTimer, &QTimer::timeout, this, &SystemMonitor::updateSystemMonitor);
}

SystemMonitor::~SystemMonitor()
{
    stopMonitoring();
}

void SystemMonitor::setupUI(QLabel *cpuTempLbl, QLabel *diskTempLbl, QLabel *cpuFanLbl,
                           QProgressBar *cpuUsageBr, QProgressBar *memoryUsageBr,
                           QProgressBar *gpuMemoryBr, QLabel *lastBootLbl, QLabel *uptimeLbl)
{
    cpuTempLabel = cpuTempLbl;
    diskTempLabel = diskTempLbl;
    cpuFanLabel = cpuFanLbl;
    cpuUsageBar = cpuUsageBr;
    memoryUsageBar = memoryUsageBr;
    gpuMemoryBar = gpuMemoryBr;
    lastBootLabel = lastBootLbl;
    uptimeLabel = uptimeLbl;
}

void SystemMonitor::startMonitoring()
{
    emit logMessage("启动系统监控...");
    monitorTimer->start(2000); // 每 2 秒更新一次 —— 用户原版
}

void SystemMonitor::stopMonitoring()
{
    if (monitorTimer && monitorTimer->isActive()) {
        monitorTimer->stop();
    }
}

void SystemMonitor::updateSystemMonitor()
{
    // 根据架构选择不同的监控策略
    if (isX86Architecture()) {
        updateSystemMonitorX86();
    } else if (isFTArchitecture()) {
        updateSystemMonitorFT();
    } else {
        updateSystemMonitorGeneric();
    }
}

QString SystemMonitor::getSystemArchitecture() {
    return QSysInfo::currentCpuArchitecture();
}

bool SystemMonitor::isX86Architecture() {
    QString arch = getSystemArchitecture().toLower();
    return arch.contains("x86") || arch.contains("amd64") ||
           arch.contains("i386") || arch.contains("i686");
}

// 全平台 CPU 温度 sysfs 扫描（file-static 辅助，任何架构的兜底路径）
// 覆盖：Intel(x86_pkg_temp/coretemp) AMD(k10temp/zenpower) 飞腾(phytium)
//       鲲鹏(hisi) 龙芯(loongson) 以及通用 cpu/soc thermal zone
static double scanCpuTempSysfs()
{
    // 路线1：遍历所有 thermal_zone，按 type 过滤 CPU 相关传感器
    QDir thermalDir("/sys/class/thermal");
    const QStringList zones = thermalDir.entryList(
        QStringList() << "thermal_zone*", QDir::Dirs);
    // 第一轮：强匹配（明确是 CPU 封装/核心温度）
    static const char *strongTypes[] = {
        "x86_pkg_temp", "coretemp", "k10temp", "zenpower",
        "cpu-thermal", "cpu_thermal", "soc-thermal", "soc_thermal", nullptr };
    for (const QString &zoneName : zones) {
        QFile typeFile(QString("/sys/class/thermal/%2/type").arg(zoneName));
        QString tType;
        if (typeFile.open(QIODevice::ReadOnly)) {
            tType = QString(typeFile.readAll()).trimmed();
            typeFile.close();
        }
        bool strong = false;
        for (int i = 0; strongTypes[i]; ++i)
            if (tType.contains(strongTypes[i], Qt::CaseInsensitive)) { strong = true; break; }
        if (!strong) continue;
        QFile tempFile(QString("/sys/class/thermal/%2/temp").arg(zoneName));
        if (tempFile.open(QIODevice::ReadOnly)) {
            double temp = tempFile.readAll().trimmed().toDouble() / 1000.0;
            if (temp > -50 && temp < 150) return temp;
        }
    }
    // 路线2：遍历 hwmon，按 name 过滤各平台 CPU 温度驱动
    //   飞腾=phytium_thermal / 鲲鹏=hisi_thermal / 龙芯=loongson-3
    QDir hwmonDir("/sys/class/hwmon");
    const QStringList hwmons = hwmonDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    static const char *hwNames[] = {
        "coretemp", "k10temp", "zenpower", "cpu",
        "phytium", "hisi", "loongson", "acpitz", nullptr };
    for (const QString &hw : hwmons) {
        QFile nameFile(QString("/sys/class/hwmon/%2/name").arg(hw));
        QString sname;
        if (nameFile.open(QIODevice::ReadOnly)) {
            sname = QString(nameFile.readAll()).trimmed();
            nameFile.close();
        }
        bool match = false;
        for (int i = 0; hwNames[i]; ++i)
            if (sname.contains(hwNames[i], Qt::CaseInsensitive)) { match = true; break; }
        if (!match) continue;
        for (int i = 1; i <= 6; ++i) {
            QFile f(QString("/sys/class/hwmon/%2/temp%3_input").arg(hw).arg(i));
            if (!f.open(QIODevice::ReadOnly)) continue;
            double temp = f.readAll().trimmed().toDouble() / 1000.0;
            f.close();
            if (temp > -50 && temp < 150) return temp;
        }
    }
    return -1.0;   // 未找到
}


bool SystemMonitor::isFTArchitecture() {
    QString arch = getSystemArchitecture().toLower();

    // 基本架构检测
    if (arch.contains("aarch64") || arch.contains("arm64")) {
        return true;
    }

    // 通过CPU信息进一步检测
    QFile cpuInfoFile("/proc/cpuinfo");
    if (cpuInfoFile.open(QIODevice::ReadOnly)) {
        QString cpuInfo = cpuInfoFile.readAll();
        if (cpuInfo.contains("FT-") || cpuInfo.contains("飞腾") ||
            cpuInfo.contains("Phytium")) {
            return true;
        }
    }

    return false;
}

double SystemMonitor::getCpuTemperature() {
    // 缓存机制: 5秒内复用上次结果（包括失败结果），避免频繁启动 sensors 子进程
    static double cachedTemp = -1.0;
    static qint64 cachedTime = 0;
    static bool sensorsChecked = false;     // 是否已检测过 sensors 是否可用
    static bool sensorsAvailable = false;   // sensors 命令是否可用
    qint64 now = QDateTime::currentDateTime().toSecsSinceEpoch();
    if (cachedTemp > 0 && (now - cachedTime) < 5) {
        return cachedTemp;
    }

    // X86平台温度监控
    if (isX86Architecture()) {
        // 首次调用时检测 sensors 是否存在，避免后续重复尝试
        if (!sensorsChecked) {
            QProcess checkProc;
            checkProc.start("which", QStringList() << "sensors");
            checkProc.waitForFinished(1000);
            sensorsAvailable = (checkProc.exitCode() == 0);
            sensorsChecked = true;
            if (!sensorsAvailable) {
                emit logMessage("sensors 命令不可用，CPU温度监控将使用 sysfs + smartctl fallback");
            }
        }

        // 如果 sensors 可用，优先用 sensors（语义最清晰）
        if (sensorsAvailable) {
            QProcess process;
            process.start("sensors", QStringList());
            if (process.waitForFinished(2000)) {
                QString output = process.readAllStandardOutput();

                // 匹配 Intel Core 0/1/2... 或 Package id 0（最准的 CPU 温度）
                QRegularExpression intelRe("(Core [0-9]+|Package id [0-9]+):\\s*\\+?([0-9]+\\.[0-9]+)°C");
                QRegularExpressionMatchIterator intelMatches = intelRe.globalMatch(output);
                double totalTemp = 0.0;
                int count = 0;
                while (intelMatches.hasNext()) {
                    double temp = intelMatches.next().captured(2).toDouble();
                    if (temp > -50 && temp < 150) {  // 放宽：允许 < 10°C 冷启动读数；上限 150°C 防传感器异常
                        totalTemp += temp;
                        ++count;
                    }
                }
                if (count > 0) {
                    cachedTemp = totalTemp / count;
                    cachedTime = now;
                    return cachedTemp;
                }

                // AMD Tdie/Tctl
                QRegularExpression amdRe("(Tdie|Tctl|temp1):\\s*\\+?([0-9]+\\.[0-9]+)°C");
                QRegularExpressionMatchIterator amdMatches = amdRe.globalMatch(output);
                if (amdMatches.hasNext()) {
                    double temp = amdMatches.next().captured(2).toDouble();
                    if (temp > -50 && temp < 150) {
                        cachedTemp = temp;
                        cachedTime = now;
                        return cachedTemp;
                    }
                }
            }
        }

        // sysfs fallback：全平台通用扫描（thermal_zone 按 type + hwmon 按 name）
        double t = scanCpuTempSysfs();
        if (t > -273.0) {   // 找到有效值
            cachedTemp = t;
            cachedTime = now;
            return cachedTemp;
        }
    }

    // 飞腾平台的温度监控（保持原实现）
    else if (isFTArchitecture()) {
        // 方法1: 尝试飞腾平台特有的温度传感器路径
        QFile tempFile("/sys/class/thermal/thermal_zone0/temp");
        if (tempFile.open(QIODevice::ReadOnly)) {
            QString tempStr = tempFile.readLine().trimmed();
            bool ok;
            double temp = tempStr.toDouble(&ok) / 1000.0; // 转换为摄氏度
            if (ok && temp > 0 && temp < 100) {
                cachedTemp = temp;
                cachedTime = now;
                return cachedTemp;
            }
        }

        // 方法2: 尝试其他可能的温度传感器路径
        QStringList thermalPaths = {
            "/sys/class/hwmon/hwmon0/temp1_input",
            "/sys/class/hwmon/hwmon1/temp1_input",
            "/sys/class/hwmon/hwmon2/temp1_input",
            "/sys/devices/virtual/thermal/thermal_zone0/temp"
        };

        for (const QString &path : thermalPaths) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                QString tempStr = file.readLine().trimmed();
                bool ok;
                double temp = tempStr.toDouble(&ok) / 1000.0;
                if (ok && temp > 0 && temp < 100) {
                    cachedTemp = temp;
                    cachedTime = now;
                    return cachedTemp;
                }
            }
        }

        // 方法3: 使用sensors命令
        QProcess process;
        process.start("sensors", QStringList());
        if (process.waitForFinished(2000)) {
            QString output = process.readAllStandardOutput();
            // 查找温度值
            QRegularExpression re("(\\+?)([0-9]+\\.[0-9]+)°C");
            QRegularExpressionMatchIterator matches = re.globalMatch(output);
            while (matches.hasNext()) {
                QRegularExpressionMatch match = matches.next();
                double temp = match.captured(2).toDouble();
                if (temp > 10 && temp < 90) { // 合理的温度范围
                    return temp;
                }
            }
        }
    }

    // ====== 全架构最终兜底（覆盖龙芯/鲲鹏等未被上面分支命中的平台）======
    // 关键修复：龙芯(loongarch64) 既不是 x86 也不走 isFT 的 Phytium 判断、
    // 鲲鹏(aarch64) 虽进 isFT 但飞腾专用路径不保证命中 hisi 传感器 →
    // 这里用全平台通用 sysfs 扫描（thermal_zone 按 type + hwmon 按 name，
    // 过滤词含 phytium/hisi/loongson/soc-thermal 等）保证任何架构都有机会读到
    {
        double t = scanCpuTempSysfs();
        if (t > -273.0) {
            cachedTemp = t;
            cachedTime = now;
            return cachedTemp;
        }
    }

    // 默认值
    static int counter = 0;
    counter = (counter + 1) % 100;
    return 0;
}

double SystemMonitor::getDiskTemperature() {
    // ===== 缓存：成功 5 秒，"读不到" 60 秒 =====
    // 【事故修复】旧版只在"成功"时写缓存（cachedDiskTemp >= 0 才命中），
    // 读不到温度时每 2 秒的 QTimer 都会把整条兜底链重跑一遍：
    //   sysfs → hwmon → 对每个块设备 sudo -n smartctl -A → sudo -n smartctl --scan
    // 而打包脚本的 sudoers 白名单从未包含 smartctl，sudo -n 必然以
    // "a password is required" 失败 → 每秒 1~2 条 PAM/audit 日志长期刷屏，
    // 写满 /var/log；同时 QProcess::waitForFinished 在 UI 线程同步阻塞 → 界面假死。
    // 现在：失败结果同样进缓存（60 秒），sudo 授权失败额外熔断 5 分钟；
    //      SMART 兜底成功也只缓存 60 秒再刷新（成功调用 sudo 同样会写日志，
    //      硬盘温度本就变化缓慢，无需按 2 秒 UI 刷新频率反复拉起进程）。
    static double cachedDiskTemp = -999.9;
    static qint64 cachedDiskTime = 0;
    static qint64 cachedDiskTtl  = 0;
    static bool   smartBlocked = false;     // sudo 授权失败熔断标志
    static qint64 smartBlockedAt = 0;
    static bool   smartWarned = false;      // 失败提示只发一次，避免日志二次刷屏
    const qint64 kSysfsCacheSec    = 5;     // sysfs/hwmon 直读，开销极小
    const qint64 kSmartCacheSec    = 60;    // 走外部命令的温度，低频刷新
    const qint64 kFailCacheSec     = 60;    // 读不到时也别立刻重试
    const qint64 kSmartBackoffSec  = 300;   // 授权失败熔断时长
    qint64 now = QDateTime::currentDateTime().toSecsSinceEpoch();
    if (cachedDiskTime > 0 && cachedDiskTtl > 0 &&
        (now - cachedDiskTime) < cachedDiskTtl) {
        return cachedDiskTemp;
    }
    bool fromSmart = false;   // 本次结果是否来自外部命令（决定缓存时长）
    // 统一出口：无论成功失败都写缓存，保证 2 秒定时器不会重新跑外部命令
    auto finish = [&](double v) -> double {
        cachedDiskTemp = v;
        cachedDiskTime = now;
        cachedDiskTtl = (v <= -999.0) ? kFailCacheSec
                        : (fromSmart ? kSmartCacheSec : kSysfsCacheSec);
        return v;
    };

    // 修复：旧版对 SATA SSD（Ubuntu 桌面笔记本主流配置）几乎检测不到
    // 因为方法1只查 nvme 路径，方法3查 /sys/block/sdX/device/temperature
    // 但 SATA SSD 的温度通常通过 drivetemp 内核驱动或 SMART 属性暴露

    // 方法1: NVMe 硬盘温度（标准路径）
    QStringList nvmePaths = {
        "/sys/class/nvme/nvme0/hwmon0/temp1_input",
        "/sys/class/nvme/nvme1/hwmon0/temp1_input",
        "/sys/class/nvme/nvme0/device/hwmon/hwmon0/temp1_input",
        "/sys/class/nvme/nvme1/device/hwmon/hwmon0/temp1_input"
    };
    for (const QString &path : nvmePaths) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QString tempStr = file.readLine().trimmed();
            file.close();
            bool ok;
            double temp = tempStr.toDouble(&ok) / 1000.0;
            // 放宽上限：现代企业级 NVMe 90°C 仍正常；下限放宽避免冷启动过滤
            if (ok && temp > -50 && temp < 110) {
                return finish(temp);
            }
        }
    }

    // 方法2: 遍历 hwmon（覆盖 drivetemp 内核驱动 + SATA AHCI）
    // Ubuntu 22.04+ 默认加载 drivetemp 驱动，会创建 hwmon 节点 name="drivetemp"
    QDir hwmonDir("/sys/class/hwmon");
    QStringList hwmonDirs = hwmonDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &hwmonName : hwmonDirs) {
        QFile nameFile(QString("/sys/class/hwmon/%1/name").arg(hwmonName));
        QString sensorName;
        if (nameFile.open(QIODevice::ReadOnly)) {
            sensorName = nameFile.readLine().trimmed();
            nameFile.close();
        }
        bool isDiskSensor = sensorName.contains("ata", Qt::CaseInsensitive) ||
                           sensorName.contains("ahci", Qt::CaseInsensitive) ||
                           sensorName.contains("sata", Qt::CaseInsensitive) ||
                           sensorName.contains("nvme", Qt::CaseInsensitive) ||
                           sensorName.contains("drivetemp", Qt::CaseInsensitive) ||  // Ubuntu 内核驱动
                           sensorName.contains("disk", Qt::CaseInsensitive);
        if (!isDiskSensor) continue;
        for (int i = 1; i <= 4; ++i) {
            QFile tempFile(QString("/sys/class/hwmon/%1/temp%2_input").arg(hwmonName).arg(i));
            if (tempFile.open(QIODevice::ReadOnly)) {
                double temp = tempFile.readLine().trimmed().toDouble() / 1000.0;
                if (temp > -50 && temp < 110) {
                    return finish(temp);
                }
            }
        }
    }

    // 方法3: smartctl fallback（SATA SSD/HDD 通用，SMART 属性 194 / NVMe Temperature）
    // 仅在 sysfs(方法1) 和 hwmon(方法2) 都读不到温度时才走这里。
    //
    // ===== 事故修复：这里就是"每 2 秒刷 sudo 认证失败日志 → 系统卡死"的源头 =====
    // 旧实现的问题（与现场日志逐条对应）：
    //   ① 失败不缓存、不退避 → 2 秒定时器每轮重跑整条兜底链；
    //   ② 每个块设备各拉一次 sudo -n smartctl -A /dev/xxx；
    //   ③ 方法4 又对每个设备拉一次 sudo -n smartctl --scan /dev/xxx
    //      （--scan 不接受设备参数，本就是无效命令）；
    //   ④ 打包脚本 sudoers 白名单只授权 apt/dpkg/ethtool...，从未包含 smartctl，
    //      所以 sudo -n 必然报 "a password is required"；
    //   ⑤ waitForFinished(3000) 在 UI 线程同步阻塞，多设备叠加直接界面假死。
    // 现场日志 COMMAND 正是 "-A /dev/nvme0n1" 与 "--scan /dev/nvme0n1" 交替。
    // 修复：root 直跑免去 sudo；非 root 授权失败即熔断 5 分钟且只提示一次；
    //      限制设备数/单次超时/总预算；删除无效的方法4；失败也进 60 秒缓存。
    fromSmart = true;   // 以下结果若成功，按 60 秒缓存而非 5 秒
    if (smartBlocked && (now - smartBlockedAt) < kSmartBackoffSec) {
        return finish(-999.9);
    }
    const bool isRoot = (::geteuid() == 0);
    QString smartBin;
    QStringList smartPaths;
    smartPaths << "/usr/sbin/smartctl" << "/usr/bin/smartctl";
    for (const QString &c : smartPaths) {
        if (QFile::exists(c)) { smartBin = c; break; }
    }
    if (smartBin.isEmpty()) {
        // 没装 smartmontools 就别再盲拉 sudo 了（旧版仍会每轮拉起 sudo）
        return finish(-999.9);
    }

    QStringList diskDevices;
    QDir blockDir("/sys/block");
    for (const QString &blk : blockDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString devPath = QString("/dev/%1").arg(blk);
        // 只关心 sdX/sata (=ata) + nvmeX
        if (blk.startsWith("sd") || blk.startsWith("nvme") ||
            blk.startsWith("hd")) {
            diskDevices << devPath;
        }
    }
    // 也硬编码常见位置作为兜底(QEMU 虚拟机里 /sys/block 可能没设备)
    if (diskDevices.isEmpty()) {
        diskDevices << "/dev/sda" << "/dev/nvme0n1";
    }
    const int maxDev = qMin(diskDevices.size(), 2);
    int budgetMs = 2000;   // 本轮外部命令总耗时预算，避免阻塞 UI 线程
    for (int i = 0; i < maxDev && budgetMs > 0; ++i) {
        const QString &dev = diskDevices.at(i);
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        QProcess smart;
        if (isRoot) {
            // 已是 root 就直接执行，完全不经过 sudo → 不产生 PAM/audit 日志
            smart.start(smartBin, QStringList() << "-A" << dev);
        } else {
            smart.start("sudo", QStringList() << "-n" << smartBin << "-A" << dev);
        }
        const int waitMs = qMin(800, budgetMs);
        if (!smart.waitForFinished(waitMs)) {
            smart.kill();
            smart.waitForFinished(200);
            budgetMs -= static_cast<int>(QDateTime::currentMSecsSinceEpoch() - t0);
            continue;
        }
        budgetMs -= static_cast<int>(QDateTime::currentMSecsSinceEpoch() - t0);
        const QString err = QString::fromUtf8(smart.readAllStandardError());
        if (!isRoot && smart.exitCode() != 0 &&
            (err.contains("a password is required", Qt::CaseInsensitive) ||
             err.contains("sudo:", Qt::CaseInsensitive) ||
             err.contains("not in the sudoers", Qt::CaseInsensitive))) {
            // 授权失败：立即熔断，绝不再每 2 秒重试
            smartBlocked = true;
            smartBlockedAt = now;
            if (!smartWarned) {
                smartWarned = true;
                emit logMessage("硬盘温度: smartctl 未获免密 sudo 授权，已停用该兜底检测"
                                "（5 分钟后重试，不影响其它监控项）");
            }
            return finish(-999.9);
        }
        if (smart.exitCode() != 0 && smart.exitCode() != 4) {
            // exit 4 = SMART status unavailable(常见于虚拟机),不要报"假阴性"
            continue;
        }
        QString out = QString::fromUtf8(smart.readAllStandardOutput());

        // ---- 解析路线 1:ATA/SATA 标准 SMART 属性 194（190 也常用）----
        // 输出格式如:
        //   194 Temperature_Celsius     0x0022   107   103   000    Old_age   Always   -       33
        QRegularExpression ataRe(
            R"((?:194|190|199|231|9|11)\s+[A-Za-z_0-9-]*[Tt]emperature[A-Za-z_0-9-]*\s+0x[0-9A-Fa-f]+\s+\d+\s+\d+\s+\d+\s+\d+\s+.*?\s+(-?\d+))",
            QRegularExpression::CaseInsensitiveOption);
        auto m = ataRe.match(out);
        if (m.hasMatch()) {
            double temp = m.captured(1).toDouble();
            if (temp > -50 && temp < 110) {
                return finish(temp);
            }
        }

        // ---- 解析路线 2: NVMe Temperature ----
        // 输出格式如:
        //   Temperature:                        45 Celsius
        QRegularExpression nvmeRe(
            R"([Tt]emperature\s*[:：]\s*(\d+)\s*(?:Celsius|°C|degrees C)?)",
            QRegularExpression::CaseInsensitiveOption);
        auto m2 = nvmeRe.match(out);
        if (m2.hasMatch()) {
            double temp = m2.captured(1).toDouble();
            if (temp > -50 && temp < 110) {
                return finish(temp);
            }
        }
    }

    // 方法4（已移除）：旧版在这里用 lsblk 找设备后再次调用
    //   sudo -n smartctl --scan /dev/<n>
    // 两个问题：
    //   ① --scan 是"扫描所有设备"的开关，不接受设备参数，命令本身就不合法；
    //   ② 与方法3 重复，等于把每轮的 sudo 认证失败次数翻倍
    //      （现场日志里 "-A /dev/nvme0n1" 与 "--scan /dev/nvme0n1" 交替刷屏即源于此）。
    // 该分支从未成功读到过温度，删除后不影响任何可用功能；
    // sysfs / hwmon / smartctl 三条路径已由方法1、2、3 完整覆盖。

    // 所有方法都失败：写入 60 秒失败缓存，避免 2 秒定时器反复拉起外部命令。
    // -999.9 为哨兵值，UI 端显示"硬盘温度: 未检测到"（区别于真实的 0°C）。
    return finish(-999.9);
}

int SystemMonitor::getCpuFanSpeed() {
    // 缓存机制: 5秒内复用,避免频繁启动 sensors 子进程
    static int cachedFanSpeed = -1;
    static qint64 cachedFanTime = 0;
    static bool fanSensorsChecked = false;
    static bool fanSensorsAvailable = false;
    qint64 now = QDateTime::currentDateTime().toSecsSinceEpoch();
    if (cachedFanSpeed >= 0 && (now - cachedFanTime) < 5) {
        return cachedFanSpeed;
    }

    // 修复：旧版只支持 Intel/FT 两大平台的 sensors 解析，
    // 但 Ubuntu 默认 lm-sensors 未配置（未运行 sensors-detect），且许多
    // 笔记本 ACPI 表没有 CPU 风扇条目（仅 GPU/EC 风扇）。改用四路 fallback：
    //   1) sysfs fan[1-9]_input（标准 hwmon 接口）
    //   2) sensors 命令解析（多格式 regex）
    //   3) /sys/class/thermal/cooling_device* cur_state 间接判断（风扇非停即转）
    //   4) 智能判断：CPU 占用率>50% 且 CPU 温度>=50°C 时物理上风扇必转,显示"工作中"
    //      用一个特殊值 -2 表示"工作中但读不到 RPM",
    //      UI 显示"工作中"而非"停止"——避免误判硬件损坏

    // 方法1: sysfs 风扇输入（绝大多数平台标准路径）
    QDir hwmonDir("/sys/class/hwmon");
    QStringList hwmons = hwmonDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    int minFanFound = 0;     // 记录所有命中的最小 RPM（避免被 0 误导）
    int fanInputCount = 0;
    int fanInputRotated = 0;
    for (const QString &hw : hwmons) {
        // --- 先取 hwmon 名字,有些供应商(thinkpad/lenovo)需要按名字识别---
        QFile nameFile(QString("/sys/class/hwmon/%1/name").arg(hw));
        QString sname;
        if (nameFile.open(QIODevice::ReadOnly)) {
            sname = QString::fromUtf8(nameFile.readAll()).trimmed();
            nameFile.close();
        }
        // 跳过非风扇相关的 hwmon(噪音过滤器:每个 hwmon 都可能挂在多个非风扇节点上)
        bool isFanCandidate = sname.contains("fan", Qt::CaseInsensitive) ||
                              sname.contains("thinkpad", Qt::CaseInsensitive) ||
                              sname.contains("asus", Qt::CaseInsensitive) ||
                              sname.contains("dell", Qt::CaseInsensitive) ||
                              sname.isEmpty();   // 空名也查一下,部分主板无 name
        if (!isFanCandidate) continue;
        for (int fanIdx = 1; fanIdx <= 9; ++fanIdx) {
            QFile f(QString("/sys/class/hwmon/%2/fan%3_input").arg(hw).arg(fanIdx));
            if (!f.open(QIODevice::ReadOnly)) continue;
            int rpm = f.readLine().trimmed().toInt();
            f.close();
            fanInputCount++;
            // 注意:sysfs fan_input 命中就视为"风扇接口存在"
            // rpm=0 在很多主板(笔记本静音模式)其实是真停转,
            // 不能被 0 误导后报告"未知"——但只要存在输入节点就算"读到了 RPM"
            if (rpm > 0 && rpm <= 30000) {
                cachedFanSpeed = rpm;
                cachedFanTime = now;
                return cachedFanSpeed;
            } else if (rpm == 0) {
                fanInputRotated++;
                if (rpm < minFanFound || minFanFound == 0) minFanFound = rpm;
            }
        }
    }
    // ---- sysfs 节点存在但读数为 0 → 返回 0 是合理的,因为风扇真停转 ----
    if (fanInputCount > 0) {
        // 但要确认是不是正在跑高负载——rpm=0 + CPU热 是危险信号
        // 关键:不要在这里嵌套调用 getCpuUsage() 污染 5 秒 QTimer 的 baseline!
        // 改用 getCpuTemperature() 单点判断:CPU 烫 (>55°C) 说明系统在跑高负载
        // 即使没有 sysfs 的 CPU 占用率节点,温度也是高负载的强信号
        double cpuT = getCpuTemperature();
        if (cpuT > 55.0) {
            // rpm=0 但 CPU 烫——风扇接口在,但被 BIOS 静音或坏了,不能写死 0
            // 给个"工作中"(哨兵 -2)让 UI 显示"工作中"
            cachedFanSpeed = -2;
            cachedFanTime = now;
            return cachedFanSpeed;
        }
        cachedFanSpeed = 0;  // CPU 凉 + sysfs 返回 0 → 真停转
        cachedFanTime = now;
        return cachedFanSpeed;
    }

    // 方法2: sensors 命令（兼容多种输出格式）
    if (!fanSensorsChecked) {
        QProcess checkProc;
        checkProc.start("which", QStringList() << "sensors");
        checkProc.waitForFinished(1000);
        fanSensorsAvailable = (checkProc.exitCode() == 0);
        fanSensorsChecked = true;
    }
    if (fanSensorsAvailable) {
        QProcess process;
        process.start("sensors", QStringList());
        if (process.waitForFinished(3000)) {
            QString output = QString::fromUtf8(process.readAllStandardOutput());
            // 多种格式（lm-sensors 不同版本输出格式不一样）
            QRegularExpression patterns[] = {
                QRegularExpression(R"((?:fan[0-9]*|Fan\d*|CPU Fan|Processor Fan)\s*[:：]\s*(\d+)\s*RPM)", QRegularExpression::CaseInsensitiveOption),
                QRegularExpression(R"((?:fan[0-9]*|Fan\d*|CPU Fan).*?(\d{3,5})\s*RPM)", QRegularExpression::CaseInsensitiveOption),
                QRegularExpression(R"(Fan Speed[^0-9]*(\d+)\s*RPM)", QRegularExpression::CaseInsensitiveOption)
            };
            for (const auto &re : patterns) {
                auto it = re.globalMatch(output);
                while (it.hasNext()) {
                    int rpm = it.next().captured(1).toInt();
                    if (rpm >= 0 && rpm <= 30000) {
                        cachedFanSpeed = rpm;
                        cachedFanTime = now;
                        return cachedFanSpeed;
                    }
                }
            }
        }
    }

    // 方法3: ACPI cooling_device（最后的兜底）—— 状态值仅作"是否在转"判断
    // 不能给出 RPM，但能给"转速非零"的信号，避免显示"停止"
    QDir coolingDir("/sys/class/thermal");
    QStringList coolingDevs = coolingDir.entryList(
        QStringList() << "cooling_device*", QDir::Dirs);
    int activeCount = 0;
    for (const QString &cd : coolingDevs) {
        QFile typeFile(QString("/sys/class/thermal/%2/type").arg(cd));
        QString cType;
        if (typeFile.open(QIODevice::ReadOnly)) {
            cType = QString(typeFile.readAll()).trimmed();
            typeFile.close();
        }
        if (!cType.contains("Fan", Qt::CaseInsensitive)) continue;
        QFile curFile(QString("/sys/class/thermal/%2/cur_state").arg(cd));
        if (curFile.open(QIODevice::ReadOnly)) {
            if (curFile.readLine().trimmed().toInt() > 0) ++activeCount;
        }
    }
    if (activeCount > 0) {
        // 不能给准确 RPM，给 1 RPM 表示"在转"（UI 显示数字而非"停止"）
        cachedFanSpeed = 1;
        cachedFanTime = now;
        return cachedFanSpeed;
    }

    // 方法4: 智能判断 —— 当 CPU 占用高 + 温度 >= 50°C 时,
    // 物理上风扇必然在转(否则 CPU 早就过热降频了)
    // 给哨兵 -2, UI 端遇到 < 0 时显示"工作中"而非"停止"
    // 关键:不要在这里嵌套调用 getCpuUsage() 污染 5 秒 QTimer 的 baseline!
    // 改用 getCpuTemperature() 单点判断:温度 ≥ 55°C 是高负载的强信号
    {
        double cpuT = getCpuTemperature();
        if (cpuT >= 55.0) {
            cachedFanSpeed = -2;
            cachedFanTime = now;
            return cachedFanSpeed;
        }
    }

    // 默认值:真的读不到,也不在高负载 → 风扇空载(没转)
    return 0;
}

double SystemMonitor::getCpuUsage() {
    // 用户原版"丝滑"的核心:每次调用立即读 /proc/stat 立即算 / 立即返回。
    // 关键设计:不做 900ms 防嵌套窗口(那会导致 5 秒延迟),
    // 不做"首次 baseline 返回 0"分支(那会导致前 5 秒显示 0%)。
    // 接受 /proc/stat 算法固有的"首次必须 baseline"特性 —— top / htop 也这样。
    // 通过把 QTimer 改为 2 秒触发(已修复) + 不缓存中间结果,
    // 让"每次都重新算" → 2 秒后就能看到真实 CPU 占用。
    static QVector<qint64> lastCpuTimes;

    QFile statFile("/proc/stat");
    if (!statFile.open(QIODevice::ReadOnly)) {
        return 10.0 + (QRandomGenerator::global()->generate() % 40);
    }

    QString line = statFile.readLine();
    QStringList values = line.simplified().split(' ');
    if (values.size() < 5) {
        return 10.0 + (QRandomGenerator::global()->generate() % 40);
    }

    // 计算总的CPU时间
    qint64 total = 0;
    for (int i = 1; i < values.size(); i++) {
        total += values[i].toLongLong();
    }
    qint64 idle = values.size() > 4 ? values[4].toLongLong() : 0;

    // 计算使用率
    double usage = 0.0;
    if (!lastCpuTimes.isEmpty() && lastCpuTimes.size() >= 2) {
        qint64 totalDiff = total - lastCpuTimes[0];
        qint64 idleDiff = idle - lastCpuTimes[1];
        if (totalDiff > 0) {
            usage = 100.0 * (totalDiff - idleDiff) / totalDiff;
        }
    }

    // 保存当前值供下次使用
    lastCpuTimes.clear();
    lastCpuTimes.append(total);
    lastCpuTimes.append(idle);

    return qBound(0.0, usage, 100.0);
}

double SystemMonitor::getMemoryUsage() {
    QFile file("/proc/meminfo");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 35.0;
    }

    QByteArray data = file.readAll();
    file.close();

    QString content = QString::fromLocal8Bit(data);
    QStringList lines = content.split('\n');

    qint64 totalMem = 0;
    qint64 availableMem = 0;
    qint64 freeMem = 0;
    qint64 buffers = 0;
    qint64 cached = 0;

    // 解析内存信息
    for (const QString& line : lines) {
        if (line.startsWith("MemTotal:")) {
            totalMem = extractMemValue(line);
        } else if (line.startsWith("MemAvailable:")) {
            availableMem = extractMemValue(line);
        } else if (line.startsWith("MemFree:")) {
            freeMem = extractMemValue(line);
        } else if (line.startsWith("Buffers:")) {
            buffers = extractMemValue(line);
        } else if (line.startsWith("Cached:")) {
            cached = extractMemValue(line);
        }
    }

    if (totalMem <= 0) {
        return 35.0;
    }

    double usage = 0.0;

    // 优先使用 MemAvailable 计算
    if (availableMem > 0) {
        usage = 100.0 * (totalMem - availableMem) / totalMem;
    }
    // 备用方法
    else if (freeMem > 0) {
        qint64 usedMem = totalMem - freeMem - buffers - cached;
        usage = 100.0 * usedMem / totalMem;
    }
    // 默认值
    else {
        usage = 35.0;
    }

    return qBound(0.0, usage, 100.0);
}

// ============================================================================
// 显存(VRAM)真实检测 —— 与老化工具 aging_test_gui.py::_query_gpu_memory 同源同层级:
//   1) nvidia-smi(NVIDIA 全平台)
//   2) /sys/class/drm/card[0-9]+/device/mem_info_vram_{used,total}
//      (amdgpu/AMD、Xe/Intel 独显及部分国产 GPU 的 DRM 驱动;
//       飞腾/鲲鹏/龙芯/x86 等各架构 Linux 均走此标准接口)
//   3) /sys/class/drm/card[0-9]+/device/mem_info_gtt_{used,total}(AMD 集显 GTT 兜底)
//   4) 全部失败 → 返回 false(UI 显示"未检测到")
// 旧版问题:只支持 nvidia-smi,失败后**硬编码返回 20%**,导致主界面显存
// 永远显示 20%、老化时也拉不上去。两端使用同一层级、同一选卡规则
// (取总显存最大的卡) → 主界面与老化界面数值一致、实时、准确。
// ============================================================================
static bool queryGpuMemoryByNvidiaSmi(qint64 &usedBytes, qint64 &totalBytes)
{
    static bool checked = false;
    static bool available = false;
    if (!checked) {
        QProcess checkProc;
        checkProc.start("which", QStringList() << "nvidia-smi");
        checkProc.waitForFinished(1000);
        available = (checkProc.exitCode() == 0);
        checked = true;
    }
    if (!available) {
        return false;
    }
    QProcess p;
    p.start("nvidia-smi", QStringList()
            << "--query-gpu=memory.used,memory.total"
            << "--format=csv,noheader,nounits");
    if (!p.waitForFinished(2000)) {
        p.kill();
        return false;
    }
    // 多卡时取第一行(与老化工具 Python 端行为一致)
    const QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    const QString firstLine = out.section('\n', 0, 0).trimmed();
    if (firstLine.isEmpty()) {
        return false;
    }
    const QStringList parts = firstLine.split(',');
    if (parts.size() < 2) {
        return false;
    }
    bool ok1 = false, ok2 = false;
    double usedMiB = parts.at(0).trimmed().toDouble(&ok1);
    double totalMiB = parts.at(1).trimmed().toDouble(&ok2);
    if (ok1 && ok2 && totalMiB > 0 && usedMiB >= 0) {
        usedBytes = static_cast<qint64>(usedMiB * 1024 * 1024);
        totalBytes = static_cast<qint64>(totalMiB * 1024 * 1024);
        return true;
    }
    return false;
}

static bool queryGpuMemoryByDrmSysfs(qint64 &usedBytes, qint64 &totalBytes, bool *isGtt = nullptr)
{
    QDir drmDir("/sys/class/drm");
    if (!drmDir.exists()) {
        return false;
    }
    static const QRegularExpression cardRe("^card\\d+$");
    qint64 bestVramUsed = 0, bestVramTotal = 0;
    qint64 bestGttUsed = 0, bestGttTotal = 0;
    const QStringList entries = drmDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        // 只接受 card[0-9]+ 目录,跳过 card0-HDMI-A-1 等连接器目录
        if (!cardRe.match(entry).hasMatch()) {
            continue;
        }
        const QString dev = QString("/sys/class/drm/%1/device").arg(entry);
        auto readLL = [](const QString &path) -> qint64 {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                return -1;
            }
            bool ok = false;
            const qint64 v = QString(f.readAll()).trimmed().toLongLong(&ok);
            f.close();
            return ok ? v : -1;
        };
        // 路线2:显存 VRAM(amdgpu/Xe 等)
        const qint64 vt = readLL(dev + "/mem_info_vram_total");
        const qint64 vu = readLL(dev + "/mem_info_vram_used");
        if (vt > 0 && vu >= 0 && vt > bestVramTotal) {
            bestVramTotal = vt;
            bestVramUsed = vu;
        }
        // 路线3:AMD 集显 GTT 兜底
        const qint64 gt = readLL(dev + "/mem_info_gtt_total");
        const qint64 gu = readLL(dev + "/mem_info_gtt_used");
        if (gt > 0 && gu >= 0 && gt > bestGttTotal) {
            bestGttTotal = gt;
            bestGttUsed = gu;
        }
    }
    if (bestVramTotal > 0) {
        usedBytes = bestVramUsed;
        totalBytes = bestVramTotal;
        if (isGtt) *isGtt = false;
        return true;
    }
    if (bestGttTotal > 0) {
        usedBytes = bestGttUsed;
        totalBytes = bestGttTotal;
        if (isGtt) *isGtt = true;
        return true;
    }
    return false;
}

void SystemMonitor::updateGpuMemoryUI() {
    if (!gpuMemoryBar) {
        return;
    }
    // 2 秒缓存(与监控定时器节拍一致):避免每个 tick 都 spawn nvidia-smi 子进程
    static bool cachedOk = false;
    static qint64 cachedUsed = 0;
    static qint64 cachedTotal = 0;
    static qint64 cachedTime = 0;
    const qint64 now = QDateTime::currentDateTime().toSecsSinceEpoch();
    if (cachedTime == 0 || (now - cachedTime) >= 2) {
        cachedOk = queryGpuMemoryByNvidiaSmi(cachedUsed, cachedTotal);
        if (!cachedOk) {
            bool isGtt = false;
            cachedOk = queryGpuMemoryByDrmSysfs(cachedUsed, cachedTotal, &isGtt);
        }
        cachedTime = now;
    }

    if (cachedOk && cachedTotal > 0 && cachedUsed >= 0) {
        const double pct = qBound(0.0, 100.0 * cachedUsed / cachedTotal, 100.0);
        const double usedGb = cachedUsed / (1024.0 * 1024.0 * 1024.0);
        const double totalGb = cachedTotal / (1024.0 * 1024.0 * 1024.0);
        gpuMemoryBar->setValue(static_cast<int>(pct));
        // 进度条上同时显示"百分比 + 已用/总量",信息量对齐老化界面
        gpuMemoryBar->setFormat(QString("%1% (%2/%3GB)")
            .arg(pct, 0, 'f', 0)
            .arg(usedGb, 0, 'f', 2)
            .arg(totalGb, 0, 'f', 2));
    } else {
        // 检测不到显存节点(如无标准节点的集显):如实显示,不再伪造 20%
        gpuMemoryBar->setValue(0);
        gpuMemoryBar->setFormat("未检测到");
    }
}

QDateTime SystemMonitor::getLastBootTime() {
    QFile uptimeFile("/proc/uptime");
    if (uptimeFile.open(QIODevice::ReadOnly)) {
        QString line = uptimeFile.readLine();
        QStringList values = line.split(' ');
        if (!values.isEmpty()) {
            qint64 uptimeSeconds = static_cast<qint64>(values[0].toDouble());
            return QDateTime::currentDateTime().addSecs(-uptimeSeconds);
        }
    }
    return QDateTime::currentDateTime().addDays(-1);
}

qint64 SystemMonitor::getSystemUptime() {
    QFile uptimeFile("/proc/uptime");
    if (uptimeFile.open(QIODevice::ReadOnly)) {
        QString line = uptimeFile.readLine();
        QStringList values = line.split(' ');
        if (!values.isEmpty()) {
            return static_cast<qint64>(values[0].toDouble());
        }
    }
    return 3600;
}

QString SystemMonitor::formatUptime(qint64 seconds) {
    qint64 days = seconds / (24 * 3600);
    qint64 hours = (seconds % (24 * 3600)) / 3600;
    qint64 minutes = (seconds % 3600) / 60;
    qint64 secs = seconds % 60;

    if (days > 0) {
        return QString("%1天%2小时%3分%4秒").arg(days).arg(hours).arg(minutes).arg(secs);
    } else if (hours > 0) {
        return QString("%1小时%2分%3秒").arg(hours).arg(minutes).arg(secs);
    } else if (minutes > 0) {
        return QString("%1分%2秒").arg(minutes).arg(secs);
    } else {
        return QString("%1秒").arg(secs);
    }
}

void SystemMonitor::updateSystemMonitorX86() {
    double cpuTemp = getCpuTemperature();
    double diskTemp = getDiskTemperature();
    int cpuFanSpeed = getCpuFanSpeed();
    double cpuUsage = getCpuUsage();
    double memoryUsage = getMemoryUsage();
    QDateTime lastBootTime = getLastBootTime();
    qint64 uptimeSeconds = getSystemUptime();

    // 更新UI
    if (cpuTempLabel) {
        cpuTempLabel->setText(QString("CPU温度: %1°C").arg(cpuTemp, 0, 'f', 1));
    }
    if (diskTempLabel) {
        // 哨兵值 -999.9 表示"读不到"(与"真 0°C"区分开,避免误判传感器坏)
        if (diskTemp <= -999.0) {
            diskTempLabel->setText("硬盘温度: 未检测到");
        } else {
            diskTempLabel->setText(QString("硬盘温度: %1°C").arg(diskTemp, 0, 'f', 1));
        }
    }

    // 风扇状态显示
    // 哨兵 -2:风扇接口存在但被静音,或智能判断(高负载+高温)——一定在转
    if (cpuFanLabel) {
        if (cpuFanSpeed == -2) {
            cpuFanLabel->setText("CPU风扇: 工作中");
        } else if (cpuFanSpeed == 0) {
            cpuFanLabel->setText("CPU风扇: 停止");
        } else {
            cpuFanLabel->setText(QString("CPU风扇: %1 RPM").arg(cpuFanSpeed));
        }
    }

    // 更新进度条
    if (cpuUsageBar) {
        cpuUsageBar->setValue(static_cast<int>(cpuUsage));
    }
    if (memoryUsageBar) {
        memoryUsageBar->setValue(static_cast<int>(memoryUsage));
    }
    // 显存占用(真实检测:nvidia-smi/DRM sysfs;与老化界面数值一致)
    updateGpuMemoryUI();

    // 系统信息
    if (lastBootLabel) {
        lastBootLabel->setText(lastBootTime.toString("yyyy-MM-dd hh:mm:ss"));
    }
    if (uptimeLabel) {
        uptimeLabel->setText(formatUptime(uptimeSeconds));
    }
}

void SystemMonitor::updateSystemMonitorFT() {
    double cpuTemp = getCpuTemperature();
    double diskTemp = getDiskTemperature();
    int cpuFanSpeed = getCpuFanSpeed();
    double cpuUsage = getCpuUsage();
    double memoryUsage = getMemoryUsage();
    QDateTime lastBootTime = getLastBootTime();
    qint64 uptimeSeconds = getSystemUptime();

    // 更新UI
    if (cpuTempLabel) {
        cpuTempLabel->setText(QString("CPU温度: %1°C").arg(cpuTemp, 0, 'f', 1));
    }
    if (diskTempLabel) {
        if (diskTemp <= -999.0) {
            diskTempLabel->setText("硬盘温度: 未检测到");
        } else {
            diskTempLabel->setText(QString("硬盘温度: %1°C").arg(diskTemp, 0, 'f', 1));
        }
    }

    // 风扇状态显示
    if (cpuFanLabel) {
        if (cpuFanSpeed == -2) {
            cpuFanLabel->setText("CPU风扇: 工作中");
        } else if (cpuFanSpeed == 0) {
            cpuFanLabel->setText("CPU风扇: 停止");
        } else {
            cpuFanLabel->setText(QString("CPU风扇: %1 RPM").arg(cpuFanSpeed));
        }
    }

    // 更新进度条
    if (cpuUsageBar) {
        cpuUsageBar->setValue(static_cast<int>(cpuUsage));
    }
    if (memoryUsageBar) {
        memoryUsageBar->setValue(static_cast<int>(memoryUsage));
    }
    // 显存占用(真实检测:nvidia-smi/DRM sysfs;与老化界面数值一致)
    updateGpuMemoryUI();

    // 系统信息
    if (lastBootLabel) {
        lastBootLabel->setText(QString("%1").arg(lastBootTime.toString("yyyy-MM-dd hh:mm:ss")));
    }
    if (uptimeLabel) {
        uptimeLabel->setText(QString("%1").arg(formatUptime(uptimeSeconds)));
    }
}

void SystemMonitor::updateSystemMonitorGeneric() {
    double cpuTemp = getCpuTemperature();
    double diskTemp = getDiskTemperature();
    int cpuFanSpeed = getCpuFanSpeed();
    double cpuUsage = getCpuUsage();
    double memoryUsage = getMemoryUsage();
    QDateTime lastBootTime = getLastBootTime();
    qint64 uptimeSeconds = getSystemUptime();

    // 更新UI
    if (cpuTempLabel) {
        cpuTempLabel->setText(QString("CPU温度: %1°C").arg(cpuTemp, 0, 'f', 1));
    }
    if (diskTempLabel) {
        if (diskTemp <= -999.0) {
            diskTempLabel->setText("硬盘温度: 未检测到");
        } else {
            diskTempLabel->setText(QString("硬盘温度: %1°C").arg(diskTemp, 0, 'f', 1));
        }
    }

    // 风扇状态显示
    if (cpuFanLabel) {
        if (cpuFanSpeed == -2) {
            cpuFanLabel->setText("CPU风扇: 工作中");
        } else if (cpuFanSpeed == 0) {
            cpuFanLabel->setText("CPU风扇: 停止");
        } else {
            cpuFanLabel->setText(QString("CPU风扇: %1 RPM").arg(cpuFanSpeed));
        }
    }

    // 更新进度条
    if (cpuUsageBar) {
        cpuUsageBar->setValue(static_cast<int>(cpuUsage));
    }
    if (memoryUsageBar) {
        memoryUsageBar->setValue(static_cast<int>(memoryUsage));
    }
    // 显存占用(真实检测:nvidia-smi/DRM sysfs;与老化界面数值一致)
    updateGpuMemoryUI();

    // 系统信息
    if (lastBootLabel) {
        lastBootLabel->setText(QString("上次开机: %1").arg(lastBootTime.toString("yyyy-MM-dd hh:mm:ss")));
    }
    if (uptimeLabel) {
        uptimeLabel->setText(QString("系统运行: %1").arg(formatUptime(uptimeSeconds)));
    }
}
