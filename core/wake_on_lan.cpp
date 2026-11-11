#include "wake_on_lan.h"
#include "server_connector.h"
#include "privilege_helper.h"
#include <QCoreApplication>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QMessageBox>
#include <QInputDialog>
#include <QNetworkInterface>
#include <QDataStream>
#include <QSettings>
#include <QProcess>

WakeOnLan::WakeOnLan(QObject *parent)
    : QObject(parent)
    , wolToggleButton(nullptr)
    , serverConnector(nullptr)
    , isWakeOnLanEnabled(false)
    , currentInterval(60)
{
}

void WakeOnLan::setupUI(QPushButton *button)
{
    wolToggleButton = button;
    connect(wolToggleButton, &QPushButton::clicked, this, &WakeOnLan::onWakeOnLanToggle);

    // 加载保存的状态
    loadState();
}

void WakeOnLan::loadState()
{
    QSettings settings("ELSKY", "TestClient");
    isWakeOnLanEnabled = settings.value("wakeonlan/enabled", false).toBool();
    currentMacAddress = settings.value("wakeonlan/macAddress", "").toString();
    currentInterval = settings.value("wakeonlan/interval", 60).toInt();

    if (wolToggleButton) {
        if (isWakeOnLanEnabled && !currentMacAddress.isEmpty()) {
            wolToggleButton->setText("取消唤醒");
            wolToggleButton->setStyleSheet("QPushButton { background-color: #f44336; color: white; }");
            emit logMessage("检测到未完成的网络唤醒配置，状态已恢复");
        } else {
            wolToggleButton->setText("启动唤醒");
            wolToggleButton->setStyleSheet("QPushButton { background-color: #2196F3; color: white; }");
            isWakeOnLanEnabled = false;
        }
    }
}

void WakeOnLan::saveState()
{
    QSettings settings("ELSKY", "TestClient");
    settings.setValue("wakeonlan/enabled", isWakeOnLanEnabled);
    settings.setValue("wakeonlan/macAddress", currentMacAddress);
    settings.setValue("wakeonlan/interval", currentInterval);
}

QString WakeOnLan::getLocalMacAddress()
{
    foreach (QNetworkInterface interface, QNetworkInterface::allInterfaces()) {
        // 跳过回环接口和未启用的接口
        if (interface.flags().testFlag(QNetworkInterface::IsLoopBack) ||
            !interface.flags().testFlag(QNetworkInterface::IsUp) ||
            !interface.flags().testFlag(QNetworkInterface::IsRunning)) {
            continue;
        }

        // 优先选择以太网接口
        if (interface.type() == QNetworkInterface::Ethernet &&
            !interface.hardwareAddress().isEmpty() &&
            interface.hardwareAddress() != "00:00:00:00:00:00") {
            return interface.hardwareAddress();
        }
    }

    // 如果没有找到以太网接口，返回第一个有效的MAC地址
    foreach (QNetworkInterface interface, QNetworkInterface::allInterfaces()) {
        if (!interface.hardwareAddress().isEmpty() &&
            interface.hardwareAddress() != "00:00:00:00:00:00" &&
            interface.flags().testFlag(QNetworkInterface::IsUp) &&
            interface.flags().testFlag(QNetworkInterface::IsRunning)) {
            return interface.hardwareAddress();
        }
    }

    return "00:00:00:00:00:00";
}

void WakeOnLan::onWakeOnLanToggle()
{
    if (!serverConnector || !serverConnector->isConnected()) {
        QMessageBox::warning(nullptr, "连接错误", "请先连接到服务器才能设置网络唤醒功能");
        return;
    }

    if (!isWakeOnLanEnabled) {
        setupWakeOnLan();
    } else {
        cancelWakeOnLan();
    }
}

void WakeOnLan::setupWakeOnLan()
{
    QDialog dialog;
    dialog.setWindowTitle("设置网络唤醒");
    dialog.setFixedSize(300, 150);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QHBoxLayout *macLayout = new QHBoxLayout();
    QLabel *macLabel = new QLabel("MAC地址:", &dialog);
    QLineEdit *macEdit = new QLineEdit(&dialog);
    macEdit->setText(getLocalMacAddress());
    macEdit->setPlaceholderText("00:11:22:33:44:55");
    macLayout->addWidget(macLabel);
    macLayout->addWidget(macEdit);

    QHBoxLayout *intervalLayout = new QHBoxLayout();
    QLabel *intervalLabel = new QLabel("唤醒间隔:", &dialog);
    QSpinBox *intervalSpin = new QSpinBox(&dialog);
    intervalSpin->setRange(1, 86400);
    intervalSpin->setValue(currentInterval);
    intervalSpin->setSuffix("秒");
    intervalLayout->addWidget(intervalLabel);
    intervalLayout->addWidget(intervalSpin);

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    QPushButton *okButton = new QPushButton("确定", &dialog);
    QPushButton *cancelButton = new QPushButton("取消", &dialog);
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);

    layout->addLayout(macLayout);
    layout->addLayout(intervalLayout);
    layout->addLayout(buttonLayout);

    connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        QString macAddress = macEdit->text().trimmed();
        if (macAddress.isEmpty()) {
            QMessageBox::warning(nullptr, "警告", "请输入MAC地址");
            return;
        }

        // 1. 关闭防火墙 + 2. 麒麟系统设置 Magic
        // 使用 pkexec 弹出图形化认证框，一次认证执行所有 sudo 命令
        emit logMessage("[唤醒] 正在配置网络唤醒环境（需要授权）...");

        // 先获取系统信息（无需 root）
        QProcess osDetect;
        osDetect.start("bash", QStringList() << "-c"
            << "cat /etc/os-release 2>/dev/null | grep -i 'kylin' | grep -iv 'cmd'");
        osDetect.waitForFinished(5000);
        bool isKylin = (osDetect.exitCode() == 0);

        QProcess uosDetect;
        uosDetect.start("bash", QStringList() << "-c"
            << "cat /etc/os-release 2>/dev/null | grep -iE 'uOS|uniontech'");
        uosDetect.waitForFinished(5000);
        bool isUOS = (uosDetect.exitCode() == 0);

        QProcess ifaceProcess;
        ifaceProcess.start("bash", QStringList() << "-c"
            << "ip route show default 2>/dev/null | awk '{print $5}' | head -1");
        ifaceProcess.waitForFinished(5000);
        QString iface = QString::fromUtf8(ifaceProcess.readAllStandardOutput()).trimmed();

        // 获取活动连接名（nmcli）
        QString connName;
        if (!iface.isEmpty()) {
            QProcess connProcess;
            connProcess.start("bash", QStringList() << "-c"
                << QString("nmcli -t -f NAME,DEVICE con show --active 2>/dev/null | grep '%1' | cut -d: -f1 | head -1").arg(iface));
            connProcess.waitForFinished(5000);
            connName = QString::fromUtf8(connProcess.readAllStandardOutput()).trimmed();
        }

        // 构建所有 sudo 命令，通过 pkexec 一次性执行
        QString script = "#!/bin/bash\n";
        // 修复（C8）：临时文件名加本进程 PID，多实例/并发运行时互不覆盖、互误删
        const QString tmpTag = QString::number(QCoreApplication::applicationPid());
        const QString resultFile = QString("/tmp/elsky_fw_result_%1.txt").arg(tmpTag);

        // === 关闭防火墙（麒麟/统信/通用）===
        if (isKylin) {
            // 麒麟系统：使用 kylin-firewall 命令
            script += "kylin-firewall -s off 2>&1\n";
            script += "systemctl restart ksc-defender-daemon 2>/dev/null\n";
            script += "if [ -f /etc/kylin-firewall/kylin-firewall.conf ]; then\n";
            script += "    head -26 /etc/kylin-firewall/kylin-firewall.conf > /tmp/fw_conf_clean_$$.txt\n";
            script += "    cp /tmp/fw_conf_clean_$$.txt /etc/kylin-firewall/kylin-firewall.conf\n";
            script += "    rm -f /tmp/fw_conf_clean_$$.txt\n";
            script += "fi\n";
            script += "pkill -u $(logname 2>/dev/null || echo it0) -f '/usr/sbin/ksc-defender' 2>/dev/null || true\n";
        } else {
            // 统信及其他系统：尝试 ufw 或 firewalld
            script += "if command -v ufw >/dev/null 2>&1; then\n";
            script += "    ufw disable 2>&1\n";
            script += "elif command -v firewall-cmd >/dev/null 2>&1; then\n";
            script += "    systemctl stop firewalld 2>/dev/null\n";
            script += "    systemctl disable firewalld 2>/dev/null\n";
            script += "fi\n";
        }
        // 验证防火墙状态（结果文件带 PID，防多实例覆盖）
        script += QString("RFILE=%1\n").arg(resultFile);
        if (isKylin) {
            script += "kylin-firewall -g > $RFILE 2>&1\n";
        } else if (isUOS) {
            script += "if command -v ufw >/dev/null 2>&1; then ufw status > $RFILE 2>&1; else echo 'unknown' > $RFILE; fi\n";
        } else {
            script += "if command -v ufw >/dev/null 2>&1; then ufw status > $RFILE 2>&1; elif command -v firewall-cmd >/dev/null 2>&1; then firewall-cmd --state > $RFILE 2>&1; else echo 'no firewall tool found' > $RFILE; fi\n";
        }
        script += "chmod 666 $RFILE\n";

        // 网卡 Magic 唤醒设置（全系统通用：ethtool/nmcli 均为跨发行版标准工具，
        // Ubuntu/Debian/CentOS 与麒麟/统信行为一致；deb 包 postinst 已将
        // ethtool 兜底安装到 /usr/local/bin，无需联网）
        // 之前误将此段限定在麒麟/统信 → Ubuntu 上网卡 Magic 模式从未开启，
        // 关机后主板收不到魔术包 → 无法网络唤醒（根因）
        if (!iface.isEmpty()) {
            // ethtool 设置（内核级，立即生效）
            script += QString("ethtool -s %1 wol g 2>&1\n").arg(iface);
            // nmcli 设置（持久化到 NetworkManager 连接配置，重启后仍生效）
            if (!connName.isEmpty()) {
                script += QString("nmcli c modify \"%1\" 802-3-ethernet.wake-on-lan magic 2>&1\n").arg(connName);
            } else {
                script += QString("nmcli c modify \"%1\" 802-3-ethernet.wake-on-lan magic 2>&1\n").arg(iface);
            }
            // Ubuntu GNOME 的 NetworkManager 某些电源档位(power-profiles-daemon
            // 的 latency/performance 档)会在启动时覆盖禁用 WOL —— 写入 nmcli
            // 持久配置可与之对抗；再为网卡绑定做一次运行时确认
            script += "sleep 1\n";
            script += QString("ethtool -s %1 wol g 2>/dev/null\n").arg(iface);
        }

        // 写入临时脚本文件并执行（带 PID，防多实例覆盖）
        QString scriptPath = QString("/tmp/elsky_wol_setup_%1.sh").arg(tmpTag);
        QProcess writeScript;
        writeScript.start("bash", QStringList() << "-c"
            << QString("cat > '%1' << 'ELSKEY_WOL_SCRIPT'\n%2\nELSKEY_WOL_SCRIPT").arg(scriptPath).arg(script));
        writeScript.waitForFinished(3000);
        QProcess::execute("bash", QStringList() << "-c" << QString("chmod +x '%1'").arg(scriptPath));

        // 使用统一权限模块执行脚本（优先免密/记忆密码，兜底弹窗）
        QString errOutput;
        bool execOk = PrivilegeHelper::runPrivileged(QString("bash '%1'").arg(scriptPath), &errOutput);

        // 清理临时脚本
        QProcess::execute("bash", QStringList() << "-c" << QString("rm -f '%1'").arg(scriptPath));

        if (execOk) {
            emit logMessage("[唤醒] 配置脚本执行完成");
        } else {
            emit logMessage("[唤醒] 配置执行失败");
            if (!errOutput.isEmpty()) {
                emit logMessage(QString("[唤醒] 错误: %1").arg(errOutput.left(200)));
            }
        }

        // 读取防火墙诊断结果（读取同 PID 的结果文件）
        QProcess readResult;
        readResult.start("bash", QStringList() << "-c" << QString("cat '%1' 2>/dev/null").arg(resultFile));
        readResult.waitForFinished(3000);
        QString resultOutput = QString::fromUtf8(readResult.readAllStandardOutput()).trimmed();
        if (!resultOutput.isEmpty()) {
            QStringList resultLines = resultOutput.split("\n");
            for (const QString &line : resultLines) {
                if (!line.trimmed().isEmpty()) {
                    emit logMessage(QString("[防火墙] %1").arg(line.trimmed()));
                }
            }
        }
        QProcess::execute("bash", QStringList() << "-c" << QString("rm -f '%1'").arg(resultFile));

        // WOL 状态验证（全系统通用，输出到日志便于诊断）
        if (!iface.isEmpty()) {
            QProcess verifyWol;
            verifyWol.start("bash", QStringList() << "-c"
                << QString("ethtool %1 2>/dev/null | grep -i 'Wake-on'").arg(iface));
            verifyWol.waitForFinished(3000);
            QString wolStatus = QString::fromUtf8(verifyWol.readAllStandardOutput()).trimmed();
            if (wolStatus.isEmpty()) {
                // ethtool 不可用时，用 nmcli 验证
                QString verifyConn = connName.isEmpty() ? iface : connName;
                QProcess verifyNmcli;
                verifyNmcli.start("bash", QStringList() << "-c"
                    << QString("nmcli -f 802-3-ethernet.wake-on-lan c show \"%1\" 2>/dev/null").arg(verifyConn));
                verifyNmcli.waitForFinished(3000);
                QString nmcliWol = QString::fromUtf8(verifyNmcli.readAllStandardOutput()).trimmed();
                emit logMessage(QString("[唤醒] nmcli Wake-on-LAN: %1").arg(nmcliWol.isEmpty() ? "未获取到" : nmcliWol));
            } else {
                emit logMessage(QString("[唤醒] 网卡 %1 Wake-on-LAN: %2").arg(iface, wolStatus));
            }
        }

        if (execOk && !iface.isEmpty()) {
            emit logMessage(QString("[唤醒] 网卡 %1 LAN 唤醒已设置为 Magic 模式").arg(iface));
        }

        if (serverConnector->isConnected()) {
            NetworkMessage msg;
            msg.type = MSG_WAKE_ON_LAN_SETUP;
            msg.clientId = getLocalMacAddress();
            WakeOnLanConfig config;
            config.macAddress = macAddress;
            config.intervalSeconds = intervalSpin->value();
            config.enabled = true;
            QByteArray data;
            QDataStream stream(&data, QIODevice::WriteOnly);
            stream << config;
            msg.data = data;
            serverConnector->sendMessage(msg);

            isWakeOnLanEnabled = true;
            currentMacAddress = macAddress;
            currentInterval = intervalSpin->value();

            saveState();

            wolToggleButton->setText("取消唤醒");
            wolToggleButton->setStyleSheet("QPushButton { background-color: #f44336; color: white; }");
            emit logMessage(QString("设置网络唤醒: MAC=%1, 间隔=%2秒").arg(macAddress).arg(intervalSpin->value()));
        } else {
            QMessageBox::warning(nullptr, "连接错误", "与服务器的连接已断开，无法设置网络唤醒");
        }
    }
}

void WakeOnLan::cancelWakeOnLan()
{
    if (serverConnector->isConnected()) {
        NetworkMessage msg;
        msg.type = MSG_WAKE_ON_LAN_CANCEL;
        msg.clientId = getLocalMacAddress();
        WakeOnLanConfig config;
        config.macAddress = currentMacAddress.isEmpty() ? getLocalMacAddress() : currentMacAddress;
        config.enabled = false;
        QByteArray data;
        QDataStream stream(&data, QIODevice::WriteOnly);
        stream << config;
        msg.data = data;
        serverConnector->sendMessage(msg);

        isWakeOnLanEnabled = false;

        QSettings settings("ELSKY", "TestClient");
        settings.remove("wakeonlan");

        wolToggleButton->setText("启动唤醒");
        wolToggleButton->setStyleSheet("QPushButton { background-color: #2196F3; color: white; }");
        emit logMessage("取消网络唤醒");
    } else {
        QMessageBox::warning(nullptr, "连接错误", "与服务器的连接已断开，无法取消网络唤醒");
    }
}

void WakeOnLan::cancelWakeOnLanAuto()
{
    if (!serverConnector || !serverConnector->isConnected()) {
        emit logMessage("[唤醒] 错误: 未连接服务器，无法取消唤醒");
        return;
    }

    NetworkMessage msg;
    msg.type = MSG_WAKE_ON_LAN_CANCEL;
    msg.clientId = getLocalMacAddress();
    WakeOnLanConfig config;
    config.macAddress = currentMacAddress.isEmpty() ? getLocalMacAddress() : currentMacAddress;
    config.enabled = false;
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << config;
    msg.data = data;
    serverConnector->sendMessage(msg);

    isWakeOnLanEnabled = false;
    QSettings settings("ELSKY", "TestClient");
    settings.remove("wakeonlan");

    if (wolToggleButton) {
        wolToggleButton->setText("启动唤醒");
        wolToggleButton->setStyleSheet("QPushButton { background-color: #2196F3; color: white; }");
    }
    emit logMessage("[唤醒] 已取消网络唤醒");
}

void WakeOnLan::startWakeOnLanAuto()
{
    if (!serverConnector || !serverConnector->isConnected()) {
        emit logMessage("[唤醒] 错误: 未连接服务器，无法启动唤醒");
        return;
    }

    QString macAddress = getLocalMacAddress();
    if (macAddress.isEmpty() || macAddress == "00:00:00:00:00:00") {
        emit logMessage("[唤醒] 错误: 无法获取本机MAC地址");
        return;
    }

    emit logMessage("[唤醒] 收到启动唤醒指令（自动模式）...");

    // 检测活动网卡（Magic 模式设置对所有发行版生效，不再区分麒麟/统信）
    QProcess ifaceProcess;
    ifaceProcess.start("bash", QStringList() << "-c" << "ip route show default 2>/dev/null | awk '{print $5}' | head -1");
    ifaceProcess.waitForFinished(5000);
    QString iface = QString::fromUtf8(ifaceProcess.readAllStandardOutput()).trimmed();

    QString connName;
    if (!iface.isEmpty()) {
        QProcess connProcess;
        connProcess.start("bash", QStringList() << "-c"
            << QString("nmcli -t -f NAME,DEVICE con show --active 2>/dev/null | grep '%1' | cut -d: -f1 | head -1").arg(iface));
        connProcess.waitForFinished(5000);
        connName = QString::fromUtf8(connProcess.readAllStandardOutput()).trimmed();
    }

    // 仅设置 Magic 模式（使用 sudo，通过部署时配置的 NOPASSWD 免密）
    // 修复：旧实现仅对麒麟/统信设置 Magic，其它发行版直接视为成功——
    // 导致 Ubuntu 等平台自动唤醒关机后无法被唤醒（手动路径 setupWakeOnLan
    // 早已对所有发行版设置，此处保持一致）。
    bool magicOk = false;
    if (!iface.isEmpty()) {
        // 确保 ethtool 已安装
        QProcess::execute("bash", QStringList() << "-c"
            << "which ethtool >/dev/null 2>&1 || sudo apt-get install -y ethtool 2>/dev/null");

        // ethtool 设置 Magic 模式
        QProcess ethtoolProcess;
        ethtoolProcess.start("bash", QStringList() << "-c"
            << QString("sudo ethtool -s %1 wol g 2>&1").arg(iface));
        ethtoolProcess.waitForFinished(10000);
        QString ethtoolOutput = QString::fromUtf8(ethtoolProcess.readAllStandardOutput()).trimmed();
        QString ethtoolErr = QString::fromUtf8(ethtoolProcess.readAllStandardError()).trimmed();

        // nmcli 设置 Magic 模式
        QString target = connName.isEmpty() ? iface : connName;
        QProcess nmcliProcess;
        nmcliProcess.start("bash", QStringList() << "-c"
            << QString("sudo nmcli c modify \"%1\" 802-3-ethernet.wake-on-lan magic 2>&1").arg(target));
        nmcliProcess.waitForFinished(10000);
        QString nmcliOutput = QString::fromUtf8(nmcliProcess.readAllStandardOutput()).trimmed();
        QString nmcliErr = QString::fromUtf8(nmcliProcess.readAllStandardError()).trimmed();

        if (ethtoolProcess.exitCode() == 0 && nmcliProcess.exitCode() == 0) {
            magicOk = true;
            emit logMessage(QString("[唤醒] 网卡 %1 LAN 唤醒已设置为 Magic 模式").arg(iface));
        } else {
            emit logMessage(QString("[唤醒] Magic 模式设置可能失败"));
            if (!ethtoolErr.isEmpty()) emit logMessage(QString("[唤醒] ethtool: %1").arg(ethtoolErr.left(100)));
            if (!nmcliErr.isEmpty()) emit logMessage(QString("[唤醒] nmcli: %1").arg(nmcliErr.left(100)));
        }

        // 验证 Wake-on-LAN 状态
        QProcess verifyProcess;
        verifyProcess.start("bash", QStringList() << "-c"
            << QString("ethtool %1 2>/dev/null | grep -i 'Wake-on'").arg(iface));
        verifyProcess.waitForFinished(3000);
        QString wolStatus = QString::fromUtf8(verifyProcess.readAllStandardOutput()).trimmed();
        if (!wolStatus.isEmpty()) {
            emit logMessage(QString("[唤醒] 验证 - %1: %2").arg(iface, wolStatus));
        } else {
            QProcess verifyNmcli;
            verifyNmcli.start("bash", QStringList() << "-c"
                << QString("nmcli -f 802-3-ethernet.wake-on-lan c show \"%1\" 2>/dev/null").arg(target));
            verifyNmcli.waitForFinished(3000);
            QString nmcliStatus = QString::fromUtf8(verifyNmcli.readAllStandardOutput()).trimmed();
            emit logMessage(QString("[唤醒] 验证 - nmcli: %1").arg(nmcliStatus.isEmpty() ? "未获取到" : nmcliStatus));
        }
    } else {
        emit logMessage("[唤醒] 未检测到活动网卡，跳过 Magic 模式设置（关机唤醒可能不可用）");
        magicOk = true; // 无网卡时不阻断流程，但已明确提示
    }

    if (magicOk) {
        // 发送WOL配置到服务器
        NetworkMessage msg;
        msg.type = MSG_WAKE_ON_LAN_SETUP;
        msg.clientId = getLocalMacAddress();
        WakeOnLanConfig config;
        config.macAddress = macAddress;
        config.intervalSeconds = currentInterval;
        config.enabled = true;
        QByteArray data;
        QDataStream stream(&data, QIODevice::WriteOnly);
        stream << config;
        msg.data = data;
        serverConnector->sendMessage(msg);

        isWakeOnLanEnabled = true;
        currentMacAddress = macAddress;
        saveState();
        if (wolToggleButton) {
            wolToggleButton->setText("取消唤醒");
            wolToggleButton->setStyleSheet("QPushButton { background-color: #f44336; color: white; }");
        }
        emit logMessage(QString("[唤醒] 启动成功 MAC=%1, 间隔=%2秒").arg(macAddress).arg(currentInterval));
    }
}
