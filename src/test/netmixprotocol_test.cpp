#include <gtest/gtest.h>

#include <QtDebug>
#include <QVector>

#include "netmix/protocol.h"

namespace {

class NetmixProtocolTest : public ::testing::Test {};

// ---------------------------------------------------------------------------
// Round-trip tests — encode then decode, verify fields match
// ---------------------------------------------------------------------------

TEST_F(NetmixProtocolTest, RoundTrip_Hello) {
    NetmixHello payload;
    payload.peerProtocolVersion = 1;
    payload.peerName = QStringLiteral("alice");
    payload.tickRate = 480;
    payload.rollbackWindow = 16;

    NetmixMessage msg{NetmixMessageType::Hello, payload};
    QByteArray wire = encodeMessage(msg);
    auto decoded = decodeMessage(wire);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(NetmixMessageType::Hello, decoded->type);
    auto* p = std::get_if<NetmixHello>(&decoded->payload);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(1, p->peerProtocolVersion);
    EXPECT_EQ(QStringLiteral("alice"), p->peerName);
    EXPECT_EQ(480, p->tickRate);
    EXPECT_EQ(16, p->rollbackWindow);
}

TEST_F(NetmixProtocolTest, RoundTrip_HelloAck) {
    NetmixHelloAck payload;
    payload.peerProtocolVersion = 1;
    payload.peerName = QStringLiteral("bob");
    payload.peerId = 7;
    payload.tickRate = 240;
    payload.rollbackWindow = 8;

    NetmixMessage msg{NetmixMessageType::HelloAck, payload};
    auto decoded = decodeMessage(encodeMessage(msg));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(NetmixMessageType::HelloAck, decoded->type);
    auto* p = std::get_if<NetmixHelloAck>(&decoded->payload);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(1, p->peerProtocolVersion);
    EXPECT_EQ(QStringLiteral("bob"), p->peerName);
    EXPECT_EQ(7, p->peerId);
    EXPECT_EQ(240, p->tickRate);
    EXPECT_EQ(8, p->rollbackWindow);
}

TEST_F(NetmixProtocolTest, RoundTrip_Ping) {
    NetmixPing payload;
    payload.sentTick = 42;

    NetmixMessage msg{NetmixMessageType::Ping, payload};
    auto decoded = decodeMessage(encodeMessage(msg));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(NetmixMessageType::Ping, decoded->type);
    auto* p = std::get_if<NetmixPing>(&decoded->payload);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(42u, p->sentTick);
}

TEST_F(NetmixProtocolTest, RoundTrip_Pong) {
    NetmixPong payload;
    payload.sentTick = 100;
    payload.remoteTick = 200;

    NetmixMessage msg{NetmixMessageType::Pong, payload};
    auto decoded = decodeMessage(encodeMessage(msg));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(NetmixMessageType::Pong, decoded->type);
    auto* p = std::get_if<NetmixPong>(&decoded->payload);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(100u, p->sentTick);
    EXPECT_EQ(200u, p->remoteTick);
}

TEST_F(NetmixProtocolTest, RoundTrip_InputFrame) {
    NetmixInputFrame payload;
    payload.baseTick = 1000;
    payload.events = {
        {1, 0.5},
        {2, -0.75},
        {3, 1.0},
    };

    NetmixMessage msg{NetmixMessageType::InputFrame, payload};
    auto decoded = decodeMessage(encodeMessage(msg));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(NetmixMessageType::InputFrame, decoded->type);
    auto* p = std::get_if<NetmixInputFrame>(&decoded->payload);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(1000u, p->baseTick);
    ASSERT_EQ(3, p->events.size());
    EXPECT_EQ(1, p->events[0].wireId);
    EXPECT_DOUBLE_EQ(0.5, p->events[0].value);
    EXPECT_EQ(2, p->events[1].wireId);
    EXPECT_DOUBLE_EQ(-0.75, p->events[1].value);
    EXPECT_EQ(3, p->events[2].wireId);
    EXPECT_DOUBLE_EQ(1.0, p->events[2].value);
}

TEST_F(NetmixProtocolTest, RoundTrip_OwnershipClaim) {
    NetmixOwnershipClaim payload;
    payload.channelId = 3;

    NetmixMessage msg{NetmixMessageType::OwnershipClaim, payload};
    auto decoded = decodeMessage(encodeMessage(msg));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(NetmixMessageType::OwnershipClaim, decoded->type);
    auto* p = std::get_if<NetmixOwnershipClaim>(&decoded->payload);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(3, p->channelId);
}

TEST_F(NetmixProtocolTest, RoundTrip_OwnershipGrant) {
    NetmixOwnershipGrant payload;
    payload.channelId = 5;

    NetmixMessage msg{NetmixMessageType::OwnershipGrant, payload};
    auto decoded = decodeMessage(encodeMessage(msg));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(NetmixMessageType::OwnershipGrant, decoded->type);
    auto* p = std::get_if<NetmixOwnershipGrant>(&decoded->payload);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(5, p->channelId);
}

TEST_F(NetmixProtocolTest, RoundTrip_OwnershipDeny) {
    NetmixOwnershipDeny payload;
    payload.channelId = 2;
    payload.reason = 1;

    NetmixMessage msg{NetmixMessageType::OwnershipDeny, payload};
    auto decoded = decodeMessage(encodeMessage(msg));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(NetmixMessageType::OwnershipDeny, decoded->type);
    auto* p = std::get_if<NetmixOwnershipDeny>(&decoded->payload);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(2, p->channelId);
    EXPECT_EQ(1, p->reason);
}

TEST_F(NetmixProtocolTest, RoundTrip_OwnershipRelease) {
    NetmixOwnershipRelease payload;
    payload.channelId = 9;

    NetmixMessage msg{NetmixMessageType::OwnershipRelease, payload};
    auto decoded = decodeMessage(encodeMessage(msg));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(NetmixMessageType::OwnershipRelease, decoded->type);
    auto* p = std::get_if<NetmixOwnershipRelease>(&decoded->payload);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(9, p->channelId);
}

TEST_F(NetmixProtocolTest, RoundTrip_TrackOffer) {
    NetmixTrackOffer payload;
    payload.hash = QByteArray(32, '\xAB');
    payload.size = 12345678;
    payload.name = QStringLiteral("song.mp3");
    payload.mime = QStringLiteral("audio/mpeg");

    NetmixMessage msg{NetmixMessageType::TrackOffer, payload};
    auto decoded = decodeMessage(encodeMessage(msg));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(NetmixMessageType::TrackOffer, decoded->type);
    auto* p = std::get_if<NetmixTrackOffer>(&decoded->payload);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(QByteArray(32, '\xAB'), p->hash);
    EXPECT_EQ(12345678u, p->size);
    EXPECT_EQ(QStringLiteral("song.mp3"), p->name);
    EXPECT_EQ(QStringLiteral("audio/mpeg"), p->mime);
}

TEST_F(NetmixProtocolTest, RoundTrip_TrackAccept) {
    NetmixTrackAccept payload;
    payload.hash = QByteArray(32, '\x42');
    payload.haveBytes = 500000;

    NetmixMessage msg{NetmixMessageType::TrackAccept, payload};
    auto decoded = decodeMessage(encodeMessage(msg));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(NetmixMessageType::TrackAccept, decoded->type);
    auto* p = std::get_if<NetmixTrackAccept>(&decoded->payload);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(QByteArray(32, '\x42'), p->hash);
    EXPECT_EQ(500000u, p->haveBytes);
}

TEST_F(NetmixProtocolTest, RoundTrip_TrackChunk) {
    QByteArray chunkData;
    chunkData.resize(64 * 1024);
    for (int i = 0; i < chunkData.size(); ++i) {
        chunkData[i] = static_cast<char>(i & 0xFF);
    }

    NetmixTrackChunk payload;
    payload.hash = QByteArray(32, '\x99');
    payload.offset = 65536;
    payload.data = chunkData;

    NetmixMessage msg{NetmixMessageType::TrackChunk, payload};
    auto decoded = decodeMessage(encodeMessage(msg));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(NetmixMessageType::TrackChunk, decoded->type);
    auto* p = std::get_if<NetmixTrackChunk>(&decoded->payload);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(QByteArray(32, '\x99'), p->hash);
    EXPECT_EQ(65536u, p->offset);
    ASSERT_EQ(chunkData.size(), p->data.size());
    EXPECT_EQ(chunkData, p->data);
}

TEST_F(NetmixProtocolTest, RoundTrip_TrackComplete) {
    NetmixTrackComplete payload;
    payload.hash = QByteArray(32, '\x11');

    NetmixMessage msg{NetmixMessageType::TrackComplete, payload};
    auto decoded = decodeMessage(encodeMessage(msg));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(NetmixMessageType::TrackComplete, decoded->type);
    auto* p = std::get_if<NetmixTrackComplete>(&decoded->payload);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(QByteArray(32, '\x11'), p->hash);
}

TEST_F(NetmixProtocolTest, RoundTrip_TrackReady) {
    NetmixTrackReady payload;
    payload.hash = QByteArray(32, '\x22');

    NetmixMessage msg{NetmixMessageType::TrackReady, payload};
    auto decoded = decodeMessage(encodeMessage(msg));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(NetmixMessageType::TrackReady, decoded->type);
    auto* p = std::get_if<NetmixTrackReady>(&decoded->payload);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(QByteArray(32, '\x22'), p->hash);
}

TEST_F(NetmixProtocolTest, RoundTrip_Bye) {
    NetmixBye payload;
    payload.reason = QStringLiteral("leaving");

    NetmixMessage msg{NetmixMessageType::Bye, payload};
    auto decoded = decodeMessage(encodeMessage(msg));

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(NetmixMessageType::Bye, decoded->type);
    auto* p = std::get_if<NetmixBye>(&decoded->payload);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(QStringLiteral("leaving"), p->reason);
}

// ---------------------------------------------------------------------------
// Malformed-input tests
// ---------------------------------------------------------------------------

TEST_F(NetmixProtocolTest, BadMagic_ReturnsNullopt) {
    NetmixHello payload;
    payload.peerProtocolVersion = 1;
    payload.peerName = QStringLiteral("alice");

    NetmixMessage msg{NetmixMessageType::Hello, payload};
    QByteArray wire = encodeMessage(msg);
    ASSERT_GE(wire.size(), 4);
    wire[0] = 0x00;
    wire[1] = 0x00;
    wire[2] = 0x00;
    wire[3] = 0x00;

    auto decoded = decodeMessage(wire);
    EXPECT_FALSE(decoded.has_value());
}

TEST_F(NetmixProtocolTest, BadVersion_ReturnsNullopt) {
    NetmixPing payload;
    payload.sentTick = 42;

    NetmixMessage msg{NetmixMessageType::Ping, payload};
    QByteArray wire = encodeMessage(msg);
    // version is bytes 4-5 in the header (little-endian)
    ASSERT_GE(wire.size(), 6);
    wire[4] = 0xFF;
    wire[5] = 0x00;

    auto decoded = decodeMessage(wire);
    EXPECT_FALSE(decoded.has_value());
}

TEST_F(NetmixProtocolTest, TruncatedHeader_ReturnsNullopt) {
    QByteArray truncated(8, '\x00');
    auto decoded = decodeMessage(truncated);
    EXPECT_FALSE(decoded.has_value());
}

TEST_F(NetmixProtocolTest, TruncatedPayload_ReturnsNullopt) {
    NetmixBye payload;
    payload.reason = QStringLiteral("bye");

    NetmixMessage msg{NetmixMessageType::Bye, payload};
    QByteArray wire = encodeMessage(msg);
    // Keep only the 12-byte header — omit the payload
    QByteArray truncated = wire.left(12);

    auto decoded = decodeMessage(truncated);
    EXPECT_FALSE(decoded.has_value());
}

TEST_F(NetmixProtocolTest, EmptyInput_ReturnsNullopt) {
    QByteArray empty;
    auto decoded = decodeMessage(empty);
    EXPECT_FALSE(decoded.has_value());
}

TEST_F(NetmixProtocolTest, UnknownType_ReturnsNullopt) {
    // Build a valid message then overwrite the type field to 0xFF
    NetmixPing payload;
    payload.sentTick = 42;

    NetmixMessage msg{NetmixMessageType::Ping, payload};
    QByteArray wire = encodeMessage(msg);
    // type is bytes 6-7 in the header (little-endian)
    ASSERT_GE(wire.size(), 8);
    wire[6] = 0xFF;
    wire[7] = 0x00;

    auto decoded = decodeMessage(wire);
    EXPECT_FALSE(decoded.has_value());
}

// ---------------------------------------------------------------------------
// Determinism — same input => same output bytes
// ---------------------------------------------------------------------------

TEST_F(NetmixProtocolTest, DeterministicEncoding) {
    NetmixPing payload;
    payload.sentTick = 42;

    NetmixMessage msg{NetmixMessageType::Ping, payload};
    QByteArray wire1 = encodeMessage(msg);
    QByteArray wire2 = encodeMessage(msg);

    EXPECT_EQ(wire1, wire2);
}

} // namespace
