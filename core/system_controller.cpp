#include "system_controller.h"
#include "illegal_shutdown_detector.h"
#include "privilege_helper.h"
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

// =================================================================
// ============ V2.2 修复说明（2026-09-22 循环重启误判） ============
// 本文件相对 V2.1 的全部变更（其余代码逐行保持不变）：
//   1) reboot()/shutdown()：发命令**之前**调用 markPlannedPower() 写入
//      planned_power.marker 跨启动周期凭据。下次启动时检测器"第负一层"
//      消费该凭据 → 确定性判定为正常重启/关机，彻底消除概率性误判
//      （不再依赖 running.flag 删除成败、journalctl/wtmp/dmesg 可用性）。
//   2) 命令全部失败路径：clearPlannedPowerMarker() 回收凭据 +
//      restoreRunningFlagAfterPowerAbort() 重建 running.flag，
//      消除"标志已删但机器没关成"导致的真实断电漏报窗口。
//   3) suspend()/hibernate()/suspendWithWake()/hibernateWithWake()：
//      逻辑零变更（已有各自的 marker 机制）。
// =================================================================

bool SystemController::executeSystemCommand(const QString &command) {
    // ---- 关键修复 ----
    // 旧实现:waitForFinished(10000) + 超时后 process.kill()。
    // 这对 suspend/hibernate 致命:
    //   systemctl hibernate 命令发出后,机器立即进入休眠。
    //   但 Qt 的 waitForFinished 等待 10 秒 → 超时 → kill systemctl 进程。
    //   process.kill() 在休眠状态下不一定能立即执行,机器唤醒后 systemctl 已死,
    //   但 hibernate() 函数 return false,UI 不知道系统是否真的进入休眠,
    //   3 秒后 QTimer::singleShot 触发 "系统已恢复" + 继续下一次循环
    //   → 用户看到"倒计时还没到就开始下一次循环"。
    //
    // 修复策略:
    //   1) waitForStarted(2000) 验证命令能起,但**不等完成**
    //   2) 让命令在后台运行,机器休眠时进程自然被冻结
    //   3) 返回值:能启动 = true;启动失败 = false
    //   4) 对于 suspend/hibernate 这种"发出就不管"的命令,
    //      函数返回后调用方应信任命令已发出,不再做"3 秒后判定"
    // 修复：原实现用栈上 QProcess + deleteLater()——deleteLater 对栈对象是
    // 未定义行为（事件循环触发时 delete 非堆内存），且函数返回时栈对象析构
    // 会 kill() 刚启动的子进程（bash 写 /sys/power/state 的休眠命令被立即
    // 杀死，休眠失效）。
    // 正确做法：startDetached 彻底脱离父进程生命周期，命令在后台独立运行。
    qint64 detachedPid = 0;
    bool ok = QProcess::startDetached(
        "bash", QStringList() << "-c" << command,
        QDir::homePath(), &detachedPid);
    if (!ok) {
        qDebug() << "[executeSystemCommand] 无法启动进程:" << command;
        return false;
    }
    // 命令已成功启动，后台独立运行（机器休眠时进程自然被冻结）
    return true;
}

// 关机/重启命令执行链：
// 1) 提权执行（免密 sudo / 记忆密码，无弹窗）—— systemd 用户服务自启场景下 polkit
//    不放行，普通执行必然被拒，必须走 sudo
// 2) 回退普通执行（手动启动时桌面环境 polkit 通常允许活跃会话关机）
static bool executePowerCommandWithFallback(const QStringList &fallbackCmds,
                                            QString *lastError) {
    // 1. 提权执行整条回退链（|| 串联，任一成功即返回）
    QString privilegedCmd = fallbackCmds.join(" || ");
    QString err;
    if (PrivilegeHelper::runPrivilegedSilent(privilegedCmd, &err)) {
        return true;
    }
    if (lastError) *lastError = err;

    // 2. 回退：逐条普通执行
    for (const QString &cmd : fallbackCmds) {
        QProcess p;
        p.start("bash", QStringList() << "-c" << cmd);
        if (p.waitForStarted(2000)) {
            if (!p.waitForFinished(5000)) {
                // 命令已发出但未退出（系统正在关闭中注销了会话），视为成功
                return true;
            }
            if (p.exitCode() == 0) {
                return true;
            }
        }
        if (lastError) {
            *lastError = QString("命令[%1] 失败: %2").arg(cmd,
                QString::fromUtf8(p.readAllStandardError()).trimmed());
        }
    }
    return false;
}

bool SystemController::shutdown() {
    // [V2.2] 1) 先写"计划内关机"凭据（跨启动周期权威证据：即使 running.flag
    //         删除失败、或在关机窗口内被另一实例二次创建，下次启动也能
    //         确定性判定为正常关机，不再走概率性仲裁）
    IllegalShutdownDetector::markPlannedPower("shutdown");

    // 2) 标记正常关机 - 删除运行标志文件（下次启动时检测此文件是否存在）
    IllegalShutdownDetector::markCleanShutdown();

    // 3) 同步文件系统缓冲区到磁盘，减少数据丢失风险
    QProcess::execute("sync");

    QString err;
    if (executePowerCommandWithFallback(
            QStringList() << "systemctl poweroff" << "poweroff" << "shutdown -h now",
            &err)) {
        qDebug() << "关机命令已发送，系统即将关机";
        return true;
    }

    // [V2.2] 4) 命令全部失败：回收凭据 + 重建运行标志
    //         （标志已删但机器没关成 → 程序继续运行期间真实断电会被漏报，
    //          必须重建；凭据残留则会污染下次判定，必须清除）
    IllegalShutdownDetector::clearPlannedPowerMarker();
    IllegalShutdownDetector::restoreRunningFlagAfterPowerAbort();

    qDebug() << "关机命令执行失败:" << err;
    return false;
}

bool SystemController::reboot() {
    // [V2.2] 1) 先写"计划内重启"凭据（跨启动周期权威证据：即使 running.flag
    //         删除失败、或在"删标志→掉电"的 10~60 秒窗口内被二次创建
    //         （双实例自启动/二次检测触发 createRunningFlag），下次启动也能
    //         确定性判定为正常重启，不再走 journalctl/wtmp 概率性仲裁）
    IllegalShutdownDetector::markPlannedPower("reboot");

    // 2) 标记正常重启 - 删除运行标志文件（下次启动时检测此文件是否存在）
    IllegalShutdownDetector::markCleanShutdown();

    // 3) 同步文件系统缓冲区到磁盘，减少数据丢失风险
    QProcess::execute("sync");

    QString err;
    if (executePowerCommandWithFallback(
            QStringList() << "systemctl reboot" << "reboot" << "shutdown -r now",
            &err)) {
        qDebug() << "重启命令已发送，系统即将重启";
        return true;
    }

    // [V2.2] 4) 命令全部失败：回收凭据 + 重建运行标志（同 shutdown() 注释）
    IllegalShutdownDetector::clearPlannedPowerMarker();
    IllegalShutdownDetector::restoreRunningFlagAfterPowerAbort();

    qDebug() << "重启命令执行失败:" << err;
    return false;
}

bool SystemController::suspend() {
    // ---- 关键修复 ----
    // 1) 旧实现用 executeSystemCommand 走"普通执行"路径,polkit 在桌面环境外
    //    (systemd 用户服务 / SSH / tty) 会拒绝 systemctl suspend 命令。
    // 2) 旧实现没调 IllegalShutdownDetector::markCleanShutdown() → running.flag
    //    没被删除,唤醒后被误判为非法关机 +1
    // 修复:先标记为正常休眠 + 走提权路径
    // 注意:suspend(待机到内存)**不写** hibernate marker —— 待机中意外断电
    //      属于应当检出的非法关机场景,不能被 marker 掩盖;
    //      且 suspend 不重启(boot_id 不变),不影响下次启动检测。
    QStringList suspendCommands = {
        "systemctl suspend",
        "pm-suspend",
        "echo mem > /sys/power/state"
    };

    // 关键:必须在执行休眠命令**之前**删除 running.flag
    // 这样唤醒后 detectIllegalShutdown() 看到 flag 不存在 → 不会误判
    IllegalShutdownDetector::markCleanShutdown();
    // 2026-09-11 飞腾真机修复:同时写 suspend.marker,用于识别"suspend 期间被断电"——
    // marker 记录 suspend 命令发出时刻;启动时若 bootTime - markerTime < 60s
    // → 极可能是用户对挂起状态直接断电(没有给 BIOS 正常关电流程时间)
    // → 判定为非法关机。否则 marker 是用户"休眠→正常唤醒→正常关机"残留,清除。
    IllegalShutdownDetector::markSuspend();
    // 同步文件系统,确保 flag 删除与 marker 写入被持久化(机器马上要休眠)
    QProcess::execute("sync");

    QString err;
    if (executePowerCommandWithFallback(suspendCommands, &err)) {
        return true;
    }
    // 最后兜底:直接执行
    for (const QString &command : suspendCommands) {
        if (executeSystemCommand(command)) {
            return true;
        }
    }
    return false;
}

bool SystemController::hibernate() {
    // 与 suspend 同理,先走提权路径 + 标记为正常休眠
    // 关键:hibernate 命令发出后,机器会**断电**(镜像到 swap 后 poweroff)
    // 唤醒时 uptime 会从 0 重新开始 ——这是 hibernate 的固有行为
    //
    // ---- 为什么 hibernate 必须写 marker 而 suspend 不写 ----
    // hibernate = 内存写入 swap 后直接断电。journalctl 上个启动周期
    // 没有"正常关机流程"(无 Stopping Session / Reached target Shutdown),
    // 唤醒后即使 running.flag 已删,"系统证据仲裁"仍会把这次断电
    // 误判为非法关机(用户实测:两次休眠两次误报)。
    // marker 让 IllegalShutdownDetector 第零层识别"这是软件主动休眠"。
    // suspend 不断电不重启,无需 marker;且待机中真断电要能检出,不能掩盖。
    QStringList hibernateCommands = {
        "systemctl hibernate",
        "pm-hibernate",
        "echo disk > /sys/power/state"
    };

    // 关键:必须在执行休眠命令**之前**删除 running.flag
    IllegalShutdownDetector::markCleanShutdown();
    // 关键:写休眠标记(必须先删 flag 再写 marker,顺序无所谓但都要在断电前落盘)
    IllegalShutdownDetector::markHibernate();
    // 同步文件系统,确保 flag 删除与 marker 写入被持久化(机器马上要断电)
    QProcess::execute("sync");

    QString err;
    if (executePowerCommandWithFallback(hibernateCommands, &err)) {
        return true;
    }
    // 最后兜底:直接执行
    for (const QString &command : hibernateCommands) {
        if (executeSystemCommand(command)) {
            return true;
        }
    }
    return false;
}

// ---- 关键修复:带 RTC 自唤醒的休眠 ----
// 问题(用户实测,飞腾 D2000/D3000 平台):旧 cycle_controller 直接调 suspend(),
// 没有任何"休眠 N 秒后自动唤醒"机制(无 rtcwake / 无 systemd sleep hook)。
// "40 秒倒计时"实际是"唤醒→下次休眠的间隔",**不是休眠时长**。
// 第一次休眠被外部源唤醒(BIOS 定时开机 / 用户手动按键)后,
// 第二次进入 suspend 时外部源不再触发 → 永久卡死在"系统休眠中"状态。
//
// 修复策略:用 rtcwake 把"休眠 N 秒 + RTC 自动唤醒"做成一个原子操作。
// rtcwake -m mem -s N 命令:1) 设置 RTC 闹钟 N 秒后触发;
//                        2) 立即进入 mem 休眠;
//                        3) 闹钟触发时内核自动唤醒(无需应用层介入)。
// 飞腾 D2000/D3000 主板均支持 RTC 唤醒(标准 ARM SBSA);x86 BIOS 也普遍支持。
// 兜底:rtcwake 不存在或权限不足 → 走原 suspend() 路径(保留旧行为,不阻塞用户)。
bool SystemController::suspendWithWake(int seconds)
{
    if (seconds < 5) seconds = 5;  // rtcwake 最小秒数防御
    if (seconds > 86400 * 365) seconds = 86400 * 365;  // 最多一年

    // 检测 rtcwake 可用性
    QString rtcwakePath;
    if (QFile::exists("/usr/sbin/rtcwake")) rtcwakePath = "/usr/sbin/rtcwake";
    else if (QFile::exists("/sbin/rtcwake")) rtcwakePath = "/sbin/rtcwake";
    else {
        qWarning() << "[suspendWithWake] rtcwake 不在 /usr/sbin 或 /sbin,回退普通 suspend()";
        return suspend();
    }

    // 先做清理:删 flag + 写 marker(沿用 hibernate 的 marker 机制;mem 休眠断电后
    // 也会留下"无正常关机流程"的日志,marker 用于唤醒后识别)
    IllegalShutdownDetector::markCleanShutdown();
    IllegalShutdownDetector::markHibernate();
    QProcess::execute("sync");

    // 优先路径:rtcwake 原子休眠+唤醒
    // 注意:rtcwake 必须以 root 运行,polkit 在 desktop 自动放行,但 systemd 用户服务场景
    //      必须走 sudo。失败的真正原因往往是 RTC 设备节点不可写 / rtcwake 本身被禁。
    QStringList cmds = {
        QString("%1 -m mem -s %2").arg(rtcwakePath).arg(seconds),
        QString("echo mem > /sys/power/state")  // 极端兜底(此时无自唤醒,等同旧行为)
    };
    QString err;
    if (executePowerCommandWithFallback(cmds, &err)) {
        qDebug() << "[suspendWithWake] rtcwake 发起成功,机器将在" << seconds
                 << "秒后由 RTC 闹钟唤醒";
        return true;
    }

    // rtcwake 路径失败,降级到普通 suspend()(外部源唤醒)
    qWarning() << "[suspendWithWake] rtcwake 路径失败,降级到普通 suspend():" << err;
    return suspend();
}

// hibernate 自唤醒版:同样用 rtcwake,但 -m disk(hibernate 模式)。
// 飞腾 D2000 实测 rtcwake -m disk 在部分 BIOS 上不稳定,优先回退普通 hibernate。
bool SystemController::hibernateWithWake(int seconds)
{
    if (seconds < 5) seconds = 5;
    if (seconds > 86400 * 365) seconds = 86400 * 365;

    QString rtcwakePath;
    if (QFile::exists("/usr/sbin/rtcwake")) rtcwakePath = "/usr/sbin/rtcwake";
    else if (QFile::exists("/sbin/rtcwake")) rtcwakePath = "/sbin/rtcwake";
    else {
        qWarning() << "[hibernateWithWake] rtcwake 不存在,回退普通 hibernate()";
        return hibernate();
    }

    // 关键:必须先 markCleanShutdown + markHibernate(沿用 hibernate 逻辑,marker 持久化到磁盘)
    IllegalShutdownDetector::markCleanShutdown();
    IllegalShutdownDetector::markHibernate();
    QProcess::execute("sync");

    // 优先路径:rtcwake -m disk
    QStringList cmds = {
        QString("%1 -m disk -s %2").arg(rtcwakePath).arg(seconds),
        "systemctl hibernate",
        "pm-hibernate"
    };
    QString err;
    if (executePowerCommandWithFallback(cmds, &err)) {
        qDebug() << "[hibernateWithWake] rtcwake disk 模式发起成功";
        return true;
    }
    // rtcwake disk 不稳定,降级到普通 hibernate()(断电后由用户手动开机)
    qWarning() << "[hibernateWithWake] rtcwake disk 失败,降级到普通 hibernate():" << err;
    return hibernate();
}
