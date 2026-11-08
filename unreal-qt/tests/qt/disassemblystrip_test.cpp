// The rows the disassembly view keeps (DisassemblyStrip; docs/inprogress/2026-11-05-disasm-virtual-scroll §3.4):
// scrolling up N then down N shows the same rows, past the strip's ends too, and the strip is dropped when the bytes
// or the marks under it change.

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "debugger/disassembler/disassemblywindow.h"
#include "debugger/disassemblystrip.h"
#include "emulator/emulatorcontext.h"

namespace
{
/// Memory the test writes to while the strip keeps rows of it
class LiveSource : public disasm::Source
{
public:
    std::vector<uint8_t> memory = std::vector<uint8_t>(0x10000, 0x00);
    disasm::Marks marks;

    uint8_t Read(uint16_t address) const override { return memory[address]; }
    void MarksOf(uint16_t from, uint16_t to, disasm::Marks& out) const override
    {
        out = {};
        for (uint16_t anchor : marks.anchors)
            if (anchor >= from && anchor <= to)
                out.anchors.push_back(anchor);
        for (const disasm::Range& range : marks.data)
            if (range.hi >= from && range.lo <= to)
                out.data.push_back({std::max(range.lo, from), std::min(range.hi, to)});
    }
};

/// Bytes no assembler wrote: every prefix, every length
void FillNoise(std::vector<uint8_t>& memory, uint32_t seed)
{
    for (uint8_t& byte : memory)
    {
        seed = seed * 1664525u + 1013904223u;
        byte = static_cast<uint8_t>(seed >> 24);
    }
}

std::vector<uint16_t> Shown(const DisassemblyStrip& strip, size_t count)
{
    std::vector<uint16_t> addresses;
    for (size_t i = strip.Top(); i < strip.Rows().size() && addresses.size() < count; ++i)
        addresses.push_back(strip.Rows()[i].address);
    return addresses;
}
}  // namespace

class DisassemblyStrip_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = std::make_unique<EmulatorContext>();
        _disassembler = std::make_unique<Z80Disassembler>(_context.get());
        FillNoise(_source.memory, 11);
        _source.marks.anchors = {0x8123, 0x8400};
        _source.marks.data = {{0x8200, 0x823F}};
    }

    std::unique_ptr<EmulatorContext> _context;
    std::unique_ptr<Z80Disassembler> _disassembler;
    LiveSource _source;
};

TEST_F(DisassemblyStrip_Test, UpThenDownShowsTheSameRows)
{
    disasm::Window window(*_disassembler, _source);
    DisassemblyStrip strip;
    constexpr size_t kVisible = 30;
    strip.ResetAt(window, _source, 1, 0x8300, kVisible);
    strip.Ensure(window, _source, 0, kVisible);
    const std::vector<uint16_t> start = Shown(strip, kVisible);
    ASSERT_EQ(start.front(), 0x8300);

    // Row by row and by pages, well past the rows the strip had (through the anchor, the data range, the noise)
    for (int step : {1, 13, 40})
    {
        constexpr int kRows = 240;
        for (int moved = 0; moved < kRows; moved += step)
            ASSERT_EQ(strip.Scroll(window, _source, -step, kVisible), -step) << "step " << step;
        for (int moved = 0; moved < kRows; moved += step)
            ASSERT_EQ(strip.Scroll(window, _source, step, kVisible), step) << "step " << step;
        EXPECT_EQ(Shown(strip, kVisible), start) << "step " << step;
        EXPECT_TRUE(strip.Validate(_source, 1));
    }

    // Down then up as well
    for (int i = 0; i < 300; ++i)
        strip.Scroll(window, _source, 1, kVisible);
    for (int i = 0; i < 300; ++i)
        strip.Scroll(window, _source, -1, kVisible);
    EXPECT_EQ(Shown(strip, kVisible), start);
}

TEST_F(DisassemblyStrip_Test, RowsAreAChainWithoutGaps)
{
    disasm::Window window(*_disassembler, _source);
    DisassemblyStrip strip;
    strip.ResetAround(window, _source, 1, 0x8400, 600, 100);
    EXPECT_EQ(strip.Top(), 600u) << "the rows above the address";
    EXPECT_EQ(strip.TopAddress(), 0x8400);
    ASSERT_LT(strip.Rows().front().address, 0x8123) << "the rows reach past the marks below";
    EXPECT_GE(strip.IndexOf(0x8400), 0) << "an anchor starts a row";
    EXPECT_GE(strip.IndexOf(0x8123), 0) << "an anchor starts a row";
    EXPECT_GE(strip.IndexOf(0x8200), 0) << "a data range starts a row";
    for (size_t i = 1; i < strip.Rows().size(); ++i)
        ASSERT_EQ(strip.Rows()[i].address, strip.Rows()[i - 1].address + strip.Rows()[i - 1].length) << i;
}

TEST_F(DisassemblyStrip_Test, DroppedWhenItsBytesOrMarksChange)
{
    disasm::Window window(*_disassembler, _source);
    DisassemblyStrip strip;
    strip.ResetAt(window, _source, 7, 0x8300, 20);
    const uint16_t end = strip.Rows().back().End();
    EXPECT_TRUE(strip.Validate(_source, 7));

    _source.memory[static_cast<uint16_t>(end + 1)] ^= 0xFF;  // past the strip: kept
    EXPECT_TRUE(strip.Validate(_source, 7));

    EXPECT_FALSE(strip.Validate(_source, 8)) << "another markup revision";
    EXPECT_TRUE(strip.Empty());

    strip.ResetAt(window, _source, 7, 0x8300, 20);
    _source.memory[0x8305] ^= 0xFF;  // a write under it
    EXPECT_FALSE(strip.Validate(_source, 7));

    strip.ResetAt(window, _source, 7, 0x8300, 20);
    _source.marks.anchors.push_back(static_cast<uint16_t>(strip.Rows()[3].address + 1));  // a label mid-row
    std::sort(_source.marks.anchors.begin(), _source.marks.anchors.end());
    EXPECT_FALSE(strip.Validate(_source, 7));
}
