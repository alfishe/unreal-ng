#include "pch.h"

#include <chrono>

#include "debugger/disassembler/disassemblywindow.h"

using disasm::BufferSource;
using disasm::Marks;
using disasm::Row;
using disasm::RowKind;

/// The virtual disassembly window (docs/inprogress/2026-11-05-disasm-virtual-scroll §3): forward rows, the local
/// backward sync, anchors and data ranges, snapping
class DisassemblyWindow_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = new EmulatorContext();
        _disasm = new Z80Disassembler(_context);
    }
    void TearDown() override
    {
        delete _disasm;
        delete _context;
    }

    static std::vector<uint8_t> MemoryWith(uint16_t address, const std::vector<uint8_t>& bytes)
    {
        std::vector<uint8_t> memory(0x10000, 0x00);
        std::copy(bytes.begin(), bytes.end(), memory.begin() + address);
        return memory;
    }
    /// Bytes no assembler wrote: every prefix, every length
    static std::vector<uint8_t> Noise(uint32_t seed)
    {
        std::vector<uint8_t> memory(0x10000);
        for (uint8_t& byte : memory)
        {
            seed = seed * 1664525u + 1013904223u;
            byte = static_cast<uint8_t>(seed >> 24);
        }
        return memory;
    }

    EmulatorContext* _context = nullptr;
    Z80Disassembler* _disasm = nullptr;
};

TEST_F(DisassemblyWindow_Test, RowsDecodeForward)
{
    BufferSource source(MemoryWith(0x8000, {0x3E, 0x01, 0xC9}));
    disasm::Window window(*_disasm, source);
    const std::vector<Row> rows = window.Rows(0x8000, 2);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].address, 0x8000);
    EXPECT_EQ(rows[0].length, 2);
    EXPECT_EQ(rows[0].kind, RowKind::Instruction);
    EXPECT_EQ(rows[0].mnemonic, "ld a,#01");
    EXPECT_EQ(rows[1].address, 0x8002);
    EXPECT_EQ(rows[1].mnemonic, "ret");
}

TEST_F(DisassemblyWindow_Test, UpThenDownGivesTheSameRowsOnNoise)
{
    // W3: the rows above an address lead forward to it, so a strip scrolled up then down shows the same rows
    Marks marks;
    marks.anchors = {0x8123, 0x9400};
    marks.data = {{0x8800, 0x88FF}, {0x9000, 0x9003}};
    BufferSource source(Noise(7), marks);
    disasm::Window window(*_disasm, source);
    for (uint32_t dragged = 0x8010; dragged < 0x9800; dragged += 0x5B)
    {
        const uint16_t top = window.SnapToInstruction(static_cast<uint16_t>(dragged));  // a scrollbar drag lands
        const std::vector<Row> up = window.RowsBefore(static_cast<uint16_t>(top), 16);
        ASSERT_EQ(up.size(), 16u) << std::hex << top;
        const std::vector<Row> down = window.Rows(up.front().address, 17);
        for (size_t i = 0; i < up.size(); ++i)
            ASSERT_EQ(down[i], up[i]) << std::hex << "top " << top << " row " << i << " at " << up[i].address;
        EXPECT_EQ(down[16].address, top) << std::hex << top;
    }
}

TEST_F(DisassemblyWindow_Test, AnAnchorDecidesTheRowBefore)
{
    // NOPs, then 01 3E 01 C9: "ld bc,#013E" from #8000 or "ld a,#01" from #8001; both land on the ret at #8003
    const std::vector<uint8_t> memory = MemoryWith(0x8000, {0x01, 0x3E, 0x01, 0xC9});
    {
        BufferSource source(memory);
        disasm::Window window(*_disasm, source);
        const std::vector<Row> up = window.RowsBefore(0x8003, 1);
        ASSERT_EQ(up.size(), 1u);
        EXPECT_EQ(up[0].address, 0x8000) << "the chain the NOPs lead to";
        EXPECT_EQ(up[0].mnemonic, "ld bc,#013E");
        EXPECT_EQ(window.SnapToInstruction(0x8001), 0x8000);
    }
    Marks marks;
    marks.anchors = {0x8001};  // W4: PC, a label, a breakpoint or the markup says an instruction starts there
    BufferSource source(memory, marks);
    disasm::Window window(*_disasm, source);
    const std::vector<Row> up = window.RowsBefore(0x8003, 2);
    ASSERT_EQ(up.size(), 2u);
    EXPECT_EQ(up[1].address, 0x8001);
    EXPECT_EQ(up[1].mnemonic, "ld a,#01");
    EXPECT_EQ(up[0].address, 0x8000);
    EXPECT_EQ(up[0].kind, RowKind::Data) << "the byte before the anchor, cut";
    EXPECT_TRUE(up[0].cut);
    EXPECT_EQ(up[0].mnemonic, "db #01");
    EXPECT_EQ(window.SnapToInstruction(0x8001), 0x8001);

    const std::vector<Row> down = window.Rows(0x8000, 3);
    ASSERT_EQ(down.size(), 3u);
    EXPECT_EQ(down[0], up[0]);
    EXPECT_EQ(down[1], up[1]);
}

TEST_F(DisassemblyWindow_Test, DataRangesAreDbRowsCutAtAnchors)
{
    Marks marks;
    marks.anchors = {0x9004};
    marks.data = {{0x9000, 0x900F}};
    // ld hl,nn at #8FFF would run into the data range: its byte is a row of its own
    BufferSource source(MemoryWith(0x8FFF, {0x21, 0xAA, 0xBB, 0x01, 0x02, 0x03, 0x04, 0x05}), marks);
    disasm::Window window(*_disasm, source);
    const std::vector<Row> rows = window.Rows(0x8FFF, 5);
    ASSERT_EQ(rows.size(), 5u);
    EXPECT_EQ(rows[0].kind, RowKind::Data);
    EXPECT_TRUE(rows[0].cut);
    EXPECT_EQ(rows[0].length, 1);
    EXPECT_EQ(rows[1].address, 0x9000);
    EXPECT_EQ(rows[1].length, 4);
    EXPECT_EQ(rows[1].mnemonic, "db #AA,#BB,#01,#02");
    EXPECT_FALSE(rows[1].cut);
    EXPECT_EQ(rows[2].address, 0x9004);
    EXPECT_EQ(rows[2].length, 8);
    EXPECT_EQ(rows[3].address, 0x900C);
    EXPECT_EQ(rows[3].length, 4);
    EXPECT_EQ(rows[4].address, 0x9010);
    EXPECT_EQ(rows[4].kind, RowKind::Instruction);

    EXPECT_EQ(window.SnapToInstruction(0x900B), 0x9004);
    EXPECT_EQ(window.SnapToInstruction(0x900D), 0x900C);
    const std::vector<Row> up = window.RowsBefore(0x9010, 3);
    ASSERT_EQ(up.size(), 3u);
    EXPECT_EQ(up[0].address, 0x9000);
    EXPECT_EQ(up[1].address, 0x9004);
    EXPECT_EQ(up[2].address, 0x900C);
}

TEST_F(DisassemblyWindow_Test, ALongDataRangeScrollsUpByRowsFromTheTop)
{
    // Entered mid-range (a go to address), the rows above count back from the top row
    Marks marks;
    marks.data = {{0x6000, 0x7FFF}};
    BufferSource source(Noise(3), marks);
    disasm::Window window(*_disasm, source);
    const std::vector<Row> up = window.RowsBefore(0x7003, 3);
    ASSERT_EQ(up.size(), 3u);
    EXPECT_EQ(up[0].address, 0x6FEB);
    EXPECT_EQ(up[2].address, 0x6FFB);
    EXPECT_EQ(up[2].length, 8);
    EXPECT_EQ(window.SnapToInstruction(0x6005), 0x6000);
}

TEST_F(DisassemblyWindow_Test, TheEndsOfTheAddressSpace)
{
    BufferSource source(MemoryWith(0xFFFE, {0x00, 0x21}));
    disasm::Window window(*_disasm, source);
    const std::vector<Row> rows = window.Rows(0xFFFE, 5);
    ASSERT_EQ(rows.size(), 2u) << "no wrap past #FFFF";
    EXPECT_EQ(rows[1].kind, RowKind::Data);
    EXPECT_TRUE(rows[1].cut) << "ld hl,nn cannot fit in the last byte";
    EXPECT_TRUE(window.RowsBefore(0x0000, 4).empty());
    const std::vector<Row> up = window.RowsBefore(0x0002, 4);
    ASSERT_EQ(up.size(), 2u);
    EXPECT_EQ(up[0].address, 0x0000);
}

TEST_F(DisassemblyWindow_Test, AScreenAroundAnAddressIsCheap)
{
    // W5: a step redraws a screen (rows above and below PC) well inside a frame
    BufferSource source(Noise(11));
    disasm::Window window(*_disasm, source);
    const auto started = std::chrono::steady_clock::now();
    const std::vector<Row> rows = window.Around(0xA000, 32, 32);
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
    EXPECT_EQ(rows.size(), 64u);
    RecordProperty("around_64_rows_us", static_cast<int>(micros));
}
