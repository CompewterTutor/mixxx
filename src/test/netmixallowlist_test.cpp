#include <gtest/gtest.h>

#include <optional>

#include "netmix/controlallowlist.h"

namespace {

class NetmixAllowlistTest : public ::testing::Test {};

// Every known key round-trips through wireIdForKey → keyForWireId.
TEST_F(NetmixAllowlistTest, RoundTrip_KnownKeys) {
    const auto& entries = ControlAllowlist::entries();
    ASSERT_GT(entries.size(), 0);

    for (const auto& entry : entries) {
        auto wireIdOpt = ControlAllowlist::wireIdForKey(entry.key);
        ASSERT_TRUE(wireIdOpt.has_value());
        EXPECT_EQ(entry.wireId, wireIdOpt.value());

        auto keyOpt = ControlAllowlist::keyForWireId(entry.wireId);
        ASSERT_TRUE(keyOpt.has_value());
        EXPECT_EQ(entry.key.group, keyOpt->group);
        EXPECT_EQ(entry.key.item, keyOpt->item);

        auto kindOpt = ControlAllowlist::kindForWireId(entry.wireId);
        ASSERT_TRUE(kindOpt.has_value());
        EXPECT_EQ(entry.kind, kindOpt.value());
    }
}

// Wire IDs are all non-zero and unique.
TEST_F(NetmixAllowlistTest, WireIdsNonZeroAndUnique) {
    const auto& entries = ControlAllowlist::entries();
    QSet<quint16> seen;
    for (const auto& entry : entries) {
        EXPECT_NE(0, entry.wireId);
        EXPECT_FALSE(seen.contains(entry.wireId))
                << "Duplicate wireId " << entry.wireId;
        seen.insert(entry.wireId);
    }
}

// Unknown key returns nullopt for all three lookups.
TEST_F(NetmixAllowlistTest, UnknownKey_ReturnsNullopt) {
    ConfigKey unknownKey("[NoSuchGroup]", "nonexistent");

    auto wireIdOpt = ControlAllowlist::wireIdForKey(unknownKey);
    EXPECT_FALSE(wireIdOpt.has_value());

    auto keyOpt = ControlAllowlist::keyForWireId(0xFFFF);
    EXPECT_FALSE(keyOpt.has_value());

    auto kindOpt = ControlAllowlist::kindForWireId(0xFFFF);
    EXPECT_FALSE(kindOpt.has_value());
}

// Kind classification matches expected semantics.
TEST_F(NetmixAllowlistTest, KindClassification) {
    // Continuous: volume, rate, pregain, superknobs, crossfader
    auto volWire = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("volume")));
    ASSERT_TRUE(volWire.has_value());
    EXPECT_EQ(ControlKind::Continuous,
            ControlAllowlist::kindForWireId(volWire.value()).value());

    auto rateWire = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("rate")));
    ASSERT_TRUE(rateWire.has_value());
    EXPECT_EQ(ControlKind::Continuous,
            ControlAllowlist::kindForWireId(rateWire.value()).value());

    auto xfadeWire = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Master"), QStringLiteral("crossfader")));
    ASSERT_TRUE(xfadeWire.has_value());
    EXPECT_EQ(ControlKind::Continuous,
            ControlAllowlist::kindForWireId(xfadeWire.value()).value());

    // Discrete: play, cue_default, start, end, hotcue
    auto playWire = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel1"), QStringLiteral("play")));
    ASSERT_TRUE(playWire.has_value());
    EXPECT_EQ(ControlKind::Discrete,
            ControlAllowlist::kindForWireId(playWire.value()).value());

    auto cueWire = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel2"), QStringLiteral("cue_default")));
    ASSERT_TRUE(cueWire.has_value());
    EXPECT_EQ(ControlKind::Discrete,
            ControlAllowlist::kindForWireId(cueWire.value()).value());

    // Seek: playposition
    auto seekWire = ControlAllowlist::wireIdForKey(
            ConfigKey(QStringLiteral("Channel3"), QStringLiteral("playposition")));
    ASSERT_TRUE(seekWire.has_value());
    EXPECT_EQ(ControlKind::Seek,
            ControlAllowlist::kindForWireId(seekWire.value()).value());
}

// Total entry count matches expected value.
TEST_F(NetmixAllowlistTest, EntryCount) {
    // 4 decks × (play + cue_default + start + end + playposition + rate +
    //             volume + pregain + 8 hotcues) = 4 × 16 = 64
    // + 4 EQ superknobs + 4 QuickEffect superknobs + 1 crossfader = 73
    EXPECT_EQ(73, ControlAllowlist::entries().size());
}

} // namespace
