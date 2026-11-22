// ZX Spectrum Next memory: banks 5 and 7 exist twice (zxnext.vhd 2961-2962, 3039-3041, 3061-3063, 3077, 3100-3104, 1665, 6562-6702).
// The CPU (and the DMA) through the MMU, the ULA, LoRes and the tilemap use the FPGA block RAM of pages #0A, #0B, #0E; Layer 2 (its display
// fetch and the #123B mapping), the NR #04 config mapping and the zxnDDMA physical addresses use the SRAM at the same page numbers.
// Nothing writes both. Page #0F (the second 8K of bank 7) is SRAM only. NextMemory::SramTwin holds the SRAM side of the three pages.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/video/next/nextvideorenderer.h"

class NextMemory_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    NextMemory* _memory = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        EmulatorContext* context = _emulator->GetContext();
        _memory = dynamic_cast<NextMemory*>(context->pMemory);
        _ports = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
        ASSERT_NE(_memory, nullptr);
        ASSERT_NE(_ports, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Out(uint16_t port, uint8_t value) { _ports->DecodePortOut(port, value, 0); }
    void NextReg(uint8_t reg, uint8_t value)
    {
        Out(0x243B, reg);
        Out(0x253B, value);
    }
    uint8_t* Twin(unsigned page8k) { return _memory->SramTwin() + NextMemory::SramTwinOffset(page8k); }
};

// The analysis' test (research-reference-compares.md, BUG A): NR #12 = 5, a write through the #123B mapping lands in the SRAM side; the
// ULA's bank 5 keeps its byte, and Layer 2's display shows the written one
TEST_F(NextMemory_Test, Layer2MappingOfBank5WritesTheSramNotTheUlaScreen)
{
    _memory->SlotWriteFast(0x4000, 0x11);  // MMU page #0A: the block RAM
    NextReg(0x12, 5);
    Out(0x123B, 0x01);                     // write mapping, segment 0: bank 5 at #0000-#3FFF
    _memory->SlotWriteFast(0x0000, 0xAA);
    _memory->SlotWriteFast(0x2001, 0xBB);  // the second 8K of the bank (page #0B)
    Out(0x123B, 0x00);

    EXPECT_EQ(_memory->PeekSlot(0x4000), 0x11) << "the CPU's bank 5 (MMU #0A) did not change";
    EXPECT_EQ(_memory->RAMPageAddress(5)[0x0000], 0x11) << "the ULA screen did not change";
    EXPECT_EQ(Twin(0x0A)[0x0000], 0xAA) << "the SRAM side of page #0A has the byte";
    EXPECT_EQ(Twin(0x0B)[0x0001], 0xBB) << "and of page #0B";
    EXPECT_NE(_memory->RAMPageAddress(5)[0x2001], 0xBB);

    // Layer 2's display fetch is the SRAM too (o_RAM_B_ADDR): pixel (0, 0) is index #AA
    NextBoard& board = _ports->Board();
    NextVideoInputs in;
    in.ram = _memory->RAMPageAddress(0);
    in.ramPages = static_cast<uint16_t>(_memory->InstalledRamPages());
    in.sramTwin = _memory->SramTwin();
    in.regs = &board.Video();
    in.layer2Enable = true;
    in.nr[0x12] = 5;
    in.nr[0x14] = 0xE3;
    std::vector<uint32_t> line(NextVideoRenderer::kWidth);
    NextVideoRenderer::RenderLine(in, NextVideoRenderer::kPaperTop, line.data());
    const uint32_t expected = NextVideoRenderer::Rgba(board.Video().PaletteEntry(1, 0xAA) & 0x1FF);
    EXPECT_EQ(line[NextVideoRenderer::kPaperLeft], expected) << "Layer 2 shows the SRAM side, not the ULA's #11";
    EXPECT_NE(NextVideoRenderer::Rgba(board.Video().PaletteEntry(1, 0x11) & 0x1FF), expected) << "the two bytes look different";
}

// The other direction: a CPU write through the MMU stays in the block RAM; the #123B read mapping does not see it
TEST_F(NextMemory_Test, CpuWritesToPagesAAndBAndEStayInTheBlockRam)
{
    NextReg(0x50 + 2, 0x0A);
    NextReg(0x50 + 3, 0x0B);
    NextReg(0x50 + 6, 0x0E);
    NextReg(0x50 + 7, 0x0F);
    _memory->SlotWriteFast(0x4010, 0x21);
    _memory->SlotWriteFast(0x6010, 0x22);
    _memory->SlotWriteFast(0xC010, 0x23);
    _memory->SlotWriteFast(0xE010, 0x24);
    EXPECT_EQ(Twin(0x0A)[0x10], 0x00);
    EXPECT_EQ(Twin(0x0B)[0x10], 0x00);
    EXPECT_EQ(Twin(0x0E)[0x10], 0x00) << "no write-through: sram_pre_active is 0 for the block RAM pages";

    NextReg(0x12, 5);
    Out(0x123B, 0x04);  // read mapping, segment 0
    EXPECT_EQ(_memory->PeekSlot(0x0010), 0x00) << "Layer 2 reads bank 5's SRAM side";
    EXPECT_EQ(_memory->PeekSlot(0x2010), 0x00);
    NextReg(0x12, 7);
    EXPECT_EQ(_memory->PeekSlot(0x0010), 0x00) << "and page #0E's";
    EXPECT_EQ(_memory->PeekSlot(0x2010), 0x24) << "page #0F has one instance, the SRAM: the CPU's write is there";
    Out(0x123B, 0x00);
    EXPECT_EQ(_memory->PeekSlot(0x4010), 0x21) << "the CPU still reads its own bytes";
    EXPECT_EQ(_memory->PeekSlot(0xC010), 0x23);
}

// The NR #04 config mapping addresses the SRAM (nr_04_romram_bank & A13, zxnext.vhd 3044-3050): bank 16 + 5 is the SRAM side
TEST_F(NextMemory_Test, ConfigMappingOfBank5ReachesTheSramSide)
{
    const uint8_t ula5 = _memory->RAMPageAddress(5)[0x20], ula7 = _memory->RAMPageAddress(7)[0x30];
    _memory->SetConfigMode(true);
    NextReg(0x04, 0x10 + 5);
    _memory->SlotWriteFast(0x0020, 0x5C);
    _memory->SlotWriteFast(0x2020, 0x5D);
    EXPECT_EQ(Twin(0x0A)[0x20], 0x5C);
    EXPECT_EQ(Twin(0x0B)[0x20], 0x5D);
    EXPECT_EQ(_memory->RAMPageAddress(5)[0x20], ula5) << "the ULA's bank 5 is not reached";
    NextReg(0x04, 0x10 + 7);
    _memory->SlotWriteFast(0x0030, 0x7C);
    _memory->SlotWriteFast(0x2030, 0x7D);
    EXPECT_EQ(Twin(0x0E)[0x30], 0x7C);
    EXPECT_EQ(_memory->RAMPageAddress(7)[0x30], ula7);
    EXPECT_EQ(_memory->RAMPageAddress(7)[0x2030], 0x7D) << "page #0F: one instance";
    NextReg(0x04, 0x10 + 2);
    _memory->SlotWriteFast(0x0040, 0x2C);
    EXPECT_EQ(_memory->RAMPageAddress(2)[0x40], 0x2C) << "a bank with one instance";
    _memory->SetConfigMode(false);
}

// The zxnDDMA physical addresses are the SRAM: #054000 is bank 5's SRAM side, #05E000 the second 8K of bank 7 (one instance)
TEST_F(NextMemory_Test, PhysicalAddressesOfBanks5And7AreTheSramSide)
{
    const uint8_t ula5 = _memory->RAMPageAddress(5)[0x50];
    _memory->PhysicalWrite(0x054000 + 0x50, 0x61);
    _memory->PhysicalWrite(0x05C000 + 0x50, 0x62);
    _memory->PhysicalWrite(0x05E000 + 0x50, 0x63);
    EXPECT_EQ(Twin(0x0A)[0x50], 0x61);
    EXPECT_EQ(Twin(0x0E)[0x50], 0x62);
    EXPECT_EQ(_memory->RAMPageAddress(7)[0x2050], 0x63);
    EXPECT_EQ(_memory->RAMPageAddress(5)[0x50], ula5) << "not the ULA screen";
    EXPECT_EQ(_memory->PhysicalRead(0x054000 + 0x50), 0x61);
    EXPECT_EQ(_memory->PhysicalRead(0x05C000 + 0x50), 0x62);
    EXPECT_EQ(_memory->PhysicalRead(0x05E000 + 0x50), 0x63);
}
