#include "pch.h"

#include <base/featuremanager.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/notifications.h>
#include <emulator/platform.h>
#include <3rdparty/message-center/messagecenter.h>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testtiminghelper.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

/// region <HUD feature flag registration>

TEST(FeatureChangedNotification_Test, HudFlagRegisteredWithCorrectMetadata)
{
    Emulator* emu = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emu, nullptr);

    FeatureManager* fm = emu->GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    // Verify HUD feature is disabled by default
    EXPECT_FALSE(fm->isEnabled(Features::kHud));

    auto features = fm->listFeatures();
    auto it = std::find_if(features.begin(), features.end(),
                           [](const FeatureManager::FeatureInfo& f) { return f.id == Features::kHud; });
    ASSERT_NE(it, features.end()) << "HUD feature must be registered";

    EXPECT_EQ(it->alias, Features::kHudAlias);
    EXPECT_EQ(it->description, Features::kHudDesc);
    EXPECT_EQ(it->category, Features::kCategoryPerformance);
    EXPECT_FALSE(it->enabled);
    ASSERT_FALSE(it->availableModes.empty());
    EXPECT_NE(std::find(it->availableModes.begin(), it->availableModes.end(), std::string("off")),
              it->availableModes.end());
    EXPECT_NE(std::find(it->availableModes.begin(), it->availableModes.end(), std::string("on")),
              it->availableModes.end());

    EmulatorTestHelper::CleanupEmulator(emu);
}

/// endregion </HUD feature flag registration>

/// region <NC_FEATURE_CHANGED notifications>

TEST(FeatureChangedNotification_Test, SetFeaturePostsNotificationWithPayload)
{
    Emulator* emu = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emu, nullptr);

    FeatureManager* fm = emu->GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    std::atomic<int> receivedCount{0};
    unreal::UUID capturedId;
    std::string capturedFeatureId;
    bool capturedEnabled = false;

    uint64_t obsId = mc.AddObserver(NC_FEATURE_CHANGED, [&](int, Message* msg) {
        if (!msg) return;
        auto* payload = dynamic_cast<FeatureChangedPayload*>(msg->obj);
        if (payload && payload->emulatorId == emu->GetContext()->emulatorId)
        {
            capturedId = payload->emulatorId;
            capturedFeatureId = payload->featureId;
            capturedEnabled = payload->enabled;
            receivedCount.fetch_add(1);
        }
    });

    // Toggle HUD feature on
    ASSERT_TRUE(fm->setFeature(Features::kHud, true));

    EXPECT_TRUE(WaitForCondition([&] { return receivedCount.load() >= 1; }));
    EXPECT_EQ(receivedCount.load(), 1);
    EXPECT_EQ(capturedId, emu->GetContext()->emulatorId);
    EXPECT_EQ(capturedFeatureId, Features::kHud);
    EXPECT_TRUE(capturedEnabled);

    // Toggle HUD feature back off
    ASSERT_TRUE(fm->setFeature(Features::kHud, false));

    EXPECT_TRUE(WaitForCondition([&] { return receivedCount.load() >= 2; }));
    EXPECT_EQ(receivedCount.load(), 2);
    EXPECT_EQ(capturedFeatureId, Features::kHud);
    EXPECT_FALSE(capturedEnabled);

    mc.RemoveObserverById(NC_FEATURE_CHANGED, obsId);
    EmulatorTestHelper::CleanupEmulator(emu);
}

TEST(FeatureChangedNotification_Test, CacheIsConsistentWhenObserverFires)
{
    Emulator* emu = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emu, nullptr);

    FeatureManager* fm = emu->GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    std::atomic<bool> observedStateAtNotification{false};
    std::atomic<bool> observerFired{false};

    uint64_t obsId = mc.AddObserver(NC_FEATURE_CHANGED, [&](int, Message* msg) {
        if (!msg) return;
        auto* payload = dynamic_cast<FeatureChangedPayload*>(msg->obj);
        if (payload && payload->emulatorId == emu->GetContext()->emulatorId &&
            payload->featureId == Features::kHud)
        {
            // Verify that fm->isEnabled() is already true when this fires
            observedStateAtNotification.store(fm->isEnabled(Features::kHud));
            observerFired.store(true);
        }
    });

    ASSERT_TRUE(fm->setFeature(Features::kHud, true));

    EXPECT_TRUE(WaitForCondition([&] { return observerFired.load(); }));
    EXPECT_TRUE(observedStateAtNotification.load())
        << "FeatureManager cache must be up-to-date BEFORE NC_FEATURE_CHANGED observer fires";

    mc.RemoveObserverById(NC_FEATURE_CHANGED, obsId);
    EmulatorTestHelper::CleanupEmulator(emu);
}

TEST(FeatureChangedNotification_Test, LoadFromFilePostsBulkNotificationWithEmptyFeatureId)
{
    Emulator* emu = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emu, nullptr);

    FeatureManager* fm = emu->GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    // Prepare a temporary features file in test scratch path
    std::string scratchDir = TestPathHelper::GetTestScratchPath("feature_notify_test");
    std::filesystem::create_directories(scratchDir);
    std::string testIniPath = scratchDir + "/features_test.ini";

    {
        std::ofstream ofs(testIniPath);
        ofs << "[hud]\n";
        ofs << "state = on\n";
    }

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    std::atomic<bool> bulkReloadReceived{false};
    std::string lastFeatureId;

    uint64_t obsId = mc.AddObserver(NC_FEATURE_CHANGED, [&](int, Message* msg) {
        if (!msg) return;
        auto* payload = dynamic_cast<FeatureChangedPayload*>(msg->obj);
        if (payload && payload->emulatorId == emu->GetContext()->emulatorId)
        {
            lastFeatureId = payload->featureId;
            if (payload->featureId.empty())
            {
                bulkReloadReceived.store(true);
            }
        }
    });

    fm->loadFromFile(testIniPath);

    EXPECT_TRUE(WaitForCondition([&] { return bulkReloadReceived.load(); }));

    EXPECT_TRUE(bulkReloadReceived.load()) << "loadFromFile must post NC_FEATURE_CHANGED with empty featureId";
    EXPECT_TRUE(fm->isEnabled(Features::kHud));

    mc.RemoveObserverById(NC_FEATURE_CHANGED, obsId);
    EmulatorTestHelper::CleanupEmulator(emu);

    std::error_code ec;
    std::filesystem::remove_all(scratchDir, ec);
}

/// endregion </NC_FEATURE_CHANGED notifications>
