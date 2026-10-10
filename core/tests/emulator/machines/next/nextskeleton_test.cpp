// ZX Spectrum Next skeleton machine (N2): the model is creatable, the MMU slot table follows the paging ports and
// NEXTREG #50-#57, the Z80N engine runs on the machine's bus, and the bare personality boots 48K BASIC from a
// user-supplied ROM set. Design: docs/inprogress/2026-10-07-zx-next/design-core.md (D6), phases.md N2.

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <algorithm>

#include <cstring>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/z80nengine.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/io/z80n/nextregtable.h"
#include "emulator/state/devicestate.h"
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
    EXPECT_EQ(_z80->MemIf, _memory->ModelMemoryInterface(_z80->isDebugMode, _context->pCore->IsContentionEffective())) << "the CPU runs on the 8-slot interface";
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

/// A continuous DMA fill of `length` bytes at `destination`: port A is the fixed byte at #8000, port B counts up (zxnext.vhd: the DMA drives
/// the same address bus as the CPU, so it sees the same slot table, ROM protection and Layer 2 write mapping)
static void DmaFill(PortDecoder_Next* ports, uint16_t destination, uint16_t length)
{
    for (uint8_t b : {uint8_t(0x83), uint8_t(0x7D), uint8_t(0x00), uint8_t(0x80), uint8_t(length & 0xFF), uint8_t(length >> 8), uint8_t(0x24), uint8_t(0x10), uint8_t(0xAD), uint8_t(destination & 0xFF),
                      uint8_t(destination >> 8), uint8_t(0xCF), uint8_t(0x87)})
        ports->DecodePortOut(0x6B, b, 0);
}

// The Next demos that draw by DMA into a mapped screen: the DMA's write goes where a CPU write would
TEST_F(NextSkeleton_Test, DmaWritesFollowTheSlotTableNotThe16KWindows)
{
    // two 8K slots of one 16K window mapped to pages that are not neighbours
    _memory->SetMmu(6, 0x30);  // bank 24, low half
    _memory->SetMmu(7, 0x51);  // bank 40, high half
    _memory->PokeSlot(0x8000, 0xAA);
    DmaFill(_ports, 0xE000, 16);
    _z80->EngineStep();
    EXPECT_EQ(_memory->PeekSlot(0xE000), 0xAA) << "the byte is at the slot's own page";
    EXPECT_EQ(_memory->RAMPageAddress(40)[0x2000], 0xAA);
    EXPECT_EQ(_memory->RAMPageAddress(24)[0x2000], 0x00) << "not the second half of the page the even slot shows";
}

TEST_F(NextSkeleton_Test, DmaWritesToRomAreIgnoredAndLayer2WriteMappingTakesThem)
{
    _memory->PokeSlot(0x8000, 0x55);
    const uint8_t rom0 = _memory->PeekSlot(0x0000);
    ASSERT_NE(rom0, 0x55);
    DmaFill(_ports, 0x0000, 16);
    _z80->EngineStep();
    EXPECT_EQ(_memory->PeekSlot(0x0000), rom0) << "ROM is read-only for the DMA too";
    // port #123B bit 0: the write mapping of Layer 2 (bank NR #12 = 9) over #0000-#3FFF; reads still show the ROM
    _ports->Board().Write(0x12, 9);
    Out(0x123B, 0x01);
    DmaFill(_ports, 0x0000, 16);
    _z80->EngineStep();
    EXPECT_EQ(_memory->RAMPageAddress(9)[0], 0x55);
    EXPECT_EQ(_memory->RAMPageAddress(9)[15], 0x55);
    EXPECT_EQ(_memory->PeekSlot(0x0000), rom0);
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

// N3b: the video logic's wait on the banks of the frame family, at 3.5 MHz only (research-fpga-vhdl.md section 2)
namespace
{
// T-states the read of `addr` takes when it starts at frame T-state `t0`
uint32_t ReadTime(Z80* z80, uint16_t addr, uint32_t t0)
{
    z80->t = t0;
    z80->rd(addr);
    return z80->t - t0;
}

// The start T-states in the paper window where the read of `addr` waits
unsigned WaitingStarts(Z80* z80, uint16_t addr)
{
    unsigned n = 0;
    for (uint32_t t0 = 16300; t0 < 17300; t0++)  // paper lines: the 128K raster starts them at 16188
        n += ReadTime(z80, addr, t0) > ReadTime(z80, 0x0000, t0) ? 1 : 0;
    return n;
}
}  // namespace

TEST_F(NextSkeleton_Test, ContentionHitsTheOddBanksOfThe128KFamily)
{
    EXPECT_GT(WaitingStarts(_z80, 0x4000), 100u) << "bank 5";
    EXPECT_EQ(WaitingStarts(_z80, 0x8000), 0u) << "bank 2";
    EXPECT_EQ(WaitingStarts(_z80, 0xC000), 0u) << "bank 0";
    Out(0x7FFD, 0x01);
    EXPECT_GT(WaitingStarts(_z80, 0xC000), 100u) << "bank 1 is odd";
}

TEST_F(NextSkeleton_Test, Only48KBank5AndNothingAbove3_5MHz)
{
    Out(0x7FFD, 0x01);
    Out(0x243B, 0x03);
    Out(0x253B, 0x90);  // timing family 1 (48K)
    _emulator->RunFrame(true);
    ASSERT_EQ(_context->emulatorState.ula_timing_class, 1);
    EXPECT_GT(WaitingStarts(_z80, 0x4000), 100u) << "bank 5";
    EXPECT_EQ(WaitingStarts(_z80, 0xC000), 0u) << "48K timing: bank 1 is not contended";

    Out(0x243B, 0x07);
    Out(0x253B, 0x01);  // 7 MHz
    EXPECT_EQ(WaitingStarts(_z80, 0x4000), 0u) << "no contention above 3.5 MHz";
    Out(0x253B, 0x00);
    EXPECT_GT(WaitingStarts(_z80, 0x4000), 100u);
    Out(0x243B, 0x08);
    Out(0x253B, 0x40);  // NR #08 bit 6: contention disabled
    EXPECT_EQ(WaitingStarts(_z80, 0x4000), 0u);
}

// t80n_mcode.vhd X"91": NEXTREG n,v is 6 memory cycles (2 M1, 2 operand, 2 trailing reads with NoRead = 0), all stretched by the +3 gate
// array in bank 5: a stream of them in a paper row takes up to 48 T per instruction (20 T + 28 T of waits), against 20 T in an uncontended bank.
// This is the Changing8kBank board test's loop (the same stream measured on the emulator: ON - OFF = 14146 T of 42671 T).
TEST_F(NextSkeleton_Test, NextRegStreamInContendedBank5TakesTheSixCycleWaits)
{
    Out(0x243B, 0x03);
    Out(0x253B, 0xB0);  // timing +3
    _emulator->RunFrame(true);
    ASSERT_EQ(_context->emulatorState.ula_timing_class, 3);
    auto stream = [&](uint16_t base) {
        for (unsigned i = 0; i < 40; i++)
        {
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(base + i * 4), 0xED);
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(base + i * 4 + 1), 0x91);
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(base + i * 4 + 2), 0x7F);  // a register nothing reacts to
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(base + i * 4 + 3), 0x00);
        }
        _z80->pc = base;
        _z80->t = 17000;  // inside the paper rows (+3 raster)
        for (unsigned i = 0; i < 10; i++)  // settle into the cadence
            _z80->EngineStep();
        uint32_t longest = 0;
        for (unsigned i = 0; i < 20; i++)  // instructions inside the 128 T window of a row
        {
            const uint32_t t0 = _z80->t;
            _z80->EngineStep();
            longest = std::max(longest, _z80->t - t0);
        }
        return static_cast<double>(longest);
    };
    EXPECT_NEAR(stream(0x6000), 48.0, 2.5) << "bank 5: six contended cycles (four would give 40)";
    EXPECT_DOUBLE_EQ(stream(0xC000), 20.0) << "bank 0: no waits";
}

// zxnext.vhd 3171-3181: at 28 MHz each CPU read cycle that reaches the SRAM waits one clock (a NOP: 5 clocks, not 4); the bank 7 BRAM page
// (#0E) has its own read port and does not; at 14 MHz nobody waits
TEST_F(NextSkeleton_Test, At28MHzSramReadsWaitOneClockAndBank7DoesNot)
{
    auto nops = [&](uint8_t mmu6, uint8_t speed) {
        Out(0x243B, 0x07);
        Out(0x253B, speed);
        Out(0x243B, 0x56);
        Out(0x253B, mmu6);
        _emulator->RunFrame(true);  // the speed applies at a frame start
        for (unsigned i = 0; i < 200; i++)
            _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0xC000 + i), 0x00);
        _z80->pc = 0xC000;
        const uint32_t t0 = _z80->t;
        for (unsigned i = 0; i < 100; i++)
            _z80->EngineStep();
        return static_cast<double>(_z80->t - t0);
    };
    const double sram = nops(0x00, 3);
    const double bram = nops(0x0E, 3);
    EXPECT_NEAR(sram / bram, 5.0 / 4.0, 0.01) << "sram " << sram << " bram " << bram;
    const double mid = nops(0x00, 2);
    EXPECT_NEAR(mid / bram, 1.0, 0.01) << "14 MHz: 4 clocks per NOP, no waits";
}

TEST_F(NextSkeleton_Test, PentagonTimingHasNoContention)
{
    Out(0x243B, 0x03);
    Out(0x253B, 0xC0);  // timing family 4
    _emulator->RunFrame(true);
    EXPECT_EQ(WaitingStarts(_z80, 0x4000), 0u);
}

// NR #8E carries the paging ports' state in one register (registers.txt); a write acts as if by the port writes
TEST_F(NextSkeleton_Test, Nr8EIsThePagingPortsInOneRegister)
{
    auto nextreg = [&](uint8_t reg, uint8_t v) {
        Out(0x243B, reg);
        Out(0x253B, v);
    };
    _ports->Board().SetMachineType(3);  // +3 type: the ROM number has two bits
    Out(0x7FFD, 0x00);  // (the test helper starts the 48K BASIC mode: ROM 1)
    Out(0x243B, 0x8E);
    EXPECT_EQ(In(0x253B), 0x08) << "reset: bank 0, normal paging, ROM 0, bit 3 reads 1";
    nextreg(0x8E, 0x01);  // ROM select: #7FFD bit 4
    EXPECT_EQ(_memory->GetRomSelect(), 1);
    nextreg(0x8E, 0x02);  // #1FFD bit 2
    EXPECT_EQ(_memory->GetRomSelect(), 2);
    nextreg(0x8E, 0x03);
    EXPECT_EQ(_memory->GetRomSelect(), 3);
    EXPECT_EQ(_memory->GetMmu(6), 0) << "bit 3 clear: the RAM bank is not touched";
    nextreg(0x8E, 0x08 | 0x30);  // change the RAM bank: #7FFD bits 2:0 = 3
    EXPECT_EQ(_memory->GetMmu(6), 6);
    EXPECT_EQ(_memory->GetMmu(7), 7);
    nextreg(0x8E, 0x08 | 0x80 | 0x30);  // #DFFD bit 0: bank 8 + 3
    EXPECT_EQ(_memory->GetMmu(6), 22);
    Out(0x243B, 0x8E);
    EXPECT_EQ(In(0x253B) & 0xF8, 0x80 | 0x30 | 0x08);
    // special paging (bit 2): all RAM, configuration in bits 1:0 -> #1FFD bits 2 and 1
    nextreg(0x8E, 0x04 | 0x01);
    EXPECT_EQ(_memory->GetMmu(0), 8) << "banks 4 5 6 3: slot 0 is bank 4";
}

// The Next's DivMMC (esxdos-and-sd.md section 3): #E3 shows the ROM and a RAM bank at #0000-#3FFF, MAPRAM is sticky,
// the automap follows NR #B8-#BB and NR #0A bit 4
TEST_F(NextSkeleton_Test, DivMmcMapsRomAndBankAndTheAutomapFollowsTheRegisters)
{
    auto nextreg = [&](uint8_t reg, uint8_t v) {
        Out(0x243B, reg);
        Out(0x253B, v);
    };
    Out(0x7FFD, 0x00);
    // the system area holds the DivMMC ROM (page 4) and RAM (pages 8-15): the firmware's job, here a pattern
    uint8_t* rom = _memory->ROMPageHostAddress(4);
    std::memset(rom, 0xD1, 0x2000);
    rom[0x100] = 0x42;
    std::memset(_memory->ROMPageHostAddress(8), 0x00, 0x4000);  // bank 0 and 1
    const uint8_t spectrumRom = _memory->PeekSlot(0x0100);

    Out(0xE3, 0x80 | 0x01);  // CONMEM, bank 1
    EXPECT_EQ(_memory->PeekSlot(0x0100), 0x42) << "the DivMMC ROM";
    _memory->SlotWriteFast(0x2000, 0x77);
    EXPECT_EQ(_memory->ROMPageHostAddress(8)[0x2000], 0x77) << "bank 1 is the second half of system page 8";
    _memory->SlotWriteFast(0x0100, 0x00);
    EXPECT_EQ(_memory->PeekSlot(0x0100), 0x42) << "the ROM is read-only";
    Out(0xE3, 0x00);
    EXPECT_EQ(_memory->PeekSlot(0x0100), spectrumRom);

    // automap: off until NR #0A bit 4
    uint8_t code[] = {0xCF};  // RST 8
    _memory->PokeSlot(0x8000, code[0]);
    _z80->pc = 0x8000;
    _z80->sp = 0xBF00;
    _z80->EngineStep();
    _z80->EngineStep();
    EXPECT_FALSE(_ports->DivMmc().Automapped()) << "NR #0A bit 4 is clear";
    nextreg(0x0A, 0x10);
    // #0008 is valid only with the 48K ROM paged: ROM 1 of the 128K family
    _z80->pc = 0x8000;
    _z80->sp = 0xBF00;
    _z80->EngineStep();  // RST 8 -> pc #0008
    _z80->EngineStep();  // the opcode at #0008
    EXPECT_FALSE(_ports->DivMmc().Automapped()) << "ROM 0 is paged: the #0008 entry is ROM-3-only";
    nextreg(0xB9, 0x03);  // #0000 and #0008 always valid
    _z80->pc = 0x8000;
    _z80->sp = 0xBF00;
    _z80->EngineStep();
    _z80->EngineStep();
    EXPECT_TRUE(_ports->DivMmc().Automapped());
    EXPECT_EQ(_memory->PeekSlot(0x0100), 0x42);
    _z80->pc = 0x1FF8;
    _z80->EngineStep();
    EXPECT_FALSE(_ports->DivMmc().Automapped()) << "the off-area maps out";

    // MAPRAM: bank 3 at #0000, read-only, and the sticky bit survives a write of 0
    Out(0xE3, 0x40 | 0x03);
    Out(0xE3, 0x00);
    EXPECT_TRUE(_ports->DivMmc().Mapram());
    nextreg(0x09, 0x08);  // NR #09 bit 3 clears it
    EXPECT_FALSE(_ports->DivMmc().Mapram());
}

// The register table (generated from the distribution's registers.txt) and the board's reset values from it
TEST_F(NextSkeleton_Test, RegisterTableMatchesTheDocumentAndTheBoardResets)
{
    size_t count = 0;
    const NextRegInfo* table = NextRegTable(count);
    EXPECT_EQ(count, 145u);
    ASSERT_NE(FindNextReg(0x50), nullptr);
    EXPECT_STREQ(FindNextReg(0x50)->name, "MMU slot 0");
    EXPECT_TRUE(FindNextReg(0x00)->readable);
    EXPECT_FALSE(FindNextReg(0x00)->writable);
    EXPECT_EQ(FindNextReg(0x14)->reset, 0xE3);
    EXPECT_EQ(FindNextReg(0x4A)->reset, 0xE3);
    EXPECT_EQ(FindNextReg(0xC4)->reset, 0x81);
    EXPECT_EQ(FindNextReg(0xB8)->reset, 0x83);
    EXPECT_EQ(FindNextReg(0xBB)->reset, 0xCD);
    EXPECT_EQ(FindNextReg(0x7F)->reset, 0xFF);
    EXPECT_EQ(FindNextReg(0xFE), nullptr);
    // after a hard reset the board holds every table reset value (the registers a device owns answer for themselves)
    for (size_t i = 0; i < count; i++)
    {
        const NextRegInfo& r = table[i];
        if (!r.hasReset || r.number == 0x03 || (r.number >= 0x50 && r.number <= 0x57) || (r.number >= 0xC0 && r.number <= 0xCE) ||
            r.number == 0x07 || r.number == 0x8E || r.number == 0x02 || r.number == 0x00 || r.number == 0x01 || r.number == 0x0E)
            continue;
        EXPECT_EQ(_ports->Board().Stored(r.number), r.reset) << "NR " << std::hex << int(r.number) << " " << r.name;
    }
    const std::string report = _ports->Board().DescribeRegisters();
    EXPECT_NE(report.find("NR 50 RW MMU slot 0"), std::string::npos);
    EXPECT_NE(report.find("NR 00 R- Machine ID"), std::string::npos);
}

// The reports (DeviceState::Next / NextRegs / NextMmu) behind /state/next, /state/next/regs, /state/next/mmu
TEST_F(NextSkeleton_Test, ReportsDescribeTheMachine)
{
    StateNode next = DeviceState::Next(_context);
    ASSERT_NE(next.find("available"), nullptr);
    ASSERT_NE(next.find("machine"), nullptr);
    EXPECT_EQ(next.find("mmu")->find("slots")->size(), 8u);
    ASSERT_NE(next.find("divmmc"), nullptr);
    ASSERT_NE(next.find("interrupts"), nullptr);
    EXPECT_EQ(next.find("ctc")->size(), 4u);
    StateNode regs = DeviceState::NextRegs(_context);
    EXPECT_EQ(regs.find("registers")->size(), 145u);
    StateNode mmu = DeviceState::NextMmu(_context);
    EXPECT_EQ(mmu.find("slots")->size(), 8u);
}

// NR #8C: the alternate ROM (system pages 6 and 7) replaces the ROM for reads, or is written through the ROM area; the
// lock bits pick the ROM whatever the paging ports say; the low nibble reaches the high one at a soft reset
TEST_F(NextSkeleton_Test, AlternateRomAndLocks)
{
    auto nextreg = [&](uint8_t reg, uint8_t v) {
        Out(0x243B, reg);
        Out(0x253B, v);
    };
    Out(0x7FFD, 0x00);  // ROM 0 (128K editor): alt ROM 0 is system page 6
    std::memset(_memory->ROMPageHostAddress(6), 0xA6, 0x4000);
    std::memset(_memory->ROMPageHostAddress(7), 0xA7, 0x4000);
    const uint8_t rom0 = _memory->PeekSlot(0x0100);
    nextreg(0x8C, 0x80);
    EXPECT_EQ(_memory->PeekSlot(0x0100), 0xA6) << "alt ROM replaces the ROM for reads";
    _memory->SlotWriteFast(0x0100, 0x11);
    EXPECT_EQ(_memory->ROMPageHostAddress(6)[0x100], 0xA6) << "and is not written";
    Out(0x7FFD, 0x10);  // ROM 1: alt ROM 1 (48K)
    EXPECT_EQ(_memory->PeekSlot(0x0100), 0xA7);
    nextreg(0x8C, 0xC0);  // visible during writes only
    EXPECT_NE(_memory->PeekSlot(0x0100), 0xA7) << "reads show the normal ROM";
    _memory->SlotWriteFast(0x0100, 0x5C);
    EXPECT_EQ(_memory->ROMPageHostAddress(7)[0x100], 0x5C) << "writes land in the alt ROM";
    nextreg(0x8C, 0x00);
    Out(0x243B, 0x8C);
    EXPECT_EQ(In(0x253B), 0x00);
    // locks (128K family: lock ROM 1 forces ROM 1, lock ROM 0 forces ROM 0 - bit 5 wins)
    Out(0x7FFD, 0x00);
    nextreg(0x8C, 0x20);
    EXPECT_EQ(_memory->GetRomSelect(), 1);
    nextreg(0x8C, 0x10);
    Out(0x7FFD, 0x10);
    EXPECT_EQ(_memory->GetRomSelect(), 0);
    (void)rom0;
}

// NR #18-#1C clip windows and NR #40-#44 palettes: the registers the ROM reads back and restores (cosim first diff)
TEST_F(NextSkeleton_Test, ClipWindowsAndPalettesReadBackAsWritten)
{
    auto nextreg = [&](uint8_t reg, uint8_t v) {
        Out(0x243B, reg);
        Out(0x253B, v);
    };
    auto read = [&](uint8_t reg) {
        Out(0x243B, reg);
        return In(0x253B);
    };
    // the clip index advances on writes, not on reads
    EXPECT_EQ(read(0x18), 0x00);
    EXPECT_EQ(read(0x18), 0x00);
    nextreg(0x18, 10);
    EXPECT_EQ(read(0x18), 0xFF) << "X2 is next";
    nextreg(0x18, 200);
    nextreg(0x18, 20);
    nextreg(0x18, 180);
    EXPECT_EQ(read(0x18), 10) << "the index wrapped to X1";
    EXPECT_EQ(read(0x1C) & 3, 0);
    EXPECT_EQ(read(0x1B), 0x00);
    nextreg(0x1C, 0x08);  // reset the tilemap index
    // the ULA palette: standard colours, bright white, write + auto-increment
    nextreg(0x43, 0x00);
    nextreg(0x40, 7);
    EXPECT_EQ(read(0x41), 0xB6) << "white";
    nextreg(0x40, 15);
    EXPECT_EQ(read(0x41), 0xFF) << "bright white";
    nextreg(0x40, 100);
    nextreg(0x41, 0x1C);
    EXPECT_EQ(read(0x40), 101) << "auto-increment after a write";
    nextreg(0x40, 100);
    EXPECT_EQ(read(0x41), 0x1C);
    nextreg(0x43, 0x80);  // no auto-increment
    nextreg(0x40, 5);
    nextreg(0x41, 0x55);
    EXPECT_EQ(read(0x40), 5);
    nextreg(0x43, 0x00);
    // 9-bit: two writes, the second carries the low blue bit
    nextreg(0x40, 20);
    nextreg(0x44, 0xE0);
    nextreg(0x44, 0x01);
    EXPECT_EQ(read(0x40), 21);
    nextreg(0x40, 20);
    EXPECT_EQ(read(0x41), 0xE0);
    EXPECT_EQ(read(0x44), 0x01);
    // NR #08 bit 7: port #7FFD not locked
    EXPECT_EQ(read(0x08) & 0x80, 0x80);
}
