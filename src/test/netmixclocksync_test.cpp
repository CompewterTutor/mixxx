#include <gtest/gtest.h>

#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTimer>

#include "netmix/clocksync.h"
#include "netmix/sessionclock.h"
#include "test/mixxxtest.h"

namespace {

class NetmixClockSyncTest : public MixxxTest {
  protected:
    static constexpr int kFramesPerTick = 200;
    static constexpr int kSampleRate = 48000;

    SessionClock m_clockA;
    SessionClock m_clockB;
    ClockSync m_syncA;
    ClockSync m_syncB;

    NetmixClockSyncTest()
            : m_clockA(nullptr),
              m_clockB(nullptr),
              m_syncA(&m_clockA, nullptr),
              m_syncB(&m_clockB, nullptr) {
    }

    void SetUp() override {
        m_syncA.startTestMode();
        m_syncB.startTestMode();
    }

    void TearDown() override {
        m_syncA.stop();
        m_syncB.stop();
    }

    void advanceBothClocks(int ticks) {
        int frames = ticks * kFramesPerTick;
        m_clockA.onFramesProcessed(frames, kSampleRate);
        m_clockB.onFramesProcessed(frames, kSampleRate);
    }

    void wireSyncs() {
        QObject::connect(&m_syncA, &ClockSync::outgoingMessage,
                [this](const NetmixMessage& msg) {
                    m_syncB.injectMessage(msg);
                });
        QObject::connect(&m_syncB, &ClockSync::outgoingMessage,
                [this](const NetmixMessage& msg) {
                    m_syncA.injectMessage(msg);
                });
    }

    void wireSyncsWithDelay(int delayMs) {
        QObject::connect(&m_syncA, &ClockSync::outgoingMessage,
                [this, delayMs](const NetmixMessage& msg) {
                    QTimer::singleShot(delayMs,
                            [this, msg]() { m_syncB.injectMessage(msg); });
                });
        QObject::connect(&m_syncB, &ClockSync::outgoingMessage,
                [this, delayMs](const NetmixMessage& msg) {
                    QTimer::singleShot(delayMs,
                            [this, msg]() { m_syncA.injectMessage(msg); });
                });
    }

    void wireSyncsWithJitter(int delayA, int delayB) {
        auto counterA = std::make_shared<int>(0);
        QObject::connect(&m_syncA, &ClockSync::outgoingMessage,
                [this, delayA, delayB, counterA](const NetmixMessage& msg) {
                    int d = ((*counterA)++ % 2 == 0) ? delayA : delayB;
                    QTimer::singleShot(d,
                            [this, msg]() { m_syncB.injectMessage(msg); });
                });
        auto counterB = std::make_shared<int>(0);
        QObject::connect(&m_syncB, &ClockSync::outgoingMessage,
                [this, delayA, delayB, counterB](const NetmixMessage& msg) {
                    int d = ((*counterB)++ % 2 == 0) ? delayA : delayB;
                    QTimer::singleShot(d,
                            [this, msg]() { m_syncA.injectMessage(msg); });
                });
    }

    void disconnectWires() {
        QObject::disconnect(&m_syncA, &ClockSync::outgoingMessage,
                nullptr, nullptr);
        QObject::disconnect(&m_syncB, &ClockSync::outgoingMessage,
                nullptr, nullptr);
    }

    static void pumpEvents(int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(
                    QEventLoop::AllEvents, 5);
        }
    }

    static void waitForPong(ClockSync& sync, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (!sync.hasPendingPong()) {
                return;
            }
            QCoreApplication::processEvents(
                    QEventLoop::AllEvents, 5);
        }
    }
};

// ---------------------------------------------------------------------------
// Convergence_ZeroLatency
// ---------------------------------------------------------------------------

TEST_F(NetmixClockSyncTest, Convergence_ZeroLatency) {
    wireSyncs();
    advanceBothClocks(100);

    for (int i = 0; i < 20; i++) {
        m_syncA.sendPingNow();
        pumpEvents(50);
        waitForPong(m_syncA, 1000);
    }

    EXPECT_NEAR(0, m_syncA.currentOffset(), 1);
    EXPECT_NEAR(0, m_syncB.currentOffset(), 1);
}

// ---------------------------------------------------------------------------
// Convergence_SymmetricLatency
// ---------------------------------------------------------------------------

TEST_F(NetmixClockSyncTest, Convergence_SymmetricLatency) {
    wireSyncsWithDelay(10);
    advanceBothClocks(100);

    for (int i = 0; i < 32; i++) {
        m_syncA.sendPingNow();
        pumpEvents(60);
    }

    EXPECT_NEAR(0, m_syncA.currentOffset(), 1);
}

// ---------------------------------------------------------------------------
// Convergence_WithJitter
// ---------------------------------------------------------------------------

TEST_F(NetmixClockSyncTest, Convergence_WithJitter) {
    wireSyncsWithJitter(5, 25);
    advanceBothClocks(100);

    for (int i = 0; i < 32; i++) {
        m_syncA.sendPingNow();
        pumpEvents(80);
    }

    EXPECT_NEAR(0, m_syncA.currentOffset(), 2);
}

// ---------------------------------------------------------------------------
// OutlierRejection
// ---------------------------------------------------------------------------

TEST_F(NetmixClockSyncTest, OutlierRejection) {
    wireSyncs();
    advanceBothClocks(100);

    // Warm up median filter with 16 normal exchanges
    for (int i = 0; i < 16; i++) {
        m_syncA.sendPingNow();
        pumpEvents(50);
    }

    qint32 offsetBefore = m_syncA.currentOffset();

    // Perform one more exchange to set m_lastSentTick
    m_syncA.sendPingNow();
    pumpEvents(50);

    // Inject a spike Pong with extreme remoteTick.
    // m_lastSentTick inside ClockSync equals agreedTick at sendPingNow time.
    // After the synchronous exchange completes and offset ≈ 0,
    // agreedTick hasn't changed, so we can read it now.
    quint32 matchingTick = m_clockA.agreedTick();
    NetmixPong spike;
    spike.sentTick = matchingTick;
    spike.remoteTick = matchingTick + 500; // Simulates 500-tick offset spike
    NetmixMessage spikeMsg;
    spikeMsg.type = NetmixMessageType::Pong;
    spikeMsg.payload = spike;
    m_syncA.injectMessage(spikeMsg);
    pumpEvents(50);

    // Median filter should reject the spike — offset should remain near previous value
    EXPECT_NEAR(offsetBefore, m_syncA.currentOffset(), 2);
}

// ---------------------------------------------------------------------------
// InitialOffset_HelloAck
// ---------------------------------------------------------------------------

TEST_F(NetmixClockSyncTest, InitialOffset_HelloAck) {
    // Both clocks at tick 0
    m_syncA.setInitialOffset(500, 200);
    EXPECT_EQ(m_clockA.agreedTick(), 300u);

    // After advancing, agreedTick reflects offset
    m_clockA.onFramesProcessed(kFramesPerTick, kSampleRate);
    EXPECT_EQ(m_clockA.agreedTick(), 301u);
}

// ---------------------------------------------------------------------------
// RttUpdate
// ---------------------------------------------------------------------------

TEST_F(NetmixClockSyncTest, RttUpdate) {
    wireSyncsWithDelay(15);
    advanceBothClocks(100);

    m_syncA.sendPingNow();
    pumpEvents(200);
    waitForPong(m_syncA, 2000);

    double rtt = m_syncA.smoothedRttMs();
    EXPECT_NEAR(30.0, rtt, 10.0);
}

} // namespace
