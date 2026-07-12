#include <gtest/gtest.h>

#include "netmix/channelownership.h"
#include "netmix/controlallowlist.h"

namespace {

class NetmixOwnershipTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ControlAllowlist::entries(); // ensure table built
    }
};

// ---------------------------------------------------------------------------
// State machine transitions
// ---------------------------------------------------------------------------

TEST_F(NetmixOwnershipTest, PreAssigned_EntersSessionOwnedLocal) {
    ChannelOwnership co(0); // peerId=0
    co.setLocalPreAssignment({1, 2});
    co.setRemotePreAssignment({});
    co.resolvePreAssignment();

    EXPECT_EQ(OwnershipState::OwnedLocal, co.state(1));
    EXPECT_EQ(OwnershipState::OwnedLocal, co.state(2));
    EXPECT_TRUE(co.isOwnedByLocal(1));
    EXPECT_TRUE(co.isOwnedByLocal(2));
}

TEST_F(NetmixOwnershipTest, PreAssigned_RemoteOwned) {
    ChannelOwnership co(1); // peerId=1
    co.setLocalPreAssignment({});
    co.setRemotePreAssignment({1, 3});
    co.resolvePreAssignment();

    EXPECT_EQ(OwnershipState::OwnedRemote, co.state(1));
    EXPECT_EQ(OwnershipState::OwnedRemote, co.state(3));
    EXPECT_TRUE(co.isOwnedByRemote(1));
    EXPECT_TRUE(co.isOwnedByRemote(3));
}

TEST_F(NetmixOwnershipTest, PreAssigned_HostSideRemote) {
    // Host (peerId=0) receives client's (peerId=1) pre-assignment
    ChannelOwnership co(0);
    co.setLocalPreAssignment({});
    co.setRemotePreAssignment({1, 3});
    co.resolvePreAssignment();

    EXPECT_EQ(OwnershipState::OwnedRemote, co.state(1));
    EXPECT_EQ(OwnershipState::OwnedRemote, co.state(3));
    EXPECT_TRUE(co.isOwnedByRemote(1));
    EXPECT_TRUE(co.isOwnedByRemote(3));
}

TEST_F(NetmixOwnershipTest, PreAssigned_LowerPeerIdWins) {
    // Both claim channel 2
    ChannelOwnership co(1); // selfPeerId=1 (higher)
    co.setLocalPreAssignment({2});
    co.setRemotePreAssignment({2}); // remote has peerId=0 (lower)
    co.resolvePreAssignment();

    // Lower peerId (remote, 0) wins
    EXPECT_EQ(OwnershipState::OwnedRemote, co.state(2));
    EXPECT_TRUE(co.isOwnedByRemote(2));
}

TEST_F(NetmixOwnershipTest, Claim_OnUnowned_Succeeds) {
    ChannelOwnership co(0);
    bool ok = co.claim(1);
    EXPECT_TRUE(ok);
    EXPECT_EQ(OwnershipState::PendingClaim, co.state(1));
}

TEST_F(NetmixOwnershipTest, Claim_OnOwnedLocal_ReturnsFalse) {
    ChannelOwnership co(0);
    co.setLocalPreAssignment({1});
    co.setRemotePreAssignment({});
    co.resolvePreAssignment();

    bool ok = co.claim(1);
    EXPECT_FALSE(ok);
    EXPECT_EQ(OwnershipState::OwnedLocal, co.state(1));
}

TEST_F(NetmixOwnershipTest, Claim_OnOwnedRemote_ReturnsFalse) {
    ChannelOwnership co(1);
    co.setLocalPreAssignment({});
    co.setRemotePreAssignment({1});
    co.resolvePreAssignment();

    bool ok = co.claim(1);
    EXPECT_FALSE(ok);
    EXPECT_EQ(OwnershipState::OwnedRemote, co.state(1));
}

TEST_F(NetmixOwnershipTest, Grant_TransitionsToOwnedLocal) {
    ChannelOwnership co(0);
    co.claim(1);
    EXPECT_EQ(OwnershipState::PendingClaim, co.state(1));

    co.handleGrant(1);
    EXPECT_EQ(OwnershipState::OwnedLocal, co.state(1));
    EXPECT_TRUE(co.isOwnedByLocal(1));
}

TEST_F(NetmixOwnershipTest, Deny_ResetsToUnowned) {
    ChannelOwnership co(0);
    co.claim(1);
    EXPECT_EQ(OwnershipState::PendingClaim, co.state(1));

    co.handleDeny(1, 1);
    EXPECT_EQ(OwnershipState::Unowned, co.state(1));
    EXPECT_TRUE(co.canClaim(1));
}

// ---------------------------------------------------------------------------
// Race resolution: simultaneous claim
// ---------------------------------------------------------------------------

TEST_F(NetmixOwnershipTest, SimultaneousClaim_LowerPeerIdWins) {
    ChannelOwnership coA(0);
    ChannelOwnership coB(1);

    // Both claim locally
    coA.claim(3);
    coB.claim(3);
    EXPECT_EQ(OwnershipState::PendingClaim, coA.state(3));
    EXPECT_EQ(OwnershipState::PendingClaim, coB.state(3));

    // PeerA (0, lower) receives PeerB's (1, higher) claim
    // PeerA wins → denies PeerB
    auto actionFromA = coA.handleRemoteClaim(3, 1);
    EXPECT_EQ(ChannelOwnership::DenyAction, actionFromA);
    EXPECT_EQ(OwnershipState::PendingClaim, coA.state(3));

    // PeerB receives PeerA's claim — PeerA (0) is lower, so PeerB auto-concedes
    auto actionFromB = coB.handleRemoteClaim(3, 0);
    EXPECT_EQ(ChannelOwnership::GrantAction, actionFromB);
    // PeerB's state: auto-denied PendingClaim then granted to remote
    EXPECT_EQ(OwnershipState::OwnedRemote, coB.state(3));

    // PeerA receives the Grant from PeerB → OwnedLocal
    coA.handleGrant(3);
    EXPECT_EQ(OwnershipState::OwnedLocal, coA.state(3));

    // Both converge: PeerA owns, PeerB knows PeerA owns
    EXPECT_TRUE(coA.isOwnedByLocal(3));
    EXPECT_TRUE(coB.isOwnedByRemote(3));
}

// ---------------------------------------------------------------------------
// Disconnect release
// ---------------------------------------------------------------------------

TEST_F(NetmixOwnershipTest, Disconnect_ReleasesAll) {
    ChannelOwnership co(0);
    co.setLocalPreAssignment({1, 2});
    co.setRemotePreAssignment({3});
    co.resolvePreAssignment();

    EXPECT_EQ(OwnershipState::OwnedLocal, co.state(1));
    EXPECT_EQ(OwnershipState::OwnedLocal, co.state(2));
    EXPECT_EQ(OwnershipState::OwnedRemote, co.state(3));

    co.autoReleaseAll();

    EXPECT_EQ(OwnershipState::Unowned, co.state(1));
    EXPECT_EQ(OwnershipState::Unowned, co.state(2));
    EXPECT_EQ(OwnershipState::Unowned, co.state(3));
}

// ---------------------------------------------------------------------------
// Explicit release
// ---------------------------------------------------------------------------

TEST_F(NetmixOwnershipTest, ExplicitRelease_ResetsToUnowned) {
    ChannelOwnership co(0);
    co.setLocalPreAssignment({1});
    co.resolvePreAssignment();
    EXPECT_EQ(OwnershipState::OwnedLocal, co.state(1));

    co.release(1);
    EXPECT_EQ(OwnershipState::Unowned, co.state(1));
    EXPECT_TRUE(co.canClaim(1));
}

// ---------------------------------------------------------------------------
// Crossfader ownership
// ---------------------------------------------------------------------------

TEST_F(NetmixOwnershipTest, Crossfader_Channel0) {
    auto ch = ControlAllowlist::channelForWireId(73);
    ASSERT_TRUE(ch.has_value());
    EXPECT_EQ(0, *ch);

    ChannelOwnership co(0);
    co.setLocalPreAssignment({0});
    co.resolvePreAssignment();
    EXPECT_TRUE(co.isOwnedByLocal(0));
}

// ---------------------------------------------------------------------------
// canClaim
// ---------------------------------------------------------------------------

TEST_F(NetmixOwnershipTest, CanClaim_UnownedReturnsTrue) {
    ChannelOwnership co(0);
    EXPECT_TRUE(co.canClaim(1));
    EXPECT_TRUE(co.canClaim(2));
}

TEST_F(NetmixOwnershipTest, CanClaim_OwnedReturnsFalse) {
    ChannelOwnership co(0);
    co.setLocalPreAssignment({1});
    co.setRemotePreAssignment({2});
    co.resolvePreAssignment();

    EXPECT_FALSE(co.canClaim(1));
    EXPECT_FALSE(co.canClaim(2));
}

// ---------------------------------------------------------------------------
// handleRemoteClaim
// ---------------------------------------------------------------------------

TEST_F(NetmixOwnershipTest, RemoteClaim_OnUnowned_Grants) {
    ChannelOwnership co(1); // peerId=1
    auto action = co.handleRemoteClaim(1, 0);
    EXPECT_EQ(ChannelOwnership::GrantAction, action);
    EXPECT_EQ(OwnershipState::OwnedRemote, co.state(1));
    EXPECT_TRUE(co.isOwnedByRemote(1));
}

TEST_F(NetmixOwnershipTest, RemoteClaim_OnOwnedLocal_Denies) {
    ChannelOwnership co(0);
    co.setLocalPreAssignment({1});
    co.resolvePreAssignment();

    auto action = co.handleRemoteClaim(1, 1);
    EXPECT_EQ(ChannelOwnership::DenyAction, action);
    EXPECT_EQ(OwnershipState::OwnedLocal, co.state(1));
}

// ---------------------------------------------------------------------------
// handleRelease from remote
// ---------------------------------------------------------------------------

TEST_F(NetmixOwnershipTest, RemoteRelease_FreesChannel) {
    ChannelOwnership co(1);
    co.setRemotePreAssignment({1});
    co.resolvePreAssignment();
    EXPECT_EQ(OwnershipState::OwnedRemote, co.state(1));

    co.handleRelease(1);
    EXPECT_EQ(OwnershipState::Unowned, co.state(1));
}

// ---------------------------------------------------------------------------
// channelForWireId mapping
// ---------------------------------------------------------------------------

TEST_F(NetmixOwnershipTest, ChannelForWireId_Deck1) {
    auto ch = ControlAllowlist::channelForWireId(1);
    ASSERT_TRUE(ch.has_value());
    EXPECT_EQ(1, *ch);
}

TEST_F(NetmixOwnershipTest, ChannelForWireId_Deck4) {
    auto ch = ControlAllowlist::channelForWireId(4);
    ASSERT_TRUE(ch.has_value());
    EXPECT_EQ(4, *ch);
}

TEST_F(NetmixOwnershipTest, ChannelForWireId_Hotcue) {
    // Hotcue 1, deck 2: wireId = 33 + 1 = 34
    auto ch = ControlAllowlist::channelForWireId(34);
    ASSERT_TRUE(ch.has_value());
    EXPECT_EQ(2, *ch);
}

TEST_F(NetmixOwnershipTest, ChannelForWireId_Crossfader) {
    auto ch = ControlAllowlist::channelForWireId(73);
    ASSERT_TRUE(ch.has_value());
    EXPECT_EQ(0, *ch);
}

TEST_F(NetmixOwnershipTest, ChannelForWireId_Invalid) {
    auto ch = ControlAllowlist::channelForWireId(99);
    EXPECT_FALSE(ch.has_value());
}

} // namespace
