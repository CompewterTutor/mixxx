#include <gtest/gtest.h>

#include <memory>

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "netmix/controlallowlist.h"
#include "netmix/controlapplier.h"
#include "netmix/controlcapture.h"
#include "netmix/sessionclock.h"
#include "test/mixxxtest.h"

namespace {

class NetmixApplierTest : public MixxxTest {
  protected:
    void SetUp() override {
        // Create real COs for allowlisted keys
        new ControlObject(
                ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
        new ControlObject(
                ConfigKey(QStringLiteral("Channel1"), QStringLiteral("playposition")));
        new ControlObject(
                ConfigKey(QStringLiteral("Master"), QStringLiteral("crossfader")));

        m_pClock = std::make_unique<SessionClock>();
        m_pCapture = std::make_unique<ControlCapture>();
        m_pApplier = std::make_unique<ControlApplier>();

        m_pClock->onFramesProcessed(441, 44100);

        m_pCapture->start(m_pClock.get());
        m_pApplier->setProxies(m_pCapture->proxies());
    }

    void TearDown() override {
        m_pCapture->stop();
        m_pApplier.reset();
        m_pCapture.reset();
        m_pClock.reset();
    }

    std::optional<quint16> wireId(const ConfigKey& key) const {
        return ControlAllowlist::wireIdForKey(key);
    }

    std::unique_ptr<SessionClock> m_pClock;
    std::unique_ptr<ControlCapture> m_pCapture;
    std::unique_ptr<ControlApplier> m_pApplier;
};

// Apply sets the CO value correctly.
TEST_F(NetmixApplierTest, ApplySetsValue) {
    auto wid = wireId(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ASSERT_TRUE(wid.has_value());

    ControlProxy proxyVolume(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    EXPECT_DOUBLE_EQ(0.0, proxyVolume.get());

    m_pApplier->apply(wid.value(), 0.75);

    EXPECT_DOUBLE_EQ(0.75, proxyVolume.get());
}

// Apply does NOT cause capture to re-emit (echo suppression).
TEST_F(NetmixApplierTest, ApplyEchoSuppression) {
    auto wid = wireId(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ASSERT_TRUE(wid.has_value());

    int capturedCount = 0;
    QMetaObject::Connection conn = QObject::connect(
            m_pCapture.get(),
            &ControlCapture::captured,
            [&](quint32, quint16, double) { ++capturedCount; });

    m_pApplier->apply(wid.value(), 0.5);

    EXPECT_EQ(0, capturedCount);

    QObject::disconnect(conn);
}

// Ramp converges to target over requested tick count.
TEST_F(NetmixApplierTest, RampConverges) {
    auto wid = wireId(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ASSERT_TRUE(wid.has_value());

    ControlProxy proxyVolume(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));

    constexpr int kRampTicks = 5;
    m_pApplier->applyRamped(wid.value(), 1.0, kRampTicks);

    for (int i = 0; i < kRampTicks; ++i) {
        m_pApplier->advanceTick();
    }

    EXPECT_DOUBLE_EQ(1.0, proxyVolume.get());
}

// Ramp values progress monotonically toward target.
TEST_F(NetmixApplierTest, RampMonotonic) {
    auto wid = wireId(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ASSERT_TRUE(wid.has_value());

    ControlProxy proxyVolume(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));

    constexpr int kRampTicks = 10;
    m_pApplier->applyRamped(wid.value(), 1.0, kRampTicks);

    double prev = proxyVolume.get();
    for (int i = 1; i <= kRampTicks; ++i) {
        m_pApplier->advanceTick();
        double current = proxyVolume.get();
        // The ramp moves from start toward target; since start=0, target=1,
        // and we go from high remainingTicks → low, the value increases.
        // But our formula is: current = target + (start - target) * (remaining/total)
        // At remaining=total: current = 1 + (0-1)*1 = 0
        // At remaining=0: current = 1
        // So each tick moves us closer to 1.0.
        if (i > 1) {
            EXPECT_GE(current, prev);
        }
        prev = current;
    }

    EXPECT_DOUBLE_EQ(1.0, prev);
}

// Applying a new value supersedes an in-flight ramp.
TEST_F(NetmixApplierTest, ApplySupersedesRamp) {
    auto wid = wireId(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ASSERT_TRUE(wid.has_value());

    ControlProxy proxyVolume(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));

    m_pApplier->applyRamped(wid.value(), 1.0, 10);
    // Mid-ramp, apply a new value — ramp should be cancelled
    m_pApplier->apply(wid.value(), 0.5);

    // Value should be 0.5 immediately (no ramp)
    EXPECT_DOUBLE_EQ(0.5, proxyVolume.get());

    // Subsequent advanceTick should NOT move it back toward 1.0
    for (int i = 0; i < 5; ++i) {
        m_pApplier->advanceTick();
    }
    EXPECT_DOUBLE_EQ(0.5, proxyVolume.get());
}

// Seek-kind control applies directly (no ramp).
TEST_F(NetmixApplierTest, SeekAppliesDirectly) {
    auto wid = wireId(ConfigKey(
            QStringLiteral("Channel1"), QStringLiteral("playposition")));
    ASSERT_TRUE(wid.has_value());

    ControlProxy proxyPos(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("playposition")));

    // Seek control: applyRamped should still apply directly
    m_pApplier->applyRamped(wid.value(), 0.5, 10);

    // Should be set immediately
    EXPECT_DOUBLE_EQ(0.5, proxyPos.get());
}

// Crossfader (Continuous) ramps correctly.
TEST_F(NetmixApplierTest, CrossfaderRamp) {
    auto wid = wireId(
            ConfigKey(QStringLiteral("Master"), QStringLiteral("crossfader")));
    ASSERT_TRUE(wid.has_value());

    ControlProxy proxyXfade(
            ConfigKey(QStringLiteral("Master"), QStringLiteral("crossfader")));

    m_pApplier->applyRamped(wid.value(), 0.3, 3);

    m_pApplier->advanceTick();
    m_pApplier->advanceTick();
    m_pApplier->advanceTick();

    EXPECT_DOUBLE_EQ(0.3, proxyXfade.get());
}

} // namespace
