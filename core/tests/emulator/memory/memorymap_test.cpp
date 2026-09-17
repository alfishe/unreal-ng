// Unit tests for the single-source sparse memory map + compact read formats
// (TD-3 Phase 1). Every automation surface renders these helpers, so the
// block splitting / zero-run folding / coalescing / formatting rules are
// pinned here once.

#include <gtest/gtest.h>

#include <cstring>

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memorymap.h"
#include "emulator/platform.h"

class MemoryMap_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        ASSERT_NE(_context, nullptr);
        if (_context->pModuleLogger)
            _context->pModuleLogger->TurnOffLoggingForAll();

        _memory = new Memory(_context);
        ASSERT_NE(_memory, nullptr);
        _memory->Reset();
        _memory->DefaultBanksFor48k();

        // 48K model identity for BuildMemoryMap (model name + ram page count)
        _context->config.mem_model = MM_SPECTRUM48;
        _context->config.ramsize = 48;
    }

    void TearDown() override
    {
        delete _memory;
        _memory = nullptr;
        delete _context;
        _context = nullptr;
    }

    /// Zeroes every page visible in the 48K address view so assertions do not
    /// depend on ROM load state or leftover RAM contents
    void ZeroAddressView()
    {
        const uint16_t ramPages[] = {5, 2, 0};
        for (uint16_t page : ramPages)
        {
            uint8_t* host = _memory->RAMPageAddress(page);
            ASSERT_NE(host, nullptr);
            std::memset(host, 0, PAGE_SIZE);
        }
        uint8_t* rom = _memory->ROMPageHostAddress(0);
        ASSERT_NE(rom, nullptr);
        std::memset(rom, 0, PAGE_SIZE);
    }
};

/// region <BuildMemoryMap: address view>

TEST_F(MemoryMap_Test, AddressView_SplitsZeroAndDataRegions)
{
    ZeroAddressView();
    for (uint32_t i = 0; i < 0x400; i++)
        _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x9000 + i), 0xAA);

    MemoryMapReport report = BuildMemoryMap(*_memory, _context->config, MemoryMapView::AddressSpace);

    EXPECT_EQ(report.totalSize, 65536u);
    EXPECT_EQ(report.ramView, false);
    EXPECT_EQ(report.nonZeroBytes, 0x400u);
    ASSERT_EQ(report.blocks.size(), 6u); // bank0, bank1, zero/data/zero in bank2, bank3

    const MemoryMapBlock& data = report.blocks[3];
    EXPECT_EQ(data.address, 0x9000u);
    EXPECT_EQ(data.size, 0x400u);
    EXPECT_EQ(data.bank, 2);
    EXPECT_EQ(data.page, 2);
    EXPECT_EQ(data.isRom, false);
    EXPECT_EQ(data.nonZero, 0x400u);
    EXPECT_EQ(data.typeName, "ram2");
    EXPECT_NE(data.hash, 0u);
    EXPECT_FALSE(data.IsZeroFill());

    // Zero-fill blocks around the data region, never merging across banks
    EXPECT_TRUE(report.blocks[0].IsZeroFill() && report.blocks[0].address == 0x0000u);
    EXPECT_TRUE(report.blocks[1].IsZeroFill() && report.blocks[1].address == 0x4000u);
    EXPECT_TRUE(report.blocks[2].IsZeroFill() && report.blocks[2].address == 0x8000u &&
                report.blocks[2].size == 0x1000u);
    EXPECT_TRUE(report.blocks[4].IsZeroFill() && report.blocks[4].address == 0x9400u &&
                report.blocks[4].size == 0xBFFF - 0x9400u + 1);
    EXPECT_TRUE(report.blocks[5].IsZeroFill() && report.blocks[5].address == 0xC000u);
}

TEST_F(MemoryMap_Test, AddressView_MinRunFoldsShortZeroRuns)
{
    ZeroAddressView();
    for (uint32_t i = 0; i < 0x100; i++)
    {
        // 0x8000-0x803F data, 0x8040-0x805F zeros (32 < minRun 64), 0x8060-0x80FF data
        const bool zeroHole = i >= 0x40 && i < 0x60;
        _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), zeroHole ? 0 : 0x5A);
    }

    // Default minRun folds the 32-byte hole into one data block
    MemoryMapReport folded = BuildMemoryMap(*_memory, _context->config, MemoryMapView::AddressSpace,
                                            kMemoryMapDefaultMinRun, kMemoryMapDefaultMaxBlocks);
    ASSERT_EQ(folded.blocks.size(), 5u); // bank0, bank1, data + zero tail in bank2, bank3
    EXPECT_EQ(folded.blocks[2].address, 0x8000u);
    EXPECT_EQ(folded.blocks[2].size, 0x100u);
    EXPECT_EQ(folded.blocks[2].nonZero, 0xE0u);

    // minRun 16 keeps the hole as its own zero block: data, zeros, data, zero tail
    MemoryMapReport split = BuildMemoryMap(*_memory, _context->config, MemoryMapView::AddressSpace, 16, 48);
    ASSERT_EQ(split.blocks.size(), 7u);
    EXPECT_EQ(split.blocks[2].address, 0x8000u);
    EXPECT_EQ(split.blocks[2].size, 0x40u);
    EXPECT_TRUE(split.blocks[3].IsZeroFill());
    EXPECT_EQ(split.blocks[3].address, 0x8040u);
    EXPECT_EQ(split.blocks[3].size, 0x20u);
    EXPECT_EQ(split.blocks[4].address, 0x8060u);
    EXPECT_EQ(split.blocks[4].size, 0xA0u);
}

TEST_F(MemoryMap_Test, AddressView_HashTracksContentChanges)
{
    ZeroAddressView();
    for (uint32_t i = 0; i < 0x100; i++)
        _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), 0x11);

    MemoryMapReport before = BuildMemoryMap(*_memory, _context->config);
    _memory->DirectWriteToZ80Memory(0x8080, 0x22);
    MemoryMapReport after = BuildMemoryMap(*_memory, _context->config);

    const uint64_t hashBefore = before.blocks[2].hash;
    const uint64_t hashAfter = after.blocks[2].hash;
    EXPECT_NE(hashBefore, hashAfter);
    EXPECT_EQ(after.blocks[2].nonZero, before.blocks[2].nonZero);
}

TEST_F(MemoryMap_Test, AddressView_BlockBudgetRaisesMinRunThenTruncates)
{
    ZeroAddressView();
    _memory->DirectWriteToZ80Memory(0x8000, 1);
    _memory->DirectWriteToZ80Memory(0xC000, 1);

    // Four banks each produce one block (the 1-byte data run folds with its
    // zero tail once minRun reaches 16K), so a budget of 2 cannot fit
    MemoryMapReport report = BuildMemoryMap(*_memory, _context->config, MemoryMapView::AddressSpace, 64, 2);

    EXPECT_TRUE(report.truncated);
    EXPECT_EQ(report.minRun, PAGE_SIZE);
    EXPECT_EQ(report.blocks.size(), 2u);
}

/// endregion

/// region <BuildMemoryMap: ram view>

TEST_F(MemoryMap_Test, RamView_CoalescesZeroPages)
{
    // 128K = 8 RAM pages; only page 3 carries data
    _context->config.ramsize = 128;
    for (uint32_t page = 0; page < 8; page++)
    {
        uint8_t* host = _memory->RAMPageAddress(static_cast<uint16_t>(page));
        ASSERT_NE(host, nullptr);
        std::memset(host, 0, PAGE_SIZE);
    }
    uint8_t* page3 = _memory->RAMPageAddress(3);
    std::memset(page3, 0x77, 0x100);

    MemoryMapReport report = BuildMemoryMap(*_memory, _context->config, MemoryMapView::RamPages);

    EXPECT_EQ(report.ramView, true);
    EXPECT_EQ(report.totalSize, 8u * PAGE_SIZE);
    EXPECT_EQ(report.nonZeroBytes, 0x100u);
    ASSERT_EQ(report.blocks.size(), 3u);

    // pages 0-2 coalesce into one span, then the page-3 data run...
    EXPECT_EQ(report.blocks[0].typeName, "ram0-ram2");
    EXPECT_EQ(report.blocks[0].size, 3u * PAGE_SIZE);
    EXPECT_TRUE(report.blocks[0].IsZeroFill());

    EXPECT_EQ(report.blocks[1].typeName, "ram3");
    EXPECT_EQ(report.blocks[1].address, 3u * PAGE_SIZE);
    EXPECT_EQ(report.blocks[1].size, 0x100u);
    EXPECT_EQ(report.blocks[1].nonZero, 0x100u);

    // ...and its zero tail merges with the all-zero pages 4-7 into one span
    EXPECT_TRUE(report.blocks[2].IsZeroFill());
    EXPECT_EQ(report.blocks[2].address, 3u * PAGE_SIZE + 0x100u);
    EXPECT_EQ(report.blocks[2].size, 5u * PAGE_SIZE - 0x100u);
    EXPECT_EQ(report.blocks[2].typeName, "ram3-ram7");
}

/// endregion

/// region <FormatHexDump>

TEST(MemoryMapFormat_Test, HexDump_GroupsOfEightWithAsciiSidebar)
{
    const uint8_t data[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                              0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    const std::string dump = FormatHexDump(data, sizeof(data), 0);

    EXPECT_EQ(dump,
              "0x0000: 00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F  |................|\n");
}

TEST(MemoryMapFormat_Test, HexDump_PartialLinePadsAndUsesBaseAddress)
{
    const uint8_t data[9] = {0x3E, 0x21, 0x00, 0x41, 0xFF, 0x7E, 0x7F, 0x20, 0x01};
    const std::string dump = FormatHexDump(data, sizeof(data), 0x60);

    // 23 spaces: the slot-8 separator, 6 empty 3-char slots, the 2-char
    // slot-15 field and the 2-char pre-sidebar separator keep full-line alignment
    const std::string expected = "0x0060: 3E 21 00 41 FF 7E 7F 20  01" + std::string(23, ' ') +
                                 "|>!.A.~. .|\n";
    EXPECT_EQ(dump, expected);
}

/// endregion

/// region <BuildSparseSegments / CountNonZeroBytes>

TEST(MemoryMapFormat_Test, SparseSegments_SplitFillRunsAtThreshold)
{
    const uint8_t data[12] = {0xAA, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xBB};

    const std::vector<MemorySparseSegment> split = BuildSparseSegments(data, sizeof(data), 8);
    ASSERT_EQ(split.size(), 3u);
    EXPECT_FALSE(split[0].isFill);
    EXPECT_EQ(split[0].offset, 0u);
    EXPECT_EQ(split[0].hex, "aa");
    EXPECT_TRUE(split[1].isFill);
    EXPECT_EQ(split[1].fill, 0);
    EXPECT_EQ(split[1].offset, 1u);
    EXPECT_EQ(split[1].length, 10u);
    EXPECT_FALSE(split[2].isFill);
    EXPECT_EQ(split[2].offset, 11u);
    EXPECT_EQ(split[2].hex, "bb");

    // Below the threshold the zero run stays inside the hex payload
    const std::vector<MemorySparseSegment> folded = BuildSparseSegments(data, sizeof(data), 16);
    ASSERT_EQ(folded.size(), 1u);
    EXPECT_EQ(folded[0].hex, "aa00000000000000000000bb");
}

TEST(MemoryMapFormat_Test, SparseSegments_RecognizesFFFill)
{
    const uint8_t data[12] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    const std::vector<MemorySparseSegment> segments = BuildSparseSegments(data, sizeof(data), 8);
    ASSERT_EQ(segments.size(), 1u);
    EXPECT_TRUE(segments[0].isFill);
    EXPECT_EQ(segments[0].fill, 0xFF);
    EXPECT_EQ(segments[0].length, 12u);
    EXPECT_EQ(CountNonZeroBytes(data, sizeof(data)), 12u);
}

/// endregion
