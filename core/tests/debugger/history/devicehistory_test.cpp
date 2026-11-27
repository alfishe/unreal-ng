// history::DeviceHistory: the one history pattern of every peripheral (off / ring / capture, an optional stream)

#include "debugger/history/devicehistory.h"

#include <gtest/gtest.h>

#include <fstream>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "debugger/history/exechistory.h"
#include "emulator/ports/porthistory.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
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

/// region <Record-time filter (historyfilter.h)>

TEST(HistoryFilter_Test, ParsesTermsValuesAndRanges)
{
    history::RecordFilter filter;
    std::string error;
    ASSERT_TRUE(history::RecordFilter::Parse("  Side=host,GS  port=#B3,0xBB,$33  pc=100-#200 ", filter, error)) << error;
    ASSERT_EQ(filter.Terms().size(), 3u);
    EXPECT_EQ(filter.Text(), "Side=host,GS port=#B3,0xBB,$33 pc=100-#200");
    EXPECT_EQ(filter.Terms()[0].field, "side");
    EXPECT_EQ(filter.Terms()[0].words, (std::vector<std::string>{ "host", "gs" }));
    ASSERT_EQ(filter.Terms()[1].numbers.size(), 3u);
    EXPECT_EQ(filter.Terms()[1].numbers[1].lo, 0xBB);
    EXPECT_EQ(filter.Terms()[2].numbers[0].lo, 100) << "plain digits are decimal";
    EXPECT_EQ(filter.Terms()[2].numbers[0].hi, 0x200);

    ASSERT_TRUE(history::RecordFilter::Parse("", filter, error));
    EXPECT_TRUE(filter.Empty());
    for (const char* bad : { "side", "=host", "side=", "side=host,,gs" })
    {
        EXPECT_FALSE(history::RecordFilter::Parse(bad, filter, error)) << bad;
        EXPECT_NE(error.find("field=value"), std::string::npos) << error;
    }
}

TEST(DeviceHistory_Test, FilterKeepsOnlyTheRecordsThatPass)
{
    TestHistory h("dev", "things");
    history::Config config;
    config.mode = history::Mode::Capture;
    config.stream = TestPathHelper::GetUniqueTestScratchPath("history-filter.jsonl");
    config.filter = "value=2-3,7 done=false";
    std::string error;
    ASSERT_TRUE(h.Configure(config, error)) << error;
    for (int i = 0; i < 10; i++)
        h.Push({ i, i == 3 });
    EXPECT_EQ(Values(h), (std::vector<int>{ 2, 7 })) << "3 is done: the other term does not hold";
    history::Stats stats = h.GetStats();
    EXPECT_EQ(stats.written, 2u);
    EXPECT_EQ(stats.filtered, 8u);
    EXPECT_EQ(stats.config.filter, "value=2-3,7 done=false");
    const StateNode node = history::StatsNode(h);
    EXPECT_EQ(node.find("filtered")->i, 8);

    // A record left out is not the one a later completion means
    h.UpdateLast([](TestRecord& r) { r.value = 99; });
    EXPECT_EQ(Values(h), (std::vector<int>{ 2, 7 })) << "9 was left out: nothing to complete";
    h.Push({ 2, false });
    h.UpdateLast([](TestRecord& r) { r.done = true; });
    EXPECT_TRUE(h.Snapshot().back().done);

    // The stream holds the same records and no other
    config.mode = history::Mode::Off;
    config.stream.clear();
    const std::string path = stats.config.stream;
    ASSERT_TRUE(h.Configure(config, error)) << error;
    EXPECT_EQ(Lines(path).size(), 3u);

    // A filter dropped: every record again; the counts start afresh
    config.mode = history::Mode::Ring;
    config.filter.clear();
    ASSERT_TRUE(h.Configure(config, error)) << error;
    h.Push({ 5, false });
    EXPECT_EQ(Values(h), (std::vector<int>{ 5 }));
    EXPECT_EQ(h.GetStats().filtered, 0u);
    EXPECT_EQ(history::StatsNode(h).find("filter"), nullptr);
}

TEST(DeviceHistory_Test, ARefusedFilterLeavesTheHistoryAsItWas)
{
    TestHistory h("dev", "things");
    history::Config config;
    config.mode = history::Mode::Ring;
    std::string error;
    ASSERT_TRUE(h.Configure(config, error));
    h.Push({ 1, false });
    config.filter = "value";
    EXPECT_FALSE(h.Configure(config, error));
    EXPECT_EQ(Values(h), (std::vector<int>{ 1 })) << "the records stay";
    EXPECT_TRUE(h.GetStats().config.filter.empty());

    // A record that names its fields refuses an unknown field and an unknown word with the valid ones
    GSHistory gs("zxbus.1", "GS events");
    config.filter = "sidee=host";
    EXPECT_FALSE(gs.Configure(config, error));
    EXPECT_NE(error.find("no field 'sidee'"), std::string::npos) << error;
    EXPECT_NE(error.find("side: host, gs"), std::string::npos) << error;
    config.filter = "side=hots";
    EXPECT_FALSE(gs.Configure(config, error));
    EXPECT_NE(error.find("'hots' is not a value of side"), std::string::npos) << error;
}

TEST(DeviceHistory_Test, FilterOnAFastPushedRing)
{
    history::DeviceHistory<ExecRecord> h("cpu", "instructions", 8);
    h.EnableFastPush(4);
    history::Config config;
    config.mode = history::Mode::Ring;
    config.filter = "pc=#8000-#8002 kind=ram";
    std::string error;
    ASSERT_TRUE(h.Configure(config, error)) << error;
    for (uint16_t pc = 0x7FFE; pc < 0x8006; pc++)
    {
        ExecRecord record;
        record.pc = pc;
        record.kind = pc == 0x8001 ? 0 : 1;
        h.PushFast(record);
    }
    std::vector<uint16_t> pcs;
    for (const ExecRecord& r : h.Snapshot())
        pcs.push_back(r.pc);
    EXPECT_EQ(pcs, (std::vector<uint16_t>{ 0x8000, 0x8002 }));
    EXPECT_EQ(h.GetStats().filtered, 6u);
}

/// The compiled getters of a record that names its fields and its rendered ToState give the same answer
template <typename Record>
void ExpectCompiledAsRendered(const std::vector<Record>& records, const std::vector<std::string>& filters)
{
    for (const std::string& text : filters)
    {
        history::DeviceHistory<Record> h("dev", "records");
        history::Config config;
        config.mode = history::Mode::Capture;
        config.filter = text;
        std::string error;
        ASSERT_TRUE(h.Configure(config, error)) << text << ": " << error;
        history::RecordFilter filter;
        ASSERT_TRUE(history::RecordFilter::Parse(text, filter, error));
        size_t expected = 0;
        for (const Record& record : records)
        {
            StateNode node = StateNode::Object();
            record.ToState(node);
            expected += filter.Matches(node) ? 1 : 0;
            h.Push(record);
        }
        EXPECT_EQ(h.GetStats().written, expected) << text;
        EXPECT_EQ(h.GetStats().filtered, records.size() - expected) << text;
    }
}

TEST(DeviceHistory_Test, CompiledFiltersAgreeWithTheRenderedRecords)
{
    std::vector<GSTraceEvent> gs;
    for (int side = 0; side <= 5; side++)
    {
        for (uint8_t flags = 0; flags < 8; flags++)
        {
            GSTraceEvent e;
            e.side = static_cast<GSTraceSide>(side);
            e.flags = flags;
            e.port = static_cast<uint16_t>(side == 0 ? 0xB3 + 8 * (flags & 1) : 0x6000 + flags);
            e.pc = static_cast<uint16_t>(0x100 * side + flags);
            e.value = static_cast<uint8_t>(flags * 3);
            e.channel = static_cast<uint8_t>(flags & 3);
            e.iterations = 5 + flags;
            e.frameNumber = 100 + side;
            gs.push_back(e);
        }
    }
    ExpectCompiledAsRendered(gs, { "side=host", "side=host out=true", "side=host,gs port=#B3,#BB", "out=false",
                                   "nmi=true", "side=interrupt nmi=0", "channel=1-2", "iterations=6-9", "dropped=yes",
                                   "pc=#100-#2FF value=3,6", "frame=101 side=gs", "card_address=#6001-#26003",
                                   "wait=0", "host_t=5-12", "side=zx-dma,idle" });

    std::vector<PortAccessRecord> ports;
    for (int i = 0; i < 16; i++)
    {
        PortAccessRecord r;
        r.frame = static_cast<uint64_t>(i / 4);
        r.tacts = static_cast<uint16_t>(i * 100);
        r.pc = static_cast<uint16_t>(0x8000 + i);
        r.port = static_cast<uint16_t>((i & 1) ? 0xFFFD : 0xBFFD);
        r.value = static_cast<uint8_t>(i);
        r.out = (i & 2) != 0;
        r.dos = (i & 4) != 0;
        ports.push_back(r);
    }
    ExpectCompiledAsRendered(ports, { "dir=out", "dir=in port=#FFFD", "dos=true", "dos=1 dir=out,in",
                                      "pc=#8004-#8008 value=5,6,7", "frame=2-3 tacts=900-1200" });

    std::vector<ExecRecord> exec;
    for (int i = 0; i < 12; i++)
    {
        ExecRecord r;
        r.frame = static_cast<uint64_t>(i / 3);
        r.pc = static_cast<uint16_t>(0x4000 * (i % 4) + i);
        r.kind = static_cast<uint8_t>(i % 4 == 3 ? 0xFF : i % 4);
        r.page = static_cast<uint8_t>(i);
        r.idle = i == 5 ? 40 : 0;
        exec.push_back(r);
    }
    ExpectCompiledAsRendered(exec, { "kind=ram", "kind=rom,none", "pc=#4000-#7FFF", "page=2-6 kind=cache",
                                     "idle_iterations=40", "frame=1 kind=ram,rom" });
}

/// endregion </Record-time filter>
