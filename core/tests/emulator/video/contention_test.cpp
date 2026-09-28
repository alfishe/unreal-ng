#include "stdafx.h"
#include "pch.h"

#include <initializer_list>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"
#include "emulator/video/ulacontention.h"

/// ULA memory/IO contention tests - ZX-Spectrum 48K and 128K separately.
///
/// The Ferranti ULA shares the memory bus with the CPU: accesses to contended
/// RAM while the ULA fetches video data stall the CPU per the canonical
/// pattern 6,5,4,3,2,1,0,0 across each 8T character cell (zxnet FAQ
/// "Contended memory"). Contended regions:
///   48K:  0x4000-0x7FFF
///   128K: 0x4000-0x7FFF (page 5) + 0xC000-0xFFFF when an odd page (1/3/5/7)
///         is mapped into bank 3 via port 7FFD
/// Machine geometry differs: 48K 224 T/line (69888 T/frame), 128K 228 T/line
/// (70908 T/frame). Patterns are anchored on the INT, the physical reference:
/// the first contended T is INT + 14335 on the 48K and INT + 14361 on the 128K
/// (classic onset, ZXMAK2), 5 T before the first displayed pixel at +14340 /
/// +14366 (Xpeccy ULA.48/128, MiSTer ula.sv). The INT fires at intstart + 1.

namespace
{
constexpr uint8_t kPattern[8] = {6, 5, 4, 3, 2, 1, 0, 0};
}

/// Shared fixture logic; model injected by subclasses
class ContentionTestBase : public ::testing::Test
{
protected:
    const char* _modelName = "48K";
    uint32_t _intToFirstContended = 14335;

    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    UlaContention* _ula = nullptr;

    uint32_t _firstContendedT = 0;  // First t-state of the first contended cell

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(_modelName, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create " << _modelName << " emulator";

        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _memory = _context->pMemory;
        // Mode detection (InitRaster) runs at frame start - trigger it so the
        // model's base video mode (and contention flag) is applied
        _context->pScreen->InitFrame();

        _ula = _context->pUlaContention;
        ASSERT_NE(_ula, nullptr);
        ASSERT_TRUE(_ula->IsContentionEnabled()) << _modelName << " must have ULA contention enabled"
            << " (mem_model=" << (int)_context->config.mem_model
            << " videoMode=" << (int)_context->pScreen->GetVideoMode() << ")";

        _firstContendedT = _context->config.intstart + 1 + _intToFirstContended;

        // The same T seen from the raster: 5 T before the renderer's first paper pixel
        const ContentionRaster& raster = _ula->GetRaster();
        ASSERT_EQ(_firstContendedT, raster.screenAreaStart + raster.screenLineAreaStart - 5)
            << "INT-relative contention onset and the raster disagree";
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    uint8_t delayAt(uint32_t t)
    {
        _z80->t = t;
        return _ula->GetContentionDelay();
    }

    /// Execute LD A,(HL) at $8000 (uncontended code) with the data read
    /// starting at exactly `dataReadT`; returns total instruction T-states
    uint32_t runLdAHl(uint16_t hl, uint32_t dataReadT)
    {
        _memory->DirectWriteToZ80Memory(0x8000, 0x7E);  // LD A,(HL)
        _z80->pc = 0x8000;
        _z80->hl = hl;
        _z80->iff1 = 0;
        _z80->t = dataReadT - 4;  // 4T opcode fetch precedes the data read

        uint32_t t0 = _z80->t;
        _z80->Z80Step();
        return _z80->t - t0;
    }

    /// Execute one instruction placed at `addr` with its opcode fetch (M1) starting at exactly `fetchT`;
    /// returns its T-states. Code runs where the instruction is placed, so a contended `addr` puts the
    /// fetch and the operand bytes in contended memory
    uint32_t runAt(uint16_t addr, std::initializer_list<uint8_t> bytes, uint32_t fetchT)
    {
        uint16_t a = addr;
        for (uint8_t b : bytes)
            _memory->DirectWriteToZ80Memory(a++, b);
        _z80->pc = addr;
        _z80->iff1 = 0;
        _z80->t = fetchT;

        uint32_t t0 = _z80->t;
        _z80->Z80Step();
        return _z80->t - t0;
    }
};

class Contention48K_Test : public ContentionTestBase
{
protected:
    Contention48K_Test() { _modelName = "48K"; }
};

class Contention128K_Test : public ContentionTestBase
{
protected:
    Contention128K_Test()
    {
        _modelName = "128K";
        _intToFirstContended = 14361;
    }
};

/// region <ZX-Spectrum 48K>

TEST_F(Contention48K_Test, MachineGeometry)
{
    const ContentionRaster& raster = _ula->GetRaster();
    EXPECT_EQ(raster.tstatesPerLine, 224u) << "48K: 224 T-states per line";
    EXPECT_EQ(raster.configFrameDuration, 69888u) << "48K: 224 * 312 lines";
}

TEST_F(Contention48K_Test, NoContentionOneTBeforeOnsetOrAfterLastCell)
{
    const ContentionRaster& raster = _ula->GetRaster();
    EXPECT_EQ(delayAt(_firstContendedT - 1), 0u) << "INT + 14334 is not contended";
    EXPECT_EQ(delayAt(_firstContendedT + 127), kPattern[7]) << "last T of the 128 T contended span";
    EXPECT_EQ(delayAt(_firstContendedT + 120), kPattern[0]) << "last cell starts at +120";
    EXPECT_EQ(delayAt(_firstContendedT + 128), 0u) << "right border";
    EXPECT_EQ(delayAt(_firstContendedT + 191 * raster.tstatesPerLine), kPattern[0]) << "last paper line";
    EXPECT_EQ(delayAt(_firstContendedT + 192 * raster.tstatesPerLine), 0u) << "first bottom-border line";
}

TEST_F(Contention48K_Test, PatternShape_FirstCell)
{
    for (uint32_t k = 0; k < 8; k++)
    {
        EXPECT_EQ(delayAt(_firstContendedT + k), kPattern[k])
            << "48K contention pattern at cell offset " << k;
    }
}

TEST_F(Contention48K_Test, PatternRepeats_AcrossCellsAndLines)
{
    const ContentionRaster& raster = _ula->GetRaster();

    // Next cell in the same line
    for (uint32_t k = 0; k < 8; k++)
        EXPECT_EQ(delayAt(_firstContendedT + 8 + k), kPattern[k]);

    // Line 100 of the paper area
    uint32_t line100 = _firstContendedT + 100 * raster.tstatesPerLine;
    for (uint32_t k = 0; k < 8; k++)
        EXPECT_EQ(delayAt(line100 + k), kPattern[k]);
}

TEST_F(Contention48K_Test, NoContentionOutsidePaperArea)
{
    const ContentionRaster& raster = _ula->GetRaster();

    EXPECT_EQ(delayAt(raster.screenAreaStart - 10), 0u) << "Above paper (top border)";
    EXPECT_EQ(delayAt(raster.screenAreaEnd + 10), 0u) << "Below paper (bottom border)";

    // Horizontal border of a paper line: right after the contended line area
    uint32_t line5 = raster.screenAreaStart + 5 * raster.tstatesPerLine;
    EXPECT_EQ(delayAt(line5 + raster.screenLineAreaEnd + 2), 0u) << "Right border";
}

TEST_F(Contention48K_Test, MemoryAccess_ContendedVsUncontended)
{
    // Data read entering at cell offset 0 -> +6T stall
    EXPECT_EQ(runLdAHl(0x4000, _firstContendedT), 13u) << "LD A,(HL) from contended $4000: 7 + 6";
    EXPECT_EQ(runLdAHl(0x7FFF, _firstContendedT), 13u) << "$7FFF still contended";

    // Uncontended regions: ROM and upper RAM
    EXPECT_EQ(runLdAHl(0x3FFF, _firstContendedT), 7u) << "ROM is not contended";
    EXPECT_EQ(runLdAHl(0x8000, _firstContendedT), 7u) << "$8000+ is not contended on 48K";
    EXPECT_EQ(runLdAHl(0xC000, _firstContendedT), 7u) << "$C000+ is not contended on 48K";

    // Contended address outside the paper area: no stall
    EXPECT_EQ(runLdAHl(0x4000, _ula->GetRaster().screenAreaStart - 1000), 7u);
}

TEST_F(Contention48K_Test, IOContention_EvenVsOddPorts)
{
    // In paper, cell offset 0
    _z80->t = _firstContendedT;
    EXPECT_EQ(_ula->GetIOContentionDelay(0x00FE), 6u) << "48K even port (A0=0): pattern delay";
    EXPECT_EQ(_ula->GetIOContentionDelay(0x00FF), 0u) << "48K odd port (A0=1): no delay";
    EXPECT_EQ(_ula->GetIOContentionDelay(0x00FD), 0u) << "48K odd port (A0=1, A1=0): no delay";

    // Outside paper: no IO contention on 48K
    _z80->t = _ula->GetRaster().screenAreaStart - 1000;
    EXPECT_EQ(_ula->GetIOContentionDelay(0x00FE), 0u);
}

TEST_F(Contention48K_Test, AddressContentionMap)
{
    EXPECT_FALSE(_ula->IsAddressContended(0x3FFF));
    EXPECT_TRUE(_ula->IsAddressContended(0x4000));
    EXPECT_TRUE(_ula->IsAddressContended(0x7FFF));
    EXPECT_FALSE(_ula->IsAddressContended(0x8000));
    EXPECT_FALSE(_ula->IsAddressContended(0xC000)) << "48K has no paged bank 3 contention";
}

/// Opcode fetches (M1) and operand bytes read at PC wait like data accesses (M1 contention rework)
TEST_F(Contention48K_Test, M1_NopFromContendedRamWaitsThePattern)
{
    for (uint32_t k = 0; k < 16; k++)
        EXPECT_EQ(runAt(0x4000, { 0x00 }, _firstContendedT + k), 4u + kPattern[k % 8]) << "NOP at $4000, cell offset " << k;
}

TEST_F(Contention48K_Test, M1_NoWaitOutsideContendedRamOrPaper)
{
    EXPECT_EQ(runAt(0x8000, { 0x00 }, _firstContendedT), 4u) << "$8000 is not contended";
    EXPECT_EQ(runAt(0x4000, { 0x00 }, _firstContendedT - 1), 4u) << "1 T before the onset";
    EXPECT_EQ(runAt(0x4000, { 0x00 }, _firstContendedT + 128), 4u) << "right border";
    EXPECT_EQ(runAt(0x4000, { 0x00 }, _ula->GetRaster().screenAreaStart - 1000), 4u) << "top border";
}

TEST_F(Contention48K_Test, M1_OperandBytesAtPcWait)
{
    // LD A,n at $4000, fetch at cell offset 0: fetch waits 6, the operand read starts 10 T later (offset 2)
    // and waits 4
    EXPECT_EQ(runAt(0x4000, { 0x3E, 0x55 }, _firstContendedT), 7u + 6u + 4u);
    // Opcode at $7FFF (contended), operand at $8000 (not)
    EXPECT_EQ(runAt(0x7FFF, { 0x3E, 0x55 }, _firstContendedT), 7u + 6u);
}

TEST_F(Contention48K_Test, M1_PrefixedOpcodeFetchesWaitTwice)
{
    // INC IX (DD 23): the DD fetch waits 6, the 23 fetch starts at offset 2 and waits 4 (its 2 internal
    // cycles are no-MREQ: not contended in this phase)
    EXPECT_EQ(runAt(0x4000, { 0xDD, 0x23 }, _firstContendedT), 10u + 6u + 4u);
}

TEST_F(Contention48K_Test, M1_CodeAndDataInContendedRam)
{
    // LD A,(HL) at $4000 with HL = $4000: the fetch waits 6, the data read starts at offset 2 and waits 4
    _z80->hl = 0x4000;
    EXPECT_EQ(runAt(0x4000, { 0x7E }, _firstContendedT), 7u + 6u + 4u);
}

/// endregion </ZX-Spectrum 48K>

/// region <ZX-Spectrum 128K>

TEST_F(Contention128K_Test, MachineGeometry)
{
    const ContentionRaster& raster = _ula->GetRaster();
    EXPECT_EQ(raster.tstatesPerLine, 228u)
        << "128K: 228 T-states per line (mem_model=" << (int)_context->config.mem_model
        << " videoMode=" << (int)_context->pScreen->GetVideoMode() << ")";
    EXPECT_EQ(raster.configFrameDuration, 70908u) << "128K: 228 * 311 lines";
}

TEST_F(Contention128K_Test, PatternShape_FirstCell)
{
    for (uint32_t k = 0; k < 8; k++)
    {
        EXPECT_EQ(delayAt(_firstContendedT + k), kPattern[k])
            << "128K contention pattern at cell offset " << k;
    }
}

TEST_F(Contention128K_Test, NoContentionOutsidePaperArea)
{
    const ContentionRaster& raster = _ula->GetRaster();
    EXPECT_EQ(delayAt(raster.screenAreaStart - 10), 0u);
    EXPECT_EQ(delayAt(raster.screenAreaEnd + 10), 0u);
}

TEST_F(Contention128K_Test, MemoryAccess_Page5AlwaysContended)
{
    EXPECT_EQ(runLdAHl(0x4000, _firstContendedT), 13u) << "LD A,(HL) from $4000 (page 5): 7 + 6";
    EXPECT_EQ(runLdAHl(0x8000, _firstContendedT), 7u) << "$8000 (page 2, even) not contended";
}

TEST_F(Contention128K_Test, Bank3_OddPagesContended)
{
    // Default page 0 (even): uncontended
    ASSERT_EQ(_memory->GetRAMPageForBank3(), 0u);
    EXPECT_FALSE(_ula->IsAddressContended(0xC000));
    EXPECT_EQ(runLdAHl(0xC000, _firstContendedT), 7u) << "Page 0 in bank 3: no contention";

    // Odd pages 1/3/5/7 share the ULA's bus: contended
    for (uint16_t page : {1, 3, 5, 7})
    {
        _memory->SetRAMPageToBank3(page);
        EXPECT_TRUE(_ula->IsAddressContended(0xC000)) << "Page " << page;
        EXPECT_TRUE(_ula->IsAddressContended(0xFFFF)) << "Page " << page;
        EXPECT_EQ(runLdAHl(0xC000, _firstContendedT), 13u)
            << "Page " << page << " in bank 3: contended (7 + 6)";
    }

    // Even pages: uncontended
    for (uint16_t page : {2, 4, 6})
    {
        _memory->SetRAMPageToBank3(page);
        EXPECT_FALSE(_ula->IsAddressContended(0xC000)) << "Page " << page;
        EXPECT_EQ(runLdAHl(0xC000, _firstContendedT), 7u) << "Page " << page;
    }

    _memory->SetRAMPageToBank3(0);
}

TEST_F(Contention128K_Test, IOContention_128KRules)
{
    // In paper, cell offset 0. 128K rules (Contended_I/O):
    //   A0=0:        pattern delay + 1T
    //   A0=1, A1=0:  1T
    //   A0=1, A1=1:  no delay
    _z80->t = _firstContendedT;
    EXPECT_EQ(_ula->GetIOContentionDelay(0x00FE), 7u) << "128K even port: pattern + 1";
    EXPECT_EQ(_ula->GetIOContentionDelay(0x00FD), 1u) << "128K A0=1, A1=0: 1T";
    EXPECT_EQ(_ula->GetIOContentionDelay(0x00FF), 0u) << "128K A0=1, A1=1: no delay";

    // Outside paper: even ports keep the +1, odd A1=0 keeps 1T
    _z80->t = _ula->GetRaster().screenAreaStart - 1000;
    EXPECT_EQ(_ula->GetIOContentionDelay(0x00FE), 1u);
    EXPECT_EQ(_ula->GetIOContentionDelay(0x00FD), 1u);
    EXPECT_EQ(_ula->GetIOContentionDelay(0x00FF), 0u);
}

TEST_F(Contention128K_Test, M1_FetchFromOddPageAtC000Waits)
{
    for (uint32_t k = 0; k < 8; k++)
        EXPECT_EQ(runAt(0x4000, { 0x00 }, _firstContendedT + k), 4u + kPattern[k]) << "page 5, cell offset " << k;

    _memory->SetRAMPageToBank3(7);
    EXPECT_EQ(runAt(0xC000, { 0x00 }, _firstContendedT), 4u + 6u) << "page 7 at $C000";
    _memory->SetRAMPageToBank3(2);
    EXPECT_EQ(runAt(0xC000, { 0x00 }, _firstContendedT), 4u) << "page 2 at $C000";
    _memory->SetRAMPageToBank3(0);
}

/// endregion </ZX-Spectrum 128K>

/// region <ZX-Spectrum +2A/+3 (Amstrad gate array)>

/// The +2A/+3 gate array keeps the 128K frame and contention onset but has its own rules (consensus of
/// MAME, ZXMAK2, BizHawk, ZX-M8XXX, Spectral, xpeccy-plus, zxsp, jnext): the 1,0,7,6,5,4,3,2 pattern,
/// RAM pages 4-7 contended in any slot, no I/O contention, and a floating bus only on #0FFD-type ports
class ContentionPlus3_Test : public ContentionTestBase
{
protected:
    ContentionPlus3_Test()
    {
        _modelName = "PLUS3";
        _intToFirstContended = 14361;
    }

    void Out1FFD(uint8_t value) { _context->pPortDecoder->DecodePortOut(0x1FFD, value, 0x8000); }

    /// The standard machine boots into 48 BASIC, which locks #7FFD paging (and with it the #1FFD layouts
    /// and the floating bus): unlock it the way a reset into the menu leaves it
    void Unlock() { _context->emulatorState.p7FFD &= static_cast<uint8_t>(~0x20); }
};

TEST_F(ContentionPlus3_Test, GateArrayPatternFromTheSameOnset)
{
    ASSERT_TRUE(_ula->IsGateArray());
    const uint8_t pattern[8] = { 1, 0, 7, 6, 5, 4, 3, 2 };
    for (uint32_t k = 0; k < 16; k++)
        EXPECT_EQ(delayAt(_firstContendedT + k), pattern[k % 8]) << "cell offset " << k;
    EXPECT_EQ(delayAt(_firstContendedT - 1), 0u);
    EXPECT_EQ(runLdAHl(0x4000, _firstContendedT), 8u) << "LD A,(HL) from $4000 at offset 0: 7 + 1";
    EXPECT_EQ(runLdAHl(0x4000, _firstContendedT + 2), 14u) << "at offset 2: 7 + 7";
}

TEST_F(ContentionPlus3_Test, PagesFourToSevenInAnySlot)
{
    for (uint16_t page : { 4, 5, 6, 7 })
    {
        _memory->SetRAMPageToBank3(page);
        EXPECT_TRUE(_ula->IsAddressContended(0xC000)) << "page " << page;
    }
    for (uint16_t page : { 0, 1, 2, 3 })
    {
        _memory->SetRAMPageToBank3(page);
        EXPECT_FALSE(_ula->IsAddressContended(0xC000)) << "page " << page << " (odd pages are a 128K rule)";
    }

    // All-RAM layout 0 = pages 0,1,2,3: nothing contended, #4000 included
    Unlock();
    Out1FFD(0x01);
    for (uint16_t addr : { 0x0000, 0x4000, 0x8000, 0xC000 })
        EXPECT_FALSE(_ula->IsAddressContended(addr)) << std::hex << addr;

    // All-RAM layout 1 = pages 4,5,6,7: every slot contended, #0000 included
    Out1FFD(0x03);
    for (uint16_t addr : { 0x0000, 0x4000, 0x8000, 0xC000 })
        EXPECT_TRUE(_ula->IsAddressContended(addr)) << std::hex << addr;
    EXPECT_EQ(runLdAHl(0x0000, _firstContendedT), 8u) << "page 4 at #0000 is contended";

    // Back to ROM at #0000: ROM is never contended
    Out1FFD(0x00);
    EXPECT_FALSE(_ula->IsAddressContended(0x0000));
    EXPECT_TRUE(_ula->IsAddressContended(0x4000)) << "page 5";
}

TEST_F(ContentionPlus3_Test, NoIoContention)
{
    for (uint32_t k = 0; k < 8; k++)
    {
        _z80->t = _firstContendedT + k;
        EXPECT_EQ(_ula->GetIOContentionDelay(0x00FE), 0u) << "offset " << k;
        EXPECT_EQ(_ula->GetIOContentionDelay(0x00FD), 0u);
        EXPECT_EQ(_ula->GetIOContentionDelay(0x7FFD), 0u);
    }
}

TEST_F(ContentionPlus3_Test, FloatingBusOnlyOnPort0FFDWhilePagingIsUnlocked)
{
    _context->config.floatbus = 1;
    Unlock();
    for (uint16_t a = 0x4000; a < 0x5800; a++)
        _memory->DirectWriteToZ80Memory(a, 0x40);  // bitmap
    for (uint16_t a = 0x5800; a < 0x5B00; a++)
        _memory->DirectWriteToZ80Memory(a, 0x38);  // attributes

    // During the first paper cells the fetched bytes appear with bit 0 set
    bool sawBitmap = false;
    bool sawAttribute = false;
    for (uint32_t k = 0; k < 16; k++)
    {
        _z80->t = _firstContendedT + k;
        const uint8_t value = _ula->GetGateArrayFloatingBus(0x0FFD);
        sawBitmap |= value == 0x41;
        sawAttribute |= value == 0x39;
        EXPECT_EQ(_ula->GetGateArrayFloatingBus(0x00FF), 0xFF) << "not a 0000 xxxx xxxx xx01 port";
        EXPECT_EQ(_ula->GetGateArrayFloatingBus(0x1FFD), 0xFF) << "not a 0000 xxxx xxxx xx01 port";
    }
    EXPECT_TRUE(sawBitmap);
    EXPECT_TRUE(sawAttribute);

    // Between fetches (the border): the byte of the last contended access, here a real LD A,(HL)
    _memory->DirectWriteToZ80Memory(0x4000, 0x80);
    const uint32_t border = _ula->GetRaster().screenAreaStart - 1000;
    runLdAHl(0x4000, border);
    _z80->t = border;
    EXPECT_EQ(_ula->GetGateArrayFloatingBus(0x0FFD), 0x81);

    // Paging locked: the gate array stops driving the bus
    _context->emulatorState.p7FFD |= 0x20;
    EXPECT_EQ(_ula->GetGateArrayFloatingBus(0x0FFD), 0xFF);
    _context->emulatorState.p7FFD &= static_cast<uint8_t>(~0x20);
}

TEST_F(ContentionPlus3_Test, M1_FetchWaitsTheGateArrayPattern)
{
    const uint8_t pattern[8] = { 1, 0, 7, 6, 5, 4, 3, 2 };
    for (uint32_t k = 0; k < 8; k++)
        EXPECT_EQ(runAt(0x4000, { 0x00 }, _firstContendedT + k), 4u + pattern[k]) << "cell offset " << k;

    // All-RAM layout 1: page 4 at #0000, code there waits too
    Unlock();
    Out1FFD(0x03);
    EXPECT_EQ(runAt(0x0000, { 0x00 }, _firstContendedT), 4u + 1u) << "page 4 at #0000";
    Out1FFD(0x00);
}

TEST_F(ContentionPlus3_Test, M1_FloatingBusLatchSeesOpcodeFetches)
{
    _context->config.floatbus = 1;
    Unlock();
    const uint32_t border = _ula->GetRaster().screenAreaStart - 1000;  // between screen fetches

    _memory->DirectWriteToZ80Memory(0x5000, 0xAA);
    runLdAHl(0x5000, _firstContendedT);  // data read: the latch holds #AA
    _z80->t = border;
    EXPECT_EQ(_ula->GetGateArrayFloatingBus(0x0FFD), 0xABu);

    runAt(0x4000, { 0x00 }, _firstContendedT);  // opcode fetch from contended RAM: the latch holds #00
    _z80->t = border;
    EXPECT_EQ(_ula->GetGateArrayFloatingBus(0x0FFD), 0x01u) << "the fetched opcode, bit 0 forced";
}

/// endregion </ZX-Spectrum +2A/+3 (Amstrad gate array)>

/// region <Contrast: Pentagon has no contention>

TEST(ContentionPentagon_Test, NoContentionAnywhere)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);

    UlaContention* ula = emulator->GetContext()->pUlaContention;
    ASSERT_NE(ula, nullptr);
    EXPECT_FALSE(ula->IsContentionEnabled())
        << "(mem_model=" << (int)emulator->GetContext()->config.mem_model
        << " videoMode=" << (int)emulator->GetContext()->pScreen->GetVideoMode() << ")";
    EXPECT_FALSE(ula->IsAddressContended(0x4000));

    Z80* z80 = emulator->GetContext()->pCore->GetZ80();
    z80->t = 20000;  // Mid-paper on Pentagon
    EXPECT_EQ(ula->GetContentionDelay(), 0u);
    EXPECT_EQ(ula->GetIOContentionDelay(0x00FE), 0u);

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// endregion </Contrast>
/// region <Memory interface selection>

/// Core::SelectMemoryInterface: machines with contention run the contended interfaces (and the I/O rule of
/// the Ferranti ULA), all others the plain ones; the debugger swaps Fast for Debug in both
TEST(MemoryInterfaceSelection_Test, FollowsTheMachineAndTheDebugger)
{
    struct Case
    {
        const char* model;
        bool contended;
        bool ioContended;
    };
    const Case cases[] = {
        { "PENTAGON", false, false }, { "48K", true, true },       { "128K", true, true },
        { "PLUS2", true, true },      { "PLUS2A", true, false },   { "PLUS3", true, false },
        { "SCORPION", false, false }, { "PROFSCORP", false, false }, { "PROFI", false, false },
        { "ATM710", false, false },   { "ATM3", false, false },
    };

    for (const Case& c : cases)
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(c.model, LoggerLevel::LogError);
        ASSERT_NE(emulator, nullptr) << c.model;
        EmulatorContext* context = emulator->GetContext();
        context->pScreen->InitFrame();
        Z80* z80 = context->pCore->GetZ80();

        EXPECT_EQ(z80->MemIf, c.contended ? z80->FastContendedMemIf : z80->FastMemIf) << c.model;
        EXPECT_EQ(z80->ioContention, c.ioContended ? context->pUlaContention : nullptr) << c.model;

        emulator->DebugOn();
        EXPECT_EQ(z80->MemIf, c.contended ? z80->DbgContendedMemIf : z80->DbgMemIf) << c.model << " debug on";
        EXPECT_EQ(z80->ioContention, c.ioContended ? context->pUlaContention : nullptr) << c.model << " debug on";

        emulator->DebugOff();
        EXPECT_EQ(z80->MemIf, c.contended ? z80->FastContendedMemIf : z80->FastMemIf) << c.model << " debug off";

        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}

/// endregion </Memory interface selection>

