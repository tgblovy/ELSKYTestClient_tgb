#ifndef PRIVILEGE_HELPER_H
#define PRIVILEGE_HELPER_H

#include <QString>

// QProcess 仅做指针返回，前置声明即可（避免包含 <QProcess> 拖入大量依赖）
class QProcess;

// 统一权限辅助模块
// 目标：消除各功能重复弹密码框的问题。
// 策略（两者结合）：
//   1) 优先：部署/安装时已配置 sudoers 免密（NOPASSWD），sudo -n 直接执行，全程无密码。
//   2) 降级：免密不可用时，读取"记忆密码"通过 sudo -S 静默执行。
//   3) 兜底：无记忆密码时弹出一次密码对话框，用户输入后自动记忆，后续功能复用。
class PrivilegeHelper
{
public:
    // 以管理员权限执行 shell 命令。
    // 返回 true 表示命令成功执行（退出码 0），false 表示失败或用户取消。
    // errorOutput 输出错误信息（用于日志/提示）。
    static bool runPrivileged(const QString &command, QString *errorOutput = nullptr);

    // 非交互版本：仅用免密 sudo 或记忆密码执行，不弹密码框。
    // 适合在非主线程中调用，失败后由调用方回到主线程兜底。
    static bool runPrivilegedSilent(const QString &command, QString *errorOutput = nullptr);

    // 确保当前进程已具备提权能力（确保后续 sudo -S / sudo -n 能通过）：
    //   - 免密：直接返回 true
    //   - 记忆密码：直接返回 true（sudo -S 会用到）
    //   - 都不可用：在主线程弹密码框，记忆后返回 true
    //   - 用户取消：返回 false
    // 调用后，调用方可直接用 QProcess 启动 "sudo -n bash script"
    // 或 "bash -c \"echo $PASSWORD | sudo -S bash script\""。
    // 阻塞式：内部可能弹 QInputDialog，需在主线程调用。
    static bool ensureAuthForPrivilege(QString *errorOutput = nullptr);

    // 启动一个提权 QProcess（用于需要实时输出/进度的长时间任务，如 U 盘测速）。
    // 返回的 QProcess 由调用方持有（用 new，调用方负责 delete）。
    // 内部已确保提权能力（调用 ensureAuthForPrivilege），会以 sudo -n 或 sudo -S 启动。
    static QProcess *startPrivileged(const QStringList &args, QString *errorOutput = nullptr);

    // 保存/读取记忆的密码（存于 ~/.config/ELSKY/.sudo_pass，权限 600）。
    // 密码做简单的 XOR 混淆，避免明文直接暴露（非高强度加密，仅防随手查看）。
    static void savePassword(const QString &password);
    static QString loadPassword();
    static void clearPassword();

    // 检查当前用户是否已配置 sudoers 免密（能否 sudo -n 直接执行）。
    static bool isPasswordlessSudo();

private:
    static QString passwordFilePath();
};

#endif // PRIVILEGE_HELPER_H
