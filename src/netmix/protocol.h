#pragma once

#include <QByteArray>
#include <QDataStream>
#include <QString>
#include <QVector>
#include <optional>
#include <variant>

constexpr quint32 kNetmixMagic = 0x584D4E;
constexpr quint16 kNetmixProtocolVersion = 5;

enum class NetmixMessageType : quint16 {
    Hello = 0,
    HelloAck = 1,
    Ping = 2,
    Pong = 3,
    InputFrame = 4,
    OwnershipClaim = 5,
    OwnershipGrant = 6,
    OwnershipDeny = 7,
    OwnershipRelease = 8,
    TrackOffer = 9,
    TrackAccept = 10,
    TrackChunk = 11,
    TrackComplete = 12,
    TrackReady = 13,
    Bye = 14,
    CueSnapshot = 15,
};

struct NetmixProtocolHeader {
    quint32 magic = kNetmixMagic;
    quint16 version = kNetmixProtocolVersion;
    quint16 type = 0;
    quint32 length = 0;
};

static_assert(sizeof(NetmixProtocolHeader) == 12,
        "NetmixProtocolHeader must be exactly 12 bytes (no padding)");

QDataStream& operator<<(QDataStream& stream, const NetmixProtocolHeader& header);
QDataStream& operator>>(QDataStream& stream, NetmixProtocolHeader& header);

struct NetmixHello {
    quint16 peerProtocolVersion = kNetmixProtocolVersion;
    QString peerName;
    quint16 tickRate = 240;
    quint16 rollbackWindow = 8;
    quint16 udpPort = 0;
    QVector<quint16> preassignedChannels;
};

QDataStream& operator<<(QDataStream& stream, const NetmixHello& msg);
QDataStream& operator>>(QDataStream& stream, NetmixHello& msg);

struct NetmixHelloAck {
    quint16 peerProtocolVersion = kNetmixProtocolVersion;
    QString peerName;
    quint8 peerId = 0;
    quint16 tickRate = 240;
    quint16 rollbackWindow = 8;
    quint32 initiatorTick = 0;
    QVector<quint16> preassignedChannels;
};

QDataStream& operator<<(QDataStream& stream, const NetmixHelloAck& msg);
QDataStream& operator>>(QDataStream& stream, NetmixHelloAck& msg);

struct NetmixPing {
    quint32 sentTick = 0;
};

QDataStream& operator<<(QDataStream& stream, const NetmixPing& msg);
QDataStream& operator>>(QDataStream& stream, NetmixPing& msg);

struct NetmixPong {
    quint32 sentTick = 0;
    quint32 remoteTick = 0;
};

QDataStream& operator<<(QDataStream& stream, const NetmixPong& msg);
QDataStream& operator>>(QDataStream& stream, NetmixPong& msg);

struct NetmixInputFrameEvent {
    quint16 wireId = 0;
    double value = 0.0;
};

QDataStream& operator<<(QDataStream& stream, const NetmixInputFrameEvent& evt);
QDataStream& operator>>(QDataStream& stream, NetmixInputFrameEvent& evt);

struct NetmixInputFrame {
    quint32 baseTick = 0;
    QVector<NetmixInputFrameEvent> events;
};

QDataStream& operator<<(QDataStream& stream, const NetmixInputFrame& msg);
QDataStream& operator>>(QDataStream& stream, NetmixInputFrame& msg);

struct NetmixOwnershipClaim {
    quint16 channelId = 0;
};

QDataStream& operator<<(QDataStream& stream, const NetmixOwnershipClaim& msg);
QDataStream& operator>>(QDataStream& stream, NetmixOwnershipClaim& msg);

struct NetmixOwnershipGrant {
    quint16 channelId = 0;
};

QDataStream& operator<<(QDataStream& stream, const NetmixOwnershipGrant& msg);
QDataStream& operator>>(QDataStream& stream, NetmixOwnershipGrant& msg);

struct NetmixOwnershipDeny {
    quint16 channelId = 0;
    quint8 reason = 0;
};

QDataStream& operator<<(QDataStream& stream, const NetmixOwnershipDeny& msg);
QDataStream& operator>>(QDataStream& stream, NetmixOwnershipDeny& msg);

struct NetmixOwnershipRelease {
    quint16 channelId = 0;
};

QDataStream& operator<<(QDataStream& stream, const NetmixOwnershipRelease& msg);
QDataStream& operator>>(QDataStream& stream, NetmixOwnershipRelease& msg);

struct NetmixTrackOffer {
    QByteArray hash; // 32 bytes SHA-256
    quint64 size = 0;
    QString name;
    QString mime;
    quint16 channelId = 0; // v5: which deck to load into on receiver
};

QDataStream& operator<<(QDataStream& stream, const NetmixTrackOffer& msg);
QDataStream& operator>>(QDataStream& stream, NetmixTrackOffer& msg);

struct NetmixTrackAccept {
    QByteArray hash; // 32 bytes
    quint64 haveBytes = 0;
};

QDataStream& operator<<(QDataStream& stream, const NetmixTrackAccept& msg);
QDataStream& operator>>(QDataStream& stream, NetmixTrackAccept& msg);

struct NetmixTrackChunk {
    QByteArray hash; // 32 bytes
    quint64 offset = 0;
    QByteArray data;
};

QDataStream& operator<<(QDataStream& stream, const NetmixTrackChunk& msg);
QDataStream& operator>>(QDataStream& stream, NetmixTrackChunk& msg);

struct NetmixTrackComplete {
    QByteArray hash; // 32 bytes
};

QDataStream& operator<<(QDataStream& stream, const NetmixTrackComplete& msg);
QDataStream& operator>>(QDataStream& stream, NetmixTrackComplete& msg);

struct NetmixTrackReady {
    QByteArray hash; // 32 bytes
};

QDataStream& operator<<(QDataStream& stream, const NetmixTrackReady& msg);
QDataStream& operator>>(QDataStream& stream, NetmixTrackReady& msg);

struct NetmixBye {
    QString reason;
};

QDataStream& operator<<(QDataStream& stream, const NetmixBye& msg);
QDataStream& operator>>(QDataStream& stream, NetmixBye& msg);

struct NetmixCueSnapshotEntry {
    quint16 type = 0;
    qint32 hotcueIndex = -1;
    double startPositionSamples = -1.0;
    double endPositionSamples = -1.0;
    quint32 color = 0;
    QString label;
};

QDataStream& operator<<(QDataStream& stream, const NetmixCueSnapshotEntry& entry);
QDataStream& operator>>(QDataStream& stream, NetmixCueSnapshotEntry& entry);

struct NetmixCueSnapshot {
    QByteArray hash; // 32 bytes SHA-256
    QVector<NetmixCueSnapshotEntry> cues;
};

QDataStream& operator<<(QDataStream& stream, const NetmixCueSnapshot& msg);
QDataStream& operator>>(QDataStream& stream, NetmixCueSnapshot& msg);

using NetmixPayload = std::variant<
        NetmixHello,
        NetmixHelloAck,
        NetmixPing,
        NetmixPong,
        NetmixInputFrame,
        NetmixOwnershipClaim,
        NetmixOwnershipGrant,
        NetmixOwnershipDeny,
        NetmixOwnershipRelease,
        NetmixTrackOffer,
        NetmixTrackAccept,
        NetmixTrackChunk,
        NetmixTrackComplete,
        NetmixTrackReady,
        NetmixBye,
        NetmixCueSnapshot>;

struct NetmixMessage {
    NetmixMessageType type;
    NetmixPayload payload;
};

QByteArray encodeMessage(const NetmixMessage& msg);
std::optional<NetmixMessage> decodeMessage(const QByteArray& data);
