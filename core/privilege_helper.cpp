#include "privilege_helper.h"

#include <QProcess>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QInputDialog>
#include <QLineEdit>
#include <QByteArray>
#include <QApplication>

// XOR 混淆密钥（仅防明文查看，非安全加密）
static const QByteArray kXorKey = QByteArray("ELSKY-2024-xor-key");

static QByteArray xorTransform(const QByteArray &data)
{
    QByteArray out(data);
    for (int i = 0; i < out.size(); ++i) {
        out[i] = out[i] ^ kXorKey[i % kXorKey.size()];
    }
    return out;
}

QString PrivilegeHelper::passwordFilePath()
{
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (configDir.isEmpty()) {
        configDir = QDir::homePath() + "/.config";
    }
    return configDir + "/ELSKY/.sudo_pass";
}

void PrivilegeHelper::savePassword(const QString &password)
{
    if (password.isEmpty()) return;

    QFileInfo fi(passwordFilePath());
    QDir dir = fi.absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QByteArray encoded = xorTransform(password.toUtf8()).toBase64();
    QFile file(passwordFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(encoded);
        file.close();
        // 仅当前用户可读写
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    }
}

QString PrivilegeHelper::loadPassword()
{
    QFile file(passwordFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QByteArray encoded = file.readAll();
    file.close();

    QByteArray decoded = QByteArray::fromBase64(encoded);
    return QString::fromUtf8(xorTransform(decoded));
}

void PrivilegeHelper::clearPassword()
{
    QFile file(passwordFilePath());
    if (file.exists()) {
        file.remove();
    }
}

bool PrivilegeHelper::isPasswordlessSudo()
{
    QProcess p;
    p.start("sudo", QStringList() << "-n" << "true");
    p.waitForFinished(3000);
    return (p.exitCode() == 0);
}

bool PrivilegeHelper::runPrivilegedSilent(const QString &command, QString *errorOutput)
{
    if (command.trimmed().isEmpty()) {
        return false;
    }

    // 策略1：sudoers 已配置免密，用 sudo -n 执行（无弹窗）
    // 注意：免密只是 sudo 不再询问密码，命令仍需 sudo 前缀才有 root 权限
    if (isPasswordlessSudo()) {
        QProcess p;
        p.start("sudo", QStringList() << "-n" << "bash" << "-c" << command);
        p.waitForFinished(-1);
        if (p.exitCode() == 0) {
            return true;
        }
        if (errorOutput) {
            QString combined = QString::fromUtf8(p.readAllStandardError()) + "\n" +
                               QString::fromUtf8(p.readAllStandardOutput());
            *errorOutput = combined.trimmed();
        }
        return false;
    }

    // 策略2：使用记忆的密码静默执行
    // 安全修复：密码绝不能拼进命令行（旧实现 echo '密码' | sudo -S 会让密码
    // 出现在 /proc/<pid>/cmdline 中，本机任意用户 ps 可见；且密码含单引号时
    // 命令直接破裂）。改为：直接启动 sudo -S，密码通过 stdin 管道写入。
    QString remembered = loadPassword();
    if (!remembered.isEmpty()) {
        QProcess p;
        // -S 从 stdin 读密码；-p '' 去掉提示符避免污染输出
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start("sudo", QStringList() << "-S" << "-p" << "" << "bash" << "-c" << command);
        if (!p.waitForStarted(3000)) {
            if (errorOutput) *errorOutput = "无法启动 sudo 进程";
            return false;
        }
        p.write((remembered + "\n").toUtf8());
        p.closeWriteChannel();
        // 注意：不能设超时后 kill——suspend/hibernate 等命令在机器休眠期间
        // 会长时间不退出，超时误杀会在唤醒后触发错误的回退链
        p.waitForFinished(-1);
        QString output = QString::fromUtf8(p.readAll());
        if (p.exitCode() == 0 && !output.contains("incorrect password")) {
            return true;
        }
        // 密码错误，清除记忆
        clearPassword();
        if (errorOutput) {
            *errorOutput = output.trimmed();
        }
    }
    return false;
}

bool PrivilegeHelper::runPrivileged(const QString &command, QString *errorOutput)
{
    if (command.trimmed().isEmpty()) {
        return false;
    }

    // 先尝试免密/记忆密码（非交互），成功则直接返回
    QString silentErr;
    if (runPrivilegedSilent(command, &silentErr)) {
        return true;
    }

    // 兜底：弹出密码框询问，成功后记忆
    QInputDialog dlg(nullptr);
    dlg.setWindowTitle("需要管理员权限");
    dlg.setLabelText("该操作需要管理员权限，请输入密码（仅本次询问，后续操作自动复用）：");
    dlg.setTextEchoMode(QLineEdit::Password);
    dlg.setOkButtonText("确认");
    dlg.setCancelButtonText("取消");

    if (dlg.exec() != QDialog::Accepted) {
        if (errorOutput) {
            *errorOutput = "用户取消输入密码";
        }
        return false;
    }

    QString password = dlg.textValue();
    if (password.isEmpty()) {
        if (errorOutput) {
            *errorOutput = "密码为空";
        }
        return false;
    }

    // 安全修复：密码通过 stdin 传递（不进命令行），命令作为独立参数（无 shell 注入）
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start("sudo", QStringList() << "-S" << "-p" << "" << "bash" << "-c" << command);
    if (!p.waitForStarted(3000)) {
        if (errorOutput) *errorOutput = "无法启动 sudo 进程";
        return false;
    }
    p.write((password + "\n").toUtf8());
    p.closeWriteChannel();
    p.waitForFinished(-1);
    QString output = QString::fromUtf8(p.readAll());

    if (p.exitCode() == 0 && !output.contains("incorrect password")) {
        savePassword(password);  // 记忆密码，后续复用
        return true;
    }

    if (errorOutput) {
        *errorOutput = output.trimmed();
    }
    return false;
}

bool PrivilegeHelper::ensureAuthForPrivilege(QString *errorOutput)
{
    // 免密：已具备提权能力
    if (isPasswordlessSudo()) {
        return true;
    }
    // 记忆密码：后续 sudo -S 会用到
    QString remembered = loadPassword();
    if (!remembered.isEmpty()) {
        // 验证记忆密码是否仍然有效（密码可能已被管理员修改）
        // 修复（C2）：旧实现 echo '密码' | sudo -S true 把密码拼进命令行——
        // 1) 密码出现在 /proc/<pid>/cmdline，同机任意用户 ps 可见；
        // 2) 密码含单引号时命令破裂，含 $()/; 时存在命令注入。
        // 与 runPrivilegedSilent 的安全实现保持一致：密码只走 stdin。
        QProcess test;
        test.setProcessChannelMode(QProcess::MergedChannels);
        test.start("sudo", QStringList() << "-S" << "-p" << "" << "bash" << "-c" << "true");
        if (test.waitForStarted(3000)) {
            test.write((remembered + "\n").toUtf8());
            test.closeWriteChannel();
            test.waitForFinished(5000);
            QString out = QString::fromUtf8(test.readAll());
            if (test.exitCode() == 0 && !out.contains("incorrect password")) {
                return true;
            }
        }
        // 密码失效/验证失败，清除
        clearPassword();
    }

    // 主线程弹密码框
    QInputDialog dlg;
    dlg.setWindowTitle("需要管理员权限");
    dlg.setLabelText("该操作需要管理员权限，请输入密码（仅本次询问，后续自动复用）：");
    dlg.setTextEchoMode(QLineEdit::Password);
    dlg.setOkButtonText("确认");
    dlg.setCancelButtonText("取消");

    if (dlg.exec() != QDialog::Accepted) {
        if (errorOutput) *errorOutput = "用户取消";
        return false;
    }
    QString password = dlg.textValue();
    if (password.isEmpty()) {
        if (errorOutput) *errorOutput = "密码为空";
        return false;
    }

    // 立即用 sudo -S 验证密码是否正确（密码走 stdin，不进命令行）
    QProcess test;
    test.setProcessChannelMode(QProcess::MergedChannels);
    test.start("sudo", QStringList() << "-S" << "-p" << "" << "true");
    if (test.waitForStarted(3000)) {
        test.write((password + "\n").toUtf8());
        test.closeWriteChannel();
        test.waitForFinished(5000);
    }
    QString out = QString::fromUtf8(test.readAll());
    if (test.exitCode() != 0 || out.contains("incorrect password")) {
        if (errorOutput) *errorOutput = "密码错误";
        return false;
    }

    savePassword(password);
    return true;
}

QProcess *PrivilegeHelper::startPrivileged(const QStringList &args, QString *errorOutput)
{
    if (args.isEmpty()) {
        if (errorOutput) *errorOutput = "参数为空";
        return nullptr;
    }

    // 先确保已具备提权能力（可能弹窗），仅在主线程安全执行
    if (!ensureAuthForPrivilege(errorOutput)) {
        return nullptr;
    }

    QProcess *proc = new QProcess();

    // 优先免密 sudo -n
    if (isPasswordlessSudo()) {
        proc->start("sudo", QStringList() << "-n" << "bash" << args);
        return proc;
    }

    // 记忆密码 sudo -S（密码通过 stdin 写入，不进命令行；args 作为独立
    // 参数传递，含空格/特殊字符的路径不再需要转义）
    QString remembered = loadPassword();
    if (!remembered.isEmpty()) {
        QStringList sudoArgs;
        sudoArgs << "-S" << "-p" << "" << "bash" << args;
        proc->start("sudo", sudoArgs);
        if (proc->waitForStarted(3000)) {
            proc->write((remembered + "\n").toUtf8());
            proc->closeWriteChannel();
            return proc;
        }
        // 启动失败，走兜底逻辑
    }

    delete proc;
    if (errorOutput) *errorOutput = "提权准备失败";
    return nullptr;
}
