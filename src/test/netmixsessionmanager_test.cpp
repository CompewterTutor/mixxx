#include <gtest/gtest.h>

#include "control/controlproxy.h"
#include "netmix/netmixsessionmanager.h"

namespace {

class NetmixSessionManagerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_pManager = std::make_unique<NetmixSessionManager>();
    }

    void TearDown() override {
        m_pManager.reset();
    }

    std::unique_ptr<NetmixSessionManager> m_pManager;
};

TEST_F(NetmixSessionManagerTest, InitialStateIsIdle) {
    EXPECT_EQ(NetmixSessionManager::Idle, m_pManager->state());
}

TEST_F(NetmixSessionManagerTest, StatusCOIsReadable) {
    ControlProxy statusCO("[Netmix]", "status");
    EXPECT_DOUBLE_EQ(0.0, statusCO.get());
}

TEST_F(NetmixSessionManagerTest, StatusCOIsReadOnly) {
    ControlProxy statusCO("[Netmix]", "status");
    EXPECT_DOUBLE_EQ(0.0, statusCO.get());

    // Attempt to write — should have no effect on state.
    statusCO.set(2.0);
    EXPECT_DOUBLE_EQ(0.0, statusCO.get());
    EXPECT_EQ(NetmixSessionManager::Idle, m_pManager->state());
}

} // namespace
