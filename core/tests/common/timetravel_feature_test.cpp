// Tests for the timetravel feature flag registration and gating.
// Sprint 0, Item 0.3a — TDD §10.3.
//
// The GDB server (gdbserver) is intentionally NOT a runtime feature here;
// it is a build-time CMake flag (ENABLE_GDB_AUTOMATION) and is exercised
// via CMake smoke check, not by this test.

#include "pch.h"

#include <base/featuremanager.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>

#include "_helpers/emulatortesthelper.h"

#include <filesystem>

/// region <Timetravel feature flag registration>

TEST(TimeTravelFeature_Test, FlagRegisteredWithCorrectMetadata)
{
    Emulator* emu = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emu, nullptr);

    FeatureManager* fm = emu->GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    auto features = fm->listFeatures();

    // Find the timetravel feature
    auto it = std::find_if(features.begin(), features.end(),
                           [](const FeatureManager::FeatureInfo& f) { return f.id == Features::kTimeTravel; });
    ASSERT_NE(it, features.end()) << "timetravel feature must be registered";

    EXPECT_EQ(it->alias, Features::kTimeTravelAlias) << "alias must be 'ttd'";
    EXPECT_EQ(it->category, Features::kCategoryDebug) << "category must be debug";
    EXPECT_FALSE(it->enabled) << "must be OFF by default";
    ASSERT_FALSE(it->availableModes.empty());
    EXPECT_NE(std::find(it->availableModes.begin(), it->availableModes.end(), std::string("off")),
              it->availableModes.end());
    EXPECT_NE(std::find(it->availableModes.begin(), it->availableModes.end(), std::string("on")),
              it->availableModes.end());

    EmulatorTestHelper::CleanupEmulator(emu);
}

TEST(TimeTravelFeature_Test, AliasResolvesToTimetravel)
{
    Emulator* emu = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emu, nullptr);

    FeatureManager* fm = emu->GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    // Disable debugmode first so the auto-enable path is observable later
    ASSERT_TRUE(fm->setFeature(Features::kDebugMode, false));
    ASSERT_FALSE(fm->isEnabled(Features::kDebugMode));

    // Use the 'ttd' alias to enable
    EXPECT_TRUE(fm->setFeature(Features::kTimeTravelAlias, true));
    EXPECT_TRUE(fm->isEnabled(Features::kTimeTravel))
        << "enabling via alias 'ttd' must reflect on canonical id 'timetravel'";

    EmulatorTestHelper::CleanupEmulator(emu);
}

/// endregion </Timetravel feature flag registration>

/// region <Auto-enable master debugmode>

TEST(TimeTravelFeature_Test, EnablingTimetravelAutoEnablesDebugMode)
{
    Emulator* emu = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emu, nullptr);

    FeatureManager* fm = emu->GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    // Precondition: debugmode off
    ASSERT_TRUE(fm->setFeature(Features::kDebugMode, false));
    ASSERT_FALSE(fm->isEnabled(Features::kDebugMode));

    // Action: enable timetravel
    ASSERT_TRUE(fm->setFeature(Features::kTimeTravel, true));

    // Per TDD §10.3: depends: debugmode=on (same pattern as breakpoints/calltrace/memorytracking)
    EXPECT_TRUE(fm->isEnabled(Features::kDebugMode))
        << "Enabling timetravel must auto-enable debugmode (TDD §10.3 dependency)";

    EmulatorTestHelper::CleanupEmulator(emu);
}

TEST(TimeTravelFeature_Test, DisablingTimetravelDoesNotDisableDebugMode)
{
    // Reverse direction is intentionally not auto-cascading — debugmode is a master
    // that may be on for many reasons; turning off a subfeature never turns off the master.
    Emulator* emu = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emu, nullptr);

    FeatureManager* fm = emu->GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    ASSERT_TRUE(fm->setFeature(Features::kTimeTravel, true));
    ASSERT_TRUE(fm->isEnabled(Features::kDebugMode));

    ASSERT_TRUE(fm->setFeature(Features::kTimeTravel, false));
    EXPECT_FALSE(fm->isEnabled(Features::kTimeTravel));
    EXPECT_TRUE(fm->isEnabled(Features::kDebugMode))
        << "Disabling a subfeature must not cascade to disabling the master debugmode";

    EmulatorTestHelper::CleanupEmulator(emu);
}

/// endregion </Auto-enable master debugmode>

/// region <Persistence round-trip>

TEST(TimeTravelFeature_Test, RoundTripsThroughFeaturesIni)
{
    // The feature must survive a save→load cycle, like every other feature.
    Emulator* emu = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emu, nullptr);

    FeatureManager* fm = emu->GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "unreal-ng-ttd-feature-test.ini";

    // Set known state, save
    ASSERT_TRUE(fm->setFeature(Features::kTimeTravel, true));
    fm->saveToFile(tmp.string());

    // New manager, fresh defaults, load
    EmulatorContext ctx(LoggerLevel::LogError);
    FeatureManager loaded(&ctx);
    loaded.loadFromFile(tmp.string());

    EXPECT_TRUE(loaded.isEnabled(Features::kTimeTravel))
        << "timetravel state must round-trip through features.ini";

    std::error_code ec;
    fs::remove(tmp, ec);

    EmulatorTestHelper::CleanupEmulator(emu);
}

/// endregion </Persistence round-trip>
