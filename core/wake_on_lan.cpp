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
#include <QDataStream>  // 添加这行

WakeOnLan::WakeOnLan(QObject *parent)
    : QObject(parent)
    , wolToggleButton(nullptr)
    , serverConnector(nullptr)
    , isWakeOnLanEnabled(false)
{
}

void WakeOnLan::setupUI(QPushButton *button)
{
    wolToggleButton = button;
    connect(wolToggleButton, &QPushButton::clicked, this, &WakeOnLan::onWakeOnLanToggle);
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

QString WakeOnLan::getLocalMacAddress()
{
    foreach (QNetworkInterface interface, QNetworkInterface::allInterfaces()) {
        if (interface.flags().testFlag(QNetworkInterface::IsLoopBack) ||
            !interface.flags().testFlag(QNetworkInterface::IsUp) ||
            !interface.flags().testFlag(QNetworkInterface::IsRunning)) {
            continue;
        }
        if (!interface.hardwareAddress().isEmpty()) {
            return interface.hardwareAddress();
        }
    }
    return "00:00:00:00:00:00";
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
    intervalSpin->setValue(60);
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
        config.macAddress = getLocalMacAddress();
        config.enabled = false;
        QByteArray data;
        QDataStream stream(&data, QIODevice::WriteOnly);
        stream << config;
        msg.data = data;
        serverConnector->sendMessage(msg);

        isWakeOnLanEnabled = false;
        wolToggleButton->setText("启动唤醒");
        wolToggleButton->setStyleSheet("QPushButton { background-color: #2196F3; color: white; }");
        emit logMessage("取消网络唤醒");
    } else {
        QMessageBox::warning(nullptr, "连接错误", "与服务器的连接已断开，无法取消网络唤醒");
    }
}
