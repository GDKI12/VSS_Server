#ifndef VSS_PROTOCOL_H
#define VSS_PROTOCOL_H

#include <QByteArray>
#include <QDataStream>
#include <QtGlobal>

namespace VssProtocol {

constexpr quint32 Magic = 0x56535331; // "VSS1"
constexpr int HeaderSize = sizeof(quint32) + sizeof(quint8) + sizeof(quint32);
constexpr quint32 MaxPayloadSize = 4 * 1024 * 1024;
constexpr int VideoChunkSize = 256 * 1024;

enum class PacketType : quint8
{
    Meta = 1,
    VideoChunk = 2,
    VideoEnd = 3,
    Result = 4,
    Error = 5,
    Resume = 6
};

inline QByteArray makePacket(PacketType type, const QByteArray& payload)
{
    QByteArray packet;
    packet.reserve(HeaderSize + payload.size());

    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << Magic
           << static_cast<quint8>(type)
           << static_cast<quint32>(payload.size());

    packet.append(payload);
    return packet;
}

} // namespace VssProtocol

#endif // VSS_PROTOCOL_H
