#include "device_monitor.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

DeviceMonitor::DeviceMonitor(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
    , m_settings("ELSKY", "DeviceMonitor")
{
    connect(m_timer, &QTimer::timeout, this, &DeviceMonitor::checkDevices);
    loadLastDevices();
}

void DeviceMonitor::startMonitoring(int intervalMs)
{
    if (m_timer->isActive()) {
        return;
    }
    
    // 初始检测
    checkDevices();
    
    // 启动定时检测
    m_timer->start(intervalMs);
    emit logMessage(tr("设备监控已启动，检测间隔: %1ms").arg(intervalMs));
}

void DeviceMonitor::stopMonitoring()
{
    m_timer->stop();
    saveCurrentDevices();
    emit logMessage(tr("设备监控已停止"));
}

QVariantMap DeviceMonitor::getCurrentDevices() const
{
    return enumerateDevices();
}

QVariantMap DeviceMonitor::getLastSessionDevices() const
{
    return m_lastDevices;
}

void DeviceMonitor::checkDevices()
{
    QVariantMap currentDevices = enumerateDevices();
    
    // 检测新增设备
    for (auto it = currentDevices.constBegin(); it != currentDevices.constEnd(); ++it) {
        if (!m_lastDevices.contains(it.key())) {
            emit deviceChanged("added", it.value().toMap());
            emit logMessage(tr("检测到新设备: %1").arg(it.key()));
        }
    }
    
    // 检测移除设备
    for (auto it = m_lastDevices.constBegin(); it != m_lastDevices.constEnd(); ++it) {
        if (!currentDevices.contains(it.key())) {
            emit deviceChanged("removed", it.value().toMap());
            emit logMessage(tr("检测到设备移除: %1").arg(it.key()));
        }
    }
    
    m_lastDevices = currentDevices;
    saveCurrentDevices();
}

void DeviceMonitor::loadLastDevices()
{
    QByteArray jsonData = m_settings.value("lastDevices").toByteArray();
    QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    
    if (!doc.isNull() && doc.isObject()) {
        m_lastDevices = doc.object().toVariantMap();
    }
    emit logMessage(tr("加载上次会话设备信息，共 %1 个设备").arg(m_lastDevices.size()));
}

void DeviceMonitor::saveCurrentDevices()
{
    QJsonDocument doc(QJsonObject::fromVariantMap(m_lastDevices));
    m_settings.setValue("lastDevices", doc.toJson());
}

QVariantMap DeviceMonitor::enumerateDevices() const
{
    QVariantMap devices;
    
    // 枚举存储设备
    for (const QStorageInfo &storage : QStorageInfo::mountedVolumes()) {
        if (storage.isValid() && storage.isReady()) {
            QVariantMap info;
            info["type"] = "storage";
            info["name"] = storage.displayName();
            info["rootPath"] = storage.rootPath();
            info["device"] = storage.device();
            info["size"] = storage.bytesTotal();
            info["available"] = storage.bytesAvailable();
            info["fileSystem"] = storage.fileSystemType();
            
            devices[storage.rootPath()] = info;
        }
    }
    
    // 枚举串口设备
    for (const QSerialPortInfo &port : QSerialPortInfo::availablePorts()) {
        QVariantMap info;
        info["type"] = "serial";
        info["portName"] = port.portName();
        info["description"] = port.description();
        info["manufacturer"] = port.manufacturer();
        info["systemLocation"] = port.systemLocation();
        info["vendorIdentifier"] = port.vendorIdentifier();
        info["productIdentifier"] = port.productIdentifier();
        
        devices[port.portName()] = info;
    }
    
    // 枚举网络接口
    for (const QNetworkInterface &interface : QNetworkInterface::allInterfaces()) {
        if (interface.isValid() && !interface.hardwareAddress().isEmpty()) {
            QVariantMap info;
            info["type"] = "network";
            info["name"] = interface.name();
            info["hardwareAddress"] = interface.hardwareAddress();
            info["humanReadableName"] = interface.humanReadableName();
            
            QVariantList addresses;
            for (const QNetworkAddressEntry &entry : interface.addressEntries()) {
                QVariantMap addr;
                addr["ip"] = entry.ip().toString();
                addr["netmask"] = entry.netmask().toString();
                addr["broadcast"] = entry.broadcast().toString();
                addresses.append(addr);
            }
            info["addresses"] = addresses;
            
            devices[interface.name()] = info;
        }
    }
    
    return devices;
}