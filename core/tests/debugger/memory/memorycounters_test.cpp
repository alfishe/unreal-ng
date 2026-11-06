// MemoryCounters (memorycounters.h) and MemoryWatch (memorywatch.h): the access counters of every cell in the cpu,
// page, ram and phys spaces (windows, cells, sparse, top), the watches over regions and partially decoded ports, and
// both on a running machine. A 48K: ROM 0 at #0000, RAM 5 at #4000, RAM 2 at #8000, RAM 0 at #C000.

#include <gtest/gtest.h>

#include "_helpers/testwaithelper.h"

#include <memory>
#include <string>

#include "debugger/memory/memorycounters.h"
#include "debugger/memory/memorywatch.h"
#include "debugger/tracecontrol/tracecontrol.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"

class MemoryCounters_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("memorycounters-test", "48K", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _tracker = &_context->pMemory->GetAccessTracker();
    }
    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
        }
    }

    void StartSession()
    {
        const tracecontrol::Reply reply = tracecontrol::Execute(*_emulator, "memory", "start");
        ASSERT_TRUE(reply.Ok()) << reply.message;
    }

    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    MemoryAccessTracker* _tracker = nullptr;
};

TEST_F(MemoryCounters_Test, NoSessionReadsZeros)
{
    const MemoryCounters::Result cell = MemoryCounters::ReadCell(_context, "cpu", 0x5C00);
    ASSERT_TRUE(cell.error.empty()) << cell.error;
    EXPECT_FALSE(cell.allocated);
    EXPECT_EQ(cell.session, "stopped");
    ASSERT_EQ(cell.cells.size(), 1u);
    EXPECT_EQ(cell.cells[0].reads, 0u);
}

TEST_F(MemoryCounters_Test, OneCellInEverySpace)
{
    ASSERT_NO_FATAL_FAILURE(StartSession());
    for (int i = 0; i < 3; i++)
        _tracker->TrackMemoryRead(0x5C00, 0, 0x1234);
    _tracker->TrackMemoryWrite(0x5C00, 7, 0x1234);
    _tracker->TrackMemoryExecute(0x8000, 0x8000);

    const uint32_t ram5 = 5 * 0x4000 + 0x1C00;   // #5C00 is RAM 5's #1C00
    for (const auto& [space, address] : std::initializer_list<std::pair<const char*, uint32_t>>{
             {"cpu", 0x5C00}, {"", 0x5C00}, {"ram5", 0x1C00}, {"RAM5", 0x1C00}, {"ram", ram5}, {"phys", ram5}})
    {
        const MemoryCounters::Result r = MemoryCounters::ReadCell(_context, space, address);
        ASSERT_TRUE(r.error.empty()) << space << ": " << r.error;
        EXPECT_TRUE(r.allocated);
        EXPECT_EQ(r.session, "capturing");
        ASSERT_EQ(r.cells.size(), 1u);
        EXPECT_EQ(r.cells[0].reads, 3u) << space;
        EXPECT_EQ(r.cells[0].writes, 1u) << space;
        EXPECT_EQ(r.cells[0].executes, 0u) << space;
    }
    EXPECT_EQ(MemoryCounters::ReadCell(_context, "ram2", 0).cells[0].executes, 1u) << "#8000 is RAM 2's #0000";
    EXPECT_EQ(MemoryCounters::ReadCell(_context, "ram5", 0).cells[0].reads, 0u);
}

TEST_F(MemoryCounters_Test, WindowsSparseTopAndCells)
{
    ASSERT_NO_FATAL_FAILURE(StartSession());
    for (int i = 0; i < 5; i++)
        _tracker->TrackMemoryRead(0x9000, 0, 0);
    for (int i = 0; i < 2; i++)
        _tracker->TrackMemoryWrite(0x9001, 0, 0);
    _tracker->TrackMemoryExecute(0x9002, 0x9002);

    MemoryCounters::Request request;
    request.start = 0x8F00;
    request.length = 0x200;
    MemoryCounters::Result r = MemoryCounters::Read(_context, request);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_EQ(r.cells.size(), 3u) << "sparse: the cells that were touched";
    EXPECT_EQ(r.cells[0].address, 0x9000u);
    EXPECT_EQ(r.cells[2].address, 0x9002u);
    EXPECT_EQ(r.reads, 5u);
    EXPECT_EQ(r.writes, 2u);
    EXPECT_EQ(r.executes, 1u);

    request.kinds = MemoryCounters::kWrite | MemoryCounters::kExecute;
    request.min = 2;
    r = MemoryCounters::Read(_context, request);
    ASSERT_EQ(r.cells.size(), 1u) << "written or run at least twice";
    EXPECT_EQ(r.cells[0].address, 0x9001u);

    request = {};
    request.mode = MemoryCounters::Mode::Top;
    request.limit = 2;
    r = MemoryCounters::Read(_context, request);
    ASSERT_EQ(r.cells.size(), 2u);
    EXPECT_EQ(r.cells[0].address, 0x9000u) << "heaviest first";
    EXPECT_EQ(r.cells[1].address, 0x9001u);
    EXPECT_EQ(r.matched, 3u);
    EXPECT_TRUE(r.truncated);

    request = {};
    request.mode = MemoryCounters::Mode::Dense;
    request.start = 0x9000;
    request.length = 4;
    r = MemoryCounters::Read(_context, request);
    ASSERT_EQ(r.cells.size(), 4u) << "dense: zeros too";
    EXPECT_EQ(r.cells[3].reads + r.cells[3].writes + r.cells[3].executes, 0u);
    request.start = 0xFFFE;
    request.length = 100;
    EXPECT_EQ(MemoryCounters::Read(_context, request).cells.size(), 2u) << "a window stops at the space's end";

    request = {};
    request.addresses = {0x9002, 0x9000, 0x1234};
    r = MemoryCounters::Read(_context, request);
    ASSERT_EQ(r.cells.size(), 3u) << "named cells in their order";
    EXPECT_EQ(r.cells[0].executes, 1u);
    EXPECT_EQ(r.cells[1].reads, 5u);
    EXPECT_EQ(r.cells[2].reads, 0u);

    // phys: the page of a cell
    request = {};
    request.space = "phys";
    request.mode = MemoryCounters::Mode::Top;
    request.limit = 1;
    r = MemoryCounters::Read(_context, request);
    ASSERT_EQ(r.cells.size(), 1u);
    EXPECT_EQ(MemoryCounters::PhysPageName(r.cells[0].address / 0x4000), "ram2");
    EXPECT_EQ(r.cells[0].address % 0x4000, 0x1000u);
}

TEST_F(MemoryCounters_Test, RefusalsSayWhy)
{
    MemoryCounters::Request request;
    request.space = "neogs.ram";
    EXPECT_NE(MemoryCounters::Read(_context, request).error.find("no access counters"), std::string::npos);
    request.space = "ram9";
    EXPECT_EQ(MemoryCounters::Read(_context, request).error, "this machine has no page ram9");
    request.space = "cpu";
    request.start = 0x10000;
    EXPECT_NE(MemoryCounters::Read(_context, request).error.find("past the end"), std::string::npos);
    request = {};
    request.space = "phys";
    request.mode = MemoryCounters::Mode::Dense;
    EXPECT_NE(MemoryCounters::Read(_context, request).error.find("at most 1048576"), std::string::npos);
    request = {};
    request.addresses = {0x10000};
    EXPECT_NE(MemoryCounters::Read(_context, request).error.find("past the end"), std::string::npos);
    request = {};
    request.kinds = 0;
    EXPECT_FALSE(MemoryCounters::Read(_context, request).error.empty());
}

TEST_F(MemoryCounters_Test, ParsersTakeTheSurfaceForms)
{
    uint8_t kinds = 0;
    std::string error;
    ASSERT_TRUE(MemoryCounters::ParseKinds("rx", kinds, error));
    EXPECT_EQ(kinds, MemoryCounters::kRead | MemoryCounters::kExecute);
    ASSERT_TRUE(MemoryCounters::ParseKinds("all", kinds, error));
    EXPECT_EQ(kinds, MemoryCounters::kAll);
    EXPECT_EQ(MemoryCounters::KindsName(MemoryCounters::kWrite | MemoryCounters::kExecute), "wx");
    EXPECT_FALSE(MemoryCounters::ParseKinds("q", kinds, error));

    std::vector<uint32_t> addresses;
    ASSERT_TRUE(MemoryCounters::ParseAddresses("0x5C00,#5C08 23560, 0x8000-0x8002", addresses, error)) << error;
    EXPECT_EQ(addresses, (std::vector<uint32_t>{0x5C00, 0x5C08, 23560, 0x8000, 0x8001, 0x8002}));
    EXPECT_FALSE(MemoryCounters::ParseAddresses("0x8002-0x8000", addresses, error));
    EXPECT_FALSE(MemoryCounters::ParseAddresses("zz", addresses, error));
    EXPECT_FALSE(MemoryCounters::ParseAddresses("0-0x10000", addresses, error)) << "more than 65536 cells";

    EXPECT_EQ(MemoryCounters::PhysPageName(5), "ram5");
    EXPECT_EQ(MemoryCounters::PhysPageName(MAX_RAM_PAGES + 1), "cache1");
    EXPECT_EQ(MemoryCounters::PhysPageName(MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES + 2), "rom2");
}

TEST_F(MemoryCounters_Test, WatchesCountRegionsAndMaskedPorts)
{
    ASSERT_NO_FATAL_FAILURE(StartSession());
    MemoryWatch::Options options;
    options.callers = true;
    options.values = true;
    // A region up to #FFFF: its address map once looped forever (a 16-bit counter)
    MemoryWatch::Result added = MemoryWatch::AddRegion(_emulator.get(), "top", 0xFF00, 0x100, options);
    ASSERT_TRUE(added.ok) << added.error;
    EXPECT_EQ(added.moment, "stopped");
    ASSERT_EQ(added.watches.size(), 1u);
    EXPECT_EQ(added.watches[0].length, 0x100u);
    ASSERT_TRUE(MemoryWatch::AddPort(_emulator.get(), "kempston", 0x1F, 0x00FF, options).ok);

    _tracker->TrackMemoryRead(0xFFFF, 0x42, 0x8000);
    _tracker->TrackMemoryWrite(0xFF00, 0x43, 0x8001);
    _tracker->TrackMemoryRead(0xFEFF, 0x44, 0x8002);   // outside
    _tracker->TrackPortRead(0x001F, 0x01, 0x9000);
    _tracker->TrackPortRead(0xFF1F, 0x02, 0x9000);     // the high byte is not decoded
    _tracker->TrackPortRead(0x00FE, 0xBF, 0x9000);     // another port

    const MemoryWatch::Result list = MemoryWatch::List(_emulator.get());
    ASSERT_TRUE(list.ok) << list.error;
    ASSERT_EQ(list.watches.size(), 2u);
    const MemoryWatch::Watch& kempston = list.watches[0];   // by name
    EXPECT_EQ(kempston.name, "kempston");
    EXPECT_TRUE(kempston.port);
    EXPECT_EQ(kempston.reads, 2u);
    EXPECT_EQ(kempston.callers.at(0x9000), 2u);
    EXPECT_EQ(kempston.values.size(), 2u);
    const MemoryWatch::Watch& top = list.watches[1];
    EXPECT_EQ(top.reads, 1u);
    EXPECT_EQ(top.writes, 1u);
    EXPECT_EQ(top.values.at(0x42), 1u);
    EXPECT_EQ(top.callers.count(0x8002), 0u);

    uint64_t revision = 0;
    _tracker->PublishWatches();
    std::vector<PublishedWatch> published = _tracker->PublishedWatches(&revision);
    ASSERT_EQ(published.size(), 2u) << "the debug.watches topic's totals";
    EXPECT_GT(revision, 0u);

    EXPECT_NE(MemoryWatch::AddPort(_emulator.get(), "top", 1, 0xFFFF, options).error.find("exists"), std::string::npos)
        << "one name space over regions and ports";
    EXPECT_FALSE(MemoryWatch::AddRegion(_emulator.get(), "wide", 0xFF00, 0x101, options).ok) << "past #FFFF";
    EXPECT_FALSE(MemoryWatch::AddRegion(_emulator.get(), "bad name", 0, 1, options).ok);
    EXPECT_FALSE(MemoryWatch::AddPort(_emulator.get(), "none", 0xFE, 0, options).ok) << "a zero mask";

    EXPECT_TRUE(MemoryWatch::Remove(_emulator.get(), "top").ok);
    EXPECT_NE(MemoryWatch::Remove(_emulator.get(), "top").error.find("no watch"), std::string::npos);
    ASSERT_TRUE(MemoryWatch::Clear(_emulator.get()).ok);
    EXPECT_TRUE(MemoryWatch::List(_emulator.get()).watches.empty());
    EXPECT_TRUE(_tracker->PublishedWatches().empty()) << "a change publishes at once";
}

TEST_F(MemoryCounters_Test, RunningMachineCountsAndWatches)
{
    _emulator->EnableTurboMode();
    ASSERT_NO_FATAL_FAILURE(StartSession());
    MemoryWatch::Options options;
    options.callers = true;
    _emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return _emulator->IsRunning(); }));
    // The 48K ROM scans the keyboard (IN #FE) every interrupt
    const MemoryWatch::Result added = MemoryWatch::AddPort(_emulator.get(), "keys", 0xFE, 0x00FF, options);
    ASSERT_TRUE(added.ok) << added.error;
    EXPECT_EQ(added.moment, "frame") << "between two frames, without a pause";

    MemoryWatch::Result list;
    ASSERT_TRUE(TestWait::For([&] {
        list = MemoryWatch::List(_emulator.get());
        return list.ok && !list.watches.empty() && list.watches[0].reads > 0;
    })) << list.error;
    EXPECT_FALSE(list.watches[0].callers.empty());
    ASSERT_TRUE(TestWait::For([&] { return !_tracker->PublishedWatches().empty() && _tracker->PublishedWatches()[0].readCount > 0; }))
        << "a frame's end publishes the totals";

    MemoryCounters::Request request;
    request.mode = MemoryCounters::Mode::Top;
    request.kinds = MemoryCounters::kExecute;
    request.limit = 8;
    const MemoryCounters::Result top = MemoryCounters::Read(_context, request);
    ASSERT_TRUE(top.error.empty()) << top.error;
    ASSERT_FALSE(top.cells.empty());
    EXPECT_GT(top.cells[0].executes, 0u);
    EXPECT_LT(top.cells[0].address, 0x4000u) << "the ROM runs";
}
