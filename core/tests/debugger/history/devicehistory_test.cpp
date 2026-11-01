// history::DeviceHistory: the one history pattern of every peripheral (off / ring / capture, an optional stream)

#include "debugger/history/devicehistory.h"

#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"

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
