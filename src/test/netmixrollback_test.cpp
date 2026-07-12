#include <gtest/gtest.h>

#include <memory>

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "netmix/controlallowlist.h"
#include "netmix/controlapplier.h"
#include "netmix/controlcapture.h"
#include "netmix/inputbuffer.h"
#include "netmix/prediction.h"
#include "netmix/rollbackengine.h"
#include "netmix/sessionclock.h"
#include "test/mixxxtest.h"

namespace {

class NetmixRollbackTest : public MixxxTest {
  protected:
    using EventList = std::initializer_list<std::pair<quint16, double>>;

    void SetUp() override {
        new ControlObject(
                ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
        new ControlObject(
                ConfigKey(QStringLiteral("Master"), QStringLiteral("crossfader")));
        new ControlObject(
                ConfigKey(QStringLiteral("Channel1"), QStringLiteral("play")));

        m_pClock = std::make_unique<SessionClock>();
        m_pClock->onFramesProcessed(441, 44100);

        m_pCapture = std::make_unique<ControlCapture>();
        m_pApplier = std::make_unique<ControlApplier>();

        m_pCapture->start(m_pClock.get());
        m_pCapture->setMuted(true);
        m_pApplier->setProxies(m_pCapture->proxies());

        m_pBuffer = std::make_unique<InputBuffer>();
        m_pPrediction = std::make_unique<HoldLastPrediction>();
        m_pEngine = std::make_unique<RollbackEngine>();

        m_pEngine->setInputBuffer(m_pBuffer.get());
        m_pEngine->setPredictionStrategy(m_pPrediction.get());
        m_pEngine->setControlApplier(m_pApplier.get());
        m_pEngine->initialize();
    }

    void TearDown() override {
        m_pEngine.reset();
        m_pPrediction.reset();
        m_pBuffer.reset();
        m_pApplier.reset();
        m_pCapture->stop();
        m_pCapture.reset();
        m_pClock.reset();
    }

    static NetmixInputFrame makeFrame(
            quint32 tick, EventList events) {
        NetmixInputFrame frame;
        frame.baseTick = tick;
        for (auto& [wireId, value] : events) {
            frame.events.append({wireId, value});
        }
        return frame;
    }

    void advanceTick(quint32 tick, EventList predicted, EventList local) {
        for (auto& [wid, val] : predicted) {
            m_pApplier->apply(wid, val);
        }
        auto predFrame = makeFrame(tick, predicted);
        m_pBuffer->insertRemotePredicted(tick, predFrame);

        for (auto& [wid, val] : local) {
            m_pApplier->apply(wid, val);
        }
        auto localFrame = makeFrame(tick, local);
        m_pBuffer->insertLocal(tick, localFrame);

        m_pEngine->onTick(tick);
    }

    void insertRemoteConfirmed(quint32 tick, EventList events) {
        auto frame = makeFrame(tick, events);
        m_pBuffer->insertRemoteConfirmed(tick, frame);
    }

    std::unique_ptr<SessionClock> m_pClock;
    std::unique_ptr<ControlCapture> m_pCapture;
    std::unique_ptr<ControlApplier> m_pApplier;
    std::unique_ptr<InputBuffer> m_pBuffer;
    std::unique_ptr<HoldLastPrediction> m_pPrediction;
    std::unique_ptr<RollbackEngine> m_pEngine;
};

TEST_F(NetmixRollbackTest, NoDivergenceNoRollback) {
    auto widVol = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ASSERT_TRUE(widVol.has_value());

    ControlProxy proxyVol(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));

    EXPECT_DOUBLE_EQ(0.0, proxyVol.get());

    advanceTick(1, {{widVol.value(), 0.5}}, {});

    insertRemoteConfirmed(1, {{widVol.value(), 0.5}});

    advanceTick(2, {{widVol.value(), 0.5}}, {});

    EXPECT_FALSE(m_pBuffer->firstDivergentTick().has_value());
    EXPECT_EQ(0, m_pEngine->rollbackCount());
}

TEST_F(NetmixRollbackTest, LateFaderCorrection) {
    auto widVol = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ASSERT_TRUE(widVol.has_value());

    ControlProxy proxyVol(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));

    advanceTick(1, {{widVol.value(), 0.5}}, {});
    advanceTick(2, {{widVol.value(), 0.5}}, {});
    advanceTick(3, {{widVol.value(), 0.5}}, {});

    EXPECT_DOUBLE_EQ(0.5, proxyVol.get());

    insertRemoteConfirmed(1, {{widVol.value(), 0.8}});

    EXPECT_TRUE(m_pBuffer->firstDivergentTick().has_value());
    EXPECT_EQ(1u, m_pBuffer->firstDivergentTick().value());

    // Manual rollback trigger — avoid predicted apply that would cancel ramp
    auto pred4 = makeFrame(4, {{widVol.value(), 0.5}});
    m_pBuffer->insertRemotePredicted(4, pred4);
    m_pEngine->onTick(4);

    EXPECT_EQ(1, m_pEngine->rollbackCount());
    // Ramp 3 ticks (|0.8-0.0|*4.0 = 3), not yet advanced
    EXPECT_DOUBLE_EQ(0.0, proxyVol.get());

    // Ramp progresses over ticks 5-7
    m_pEngine->onTick(5);
    EXPECT_GT(proxyVol.get(), 0.0);
    EXPECT_LT(proxyVol.get(), 0.8);

    m_pEngine->onTick(6);
    EXPECT_GT(proxyVol.get(), 0.26);
    EXPECT_LT(proxyVol.get(), 0.55);

    m_pEngine->onTick(7);
    EXPECT_DOUBLE_EQ(0.8, proxyVol.get());
}

TEST_F(NetmixRollbackTest, CorrectLocalPreservedThroughRollback) {
    auto widVol = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    auto widXfade = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Master"), QStringLiteral("crossfader")));
    ASSERT_TRUE(widVol.has_value());
    ASSERT_TRUE(widXfade.has_value());

    ControlProxy proxyVol(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ControlProxy proxyXfade(
            ConfigKey(QStringLiteral("Master"), QStringLiteral("crossfader")));

    advanceTick(1, {{widVol.value(), 0.5}}, {{widXfade.value(), 0.6}});
    advanceTick(2, {{widVol.value(), 0.5}}, {});
    advanceTick(3, {{widVol.value(), 0.5}}, {});

    EXPECT_DOUBLE_EQ(0.5, proxyVol.get());
    EXPECT_DOUBLE_EQ(0.6, proxyXfade.get());

    insertRemoteConfirmed(1, {{widVol.value(), 0.3}});

    // Manual rollback trigger — avoid predicted apply that would cancel ramp
    auto pred4 = makeFrame(4, {{widVol.value(), 0.5}});
    m_pBuffer->insertRemotePredicted(4, pred4);
    m_pEngine->onTick(4);

    EXPECT_EQ(1, m_pEngine->rollbackCount());
    // Vol ramp 1 tick (|0.3-0.0|*4.0 = 1), not yet advanced
    EXPECT_DOUBLE_EQ(0.0, proxyVol.get());
    // Crossfader preserved via local event
    EXPECT_DOUBLE_EQ(0.6, proxyXfade.get());

    // One more tick completes the 1-tick ramp
    m_pEngine->onTick(5);
    EXPECT_DOUBLE_EQ(0.3, proxyVol.get());
    EXPECT_DOUBLE_EQ(0.6, proxyXfade.get());
}

TEST_F(NetmixRollbackTest, WindowExceededApplyForward) {
    auto widVol = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ASSERT_TRUE(widVol.has_value());

    ControlProxy proxyVol(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));

    int signalCount = 0;
    quint32 signaledTick = 0;
    QMetaObject::Connection conn = QObject::connect(
            m_pEngine.get(),
            &RollbackEngine::windowExceeded,
            [&](quint32 tick) {
                ++signalCount;
                signaledTick = tick;
            });

    m_pEngine->setWindowSize(3);
    m_pEngine->initialize();

    advanceTick(1, {{widVol.value(), 0.1}}, {});
    advanceTick(2, {{widVol.value(), 0.2}}, {});
    advanceTick(3, {{widVol.value(), 0.3}}, {});
    advanceTick(4, {{widVol.value(), 0.4}}, {});
    advanceTick(5, {{widVol.value(), 0.5}}, {});

    insertRemoteConfirmed(1, {{widVol.value(), 0.9}});

    advanceTick(6, {{widVol.value(), 0.5}}, {});

    EXPECT_EQ(1, signalCount);
    EXPECT_EQ(1u, signaledTick);
    EXPECT_EQ(0, m_pEngine->rollbackCount());
    EXPECT_EQ(1, m_pEngine->windowExceededCount());
    EXPECT_DOUBLE_EQ(0.9, proxyVol.get());

    QObject::disconnect(conn);
}

TEST_F(NetmixRollbackTest, MultipleTickReSim) {
    auto widVol = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    auto widPlay = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("play")));
    ASSERT_TRUE(widVol.has_value());
    ASSERT_TRUE(widPlay.has_value());

    ControlProxy proxyVol(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ControlProxy proxyPlay(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("play")));

    advanceTick(1, {{widVol.value(), 0.5}}, {});
    advanceTick(2, {{widVol.value(), 0.5}}, {});
    advanceTick(3, {{widVol.value(), 0.5}}, {});
    advanceTick(4, {{widVol.value(), 0.5}}, {});
    advanceTick(5, {{widVol.value(), 0.5}}, {});

    insertRemoteConfirmed(1, {{widVol.value(), 0.8}, {widPlay.value(), 1.0}});
    insertRemoteConfirmed(3, {{widVol.value(), 0.3}});

    // Manual rollback trigger — avoid predicted apply interference
    auto pred6 = makeFrame(6, {{widVol.value(), 0.5}});
    m_pBuffer->insertRemotePredicted(6, pred6);
    m_pEngine->onTick(6);

    EXPECT_EQ(1, m_pEngine->rollbackCount());
    // Vol ramp (second correction 0.0→0.3, |0.3|*4 = 1 tick), not yet advanced
    EXPECT_DOUBLE_EQ(0.0, proxyVol.get());
    // Play is discrete — snaps immediately
    EXPECT_DOUBLE_EQ(1.0, proxyPlay.get());

    m_pEngine->onTick(7);
    EXPECT_DOUBLE_EQ(0.3, proxyVol.get());
    EXPECT_DOUBLE_EQ(1.0, proxyPlay.get());
}

TEST_F(NetmixRollbackTest, NoReRollbackSameDivergence) {
    auto widVol = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ASSERT_TRUE(widVol.has_value());

    ControlProxy proxyVol(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));

    advanceTick(1, {{widVol.value(), 0.5}}, {});
    advanceTick(2, {{widVol.value(), 0.5}}, {});

    insertRemoteConfirmed(1, {{widVol.value(), 0.8}});

    // Manual rollback trigger — avoid predicted apply interference
    auto pred3 = makeFrame(3, {{widVol.value(), 0.5}});
    m_pBuffer->insertRemotePredicted(3, pred3);
    m_pEngine->onTick(3);

    EXPECT_EQ(1, m_pEngine->rollbackCount());
    // Ramp 3 ticks (0.0→0.8, |0.8|*4 = 3), not yet advanced
    EXPECT_DOUBLE_EQ(0.0, proxyVol.get());

    // Subsequent tick does NOT trigger another rollback
    m_pEngine->onTick(4);
    EXPECT_EQ(1, m_pEngine->rollbackCount());
    // Ramp progresses (not stuck at 0.0)
    EXPECT_GT(proxyVol.get(), 0.0);
}

// Ramp: continuous control ramps to target with no inter-tick jump > ramp step
TEST_F(NetmixRollbackTest, RampedFaderCorrection) {
    auto widVol = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ASSERT_TRUE(widVol.has_value());

    ControlProxy proxyVol(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));

    advanceTick(1, {{widVol.value(), 0.5}}, {});
    advanceTick(2, {{widVol.value(), 0.5}}, {});

    insertRemoteConfirmed(1, {{widVol.value(), 0.8}});

    // Trigger rollback
    auto pred3 = makeFrame(3, {{widVol.value(), 0.5}});
    m_pBuffer->insertRemotePredicted(3, pred3);
    m_pEngine->onTick(3);

    EXPECT_EQ(1, m_pEngine->rollbackCount());

    // Ramp: 0.0→0.8 over 3 ticks (|0.8|*4 = 3)
    // Verify continuity: no inter-tick jump > 0.8/3 ≈ 0.267
    double prev = proxyVol.get(); // 0.0
    for (quint32 t = 4; t <= 6; ++t) {
        m_pEngine->onTick(t);
        double cur = proxyVol.get();
        double step = cur - prev;
        EXPECT_LE(step, 0.27);
        EXPECT_GE(cur, prev);
        prev = cur;
    }
    EXPECT_DOUBLE_EQ(0.8, proxyVol.get());
}

// Ramp supersession: newer confirmed correction overrides older ramp target
TEST_F(NetmixRollbackTest, RampSupersessionByNewerCorrection) {
    auto widVol = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ASSERT_TRUE(widVol.has_value());

    ControlProxy proxyVol(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));

    advanceTick(1, {{widVol.value(), 0.5}}, {});
    advanceTick(2, {{widVol.value(), 0.5}}, {});

    // Two corrections: tick 1 = 0.8, tick 2 = 0.9 (newer supersedes)
    insertRemoteConfirmed(1, {{widVol.value(), 0.8}});
    insertRemoteConfirmed(2, {{widVol.value(), 0.9}});

    // Trigger rollback
    auto pred3 = makeFrame(3, {{widVol.value(), 0.5}});
    m_pBuffer->insertRemotePredicted(3, pred3);
    m_pEngine->onTick(3);

    EXPECT_EQ(1, m_pEngine->rollbackCount());
    EXPECT_DOUBLE_EQ(0.0, proxyVol.get());

    // Ramp target is 0.9 (supersedes 0.8), 3 ticks (|0.9|*4 = 3)
    // Trajectory monotonic toward 0.9, no dip toward 0.8
    double prev = proxyVol.get();
    for (quint32 t = 4; t <= 6; ++t) {
        m_pEngine->onTick(t);
        double cur = proxyVol.get();
        EXPECT_GE(cur, prev);
        prev = cur;
    }
    EXPECT_DOUBLE_EQ(0.9, proxyVol.get());
}

// Ramp supersession: fresh local input cancels ramp, snaps immediately
TEST_F(NetmixRollbackTest, RampSupersessionByFreshLocalInput) {
    auto widVol = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ASSERT_TRUE(widVol.has_value());

    ControlProxy proxyVol(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));

    advanceTick(1, {{widVol.value(), 0.5}}, {});
    advanceTick(2, {{widVol.value(), 0.5}}, {});

    insertRemoteConfirmed(1, {{widVol.value(), 0.8}});

    // Trigger rollback
    auto pred3 = makeFrame(3, {{widVol.value(), 0.5}});
    m_pBuffer->insertRemotePredicted(3, pred3);
    m_pEngine->onTick(3);

    EXPECT_EQ(1, m_pEngine->rollbackCount());
    EXPECT_DOUBLE_EQ(0.0, proxyVol.get());

    // Local input at tick 4: should cancel ramp and snap
    auto pred4 = makeFrame(4, {{widVol.value(), 0.5}});
    auto local4 = makeFrame(4, {{widVol.value(), 0.2}});
    m_pApplier->apply(widVol.value(), 0.2); // Local apply
    m_pBuffer->insertRemotePredicted(4, pred4);
    m_pBuffer->insertLocal(4, local4);
    m_pEngine->onTick(4);

    // Ramp cancelled, value snapped to 0.2
    EXPECT_DOUBLE_EQ(0.2, proxyVol.get());
    EXPECT_EQ(1, m_pEngine->rollbackCount());

    // Subsequent ticks: value stays at 0.2 (no ramp recovery)
    m_pEngine->onTick(5);
    EXPECT_DOUBLE_EQ(0.2, proxyVol.get());
}

// Discrete controls still snap (no ramp)
TEST_F(NetmixRollbackTest, DiscreteControlStillSnaps) {
    auto widVol = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    auto widPlay = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("play")));
    ASSERT_TRUE(widVol.has_value());
    ASSERT_TRUE(widPlay.has_value());

    ControlProxy proxyVol(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ControlProxy proxyPlay(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("play")));

    advanceTick(1, {{widVol.value(), 0.5}}, {});
    advanceTick(2, {{widVol.value(), 0.5}}, {});
    advanceTick(3, {{widVol.value(), 0.5}}, {});

    insertRemoteConfirmed(1, {{widVol.value(), 0.8}, {widPlay.value(), 1.0}});

    // Trigger rollback
    auto pred4 = makeFrame(4, {{widVol.value(), 0.5}});
    m_pBuffer->insertRemotePredicted(4, pred4);
    m_pEngine->onTick(4);

    EXPECT_EQ(1, m_pEngine->rollbackCount());
    // Vol is continuous — ramping, not snapped to 0.8
    EXPECT_DOUBLE_EQ(0.0, proxyVol.get());
    // Play is discrete — snapped immediately to 1.0
    EXPECT_DOUBLE_EQ(1.0, proxyPlay.get());
}

} // namespace
