#include "server_connector.h"
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>

ServerConnector::ServerConnector(QObject *parent)
    : QObject(parent)
    , tcpSocket(nullptr)
    , serverCheckBox(nullptr)
    , m_isConnected(false)
{
    tcpSocket = new QTcpSocket(this);
    connect(tcpSocket, &QTcpSocket::connected, this, &ServerConnector::onConnected);
    connect(tcpSocket, &QTcpSocket::disconnected, this, &ServerConnector::onDisconnected);
    connect(tcpSocket, &QTcpSocket::readyRead, this, &ServerConnector::onReadyRead);
    connect(tcpSocket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(onErrorOccurred(QAbstractSocket::SocketError)));
}

ServerConnector::~ServerConnector()
{
    if (m_isConnected) {
        disconnectFromServer();
    }
}

void ServerConnector::setupUI(QCheckBox *checkBox)
{
    serverCheckBox = checkBox;
    connect(serverCheckBox, &QCheckBox::toggled, this, &ServerConnector::onServerConnectToggled);

    // 加载保存的设置
    QSettings settings("ELSKY", "TestClient");
    bool serverConnected = settings.value("server/connected", false).toBool();
    serverIP = settings.value("server/ip", "").toString();
    serverCheckBox->setChecked(serverConnected);
}

void ServerConnector::connectToServer(const QString &ip)
{
    if (ip.isEmpty()) {
        emit logMessage("错误: 服务器地址不能为空");
        serverCheckBox->setChecked(false);
        return;
    }

    quint16 port = 8888;
    emit logMessage(QString("正在连接到服务器 %1:%2").arg(ip).arg(port));
    tcpSocket->connectToHost(ip, port);

    if (!tcpSocket->waitForConnected(5000)) {
        emit logMessage("连接服务器超时");
        serverCheckBox->setChecked(false);
    }
}

void ServerConnector::disconnectFromServer()
{
    tcpSocket->disconnectFromHost();
}

void ServerConnector::sendMessage(const NetworkMessage &msg)
{
    if (m_isConnected) {
        QByteArray data = msg.toByteArray();
        tcpSocket->write(data);
    }
}

void ServerConnector::onConnected()
{
    m_isConnected = true;
    emit logMessage("成功连接到服务器");
    emit connectionStatusChanged(true);
}

void ServerConnector::onDisconnected()
{
    m_isConnected = false;
    emit logMessage("与服务器断开连接");
    serverCheckBox->setChecked(false);
    emit connectionStatusChanged(false);
}

void ServerConnector::onReadyRead()
{
    if (m_isConnected) {
        QByteArray data = tcpSocket->readAll();
        if (!data.isEmpty()) {
            QString message = QString::fromUtf8(data);
            emit messageReceived(message.trimmed());
            emit logMessage("从服务器接收: " + message.trimmed());
        }
    }
}

void ServerConnector::onErrorOccurred(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    emit logMessage("网络错误: " + tcpSocket->errorString());
    serverCheckBox->setChecked(false);
}

void ServerConnector::onServerConnectToggled(bool checked)
{
    if (checked) {
        bool ok;
        QString ip = QInputDialog::getText(nullptr, "连接服务器",
                                         "请输入服务器IP地址:",
                                         QLineEdit::Normal,
                                         "192.168.1.100", &ok);
        if (ok && !ip.isEmpty()) {
            serverIP = ip;
            emit logMessage("尝试连接服务器: " + ip);
            connectToServer(ip);

            // 保存设置
            QSettings settings("ELSKY", "TestClient");
            settings.setValue("server/ip", serverIP);
        } else {
            serverCheckBox->setChecked(false);
        }
    } else {
        if (m_isConnected) {
            disconnectFromServer();
        }
        serverIP.clear();
        emit logMessage("已断开服务器连接");

        // 保存设置
        QSettings settings("ELSKY", "TestClient");
        settings.setValue("server/connected", false);
    }
}
