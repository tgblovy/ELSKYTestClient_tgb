#include "wake_on_lan.h"
#include "server_connector.h"
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
