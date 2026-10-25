#include <gtest/gtest.h>

#include <cstdio>
#include <memory>
#include <string>

#include "hud/core/hudmodel.h"
#include "hud/core/hudtiming.h"

/// What the HUD model's update path promises, as facts and not as microseconds: this file used to be a "benchmark" whose
/// tests asserted time per call (a speed of the host and of the build type, flaky by nature). The promises behind those
/// numbers are checked directly: an update with the same content publishes nothing new (dirty tracking), a snapshot read
/// is a load of the published snapshot (no copy), and the model holds what was last set.
class HudModelUpdates_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _model = std::make_shared<HudModel>(nullptr);
        _model->onFeatureChanged(true);  // Enable HUD
    }

    std::shared_ptr<HudModel> _model;
};

// Indicator updates with SAME content (the beeper firing every frame) publish nothing: the snapshot object stays
TEST_F(HudModelUpdates_Test, IndicatorUpdateSameContentPublishesNothingNew)
{
    constexpr int kIterations = 10000;

    _model->setIndicatorAt("beeper", HudTilePosition::TopLeft, HudState::Active, "BEEP", "", "speaker",
                           std::chrono::milliseconds(1000), true);
    const auto published = _model->snapshot();
    ASSERT_EQ(published->elements.size(), 1u);

    for (int i = 0; i < kIterations; ++i)
    {
        _model->setIndicatorAt("beeper", HudTilePosition::TopLeft, HudState::Active, "BEEP", "", "speaker",
                               std::chrono::milliseconds(1000), true);
    }

    EXPECT_EQ(_model->snapshot().get(), published.get()) << "dirty tracking: same content, same published snapshot";
}

// Indicator updates with DIFFERENT content publish: the snapshot follows the last one set
TEST_F(HudModelUpdates_Test, IndicatorUpdateDifferentContentPublishesTheLastOne)
{
    constexpr int kIterations = 10000;

    for (int i = 0; i < kIterations; ++i)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "RAM %d", i % 8);
        _model->setIndicatorAt("mem", HudTilePosition::TopLeft, HudState::Active, buf, "", "ram",
                               std::chrono::milliseconds(1200), true);
    }

    const auto snap = _model->snapshot();
    ASSERT_EQ(snap->elements.size(), 1u);
    EXPECT_EQ(snap->elements[0].id, "ind/mem");
    EXPECT_EQ(snap->elements[0].value, "RAM 7") << "the last of 10000 (9999 % 8)";
}

// A snapshot read is a load of the published snapshot: the same object every time, nothing copied
TEST_F(HudModelUpdates_Test, SnapshotReadReturnsThePublishedObject)
{
    _model->setIndicatorAt("mem", HudTilePosition::TopLeft, HudState::Active, "RAM 3", "", "ram",
                           std::chrono::milliseconds(1200), true);
    _model->setIndicatorAt("rom", HudTilePosition::TopLeft, HudState::Active, "ROM 0", "", "rom",
                           std::chrono::milliseconds(1200), true);
    _model->setIndicatorAt("beeper", HudTilePosition::TopLeft, HudState::Active, "BEEP", "", "speaker",
                           std::chrono::milliseconds(1000), true);

    const auto first = _model->snapshot();
    EXPECT_EQ(first->elements.size(), 3u);
    for (int i = 0; i < 10000; ++i)
        ASSERT_EQ(_model->snapshot().get(), first.get());
}

// A typical frame (the beeper active, a page switch every 10 frames, the expiry check, the paint's snapshot read)
// leaves both indicators in the model with the last page shown
TEST_F(HudModelUpdates_Test, SimulatedFrameWorkloadKeepsTheIndicators)
{
    constexpr int kFrames = 1000;

    for (int frame = 0; frame < kFrames; ++frame)
    {
        _model->setIndicatorAt("beeper", HudTilePosition::TopLeft, HudState::Active, "BEEP", "", "speaker",
                               std::chrono::milliseconds(1000), true);
        if (frame % 10 == 0)
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "RAM %d", frame % 8);
            _model->setIndicatorAt("mem", HudTilePosition::TopLeft, HudState::Active, buf, "", "ram",
                                   std::chrono::milliseconds(1200), true);
        }
        _model->expire(HudClock::time_point{});  // an instant before everything: the call is made, nothing is due
        (void)_model->snapshot();
    }

    const auto snap = _model->snapshot();
    ASSERT_EQ(snap->elements.size(), 2u);
    bool beeper = false;
    std::string page;
    for (const auto& e : snap->elements)
    {
        beeper = beeper || e.id == "ind/beeper";
        if (e.id == "ind/mem")
            page = e.value;
    }
    EXPECT_TRUE(beeper);
    EXPECT_EQ(page, "RAM 6") << "the last page switch was frame 990 (990 % 8 = 6)";
}
