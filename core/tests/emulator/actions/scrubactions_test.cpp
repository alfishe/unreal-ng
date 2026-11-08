// The analog scrub of the history in the core (scrubactions.h; controller manager debug-and-ttd.md §3.2): the curve,
// the clock's frames per tick, both directions against each other, a stop on a refused jog, and the registered
// actions setting the clock's levels.

#include "emulator/actions/scrubactions.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

#include "_helpers/testwaithelper.h"

using namespace actions;

namespace
{
constexpr uint64_t kSecond = 1'000'000'000ull;

struct Jogs
{
    struct Jog
    {
        std::string instance;
        int64_t frames;
    };
    std::mutex mutex;
    std::vector<Jog> seen;
    std::atomic<int> count{0};

    ScrubClock::Jog Sink()
    {
        return [this](const std::string& instance, int64_t frames, double, const ActionOrigin&) {
            std::lock_guard<std::mutex> lock(mutex);
            seen.push_back({instance, frames});
            count++;
        };
    }
};
}  // namespace

TEST(ScrubActions_Test, ScrubSpeed_TheCurvePoints)
{
    EXPECT_EQ(ScrubSpeed(0.0f), 0.0f);
    EXPECT_EQ(ScrubSpeed(0.07f), 0.0f) << "a resting finger";
    EXPECT_NEAR(ScrubSpeed(0.08f), 1.0f, 1e-3f);
    EXPECT_NEAR(ScrubSpeed(0.30f), 5.0f, 1e-2f);
    EXPECT_NEAR(ScrubSpeed(0.45f), std::sqrt(5.0f * 50.0f), 0.1f) << "the geometric mean halfway";
    EXPECT_NEAR(ScrubSpeed(0.90f), 500.0f, 0.5f);
    EXPECT_NEAR(ScrubSpeed(1.0f), 5000.0f, 1.0f);
    EXPECT_NEAR(ScrubSpeed(1.5f), 5000.0f, 1.0f);
}

TEST(ScrubActions_Test, Clock_FramesCoveredPerTick)
{
    Jogs jogs;
    ScrubClock clock(jogs.Sink(), false);
    clock.SetAt("a", false, 0.30, {}, 0);  // 5 frames/s back
    clock.TickAt(kSecond / 10);            // 0.5 frame: waits
    EXPECT_EQ(jogs.count.load(), 0);
    clock.TickAt(kSecond / 2);  // 2.5 frames owed
    ASSERT_EQ(jogs.count.load(), 1);
    EXPECT_EQ(jogs.seen[0].frames, -2);
    EXPECT_EQ(jogs.seen[0].instance, "a");

    clock.SetAt("a", true, 0.30, {}, kSecond);  // forward as much: they cancel
    clock.TickAt(3 * kSecond);
    EXPECT_EQ(jogs.count.load(), 1) << "both triggers held at one level count against each other";

    clock.SetAt("a", false, 0, {}, 3 * kSecond);  // the back trigger released: forward at 5 frames/s
    clock.TickAt(4 * kSecond);
    ASSERT_EQ(jogs.count.load(), 2);
    EXPECT_GT(jogs.seen[1].frames, 0);

    clock.SetAt("a", true, 0, {}, 4 * kSecond);
    EXPECT_EQ(clock.Active(), 0u) << "a level of 0 on both ends the scrub";
}

TEST(ScrubActions_Test, Clock_StopsOnARefusalUntilTheLevelChanges)
{
    Jogs jogs;
    ScrubClock clock(jogs.Sink(), false);
    clock.SetAt("a", true, 1.0, {}, 0);
    clock.TickAt(kSecond / 50);
    ASSERT_EQ(jogs.count.load(), 1);
    clock.Stop("a");  // the end of the history
    clock.TickAt(kSecond);
    EXPECT_EQ(jogs.count.load(), 1);
    EXPECT_EQ(clock.Active(), 0u);
    clock.SetAt("a", true, 0.9, {}, kSecond);  // the trigger moves: on again, from now
    clock.TickAt(kSecond + kSecond / 50);
    ASSERT_EQ(jogs.count.load(), 2);
    EXPECT_LE(jogs.seen[1].frames, 11) << "no frames owed from the stopped time";
}

TEST(ScrubActions_Test, Clock_ThreadTicksWhileHeld)
{
    Jogs jogs;
    ScrubClock clock(jogs.Sink());
    clock.Set("a", true, 1.0, {});  // 5000 frames/s: 100 a tick
    ASSERT_TRUE(TestWait::ForAtLeast(jogs.count, 1)) << "the clock jogs within a tick";
    clock.Set("a", true, 0, {});
    EXPECT_EQ(clock.Active(), 0u);
}

TEST(ScrubActions_Test, ActionsSetTheLevels)
{
    ActionRegistry registry([](const std::string&) { return std::shared_ptr<Emulator>(); });
    Jogs jogs;
    ScrubClock clock(jogs.Sink(), false);
    const std::vector<RegistrationHandle> handles = RegisterScrubActions(registry, clock);
    ASSERT_EQ(handles.size(), 2u);
    const std::optional<ActionInfo> back = registry.Find("ttd.scrub_back");
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->kind, ValueKind::Level);
    EXPECT_TRUE(registry.Find("ttd.scrub_forward").has_value());
}
