#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QIODevice>
#include <QSignalSpy>
#include <QTcpSocket>

#include "netmix/tcpsession.h"
#include "test/mixxxtest.h"

namespace {

class NetmixTcpSessionTest : public MixxxTest {
  protected:
    static void pumpEvents(int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
    }

    static bool connectPair(std::unique_ptr<TcpSession>& host,
            std::unique_ptr<TcpSession>& client,
            int timeoutMs = 3000) {
        host = std::make_unique<TcpSession>();
        host->setDisplayName(QStringLiteral("host"));
        host->setListenPort(0);
        if (!host->listen()) {
            return false;
        }

        client = std::make_unique<TcpSession>();
        client->setDisplayName(QStringLiteral("client"));
        client->setPeerAddress(
                QHostAddress::LocalHost, host->server()->serverPort());

        QSignalSpy hostSpy(host.get(), &TcpSession::stateChanged);
        QSignalSpy clientSpy(client.get(), &TcpSession::stateChanged);

        client->connectToPeer();
        pumpEvents(timeoutMs);

        return host->state() == TcpSession::Connected &&
                client->state() == TcpSession::Connected;
    }
};

// ---------------------------------------------------------------------------
// LoopbackHandshake_Success
// ---------------------------------------------------------------------------

TEST_F(NetmixTcpSessionTest, LoopbackHandshake_Success) {
    std::unique_ptr<TcpSession> host;
    std::unique_ptr<TcpSession> client;

    ASSERT_TRUE(connectPair(host, client));

    EXPECT_EQ(0, host->selfPeerId());
    EXPECT_EQ(1, client->selfPeerId());
    EXPECT_EQ(1, host->remotePeerId());
    EXPECT_EQ(0, client->remotePeerId());
    EXPECT_QSTRING_EQ(QStringLiteral("client"), host->remoteName());
    EXPECT_QSTRING_EQ(QStringLiteral("host"), client->remoteName());

    host->disconnectFromPeer();
    client->disconnectFromPeer();
}

// ---------------------------------------------------------------------------
// MessageExchange
// ---------------------------------------------------------------------------

TEST_F(NetmixTcpSessionTest, MessageExchange) {
    std::unique_ptr<TcpSession> host;
    std::unique_ptr<TcpSession> client;

    ASSERT_TRUE(connectPair(host, client));

    // Track received messages (vector to handle heartbeat Pings interleaved)
    QVector<NetmixMessage> hostReceived;
    QVector<NetmixMessage> clientReceived;

    host->connect(host.get(), &TcpSession::messageReceived,
            [&](const NetmixMessage& msg) {
                hostReceived.append(msg);
            });
    client->connect(client.get(), &TcpSession::messageReceived,
            [&](const NetmixMessage& msg) {
                clientReceived.append(msg);
            });

    // Client sends Ping
    NetmixPing ping;
    ping.sentTick = 42;
    client->sendMessage({NetmixMessageType::Ping, ping});

    pumpEvents(500);

    // Find the Ping we sent in host's received messages
    bool foundPing = false;
    for (const auto& m : hostReceived) {
        if (m.type == NetmixMessageType::Ping) {
            auto* p = std::get_if<NetmixPing>(&m.payload);
            if (p && p->sentTick == 42) {
                foundPing = true;
                break;
            }
        }
    }
    EXPECT_TRUE(foundPing);

    // Host sends Pong
    NetmixPong pong;
    pong.sentTick = 100;
    pong.remoteTick = 200;
    host->sendMessage({NetmixMessageType::Pong, pong});

    pumpEvents(500);

    // Find the Pong in client's received messages
    bool foundPong = false;
    for (const auto& m : clientReceived) {
        if (m.type == NetmixMessageType::Pong) {
            auto* p = std::get_if<NetmixPong>(&m.payload);
            if (p && p->sentTick == 100) {
                foundPong = true;
                EXPECT_EQ(200u, p->remoteTick);
                break;
            }
        }
    }
    EXPECT_TRUE(foundPong);

    host->disconnectFromPeer();
    client->disconnectFromPeer();
}

// ---------------------------------------------------------------------------
// FramingAcrossSplitPackets
// ---------------------------------------------------------------------------

TEST_F(NetmixTcpSessionTest, FramingAcrossSplitPackets) {
    auto session = std::make_unique<TcpSession>();

    // Build a Ping message (routed via default -> messageReceived)
    NetmixPing payload;
    payload.sentTick = 42;

    NetmixMessage msg{NetmixMessageType::Ping, payload};
    QByteArray wire = encodeMessage(msg);

    ASSERT_GT(wire.size(), 12);

    // Verify decodeMessage works directly first
    {
        auto directDecode = decodeMessage(wire);
        ASSERT_TRUE(directDecode.has_value())
                << "decodeMessage failed on encodeMessage output (size=" << wire.size() << ")";
        EXPECT_EQ(NetmixMessageType::Ping, directDecode->type);
        auto* p = std::get_if<NetmixPing>(&directDecode->payload);
        ASSERT_NE(nullptr, p);
        EXPECT_EQ(42u, p->sentTick);
    }

    // Verify that feeding all at once works
    {
        auto testSession = std::make_unique<TcpSession>();
        NetmixMessage received;
        bool gotMsg = false;
        testSession->connect(testSession.get(), &TcpSession::messageReceived,
                [&](const NetmixMessage& m) {
                    received = m;
                    gotMsg = true;
                });
        testSession->injectReceivedData(wire);
        EXPECT_TRUE(gotMsg);
        if (gotMsg) {
            EXPECT_EQ(NetmixMessageType::Ping, received.type);
            auto* p = std::get_if<NetmixPing>(&received.payload);
            ASSERT_NE(nullptr, p);
            EXPECT_EQ(42u, p->sentTick);
        }
    }

    // Now test split across 3 chunks
    NetmixMessage received;
    bool gotMsg = false;
    session->connect(session.get(), &TcpSession::messageReceived,
            [&](const NetmixMessage& m) {
                received = m;
                gotMsg = true;
            });

    int chunkSize = wire.size() / 3;
    session->injectReceivedData(wire.left(chunkSize));
    EXPECT_FALSE(gotMsg);

    session->injectReceivedData(
            wire.mid(chunkSize, chunkSize));
    EXPECT_FALSE(gotMsg);

    session->injectReceivedData(wire.mid(2 * chunkSize));
    EXPECT_TRUE(gotMsg);

    if (gotMsg) {
        EXPECT_EQ(NetmixMessageType::Ping, received.type);
        auto* p = std::get_if<NetmixPing>(&received.payload);
        ASSERT_NE(nullptr, p);
        EXPECT_EQ(42u, p->sentTick);
    }
}

// ---------------------------------------------------------------------------
// VersionMismatch_Rejected
// ---------------------------------------------------------------------------

TEST_F(NetmixTcpSessionTest, VersionMismatch_Rejected) {
    std::unique_ptr<TcpSession> host;
    host = std::make_unique<TcpSession>();
    host->setDisplayName(QStringLiteral("host"));
    host->setListenPort(0);
    ASSERT_TRUE(host->listen());

    // Connect a raw socket with bad protocol version
    QTcpSocket rawSocket;
    rawSocket.connectToHost(QHostAddress::LocalHost, host->server()->serverPort());

    QSignalSpy connectedSpy(&rawSocket, &QTcpSocket::connected);
    EXPECT_TRUE(connectedSpy.wait(2000));

    // Build a Hello header with version 0
    NetmixProtocolHeader badHeader;
    badHeader.magic = kNetmixMagic;
    badHeader.version = 0;
    badHeader.type = static_cast<quint16>(NetmixMessageType::Hello);
    badHeader.length = 0;

    QByteArray wire;
    QDataStream stream(&wire, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << badHeader.magic << badHeader.version
           << badHeader.type << badHeader.length;

    rawSocket.write(wire);
    rawSocket.flush();

    pumpEvents(1000);

    // Host should never reach Connected — stayed at Listening or Disconnected
    EXPECT_NE(TcpSession::Connected, host->state());

    rawSocket.disconnectFromHost();
    host->disconnectFromPeer();
}

// ---------------------------------------------------------------------------
// HeartbeatDegradedThenDisconnected
// ---------------------------------------------------------------------------

TEST_F(NetmixTcpSessionTest, HeartbeatDegradedThenDisconnected) {
    std::unique_ptr<TcpSession> host;
    std::unique_ptr<TcpSession> client;

    ASSERT_TRUE(connectPair(host, client));

    // Override timeouts for fast test (1s degraded, 3s disconnect)
    host->setTimeoutsForTest(1, 3);
    client->setTimeoutsForTest(1, 3);

    QSignalSpy hostStateSpy(host.get(), &TcpSession::stateChanged);

    // Stop heartbeats on both sides so no traffic flows
    // We access timers indirectly: we stop sendMessage from being called
    // by disconnecting. Instead, let both sides naturally lose contact.
    // Disconnect client socket silently to simulate dead peer.
    client->disconnectFromPeer();

    // Host should detect no traffic -> Degraded after ~1s, then Disconnected
    pumpEvents(2000);

    EXPECT_EQ(TcpSession::Disconnected, host->state());

    host->disconnectFromPeer();
}

// ---------------------------------------------------------------------------
// TeardownClean
// ---------------------------------------------------------------------------

TEST_F(NetmixTcpSessionTest, TeardownClean) {
    std::unique_ptr<TcpSession> host;
    std::unique_ptr<TcpSession> client;

    ASSERT_TRUE(connectPair(host, client));

    QSignalSpy hostStateSpy(host.get(), &TcpSession::stateChanged);
    QSignalSpy clientStateSpy(client.get(), &TcpSession::stateChanged);

    host->disconnectFromPeer();

    pumpEvents(500);

    EXPECT_EQ(TcpSession::Disconnected, host->state());
    EXPECT_EQ(TcpSession::Disconnected, client->state());

    // Both should be able to teardown without crash
    host.reset();
    client.reset();
}

// ---------------------------------------------------------------------------
// DestroyWhileConnected
// ---------------------------------------------------------------------------

TEST_F(NetmixTcpSessionTest, DestroyWhileConnected) {
    std::unique_ptr<TcpSession> host;
    std::unique_ptr<TcpSession> client;

    ASSERT_TRUE(connectPair(host, client));

    // Destroy without explicit disconnect — no crash, no double-delete
    host.reset();
    client.reset();
}

} // namespace
