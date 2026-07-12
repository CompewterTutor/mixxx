#include <gtest/gtest.h>

#include <QVector>

#include "netmix/inputframe.h"
#include "netmix/protocol.h"

namespace {

class NetmixInputFrameTest : public ::testing::Test {
  protected:
    void advanceOneTick(InputFramePacker& packer, quint32 tick) {
        // Add a few events, then finish
        packer.addEvent(1, 0.5);
        packer.addEvent(2, -0.75);
        packer.finishTick(tick);
    }
};

// Basic add + finish + framesForSend round-trip.
TEST_F(NetmixInputFrameTest, BasicRoundTrip) {
    InputFramePacker packer;

    packer.addEvent(1, 0.5);
    packer.addEvent(2, -0.75);
    packer.finishTick(100);

    auto frames = packer.framesForSend(4);
    ASSERT_EQ(1, frames.size());
    EXPECT_EQ(100u, frames[0].baseTick);
    ASSERT_EQ(2, frames[0].events.size());
    EXPECT_EQ(1, frames[0].events[0].wireId);
    EXPECT_DOUBLE_EQ(0.5, frames[0].events[0].value);
    EXPECT_EQ(2, frames[0].events[1].wireId);
    EXPECT_DOUBLE_EQ(-0.75, frames[0].events[1].value);
}

// Multiple ticks produce multiple frames.
TEST_F(NetmixInputFrameTest, MultipleTicks) {
    InputFramePacker packer;

    advanceOneTick(packer, 100);
    advanceOneTick(packer, 101);
    advanceOneTick(packer, 102);

    auto frames = packer.framesForSend(4);
    ASSERT_EQ(3, frames.size());
    // framesForSend returns newest-first
    EXPECT_EQ(102u, frames[0].baseTick);
    EXPECT_EQ(101u, frames[1].baseTick);
    EXPECT_EQ(100u, frames[2].baseTick);
}

// Dedup: same wireId twice in same tick keeps the last value.
TEST_F(NetmixInputFrameTest, DedupSameTick) {
    InputFramePacker packer;

    packer.addEvent(1, 0.5);
    packer.addEvent(1, 0.9); // overwrite
    packer.addEvent(2, 1.0);
    packer.finishTick(200);

    auto frames = packer.framesForSend(4);
    ASSERT_EQ(1, frames.size());
    ASSERT_EQ(2, frames[0].events.size());

    // wireId 1 should have the LAST value (0.9)
    EXPECT_EQ(1, frames[0].events[0].wireId);
    EXPECT_DOUBLE_EQ(0.9, frames[0].events[0].value);
    EXPECT_EQ(2, frames[0].events[1].wireId);
    EXPECT_DOUBLE_EQ(1.0, frames[0].events[1].value);
}

// Empty state: no events added → no frames.
TEST_F(NetmixInputFrameTest, EmptyState) {
    InputFramePacker packer;

    auto frames = packer.framesForSend(4);
    EXPECT_TRUE(frames.isEmpty());
}

// Encode/decode round-trip through protocol.
TEST_F(NetmixInputFrameTest, ProtocolRoundTrip) {
    InputFramePacker packer;

    packer.addEvent(1, 0.5);
    packer.addEvent(3, -1.0);
    packer.finishTick(500);
    packer.addEvent(2, 0.25);
    packer.finishTick(501);

    auto frames = packer.framesForSend(4);
    ASSERT_EQ(2, frames.size());

    for (const auto& frame : frames) {
        NetmixMessage msg{NetmixMessageType::InputFrame, frame};
        QByteArray wire = encodeMessage(msg);
        auto decoded = decodeMessage(wire);
        ASSERT_TRUE(decoded.has_value());
        EXPECT_EQ(NetmixMessageType::InputFrame, decoded->type);

        auto* decodedFrame = std::get_if<NetmixInputFrame>(&decoded->payload);
        ASSERT_NE(nullptr, decodedFrame);
        EXPECT_EQ(frame.baseTick, decodedFrame->baseTick);
        ASSERT_EQ(frame.events.size(), decodedFrame->events.size());
        for (int i = 0; i < frame.events.size(); ++i) {
            EXPECT_EQ(frame.events[i].wireId, decodedFrame->events[i].wireId);
            EXPECT_DOUBLE_EQ(frame.events[i].value, decodedFrame->events[i].value);
        }
    }
}

// Size bound: realistic event load stays under 1200 bytes.
TEST_F(NetmixInputFrameTest, SizeBound) {
    InputFramePacker packer;

    // Fill 4 ticks with 20 events each (high but plausible)
    for (quint32 tick = 0; tick < 4; ++tick) {
        for (int e = 0; e < 20; ++e) {
            packer.addEvent(static_cast<quint16>(e + 1),
                    static_cast<double>(e) / 20.0);
        }
        packer.finishTick(tick);
    }

    auto frames = packer.framesForSend(4);
    ASSERT_EQ(4, frames.size());

    quint32 totalBytes = 0;
    for (const auto& frame : frames) {
        NetmixMessage msg{NetmixMessageType::InputFrame, frame};
        QByteArray wire = encodeMessage(msg);
        totalBytes += wire.size();
    }

    EXPECT_LT(totalBytes, 1200u);
}

// Batch size limits frames returned.
TEST_F(NetmixInputFrameTest, BatchSizeLimit) {
    InputFramePacker packer;

    for (quint32 tick = 0; tick < 8; ++tick) {
        advanceOneTick(packer, tick);
    }

    // Request batch of 3
    auto frames = packer.framesForSend(3);
    EXPECT_EQ(3, frames.size());
}

// Clear resets all state.
TEST_F(NetmixInputFrameTest, ClearResetsState) {
    InputFramePacker packer;

    advanceOneTick(packer, 100);
    advanceOneTick(packer, 101);

    EXPECT_EQ(2, packer.framesForSend(4).size());

    packer.clear();

    EXPECT_EQ(0, packer.framesForSend(4).size());
}

// Max events per tick enforced.
TEST_F(NetmixInputFrameTest, MaxEventsPerTick) {
    InputFramePacker packer;

    // Add more events than the per-tick max
    for (int i = 0; i < InputFramePacker::kMaxEventsPerTick + 10; ++i) {
        packer.addEvent(static_cast<quint16>(i + 1),
                static_cast<double>(i));
    }
    packer.finishTick(999);

    auto frames = packer.framesForSend(4);
    ASSERT_EQ(1, frames.size());
    EXPECT_LE(frames[0].events.size(),
            InputFramePacker::kMaxEventsPerTick);
}

} // namespace
