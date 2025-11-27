#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <QString>
#include <QDataStream>
#include <QByteArray>

// 消息类型枚举
enum MessageType {
    MSG_WAKE_ON_LAN_SETUP = 1,  // 网络唤醒设置
    MSG_SHUTDOWN_REQUEST,       // 关机请求
    MSG_REBOOT_REQUEST,         // 重启请求
    MSG_SUSPEND_REQUEST,        // 睡眠请求
    MSG_HIBERNATE_REQUEST,      // 休眠请求
    MSG_STATUS_RESPONSE,        // 状态响应
    MSG_MANUAL_WAKE_REQUEST,    // 手动唤醒请求
    MSG_WAKE_ON_LAN_CANCEL,     // 取消网络唤醒
    MSG_CYCLE_CONTROL           // 循环控制
};

// 循环操作类型
enum CycleOperation {
    CYCLE_SHUTDOWN = 1,   // 关机
    CYCLE_REBOOT,         // 重启
    CYCLE_SUSPEND,        // 睡眠
    CYCLE_HIBERNATE       // 休眠
};

// 循环配置结构
struct CycleConfig {
    CycleOperation operation;  // 循环操作类型
    int intervalSeconds;       // 循环间隔时间（秒）
    int cycleCount;           // 循环次数
    bool enabled;             // 是否启用

    CycleConfig() : operation(CYCLE_SHUTDOWN), intervalSeconds(60), cycleCount(1), enabled(false) {}

    // 添加序列化操作符
    friend QDataStream &operator<<(QDataStream &out, const CycleConfig &config) {
        out << static_cast<quint32>(config.operation)
            << config.intervalSeconds
            << config.cycleCount
            << config.enabled;
        return out;
    }

    friend QDataStream &operator>>(QDataStream &in, CycleConfig &config) {
        quint32 operation;
        in >> operation >> config.intervalSeconds >> config.cycleCount >> config.enabled;
        config.operation = static_cast<CycleOperation>(operation);
        return in;
    }
};

// 网络唤醒配置结构
struct WakeOnLanConfig {
    QString macAddress;
    int intervalSeconds;        // 唤醒间隔（秒）
    bool enabled;

    WakeOnLanConfig() : intervalSeconds(60), enabled(false) {}

    // 添加序列化操作符
    friend QDataStream &operator<<(QDataStream &out, const WakeOnLanConfig &config) {
        out << config.macAddress << config.intervalSeconds << config.enabled;
        return out;
    }

    friend QDataStream &operator>>(QDataStream &in, WakeOnLanConfig &config) {
        in >> config.macAddress >> config.intervalSeconds >> config.enabled;
        return in;
    }
};

// 网络消息结构
struct NetworkMessage {
    MessageType type;
    QString clientId;
    QByteArray data;

    QByteArray toByteArray() const;
    static NetworkMessage fromByteArray(const QByteArray &data);
};

#endif // PROTOCOL_H
