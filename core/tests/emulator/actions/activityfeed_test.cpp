// The activity feed (Stream Deck integration architecture §5, protocol.md §4): what each job the registry ran
// records, the merge of deltas within 100 ms, the ring and its `dropped`, the listeners; and the catalogue fields
// (description, toggle, revision) the registry fills at Register.

#include "pch.h"

#include <atomic>
#include <memory>
#include <string>

#include "_helpers/testwaithelper.h"
#include "emulator/actions/activityfeed.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/catalogue.h"

using namespace actions;

namespace
{
ActionInfo Info(const char* id, ValueKind kind = ValueKind::Press)
{
    ActionInfo info;
    info.id = id;
    info.label = id;
    info.group = "Test";
    info.kind = kind;
    return info;
}

ActionValue Delta(double delta, double rate)
{
    ActionValue value;
    value.kind = ValueKind::Delta;
    value.delta = delta;
    value.rate = rate;
    return value;
}

ActionOrigin From(execution::Origin origin, const char* identity, const char* client = "")
{
    ActionOrigin out;
    out.origin = origin;
    out.identity = identity;
    out.client = client;
    return out;
}

struct Clock
{
    int64_t now = 1000;
    ActivityFeed::Clock Fn()
    {
        return [this] { return now; };
    }
};
}  // namespace

TEST(ActivityFeed_Test, AJobIsOneEntryWithItsOriginSurfaceAndResult)
{
    Clock clock;
    ActivityFeed feed(clock.Fn());
    ActionValue press;
    feed.Record("i1", Info("run.pause_toggle"), press, From(execution::Origin::HostDevice, "dev1", "Xbox pad"),
                ActionResult::Success());
    feed.Record("i1", Info("mach.nmi"), press, From(execution::Origin::Gui, ""),
                ActionResult::Failure("Run-control held", "held"));
    const ActivityFeed::Page page = feed.Since(0);
    ASSERT_EQ(page.entries.size(), 2u);
    EXPECT_EQ(page.last, 2u);
    EXPECT_EQ(page.dropped, 0u);
    const StateNode first = ActivityEntryNode(page.entries[0]);
    EXPECT_EQ(first.find("id")->s, "run.pause_toggle");
    EXPECT_EQ(first.find("origin")->s, "host_device");
    EXPECT_EQ(first.find("surface")->s, "Xbox pad");
    EXPECT_TRUE(first.find("value")->find("p")->b);
    EXPECT_TRUE(first.find("ok")->b);
    EXPECT_EQ(first.find("t_ms")->i, 1000);
    const StateNode second = ActivityEntryNode(page.entries[1]);
    EXPECT_EQ(second.find("surface")->s, "GUI");
    EXPECT_FALSE(second.find("ok")->b);
    EXPECT_EQ(second.find("error")->s, "Run-control held");
    EXPECT_EQ(feed.Since(1).entries.size(), 1u);
    EXPECT_EQ(feed.Since(0, "i2").entries.size(), 0u);
}

TEST(ActivityFeed_Test, DeltasOfOneSurfaceMergeWithin100Ms)
{
    Clock clock;
    ActivityFeed feed(clock.Fn());
    const ActionInfo seek = Info("ttd.seek_by", ValueKind::Delta);
    const ActionOrigin dial = From(execution::Origin::HostDevice, "dial");
    feed.Record("i1", seek, Delta(2, 10), dial, ActionResult::Success());
    clock.now += 40;
    feed.Record("i1", seek, Delta(3, 30), dial, ActionResult::Success());
    clock.now += 50;
    feed.Record("i1", seek, Delta(-1, 5), dial, ActionResult::Success());
    ActivityFeed::Page page = feed.Since(0);
    ASSERT_EQ(page.entries.size(), 1u) << "a scrub is one line that updates";
    EXPECT_DOUBLE_EQ(page.entries[0].value.delta, 4);
    EXPECT_DOUBLE_EQ(page.entries[0].value.rate, 30) << "the higher rate";
    EXPECT_EQ(page.entries[0].merged, 3u);
    EXPECT_EQ(page.entries[0].seq, 3u) << "a merge gives a new seq, so a reader after the first sees it";

    // Another surface, another action, or past the window: new entries
    feed.Record("i1", seek, Delta(1, 1), From(execution::Origin::Companion, "deck"), ActionResult::Success());
    feed.Record("i1", Info("ttd.jog_frames", ValueKind::Delta), Delta(1, 1), dial, ActionResult::Success());
    clock.now += 200;
    feed.Record("i1", Info("ttd.jog_frames", ValueKind::Delta), Delta(1, 1), dial, ActionResult::Success());
    EXPECT_EQ(feed.Since(0).entries.size(), 4u);
}

TEST(ActivityFeed_Test, TheRingDropsTheOldestAndSaysHowMany)
{
    Clock clock;
    ActivityFeed feed(clock.Fn());
    std::atomic<int> heard{0};
    const uint64_t listener = feed.AddListener([&](const ActivityEntry&) { heard++; });
    for (size_t i = 0; i < ActivityFeed::kCapacity + 10; i++)
        feed.Record("i1", Info("run.frame_step"), ActionValue{}, From(execution::Origin::WebApi, "w"),
                    ActionResult::Success());
    EXPECT_EQ(heard.load(), static_cast<int>(ActivityFeed::kCapacity + 10)) << "listeners hear every record";
    feed.RemoveListener(listener);
    feed.Record("i1", Info("run.frame_step"), ActionValue{}, From(execution::Origin::WebApi, "w"),
                ActionResult::Success());
    EXPECT_EQ(heard.load(), static_cast<int>(ActivityFeed::kCapacity + 10));

    ActivityFeed::Page page = feed.Since(0, {}, 1000);
    EXPECT_EQ(page.entries.size(), ActivityFeed::kCapacity);
    EXPECT_EQ(page.dropped, 11u);
    EXPECT_EQ(feed.Since(page.last).entries.size(), 0u);
    EXPECT_EQ(feed.Since(page.last).dropped, 0u);
    page = feed.Since(0, {}, 5);
    ASSERT_EQ(page.entries.size(), 5u) << "the newest within the limit";
    EXPECT_EQ(page.entries.back().seq, page.last);
}

TEST(ActivityFeed_Test, TheRegistryRecordsWhatItsWorkerRan)
{
    ActivityFeed feed;
    ActionRegistry registry([](const std::string&) -> std::shared_ptr<Emulator> { return nullptr; });
    registry.SetActivityFeed(&feed);
    // No instance resolves: Submit answers at once, nothing ran, nothing is recorded
    std::atomic<bool> done{false};
    RegistrationHandle handle = registry.Register(Info("t.press"), [](const ActionCall&) {
        return ActionResult::Success();
    });
    registry.Submit("t.press", ActionValue{}, "nobody", From(execution::Origin::WebApi, "w"),
                    [&](ActionResult) { done = true; });
    EXPECT_TRUE(done.load());
    EXPECT_EQ(feed.Since(0).entries.size(), 0u);
}

TEST(ActionCatalogue_Test, RegisterFillsTheDescriptionTheToggleAndBumpsTheRevision)
{
    ActionRegistry registry([](const std::string&) -> std::shared_ptr<Emulator> { return nullptr; });
    const uint64_t before = registry.Revision();
    RegistrationHandle turbo = registry.Register(Info("mach.turbo_toggle"), nullptr);
    EXPECT_EQ(registry.Revision(), before + 1);
    const std::optional<ActionInfo> info = registry.Find("mach.turbo_toggle");
    ASSERT_TRUE(info);
    EXPECT_FALSE(info->description.empty());
    EXPECT_TRUE(info->toggle);
    RegistrationHandle feature = registry.Register(Info("feature.toggle"), nullptr);
    RegistrationHandle plain = registry.Register(Info("t.plain"), nullptr);
    EXPECT_FALSE(registry.Find("t.plain")->toggle);
    EXPECT_EQ(registry.Find("t.plain")->description, "");
    std::vector<std::string> stems;
    for (const ActionRegistry::ToggleReadout& toggle : registry.ToggleReadouts())
        stems.push_back(toggle.stem + (toggle.param.empty() ? "" : "." + toggle.param));
    EXPECT_EQ(stems, (std::vector<std::string>{"toggle.mach.turbo_toggle", "toggle.feature.name"}));
    EXPECT_EQ(registry.ToggleState("mach.turbo_toggle", "nobody"), std::nullopt) << "no machine";
    plain.Reset();
    EXPECT_EQ(registry.Revision(), before + 4);
}

TEST(ActionCatalogue_Test, EveryCoreActionHasADescription)
{
    for (const ActionInfo& info : ActionRegistry::Instance().List())
        EXPECT_FALSE(info.description.empty()) << info.id;
    for (const char* id : {"run.pause_toggle", "ttd.record_toggle", "mach.turbo_toggle", "audio.mute_toggle",
                           "video.overscan_toggle", "video.temporal", "dbg.mode_toggle", "feature.toggle",
                           "set.toggle"})
    {
        const std::optional<ActionInfo> info = ActionRegistry::Instance().Find(id);
        ASSERT_TRUE(info) << id;
        EXPECT_TRUE(info->toggle) << id;
    }
    EXPECT_FALSE(ActionRegistry::Instance().Find("run.step_in")->toggle);
}
