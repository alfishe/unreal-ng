// history::DeviceHistory: the one history pattern of every peripheral (off / ring / capture, an optional stream)

#include "debugger/history/devicehistory.h"

#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"

namespace
{

struct TestRecord
{
    int value = 0;
    bool done = false;

    void ToState(StateNode& node) const
    {
        node["value"] = value;
        node["done"] = done;
    }
};

using TestHistory = history::DeviceHistory<TestRecord>;

std::vector<int> Values(const TestHistory& h)
{
    std::vector<int> values;
    for (const TestRecord& r : h.Snapshot())
        values.push_back(r.value);
    return values;
}

/// A page's records as "seq:value"
std::vector<std::string> SeqValues(const history::Page& page)
{
    std::vector<std::string> out;
    for (const StateNode& record : page.records.items)
        out.push_back(std::to_string(record.find("seq")->i) + ":" + std::to_string(record.find("value")->i));
    return out;
}

std::vector<std::string> Lines(const std::string& path)
{
    std::ifstream in(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(in, line);)
        lines.push_back(line);
    return lines;
}

}  // namespace

TEST(DeviceHistory_Test, OffKeepsNothing)
{
    TestHistory h("dev", "things");
    EXPECT_FALSE(h.On());
    h.Push({ 1 });
    EXPECT_TRUE(h.Snapshot().empty());
    EXPECT_EQ(h.GetStats().written, 0u);
}

TEST(DeviceHistory_Test, RingKeepsTheNewest)
{
    TestHistory h("dev", "things");
    std::string error;
    ASSERT_TRUE(h.Configure({ .mode = history::Mode::Ring, .limit = 3 }, error));
    for (int i = 1; i <= 5; i++)
        h.Push({ i });
    EXPECT_EQ(Values(h), (std::vector<int>{ 3, 4, 5 }));
    EXPECT_EQ(h.GetStats().dropped, 2u);
    EXPECT_EQ(h.Snapshot(2).front().value, 4) << "the newest two, oldest first";
    h.UpdateLast([](TestRecord& r) { r.done = true; });
    EXPECT_TRUE(h.Snapshot().back().done);
}

TEST(DeviceHistory_Test, CaptureStopsAtItsLimit)
{
    TestHistory h("dev", "things");
    std::string error;
    ASSERT_TRUE(h.Configure({ .mode = history::Mode::Capture, .limit = 4 }, error));
    for (int i = 1; i <= 6; i++)
        h.Push({ i });
    EXPECT_EQ(Values(h), (std::vector<int>{ 1, 2, 3, 4 }));
    const history::Stats stats = h.GetStats();
    EXPECT_EQ(stats.written, 6u);
    EXPECT_EQ(stats.dropped, 2u);

    ASSERT_TRUE(h.Configure({ .mode = history::Mode::Capture }, error));
    for (int i = 0; i < 1000; i++)
        h.Push({ i });
    EXPECT_EQ(h.Snapshot().size(), 1000u) << "no limit";
}

/// The stream gets every record once it is complete: the newest when the next comes or the stream closes
TEST(DeviceHistory_Test, StreamWritesCompletedRecords)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("history-stream.jsonl");
    {
        TestHistory h("dev", "things");
        std::string error;
        ASSERT_TRUE(h.Configure({ .mode = history::Mode::Ring, .limit = 2, .stream = path }, error)) << error;
        for (int i = 1; i <= 5; i++)
        {
            h.Push({ i });
            h.UpdateLast([](TestRecord& r) { r.done = true; });
        }
        ASSERT_TRUE(h.Configure({ .mode = history::Mode::Off }, error)) << "closes the stream";
    }
    const std::vector<std::string> lines = Lines(path);
    ASSERT_EQ(lines.size(), 5u) << "the ring keeps 2, the stream all 5";
    EXPECT_EQ(lines[0], R"({"value":1,"done":true})");
    EXPECT_EQ(lines[4], R"({"value":5,"done":true})");
}

/// The records reach the file while the stream is open, not only when it closes
TEST(DeviceHistory_Test, StreamIsOnDiskWhileOpen)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("history-stream-open.jsonl");
    TestHistory h("dev", "things");
    std::string error;
    ASSERT_TRUE(h.Configure({ .mode = history::Mode::Ring, .limit = 2, .stream = path }, error)) << error;
    for (int i = 1; i <= 3; i++)
        h.Push({ i });  // the third stays open: the next record or the close completes it
    const bool onDisk = TestWait::For([&path] { return Lines(path).size() == 2; });
    EXPECT_TRUE(onDisk) << "lines in the file while open: " << Lines(path).size();
    if (onDisk)
        EXPECT_EQ(Lines(path)[1], R"({"value":2,"done":false})");
    ASSERT_TRUE(h.Configure({ .mode = history::Mode::Off }, error));
    EXPECT_EQ(Lines(path).size(), 3u) << "the close writes the open one";
}

/// Paused: nothing recorded, the records kept; resume goes on; a configuration starts unpaused
TEST(DeviceHistory_Test, PauseHoldsTheRecords)
{
    TestHistory h("dev", "things");
    std::string error;
    ASSERT_TRUE(h.Configure({ .mode = history::Mode::Ring, .limit = 8 }, error));
    h.Push({ 1 });
    h.Pause(true);
    EXPECT_FALSE(h.On());
    EXPECT_TRUE(h.GetStats().paused);
    EXPECT_EQ(Values(h), (std::vector<int>{ 1 }));
    h.Pause(false);
    EXPECT_TRUE(h.On());
    h.Push({ 2 });
    EXPECT_EQ(Values(h), (std::vector<int>{ 1, 2 }));
    h.Pause(true);
    ASSERT_TRUE(h.Configure({ .mode = history::Mode::Ring, .limit = 8 }, error));
    EXPECT_TRUE(h.On()) << "a configuration starts afresh, unpaused";
}

TEST(DeviceHistory_Test, StreamLimitAndBadPath)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("history-stream-limit.jsonl");
    TestHistory h("dev", "things");
    std::string error;
    ASSERT_TRUE(h.Configure({ .mode = history::Mode::Ring, .limit = 8, .stream = path, .streamLimit = 3 }, error));
    for (int i = 0; i < 10; i++)
        h.Push({ i });
    ASSERT_TRUE(h.Configure({ .mode = history::Mode::Off }, error));
    EXPECT_EQ(Lines(path).size(), 3u);

    EXPECT_FALSE(h.Configure({ .mode = history::Mode::Ring, .stream = path + "/no/such/dir/x.jsonl" }, error));
    EXPECT_FALSE(error.empty());
}

TEST(DeviceHistory_Test, RegistryFindsByDevice)
{
    TestHistory a("beta128", "FDC commands");
    TestHistory b("zxbus.1", "GS traffic");
    history::Registry registry;
    registry.Add(&a);
    registry.Add(&b);
    std::string what;
    EXPECT_TRUE(registry.With("zxbus.1", [&what](history::IDeviceHistory& h) { what = h.What(); }));
    EXPECT_EQ(what, "GS traffic");
    EXPECT_EQ(registry.List().items.size(), 2u);
    registry.Remove(&b);
    EXPECT_FALSE(registry.With("zxbus.1", [](history::IDeviceHistory&) {}));
}

/// PushFast (one writer, no lock): the records wait in the stage and reach the history in order, whoever drains them
/// (the writer on a full stage, a reader); the switch callback tells on and off once each
TEST(DeviceHistory_Test, FastPushKeepsTheOrder)
{
    TestHistory h("dev", "things");
    h.EnableFastPush(4);
    std::vector<bool> switches;
    h.OnSwitch([&](bool on) { switches.push_back(on); });
    std::string error;
    ASSERT_TRUE(h.Configure({ .mode = history::Mode::Ring, .limit = 6 }, error));
    for (int i = 1; i <= 10; i++)
        h.PushFast({ i });
    EXPECT_EQ(Values(h), (std::vector<int>{ 5, 6, 7, 8, 9, 10 })) << "the writer drained the full stage itself";
    EXPECT_EQ(h.GetStats().written, 10u);
    h.PushFast({ 11 });
    h.UpdateLast([](TestRecord& r) { r.done = true; });
    EXPECT_EQ(h.Snapshot().back().value, 11);
    EXPECT_TRUE(h.Snapshot().back().done) << "a staged record is the newest for UpdateLast";
    ASSERT_TRUE(h.Configure({ .mode = history::Mode::Off }, error));
    h.PushFast({ 12 });
    EXPECT_EQ(h.GetStats().written, 0u) << "off: a staged record goes nowhere";
    EXPECT_EQ(switches, (std::vector<bool>{ true, false }));
}

/// Time travel (gs-debugger design §6.3, D3): a move back holds the records after the moment back, a move forward
/// brings them back in order, a wrapped ring comes out as a straight run would have left it
TEST(DeviceHistory_Test, RewindHoldsBackAndBringsBackAWrappedRing)
{
    TestHistory h("dev", "things");
    std::string error;
    ASSERT_TRUE(h.Configure({ .mode = history::Mode::Ring, .limit = 4 }, error));
    for (int i = 1; i <= 6; i++)
        h.Push({ i });
    ASSERT_EQ(Values(h), (std::vector<int>{ 3, 4, 5, 6 }));

    h.Rewind([](const TestRecord& r) { return r.value > 4; });
    EXPECT_EQ(Values(h), (std::vector<int>{ 3, 4 }));
    EXPECT_EQ(h.Held(), 2u);
    EXPECT_EQ(h.GetStats().written, 4u) << "held records are not counted";

    h.Rewind([](const TestRecord& r) { return r.value > 5; });
    EXPECT_EQ(Values(h), (std::vector<int>{ 3, 4, 5 }));
    EXPECT_EQ(h.Held(), 1u);

    // Live from here: what is pushed lands after the kept part, as a straight run's ring
    h.Truncate([](const TestRecord& r) { return r.value > 5; }, true);
    EXPECT_EQ(h.Held(), 0u);
    h.Push({ 6 });
    h.Push({ 7 });
    EXPECT_EQ(Values(h), (std::vector<int>{ 4, 5, 6, 7 }));
    const history::Stats stats = h.GetStats();
    EXPECT_EQ(stats.written, 7u);
    EXPECT_EQ(stats.dropped, 3u);
}

/// A truncation drops the kept and held records after the moment only; a record that comes back is not streamed twice
TEST(DeviceHistory_Test, TruncateKeepsTheSharedPartAndTheStreamOnce)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("rewind-stream.jsonl");
    std::string error;
    {
        TestHistory h("dev", "things");
        ASSERT_TRUE(h.Configure({ .mode = history::Mode::Capture, .stream = path }, error)) << error;
        for (int i = 1; i <= 6; i++)
            h.Push({ i });
        h.Rewind([](const TestRecord& r) { return r.value > 2; });
        EXPECT_EQ(h.Held(), 4u);
        h.Rewind([](const TestRecord& r) { return r.value > 4; });
        EXPECT_EQ(Values(h), (std::vector<int>{ 1, 2, 3, 4 }));
        h.Truncate([](const TestRecord& r) { return r.value > 3; }, false);
        EXPECT_EQ(Values(h), (std::vector<int>{ 1, 2, 3 }));
        EXPECT_EQ(h.Held(), 0u) << "the held 5 and 6 lie after the moment too";
        h.Rewind([](const TestRecord&) { return false; });
        EXPECT_EQ(Values(h), (std::vector<int>{ 1, 2, 3 }));
    }
    EXPECT_EQ(Lines(path).size(), 6u) << "each record streamed once, when it was first pushed";
}

/// A read after a cursor: the first `last` records numbered after it, oldest first, so a poller pages forward with no
/// gap; a ring counts what it evicted unread as missed
TEST(DeviceHistory_Test, RecordsSincePagesForwardThroughARing)
{
    TestHistory h("dev", "things");
    std::string error;
    ASSERT_TRUE(h.Configure({ .mode = history::Mode::Ring, .limit = 4 }, error));
    for (int i = 1; i <= 3; i++)
        h.Push({ i * 10 });

    history::Page page = h.RecordsSince(0, 2);
    EXPECT_EQ(SeqValues(page), (std::vector<std::string>{ "1:10", "2:20" })) << "the first two, not the newest";
    EXPECT_EQ(page.nextSince, 2u);
    EXPECT_EQ(page.missed, 0u);
    EXPECT_FALSE(page.restarted);

    page = h.RecordsSince(page.nextSince, 2);
    EXPECT_EQ(SeqValues(page), (std::vector<std::string>{ "3:30" }));
    EXPECT_EQ(page.nextSince, 3u);

    page = h.RecordsSince(page.nextSince, 0);
    EXPECT_TRUE(page.records.items.empty()) << "caught up";
    EXPECT_EQ(page.nextSince, 3u) << "the cursor stays";
    EXPECT_EQ(page.missed, 0u);

    // Six more into a ring of four: seq 4 and 5 are evicted before the reader comes back
    for (int i = 4; i <= 9; i++)
        h.Push({ i * 10 });
    page = h.RecordsSince(3, 0);
    EXPECT_EQ(SeqValues(page), (std::vector<std::string>{ "6:60", "7:70", "8:80", "9:90" }));
    EXPECT_EQ(page.missed, 2u);
    EXPECT_EQ(page.nextSince, 9u);
    EXPECT_FALSE(page.restarted);
}

/// A capture keeps its first `limit`; once the reader has them all, the ones over the cap are passed over as missed
TEST(DeviceHistory_Test, RecordsSinceThroughACapture)
{
    TestHistory h("dev", "things");
    std::string error;
    ASSERT_TRUE(h.Configure({ .mode = history::Mode::Capture, .limit = 3 }, error));
    for (int i = 1; i <= 5; i++)
        h.Push({ i });

    history::Page page = h.RecordsSince(0, 2);
    EXPECT_EQ(SeqValues(page), (std::vector<std::string>{ "1:1", "2:2" }));
    EXPECT_EQ(page.nextSince, 2u);
    EXPECT_EQ(page.missed, 0u);

    page = h.RecordsSince(2, 2);
    EXPECT_EQ(SeqValues(page), (std::vector<std::string>{ "3:3" }));
    EXPECT_EQ(page.missed, 2u) << "4 and 5 went over the cap";
    EXPECT_EQ(page.nextSince, 5u);

    h.Push({ 6 });
    page = h.RecordsSince(page.nextSince, 0);
    EXPECT_TRUE(page.records.items.empty());
    EXPECT_EQ(page.missed, 1u) << "only the one after the cursor";
    EXPECT_EQ(page.nextSince, 6u);
}

/// A cursor past the newest record (the history was cleared or configured since) restarts from the oldest kept
TEST(DeviceHistory_Test, RecordsSinceRestartsAfterAClear)
{
    TestHistory h("dev", "things");
    std::string error;
    ASSERT_TRUE(h.Configure({ .mode = history::Mode::Ring, .limit = 8 }, error));
    for (int i = 1; i <= 5; i++)
        h.Push({ i });
    const uint64_t cursor = h.RecordsSince(0, 0).nextSince;
    ASSERT_EQ(cursor, 5u);

    h.Clear();
    history::Page page = h.RecordsSince(cursor, 0);
    EXPECT_TRUE(page.restarted);
    EXPECT_TRUE(page.records.items.empty());
    EXPECT_EQ(page.nextSince, 0u) << "numbered afresh";

    h.Push({ 7 });
    h.Push({ 8 });
    page = h.RecordsSince(cursor, 1);
    EXPECT_TRUE(page.restarted);
    EXPECT_EQ(SeqValues(page), (std::vector<std::string>{ "1:7" })) << "from the oldest kept";
    EXPECT_EQ(page.nextSince, 1u);
    EXPECT_EQ(page.missed, 0u);

    page = h.RecordsSince(page.nextSince, 0);
    EXPECT_FALSE(page.restarted);
    EXPECT_EQ(SeqValues(page), (std::vector<std::string>{ "2:8" }));
}

/// The fast path's staged records are numbered too: a read after a cursor drains them first
TEST(DeviceHistory_Test, RecordsSinceSeesStagedRecords)
{
    TestHistory h("dev", "things");
    h.EnableFastPush(16);
    std::string error;
    ASSERT_TRUE(h.Configure({ .mode = history::Mode::Ring, .limit = 4 }, error));
    for (int i = 1; i <= 6; i++)
        h.PushFast({ i });
    const history::Page page = h.RecordsSince(1, 0);
    EXPECT_EQ(SeqValues(page), (std::vector<std::string>{ "3:3", "4:4", "5:5", "6:6" }));
    EXPECT_EQ(page.missed, 1u);
}
