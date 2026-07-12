#include <gtest/gtest.h>

#include <QVector>

#include "netmix/inputbuffer.h"
#include "netmix/protocol.h"

namespace {

class NetmixInputBufferTest : public ::testing::Test {
  protected:
    static NetmixInputFrame makeFrame(quint32 tick, std::initializer_list<std::pair<quint16, double>> events) {
        NetmixInputFrame frame;
        frame.baseTick = tick;
        for (auto& [wireId, value] : events) {
            frame.events.append({wireId, value});
        }
        return frame;
    }
};

TEST_F(NetmixInputBufferTest, InsertConfirmedBasic) {
    InputBuffer buf;
    auto frame = makeFrame(100, {{1, 0.5}, {2, -0.75}});
    buf.insertRemoteConfirmed(100, frame);

    EXPECT_TRUE(buf.hasRemote(100));
    auto read = buf.remoteFrameAt(100);
    EXPECT_EQ(100u, read.baseTick);
    ASSERT_EQ(2, read.events.size());
    EXPECT_EQ(1, read.events[0].wireId);
    EXPECT_DOUBLE_EQ(0.5, read.events[0].value);
    EXPECT_EQ(2, read.events[1].wireId);
    EXPECT_DOUBLE_EQ(-0.75, read.events[1].value);
}

TEST_F(NetmixInputBufferTest, InsertPredictedBasic) {
    InputBuffer buf;
    auto frame = makeFrame(100, {{1, 0.5}});
    buf.insertRemotePredicted(100, frame);

    EXPECT_TRUE(buf.hasRemote(100));
    auto read = buf.remoteFrameAt(100);
    EXPECT_EQ(100u, read.baseTick);
    ASSERT_EQ(1, read.events.size());
    EXPECT_EQ(1, read.events[0].wireId);
    EXPECT_DOUBLE_EQ(0.5, read.events[0].value);
}

TEST_F(NetmixInputBufferTest, ConfirmedOverridesPredicted) {
    InputBuffer buf;
    buf.insertRemotePredicted(100, makeFrame(100, {{1, 0.5}}));
    buf.insertRemoteConfirmed(100, makeFrame(100, {{1, 0.9}}));

    EXPECT_TRUE(buf.hasRemote(100));
    auto read = buf.remoteFrameAt(100);
    ASSERT_EQ(1, read.events.size());
    EXPECT_DOUBLE_EQ(0.9, read.events[0].value);
}

TEST_F(NetmixInputBufferTest, NoDivergenceOnMatch) {
    InputBuffer buf;
    buf.insertRemotePredicted(100, makeFrame(100, {{1, 0.5}}));
    buf.insertRemoteConfirmed(100, makeFrame(100, {{1, 0.5}}));

    EXPECT_FALSE(buf.firstDivergentTick().has_value());
}

TEST_F(NetmixInputBufferTest, DivergenceOnMismatch) {
    InputBuffer buf;
    buf.insertRemotePredicted(100, makeFrame(100, {{1, 0.5}}));
    buf.insertRemoteConfirmed(100, makeFrame(100, {{1, 0.9}}));

    auto div = buf.firstDivergentTick();
    ASSERT_TRUE(div.has_value());
    EXPECT_EQ(100u, div.value());
}

TEST_F(NetmixInputBufferTest, DivergenceOnExtraEvent) {
    InputBuffer buf;
    buf.insertRemotePredicted(100, makeFrame(100, {{1, 0.5}}));
    buf.insertRemoteConfirmed(100, makeFrame(100, {{1, 0.5}, {2, -1.0}}));

    auto div = buf.firstDivergentTick();
    ASSERT_TRUE(div.has_value());
    EXPECT_EQ(100u, div.value());
}

TEST_F(NetmixInputBufferTest, DivergenceOnMissingEvent) {
    InputBuffer buf;
    buf.insertRemotePredicted(100, makeFrame(100, {{1, 0.5}, {2, -1.0}}));
    buf.insertRemoteConfirmed(100, makeFrame(100, {{1, 0.5}}));

    auto div = buf.firstDivergentTick();
    ASSERT_TRUE(div.has_value());
    EXPECT_EQ(100u, div.value());
}

TEST_F(NetmixInputBufferTest, DivergenceMultipleTicksReturnsEarliest) {
    InputBuffer buf;
    buf.insertRemotePredicted(100, makeFrame(100, {{1, 0.5}}));
    buf.insertRemotePredicted(101, makeFrame(101, {{1, 0.5}}));
    buf.insertRemotePredicted(102, makeFrame(102, {{1, 0.5}}));

    buf.insertRemoteConfirmed(100, makeFrame(100, {{1, 0.9}}));  // divergent
    buf.insertRemoteConfirmed(101, makeFrame(101, {{1, 0.5}}));  // match
    buf.insertRemoteConfirmed(102, makeFrame(102, {{1, 0.9}}));  // divergent

    auto div = buf.firstDivergentTick();
    ASSERT_TRUE(div.has_value());
    EXPECT_EQ(100u, div.value());
}

TEST_F(NetmixInputBufferTest, FirstDivergentTickResetsAfterClear) {
    InputBuffer buf;
    buf.insertRemotePredicted(100, makeFrame(100, {{1, 0.5}}));
    buf.insertRemoteConfirmed(100, makeFrame(100, {{1, 0.9}}));

    EXPECT_TRUE(buf.firstDivergentTick().has_value());
    EXPECT_EQ(100u, buf.firstDivergentTick().value());

    buf.clear();

    EXPECT_FALSE(buf.firstDivergentTick().has_value());
}

TEST_F(NetmixInputBufferTest, InsertLocalBasic) {
    InputBuffer buf;
    auto frame = makeFrame(50, {{3, 1.0}, {4, -0.5}});
    buf.insertLocal(50, frame);

    EXPECT_TRUE(buf.hasLocal(50));
    auto read = buf.localFrameAt(50);
    EXPECT_EQ(50u, read.baseTick);
    ASSERT_EQ(2, read.events.size());
    EXPECT_EQ(3, read.events[0].wireId);
    EXPECT_DOUBLE_EQ(1.0, read.events[0].value);
    EXPECT_EQ(4, read.events[1].wireId);
    EXPECT_DOUBLE_EQ(-0.5, read.events[1].value);
}

TEST_F(NetmixInputBufferTest, LocalAndRemoteIndependent) {
    InputBuffer buf;
    buf.insertLocal(100, makeFrame(100, {{1, 0.5}}));
    buf.insertRemotePredicted(100, makeFrame(100, {{2, -1.0}}));

    EXPECT_TRUE(buf.hasLocal(100));
    EXPECT_TRUE(buf.hasRemote(100));

    auto local = buf.localFrameAt(100);
    ASSERT_EQ(1, local.events.size());
    EXPECT_EQ(1, local.events[0].wireId);
    EXPECT_DOUBLE_EQ(0.5, local.events[0].value);

    auto remote = buf.remoteFrameAt(100);
    ASSERT_EQ(1, remote.events.size());
    EXPECT_EQ(2, remote.events[0].wireId);
    EXPECT_DOUBLE_EQ(-1.0, remote.events[0].value);
}

TEST_F(NetmixInputBufferTest, RingWrapAtCapacity) {
    InputBuffer buf;

    // Fill beyond capacity (256)
    for (quint32 t = 0; t < 260; ++t) {
        buf.insertRemoteConfirmed(t, makeFrame(t, {{1, static_cast<double>(t)}}));
    }

    // Ticks 0-3 should be evicted (slots reused by indices 0-3 for ticks 256-259)
    for (quint32 t = 0; t < 4; ++t) {
        EXPECT_FALSE(buf.hasRemote(t)) << "tick " << t << " should be evicted";
    }

    // Ticks 4-259 should still be readable (they occupy indices 4-255 then 0-3)
    for (quint32 t = 4; t < 260; ++t) {
        EXPECT_TRUE(buf.hasRemote(t)) << "tick " << t << " should be present";
    }
}

TEST_F(NetmixInputBufferTest, AdvanceWindow) {
    InputBuffer buf;

    for (quint32 t = 10; t <= 20; ++t) {
        buf.insertRemoteConfirmed(t, makeFrame(t, {{1, 1.0}}));
    }

    // All 11 ticks present
    for (quint32 t = 10; t <= 20; ++t) {
        EXPECT_TRUE(buf.hasRemote(t));
    }

    buf.advanceWindow(15);

    // Ticks 10-14 should be removed
    for (quint32 t = 10; t < 15; ++t) {
        EXPECT_FALSE(buf.hasRemote(t)) << "tick " << t << " should be removed";
    }

    // Ticks 15-20 should still be present
    for (quint32 t = 15; t <= 20; ++t) {
        EXPECT_TRUE(buf.hasRemote(t)) << "tick " << t << " should remain";
    }
}

TEST_F(NetmixInputBufferTest, SetCapacityTriggersClear) {
    InputBuffer buf;
    buf.insertRemoteConfirmed(100, makeFrame(100, {{1, 0.5}}));
    EXPECT_TRUE(buf.hasRemote(100));

    buf.setCapacity(128);
    EXPECT_EQ(128, buf.capacity());
    EXPECT_FALSE(buf.hasRemote(100));
}

} // namespace
