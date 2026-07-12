#include <gtest/gtest.h>

#include "netmix/controlallowlist.h"
#include "netmix/inputbuffer.h"
#include "netmix/prediction.h"
#include "netmix/protocol.h"

namespace {

class NetmixPredictionTest : public ::testing::Test {
  protected:
    static NetmixInputFrame makeFrame(
            quint32 tick,
            std::initializer_list<std::pair<quint16, double>> events) {
        NetmixInputFrame frame;
        frame.baseTick = tick;
        for (auto& [wireId, value] : events) {
            frame.events.append({wireId, value});
        }
        return frame;
    }
};

TEST_F(NetmixPredictionTest, PredictConfirmedFrame) {
    InputBuffer buf;
    auto frame = makeFrame(100, {{25, 0.8}, {29, -0.5}});
    buf.insertRemoteConfirmed(100, frame);

    HoldLastPrediction predictor;
    auto predicted = predictor.predict(100, buf);

    EXPECT_EQ(100u, predicted.baseTick);
    ASSERT_EQ(2, predicted.events.size());
    EXPECT_EQ(25, predicted.events[0].wireId);
    EXPECT_DOUBLE_EQ(0.8, predicted.events[0].value);
    EXPECT_EQ(29, predicted.events[1].wireId);
    EXPECT_DOUBLE_EQ(-0.5, predicted.events[1].value);
}

TEST_F(NetmixPredictionTest, HoldLastFromPreviousConfirmed) {
    InputBuffer buf;
    auto frame = makeFrame(100, {{25, 0.8}, {29, -0.5}});
    buf.insertRemoteConfirmed(100, frame);

    HoldLastPrediction predictor;
    auto predicted = predictor.predict(101, buf);

    EXPECT_EQ(101u, predicted.baseTick);
    ASSERT_EQ(2, predicted.events.size());
    EXPECT_EQ(25, predicted.events[0].wireId);
    EXPECT_DOUBLE_EQ(0.8, predicted.events[0].value);
    EXPECT_EQ(29, predicted.events[1].wireId);
    EXPECT_DOUBLE_EQ(-0.5, predicted.events[1].value);
}

TEST_F(NetmixPredictionTest, IgnoresDiscreteInPrediction) {
    InputBuffer buf;
    auto frame = makeFrame(100, {{25, 0.8}, {1, 1.0}});
    buf.insertRemoteConfirmed(100, frame);

    HoldLastPrediction predictor;
    auto predicted = predictor.predict(101, buf);

    EXPECT_EQ(101u, predicted.baseTick);
    ASSERT_EQ(1, predicted.events.size());
    EXPECT_EQ(25, predicted.events[0].wireId);
    EXPECT_DOUBLE_EQ(0.8, predicted.events[0].value);
}

TEST_F(NetmixPredictionTest, EmptyWhenNoHistory) {
    InputBuffer buf;

    HoldLastPrediction predictor;
    auto predicted = predictor.predict(100, buf);

    EXPECT_EQ(100u, predicted.baseTick);
    EXPECT_EQ(0, predicted.events.size());
}

TEST_F(NetmixPredictionTest, MultipleGapsHoldLast) {
    InputBuffer buf;
    auto frame = makeFrame(100, {{25, 0.8}});
    buf.insertRemoteConfirmed(100, frame);

    HoldLastPrediction predictor;

    auto p1 = predictor.predict(101, buf);
    EXPECT_EQ(101u, p1.baseTick);
    ASSERT_EQ(1, p1.events.size());
    EXPECT_DOUBLE_EQ(0.8, p1.events[0].value);

    auto p2 = predictor.predict(102, buf);
    EXPECT_EQ(102u, p2.baseTick);
    ASSERT_EQ(1, p2.events.size());
    EXPECT_DOUBLE_EQ(0.8, p2.events[0].value);
}

class MockPrediction : public PredictionStrategy {
  public:
    NetmixInputFrame predict(quint32 tick, const InputBuffer&) override {
        NetmixInputFrame frame;
        frame.baseTick = tick;
        frame.events.append({99, 1.0});
        return frame;
    }
};

TEST_F(NetmixPredictionTest, StrategyPluggable) {
    InputBuffer buf;
    MockPrediction mock;
    auto predicted = mock.predict(100, buf);

    EXPECT_EQ(100u, predicted.baseTick);
    ASSERT_EQ(1, predicted.events.size());
    EXPECT_EQ(99, predicted.events[0].wireId);
    EXPECT_DOUBLE_EQ(1.0, predicted.events[0].value);
}

} // namespace
