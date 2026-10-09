// ZX Spectrum Next skeleton machine (N2): the model is creatable, the MMU slot table follows the paging ports and
// NEXTREG #50-#57, the Z80N engine runs on the machine's bus, and the bare personality boots 48K BASIC from a
// user-supplied ROM set. Design: docs/inprogress/2026-10-07-zx-next/design-core.md (D6), phases.md N2.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/z80nengine.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"

class NextSkeleton_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    NextMemory* _memory = nullptr;
    PortDecoder_Next* _ports = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr) << "the NEXT model is not creatable";
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = dynamic_cast<NextMemory*>(_context->pMemory);
        ASSERT_NE(_memory, nullptr);
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    void Out(uint16_t port, uint8_t value) { _ports->DecodePortOut(port, value, 0); }
    uint8_t In(uint16_t port) { return _ports->DecodePortIn(port, 0); }

    void ExpectSlots(std::initializer_list<uint8_t> expected, const char* what)
    {
        unsigned s = 0;
        for (uint8_t v : expected)
        {
            EXPECT_EQ(_memory->GetMmu(s), v) << what << ": slot " << s;
            s++;
        }
    }

    void Load(uint16_t addr, std::initializer_list<uint8_t> code)
    {
        for (uint8_t b : code)
            _memory->PokeSlot(addr++, b);
    }
};

TEST_F(NextSkeleton_Test, ModelIsCreatableAndOwnsItsParts)
{
    EXPECT_TRUE(PortDecoder::IsModelSupported(MM_NEXT));
    EXPECT_EQ(_context->config.mem_model, MM_NEXT);
    EXPECT_EQ(_context->config.ramsize, 2048u);
    ASSERT_NE(_ports->Engine(), nullptr);
    EXPECT_TRUE(_ports->Engine()->IsInstalled());
    EXPECT_EQ(_z80->MemIf, _memory->ModelMemoryInterface(_z80->isDebugMode)) << "the CPU runs on the 8-slot interface";
}

TEST_F(NextSkeleton_Test, ResetSlotTable)
{
    ExpectSlots({255, 255, 10, 11, 4, 5, 0, 1}, "after reset");
}

TEST_F(NextSkeleton_Test, PagingPortsRewriteTheSlots)
{
    _ports->Board().SetMachineType(3);  // +3: every paging port exists
    Out(0x7FFD, 0x03);  // RAM bank 3, ROM 0
    ExpectSlots({255, 255, 10, 11, 4, 5, 6, 7}, "7FFD = 3");
    Out(0xDFFD, 0x01);  // bank bits above: 8 + 3 = 11
    ExpectSlots({255, 255, 10, 11, 4, 5, 22, 23}, "DFFD = 1");
    Out(0xDFFD, 0x00);
    Out(0x1FFD, 0x01);  // +3 special paging, config 0: banks 0 1 2 3
    ExpectSlots({0, 1, 2, 3, 4, 5, 6, 7}, "1FFD = 1");
    Out(0x1FFD, 0x07);  // config 3: 4 7 6 3
    ExpectSlots({8, 9, 14, 15, 12, 13, 6, 7}, "1FFD = 7");
}

TEST_F(NextSkeleton_Test, RomSelectIsFromBit2AndBit4)
{
    _ports->Board().SetMachineType(3);
    const uint8_t* rom[4];
    for (int r = 0; r < 4; r++)
        rom[r] = _memory->ROMPageHostAddress(static_cast<uint8_t>(r));
    // The four ROMs differ (128 editor / 128 syntax / 48 / 48): find a byte where ROM 0 and 1 differ
    unsigned diff = 0;
    while (diff < 0x4000 && rom[0][diff] == rom[1][diff])
        diff++;
    ASSERT_LT(diff, 0x4000u);

    Out(0x7FFD, 0x00);
    EXPECT_EQ(_memory->PeekSlot(static_cast<uint16_t>(diff)), rom[0][diff]);
    Out(0x7FFD, 0x10);
    EXPECT_EQ(_memory->PeekSlot(static_cast<uint16_t>(diff)), rom[1][diff]);
    EXPECT_EQ(_memory->GetRomSelect(), 1);
    Out(0x1FFD, 0x04);
    EXPECT_EQ(_memory->GetRomSelect(), 3);
    Out(0x7FFD, 0x00);
    EXPECT_EQ(_memory->GetRomSelect(), 2);
}

TEST_F(NextSkeleton_Test, NextRegPairAnswersAndMmuRegistersAreTheTable)
{
    Out(0x243B, 0x00);
    EXPECT_EQ(In(0x253B), 10) << "machine id";
    Out(0x243B, 0x50);
    EXPECT_EQ(In(0x253B), 255) << "MMU 0 after reset";
    Out(0x253B, 0x20);
    EXPECT_EQ(_memory->GetMmu(0), 0x20);
    Out(0x243B, 0x57);
    Out(0x253B, 0x31);
    EXPECT_EQ(_memory->GetMmu(7), 0x31);
    // a paging port write puts the classic layout back
    Out(0x7FFD, 0x00);
    ExpectSlots({255, 255, 10, 11, 4, 5, 0, 1}, "7FFD after MMU writes");
}

TEST_F(NextSkeleton_Test, CpuWritesLandInTheSlotsAndRomIsReadOnly)
{
    Load(0x8000, {0x3E, 0xA5,         // LD A,#A5
                  0x32, 0x00, 0xC0,   // LD (#C000),A      RAM bank 0, slot 6
                  0x32, 0x00, 0x00,   // LD (#0000),A      ROM: ignored
                  0x32, 0x00, 0xE0}); // LD (#E000),A      slot 7
    const uint8_t rom0 = _memory->PeekSlot(0x0000);
    _z80->pc = 0x8000;
    for (int i = 0; i < 4; i++)
        _z80->EngineStep();
    EXPECT_EQ(_memory->PeekSlot(0xC000), 0xA5);
    EXPECT_EQ(_memory->PeekSlot(0xE000), 0xA5);
    EXPECT_EQ(_memory->PeekSlot(0x0000), rom0);
    // the 8K pages are different bytes of RAM bank 0
    EXPECT_NE(_memory->SlotReadOffset(6), _memory->SlotReadOffset(7));
    EXPECT_EQ(_memory->SlotReadOffset(7), _memory->SlotReadOffset(6) + NextMemory::kSlotSize);
}

TEST_F(NextSkeleton_Test, NextRegInstructionMapsASlot)
{
    Load(0x8000, {0xED, 0x91, 0x56, 0x44,   // NEXTREG #56,#44   slot 6 -> 8K page 68
                  0x3E, 0x5A,               // LD A,#5A
                  0x32, 0x00, 0xC0});       // LD (#C000),A
    _z80->pc = 0x8000;
    for (int i = 0; i < 3; i++)
        _z80->EngineStep();
    EXPECT_EQ(_memory->GetMmu(6), 0x44);
    // page 68 is bank 34, low half
    EXPECT_EQ(_memory->PeekSlot(0xC000), 0x5A);
    EXPECT_EQ(_memory->RAMPageAddress(34)[0], 0x5A);
}

TEST_F(NextSkeleton_Test, Z80NInstructionRunsOnTheMachine)
{
    Load(0x8000, {0x11, 0x05, 0x03,   // LD DE,#0305
                  0xED, 0x30});       // MUL D,E
    _z80->pc = 0x8000;
    for (int i = 0; i < 2; i++)
        _z80->EngineStep();
    EXPECT_EQ(_z80->de, 15);
}

// Boot-bound (a ROM cold start): the bare personality's ROM 1 is the 48K ROM
TEST_F(NextSkeleton_Test, Boots48KBasicFromTheBareRom)
{
    _emulator->EnableTurboMode();
    Out(0x7FFD, 0x10);
    _z80->pc = 0;
    int frames = 0;
    EXPECT_TRUE(EmulatorTestHelper::RunUntilBASICReady(_emulator, 300, &frames)) << frames << " frames";
}
