#include "netmix/protocol.h"

#include <QDataStream>
#include <QIODevice>

// ---------------------------------------------------------------------------
// QDataStream helpers: pin version to Qt_6_0 for wire stability
// ---------------------------------------------------------------------------

static void setStreamDefaults(QDataStream& stream) {
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setVersion(QDataStream::Qt_6_0);
}

// ---------------------------------------------------------------------------
// NetmixProtocolHeader
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixProtocolHeader& header) {
    stream << header.magic;
    stream << header.version;
    stream << header.type;
    stream << header.length;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixProtocolHeader& header) {
    stream >> header.magic;
    stream >> header.version;
    stream >> header.type;
    stream >> header.length;
    return stream;
}

// ---------------------------------------------------------------------------
// NetmixHello
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixHello& msg) {
    stream << msg.peerProtocolVersion;
    stream << msg.peerName;
    stream << msg.tickRate;
    stream << msg.rollbackWindow;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixHello& msg) {
    stream >> msg.peerProtocolVersion;
    stream >> msg.peerName;
    stream >> msg.tickRate;
    stream >> msg.rollbackWindow;
    return stream;
}

// ---------------------------------------------------------------------------
// NetmixHelloAck
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixHelloAck& msg) {
    stream << msg.peerProtocolVersion;
    stream << msg.peerName;
    stream << msg.peerId;
    stream << msg.tickRate;
    stream << msg.rollbackWindow;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixHelloAck& msg) {
    stream >> msg.peerProtocolVersion;
    stream >> msg.peerName;
    stream >> msg.peerId;
    stream >> msg.tickRate;
    stream >> msg.rollbackWindow;
    return stream;
}

// ---------------------------------------------------------------------------
// NetmixPing
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixPing& msg) {
    stream << msg.sentTick;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixPing& msg) {
    stream >> msg.sentTick;
    return stream;
}

// ---------------------------------------------------------------------------
// NetmixPong
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixPong& msg) {
    stream << msg.sentTick;
    stream << msg.remoteTick;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixPong& msg) {
    stream >> msg.sentTick;
    stream >> msg.remoteTick;
    return stream;
}

// ---------------------------------------------------------------------------
// NetmixInputFrameEvent
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixInputFrameEvent& evt) {
    stream << evt.wireId;
    stream << evt.value;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixInputFrameEvent& evt) {
    stream >> evt.wireId;
    stream >> evt.value;
    return stream;
}

// ---------------------------------------------------------------------------
// NetmixInputFrame
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixInputFrame& msg) {
    stream << msg.baseTick;
    stream << msg.events;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixInputFrame& msg) {
    stream >> msg.baseTick;
    stream >> msg.events;
    return stream;
}

// ---------------------------------------------------------------------------
// NetmixOwnershipClaim
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixOwnershipClaim& msg) {
    stream << msg.channelId;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixOwnershipClaim& msg) {
    stream >> msg.channelId;
    return stream;
}

// ---------------------------------------------------------------------------
// NetmixOwnershipGrant
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixOwnershipGrant& msg) {
    stream << msg.channelId;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixOwnershipGrant& msg) {
    stream >> msg.channelId;
    return stream;
}

// ---------------------------------------------------------------------------
// NetmixOwnershipDeny
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixOwnershipDeny& msg) {
    stream << msg.channelId;
    stream << msg.reason;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixOwnershipDeny& msg) {
    stream >> msg.channelId;
    stream >> msg.reason;
    return stream;
}

// ---------------------------------------------------------------------------
// NetmixOwnershipRelease
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixOwnershipRelease& msg) {
    stream << msg.channelId;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixOwnershipRelease& msg) {
    stream >> msg.channelId;
    return stream;
}

// ---------------------------------------------------------------------------
// NetmixTrackOffer
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixTrackOffer& msg) {
    stream << msg.hash;
    stream << msg.size;
    stream << msg.name;
    stream << msg.mime;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixTrackOffer& msg) {
    stream >> msg.hash;
    stream >> msg.size;
    stream >> msg.name;
    stream >> msg.mime;
    return stream;
}

// ---------------------------------------------------------------------------
// NetmixTrackAccept
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixTrackAccept& msg) {
    stream << msg.hash;
    stream << msg.haveBytes;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixTrackAccept& msg) {
    stream >> msg.hash;
    stream >> msg.haveBytes;
    return stream;
}

// ---------------------------------------------------------------------------
// NetmixTrackChunk
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixTrackChunk& msg) {
    stream << msg.hash;
    stream << msg.offset;
    stream << msg.data;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixTrackChunk& msg) {
    stream >> msg.hash;
    stream >> msg.offset;
    stream >> msg.data;
    return stream;
}

// ---------------------------------------------------------------------------
// NetmixTrackComplete
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixTrackComplete& msg) {
    stream << msg.hash;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixTrackComplete& msg) {
    stream >> msg.hash;
    return stream;
}

// ---------------------------------------------------------------------------
// NetmixTrackReady
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixTrackReady& msg) {
    stream << msg.hash;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixTrackReady& msg) {
    stream >> msg.hash;
    return stream;
}

// ---------------------------------------------------------------------------
// NetmixBye
// ---------------------------------------------------------------------------

QDataStream& operator<<(QDataStream& stream, const NetmixBye& msg) {
    stream << msg.reason;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, NetmixBye& msg) {
    stream >> msg.reason;
    return stream;
}

// ---------------------------------------------------------------------------
// encodeMessage
// ---------------------------------------------------------------------------

namespace {

void writePayload(QDataStream& stream, const NetmixPayload& payload) {
    std::visit([&stream](const auto& p) { stream << p; }, payload);
}

} // anonymous namespace

QByteArray encodeMessage(const NetmixMessage& msg) {
    QByteArray payloadBytes;
    {
        QDataStream payloadStream(&payloadBytes, QIODevice::WriteOnly);
        setStreamDefaults(payloadStream);
        writePayload(payloadStream, msg.payload);
    }

    NetmixProtocolHeader header;
    header.type = static_cast<quint16>(msg.type);
    header.length = static_cast<quint32>(payloadBytes.size());

    QByteArray result;
    QDataStream resultStream(&result, QIODevice::WriteOnly);
    setStreamDefaults(resultStream);
    resultStream << header;
    resultStream.writeRawData(payloadBytes.constData(), payloadBytes.size());

    return result;
}

// ---------------------------------------------------------------------------
// decodeMessage
// ---------------------------------------------------------------------------

std::optional<NetmixMessage> decodeMessage(const QByteArray& data) {
    if (data.size() < static_cast<int>(sizeof(NetmixProtocolHeader))) {
        qWarning("[Netmix] decodeMessage: truncated header (%lld bytes)", data.size());
        return std::nullopt;
    }

    QDataStream stream(data);
    setStreamDefaults(stream);

    NetmixProtocolHeader header;
    stream >> header;

    if (stream.status() != QDataStream::Ok) {
        qWarning("[Netmix] decodeMessage: failed to read header");
        return std::nullopt;
    }

    if (header.magic != kNetmixMagic) {
        qWarning("[Netmix] decodeMessage: bad magic 0x%08x", header.magic);
        return std::nullopt;
    }

    if (header.version != kNetmixProtocolVersion) {
        qWarning("[Netmix] decodeMessage: bad version %u", header.version);
        return std::nullopt;
    }

    if (header.length > static_cast<quint32>(data.size() - sizeof(NetmixProtocolHeader))) {
        qWarning("[Netmix] decodeMessage: declared length %u exceeds remaining data",
                header.length);
        return std::nullopt;
    }

    NetmixMessage msg;
    msg.type = static_cast<NetmixMessageType>(header.type);

    // Read payload bytes into a separate QByteArray so we can verify exact consumption.
    QByteArray payloadData = data.mid(sizeof(NetmixProtocolHeader), header.length);
    QDataStream payloadStream(payloadData);
    setStreamDefaults(payloadStream);

    bool ok = true;
    switch (msg.type) {
    case NetmixMessageType::Hello: {
        NetmixHello p;
        payloadStream >> p;
        msg.payload = p;
        break;
    }
    case NetmixMessageType::HelloAck: {
        NetmixHelloAck p;
        payloadStream >> p;
        msg.payload = p;
        break;
    }
    case NetmixMessageType::Ping: {
        NetmixPing p;
        payloadStream >> p;
        msg.payload = p;
        break;
    }
    case NetmixMessageType::Pong: {
        NetmixPong p;
        payloadStream >> p;
        msg.payload = p;
        break;
    }
    case NetmixMessageType::InputFrame: {
        NetmixInputFrame p;
        payloadStream >> p;
        msg.payload = p;
        break;
    }
    case NetmixMessageType::OwnershipClaim: {
        NetmixOwnershipClaim p;
        payloadStream >> p;
        msg.payload = p;
        break;
    }
    case NetmixMessageType::OwnershipGrant: {
        NetmixOwnershipGrant p;
        payloadStream >> p;
        msg.payload = p;
        break;
    }
    case NetmixMessageType::OwnershipDeny: {
        NetmixOwnershipDeny p;
        payloadStream >> p;
        msg.payload = p;
        break;
    }
    case NetmixMessageType::OwnershipRelease: {
        NetmixOwnershipRelease p;
        payloadStream >> p;
        msg.payload = p;
        break;
    }
    case NetmixMessageType::TrackOffer: {
        NetmixTrackOffer p;
        payloadStream >> p;
        msg.payload = p;
        break;
    }
    case NetmixMessageType::TrackAccept: {
        NetmixTrackAccept p;
        payloadStream >> p;
        msg.payload = p;
        break;
    }
    case NetmixMessageType::TrackChunk: {
        NetmixTrackChunk p;
        payloadStream >> p;
        msg.payload = p;
        break;
    }
    case NetmixMessageType::TrackComplete: {
        NetmixTrackComplete p;
        payloadStream >> p;
        msg.payload = p;
        break;
    }
    case NetmixMessageType::TrackReady: {
        NetmixTrackReady p;
        payloadStream >> p;
        msg.payload = p;
        break;
    }
    case NetmixMessageType::Bye: {
        NetmixBye p;
        payloadStream >> p;
        msg.payload = p;
        break;
    }
    default: {
        qWarning("[Netmix] decodeMessage: unknown message type %u",
                 static_cast<quint16>(msg.type));
        ok = false;
        break;
    }
    }

    if (!ok || payloadStream.status() != QDataStream::Ok) {
        qWarning("[Netmix] decodeMessage: payload deserialization failed");
        return std::nullopt;
    }

    // Verify we consumed exactly the declared payload — no trailing garbage.
    if (!payloadStream.atEnd()) {
        qWarning("[Netmix] decodeMessage: %lld unconsumed bytes after payload",
                 payloadData.size() - payloadStream.device()->pos());
        return std::nullopt;
    }

    return msg;
}
