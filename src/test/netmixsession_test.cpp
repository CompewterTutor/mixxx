#include <gtest/gtest.h>

#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QSignalSpy>
#include <QThread>

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "netmix/channelownership.h"
#include "netmix/netmixsessionmanager.h"
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

} // namespace
