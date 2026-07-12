#include <gtest/gtest.h>

#include "netmix/quantizer.h"
#include "netmix/sessionclock.h"

namespace {

class NetmixQuantizerTest : public ::testing::Test {
  protected:
    static constexpr int kTickRate = SessionClock::kTickRate;

    void SetUp() override {
        m_pQuantizer = std::make_unique<NetmixQuantizer>();
        m_pQuantizer->setEnabled(true);
    }

    std::unique_ptr<NetmixQuantizer> m_pQuantizer;
};

TEST_F(NetmixQuantizerTest, GridMath120Bpm) {
    constexpr double bpm = 120.0;
    // 64th-grid period = (240 * 60) / (120 * 16) = 7.5 ticks (exact in double)
    EXPECT_EQ(0u, m_pQuantizer->snap(0, bpm, kTickRate));
    EXPECT_EQ(0u, m_pQuantizer->snap(3, bpm, kTickRate));
    EXPECT_EQ(8u, m_pQuantizer->snap(4, bpm, kTickRate));
    EXPECT_EQ(8u, m_pQuantizer->snap(7, bpm, kTickRate));
    EXPECT_EQ(8u, m_pQuantizer->snap(11, bpm, kTickRate));
    EXPECT_EQ(15u, m_pQuantizer->snap(12, bpm, kTickRate));
    EXPECT_EQ(15u, m_pQuantizer->snap(15, bpm, kTickRate));
    EXPECT_EQ(23u, m_pQuantizer->snap(20, bpm, kTickRate));
}

TEST_F(NetmixQuantizerTest, GridMath80Bpm) {
    constexpr double bpm = 80.0;
    // 64th-grid period = (240 * 60) / (80 * 16) = 11.25 ticks (exact in double)
    // Grid centers at: 0, 11.25, 22.5, 33.75, ...
    EXPECT_EQ(0u, m_pQuantizer->snap(0, bpm, kTickRate));
    EXPECT_EQ(0u, m_pQuantizer->snap(5, bpm, kTickRate));
    EXPECT_EQ(11u, m_pQuantizer->snap(6, bpm, kTickRate));
    EXPECT_EQ(11u, m_pQuantizer->snap(11, bpm, kTickRate));
    EXPECT_EQ(11u, m_pQuantizer->snap(16, bpm, kTickRate));
    EXPECT_EQ(23u, m_pQuantizer->snap(17, bpm, kTickRate));
    EXPECT_EQ(23u, m_pQuantizer->snap(23, bpm, kTickRate));
    EXPECT_EQ(34u, m_pQuantizer->snap(29, bpm, kTickRate));
    EXPECT_EQ(34u, m_pQuantizer->snap(34, bpm, kTickRate));
}

TEST_F(NetmixQuantizerTest, GridMath140Bpm) {
    constexpr double bpm = 140.0;
    // 64th-grid period = (240 * 60) / (140 * 16) = 14400 / 2240 = 45/7 ticks
    // Grid centers ~0, 6.43, 12.86, 19.29, 25.71, ...
    EXPECT_EQ(0u, m_pQuantizer->snap(0, bpm, kTickRate));
    EXPECT_EQ(6u, m_pQuantizer->snap(6, bpm, kTickRate));
    EXPECT_EQ(13u, m_pQuantizer->snap(13, bpm, kTickRate));
    EXPECT_EQ(19u, m_pQuantizer->snap(19, bpm, kTickRate));
    EXPECT_EQ(26u, m_pQuantizer->snap(26, bpm, kTickRate));
}

TEST_F(NetmixQuantizerTest, GridMath175Bpm) {
    constexpr double bpm = 175.0;
    // 64th-grid period = (240 * 60) / (175 * 16) = 14400 / 2800 = 36/7 ticks
    // Grid centers ~0, 5.14, 10.29, 15.43, 20.57, ...
    EXPECT_EQ(0u, m_pQuantizer->snap(0, bpm, kTickRate));
    EXPECT_EQ(5u, m_pQuantizer->snap(5, bpm, kTickRate));
    EXPECT_EQ(10u, m_pQuantizer->snap(10, bpm, kTickRate));
    EXPECT_EQ(15u, m_pQuantizer->snap(15, bpm, kTickRate));
    EXPECT_EQ(21u, m_pQuantizer->snap(21, bpm, kTickRate));
}

TEST_F(NetmixQuantizerTest, DisabledPassthrough) {
    m_pQuantizer->setEnabled(false);
    EXPECT_EQ(7u, m_pQuantizer->snap(7, 120.0, kTickRate));
    EXPECT_EQ(0u, m_pQuantizer->snap(0, 120.0, kTickRate));
    EXPECT_EQ(999u, m_pQuantizer->snap(999, 120.0, kTickRate));
}

TEST_F(NetmixQuantizerTest, EnableDisable) {
    m_pQuantizer->setEnabled(true);
    quint32 snapped = m_pQuantizer->snap(7, 120.0, kTickRate);
    EXPECT_NE(7u, snapped);

    m_pQuantizer->setEnabled(false);
    EXPECT_EQ(7u, m_pQuantizer->snap(7, 120.0, kTickRate));

    m_pQuantizer->setEnabled(true);
    EXPECT_NE(7u, m_pQuantizer->snap(7, 120.0, kTickRate));
}

TEST_F(NetmixQuantizerTest, ZeroBpmFallback) {
    m_pQuantizer->setEnabled(true);
    EXPECT_EQ(7u, m_pQuantizer->snap(7, 0.0, kTickRate));
    EXPECT_EQ(7u, m_pQuantizer->snap(7, -1.0, kTickRate));
    EXPECT_EQ(0u, m_pQuantizer->snap(0, 0.0, kTickRate));
}

TEST_F(NetmixQuantizerTest, BothSidesSymmetry) {
    NetmixQuantizer q2;
    q2.setEnabled(true);

    static constexpr double kTestBpm = 128.0;
    for (quint32 tick = 0; tick < 100; ++tick) {
        EXPECT_EQ(m_pQuantizer->snap(tick, kTestBpm, kTickRate),
                q2.snap(tick, kTestBpm, kTickRate));
    }

    // Different tickRate should produce different results
    EXPECT_NE(m_pQuantizer->snap(7, kTestBpm, kTickRate),
            q2.snap(7, kTestBpm, 480));
}

} // namespace
