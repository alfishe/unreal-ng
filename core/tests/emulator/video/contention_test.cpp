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
#include "emulator/state/devicestate.h"
#include "debugger/ttd/timetravelmanager.h"
#include "base/featuremanager.h"
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

/// Phase 2: internal (no-MREQ) T-states wait when their address is contended. INC (HL) from #8000 with HL =
/// #4000, M1 starting 4 T before the first contended T: M1 4 (uncontended) | read at offset 0: 6 + 3 | the
/// internal T on HL at offset 9 (cell offset 1): 5 + 1 | write at offset 15 (cell offset 7): 0 + 3
TEST_F(Contention48K_Test, Idle_InternalCycleOnContendedHlWaits)
{
    _z80->hl = 0x4000;
    EXPECT_EQ(runAt(0x8000, { 0x34 }, _firstContendedT - 4), 4u + (6 + 3) + (5 + 1) + (0 + 3));
    _z80->hl = 0x9000;
    EXPECT_EQ(runAt(0x8000, { 0x34 }, _firstContendedT - 4), 11u) << "HL uncontended";
}

/// JR from contended RAM: M1 and the displacement read wait, and so do the 5 internal T-states on the
/// displacement's address
TEST_F(Contention48K_Test, Idle_JrInternalCyclesOnTheDisplacementWait)
{
    // M1 at offset 0: 6 + 4 | displacement read at offset 10 (cell offset 2): 4 + 3 | 5 internal T from offset
    // 17 (cell offset 1): 5+1, then at offset 23 (7): 0+1, 24 (0): 6+1, 31 (7): 0+1, 32 (0): 6+1
    EXPECT_EQ(runAt(0x4000, { 0x18, 0x00 }, _firstContendedT), (6u + 4) + (4 + 3) + (5 + 1) + 1 + (6 + 1) + 1 + (6 + 1));
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

TEST_F(Contention128K_Test, Idle_InternalCycleOnContendedHlWaits)
{
    _z80->hl = 0x4000;
    EXPECT_EQ(runAt(0x8000, { 0x34 }, _firstContendedT - 4), 4u + (6 + 3) + (5 + 1) + (0 + 3));
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

/// The gate array's window is 129 T: after the last fetch cell (offsets 120-127: 1,0,7,6,5,4,3,2 from 120)
/// offset 128 still holds the CPU 1 T, offset 129 is free - on every paper line, the last one included. The
/// Ferranti ULA ends after 128 T (Contention48K_Test.NoContentionOneTBeforeOnsetOrAfterLastCell). Evidence:
/// Rak's Timing Test v0.3 contended NOP photographed on a real +3 and a real +2A (last row 7,6,5,4,4)
TEST_F(ContentionPlus3_Test, WindowEndsOneTAfterTheLastCell)
{
    const ContentionRaster& raster = _ula->GetRaster();
    for (uint32_t line : { 0u, 100u, 191u })
    {
        const uint32_t start = _firstContendedT + line * raster.tstatesPerLine;
        EXPECT_EQ(delayAt(start + 126), 3u) << "line " << line;
        EXPECT_EQ(delayAt(start + 127), 2u) << "line " << line;
        EXPECT_EQ(delayAt(start + 128), 1u) << "line " << line << ": the closing 1 T hold";
        EXPECT_EQ(delayAt(start + 129), 0u) << "line " << line;
    }
    EXPECT_EQ(delayAt(_firstContendedT + 192 * raster.tstatesPerLine + 128), 0u) << "below the paper";
    EXPECT_EQ(runAt(0x4000, { 0x00 }, _firstContendedT + 128), 5u) << "NOP at offset 128: 4 + 1";
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

/// The gate array does not contend internal cycles: INC (HL) waits for its read and write only (read at offset 0:
/// 1 + 3, the internal T at offset 4 free, write at offset 5 (cell offset 5): 4 + 3)
TEST_F(ContentionPlus3_Test, Idle_InternalCyclesNeverWait)
{
    _z80->hl = 0x4000;
    EXPECT_EQ(runAt(0x8000, { 0x34 }, _firstContendedT - 4), 4u + (1 + 3) + 1 + (4 + 3));
    EXPECT_EQ(_z80->idleContention, nullptr);
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

/// Machines without contention never wait: code and data in #4000-#7FFF across a paper line
TEST(ContentionNegative_Test, ClonesNeverWait)
{
    for (const char* model : { "PENTAGON", "SCORPION", "PROFSCORP", "PROFI", "ATM710", "ATM3" })
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(emulator, nullptr) << model;
        EmulatorContext* context = emulator->GetContext();
        context->pScreen->InitFrame();
        Z80* z80 = context->pCore->GetZ80();
        Memory* memory = context->pMemory;
        memory->DirectWriteToZ80Memory(0x6000, 0x00);  // NOP
        memory->DirectWriteToZ80Memory(0x6001, 0x7E);  // LD A,(HL)

        const uint32_t start = context->config.intstart + 1 + 20000;  // mid-paper on every raster
        for (uint32_t k = 0; k < 16; k++)
        {
            z80->iff1 = 0;
            z80->pc = 0x6000;
            z80->hl = 0x4000;
            z80->t = start + k;
            z80->Z80Step();
            EXPECT_EQ(z80->t, start + k + 4) << model << " NOP, offset " << k;
            z80->Z80Step();
            EXPECT_EQ(z80->t, start + k + 11) << model << " LD A,(HL), offset " << k;
        }
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}

/// endregion </Contrast>
/// region <Contention switch, statistics and the status report>

namespace
{
const StateNode& Member(const StateNode& node, const char* key)
{
    static const StateNode missing;
    const StateNode* found = node.find(key);
    return found ? *found : missing;
}

std::string SlotMask(const StateNode& report)
{
    std::string mask;
    for (const StateNode& slot : Member(report, "slots").items)
        mask += Member(slot, "contended").b ? 'C' : '-';
    return mask;
}
}  // namespace

TEST_F(Contention48K_Test, Switch_OffRunsTheMachineUncontended)
{
    FeatureManager* fm = _context->pFeatureManager;
    ASSERT_NE(fm, nullptr);

    ASSERT_TRUE(fm->setFeature(Features::kContention, false));
    EXPECT_EQ(_z80->MemIf, _z80->FastMemIf);
    EXPECT_EQ(_z80->ioContention, nullptr);
    EXPECT_EQ(runAt(0x4000, { 0x00 }, _firstContendedT), 4u) << "no fetch wait";
    EXPECT_EQ(runLdAHl(0x4000, _firstContendedT), 7u) << "no data wait";

    StateNode report = DeviceState::Contention(_context);
    EXPECT_EQ(Member(report, "rule").s, "ula48") << "the machine's rule stays";
    EXPECT_EQ(Member(report, "switch").s, "off");
    EXPECT_FALSE(Member(report, "effective").b);
    EXPECT_EQ(SlotMask(report), "----");
    EXPECT_FALSE(Member(DeviceState::Screen(_context, false), "contention").b);

    ASSERT_TRUE(fm->setFeature(Features::kContention, true));
    EXPECT_EQ(_z80->MemIf, _z80->FastContendedMemIf);
    EXPECT_EQ(runAt(0x4000, { 0x00 }, _firstContendedT), 10u);
    report = DeviceState::Contention(_context);
    EXPECT_TRUE(Member(report, "effective").b);
    EXPECT_EQ(Member(report, "memory_interface").s, "fast_contended");
    EXPECT_EQ(Member(report, "io_rule").s, "ula48");
    EXPECT_EQ(SlotMask(report), "-C--");
}

TEST_F(Contention48K_Test, Statistics_CountedOnlyWhileDebugging)
{
    _ula->ResetStatistics();
    runAt(0x4000, { 0x00 }, _firstContendedT);  // fast interface: not counted
    EXPECT_EQ(_ula->GetStatisticsTotal().accesses[CONTENTION_FETCH], 0u);
    EXPECT_EQ(Member(DeviceState::Contention(_context), "statistics").kind, StateNode::Kind::String);

    _emulator->DebugOn();
    EXPECT_EQ(_z80->MemIf, _z80->DbgContendedMemIf);
    runAt(0x4000, { 0x00 }, _firstContendedT);  // fetch from contended RAM, cell offset 0: waits 6
    runLdAHl(0x4000, _firstContendedT);          // fetch at $8000 (not counted), read from $4000 waits 6
    const ContentionCounters& c = _ula->GetStatisticsCurrentFrame();
    EXPECT_EQ(c.accesses[CONTENTION_FETCH], 1u);
    EXPECT_EQ(c.waitT[CONTENTION_FETCH], 6u);
    EXPECT_EQ(c.accesses[CONTENTION_READ], 1u);
    EXPECT_EQ(c.waitT[CONTENTION_READ], 6u);
    EXPECT_EQ(c.accesses[CONTENTION_WRITE], 0u);

    _z80->bc = 0x00FE;
    runAt(0x8000, { 0xED, 0x78 }, _firstContendedT);  // IN A,(C): an even port, contended
    EXPECT_EQ(_ula->GetStatisticsCurrentFrame().accesses[CONTENTION_IO], 1u);

    const StateNode report = DeviceState::Contention(_context);
    const StateNode& current = Member(Member(report, "statistics"), "current_frame");
    EXPECT_EQ(Member(Member(current, "fetch"), "wait_t").i, 6);
    EXPECT_EQ(Member(current, "accesses").i, 3);

    _ula->OnFrameStart();
    EXPECT_EQ(_ula->GetStatisticsLastFrame().accesses[CONTENTION_FETCH], 1u);
    EXPECT_EQ(_ula->GetStatisticsCurrentFrame().accesses[CONTENTION_FETCH], 0u);
    EXPECT_EQ(_ula->GetStatisticsTotal().accesses[CONTENTION_FETCH], 1u);

    _emulator->DebugOff();
    runAt(0x4000, { 0x00 }, _firstContendedT);
    EXPECT_EQ(_ula->GetStatisticsTotal().accesses[CONTENTION_FETCH], 1u) << "fast interface again: not counted";
}

/// DD CB d op: the (IX+d) access is a data read, not an instruction byte - it counts as a read
TEST_F(Contention48K_Test, Statistics_DdcbOperandIsADataRead)
{
    _emulator->DebugOn();
    _ula->ResetStatistics();
    _z80->ix = 0x4000;
    runAt(0x8000, { 0xDD, 0xCB, 0x01, 0x46 }, _firstContendedT);  // BIT 0,(IX+1): code uncontended
    const ContentionCounters& c = _ula->GetStatisticsCurrentFrame();
    EXPECT_EQ(c.accesses[CONTENTION_READ], 1u);
    EXPECT_EQ(c.accesses[CONTENTION_FETCH], 0u);
    _emulator->DebugOff();
}

TEST_F(Contention128K_Test, Report_OddPageAtC000IsAContendedSlot)
{
    _memory->SetRAMPageToBank3(7);
    StateNode report = DeviceState::Contention(_context);
    EXPECT_EQ(Member(report, "rule").s, "ula128");
    EXPECT_EQ(SlotMask(report), "-C-C");
    EXPECT_TRUE(_context->pCore->IsSlotContended(3));

    _memory->SetRAMPageToBank3(2);
    EXPECT_EQ(SlotMask(DeviceState::Contention(_context)), "-C--");
    _memory->SetRAMPageToBank3(0);
}

TEST_F(ContentionPlus3_Test, Report_GateArrayRuleAndAllRamLayout)
{
    StateNode report = DeviceState::Contention(_context);
    EXPECT_EQ(Member(report, "rule").s, "gatearray");
    EXPECT_EQ(Member(report, "io_rule").s, "none") << "the gate array does not contend ports";
    EXPECT_NE(report.find("floating_bus_latch"), nullptr);

    Unlock();
    Out1FFD(0x03);  // pages 4,5,6,7
    EXPECT_EQ(SlotMask(DeviceState::Contention(_context)), "CCCC");
    Out1FFD(0x00);
}

TEST(ContentionReport_Test, PentagonHasNoRuleAndTheSwitchChangesNothing)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    context->pScreen->InitFrame();
    Z80* z80 = context->pCore->GetZ80();

    StateNode report = DeviceState::Contention(context);
    EXPECT_EQ(Member(report, "rule").s, "none");
    EXPECT_FALSE(Member(report, "applicable").b);
    EXPECT_FALSE(Member(report, "effective").b);
    EXPECT_EQ(Member(report, "memory_interface").s, "fast");
    EXPECT_EQ(SlotMask(report), "----");

    ASSERT_TRUE(context->pFeatureManager->setFeature(Features::kContention, false));
    EXPECT_EQ(z80->MemIf, z80->FastMemIf);
    ASSERT_TRUE(context->pFeatureManager->setFeature(Features::kContention, true));
    EXPECT_EQ(z80->MemIf, z80->FastMemIf) << "no rule: the switch has nothing to turn on";

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// endregion </Contention switch, statistics and the status report>

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

/// The debugger and the 'contention' switch are independent inputs: all four combinations on a contended model
TEST(MemoryInterfaceSelection_Test, DebuggerAndSwitchCombinations)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("128K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    context->pScreen->InitFrame();
    Z80* z80 = context->pCore->GetZ80();
    FeatureManager* fm = context->pFeatureManager;

    struct Step
    {
        bool debug;
        bool contention;
        const MemoryInterface* Z80::*expected;
    };
    const Step steps[] = {
        { false, true, &Z80::FastContendedMemIf }, { true, true, &Z80::DbgContendedMemIf },
        { true, false, &Z80::DbgMemIf },           { false, false, &Z80::FastMemIf },
        { false, true, &Z80::FastContendedMemIf }, { true, false, &Z80::DbgMemIf },
        { true, true, &Z80::DbgContendedMemIf },   { false, false, &Z80::FastMemIf },
    };
    for (const Step& step : steps)
    {
        ASSERT_TRUE(fm->setFeature(Features::kContention, step.contention));
        if (step.debug)
            emulator->DebugOn();
        else
            emulator->DebugOff();
        context->pCore->CPUFrameCycle();  // the frame start re-selects too: nothing may drift
        EXPECT_EQ(z80->MemIf, z80->*step.expected) << "debug " << step.debug << ", contention " << step.contention;
        EXPECT_EQ(z80->ioContention, step.contention ? context->pUlaContention : nullptr);
    }

    fm->setFeature(Features::kContention, true);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// TTD replay engages the debug path; on a contended machine it must stay contended, or history replays with
/// other timing than it was recorded with
TEST(MemoryInterfaceSelection_Test, TtdReplayModeKeepsContention)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    context->pScreen->InitFrame();
    Z80* z80 = context->pCore->GetZ80();
    ASSERT_NE(context->pTimeTravelManager, nullptr);

    ASSERT_EQ(z80->MemIf, z80->FastContendedMemIf);
    context->pTimeTravelManager->EnterReplayMode();
    EXPECT_EQ(z80->MemIf, z80->DbgContendedMemIf);
    context->pTimeTravelManager->ExitReplayMode();
    EXPECT_EQ(z80->MemIf, z80->FastContendedMemIf);

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// endregion </Memory interface selection>

