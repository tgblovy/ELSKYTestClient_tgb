#include "protocol.h"
#include <QDataStream>

QByteArray NetworkMessage::toByteArray() const {
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << static_cast<quint32>(type);
    stream << clientId;
    stream << this->data;
    return data;
}

NetworkMessage NetworkMessage::fromByteArray(const QByteArray &data) {
    NetworkMessage msg;
    QDataStream stream(data);
    quint32 type;
    stream >> type;
    msg.type = static_cast<MessageType>(type);
    stream >> msg.clientId;
    stream >> msg.data;
    return msg;
}
