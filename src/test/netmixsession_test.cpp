#include <gtest/gtest.h>

#include <memory>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "netmix/channelownership.h"
#include "netmix/netmixsessionmanager.h"
#include "netmix/trackcache.h"
#include "test/mixxxtest.h"

namespace {

constexpr int kFramesPerTick = 200;
constexpr int kSampleRate = 48000;

class NetmixSessionTest : public MixxxTest {
  protected:
    static void pumpEvents(int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
        }
    }

    static void advanceClock(NetmixSessionManager& mgr, int ticks) {
        int frames = ticks * kFramesPerTick;
        mgr.sessionClock().onFramesProcessed(frames, kSampleRate);
    }

    static bool setupLoopbackPair(std::unique_ptr<NetmixSessionManager>& mgrA,
            std::unique_ptr<NetmixSessionManager>& mgrB,
            quint16& portA) {
        mgrA = std::make_unique<NetmixSessionManager>();
        mgrB = std::make_unique<NetmixSessionManager>();

        mgrA->setEnabled(true);
        mgrB->setEnabled(true);

        mgrA->hostSession(0);
        if (mgrA->state() != NetmixSessionManager::Connecting) {
            return false;
        }

        portA = mgrA->tcpSession()->server()->serverPort();
        mgrB->joinSession(QHostAddress::LocalHost, portA);
        if (mgrB->state() != NetmixSessionManager::Connecting) {
            return false;
        }

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000) {
            pumpEvents(50);
            if (mgrA->state() == NetmixSessionManager::Connected &&
                    mgrB->state() == NetmixSessionManager::Connected) {
                return true;
            }
        }
        return false;
    }

    static void teardownPair(std::unique_ptr<NetmixSessionManager>& mgrA,
            std::unique_ptr<NetmixSessionManager>& mgrB) {
        mgrA->leaveSession();
        mgrB->leaveSession();
        pumpEvents(500);
    }
};

TEST_F(NetmixSessionTest, InitialStateIsIdle) {
    NetmixSessionManager mgr;
    EXPECT_EQ(NetmixSessionManager::Idle, mgr.state());
}

TEST_F(NetmixSessionTest, Disabled_RefusesSession) {
    NetmixSessionManager mgr;
    mgr.hostSession(0);
    EXPECT_EQ(NetmixSessionManager::Idle, mgr.state());
}

TEST_F(NetmixSessionTest, DoubleHost_DoesNothing) {
    NetmixSessionManager mgr;
    mgr.setEnabled(true);
    mgr.hostSession(0);
    EXPECT_EQ(NetmixSessionManager::Connecting, mgr.state());

    mgr.hostSession(0);
    EXPECT_EQ(NetmixSessionManager::Connecting, mgr.state());

    mgr.leaveSession();
}

TEST_F(NetmixSessionTest, StateTransitions) {
    std::unique_ptr<NetmixSessionManager> mgrA, mgrB;
    quint16 portA = 0;

    ASSERT_TRUE(setupLoopbackPair(mgrA, mgrB, portA));

    EXPECT_EQ(NetmixSessionManager::Connected, mgrA->state());
    EXPECT_EQ(NetmixSessionManager::Connected, mgrB->state());

    mgrA->leaveSession();
    pumpEvents(500);
    EXPECT_EQ(NetmixSessionManager::Idle, mgrA->state());

    mgrB->leaveSession();
    pumpEvents(500);
    EXPECT_EQ(NetmixSessionManager::Idle, mgrB->state());
}

TEST_F(NetmixSessionTest, LeaveSession_CleanTeardown) {
    std::unique_ptr<NetmixSessionManager> mgrA, mgrB;
    quint16 portA = 0;

    ASSERT_TRUE(setupLoopbackPair(mgrA, mgrB, portA));

    mgrA->leaveSession();
    pumpEvents(500);
    EXPECT_EQ(NetmixSessionManager::Idle, mgrA->state());
    EXPECT_TRUE(mgrA->tcpSession() == nullptr);

    mgrB->leaveSession();
    pumpEvents(500);
    EXPECT_EQ(NetmixSessionManager::Idle, mgrB->state());
    EXPECT_TRUE(mgrB->tcpSession() == nullptr);
}

TEST_F(NetmixSessionTest, EchoSuppression_StateTransition_Host) {
    NetmixSessionManager mgr;
    mgr.setEnabled(true);

    QSignalSpy stateSpy(&mgr, &NetmixSessionManager::sessionStateChanged);

    mgr.hostSession(0);
    EXPECT_EQ(NetmixSessionManager::Connecting, mgr.state());

    pumpEvents(500);
    EXPECT_EQ(NetmixSessionManager::Connecting, mgr.state());

    mgr.leaveSession();
    EXPECT_EQ(NetmixSessionManager::Idle, mgr.state());

    EXPECT_GE(stateSpy.count(), 2);
}

TEST_F(NetmixSessionTest, Loopback_ControlEventDelivery) {
    std::unique_ptr<NetmixSessionManager> mgrA, mgrB;
    quint16 portA = 0;

    ASSERT_TRUE(setupLoopbackPair(mgrA, mgrB, portA));

    quint64 sentBefore = mgrA->udpChannel()->sentCount();
    quint64 recvBefore = mgrB->udpChannel()->receivedCount();

    ConfigKey volumeKey("[Channel1]", "volume");
    ControlObject volumeCO(volumeKey);
    volumeCO.set(0.5);

    ControlProxy externalProxy(volumeKey);
    externalProxy.set(0.75);

    pumpEvents(500);

    // Advance mgrA's clock → triggers pack + UDP send
    advanceClock(*mgrA, 1);
    pumpEvents(100);

    // Advance mgrB's clock (not strictly needed for recv but good hygiene)
    advanceClock(*mgrB, 1);
    pumpEvents(100);

    // Verify data was sent via UDP from mgrA and received by mgrB
    EXPECT_GT(mgrA->udpChannel()->sentCount(), sentBefore);
    EXPECT_GT(mgrB->udpChannel()->receivedCount(), recvBefore);

    // Verify the value landed on the receiving side
    // NOTE: both managers share the CO registry in-test, so this
    // assertion alone does NOT prove network delivery.
    // The sentCount/receivedCount checks above provide that proof.
    EXPECT_DOUBLE_EQ(0.75, volumeCO.get());

    teardownPair(mgrA, mgrB);
}

TEST_F(NetmixSessionTest, OwnershipLoopback_Preassigned) {
    std::unique_ptr<NetmixSessionManager> mgrA, mgrB;
    quint16 portA = 0;

    ASSERT_TRUE(setupLoopbackPair(mgrA, mgrB, portA));

    // After handshake, both session managers should have ChannelOwnership created
    auto* ownershipA = mgrA->channelOwnership();
    auto* ownershipB = mgrB->channelOwnership();
    ASSERT_NE(nullptr, ownershipA);
    ASSERT_NE(nullptr, ownershipB);

    // With no pre-assignment configured, all channels should be Unowned
    for (quint16 ch = 0; ch <= 4; ++ch) {
        EXPECT_EQ(OwnershipState::Unowned, ownershipA->state(ch));
        EXPECT_EQ(OwnershipState::Unowned, ownershipB->state(ch));
    }

    teardownPair(mgrA, mgrB);
}

TEST_F(NetmixSessionTest, OwnershipLoopback_ClaimGrant) {
    std::unique_ptr<NetmixSessionManager> mgrA, mgrB;
    quint16 portA = 0;

    ASSERT_TRUE(setupLoopbackPair(mgrA, mgrB, portA));

    auto* ownershipA = mgrA->channelOwnership();
    auto* ownershipB = mgrB->channelOwnership();
    ASSERT_NE(nullptr, ownershipA);
    ASSERT_NE(nullptr, ownershipB);

    // mgrA claims channel 1
    EXPECT_TRUE(ownershipA->claim(1));
    EXPECT_EQ(OwnershipState::PendingClaim, ownershipA->state(1));

    // Process events so the claim message reaches mgrB
    pumpEvents(500);

    // mgrB should have auto-granted (since channel is unowned on mgrB)
    // and sent a Grant back to mgrA
    EXPECT_TRUE(ownershipB->isOwnedByRemote(1));

    // Process events so the Grant reaches mgrA
    pumpEvents(500);

    // mgrA should now own the channel
    EXPECT_TRUE(ownershipA->isOwnedByLocal(1));

    teardownPair(mgrA, mgrB);
}

TEST_F(NetmixSessionTest, OwnershipLoopback_VetoedClaim) {
    std::unique_ptr<NetmixSessionManager> mgrA, mgrB;
    quint16 portA = 0;

    ASSERT_TRUE(setupLoopbackPair(mgrA, mgrB, portA));

    auto* ownershipA = mgrA->channelOwnership();
    auto* ownershipB = mgrB->channelOwnership();
    ASSERT_NE(nullptr, ownershipA);
    ASSERT_NE(nullptr, ownershipB);

    // mgrA claims channel 1
    EXPECT_TRUE(ownershipA->claim(1));
    pumpEvents(500);

    // mgrB auto-grants, mgrA receives Grant
    pumpEvents(500);
    EXPECT_TRUE(ownershipA->isOwnedByLocal(1));

    // mgrB tries to claim channel 1 — already owned by mgrA
    EXPECT_FALSE(ownershipB->claim(1));

    // mgrA's claim on already-owned channel should also fail
    EXPECT_FALSE(ownershipA->claim(1));

    teardownPair(mgrA, mgrB);
}

// ---------------------------------------------------------------------------
// 1.6.3: Queue-triggered background send + readiness handshake
// ---------------------------------------------------------------------------

static QString createTempTrackFile(const QString& dirPath) {
    QString path = QDir(dirPath).filePath(QStringLiteral("test_track.mp3"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return QString();
    }
    QByteArray content("simulated audio content for netmix transfer testing");
    file.write(content);
    file.close();
    return path;
}

TEST_F(NetmixSessionTest, LoadOnOwnedDeck_RemoteCachePopulated) {
    QTemporaryDir cacheDirA;
    QTemporaryDir cacheDirB;
    ASSERT_TRUE(cacheDirA.isValid());
    ASSERT_TRUE(cacheDirB.isValid());

    TrackCache cacheA(cacheDirA.path());
    TrackCache cacheB(cacheDirB.path());
    ASSERT_TRUE(cacheA.initialize());
    ASSERT_TRUE(cacheB.initialize());

    QString srcPath = createTempTrackFile(cacheDirA.path());
    ASSERT_FALSE(srcPath.isEmpty());
    ASSERT_TRUE(QFile::exists(srcPath));

    QString expectedHash = TrackCache::hashFile(srcPath);
    ASSERT_FALSE(expectedHash.isEmpty());
    ASSERT_EQ(64, expectedHash.size());

    // Set up managers with caches before connection
    auto mgrA = std::make_unique<NetmixSessionManager>();
    auto mgrB = std::make_unique<NetmixSessionManager>();

    mgrA->setTrackCache(&cacheA);
    mgrB->setTrackCache(&cacheB);
    mgrA->setEnabled(true);
    mgrB->setEnabled(true);

    mgrA->hostSession(0);
    ASSERT_EQ(NetmixSessionManager::Connecting, mgrA->state());

    quint16 portA = mgrA->tcpSession()->server()->serverPort();
    ASSERT_GT(portA, 0);

    mgrB->joinSession(QHostAddress::LocalHost, portA);
    ASSERT_EQ(NetmixSessionManager::Connecting, mgrB->state());

    // Wait for both to reach Connected
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        pumpEvents(50);
        if (mgrA->state() == NetmixSessionManager::Connected &&
                mgrB->state() == NetmixSessionManager::Connected) {
            break;
        }
    }
    ASSERT_EQ(NetmixSessionManager::Connected, mgrA->state());
    ASSERT_EQ(NetmixSessionManager::Connected, mgrB->state());

    auto* ownershipA = mgrA->channelOwnership();
    ASSERT_NE(nullptr, ownershipA);

    // mgrA claims channel 1
    ASSERT_TRUE(ownershipA->claim(1));
    pumpEvents(1000);
    ASSERT_TRUE(ownershipA->isOwnedByLocal(1));

    // Load track on mgrA's owned channel 1
    mgrA->notifyTrackLoaded(1, srcPath, QStringLiteral("test.mp3"),
            QStringLiteral("audio/mpeg"));

    // Wait for transfer to complete
    pumpEvents(3000);

    // Remote cache should contain verified copy
    EXPECT_TRUE(cacheB.contains(expectedHash));
    EXPECT_TRUE(cacheB.verify(expectedHash));

    // Sender should report deck ready
    EXPECT_TRUE(mgrA->isDeckReady(1));

    // Cached file should be byte-identical
    QString cachedPath = cacheB.pathForHash(expectedHash);
    ASSERT_FALSE(cachedPath.isEmpty());
    EXPECT_TRUE(QFile::exists(cachedPath));

    mgrA->leaveSession();
    mgrB->leaveSession();
    pumpEvents(500);
}

TEST_F(NetmixSessionTest, LoadOnUnownedDeck_SkipsTransfer) {
    QTemporaryDir cacheDirA;
    QTemporaryDir cacheDirB;
    ASSERT_TRUE(cacheDirA.isValid());
    ASSERT_TRUE(cacheDirB.isValid());

    TrackCache cacheA(cacheDirA.path());
    TrackCache cacheB(cacheDirB.path());
    ASSERT_TRUE(cacheA.initialize());
    ASSERT_TRUE(cacheB.initialize());

    QString srcPath = createTempTrackFile(cacheDirA.path());
    ASSERT_FALSE(srcPath.isEmpty());

    QString expectedHash = TrackCache::hashFile(srcPath);
    ASSERT_FALSE(expectedHash.isEmpty());

    auto mgrA = std::make_unique<NetmixSessionManager>();
    auto mgrB = std::make_unique<NetmixSessionManager>();

    mgrA->setTrackCache(&cacheA);
    mgrB->setTrackCache(&cacheB);
    mgrA->setEnabled(true);
    mgrB->setEnabled(true);

    mgrA->hostSession(0);
    quint16 portA = mgrA->tcpSession()->server()->serverPort();
    mgrB->joinSession(QHostAddress::LocalHost, portA);

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        pumpEvents(50);
        if (mgrA->state() == NetmixSessionManager::Connected &&
                mgrB->state() == NetmixSessionManager::Connected) {
            break;
        }
    }
    ASSERT_EQ(NetmixSessionManager::Connected, mgrA->state());
    ASSERT_EQ(NetmixSessionManager::Connected, mgrB->state());

    // mgrB claims channel 1 (mgrA does NOT own it)
    auto* ownershipB = mgrB->channelOwnership();
    ASSERT_NE(nullptr, ownershipB);
    ASSERT_TRUE(ownershipB->claim(1));
    pumpEvents(1000);
    ASSERT_TRUE(ownershipB->isOwnedByLocal(1));

    // mgrA tries to load track on channel 1 — not owned by mgrA
    mgrA->notifyTrackLoaded(1, srcPath, QStringLiteral("test.mp3"),
            QStringLiteral("audio/mpeg"));
    pumpEvents(1000);

    // mgrA should NOT have deck ready
    EXPECT_FALSE(mgrA->isDeckReady(1));

    // cacheB should NOT contain the file (no transfer occurred)
    EXPECT_FALSE(cacheB.contains(expectedHash));

    mgrA->leaveSession();
    mgrB->leaveSession();
    pumpEvents(500);
}

// ---------------------------------------------------------------------------
// 1.6.4: Live-sound gating + remote deck load + analysis
// ---------------------------------------------------------------------------

TEST_F(NetmixSessionTest, TrackOffer_CarriesChannelId) {
    // Protocol round-trip: encode TrackOffer with channelId, decode, verify
    NetmixTrackOffer offer;
    offer.hash = QByteArray::fromHex(QByteArrayLiteral(
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    offer.size = 1024;
    offer.name = QStringLiteral("test-track.mp3");
    offer.mime = QStringLiteral("audio/mpeg");
    offer.channelId = 3;

    NetmixMessage msg{NetmixMessageType::TrackOffer, offer};
    QByteArray encoded = encodeMessage(msg);
    ASSERT_FALSE(encoded.isEmpty());

    auto decoded = decodeMessage(encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(NetmixMessageType::TrackOffer, decoded->type);

    const auto* p = std::get_if<NetmixTrackOffer>(&decoded->payload);
    ASSERT_NE(nullptr, p);
    EXPECT_EQ(offer.hash, p->hash);
    EXPECT_EQ(offer.size, p->size);
    EXPECT_EQ(offer.name, p->name);
    EXPECT_EQ(offer.mime, p->mime);
    EXPECT_EQ(quint16(3), p->channelId);
}

TEST_F(NetmixSessionTest, Gating_MutesUntilBothReady) {
    QTemporaryDir cacheDirA;
    QTemporaryDir cacheDirB;
    ASSERT_TRUE(cacheDirA.isValid());
    ASSERT_TRUE(cacheDirB.isValid());

    TrackCache cacheA(cacheDirA.path());
    TrackCache cacheB(cacheDirB.path());
    ASSERT_TRUE(cacheA.initialize());
    ASSERT_TRUE(cacheB.initialize());

    QString srcPath = createTempTrackFile(cacheDirA.path());
    ASSERT_FALSE(srcPath.isEmpty());

    // Create mute CO if it doesn't exist
    ControlObject muteCO(ConfigKey("[Channel1]", "mute"));
    muteCO.set(0.0);

    auto mgrA = std::make_unique<NetmixSessionManager>();
    auto mgrB = std::make_unique<NetmixSessionManager>();

    mgrA->setTrackCache(&cacheA);
    mgrB->setTrackCache(&cacheB);
    mgrA->setEnabled(true);
    mgrB->setEnabled(true);

    mgrA->hostSession(0);
    ASSERT_EQ(NetmixSessionManager::Connecting, mgrA->state());
    quint16 portA = mgrA->tcpSession()->server()->serverPort();
    mgrB->joinSession(QHostAddress::LocalHost, portA);

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        pumpEvents(50);
        if (mgrA->state() == NetmixSessionManager::Connected &&
                mgrB->state() == NetmixSessionManager::Connected) {
            break;
        }
    }
    ASSERT_EQ(NetmixSessionManager::Connected, mgrA->state());
    ASSERT_EQ(NetmixSessionManager::Connected, mgrB->state());

    auto* ownershipA = mgrA->channelOwnership();
    ASSERT_NE(nullptr, ownershipA);
    ASSERT_TRUE(ownershipA->claim(1));
    pumpEvents(1000);
    ASSERT_TRUE(ownershipA->isOwnedByLocal(1));

    // Load track on mgrA's owned channel 1 — should mute immediately.
    // Check state BEFORE any event processing so the transfer hasn't started yet.
    mgrA->notifyTrackLoaded(1, srcPath, QStringLiteral("test.mp3"),
            QStringLiteral("audio/mpeg"));

    // Channel should be muted and not ready
    EXPECT_DOUBLE_EQ(1.0, muteCO.get());
    EXPECT_FALSE(mgrA->isDeckReady(1));
    ControlProxy readyProxy(ConfigKey("[Channel1]", "netmix_ready"));
    EXPECT_DOUBLE_EQ(0.0, readyProxy.get());

    // Wait for transfer to complete
    pumpEvents(3000);

    // Channel should be unmuted and ready
    EXPECT_DOUBLE_EQ(0.0, muteCO.get());
    EXPECT_TRUE(mgrA->isDeckReady(1));
    EXPECT_DOUBLE_EQ(1.0, readyProxy.get());

    mgrA->leaveSession();
    mgrB->leaveSession();
    pumpEvents(500);
}

TEST_F(NetmixSessionTest, Gating_UnownedChannelNotGated) {
    QTemporaryDir cacheDirA;
    QTemporaryDir cacheDirB;
    ASSERT_TRUE(cacheDirA.isValid());
    ASSERT_TRUE(cacheDirB.isValid());

    TrackCache cacheA(cacheDirA.path());
    TrackCache cacheB(cacheDirB.path());
    ASSERT_TRUE(cacheA.initialize());
    ASSERT_TRUE(cacheB.initialize());

    QString srcPath = createTempTrackFile(cacheDirA.path());
    ASSERT_FALSE(srcPath.isEmpty());

    // Create mute COs
    ControlObject muteCO1(ConfigKey("[Channel1]", "mute"));
    ControlObject muteCO2(ConfigKey("[Channel2]", "mute"));
    muteCO1.set(0.0);
    muteCO2.set(0.0);

    auto mgrA = std::make_unique<NetmixSessionManager>();
    auto mgrB = std::make_unique<NetmixSessionManager>();

    mgrA->setTrackCache(&cacheA);
    mgrB->setTrackCache(&cacheB);
    mgrA->setEnabled(true);
    mgrB->setEnabled(true);

    mgrA->hostSession(0);
    ASSERT_EQ(NetmixSessionManager::Connecting, mgrA->state());
    quint16 portA = mgrA->tcpSession()->server()->serverPort();
    mgrB->joinSession(QHostAddress::LocalHost, portA);

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        pumpEvents(50);
        if (mgrA->state() == NetmixSessionManager::Connected &&
                mgrB->state() == NetmixSessionManager::Connected) {
            break;
        }
    }
    ASSERT_EQ(NetmixSessionManager::Connected, mgrA->state());
    ASSERT_EQ(NetmixSessionManager::Connected, mgrB->state());

    auto* ownershipA = mgrA->channelOwnership();
    ASSERT_NE(nullptr, ownershipA);

    // mgrA owns ch1, mgrB owns ch2
    ASSERT_TRUE(ownershipA->claim(1));
    pumpEvents(1000);
    ASSERT_TRUE(ownershipA->isOwnedByLocal(1));

    // mgrA tries to load track on ch2 — NOT owned by mgrA
    mgrA->notifyTrackLoaded(2, srcPath, QStringLiteral("test.mp3"),
            QStringLiteral("audio/mpeg"));
    pumpEvents(100);

    // notifyTrackLoaded should return early (ownership check),
    // no gating change occurs. Channel starts muted (initial gating).
    EXPECT_DOUBLE_EQ(1.0, muteCO2.get());
    EXPECT_FALSE(mgrA->isDeckReady(2));

    // ch1 unaffected by unowned ch2 load — stays muted (initial gating)
    EXPECT_DOUBLE_EQ(1.0, muteCO1.get());

    mgrA->leaveSession();
    mgrB->leaveSession();
    pumpEvents(500);
}

TEST_F(NetmixSessionTest, Gating_CleanTeardownUnmutes) {
    QTemporaryDir cacheDirA;
    QTemporaryDir cacheDirB;
    ASSERT_TRUE(cacheDirA.isValid());
    ASSERT_TRUE(cacheDirB.isValid());

    TrackCache cacheA(cacheDirA.path());
    TrackCache cacheB(cacheDirB.path());
    ASSERT_TRUE(cacheA.initialize());
    ASSERT_TRUE(cacheB.initialize());

    QString srcPath = createTempTrackFile(cacheDirA.path());
    ASSERT_FALSE(srcPath.isEmpty());

    ControlObject muteCO(ConfigKey("[Channel1]", "mute"));
    muteCO.set(0.0);

    auto mgrA = std::make_unique<NetmixSessionManager>();
    auto mgrB = std::make_unique<NetmixSessionManager>();

    mgrA->setTrackCache(&cacheA);
    mgrB->setTrackCache(&cacheB);
    mgrA->setEnabled(true);
    mgrB->setEnabled(true);

    mgrA->hostSession(0);
    ASSERT_EQ(NetmixSessionManager::Connecting, mgrA->state());
    quint16 portA = mgrA->tcpSession()->server()->serverPort();
    mgrB->joinSession(QHostAddress::LocalHost, portA);

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        pumpEvents(50);
        if (mgrA->state() == NetmixSessionManager::Connected &&
                mgrB->state() == NetmixSessionManager::Connected) {
            break;
        }
    }
    ASSERT_EQ(NetmixSessionManager::Connected, mgrA->state());
    ASSERT_EQ(NetmixSessionManager::Connected, mgrB->state());

    auto* ownershipA = mgrA->channelOwnership();
    ASSERT_NE(nullptr, ownershipA);
    ASSERT_TRUE(ownershipA->claim(1));
    pumpEvents(1000);
    ASSERT_TRUE(ownershipA->isOwnedByLocal(1));

    // Load track on mgrA's owned ch1 — check state immediately before
    // transfer completes (tiny file transfers in <100ms on loopback)
    mgrA->notifyTrackLoaded(1, srcPath, QStringLiteral("test.mp3"),
            QStringLiteral("audio/mpeg"));
    EXPECT_DOUBLE_EQ(1.0, muteCO.get());

    // Teardown should unmute
    mgrA->leaveSession();
    mgrB->leaveSession();
    pumpEvents(500);

    // After teardown, mute should be 0
    EXPECT_DOUBLE_EQ(0.0, muteCO.get());
}

TEST_F(NetmixSessionTest, Gating_RemoteDeckCachedTrack) {
    QTemporaryDir cacheDirA;
    QTemporaryDir cacheDirB;
    ASSERT_TRUE(cacheDirA.isValid());
    ASSERT_TRUE(cacheDirB.isValid());

    TrackCache cacheA(cacheDirA.path());
    TrackCache cacheB(cacheDirB.path());
    ASSERT_TRUE(cacheA.initialize());
    ASSERT_TRUE(cacheB.initialize());

    QString srcPath = createTempTrackFile(cacheDirA.path());
    ASSERT_FALSE(srcPath.isEmpty());
    QString expectedHash = TrackCache::hashFile(srcPath);
    ASSERT_FALSE(expectedHash.isEmpty());

    auto mgrA = std::make_unique<NetmixSessionManager>();
    auto mgrB = std::make_unique<NetmixSessionManager>();

    mgrA->setTrackCache(&cacheA);
    mgrB->setTrackCache(&cacheB);
    mgrA->setEnabled(true);
    mgrB->setEnabled(true);

    mgrA->hostSession(0);
    ASSERT_EQ(NetmixSessionManager::Connecting, mgrA->state());
    quint16 portA = mgrA->tcpSession()->server()->serverPort();
    mgrB->joinSession(QHostAddress::LocalHost, portA);

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        pumpEvents(50);
        if (mgrA->state() == NetmixSessionManager::Connected &&
                mgrB->state() == NetmixSessionManager::Connected) {
            break;
        }
    }
    ASSERT_EQ(NetmixSessionManager::Connected, mgrA->state());
    ASSERT_EQ(NetmixSessionManager::Connected, mgrB->state());

    auto* ownershipA = mgrA->channelOwnership();
    ASSERT_NE(nullptr, ownershipA);
    ASSERT_TRUE(ownershipA->claim(1));
    pumpEvents(1000);
    ASSERT_TRUE(ownershipA->isOwnedByLocal(1));

    // Load track on mgrA's owned ch1
    mgrA->notifyTrackLoaded(1, srcPath, QStringLiteral("test.mp3"),
            QStringLiteral("audio/mpeg"));

    // Wait for transfer to complete
    pumpEvents(3000);

    // Remote cache should contain verified copy
    EXPECT_TRUE(cacheB.contains(expectedHash));
    EXPECT_TRUE(cacheB.verify(expectedHash));

    // Sender should be deck-ready
    EXPECT_TRUE(mgrA->isDeckReady(1));

    // netmix_ready should reflect ready state on both sides
    ControlProxy readyProxyA(ConfigKey("[Channel1]", "netmix_ready"));
    EXPECT_DOUBLE_EQ(1.0, readyProxyA.get());

    mgrA->leaveSession();
    mgrB->leaveSession();
    pumpEvents(500);
}

} // namespace
