#include <gtest/gtest.h>

#include <memory>

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "netmix/controlallowlist.h"
#include "netmix/controlcapture.h"
#include "netmix/sessionclock.h"
#include "test/mixxxtest.h"

namespace {

class NetmixCaptureTest : public MixxxTest {
  protected:
    void SetUp() override {
        // Create a real CO for an allowlisted key
        m_pAllowlistedCO = new ControlObject(
                ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
        // Create a non-allowlisted CO
        m_pNonAllowlistedCO = new ControlObject(
                ConfigKey(QStringLiteral("Test"), QStringLiteral("not_listed")));

        m_pClock = std::make_unique<SessionClock>();
        m_pCapture = std::make_unique<ControlCapture>();

        // Advance clock by 1 tick so agreedTick() returns non-zero
        m_pClock->onFramesProcessed(441, 44100);
    }

    void TearDown() override {
        m_pCapture->stop();
        m_pCapture.reset();
        m_pClock.reset();
        // COs cleaned up by ~MixxxTest
    }

    int findProxyIndex(const ConfigKey& key) const {
        const auto& entries = ControlAllowlist::entries();
        for (int i = 0; i < entries.size(); ++i) {
            if (entries[i].key == key) {
                return i;
            }
        }
        return -1;
    }

    std::unique_ptr<SessionClock> m_pClock;
    std::unique_ptr<ControlCapture> m_pCapture;
    ControlObject* m_pAllowlistedCO;
    ControlObject* m_pNonAllowlistedCO;
};

// Setting an allowlisted CO via an external (different) proxy emits captured.
TEST_F(NetmixCaptureTest, ExternalSetEmitsCaptured) {
    m_pCapture->start(m_pClock.get());

    int capturedCount = 0;
    quint32 capturedTick = 0;
    quint16 capturedWireId = 0;
    double capturedValue = 0;

    QMetaObject::Connection conn = QObject::connect(
            m_pCapture.get(),
            &ControlCapture::captured,
            [&](quint32 tick, quint16 wireId, double value) {
                ++capturedCount;
                capturedTick = tick;
                capturedWireId = wireId;
                capturedValue = value;
            });

    // Set via a separate proxy (not owned by capture)
    ControlProxy externalProxy(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    externalProxy.set(0.75);

    EXPECT_EQ(1, capturedCount);
    EXPECT_EQ(2u, capturedTick) << "clock advanced by 1 tick = agreedTick 2";
    EXPECT_EQ(25u, capturedWireId) << "[Channel1],volume wireId";
    EXPECT_DOUBLE_EQ(0.75, capturedValue);

    QObject::disconnect(conn);
}

// Setting via the capture's own proxy does NOT emit (echo suppression).
TEST_F(NetmixCaptureTest, OwnProxySetDoesNotEmit) {
    m_pCapture->start(m_pClock.get());

    int capturedCount = 0;
    QMetaObject::Connection conn = QObject::connect(
            m_pCapture.get(),
            &ControlCapture::captured,
            [&](quint32, quint16, double) { ++capturedCount; });

    int idx = findProxyIndex(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ASSERT_GE(idx, 0) << "[Channel1],volume not found in allowlist";
    ASSERT_LT(idx, m_pCapture->proxies().size());

    m_pCapture->proxies()[idx]->set(0.8);

    // Wait — the value change might be async if Qt::QueuedConnection.
    // But we used DirectConnection in capture, so the set() completes
    // synchronously.
    EXPECT_EQ(0, capturedCount);

    QObject::disconnect(conn);
}

// Setting a non-allowlisted CO does NOT emit captured.
TEST_F(NetmixCaptureTest, NonAllowlistedSetDoesNotEmit) {
    m_pCapture->start(m_pClock.get());

    int capturedCount = 0;
    QMetaObject::Connection conn = QObject::connect(
            m_pCapture.get(),
            &ControlCapture::captured,
            [&](quint32, quint16, double) { ++capturedCount; });

    // Set via external proxy on non-allowlisted CO
    ControlProxy externalProxy(
            ConfigKey(QStringLiteral("Test"), QStringLiteral("not_listed")));
    externalProxy.set(1.0);

    EXPECT_EQ(0, capturedCount);

    QObject::disconnect(conn);
}

// After stop(), setting allowlisted CO does NOT emit.
TEST_F(NetmixCaptureTest, StopDisconnects) {
    m_pCapture->start(m_pClock.get());
    m_pCapture->stop();

    int capturedCount = 0;
    QMetaObject::Connection conn = QObject::connect(
            m_pCapture.get(),
            &ControlCapture::captured,
            [&](quint32, quint16, double) { ++capturedCount; });

    ControlProxy externalProxy(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    externalProxy.set(0.5);

    EXPECT_EQ(0, capturedCount);

    QObject::disconnect(conn);
}

// Restart after stop works cleanly.
TEST_F(NetmixCaptureTest, RestartWorks) {
    m_pCapture->start(m_pClock.get());
    m_pCapture->stop();
    m_pCapture->start(m_pClock.get());

    int capturedCount = 0;
    QMetaObject::Connection conn = QObject::connect(
            m_pCapture.get(),
            &ControlCapture::captured,
            [&](quint32, quint16, double) { ++capturedCount; });

    ControlProxy externalProxy(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    externalProxy.set(0.6);

    EXPECT_EQ(1, capturedCount);

    QObject::disconnect(conn);
}

} // namespace
