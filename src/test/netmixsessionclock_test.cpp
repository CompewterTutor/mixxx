#include <gtest/gtest.h>

#include "netmix/sessionclock.h"

namespace {

class NetmixSessionClockTest : public ::testing::Test {};

// Tick at 44100 Hz with 441-frame buffers (10 ms).
// Each buffer: 441*240/44100 = 2.4 ticks. Floor accumulation.
// Expected: 2, 4, 7, 9, 12.
TEST_F(NetmixSessionClockTest, TickProgression_44100) {
    SessionClock clock;

    clock.onFramesProcessed(441, 44100);
    EXPECT_EQ(2u, clock.currentTick());

    clock.onFramesProcessed(441, 44100);
    EXPECT_EQ(4u, clock.currentTick());

    clock.onFramesProcessed(441, 44100);
    EXPECT_EQ(7u, clock.currentTick());

    clock.onFramesProcessed(441, 44100);
    EXPECT_EQ(9u, clock.currentTick());

    clock.onFramesProcessed(441, 44100);
    EXPECT_EQ(12u, clock.currentTick());
}

// Tick at 48000 Hz with varied buffers.
TEST_F(NetmixSessionClockTest, TickProgression_48000) {
    SessionClock clock;

    clock.onFramesProcessed(200, 48000);
    EXPECT_EQ(1u, clock.currentTick());

    clock.onFramesProcessed(200, 48000);
    EXPECT_EQ(2u, clock.currentTick());

    clock.onFramesProcessed(200, 48000);
    EXPECT_EQ(3u, clock.currentTick());

    clock.onFramesProcessed(400, 48000);
    EXPECT_EQ(5u, clock.currentTick());

    clock.onFramesProcessed(400, 48000);
    EXPECT_EQ(7u, clock.currentTick());
}

// Odd buffer sizes (73, 151, 257) at 44100 — no drift from fractional accumulation.
TEST_F(NetmixSessionClockTest, VariedBufferSizes) {
    SessionClock clock;
    const int bufferSizes[] = {73, 151, 257};
    const int kNumCycles = 10;
    quint64 totalFrames = 0;
    quint32 prevTick = 0;

    for (int cycle = 0; cycle < kNumCycles; ++cycle) {
        for (int bufSize : bufferSizes) {
            clock.onFramesProcessed(bufSize, 44100);
            totalFrames += bufSize;

            quint32 current = clock.currentTick();
            EXPECT_GE(current, prevTick);
            prevTick = current;
        }
    }

    quint32 expectedTick = static_cast<quint32>(
            (totalFrames * SessionClock::kTickRate) / 44100);
    EXPECT_EQ(expectedTick, clock.currentTick());
}

// Sample rate change mid-stream: tick math adapts, monotonicity preserved.
TEST_F(NetmixSessionClockTest, SampleRateChange) {
    SessionClock clock;

    clock.onFramesProcessed(44100, 44100);
    EXPECT_EQ(240u, clock.currentTick());

    clock.onFramesProcessed(48000, 48000);
    // totalFrames = 92100, tick = 92100*240/48000 = 460
    EXPECT_EQ(460u, clock.currentTick());

    clock.onFramesProcessed(48000, 48000);
    // totalFrames = 140100, tick = 140100*240/48000 = 700 (floor of 700.5)
    EXPECT_EQ(700u, clock.currentTick());
}

// Offset shifts agreedTick without affecting currentTick.
TEST_F(NetmixSessionClockTest, OffsetApplication) {
    SessionClock clock;

    clock.onFramesProcessed(44100, 44100);
    EXPECT_EQ(240u, clock.currentTick());
    EXPECT_EQ(240u, clock.agreedTick());

    clock.setOffset(50);
    EXPECT_EQ(240u, clock.currentTick());
    EXPECT_EQ(290u, clock.agreedTick());

    clock.setOffset(-30);
    EXPECT_EQ(240u, clock.currentTick());
    EXPECT_EQ(210u, clock.agreedTick());

    clock.onFramesProcessed(44100, 44100);
    EXPECT_EQ(480u, clock.currentTick());
    EXPECT_EQ(450u, clock.agreedTick());
}

// 1 hour @ 44100 Hz produces exactly the expected tick count (no drift).
TEST_F(NetmixSessionClockTest, LongRunDriftBound) {
    SessionClock clock;

    const int kSampleRate = 44100;
    const int kBufferSize = 44100; // 1-second chunks
    const int kSeconds = 3600;

    for (int i = 0; i < kSeconds; ++i) {
        clock.onFramesProcessed(kBufferSize, kSampleRate);
    }

    const quint64 kTotalFrames = static_cast<quint64>(kSampleRate) * kSeconds;
    quint32 expectedTick = static_cast<quint32>(
            (kTotalFrames * SessionClock::kTickRate) / kSampleRate);
    EXPECT_EQ(864000u, expectedTick);
    EXPECT_NEAR(expectedTick, clock.currentTick(), 1);
}

// Reset clears all state (tick, offset, totalFrames).
TEST_F(NetmixSessionClockTest, Reset) {
    SessionClock clock;

    clock.onFramesProcessed(44100, 44100);
    clock.setOffset(100);
    EXPECT_EQ(240u, clock.currentTick());
    EXPECT_EQ(100, clock.offset());

    clock.reset();
    EXPECT_EQ(0u, clock.currentTick());
    EXPECT_EQ(0u, clock.agreedTick());
    EXPECT_EQ(0, clock.offset());

    clock.onFramesProcessed(44100, 44100);
    EXPECT_EQ(240u, clock.currentTick());
}

// Two identically-fed instances produce identical tick sequences.
TEST_F(NetmixSessionClockTest, Monotonicity_Determinism) {
    SessionClock clock1;
    SessionClock clock2;

    const int buffers[] = {73, 441, 200, 151, 1024, 257, 4096};

    for (int bufSize : buffers) {
        clock1.onFramesProcessed(bufSize, 44100);
        clock2.onFramesProcessed(bufSize, 44100);
        EXPECT_EQ(clock1.currentTick(), clock2.currentTick());
    }
}

} // namespace
