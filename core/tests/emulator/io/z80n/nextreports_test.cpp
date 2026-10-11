// The Next's debugger reports (docs/inprogress/2026-10-07-zx-next/design-automation-coverage.md, section 5): next_dma, next_video,
// next_palette, next_ports, next_nextreg and the NextREG write control, read from a machine that was programmed through its
// ports and registers. Source: D (design; the hardware semantics are the VHDL's, cited in the device tests)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/goldentext.h"
#include "_helpers/nextreporthelper.h"
#include "debugger/ports/nextregwrite.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextreportquery.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/state/devicestate.h"

class NextReports_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    void Out(uint16_t port, uint8_t value) { _ports->DecodePortOut(port, value, 0); }
    uint8_t In(uint16_t port) { return _ports->DecodePortIn(port, 0); }
    void Nr(uint8_t reg, uint8_t value)
    {
        Out(0x243B, reg);
        Out(0x253B, value);
    }
    void Dma(std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            Out(0x6B, b);
    }
    std::string P(const StateNode& node, const char* path) { return NextReportHelper::Path(node, path); }
};

// ---------------------------------------------------------------------------------------------------------------- next_dma

TEST_F(NextReports_Test, DmaPowerOnState)
{
    const StateNode r = DeviceState::NextDma(_context);
    EXPECT_EQ(P(r, "available"), "on");
    EXPECT_EQ(P(r, "mode"), "zxn");
    EXPECT_EQ(P(r, "enabled"), "off");
    EXPECT_EQ(P(r, "transferring"), "off");
    EXPECT_EQ(P(r, "end_of_block"), "off");
    EXPECT_EQ(P(r, "burst"), "continuous");
    EXPECT_EQ(P(r, "read_mask"), "0x7F");
    EXPECT_EQ(P(r, "read_seq"), "0");
    EXPECT_EQ(P(r, "direction"), "a_to_b");
    EXPECT_EQ(P(r, "holds_bus"), "off");
    EXPECT_EQ(P(r, "interrupt_enables.nr_cc"), "0x00");
}

TEST_F(NextReports_Test, DmaFieldsAreWhatWr0ToWr6Programmed)
{
    // WR0: A -> B, A address #8000, block length #0100; WR1: A memory, increment; WR2: B I/O fixed, timing 2, prescaler #40;
    // WR4: burst, B address #00FE; WR5: CE/WAIT and auto-restart; WR6: LOAD
    Dma({0x7D, 0x00, 0x80, 0x00, 0x01});
    Dma({0x14});
    Dma({0x78, 0x22, 0x40});
    Dma({0xCD, 0xFE, 0x00});
    Dma({0xB2});
    Dma({0xCF});
    const StateNode r = DeviceState::NextDma(_context);
    EXPECT_EQ(P(r, "mode"), "zxn");
    EXPECT_EQ(P(r, "burst"), "burst");
    EXPECT_EQ(P(r, "prescaler"), "64");
    EXPECT_EQ(P(r, "auto_restart"), "on");
    EXPECT_EQ(P(r, "ce_wait"), "on");
    EXPECT_EQ(P(r, "direction"), "a_to_b");
    EXPECT_EQ(P(r, "a.address"), "0x8000");
    EXPECT_EQ(P(r, "a.type"), "memory");
    EXPECT_EQ(P(r, "a.step"), "inc");
    EXPECT_EQ(P(r, "b.address"), "0x00FE");
    EXPECT_EQ(P(r, "b.type"), "io");
    EXPECT_EQ(P(r, "b.step"), "fixed");
    EXPECT_EQ(P(r, "b.timing"), "2");
    EXPECT_EQ(P(r, "block_length"), "256");
    EXPECT_EQ(P(r, "counter"), "0");
    EXPECT_EQ(P(r, "src"), "0x8000");
    EXPECT_EQ(P(r, "dst"), "0x00FE");
    EXPECT_EQ(P(r, "enabled"), "off");
    Dma({0x87});
    EXPECT_EQ(P(DeviceState::NextDma(_context), "enabled"), "on");
    EXPECT_EQ(P(DeviceState::NextDma(_context), "transferring"), "on");
}

TEST_F(NextReports_Test, DmaThroughPort0BIsTheZ80Mode)
{
    Out(0x0B, 0xC3);
    EXPECT_EQ(P(DeviceState::NextDma(_context), "mode"), "z80");
    Out(0x6B, 0xC3);
    EXPECT_EQ(P(DeviceState::NextDma(_context), "mode"), "zxn");
}

TEST_F(NextReports_Test, DmaReportDoesNotAdvanceTheReadSequence)
{
    Dma({0x7D, 0x00, 0x80, 0x10, 0x00, 0x14, 0x10, 0xAD, 0x00, 0xA0, 0xCF});
    Dma({0xBF});  // the read sequence starts at the status byte
    const StateNode first = DeviceState::NextDma(_context);
    const StateNode second = DeviceState::NextDma(_context);
    EXPECT_EQ(P(first, "read_seq"), "0");
    EXPECT_EQ(P(second, "read_seq"), "0") << "R3: reading the report must not step the sequence";
    const std::string status = P(first, "status");
    EXPECT_EQ(status, NextReportHelper::Hex8(In(0x6B))) << "status = the byte the next read returns";
    EXPECT_EQ(P(DeviceState::NextDma(_context), "read_seq"), "1") << "a real read does step it";
}

TEST_F(NextReports_Test, DmaBurstWithAPrescalerShowsWaitingAndReleasesTheBus)
{
    // A #8000 -> B #A000, 16 bytes, burst, prescaler #10
    Dma({0x7D, 0x00, 0x80, 0x10, 0x00, 0x14, 0x50, 0x22, 0x10, 0xCD, 0x00, 0xA0, 0xCF, 0x87});
    EXPECT_EQ(_ports->Dma().Run(256, 0), 1u);
    const StateNode r = DeviceState::NextDma(_context);
    EXPECT_EQ(P(r, "waiting"), "on");
    EXPECT_EQ(P(r, "holds_bus"), "off") << "burst mode releases the bus in the prescaler wait (dma.vhd)";
    EXPECT_EQ(P(r, "counter"), "1");
    EXPECT_EQ(P(r, "src"), "0x8001");
    EXPECT_EQ(P(r, "dst"), "0xA001");
    EXPECT_NE(P(r, "wait_until_28"), "0");
}

TEST_F(NextReports_Test, DmaIntoRomIsVisibleAsTheKindOfTheDestinationSlot)
{
    // the past find: a transfer that wrote into the ROM (B address #0100)
    Dma({0x7D, 0x00, 0x80, 0x10, 0x00, 0x14, 0x10, 0xAD, 0x00, 0x01, 0xCF});
    const StateNode r = DeviceState::NextDma(_context);
    EXPECT_EQ(P(r, "dst"), "0x0100");
    NextMemory* memory = dynamic_cast<NextMemory*>(_context->pMemory);
    ASSERT_NE(memory, nullptr);
    EXPECT_EQ(P(r, "dst_kind"), memory->SlotKind(0));
    EXPECT_EQ(P(r, "src_kind"), memory->SlotKind(4));
}

TEST_F(NextReports_Test, DmaInterruptEnablesShowNrCcToCe)
{
    Nr(0xCC, 0x81);
    Nr(0xCD, 0x08);
    Nr(0xCE, 0x04);
    const StateNode r = DeviceState::NextDma(_context);
    EXPECT_EQ(P(r, "interrupt_enables.nr_cc"), "0x81");
    EXPECT_EQ(P(r, "interrupt_enables.nr_cd"), "0x08");
    EXPECT_EQ(P(r, "interrupt_enables.nr_ce"), "0x04");
}

TEST_F(NextReports_Test, DmaGolden)
{
    Dma({0x7D, 0x00, 0x80, 0x00, 0x01, 0x14, 0x78, 0x22, 0x40, 0xCD, 0xFE, 0x00, 0xB2, 0xCF});
    GoldenText::Expect("next", "dma", DeviceState::ToText(DeviceState::NextDma(_context)));
}

// -------------------------------------------------------------------------------------------------------------- next_video

TEST_F(NextReports_Test, VideoPowerOnState)
{
    const StateNode r = DeviceState::NextVideo(_context);
    EXPECT_EQ(P(r, "layer_order.name"), "SLU");
    EXPECT_EQ(P(r, "layer_order.value"), "0");
    EXPECT_EQ(P(r, "ula.enabled"), "on");
    EXPECT_EQ(P(r, "ula.mode"), "standard");
    EXPECT_EQ(P(r, "layer2.enabled"), "off");
    EXPECT_EQ(P(r, "tilemap.enabled"), "off");
    EXPECT_EQ(P(r, "sprites.enabled"), "off");
    EXPECT_EQ(P(r, "raster.vc"), "0");
}

TEST_F(NextReports_Test, VideoLayerOrderFollowsNr15)
{
    const char* names[8] = {"SLU", "LSU", "SUL", "LUS", "USL", "ULS", "blend", "blend"};
    for (unsigned order = 0; order < 8; order++)
    {
        Nr(0x15, static_cast<uint8_t>(order << 2));
        const StateNode r = DeviceState::NextVideo(_context);
        EXPECT_EQ(P(r, "layer_order.value"), std::to_string(order));
        EXPECT_EQ(P(r, "layer_order.name"), names[order]) << order;
    }
    Nr(0x15, 0x80 | 0x40 | 0x20 | 0x02 | 0x01);
    const StateNode r = DeviceState::NextVideo(_context);
    EXPECT_EQ(P(r, "lores"), "on");
    EXPECT_EQ(P(r, "sprites.enabled"), "on");
    EXPECT_EQ(P(r, "sprites.over_border"), "on");
    EXPECT_EQ(P(r, "sprites.clip_over_border"), "on");
    EXPECT_EQ(P(r, "sprites.zero_on_top"), "on");
}

TEST_F(NextReports_Test, VideoUlaModesFollowPortFf)
{
    const struct { uint8_t ff; const char* mode; } cases[] = {{0x00, "standard"}, {0x01, "screen1"}, {0x02, "hicolour"}, {0x06, "hires"}};
    for (const auto& c : cases)
    {
        Out(0x00FF, c.ff);
        const StateNode r = DeviceState::NextVideo(_context);
        EXPECT_EQ(P(r, "ula.mode"), c.mode) << int(c.ff);
        EXPECT_EQ(P(r, "ula.port_ff"), NextReportHelper::Hex8(c.ff));
    }
    Nr(0x68, 0x80);
    EXPECT_EQ(P(DeviceState::NextVideo(_context), "ula.enabled"), "off");
    Nr(0x26, 7);
    Nr(0x27, 9);
    const StateNode r = DeviceState::NextVideo(_context);
    EXPECT_EQ(P(r, "ula.scroll_x"), "7");
    EXPECT_EQ(P(r, "ula.scroll_y"), "9");
}

TEST_F(NextReports_Test, VideoLayer2ResolutionBankAndClip)
{
    Out(0x123B, 0x02);  // Layer 2 on
    Nr(0x12, 9);
    Nr(0x13, 12);
    Nr(0x70, 0x15);  // 320x256, palette offset 5
    Nr(0x16, 0x10);
    Nr(0x71, 0x01);
    Nr(0x17, 0x20);
    Nr(0x18, 4);
    Nr(0x18, 200);
    Nr(0x18, 8);
    Nr(0x18, 100);
    StateNode r = DeviceState::NextVideo(_context);
    EXPECT_EQ(P(r, "layer2.enabled"), "on");
    EXPECT_EQ(P(r, "layer2.resolution"), "320x256x8");
    EXPECT_EQ(P(r, "layer2.bank"), "9");
    EXPECT_EQ(P(r, "layer2.shadow_bank"), "12");
    EXPECT_EQ(P(r, "layer2.palette_offset"), "5");
    EXPECT_EQ(P(r, "layer2.scroll_x"), "272");
    EXPECT_EQ(P(r, "layer2.scroll_y"), "32");
    EXPECT_EQ(P(r, "layer2.clip.x1"), "4");
    EXPECT_EQ(P(r, "layer2.clip.x2"), "200");
    EXPECT_EQ(P(r, "layer2.clip.y1"), "8");
    EXPECT_EQ(P(r, "layer2.clip.y2"), "100");
    Nr(0x70, 0x20);
    EXPECT_EQ(P(DeviceState::NextVideo(_context), "layer2.resolution"), "640x256x4");
    Nr(0x70, 0x00);
    EXPECT_EQ(P(DeviceState::NextVideo(_context), "layer2.resolution"), "256x192x8");
}

TEST_F(NextReports_Test, VideoTilemapBitsAndBank7Base)
{
    // NR #6B: enable, 80 columns, no attribute entry, palette 2, text, 512 tiles, tilemap over the ULA
    Nr(0x6B, 0x80 | 0x40 | 0x20 | 0x10 | 0x08 | 0x02 | 0x01);
    Nr(0x6E, 0xA0);  // bank 7, 8K offset #2000: the past find (the map read bank 7 as 16K)
    Nr(0x6F, 0x44);
    Nr(0x6C, 0x31);
    Nr(0x4C, 0x0F);
    Nr(0x30, 0x34);
    Nr(0x2F, 0x01);
    Nr(0x31, 0x05);
    const StateNode r = DeviceState::NextVideo(_context);
    EXPECT_EQ(P(r, "tilemap.enabled"), "on");
    EXPECT_EQ(P(r, "tilemap.columns"), "80");
    EXPECT_EQ(P(r, "tilemap.attributes_in_map"), "off");
    EXPECT_EQ(P(r, "tilemap.palette"), "2");
    EXPECT_EQ(P(r, "tilemap.text"), "on");
    EXPECT_EQ(P(r, "tilemap.mode512"), "on");
    EXPECT_EQ(P(r, "tilemap.on_top"), "on");
    EXPECT_EQ(P(r, "tilemap.map_bank"), "7");
    EXPECT_EQ(P(r, "tilemap.map_offset"), "0x2000");
    EXPECT_EQ(P(r, "tilemap.map_wraps_8k"), "on");
    EXPECT_EQ(P(r, "tilemap.tile_bank"), "5");
    EXPECT_EQ(P(r, "tilemap.tile_offset"), "0x0400");
    EXPECT_EQ(P(r, "tilemap.default_attribute"), "0x31");
    EXPECT_EQ(P(r, "tilemap.transparent_index"), "15");
    EXPECT_EQ(P(r, "tilemap.scroll_x"), "308");
    EXPECT_EQ(P(r, "tilemap.scroll_y"), "5");
}

TEST_F(NextReports_Test, VideoTransparencyAndSpritesAndPalettesSelected)
{
    Nr(0x14, 0xE3);
    Nr(0x4A, 0x12);
    Nr(0x4B, 0x34);
    Nr(0x43, 0x57);  // palette 5 (Layer 2 second) selected, ULANext on, ULA palette 2 chosen
    const StateNode r = DeviceState::NextVideo(_context);
    EXPECT_EQ(P(r, "transparency.global"), "0xE3");
    EXPECT_EQ(P(r, "transparency.fallback"), "0x12");
    EXPECT_EQ(P(r, "transparency.sprites"), "0x34");
    EXPECT_EQ(P(r, "ula.palette"), "2");
    EXPECT_EQ(P(r, "ula.ulanext"), "on");
}

TEST_F(NextReports_Test, VideoRasterFollowsTheFrameT)
{
    Z80* z80 = _context->pCore->GetZ80();
    const unsigned perLine = _context->pScreen->GetTstatesPerLine();
    ASSERT_GT(perLine, 0u);
    z80->t = 3 * perLine + 10;
    const StateNode r = DeviceState::NextVideo(_context);
    EXPECT_EQ(P(r, "raster.vc"), "3");
    EXPECT_EQ(P(r, "raster.hc"), "20");
    EXPECT_EQ(P(r, "raster.frame_t"), std::to_string(3 * perLine + 10));
    EXPECT_EQ(P(r, "raster.tstates_per_line"), std::to_string(perLine));
}

TEST_F(NextReports_Test, VideoTimingFamilyAndRefresh)
{
    const StateNode r = DeviceState::NextVideo(_context);
    EXPECT_FALSE(P(r, "timing.family").empty());
    Nr(0x05, 0x04);
    EXPECT_EQ(P(DeviceState::NextVideo(_context), "timing.hz"), "60");
}

TEST_F(NextReports_Test, VideoGolden)
{
    Out(0x123B, 0x02);
    Nr(0x12, 9);
    Nr(0x15, 0x05 << 2 | 0x01);
    Nr(0x6B, 0x80 | 0x20);
    Nr(0x6E, 0xA0);
    Out(0x00FF, 0x02);
    GoldenText::Expect("next", "video", DeviceState::ToText(DeviceState::NextVideo(_context)));
}

// ------------------------------------------------------------------------------------------------------------- next_palette

TEST_F(NextReports_Test, PaletteEntriesWrittenThroughNr40Nr41Nr44ReadBack)
{
    Nr(0x43, 0x00);  // ULA first palette, auto increment
    Nr(0x40, 0x10);
    Nr(0x41, 0xE0);  // RRRGGGBB: 111 000 00 -> 9 bit 111000000
    Nr(0x41, 0x03);  // 000 000 11 -> 000000111
    Nr(0x44, 0x4A);  // 9-bit write of entry 0x12: first byte
    Nr(0x44, 0x01);  //   second byte carries the low blue bit
    NextPaletteQuery q;
    q.palette = 0;
    q.first = 0x10;
    q.last = 0x12;
    const StateNode r = DeviceState::NextPalette(_context, q);
    EXPECT_EQ(P(r, "palettes.0.name"), "ula_1");
    EXPECT_EQ(P(r, "palettes.0.entries.0.index"), "16");
    EXPECT_EQ(P(r, "palettes.0.entries.0.rgb9"), "0x1C0");
    EXPECT_EQ(P(r, "palettes.0.entries.1.rgb9"), "0x007");
    EXPECT_EQ(P(r, "palettes.0.entries.2.rgb9"), "0x095") << "0x4A << 1 | 1";
    EXPECT_EQ(P(r, "palettes.0.entries.0.red"), "7");
    EXPECT_EQ(P(r, "palettes.0.entries.1.blue"), "7");
    EXPECT_EQ(P(r, "selected.index"), "19") << "the index auto-incremented three times";
    EXPECT_EQ(P(r, "palettes.0.entries.3.rgb9"), "") << "the range ends at 0x12";
}

TEST_F(NextReports_Test, PaletteSelectedFollowsNr43)
{
    const char* names[8] = {"ula_1", "layer2_1", "sprites_1", "tilemap_1", "ula_2", "layer2_2", "sprites_2", "tilemap_2"};
    for (unsigned p = 0; p < 8; p++)
    {
        Nr(0x43, static_cast<uint8_t>(p << 4));
        NextPaletteQuery q;  // default: the selected palette
        q.first = 0;
        q.last = 1;
        const StateNode r = DeviceState::NextPalette(_context, q);
        EXPECT_EQ(P(r, "selected.palette"), std::to_string(p));
        EXPECT_EQ(P(r, "selected.name"), names[p]);
        EXPECT_EQ(P(r, "palettes.0.name"), names[p]);
    }
    Nr(0x43, 0x80 | 0x20);
    EXPECT_EQ(P(DeviceState::NextPalette(_context, NextPaletteQuery{}), "selected.auto_increment"), "off");
}

TEST_F(NextReports_Test, PaletteAllReturnsEightPalettesAndLayer2Priority)
{
    Nr(0x43, 0x10);  // Layer 2 first
    Nr(0x40, 0x07);
    Nr(0x44, 0x20);
    Nr(0x44, 0x81);  // low blue bit and the priority bit
    NextPaletteQuery q;
    q.palette = NextPaletteQuery::kAll;
    q.first = 7;
    q.last = 7;
    const StateNode r = DeviceState::NextPalette(_context, q);
    ASSERT_EQ(r.find("palettes")->items.size(), 8u);
    EXPECT_EQ(P(r, "palettes.1.name"), "layer2_1");
    EXPECT_EQ(P(r, "palettes.1.entries.0.priority"), "on");
    EXPECT_EQ(P(r, "palettes.1.entries.0.rgb9"), "0x041");
    EXPECT_EQ(P(r, "palettes.0.entries.0.priority"), "off");
}

TEST_F(NextReports_Test, PaletteReportDoesNotAutoIncrementOrConsume)
{
    Nr(0x43, 0x00);
    Nr(0x40, 0x20);
    Nr(0x44, 0x55);  // half a 9-bit write is pending
    for (int i = 0; i < 3; i++)
        (void)DeviceState::NextPalette(_context, NextPaletteQuery{});
    Nr(0x44, 0x00);
    NextPaletteQuery q;
    q.palette = 0;
    q.first = 0x20;
    q.last = 0x20;
    const StateNode r = DeviceState::NextPalette(_context, q);
    EXPECT_EQ(P(r, "palettes.0.entries.0.rgb9"), "0x0AA") << "the pending half survived the reports";
    EXPECT_EQ(P(r, "selected.index"), "33");
}

TEST_F(NextReports_Test, PaletteTransparentIndexesAreReported)
{
    Nr(0x14, 0xE3);
    Nr(0x4B, 0x77);
    Nr(0x4C, 0x05);
    const StateNode r = DeviceState::NextPalette(_context, NextPaletteQuery{});
    EXPECT_EQ(P(r, "transparent.global"), "0xE3");
    EXPECT_EQ(P(r, "transparent.sprites"), "0x77");
    EXPECT_EQ(P(r, "transparent.tilemap"), "5");
}

TEST_F(NextReports_Test, PaletteQueryParsing)
{
    NextPaletteQuery q;
    std::string error;
    ASSERT_TRUE(NextPaletteQueryFromStrings("", "", q, error)) << error;
    EXPECT_EQ(q.palette, NextPaletteQuery::kSelected);
    EXPECT_EQ(q.first, 0u);
    EXPECT_EQ(q.last, 255u);
    ASSERT_TRUE(NextPaletteQueryFromStrings("all", "16-31", q, error)) << error;
    EXPECT_EQ(q.palette, NextPaletteQuery::kAll);
    EXPECT_EQ(q.first, 16u);
    EXPECT_EQ(q.last, 31u);
    ASSERT_TRUE(NextPaletteQueryFromStrings("sprites_2", "5", q, error)) << error;
    EXPECT_EQ(q.palette, 6);
    EXPECT_EQ(q.first, 5u);
    EXPECT_EQ(q.last, 5u);
    ASSERT_TRUE(NextPaletteQueryFromStrings("3", "", q, error)) << error;
    EXPECT_EQ(q.palette, 3);
    EXPECT_FALSE(NextPaletteQueryFromStrings("9", "", q, error));
    EXPECT_FALSE(NextPaletteQueryFromStrings("", "40-20", q, error));
    EXPECT_FALSE(NextPaletteQueryFromStrings("", "300", q, error));
    EXPECT_FALSE(NextPaletteQueryFromStrings("bogus", "", q, error));
}

TEST_F(NextReports_Test, PaletteGolden)
{
    Nr(0x43, 0x10);
    Nr(0x40, 0x00);
    Nr(0x41, 0xE0);
    Nr(0x41, 0x1C);
    Nr(0x41, 0x03);
    NextPaletteQuery q;
    q.first = 0;
    q.last = 3;
    GoldenText::Expect("next", "palette", DeviceState::ToText(DeviceState::NextPalette(_context, q)));
}

// ---------------------------------------------------------------------------------------------------------------- next_ports

namespace
{
NextPortsQuery Describe(uint16_t port, bool write)
{
    NextPortsQuery q;
    q.describe = true;
    q.port = port;
    q.write = write;
    return q;
}
}  // namespace

TEST_F(NextReports_Test, PortsDescribe6BWriteIsTheZxnDma)
{
    const StateNode r = DeviceState::NextPorts(_context, Describe(0x6B, true));
    EXPECT_EQ(P(r, "describe.device"), "DMA (zxnDMA)");
    EXPECT_EQ(P(r, "describe.access"), "write");
    EXPECT_EQ(P(r, "describe.enabled_by.nr"), "0x82");
    EXPECT_EQ(P(r, "describe.enabled_by.bit"), "5");
    EXPECT_EQ(P(r, "describe.enabled_by.word_bit"), "5");
    EXPECT_EQ(P(r, "describe.enabled"), "on");
    EXPECT_EQ(P(r, "describe.side_effect"), "the next byte of the DMA's WR0-WR6 sequence; the zxn mode latch is set");
}

TEST_F(NextReports_Test, PortsDescribe253BAndTheReadOfTheSamePort)
{
    StateNode r = DeviceState::NextPorts(_context, Describe(0x253B, true));
    EXPECT_EQ(P(r, "describe.device"), "NextREG data");
    EXPECT_EQ(P(r, "describe.enabled_by"), "") << "the NextREG pair has no enable bit";
    EXPECT_NE(P(r, "describe.side_effect").find("journal"), std::string::npos);
    r = DeviceState::NextPorts(_context, Describe(0x253B, false));
    EXPECT_EQ(P(r, "describe.access"), "read");
    EXPECT_EQ(P(r, "describe.device"), "NextREG data");
    r = DeviceState::NextPorts(_context, Describe(0x243B, false));
    EXPECT_EQ(P(r, "describe.device"), "NextREG select");
}

TEST_F(NextReports_Test, PortsDescribeDependsOnTheAccess)
{
    EXPECT_EQ(P(DeviceState::NextPorts(_context, Describe(0x001F, false)), "describe.device"), "Kempston joystick 1");
    const StateNode dac = DeviceState::NextPorts(_context, Describe(0x001F, true));
    EXPECT_EQ(P(dac, "describe.device"), "DAC");
    EXPECT_EQ(P(dac, "describe.channels"), "A (Soundrive 1)");
    EXPECT_EQ(P(DeviceState::NextPorts(_context, Describe(0x00FE, true)), "describe.device"), "ULA");
    EXPECT_EQ(P(DeviceState::NextPorts(_context, Describe(0xFFFD, true)), "describe.device"), "AY register select");
    EXPECT_EQ(P(DeviceState::NextPorts(_context, Describe(0x303B, false)), "describe.device"), "Sprites status / slot select");
    EXPECT_EQ(P(DeviceState::NextPorts(_context, Describe(0x7FFD, true)), "describe.device"), "Memory paging #7FFD");
    EXPECT_EQ(P(DeviceState::NextPorts(_context, Describe(0x00E3, true)), "describe.device"), "DivMMC control");
    EXPECT_EQ(P(DeviceState::NextPorts(_context, Describe(0x0013, true)), "describe.device"), "(not decoded by the Next)");
}

TEST_F(NextReports_Test, PortsDisabledByTheEnableWordReportsEnabledBy)
{
    Nr(0x82, 0xDF);  // clear bit 5: port #6B off
    StateNode r = DeviceState::NextPorts(_context, Describe(0x6B, true));
    EXPECT_EQ(P(r, "describe.enabled"), "off");
    EXPECT_EQ(P(r, "describe.enabled_by.nr"), "0x82");
    EXPECT_EQ(P(r, "describe.enabled_by.bit"), "5");
    EXPECT_EQ(P(r, "enable_word.nr_82"), "0xDF");
    EXPECT_EQ(P(r, "enable_word.value"), "0xFFFFFFDF");
    // port DF: bit 23 of the word (NR #85... no: NR #84 bit 7) - the DAC / Kempston alias
    Nr(0x84, 0x7F);
    r = DeviceState::NextPorts(_context, Describe(0x00DF, true));
    EXPECT_EQ(P(r, "describe.enabled_by.nr"), "0x84");
    EXPECT_EQ(P(r, "describe.enabled_by.bit"), "7");
    EXPECT_EQ(P(r, "describe.enabled"), "off");
    EXPECT_EQ(P(r, "describe.enforced"), "on") << "the emulator gates the DAC ports by NR #84";
}

TEST_F(NextReports_Test, PortsTheEnableWordListsEveryBitWithItsPorts)
{
    const StateNode r = DeviceState::NextPorts(_context, NextPortsQuery{});
    EXPECT_EQ(P(r, "enable_word.value"), "0xFFFFFFFF");
    ASSERT_NE(r.find("enable_word"), nullptr);
    const StateNode* bits = r.find("enable_word")->find("bits");
    ASSERT_NE(bits, nullptr);
    EXPECT_EQ(bits->items.size(), 28u);
    EXPECT_EQ(P(r, "enable_word.bits.0.ports"), "#FF");
    EXPECT_EQ(P(r, "enable_word.bits.9.ports"), "Multiface (two variable ports)");
    EXPECT_EQ(P(r, "enable_word.bits.5.enabled"), "on");
    EXPECT_EQ(P(r, "describe"), "") << "no port asked: no describe member";
}

TEST_F(NextReports_Test, PortsQueryParsing)
{
    NextPortsQuery q;
    std::string error;
    ASSERT_TRUE(NextPortsQueryFromStrings("", "", q, error)) << error;
    EXPECT_FALSE(q.describe);
    ASSERT_TRUE(NextPortsQueryFromStrings("0x253B", "w", q, error)) << error;
    EXPECT_TRUE(q.describe);
    EXPECT_EQ(q.port, 0x253B);
    EXPECT_TRUE(q.write);
    ASSERT_TRUE(NextPortsQueryFromStrings("6b", "read", q, error)) << error;
    EXPECT_EQ(q.port, 0x6B);
    EXPECT_FALSE(q.write);
    ASSERT_TRUE(NextPortsQueryFromStrings("#E3", "", q, error)) << error;
    EXPECT_EQ(q.port, 0xE3);
    EXPECT_FALSE(q.write) << "read is the default";
    EXPECT_FALSE(NextPortsQueryFromStrings("12345", "", q, error)) << "5 hex digits do not fit a port";
    EXPECT_FALSE(NextPortsQueryFromStrings("6B", "poke", q, error));
}

TEST_F(NextReports_Test, PortsGolden)
{
    GoldenText::Expect("next", "ports", DeviceState::ToText(DeviceState::NextPorts(_context, Describe(0x253B, true))));
}

// -------------------------------------------------------------------------------------------------------------- next_nextreg

TEST_F(NextReports_Test, NextRegReadOneRegister)
{
    Nr(0x07, 0x03);
    NextRegReadQuery q;
    q.reg = 0x07;
    const StateNode r = DeviceState::NextRegRead(_context, q);
    EXPECT_EQ(P(r, "reg"), "0x07");
    EXPECT_EQ(P(r, "name"), "CPU Speed");
    EXPECT_EQ(P(r, "value"), "0x33") << "programmed | actual, as a read returns it";
    EXPECT_EQ(P(r, "stored"), "0x03");
    EXPECT_EQ(P(r, "access"), "RW");
    EXPECT_EQ(P(r, "decoded"), "CPU speed 28 MHz");
}

TEST_F(NextReports_Test, NextRegReadHasNoSideEffects)
{
    // reading NR #18 (a clip window) or the palette registers through the report must not move an index
    Nr(0x40, 0x05);
    Nr(0x43, 0x00);
    const uint8_t selected = _ports->Board().SelectedRegister();
    NextRegReadQuery q;
    for (int reg : {0x18, 0x40, 0x41, 0x44, 0x1E, 0x1F, 0x60, 0x63})
    {
        q.reg = reg;
        (void)DeviceState::NextRegRead(_context, q);
    }
    EXPECT_EQ(_ports->Board().Read(0x40), 5);
    EXPECT_EQ(_ports->Board().SelectedRegister(), selected) << "the select latch is untouched";
    EXPECT_EQ(_ports->Board().Journal().Size(), 0u);
}

TEST_F(NextReports_Test, NextRegReadAllAndChanged)
{
    StateNode all = DeviceState::NextRegRead(_context, NextRegReadQuery{});
    ASSERT_NE(all.find("registers"), nullptr);
    EXPECT_GT(all.find("registers")->items.size(), 100u);
    Nr(0x14, 0x12);
    NextRegReadQuery q;
    q.changed = true;
    const StateNode changed = DeviceState::NextRegRead(_context, q);
    const StateNode* registers = changed.find("registers");
    ASSERT_NE(registers, nullptr);
    bool found = false;
    for (const StateNode& reg : registers->items)
        found = found || NextReportHelper::Path(reg, "nr") == "0x14";
    EXPECT_TRUE(found) << "NR #14 differs from its reset";
    EXPECT_LT(registers->items.size(), all.find("registers")->items.size());
}

TEST_F(NextReports_Test, NextRegReadUnknownRegisterIsStillAnswered)
{
    NextRegReadQuery q;
    q.reg = 0xFE;
    const StateNode r = DeviceState::NextRegRead(_context, q);
    EXPECT_EQ(P(r, "reg"), "0xFE");
    EXPECT_FALSE(P(r, "value").empty());
}

TEST_F(NextReports_Test, NextRegReadQueryParsing)
{
    NextRegReadQuery q;
    std::string error;
    ASSERT_TRUE(NextRegReadQueryFromStrings("", "", q, error)) << error;
    EXPECT_EQ(q.reg, -1);
    ASSERT_TRUE(NextRegReadQueryFromStrings("0x07", "true", q, error)) << error;
    EXPECT_EQ(q.reg, 7);
    EXPECT_TRUE(q.changed);
    ASSERT_TRUE(NextRegReadQueryFromStrings("ff", "0", q, error)) << error;
    EXPECT_EQ(q.reg, 255);
    EXPECT_FALSE(q.changed);
    EXPECT_FALSE(NextRegReadQueryFromStrings("100", "", q, error));
    EXPECT_FALSE(NextRegReadQueryFromStrings("zz", "", q, error));
}

TEST_F(NextReports_Test, NextRegGolden)
{
    NextRegReadQuery q;
    q.reg = 0x15;
    Nr(0x15, 0x05);
    GoldenText::Expect("next", "nextreg", DeviceState::ToText(DeviceState::NextRegRead(_context, q)));
}

// --------------------------------------------------------------------------------------------------------- the write control

TEST_F(NextReports_Test, NextRegWriteThroughEachDoorLandsInTheJournalWithItsSource)
{
    NextBoard& board = _ports->Board();
    board.Journal().SetEnabled(true);
    NextRegWriteControl::Result a = NextRegWriteControl::Write(_emulator, 0x14, 0x11, NextRegWriteControl::Door::NextReg, "test");
    ASSERT_TRUE(a.ok) << a.error;
    EXPECT_EQ(a.previous, 0xE3);
    EXPECT_EQ(a.value, 0x11);
    NextRegWriteControl::Result b = NextRegWriteControl::Write(_emulator, 0x14, 0x22, NextRegWriteControl::Door::Port, "test");
    ASSERT_TRUE(b.ok) << b.error;
    EXPECT_EQ(b.previous, 0x11);
    NextRegWriteControl::Result c = NextRegWriteControl::Write(_emulator, 0x14, 0x33, NextRegWriteControl::Door::Internal, "test");
    ASSERT_TRUE(c.ok) << c.error;
    const auto events = board.Journal().Query(NextRegJournalQuery{});
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].source, NextRegSource::NextReg);
    EXPECT_EQ(events[1].source, NextRegSource::Port);
    EXPECT_EQ(events[2].source, NextRegSource::Internal);
    EXPECT_EQ(events[2].value, 0x33);
    EXPECT_EQ(P(DeviceState::NextRegRead(_context, [] { NextRegReadQuery q; q.reg = 0x14; return q; }()), "value"), "0x33")
        << "the value is visible in the register report";
}

TEST_F(NextReports_Test, NextRegWritePortDoorMovesTheSelectLatchAndTheNextregDoorDoesNot)
{
    NextBoard& board = _ports->Board();
    board.SelectRegister(0x55);
    ASSERT_TRUE(NextRegWriteControl::Write(_emulator, 0x14, 1, NextRegWriteControl::Door::NextReg, "test").ok);
    EXPECT_EQ(board.SelectedRegister(), 0x55) << "NEXTREG instruction semantics: the latch is untouched";
    ASSERT_TRUE(NextRegWriteControl::Write(_emulator, 0x14, 2, NextRegWriteControl::Door::Port, "test").ok);
    EXPECT_EQ(board.SelectedRegister(), 0x14) << "the port pair selects first";
}

TEST_F(NextReports_Test, NextRegWriteDoorAndStringParsing)
{
    NextRegWriteControl::Door door;
    EXPECT_TRUE(NextRegWriteControl::ParseDoor("", door));
    EXPECT_EQ(door, NextRegWriteControl::Door::NextReg) << "default: the NEXTREG instruction";
    EXPECT_TRUE(NextRegWriteControl::ParseDoor("port", door));
    EXPECT_EQ(door, NextRegWriteControl::Door::Port);
    EXPECT_TRUE(NextRegWriteControl::ParseDoor("internal", door));
    EXPECT_FALSE(NextRegWriteControl::ParseDoor("copper", door));
    uint8_t reg = 0, value = 0;
    std::string error;
    EXPECT_TRUE(NextRegWriteControl::Parse("0x07", "#03", reg, value, error)) << error;
    EXPECT_EQ(reg, 7);
    EXPECT_EQ(value, 3);
    EXPECT_FALSE(NextRegWriteControl::Parse("100", "1", reg, value, error));
    EXPECT_FALSE(NextRegWriteControl::Parse("1", "1ff", reg, value, error));
    EXPECT_FALSE(NextRegWriteControl::Parse("", "1", reg, value, error));
}

TEST_F(NextReports_Test, NextRegWriteOnAnotherMachineIsNotANext)
{
    Emulator* other = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(other, nullptr);
    const NextRegWriteControl::Result r = NextRegWriteControl::Write(other, 0x14, 1, NextRegWriteControl::Door::NextReg, "test");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.notNext);
    EXPECT_NE(r.error.find("Next"), std::string::npos);
    EmulatorTestHelper::CleanupEmulator(other);
}

TEST_F(NextReports_Test, ReportsOnAnotherMachineAreUnavailable)
{
    Emulator* other = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(other, nullptr);
    EmulatorContext* context = other->GetContext();
    for (const StateNode& r : {DeviceState::NextDma(context), DeviceState::NextVideo(context), DeviceState::NextPalette(context, NextPaletteQuery{}),
                               DeviceState::NextPorts(context, NextPortsQuery{}), DeviceState::NextRegRead(context, NextRegReadQuery{})})
        EXPECT_EQ(P(r, "available"), "off");
    EmulatorTestHelper::CleanupEmulator(other);
}
