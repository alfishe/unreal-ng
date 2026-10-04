/// @file ttdconfigfingerprint_test.cpp
/// @brief The settings a session was recorded with, compared with a live
/// machine (TTD v2, Phase 3, Step 4).

#include <gtest/gtest.h>

#include "debugger/ttd/engine/ttdconfigfingerprint.h"

using namespace ttd;

namespace
{
TTDConfigFingerprint Recorded()
{
    TTDConfigFingerprint fp;
    fp.Add("machine.model", 3, true);
    fp.Add("timing.frame", 71680);
    fp.Add("sound.decimator_high_fidelity", 0);
    fp.Add("rom.signature", 0x1234);
    return fp;
}
}  // namespace

TEST(TTDConfigFingerprint_Test, TheSameSettingsDifferInNothing)
{
    EXPECT_TRUE(Compare(Recorded(), Recorded()).empty());
    EXPECT_TRUE(Recorded() == Recorded());
}

TEST(TTDConfigFingerprint_Test, EachDifferingSettingIsNamedWithBothValues)
{
    TTDConfigFingerprint live = Recorded();
    live.fields[2].value = 1;                  // HighFidelity decimator
    live.fields[3].value = 0x5678;             // another ROM set
    const auto diffs = Compare(Recorded(), live);
    ASSERT_EQ(diffs.size(), 2u);
    EXPECT_EQ(diffs[0].field, "sound.decimator_high_fidelity");
    EXPECT_EQ(diffs[0].recorded, "0");
    EXPECT_EQ(diffs[0].live, "1");
    EXPECT_FALSE(diffs[0].affectsRestore);
    EXPECT_EQ(diffs[1].field, "rom.signature");
    EXPECT_EQ(diffs[1].recorded, std::to_string(0x1234));
    EXPECT_EQ(diffs[1].live, std::to_string(0x5678));
}

TEST(TTDConfigFingerprint_Test, AModelDifferenceChangesWhatARestoreGives)
{
    TTDConfigFingerprint live = Recorded();
    live.fields[0].value = 4;
    const auto diffs = Compare(Recorded(), live);
    ASSERT_EQ(diffs.size(), 1u);
    EXPECT_TRUE(diffs[0].affectsRestore);
}

TEST(TTDConfigFingerprint_Test, ASettingOnOnlyOneSideIsADifference)
{
    TTDConfigFingerprint live = Recorded();
    live.fields.erase(live.fields.begin() + 1);   // no timing.frame here
    live.Add("profi.turbo", 1);                   // a board option the recording lacks
    const auto diffs = Compare(Recorded(), live);
    ASSERT_EQ(diffs.size(), 2u);
    EXPECT_EQ(diffs[0].field, "timing.frame");
    EXPECT_EQ(diffs[0].live, "-");
    EXPECT_EQ(diffs[1].field, "profi.turbo");
    EXPECT_EQ(diffs[1].recorded, "-");
}
