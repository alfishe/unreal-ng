#include <gtest/gtest.h>

#include "hudmodel.h"
#include "hudsnapshot.h"
#include "hudsurface.h"

#include <3rdparty/message-center/messagecenter.h>
#include <emulator/emulator.h>
#include <emulator/notifications.h>
#include <emulator/platform.h>

#include <chrono>
#include <thread>
#include "_helpers/testtiminghelper.h"

class HudModel_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _id = unreal::UUID::Generate();
    }

    void TearDown() override
    {
    }

    unreal::UUID _id;
};

// --- Direct Producer API Tests ---

TEST_F(HudModel_Test, InitialStateIsEmptyAndEnabledByDefault)
{
    HudModel model(nullptr);

    EXPECT_TRUE(model.isEnabled());
    auto snap = model.snapshot();
    ASSERT_NE(snap, nullptr);
    EXPECT_EQ(snap->generation, 0u);
    EXPECT_TRUE(snap->elements.empty());
}

TEST_F(HudModel_Test, NotifyCreatesToastAndIncrementsGeneration)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    uint64_t genBefore = model.generation();

    HudToastRequest req;
    req.title = "Test Title";
    req.body = "Test Body";
    req.icon = "info";
    req.priority = HudPriority::High;
    req.ttl = std::chrono::milliseconds(3000);

    std::string id = model.notify(req);
    EXPECT_FALSE(id.empty());
    EXPECT_GT(model.generation(), genBefore);

    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    const auto& el = snap->elements[0];
    EXPECT_EQ(el.id, id);
    EXPECT_EQ(el.title, "Test Title");
    EXPECT_EQ(el.body, "Test Body");
    EXPECT_EQ(el.icon, "info");
    EXPECT_EQ(el.priority, HudPriority::High);
}

TEST_F(HudModel_Test, DismissRemovesToast)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    HudToastRequest req;
    req.title = "To Dismiss";
    std::string id = model.notify(req);
    EXPECT_EQ(model.snapshot()->elements.size(), 1u);

    model.dismiss(id);
    EXPECT_EQ(model.snapshot()->elements.size(), 0u);
}

TEST_F(HudModel_Test, DedupKeyCoalescesToasts)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    HudToastRequest req1;
    req1.title = "Drive A";
    req1.body = "Saved 1";
    req1.dedupKey = "disk-written/A";
    std::string id1 = model.notify(req1);

    HudToastRequest req2;
    req2.title = "Drive A";
    req2.body = "Saved 2";
    req2.dedupKey = "disk-written/A";
    std::string id2 = model.notify(req2);

    EXPECT_EQ(id1, id2);
    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    EXPECT_EQ(snap->elements[0].coalesced, 2u);
    EXPECT_EQ(snap->elements[0].body, "Saved 2");

    // Third coalesce
    model.notify(req2);
    snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    EXPECT_EQ(snap->elements[0].coalesced, 3u);
}

TEST_F(HudModel_Test, PriorityOrderingInSnapshot)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    HudToastRequest low;
    low.title = "Low";
    low.priority = HudPriority::Low;
    model.notify(low);

    HudToastRequest high;
    high.title = "High";
    high.priority = HudPriority::High;
    model.notify(high);

    HudToastRequest normal;
    normal.title = "Normal";
    normal.priority = HudPriority::Normal;
    model.notify(normal);

    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 3u);
    EXPECT_EQ(snap->elements[0].title, "High");
    EXPECT_EQ(snap->elements[1].title, "Normal");
    EXPECT_EQ(snap->elements[2].title, "Low");
}

TEST_F(HudModel_Test, QueueLimitsEnforced)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);
    model.setLimits(2, 3); // max 2 visible, 3 in queue

    for (int i = 0; i < 5; ++i)
    {
        HudToastRequest req;
        req.title = "Toast " + std::to_string(i);
        req.priority = HudPriority::Normal;
        model.notify(req);
    }

    auto snap = model.snapshot();
    // Only 2 visible in snapshot
    EXPECT_EQ(snap->elements.size(), 2u);
}

TEST_F(HudModel_Test, ExpireRemovesElapsedToasts)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    HudToastRequest req;
    req.title = "Expiring";
    req.ttl = std::chrono::milliseconds(100);
    model.notify(req);

    EXPECT_EQ(model.snapshot()->elements.size(), 1u);

    // Call expire with future timestamp (+200ms)
    auto future = HudClock::now() + std::chrono::milliseconds(200);
    model.expire(future);

    EXPECT_EQ(model.snapshot()->elements.size(), 0u);
}

TEST_F(HudModel_Test, SetAndClearIndicator)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    model.setIndicator("speed", HudState::Active, "2x", "Speed");

    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    EXPECT_EQ(snap->elements[0].id, "ind/speed");
    EXPECT_EQ(snap->elements[0].kind, HudKind::Indicator);
    EXPECT_EQ(snap->elements[0].state, HudState::Active);
    EXPECT_EQ(snap->elements[0].value, "2x");

    // Setting to Off removes it from published snapshot
    model.setIndicator("speed", HudState::Off);
    snap = model.snapshot();
    EXPECT_EQ(snap->elements.size(), 0u);

    // Re-activating and clearing
    model.setIndicator("speed", HudState::Active, "4x");
    EXPECT_EQ(model.snapshot()->elements.size(), 1u);

    model.clearIndicator("speed");
    EXPECT_EQ(model.snapshot()->elements.size(), 0u);
}

TEST_F(HudModel_Test, FeatureGatingZeroCostWhenDisabled)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    HudToastRequest req;
    req.title = "Visible";
    model.notify(req);
    model.setIndicator("test", HudState::Active, "val");
    EXPECT_GE(model.snapshot()->elements.size(), 1u);

    // Turn feature off
    model.onFeatureChanged(false);
    EXPECT_FALSE(model.isEnabled());

    auto snap = model.snapshot();
    EXPECT_TRUE(snap->elements.empty());

    // New notifications rejected while off
    std::string id = model.notify(req);
    EXPECT_TRUE(id.empty());
    EXPECT_TRUE(model.snapshot()->elements.empty());

    // Turn feature back on
    model.onFeatureChanged(true);
    EXPECT_TRUE(model.isEnabled());
}

// --- MessageCenter Event Mapping Tests ---

TEST_F(HudModel_Test, EventMapping_FddState)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    FDDStateInfo state;
    state.driveId = 0; // Drive A
    state.side = 0;
    state.track = 42;
    state.sector = 0;
    state.motorOn = true;

    mc.Post(NC_FDD_STATE_CHANGED, new FDDStatePayload(_id.toString(), state));

    auto start = std::chrono::steady_clock::now();
    while (model.snapshot()->elements.empty() &&
           std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    EXPECT_EQ(snap->elements[0].id, "ind/fdd");
    EXPECT_EQ(snap->elements[0].state, HudState::Active);
    EXPECT_EQ(snap->elements[0].value, "A: H:0 T:42 S:00");
    EXPECT_EQ(snap->elements[0].icon, "floppy");
    EXPECT_TRUE(snap->elements[0].monospace);

    // Motor off -> indicator cleared (disappears when FDC reported motor off)
    state.motorOn = false;
    mc.Post(NC_FDD_STATE_CHANGED, new FDDStatePayload(_id.toString(), state));

    start = std::chrono::steady_clock::now();
    while (!model.snapshot()->elements.empty() &&
           std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    snap = model.snapshot();
    EXPECT_TRUE(snap->elements.empty());
}

TEST_F(HudModel_Test, EventMapping_FddState_FirmPlaceholdersAndDriveIndication)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    // Test across all 4 drives (A, B, C, D) with various single-digit and double-digit tracks/sectors
    struct TestCase {
        uint8_t driveId;
        uint8_t side;
        uint8_t track;
        uint8_t sector;
        std::string expected;
    };

    std::vector<TestCase> cases = {
        {0, 0, 0, 1, "A: H:0 T:00 S:01"},
        {1, 1, 5, 9, "B: H:1 T:05 S:09"},
        {2, 0, 79, 16, "C: H:0 T:79 S:16"},
        {3, 1, 42, 8, "D: H:1 T:42 S:08"}
    };

    for (const auto& tc : cases)
    {
        FDDStateInfo state;
        state.driveId = tc.driveId;
        state.side = tc.side;
        state.track = tc.track;
        state.sector = tc.sector;
        state.motorOn = true;

        mc.Post(NC_FDD_STATE_CHANGED, new FDDStatePayload(_id.toString(), state));

        EXPECT_TRUE(WaitForCondition([&] {
            auto s = model.snapshot();
            return !s->elements.empty() && s->elements[0].value == tc.expected;
        }));

        auto snap = model.snapshot();
        ASSERT_EQ(snap->elements.size(), 1u);
        EXPECT_EQ(snap->elements[0].value, tc.expected);
        // Firm fixed-width placeholders: all formatting must be exactly 16 chars long to prevent content drift
        EXPECT_EQ(snap->elements[0].value.length(), 16u);
        EXPECT_TRUE(snap->elements[0].monospace);
        EXPECT_EQ(snap->elements[0].icon, "floppy");
    }
}

TEST_F(HudModel_Test, EventMapping_SpeedChanged)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    // 2x speed
    mc.Post(NC_SPEED_CHANGED, new SpeedChangedPayload(_id, 2, false));

    auto start = std::chrono::steady_clock::now();
    while (model.snapshot()->elements.empty() &&
           std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    EXPECT_EQ(snap->elements[0].id, "ind/speed");
    EXPECT_EQ(snap->elements[0].value, "2x");

    // Turbo
    mc.Post(NC_SPEED_CHANGED, new SpeedChangedPayload(_id, 1, true));

    start = std::chrono::steady_clock::now();
    while (model.snapshot()->elements[0].value != "TURBO" &&
           std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    snap = model.snapshot();
    EXPECT_EQ(snap->elements[0].value, "TURBO");

    // 1x speed normal -> indicator off
    mc.Post(NC_SPEED_CHANGED, new SpeedChangedPayload(_id, 1, false));

    start = std::chrono::steady_clock::now();
    while (!model.snapshot()->elements.empty() &&
           std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    snap = model.snapshot();
    EXPECT_TRUE(snap->elements.empty());
}

TEST_F(HudModel_Test, EventMapping_FileLoaded)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    // Success load
    mc.Post(NC_FILE_LOADED, new FileLoadedPayload(_id, "snapshot", "/games/elite.sna", true));

    auto start = std::chrono::steady_clock::now();
    EXPECT_TRUE(WaitForCondition([&] { return !model.snapshot()->elements.empty(); }));

    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    EXPECT_EQ(snap->elements[0].title, "Snapshot Loaded");
    EXPECT_EQ(snap->elements[0].body, "elite.sna");
    EXPECT_EQ(snap->elements[0].priority, HudPriority::Normal);

    // Failed load
    mc.Post(NC_FILE_LOADED, new FileLoadedPayload(_id, "tape", "/games/bad.tap", false));

    EXPECT_TRUE(WaitForCondition([&] { return model.snapshot()->elements.size() >= 2; }));

    snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 2u);
    // Higher priority (Load failed) must sort first
    EXPECT_EQ(snap->elements[0].title, "Tape Load Failed");
    EXPECT_EQ(snap->elements[0].body, "bad.tap");
    EXPECT_EQ(snap->elements[0].priority, HudPriority::High);
}

TEST_F(HudModel_Test, EventMapping_RecordingState)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    // Start recording
    mc.Post(NC_RECORDING_STATE, new RecordingStatePayload(_id, true, "/tmp/rec.mp4"));

    auto start = std::chrono::steady_clock::now();
    while (model.snapshot()->elements.empty() &&
           std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    EXPECT_EQ(snap->elements[0].id, "ind/rec");
    EXPECT_EQ(snap->elements[0].state, HudState::Active);

    // Stop recording
    mc.Post(NC_RECORDING_STATE, new RecordingStatePayload(_id, false, "/tmp/rec.mp4"));

    start = std::chrono::steady_clock::now();
    while ((model.snapshot()->elements.empty() ||
            model.snapshot()->elements[0].kind != HudKind::Toast) &&
           std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    EXPECT_EQ(snap->elements[0].kind, HudKind::Toast);
    EXPECT_EQ(snap->elements[0].title, "Recording saved");
    EXPECT_EQ(snap->elements[0].body, "rec.mp4");
}

TEST_F(HudModel_Test, EventMapping_EmulatorStateChange)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    // Paused
    mc.Post(NC_EMULATOR_STATE_CHANGE, new EmulatorStateChangePayload(_id, StatePaused));

    auto start = std::chrono::steady_clock::now();
    while (model.snapshot()->elements.empty() &&
           std::chrono::steady_clock::now() - start < std::chrono::milliseconds(500))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    EXPECT_EQ(snap->elements[0].id, "ind/pause");
    EXPECT_EQ(snap->elements[0].state, HudState::Active);

    // Resumed -> changes indicator to EXECUTE (with auto-expire)
    mc.Post(NC_EMULATOR_STATE_CHANGE, new EmulatorStateChangePayload(_id, StateRun));

    EXPECT_TRUE(WaitForCondition([&] {
        auto s = model.snapshot();
        return !s->elements.empty() && s->elements[0].id == "ind/pause" && s->elements[0].value == "EXECUTE";
    }));

    // Expire cleans up the EXECUTE indicator
    model.expire(HudClock::now() + std::chrono::milliseconds(2000));
    snap = model.snapshot();
    EXPECT_TRUE(snap->elements.empty());
}

TEST_F(HudModel_Test, EventMapping_BreakpointTriggered)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    mc.Post(NC_EXECUTION_BREAKPOINT, new BreakpointTriggeredPayload(_id, 3, 0x8000));

    EXPECT_TRUE(WaitForCondition([&] {
        return model.snapshot()->elements.size() >= 2;
    }));

    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 2u);

    // Indicator
    auto indIt = std::find_if(snap->elements.begin(), snap->elements.end(), [](const HudElement& e) {
        return e.kind == HudKind::Indicator && e.id == "ind/pause";
    });
    ASSERT_NE(indIt, snap->elements.end());
    EXPECT_EQ(indIt->value, "BREAKPOINT");

    // Toast
    auto toastIt = std::find_if(snap->elements.begin(), snap->elements.end(), [](const HudElement& e) {
        return e.kind == HudKind::Toast;
    });
    ASSERT_NE(toastIt, snap->elements.end());
    EXPECT_EQ(toastIt->title, "Breakpoint Hit");
    EXPECT_NE(toastIt->body.find("#8000"), std::string::npos);
    EXPECT_EQ(toastIt->icon, "breakpoint");
}

TEST_F(HudModel_Test, EventMapping_TapeLoaded)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    mc.Post(NC_FILE_LOADED, new FileLoadedPayload(_id, "tape", "/games/jetpac.tap", true));

    EXPECT_TRUE(WaitForCondition([&] {
        return !model.snapshot()->elements.empty();
    }));

    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    EXPECT_EQ(snap->elements[0].kind, HudKind::Toast);
    EXPECT_EQ(snap->elements[0].title, "Tape Loaded");
    EXPECT_EQ(snap->elements[0].body, "jetpac.tap");
    EXPECT_EQ(snap->elements[0].icon, "tape");
}

TEST_F(HudModel_Test, Augmentation_SetImageAndZIndexSorting)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    // Create 2x2 RGBA image
    uint8_t pixels[16] = {
        255, 0, 0, 255,   0, 255, 0, 255,
        0, 0, 255, 255,   255, 255, 255, 128
    };
    auto img = HudImageBuffer::CreateRgba(2, 2, pixels);
    ASSERT_NE(img, nullptr);
    EXPECT_EQ(img->width(), 2);
    EXPECT_EQ(img->height(), 2);
    EXPECT_EQ(img->format(), HudPixelFormat::RGBA8888);

    HudImageRequest imgReq;
    imgReq.image = img;
    imgReq.rect = HudRect{10, 20, 32, 32};
    imgReq.coordSpace = HudCoordSpace::Picture;
    imgReq.zIndex = -5; // Behind default indicators

    model.setImage("sprite/hero", imgReq);

    // Also add an indicator
    model.setIndicator("speed", HudState::Active, "TURBO");

    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 2u);

    // Elements must be sorted by zIndex ascending: image (-5) then indicator (100)
    EXPECT_EQ(snap->elements[0].id, "sprite/hero");
    EXPECT_EQ(snap->elements[0].kind, HudKind::Image);
    EXPECT_EQ(snap->elements[0].zIndex, -5);
    EXPECT_EQ(snap->elements[0].coordSpace, HudCoordSpace::Picture);

    EXPECT_EQ(snap->elements[1].id, "ind/speed");
    EXPECT_EQ(snap->elements[1].kind, HudKind::Indicator);
    EXPECT_EQ(snap->elements[1].zIndex, 100);
}

TEST_F(HudModel_Test, Augmentation_SetTileAndTilemap)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    uint8_t tilesetPixels[64 * 4] = {0}; // 8x8 RGBA
    auto tileset = HudImageBuffer::CreateRgba(8, 8, tilesetPixels);

    HudTileRequest tileReq;
    tileReq.tileset = tileset;
    tileReq.rect = HudRect{0, 0, 8, 8};
    tileReq.srcRect = HudRect{0, 0, 8, 8};
    tileReq.zIndex = 10;
    model.setTile("tile/grass", tileReq);

    auto tm = std::make_shared<HudTilemapData>();
    tm->tileset = tileset;
    tm->cols = 2;
    tm->rows = 2;
    tm->tileIndices = {0, 1, 0, 1};

    HudTilemapRequest tmReq;
    tmReq.tilemap = tm;
    tmReq.destRect = HudRect{0, 0, 16, 16};
    model.setTilemap("tilemap/level1", tmReq);

    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 2u);

    auto tileIt = std::find_if(snap->elements.begin(), snap->elements.end(), [](const HudElement& e) {
        return e.kind == HudKind::Tile && e.id == "tile/grass";
    });
    ASSERT_NE(tileIt, snap->elements.end());

    auto tmIt = std::find_if(snap->elements.begin(), snap->elements.end(), [](const HudElement& e) {
        return e.kind == HudKind::Tilemap && e.id == "tilemap/level1";
    });
    ASSERT_NE(tmIt, snap->elements.end());
    EXPECT_EQ(tmIt->tilemap->cols, 2);
}

TEST_F(HudModel_Test, Augmentation_SetTextAndBatching)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    uint64_t genStart = model.generation();

    model.beginBatch();

    HudTextRequest txt1;
    txt1.text = "Score: 1000";
    txt1.rect = HudRect{10, 10, 100, 20};
    model.setText("txt/score", txt1);

    HudTextRequest txt2;
    txt2.text = "Lives: 3";
    txt2.rect = HudRect{10, 35, 100, 20};
    model.setText("txt/lives", txt2);

    // During batch, no snapshot publish
    EXPECT_EQ(model.generation(), genStart);

    model.endBatch();

    // After batch ends, exactly one publish
    EXPECT_EQ(model.generation(), genStart + 1);

    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 2u);

    // Clear augmentations
    model.clearAugmentations();
    EXPECT_TRUE(model.snapshot()->elements.empty());
}

TEST_F(HudModel_Test, MapToDeviceRectCalculations)
{
    HudSurface surface;
    surface.outputRect = HudRect{0, 0, 1920, 1080};
    surface.imageRect = HudRect{240, 60, 1440, 960}; // 4:3 letterboxed
    surface.dpr = 1;

    // 1. Picture space: srcRect (128, 96, 64, 48) on native 256x192 screen
    // Must map to exactly the center of imageRect
    HudRect picRect{128, 96, 64, 48};
    HudRect mappedPic = MapToDeviceRect(picRect, HudCoordSpace::Picture, HudPoint{256, 192}, surface);

    EXPECT_EQ(mappedPic.x, 240 + (128 * 1440) / 256);
    EXPECT_EQ(mappedPic.y, 60 + (96 * 960) / 192);
    EXPECT_EQ(mappedPic.w, (64 * 1440) / 256);
    EXPECT_EQ(mappedPic.h, (48 * 960) / 192);

    // 2. Output space: relative to outputRect
    HudRect outRect{50, 50, 100, 30};
    HudRect mappedOut = MapToDeviceRect(outRect, HudCoordSpace::Output, HudPoint{0, 0}, surface);
    EXPECT_EQ(mappedOut.x, 50);
    EXPECT_EQ(mappedOut.y, 50);
    EXPECT_EQ(mappedOut.w, 100);
    EXPECT_EQ(mappedOut.h, 30);
}

TEST_F(HudModel_Test, HudTheme_RegistryAndResolution)
{
    auto themes = HudTheme::AvailableThemes();
    EXPECT_EQ(themes.size(), 6u);

    const auto& dark = HudTheme::FromId(HudThemeId::DarkGlass);
    EXPECT_EQ(dark.id, HudThemeId::DarkGlass);
    EXPECT_EQ(dark.name, "Dark Glass");

    const auto& retro = HudTheme::FromId(HudThemeId::RetroZX);
    EXPECT_EQ(retro.id, HudThemeId::RetroZX);
    EXPECT_EQ(retro.name, "Retro ZX Spectrum");
    // Authentic Sinclair ZX Spectrum palette checks
    EXPECT_EQ(retro.toast.frame.borderColor, 0xFF00D7D7u); // Bright cyan border
    EXPECT_EQ(retro.indicator.frame.borderColor, 0xFF00D700u); // Bright green border
    EXPECT_EQ(retro.alert.frame.borderColor, 0xFFD70000u); // Bright red border

    const auto& cyber = HudTheme::FromId(HudThemeId::Cyberpunk);
    EXPECT_EQ(cyber.id, HudThemeId::Cyberpunk);
    EXPECT_EQ(cyber.name, "Cyberpunk");
    EXPECT_EQ(cyber.defaultTile.content.accentColor, 0xFFFF007Fu); // Hot magenta
    EXPECT_EQ(cyber.toast.frame.borderColor, 0xCC00F0FFu); // Neon cyan toast border

    const auto& amber = HudTheme::FromId(HudThemeId::AmberCRT);
    EXPECT_EQ(amber.id, HudThemeId::AmberCRT);
    EXPECT_EQ(amber.name, "Amber CRT");

    const auto& emerald = HudTheme::FromId(HudThemeId::EmeraldCRT);
    EXPECT_EQ(emerald.id, HudThemeId::EmeraldCRT);
    EXPECT_EQ(emerald.name, "Emerald CRT");

    const auto& light = HudTheme::FromId(HudThemeId::LightModern);
    EXPECT_EQ(light.id, HudThemeId::LightModern);
    EXPECT_EQ(light.name, "Light Modern");

    // Lookup by name
    EXPECT_EQ(HudTheme::FromName("Retro ZX").id, HudThemeId::RetroZX);
    EXPECT_EQ(HudTheme::FromName("cyberpunk").id, HudThemeId::Cyberpunk);
    EXPECT_EQ(HudTheme::FromName("nonexistent").id, HudThemeId::DarkGlass);

    // Style resolution: default, specialized, and custom
    EXPECT_EQ(dark.resolveStyle("").frame.borderRadius, dark.defaultTile.frame.borderRadius);
    EXPECT_EQ(dark.resolveStyle("toast").frame.borderRadius, dark.toast.frame.borderRadius);
    EXPECT_EQ(dark.resolveStyle("indicator").content.accentColor, dark.indicator.content.accentColor);
    EXPECT_EQ(dark.resolveStyle("alert").frame.borderColor, dark.alert.frame.borderColor);

    // Custom style resolution
    HudTheme customTheme = dark;
    HudTileStyle myCustom;
    myCustom.frame.borderRadius = 42.0f;
    myCustom.content.titleFontSize = 24.0f;
    customTheme.customStyles["custom_card"] = myCustom;

    EXPECT_FLOAT_EQ(customTheme.resolveStyle("custom_card").frame.borderRadius, 42.0f);
    EXPECT_FLOAT_EQ(customTheme.resolveStyle("custom_card").content.titleFontSize, 24.0f);
    EXPECT_FLOAT_EQ(customTheme.resolveStyle("unknown_key").frame.borderRadius, dark.defaultTile.frame.borderRadius);
}

TEST_F(HudModel_Test, HudModel_ThemeAndScaleFactorPublishing)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    // Default theme and scale factor
    EXPECT_EQ(model.theme(), HudThemeId::DarkGlass);
    EXPECT_FLOAT_EQ(model.scaleFactor(), 2.0f);

    auto snap = model.snapshot();
    EXPECT_EQ(snap->themeId, HudThemeId::DarkGlass);
    EXPECT_FLOAT_EQ(snap->scaleFactor, 2.0f);

    // Change theme
    uint64_t genBefore = model.generation();
    model.setTheme(HudThemeId::Cyberpunk);
    EXPECT_EQ(model.theme(), HudThemeId::Cyberpunk);
    EXPECT_EQ(model.generation(), genBefore + 1);

    snap = model.snapshot();
    EXPECT_EQ(snap->themeId, HudThemeId::Cyberpunk);

    // Change scale factor
    genBefore = model.generation();
    model.setScaleFactor(2.5f);
    EXPECT_FLOAT_EQ(model.scaleFactor(), 2.5f);
    EXPECT_EQ(model.generation(), genBefore + 1);

    snap = model.snapshot();
    EXPECT_FLOAT_EQ(snap->scaleFactor, 2.5f);

    // Add elements with custom styleId
    HudTileRequest tileReq;
    tileReq.styleId = "custom_neon";
    tileReq.rect = HudRect{10, 10, 80, 80};
    model.setTile("tile/1", tileReq);

    HudTextRequest textReq;
    textReq.styleId = "alert";
    textReq.text = "CRITICAL WARNING";
    textReq.rect = HudRect{10, 100, 200, 30};
    model.setText("text/warning", textReq);

    snap = model.snapshot();
    auto tileIt = std::find_if(snap->elements.begin(), snap->elements.end(), [](const HudElement& e) {
        return e.id == "tile/1";
    });
    ASSERT_NE(tileIt, snap->elements.end());
    EXPECT_EQ(tileIt->styleId, "custom_neon");

    auto textIt = std::find_if(snap->elements.begin(), snap->elements.end(), [](const HudElement& e) {
        return e.id == "text/warning";
    });
    ASSERT_NE(textIt, snap->elements.end());
    EXPECT_EQ(textIt->styleId, "alert");
}

TEST_F(HudModel_Test, PredefinedTilePositions_PlacementCalculations)
{
    HudRect viewport{100, 50, 800, 600};
    int tileW = 200;
    int tileH = 100;
    HudMargins margins{20, 15, 20, 15};

    // 1. All Corners
    // TopLeft: x = 100 + 20 = 120, y = 50 + 15 = 65
    HudRect tl = ComputeTilePlacement(HudTilePosition::TopLeft, tileW, tileH, viewport, margins);
    EXPECT_EQ(tl.x, 120);
    EXPECT_EQ(tl.y, 65);
    EXPECT_EQ(tl.w, 200);
    EXPECT_EQ(tl.h, 100);

    // TopRight: x = 100 + 800 - 200 - 20 = 680, y = 65
    HudRect tr = ComputeTilePlacement(HudTilePosition::TopRight, tileW, tileH, viewport, margins);
    EXPECT_EQ(tr.x, 680);
    EXPECT_EQ(tr.y, 65);
    EXPECT_EQ(tr.w, 200);
    EXPECT_EQ(tr.h, 100);

    // BottomLeft: x = 120, y = 50 + 600 - 100 - 15 = 535
    HudRect bl = ComputeTilePlacement(HudTilePosition::BottomLeft, tileW, tileH, viewport, margins);
    EXPECT_EQ(bl.x, 120);
    EXPECT_EQ(bl.y, 535);
    EXPECT_EQ(bl.w, 200);
    EXPECT_EQ(bl.h, 100);

    // BottomRight: x = 680, y = 535
    HudRect br = ComputeTilePlacement(HudTilePosition::BottomRight, tileW, tileH, viewport, margins);
    EXPECT_EQ(br.x, 680);
    EXPECT_EQ(br.y, 535);
    EXPECT_EQ(br.w, 200);
    EXPECT_EQ(br.h, 100);

    // 2. Mid Top: x = 100 + (800 - 200) / 2 = 400, y = 65
    HudRect mt = ComputeTilePlacement(HudTilePosition::MidTop, tileW, tileH, viewport, margins);
    EXPECT_EQ(mt.x, 400);
    EXPECT_EQ(mt.y, 65);
    EXPECT_EQ(mt.w, 200);
    EXPECT_EQ(mt.h, 100);

    // 3. Mid Bottom: x = 400, y = 535
    HudRect mb = ComputeTilePlacement(HudTilePosition::MidBottom, tileW, tileH, viewport, margins);
    EXPECT_EQ(mb.x, 400);
    EXPECT_EQ(mb.y, 535);
    EXPECT_EQ(mb.w, 200);
    EXPECT_EQ(mb.h, 100);

    // 4. Center of Viewport: x = 400, y = 50 + (600 - 100) / 2 = 300
    HudRect center = ComputeTilePlacement(HudTilePosition::Center, tileW, tileH, viewport, margins);
    EXPECT_EQ(center.x, 400);
    EXPECT_EQ(center.y, 300);
    EXPECT_EQ(center.w, 200);
    EXPECT_EQ(center.h, 100);

    // Fine-tuning offsets
    HudRect offsetCenter = ComputeTilePlacement(HudTilePosition::Center, tileW, tileH, viewport, margins, 10, -5);
    EXPECT_EQ(offsetCenter.x, 410);
    EXPECT_EQ(offsetCenter.y, 295);

    // Custom coordinates fallback
    HudRect custom = ComputeTilePlacement(HudTilePosition::Custom, tileW, tileH, viewport, margins, 33, 44);
    EXPECT_EQ(custom.x, 100 + 33);
    EXPECT_EQ(custom.y, 50 + 44);
}

TEST_F(HudModel_Test, PredefinedTilePositions_StringConversions)
{
    EXPECT_EQ(HudTilePositionToString(HudTilePosition::TopLeft), "top_left");
    EXPECT_EQ(HudTilePositionToString(HudTilePosition::TopRight), "top_right");
    EXPECT_EQ(HudTilePositionToString(HudTilePosition::BottomLeft), "bottom_left");
    EXPECT_EQ(HudTilePositionToString(HudTilePosition::BottomRight), "bottom_right");
    EXPECT_EQ(HudTilePositionToString(HudTilePosition::MidTop), "mid_top");
    EXPECT_EQ(HudTilePositionToString(HudTilePosition::MidBottom), "mid_bottom");
    EXPECT_EQ(HudTilePositionToString(HudTilePosition::Center), "center");

    EXPECT_EQ(HudTilePositionFromString("top_left"), HudTilePosition::TopLeft);
    EXPECT_EQ(HudTilePositionFromString("topright"), HudTilePosition::TopRight);
    EXPECT_EQ(HudTilePositionFromString("BOTTOM_LEFT"), HudTilePosition::BottomLeft);
    EXPECT_EQ(HudTilePositionFromString("bottom-right"), HudTilePosition::BottomRight);
    EXPECT_EQ(HudTilePositionFromString("mid_top"), HudTilePosition::MidTop);
    EXPECT_EQ(HudTilePositionFromString("mid-bottom"), HudTilePosition::MidBottom);
    EXPECT_EQ(HudTilePositionFromString("center"), HudTilePosition::Center);
    EXPECT_EQ(HudTilePositionFromString("unknown"), HudTilePosition::Custom);
}

TEST_F(HudModel_Test, PredefinedTilePositions_ModelPublishingAndRequests)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    // Default positions
    EXPECT_EQ(model.toastPosition(), HudTilePosition::MidBottom);
    EXPECT_EQ(model.indicatorPosition(), HudTilePosition::TopRight);

    // Change positions
    model.setToastPosition(HudTilePosition::TopRight);
    model.setIndicatorPosition(HudTilePosition::TopLeft);
    EXPECT_EQ(model.toastPosition(), HudTilePosition::TopRight);
    EXPECT_EQ(model.indicatorPosition(), HudTilePosition::TopLeft);

    auto snap = model.snapshot();
    EXPECT_EQ(snap->toastPosition, HudTilePosition::TopRight);
    EXPECT_EQ(snap->indicatorPosition, HudTilePosition::TopLeft);

    // Add a tile with predefined position: Center of viewport
    HudTileRequest tileReq;
    tileReq.position = HudTilePosition::Center;
    tileReq.rect = HudRect{0, 0, 160, 80}; // dimensions 160x80
    model.setTile("tile/center_tile", tileReq);

    // Add a tile in TopLeft corner
    HudTileRequest tlReq;
    tlReq.position = HudTilePosition::TopLeft;
    tlReq.rect = HudRect{0, 0, 100, 50};
    model.setTile("tile/tl_tile", tlReq);

    snap = model.snapshot();
    auto cIt = std::find_if(snap->elements.begin(), snap->elements.end(), [](const HudElement& e) {
        return e.id == "tile/center_tile";
    });
    ASSERT_NE(cIt, snap->elements.end());
    EXPECT_EQ(cIt->position, HudTilePosition::Center);
    EXPECT_EQ(cIt->rect.w, 160);
    EXPECT_EQ(cIt->rect.h, 80);

    auto tlIt = std::find_if(snap->elements.begin(), snap->elements.end(), [](const HudElement& e) {
        return e.id == "tile/tl_tile";
    });
    ASSERT_NE(tlIt, snap->elements.end());
    EXPECT_EQ(tlIt->position, HudTilePosition::TopLeft);
}

TEST_F(HudModel_Test, EventMapping_FddDiskInserted_SingleCombinedNotification)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    // 1. Post NC_FDD_DISK_INSERTED for drive 0 (Drive A) with path
    mc.Post(NC_FDD_DISK_INSERTED, new FDDDiskPayload(_id.toString(), 0, "/games/elite.trd"));

    EXPECT_TRUE(WaitForCondition([&] { return !model.snapshot()->elements.empty(); }));

    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    EXPECT_EQ(snap->elements[0].title, "Disk inserted to disk drive A");
    EXPECT_EQ(snap->elements[0].body, "elite.trd");
    EXPECT_EQ(snap->elements[0].icon, "floppy");
    EXPECT_EQ(snap->elements[0].coalesced, 0u);

    // 2. Rapid duplicate NC_FDD_DISK_INSERTED must NOT create a second toast and must NOT increment coalesced counter
    mc.Post(NC_FDD_DISK_INSERTED, new FDDDiskPayload(_id.toString(), 0, "/games/elite.trd"));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    EXPECT_EQ(snap->elements[0].title, "Disk inserted to disk drive A");
    EXPECT_EQ(snap->elements[0].body, "elite.trd");
    EXPECT_EQ(snap->elements[0].coalesced, 0u); // no x2 badge

    // 3. NC_FILE_LOADED with kind="disk" and ok=true must NOT create an extra toast
    mc.Post(NC_FILE_LOADED, new FileLoadedPayload(_id, "disk", "/games/elite.trd", true));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u); // still exactly 1 toast
}

TEST_F(HudModel_Test, EventMapping_DiskEjectThenInsert_CombinesIntoSingleToast)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    // 1. Eject disk
    mc.Post(NC_FDD_DISK_EJECTED, new FDDDiskPayload(_id.toString(), 0, ""));
    EXPECT_TRUE(WaitForCondition([&] { return !model.snapshot()->elements.empty(); }));

    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    EXPECT_EQ(snap->elements[0].title, "Drive A");
    EXPECT_EQ(snap->elements[0].body, "Disk ejected");

    // 2. Insert new disk: must replace the eject toast without duplicate or coalesced badge
    mc.Post(NC_FDD_DISK_INSERTED, new FDDDiskPayload(_id.toString(), 0, "/games/dizzy.trd"));
    EXPECT_TRUE(WaitForCondition([&] {
        auto s = model.snapshot();
        return !s->elements.empty() && s->elements[0].title == "Disk inserted to disk drive A";
    }));

    snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    EXPECT_EQ(snap->elements[0].title, "Disk inserted to disk drive A");
    EXPECT_EQ(snap->elements[0].body, "dizzy.trd");
    EXPECT_EQ(snap->elements[0].coalesced, 0u);
}

TEST_F(HudModel_Test, IndicatorBorderRadius_ConsistentRoundedBoxAcrossThemes)
{
    const HudThemeId themes[] = {
        HudThemeId::DarkGlass,
        HudThemeId::RetroZX,
        HudThemeId::Cyberpunk,
        HudThemeId::AmberCRT,
        HudThemeId::EmeraldCRT,
        HudThemeId::LightModern,
    };

    for (HudThemeId themeId : themes)
    {
        const HudTheme& theme = HudTheme::FromId(themeId);
        // Indicator corner radius must be <= 12.0f to maintain rounded box shape rather than aggressive oval/capsule
        EXPECT_LE(theme.indicator.frame.borderRadius, 12.0f);
    }
}

TEST_F(HudModel_Test, EventMapping_BreakpointTriggered_HiddenFiltered)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    // Trigger a hidden breakpoint (e.g. step-over / step-out)
    mc.Post(NC_EXECUTION_BREAKPOINT, new BreakpointTriggeredPayload(_id, 5, 0x8000, true));

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Hidden breakpoint must be filtered: no HUD toast or indicator
    auto snap = model.snapshot();
    EXPECT_TRUE(snap->elements.empty());
}

TEST_F(HudModel_Test, EventMapping_CpuStep_UpdatesBreakpointToPause)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    // 1. Breakpoint hit
    mc.Post(NC_EXECUTION_BREAKPOINT, new BreakpointTriggeredPayload(_id, 1, 0x1234, false));
    EXPECT_TRUE(WaitForCondition([&] {
        auto snap = model.snapshot();
        for (const auto& e : snap->elements)
        {
            if (e.id == "ind/pause" && e.value == "BREAKPOINT")
                return true;
        }
        return false;
    }));

    // 2. CPU step finishes in paused state
    mc.Post(NC_EXECUTION_CPU_STEP, new EmulatorStateChangePayload(_id, StatePaused));
    EXPECT_TRUE(WaitForCondition([&] {
        auto snap = model.snapshot();
        for (const auto& e : snap->elements)
        {
            if (e.id == "ind/pause" && e.value == "PAUSE")
                return true;
        }
        return false;
    }));

    auto snap = model.snapshot();
    auto it = std::find_if(snap->elements.begin(), snap->elements.end(), [](const HudElement& e) {
        return e.id == "ind/pause";
    });
    ASSERT_NE(it, snap->elements.end());
    EXPECT_EQ(it->value, "PAUSE");
}

TEST_F(HudModel_Test, EventMapping_CpuStep_UpdatesBreakpointToExecuteAndExpires)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    // 1. Breakpoint hit
    mc.Post(NC_EXECUTION_BREAKPOINT, new BreakpointTriggeredPayload(_id, 1, 0x1234, false));
    EXPECT_TRUE(WaitForCondition([&] {
        auto snap = model.snapshot();
        for (const auto& e : snap->elements)
        {
            if (e.id == "ind/pause" && e.value == "BREAKPOINT")
                return true;
        }
        return false;
    }));

    // 2. CPU step finishes resuming / running
    mc.Post(NC_EXECUTION_CPU_STEP, new EmulatorStateChangePayload(_id, StateRun));
    EXPECT_TRUE(WaitForCondition([&] {
        auto snap = model.snapshot();
        for (const auto& e : snap->elements)
        {
            if (e.id == "ind/pause" && e.value == "EXECUTE")
                return true;
        }
        return false;
    }));

    // 3. Wait for EXECUTE indicator to expire (or call expire with advanced time)
    // HudTiming::IndicatorExecuteTimeout is 1500ms.
    auto futureTime = HudClock::now() + std::chrono::milliseconds(2000);
    model.expire(futureTime);

    auto snap = model.snapshot();
    auto it = std::find_if(snap->elements.begin(), snap->elements.end(), [](const HudElement& e) {
        return e.id == "ind/pause";
    });
    EXPECT_EQ(it, snap->elements.end());
}

TEST_F(HudModel_Test, GlobalStackLimit_HardEvictionAtThree)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    // Enqueue 5 toasts with distinct dedup keys
    for (int i = 1; i <= 5; ++i)
    {
        HudToastRequest req;
        req.title = "Toast " + std::to_string(i);
        req.priority = HudPriority::Normal;
        req.ttl = std::chrono::milliseconds(5000);
        req.dedupKey = "toast_" + std::to_string(i);
        model.notify(req);
    }

    auto snap = model.snapshot();
    // Maximum visible toasts allowed on screen is 3
    size_t toastCount = 0;
    for (const auto& el : snap->elements)
    {
        if (el.kind == HudKind::Toast)
            ++toastCount;
    }
    EXPECT_EQ(toastCount, 3u);
}

TEST_F(HudModel_Test, BreakpointHit_OnlySingleNotificationOnScreen_NoStacking)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    // 1. Post 2 normal toasts
    HudToastRequest req1;
    req1.title = "Notification 1";
    req1.priority = HudPriority::Normal;
    req1.ttl = std::chrono::milliseconds(5000);
    model.notify(req1);

    HudToastRequest req2;
    req2.title = "Notification 2";
    req2.priority = HudPriority::Normal;
    req2.ttl = std::chrono::milliseconds(5000);
    model.notify(req2);

    auto snap = model.snapshot();
    size_t toastCount = 0;
    for (const auto& el : snap->elements)
    {
        if (el.kind == HudKind::Toast)
            ++toastCount;
    }
    EXPECT_EQ(toastCount, 2u);

    // 2. Breakpoint hit
    mc.Post(NC_EXECUTION_BREAKPOINT, new BreakpointTriggeredPayload(_id, 1, 0x4000, false));

    EXPECT_TRUE(WaitForCondition([&] {
        auto s = model.snapshot();
        for (const auto& e : s->elements)
        {
            if (e.kind == HudKind::Toast && e.title == "Breakpoint Hit")
                return true;
        }
        return false;
    }));

    snap = model.snapshot();
    toastCount = 0;
    for (const auto& el : snap->elements)
    {
        if (el.kind == HudKind::Toast)
        {
            ++toastCount;
            EXPECT_EQ(el.title, "Breakpoint Hit");
        }
    }
    // Only single notification on screen, no stacking
    EXPECT_EQ(toastCount, 1u);
}

TEST_F(HudModel_Test, EventMapping_SnapshotLoaded_GroupsResetAndSnapshotIntoSingleToast)
{
    HudModel model(nullptr);
    model.onFeatureChanged(true);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    // 1. Snapshot loading causes a core reset first (posting NC_SYSTEM_RESET)
    mc.Post(NC_SYSTEM_RESET, new SimpleTextPayload("Core reset started"));

    EXPECT_TRUE(WaitForCondition([&] {
        auto s = model.snapshot();
        return !s->elements.empty() && s->elements[0].title == "Reset";
    }));

    auto snap = model.snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    EXPECT_EQ(snap->elements[0].title, "Reset");

    // 2. Snapshot load completes, posting NC_FILE_LOADED with kind "snapshot"
    mc.Post(NC_FILE_LOADED, new FileLoadedPayload(_id, "snapshot", "/games/jetsetwilly.sna", true));

    EXPECT_TRUE(WaitForCondition([&] {
        auto s = model.snapshot();
        return !s->elements.empty() && s->elements[0].title == "Snapshot Loaded";
    }));

    // Must be grouped into a single tile event, not multiple stacked toasts
    snap = model.snapshot();
    size_t toastCount = 0;
    for (const auto& el : snap->elements)
    {
        if (el.kind == HudKind::Toast)
            ++toastCount;
    }
    ASSERT_EQ(toastCount, 1u);
    EXPECT_EQ(snap->elements[0].title, "Snapshot Loaded");
    EXPECT_EQ(snap->elements[0].body, "jetsetwilly.sna");
    EXPECT_EQ(snap->elements[0].icon, "file");
    EXPECT_EQ(snap->elements[0].coalesced, 0u); // no x2 badge

    // 3. Even if a delayed/out-of-order NC_SYSTEM_RESET arrives afterwards,
    // it must not overwrite the Snapshot Loaded notification or spawn an extra toast
    mc.Post(NC_SYSTEM_RESET, new SimpleTextPayload("Core reset trailing"));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    snap = model.snapshot();
    toastCount = 0;
    for (const auto& el : snap->elements)
    {
        if (el.kind == HudKind::Toast)
            ++toastCount;
    }
    EXPECT_EQ(toastCount, 1u);
    EXPECT_EQ(snap->elements[0].title, "Snapshot Loaded");
}



