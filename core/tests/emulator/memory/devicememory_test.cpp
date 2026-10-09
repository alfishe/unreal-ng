// Device memory regions (emulator/memory/devicememory.h; Sprinter automation audit G5): the
// Sprinter's video RAM by name - list, read, write through the device's path, save / load, the
// CLI text and the video map's byte -> pixels for space "vram". Every interface calls these.

#include "../machines/sprinter/sprinterfixture.h"

#include <string>
#include <vector>

#include "../../../automation/cli/src/commands/cli-memory-region.h"
#include "../../../automation/gdb/include/gdbmonitormemory.h"
#include "_helpers/testpathhelper.h"
#include "debugger/memory/memoryread.h"
#include "debugger/search/memorysearch.h"
#include "emulator/memory/devicememory.h"
#include "emulator/state/devicestate.h"
#include "emulator/video/sprinter/sprintervideoram.h"

namespace
{
const StateNode* Member(const StateNode& node, const char* key) { return node.find(key); }
}  // namespace

class DeviceMemory_Test : public SprinterFixture
{
};

TEST_F(DeviceMemory_Test, SprinterListsItsVideoRam)
{
    const std::vector<IDeviceMemoryRegion*> regions = DeviceMemory::Regions(_context);
    ASSERT_EQ(regions.size(), 3u) << "vram, the CMOS clock's cmos, then the fast RAM time travel records";
    EXPECT_STREQ(regions[0]->Name(), "sprinter.vram");
    EXPECT_STREQ(regions[1]->Name(), "rtc.cmos");
    EXPECT_STREQ(regions[2]->Name(), "sprinter.fastram");
    EXPECT_EQ(regions[0]->Size(), 256u * 1024u);
    EXPECT_EQ(regions[2]->Size(), 64u * 1024u);
    EXPECT_EQ(DeviceMemory::Find(_context, "VRAM"), regions[0]) << "aliases, case-insensitive";
    EXPECT_EQ(DeviceMemory::Find(_context, "Sprinter.VRAM"), regions[0]);

    const StateNode list = DeviceState::MemoryRegions(_context);
    const StateNode* items = Member(list, "regions");
    ASSERT_NE(items, nullptr);
    ASSERT_EQ(items->items.size(), 3u);
    EXPECT_EQ(Member(items->items[0], "pages")->i, 16);
    EXPECT_EQ(Member(items->items[0], "aliases")->items.at(0).s, "vram");
    EXPECT_EQ(Member(items->items[0], "ttd_region")->s, "sprinter.vram");
    EXPECT_EQ(Member(items->items[1], "ttd_region")->kind, StateNode::Kind::Null) << "the CMOS is device state, no region";

    std::string error;
    EXPECT_EQ(DeviceMemory::Find(_context, "gsram", &error), nullptr);
    EXPECT_NE(error.find("regions: sprinter.vram, rtc.cmos, sprinter.fastram"), std::string::npos) << error;
}

// A write goes through SprinterVideoRam::Write: the pen follows the palette bytes
TEST_F(DeviceMemory_Test, WriteTakesTheDevicePath)
{
    const uint32_t pen = 0x405;  // text paper 5
    const uint32_t at = SprinterVideoRam::PenAddress(pen);
    std::string error;
    ASSERT_TRUE(DeviceMemory::Write(_context, "vram", at, {0x00, 0x00, 0xA8}, "test", error)) << error;
    EXPECT_EQ(_decoder->GetVideoRam().Pen(pen), 0xFFA80000u) << "R, G, B -> RGBA #AABBGGRR: blue";

    std::vector<uint8_t> bytes;
    ASSERT_TRUE(DeviceMemory::Read(_context, "vram", at, 3, bytes, error)) << error;
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0x00, 0x00, 0xA8}));

    const StateNode hex = DeviceState::MemoryRegionRead(_context, "vram", at, 3, "hex");
    EXPECT_EQ(Member(hex, "hex")->s, "0000A8");
    const StateNode data = DeviceState::MemoryRegionRead(_context, "vram", at, 3, "data");
    EXPECT_EQ(Member(data, "data")->items[2].i, 0xA8);

    // Ranges are checked, nothing is clipped silently
    EXPECT_FALSE(DeviceMemory::Read(_context, "vram", 0x3FFFF, 2, bytes, error));
    EXPECT_FALSE(DeviceMemory::Write(_context, "vram", 0x40000, {1}, "test", error));
    EXPECT_FALSE(Member(DeviceState::MemoryRegionRead(_context, "vram", 0x40000, 1, "hex"), "available")->b);
}

TEST_F(DeviceMemory_Test, SaveAndLoadRoundTrip)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("devicememory-vram.bin");
    std::string error;
    ASSERT_TRUE(DeviceMemory::Write(_context, "vram", 0x100, {1, 2, 3, 4}, "test", error)) << error;
    ASSERT_TRUE(DeviceMemory::Save(_context, "vram", path, 0x100, 4, error)) << error;
    ASSERT_TRUE(DeviceMemory::Write(_context, "vram", 0x100, {0, 0, 0, 0}, "test", error)) << error;
    size_t written = 0;
    ASSERT_TRUE(DeviceMemory::Load(_context, "vram", path, 0x200, written, error)) << error;
    EXPECT_EQ(written, 4u);
    EXPECT_EQ(_decoder->GetVideoRam().Read(0x203), 4);
    std::remove(path.c_str());
}

TEST_F(DeviceMemory_Test, CliRendersRegions)
{
    const std::string list = CliMemoryRegion::Text(_context, {"regions"});
    EXPECT_NE(list.find("  sprinter.vram (vram)  262144 bytes, 16 pages of 16384"), std::string::npos) << list;
    EXPECT_NE(list.find("  sprinter.fastram      65536 bytes"), std::string::npos) << "names padded to the longest: " << list;
    EXPECT_NE(CliMemoryRegion::Text(_context, {"region", "write", "vram", "0x10", "AB", "CD"}).find("Wrote 2 bytes"),
              std::string::npos);
    const std::string dump = CliMemoryRegion::Text(_context, {"region", "read", "vram", "0x10", "2"});
    EXPECT_NE(dump.find("AB CD"), std::string::npos) << dump;
    EXPECT_NE(CliMemoryRegion::Text(_context, {"region", "read", "nope", "0"}).find("Error: no memory region"),
              std::string::npos);
}

// /video/address?space=vram: a byte -> the pixels it feeds (the mapper reads video RAM directly)
TEST_F(DeviceMemory_Test, VideoAddressAcceptsVram)
{
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    Pld().rgMod = 0;
    vram.Write(SprinterVideoRam::ModeAddress(0, 0, 0), 0x20);  // graphics 320, source column 0, row 0
    const StateNode n = DeviceState::VideoAddressIn(_context, "vram", 0, 0);
    ASSERT_TRUE(Member(n, "available")->b) << DeviceState::ToText(n);
    EXPECT_TRUE(Member(n, "feeds_picture")->b) << DeviceState::ToText(n);
}

TEST(DeviceMemoryParse_Test, NumbersAndHexBytes)
{
    uint32_t v = 0;
    EXPECT_TRUE(DeviceMemory::ParseNumber("6128", v));
    EXPECT_EQ(v, 6128u);
    EXPECT_TRUE(DeviceMemory::ParseNumber("0x17F0", v));
    EXPECT_EQ(v, 0x17F0u);
    EXPECT_TRUE(DeviceMemory::ParseNumber("#17f0", v));
    EXPECT_EQ(v, 0x17F0u);
    EXPECT_FALSE(DeviceMemory::ParseNumber("12x", v));
    EXPECT_FALSE(DeviceMemory::ParseNumber("", v));

    std::vector<uint8_t> bytes;
    EXPECT_TRUE(DeviceMemory::ParseHexBytes("00 00 a8", bytes));
    EXPECT_EQ(bytes, (std::vector<uint8_t>{0, 0, 0xA8}));
    EXPECT_FALSE(DeviceMemory::ParseHexBytes("0", bytes));
    EXPECT_FALSE(DeviceMemory::ParseHexBytes("zz", bytes));
}

TEST(DeviceMemoryOther_Test, OtherMachinesHaveNone)
{
    EmulatorContext context(LoggerLevel::LogError);
    EXPECT_TRUE(DeviceMemory::Regions(&context).empty());
    EXPECT_NE(CliMemoryRegion::Text(&context, {"regions"}).find("No device memory regions"), std::string::npos);
}

// A region is a space of the search and the windowed read (memory-spaces design, step 2): by name or alias, offsets
// in the region, a match named by region and offset on every surface (ToState, the CLI text)
TEST_F(DeviceMemory_Test, SearchAndReadTakeARegion)
{
    std::string error;
    ASSERT_TRUE(DeviceMemory::Write(_context, "vram", 0x2A345, {0xC3, 0x5A, 0xA5}, "test", error)) << error;
    for (const char* space : {"vram", "sprinter.vram", "Sprinter.VRAM"})
    {
        MemorySearchRequest request;
        ASSERT_TRUE(MemorySearch::BuildRequest("C3 5A A5", nullptr, "", space, 0, 0xFFFFFFFF, 64, 1, request, error)) << error;
        const MemorySearchResult result = MemorySearch::Search(_context, request);
        ASSERT_TRUE(result.error.empty()) << result.error;
        ASSERT_EQ(result.matches.size(), 1u) << space;
        EXPECT_EQ(result.matches[0].address, 0x2A345u);
        EXPECT_EQ(result.matches[0].region, "sprinter.vram");
        const StateNode state = MemorySearch::ToState(request, result);
        ASSERT_NE(state.find("matches")->items[0].find("region"), nullptr) << "named by region";
        EXPECT_EQ(state.find("matches")->items[0].find("region")->s, "sprinter.vram");
        EXPECT_EQ(state.find("matches")->items[0].find("offset")->i, 0x2A345);
    }

    const MemoryRead::Result read = MemoryRead::Bytes(_context, "vram", 0x2A345, 3);
    ASSERT_TRUE(read.error.empty()) << read.error;
    EXPECT_EQ(read.space, "sprinter.vram");
    EXPECT_EQ(read.bytes, (std::vector<uint8_t>{0xC3, 0x5A, 0xA5}));
    EXPECT_EQ(MemoryRead::Bytes(_context, "vram", 0x3FFFE, 16).bytes.size(), 2u) << "stops at the region's end";
    EXPECT_EQ(MemoryRead::Bytes(_context, "vram", 0x40000, 1).error, "past the end of sprinter.vram");
    EXPECT_NE(MemoryRead::Bytes(_context, "nope", 0, 1).error.find("no memory region 'nope'"), std::string::npos);

    std::string space;
    uint32_t address = 0, length = 0;
    ASSERT_TRUE(MemoryRead::ParseWindow("sprinter.vram:0x2A345:3", space, address, length, error)) << error;
    EXPECT_EQ(MemoryRead::Bytes(_context, space, address, length).bytes, read.bytes) << "a snapshot window";
}

// GDB's monitor commands over every memory (gdbmonitormemory.h): the region list, a dump of any space in hex
// (GDB's convention: numbers without a prefix), and the space:offset form findlast takes
TEST_F(DeviceMemory_Test, GdbMonitorListsAndDumpsAnySpace)
{
    std::string error;
    ASSERT_TRUE(DeviceMemory::Write(_context, "vram", 0x4800, {0x01, 0x02, 0x03}, "test", error)) << error;
    const std::string list = GdbMonitorMemory::Regions(_context);
    EXPECT_NE(list.find("  sprinter.vram (vram) size 40000"), std::string::npos) << list;
    EXPECT_NE(list.find("  sprinter.fastram size 10000"), std::string::npos) << list;

    EXPECT_EQ(GdbMonitorMemory::Dump(_context, "vram:4800 3"), "sprinter.vram:04800: 01 02 03\n");
    const std::string twoLines = GdbMonitorMemory::Dump(_context, "sprinter.vram:4800 12");
    EXPECT_NE(twoLines.find("\nsprinter.vram:04810: 00 00\n"), std::string::npos) << twoLines;
    EXPECT_EQ(GdbMonitorMemory::Dump(_context, "vram:40000 1"), "Error: past the end of sprinter.vram\n");
    EXPECT_EQ(GdbMonitorMemory::Dump(_context, "vram:zz").rfind("Usage:", 0), 0u);

    std::string space;
    uint32_t offset = 0;
    ASSERT_TRUE(GdbMonitorMemory::ParseLocation("cache:123", space, offset));
    EXPECT_EQ(space, "cache");
    EXPECT_EQ(offset, 0x123u);
    ASSERT_TRUE(GdbMonitorMemory::ParseLocation("5C78", space, offset));
    EXPECT_EQ(space, "cpu");
    EXPECT_EQ(offset, 0x5C78u);
}
