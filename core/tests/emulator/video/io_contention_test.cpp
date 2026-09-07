#include "pch.h"
#include "stdafx.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"
#include "emulator/video/ulacontention.h"
#include "emulator/video/zx/screenzx.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/ports/models/portdecoder_pentagon128.h"

/// Test fixture for ULA IO contention and floating bus tests
class IOContention_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Core* _cpu = nullptr;
    Z80* _z80 = nullptr;
    Screen* _screen = nullptr;
    UlaContention* _ula = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _cpu = new Core(_context);
        ASSERT_TRUE(_cpu->Init()) << "Core::Init() failed";
        _z80 = _cpu->GetZ80();

        // Use the screen created by Core::Init
        _screen = _context->pScreen;
        ASSERT_NE(_screen, nullptr);

        // UlaContention created by Core::Init
        _ula = _context->pUlaContention;
        ASSERT_NE(_ula, nullptr);
    }

    void TearDown() override
    {
        if (_context)
            _context->pScreen = nullptr;

        delete _cpu;
        delete _context;
    }

    void SetupZX48k()
    {
        CONFIG& config = _context->config;
        config.frame = 69888;
        config.t_line = 224;
        config.mem_model = MM_SPECTRUM48;
        config.floatbus = 1;  // Enable floating bus for tests

        _screen->SetVideoMode(M_ZX48);
    }

    void SetupZX128k()
    {
        CONFIG& config = _context->config;
        config.frame = 70908;
        config.t_line = 228;
        config.mem_model = MM_SPECTRUM128;
        config.floatbus = 1;  // Enable floating bus for tests

        _screen->SetVideoMode(M_ZX128);
    }

    void SetupPentagon()
    {
        CONFIG& config = _context->config;
        config.frame = 71680;
        config.t_line = 224;
        config.mem_model = MM_PENTAGON;
        config.floatbus = 1;  // Enable floating bus for tests

        _screen->SetVideoMode(M_PENTAGON128K);
    }

    /// Calculate the first t-state of paper on a given scanline
    uint32_t PaperStartOnLine(int line)
    {
        const RasterDescriptor& rd = _screen->rasterDescriptors[_screen->GetVideoMode()];
        uint32_t tstatesPerLine = rd.pixelsPerLine / 2;
        uint32_t screenAreaStart = tstatesPerLine * (rd.vSyncLines + rd.vBlankLines + rd.screenOffsetTop);
        uint32_t linePrefix = (rd.hSyncPixels + rd.hBlankPixels + rd.screenOffsetLeft) / 2;
        return screenAreaStart + linePrefix + (uint32_t)line * tstatesPerLine;
    }
};

/// ===================== IO Contention: Model-specific ====================

TEST_F(IOContention_Test, Pentagon_NoIOContention)
{
    SetupPentagon();

    // Pentagon should never have IO contention
    uint32_t paperStart = PaperStartOnLine(64);
    _z80->t = paperStart;
    EXPECT_EQ(_ula->GetIOContentionDelay(0xFE), 0);
}

TEST_F(IOContention_Test, ZX48k_IOContentionForPortFE)
{
    SetupZX48k();

    // Port #FE is contended (A0=0). During paper area, should have contention delay.
    uint32_t paperStart = PaperStartOnLine(64);
    _z80->t = paperStart;

    // At cell offset 0, contention delay should be 6 (from {6,5,4,3,2,1,0,0} pattern)
    uint8_t delay = _ula->GetIOContentionDelay(0xFE);
    EXPECT_EQ(delay, 6);
}

TEST_F(IOContention_Test, ZX48k_IOContentionForOddPort)
{
    SetupZX48k();

    // Odd ports (A0=1) are NOT contended on 48K
    uint32_t paperStart = PaperStartOnLine(64);
    _z80->t = paperStart;

    EXPECT_EQ(_ula->GetIOContentionDelay(0xFF), 0);
    EXPECT_EQ(_ula->GetIOContentionDelay(0x01), 0);
    EXPECT_EQ(_ula->GetIOContentionDelay(0x1F), 0);
}

TEST_F(IOContention_Test, ZX48k_IOContentionZeroOutsidePaper)
{
    SetupZX48k();

    // Before paper area (blank region)
    _z80->t = 0;
    EXPECT_EQ(_ula->GetIOContentionDelay(0xFE), 0);

    // In top border
    _z80->t = 1000;
    EXPECT_EQ(_ula->GetIOContentionDelay(0xFE), 0);

    // After paper area
    _z80->t = 69700;
    EXPECT_EQ(_ula->GetIOContentionDelay(0xFE), 0);
}

/// ===================== IO Contention: Pattern ====================

TEST_F(IOContention_Test, ZX48k_IOContentionFollowsULAPattern)
{
    SetupZX48k();

    // The IO contention follows the same {6,5,4,3,2,1,0,0} pattern as memory contention
    uint32_t paperStart = PaperStartOnLine(64);
    const uint8_t expectedPattern[8] = {6, 5, 4, 3, 2, 1, 0, 0};

    for (int i = 0; i < 8; i++)
    {
        _z80->t = paperStart + i;
        uint8_t delay = _ula->GetIOContentionDelay(0xFE);
        EXPECT_EQ(delay, expectedPattern[i])
            << "IO contention pattern mismatch at cell offset " << i;
    }
}

TEST_F(IOContention_Test, ZX48k_IOContentionNoDelayAtCellOffset6and7)
{
    SetupZX48k();

    uint32_t paperStart = PaperStartOnLine(64);

    _z80->t = paperStart + 6;
    EXPECT_EQ(_ula->GetIOContentionDelay(0xFE), 0);

    _z80->t = paperStart + 7;
    EXPECT_EQ(_ula->GetIOContentionDelay(0xFE), 0);
}

/// ===================== IO Contention: 128K extra delay ====================

TEST_F(IOContention_Test, ZX128k_IOContentionForPortFE)
{
    SetupZX128k();

    // Port #FE on 128K: A0=0 → contention pattern + 1T extra delay
    uint32_t paperStart = PaperStartOnLine(64);
    _z80->t = paperStart;

    // At cell offset 0, base contention = 6, +1 for 128K = 7
    uint8_t delay = _ula->GetIOContentionDelay(0xFE);
    EXPECT_EQ(delay, 7);
}

TEST_F(IOContention_Test, ZX128k_IOContentionOddPortA1Zero)
{
    SetupZX128k();

    // On 128K, odd ports with A1=0 (like 0xFD, 0xF9) get 1T delay during paper
    uint32_t paperStart = PaperStartOnLine(64);
    _z80->t = paperStart;

    EXPECT_EQ(_ula->GetIOContentionDelay(0xFD), 1);
    EXPECT_EQ(_ula->GetIOContentionDelay(0xF9), 1);
}

TEST_F(IOContention_Test, ZX128k_IOContentionOddPortA1One)
{
    SetupZX128k();

    // On 128K, odd ports with A1=1 (like 0xFF) are NOT contended
    uint32_t paperStart = PaperStartOnLine(64);
    _z80->t = paperStart;

    EXPECT_EQ(_ula->GetIOContentionDelay(0xFF), 0);
}

TEST_F(IOContention_Test, ZX48k_OddPortNoContentionEvenA1Zero)
{
    SetupZX48k();

    // On 48K, odd ports are never contended (even with A1=0)
    uint32_t paperStart = PaperStartOnLine(64);
    _z80->t = paperStart;

    EXPECT_EQ(_ula->GetIOContentionDelay(0xFD), 0);
    EXPECT_EQ(_ula->GetIOContentionDelay(0xFF), 0);
}

/// ===================== IO Contention: Integration with in()/out() ====================

TEST_F(IOContention_Test, ZX48k_OutAddsContentionDelayDuringPaper)
{
    SetupZX48k();

    uint32_t paperStart = PaperStartOnLine(64);

    // Set t-state to paper start (cell offset 0 → delay = 6)
    _z80->t = paperStart;

    uint32_t tBefore = _z80->t;

    // Call out() - should add 6T of contention delay
    _z80->out(0xFE, 0x00);

    uint32_t tAfter = _z80->t;

    // The t-state should have increased by at least the contention delay (6T)
    // Note: the actual increase may be more if the port decoder does additional work
    EXPECT_GE(tAfter - tBefore, 6u)
        << "out() to port 0xFE during paper should add IO contention delay";
}

TEST_F(IOContention_Test, ZX48k_OutNoContentionOutsidePaper)
{
    SetupZX48k();

    // Outside paper area → no contention
    _z80->t = 0;

    uint32_t tBefore = _z80->t;
    _z80->out(0xFE, 0x00);
    uint32_t tAfter = _z80->t;

    EXPECT_EQ(tAfter - tBefore, 0u)
        << "out() outside paper area should not add contention delay";
}

TEST_F(IOContention_Test, Pentagon_OutNoContentionDelay)
{
    SetupPentagon();

    uint32_t paperStart = PaperStartOnLine(64);
    _z80->t = paperStart;

    uint32_t tBefore = _z80->t;
    _z80->out(0xFE, 0x00);
    uint32_t tAfter = _z80->t;

    EXPECT_EQ(tAfter - tBefore, 0u)
        << "Pentagon out() should never add contention delay";
}

/// ============================================================================
/// FLOATING BUS TESTS
///
/// Two architectures are tested:
///
/// ── ULA_FERRANTI (ZX-48K, ZX-128K) ──
/// 8-T-state pipeline: 4T fetch + 4T shift per cycle.
/// Verified against ZXMAK2 SpectrumRenderer.cs CalcTableItem / ReadFreeBus.
///
///   phase8 = tInPaper % 8
///   phase8   Bus content          cellIndex     t relative to paperStart
///   ──────   ──────────────────   ───────────   ─────────────────────────
///     0      0xFF (shift)            —          paperStart - 4
///     1      0xFF (shift)            —          paperStart - 3
///     2      Pixel byte (bitmap)   tInPaper/4   paperStart - 2
///     3      Attribute byte        tInPaper/4   paperStart - 1
///     4      Pixel byte (bitmap)   tInPaper/4   paperStart + 0  ← beam enters cell 0
///     5      Attribute byte        tInPaper/4   paperStart + 1
///     6      0xFF (shift)            —          paperStart + 2
///     7      0xFF (shift)            —          paperStart + 3
///
/// ── ULA_DISCRETE_LOGIC (Pentagon, Scorpion) ──
/// 4-T-state continuous fetch: NO shift gaps, ALL T-states have VRAM data.
/// Based on discrete TTL video controller (counters + multiplexers).
///
///   phase4 = tInPaper % 4
///   phase4   Bus content          cellIndex     t relative to paperStart
///   ──────   ──────────────────   ───────────   ─────────────────────────
///     0      Pixel byte (bitmap)   tInPaper/4   paperStart - 4
///     1      Pixel byte (bitmap)   tInPaper/4   paperStart - 3
///     2      Attribute byte        tInPaper/4   paperStart - 2
///     3      Attribute byte        tInPaper/4   paperStart - 1
///     0      Pixel byte (next cell) tInPaper/4  paperStart + 0  ← beam enters cell 0
///     1      Pixel byte             tInPaper/4  paperStart + 1
///     2      Attribute byte         tInPaper/4  paperStart + 2
///     3      Attribute byte         tInPaper/4  paperStart + 3
///
/// Key difference: discrete logic NEVER returns 0xFF during paper.
/// ============================================================================

/// ===================== Floating Bus: Outside Paper ====================

TEST_F(IOContention_Test, Pentagon_FloatingBusWorksDespiteNoContention)
{
    SetupPentagon();

    Memory& memory = *_context->pMemory;
    memory.DefaultBanksFor48k();

    // Pentagon has no memory contention but DOES have a floating bus.
    // Discrete logic: at paperStart+0, tInPaper=4, phase4=0 (pixel byte), cellIndex=1.
    // Pixel addr for y=64: 0x4000 | ((64&0xC0)<<5) | 0 | 0 | 1 = 0x4801
    memory.DirectWriteToZ80Memory(0x4801, 0x42);  // pixel byte for cell 1

    uint32_t paperStart = PaperStartOnLine(64);
    _z80->t = paperStart;  // phase4=0 (pixel fetch, continuous)

    EXPECT_EQ(_ula->GetFloatingBus(), 0x42)
        << "Pentagon floating bus should return video byte even without contention";
}

TEST_F(IOContention_Test, Pentagon_InFF_ServesFloatingBusWhenTrdosOff)
{
    // End-to-end pin of the Beta128 TR-DOS port gate at the Z80::in() level:
    // with the TR-DOS ROM paged out the FDC does not decode $FF (and its
    // register aliases), so IN A,($FF) must return the floating bus byte -
    // the beam-synchronous read beam-locked effects sync on. Outside paper
    // the floating bus is 0xFF, so the read must return 0xFF (not FDC status).
    //
    // The TR-DOS-active counterpart (FDC owns $FF, no floating bus leak)
    // is covered by PortDecoder_Pentagon128_Test::DecodePortIn_Beta128Ports_TRDosOn_
    // at decoder level: executing the IN from >= $4000 in this test would clear
    // CF_TRDOS via the M1 paging hook before the port access.
    SetupPentagon();

    Memory& memory = *_context->pMemory;
    memory.DefaultBanksFor48k();

    // Route Z80::in() through a real Pentagon decoder
    PortDecoder_Pentagon128 decoder(_context);
    PortDecoder* savedDecoder = _context->pPortDecoder;
    _context->pPortDecoder = &decoder;

    // Pixel byte under the beam at paper start of line 64 (same geometry as
    // Pentagon_FloatingBusWorksDespiteNoContention): cell 1 -> 0x4801
    memory.DirectWriteToZ80Memory(0x4801, 0x42);

    // IN A,($FF) at $8000: port access lands at +8T (IORQ at T2 of the IO cycle)
    memory.DirectWriteToZ80Memory(0x8000, 0xDB);
    memory.DirectWriteToZ80Memory(0x8001, 0xFF);
    _z80->iff1 = 0;
    _z80->a = 0x00;

    uint32_t paperStart = PaperStartOnLine(64);

    // Beam over paper: the pixel byte must come through port $FF
    _context->emulatorState.flags &= ~CF_TRDOS;
    _z80->pc = 0x8000;
    _z80->t = paperStart - 8;
    _z80->Z80Step();
    EXPECT_EQ(_z80->a, 0x42)
        << "TR-DOS off: IN A,($FF) must serve the floating bus during paper";

    // Beam in vertical blank: floating bus reads 0xFF -> port reads 0xFF
    _z80->pc = 0x8000;
    _z80->a = 0x00;
    _z80->t = 100;
    _z80->Z80Step();
    EXPECT_EQ(_z80->a, 0xFF)
        << "TR-DOS off: IN A,($FF) must read 0xFF outside paper (no FDC attached to the bus)";

    _context->pPortDecoder = savedDecoder;
}

TEST_F(IOContention_Test, ZX48k_FloatingBusReturnsFFInBlank)
{
    SetupZX48k();

    _z80->t = 0;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xFF);

    _z80->t = 100; // Still in blank
    EXPECT_EQ(_ula->GetFloatingBus(), 0xFF);
}

TEST_F(IOContention_Test, ZX48k_FloatingBusReturnsFFInBorder)
{
    SetupZX48k();

    // Top border area (between blank and screen)
    _z80->t = 5000;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xFF);
}

/// ===================== Floating Bus: 8T Pipeline (comprehensive) ====================

TEST_F(IOContention_Test, ZX48k_FloatingBus8TPipelineAllPhases)
{
    SetupZX48k();

    Memory& memory = *_context->pMemory;
    memory.DefaultBanksFor48k();

    // Write distinct values for pixel and attribute bytes of cells 0 and 1.
    // Line 0: pixel addr = 0x4000 + cellIndex, attr addr = 0x5800 + cellIndex
    memory.DirectWriteToZ80Memory(0x4000, 0xA0);  // cell 0 pixel
    memory.DirectWriteToZ80Memory(0x5800, 0xA1);  // cell 0 attribute
    memory.DirectWriteToZ80Memory(0x4001, 0xB0);  // cell 1 pixel
    memory.DirectWriteToZ80Memory(0x5801, 0xB1);  // cell 1 attribute

    uint32_t paperStart = PaperStartOnLine(0);

    // ── Phase 0 (shift, no fetch): tInPaper=0 → 0xFF ──
    _z80->t = paperStart - 4;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xFF)
        << "phase8=0 should return 0xFF (shift, no fetch)";

    // ── Phase 1 (shift, no fetch): tInPaper=1 → 0xFF ──
    _z80->t = paperStart - 3;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xFF)
        << "phase8=1 should return 0xFF (shift, no fetch)";

    // ── Phase 2 (pixel byte): tInPaper=2, cellIndex=0 → pixel of cell 0 ──
    _z80->t = paperStart - 2;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xA0)
        << "phase8=2 should return pixel byte 0xA0 of cell 0";

    // ── Phase 3 (attribute byte): tInPaper=3, cellIndex=0 → attr of cell 0 ──
    _z80->t = paperStart - 1;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xA1)
        << "phase8=3 should return attribute byte 0xA1 of cell 0";

    // ── Phase 4 (pixel byte): tInPaper=4, cellIndex=1 → pixel of cell 1 ──
    _z80->t = paperStart;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xB0)
        << "phase8=4 should return pixel byte 0xB0 of cell 1";

    // ── Phase 5 (attribute byte): tInPaper=5, cellIndex=1 → attr of cell 1 ──
    _z80->t = paperStart + 1;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xB1)
        << "phase8=5 should return attribute byte 0xB1 of cell 1";

    // ── Phase 6 (shift, no fetch): tInPaper=6 → 0xFF ──
    _z80->t = paperStart + 2;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xFF)
        << "phase8=6 should return 0xFF (shift, no fetch)";

    // ── Phase 7 (shift, no fetch): tInPaper=7 → 0xFF ──
    _z80->t = paperStart + 3;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xFF)
        << "phase8=7 should return 0xFF (shift, no fetch)";
}

TEST_F(IOContention_Test, ZX48k_FloatingBusPixelByteInPaper)
{
    SetupZX48k();

    Memory& memory = *_context->pMemory;
    memory.DefaultBanksFor48k();

    // Pixel byte appears on the floating bus during even phases (2, 4).
    // At paperStart+0: phase8=4, cellIndex=1 → pixel addr 0x4001
    memory.DirectWriteToZ80Memory(0x4001, 0x7E);  // pixel byte

    uint32_t paperStart = PaperStartOnLine(0);
    _z80->t = paperStart;  // phase8=4 (pixel fetch)

    EXPECT_EQ(_ula->GetFloatingBus(), 0x7E)
        << "Floating bus should return pixel byte during pixel-fetch phase";
}

TEST_F(IOContention_Test, ZX48k_FloatingBusAttributeByteInPaper)
{
    SetupZX48k();

    Memory& memory = *_context->pMemory;
    memory.DefaultBanksFor48k();

    // Attribute byte appears on the floating bus during odd phases (3, 5).
    // At paperStart+1: phase8=5, cellIndex=1 → attr addr 0x5801
    memory.DirectWriteToZ80Memory(0x5801, 0x3E);

    uint32_t paperStart = PaperStartOnLine(0);
    _z80->t = paperStart + 1;  // phase8=5 (attribute fetch)

    EXPECT_EQ(_ula->GetFloatingBus(), 0x3E)
        << "Floating bus should return attribute byte during attribute-fetch phase";
}

TEST_F(IOContention_Test, ZX48k_FloatingBusReturnsDifferentAttributePerColumn)
{
    SetupZX48k();

    Memory& memory = *_context->pMemory;
    memory.DefaultBanksFor48k();

    // Pipeline offset: ULA fetches 1 cell ahead of beam.
    // At attribute phases, test different cells across the line.
    // Cell 1 attr at phase8=5 (tInPaper=5): t = paperStart + 1
    // Cell 3 attr at phase8=5 (tInPaper=13): t = paperStart + 9
    // Cell 5 attr at phase8=5 (tInPaper=21): t = paperStart + 17
    memory.DirectWriteToZ80Memory(0x5801, 0x55);  // cell 1 attribute
    memory.DirectWriteToZ80Memory(0x5803, 0x66);  // cell 3 attribute
    memory.DirectWriteToZ80Memory(0x5805, 0x77);  // cell 5 attribute

    uint32_t paperStart = PaperStartOnLine(0);

    // Cell 1: tInPaper=5 (phase8=5, attribute), t = paperStart + 1
    _z80->t = paperStart + 1;
    EXPECT_EQ(_ula->GetFloatingBus(), 0x55);

    // Cell 3: tInPaper=13 (phase8=5, attribute), t = paperStart + 9
    _z80->t = paperStart + 9;
    EXPECT_EQ(_ula->GetFloatingBus(), 0x66);

    // Cell 5: tInPaper=21 (phase8=5, attribute), t = paperStart + 17
    _z80->t = paperStart + 17;
    EXPECT_EQ(_ula->GetFloatingBus(), 0x77);
}

TEST_F(IOContention_Test, ZX48k_FloatingBusCorrectForLine64)
{
    SetupZX48k();

    Memory& memory = *_context->pMemory;
    memory.DefaultBanksFor48k();

    // Line 64: block=(64>>6)&3=1, char_row=(64>>3)&7=0
    // Pixel addr: 0x4000 | (1<<11) | 0 | 0 | cellIndex = 0x4800 + cellIndex
    // Attr addr:  0x5800 | (1<<8) | 0 | cellIndex = 0x5900 + cellIndex
    // At paperStart+0 (phase8=4, pixel): cellIndex=1 → pixel addr 0x4801
    memory.DirectWriteToZ80Memory(0x4801, 0xC3);  // pixel byte cell 1

    uint32_t paperStart = PaperStartOnLine(64);
    _z80->t = paperStart;  // phase8=4 (pixel fetch), cellIndex=1

    EXPECT_EQ(_ula->GetFloatingBus(), 0xC3)
        << "Floating bus for line 64 should use correct interleaved pixel address";
}

TEST_F(IOContention_Test, ZX48k_FloatingBusCorrectForLine64Attribute)
{
    SetupZX48k();

    Memory& memory = *_context->pMemory;
    memory.DefaultBanksFor48k();

    // Same line 64, but attribute phase.
    // At paperStart+1 (phase8=5, attr): cellIndex=1 → attr addr 0x5901
    memory.DirectWriteToZ80Memory(0x5901, 0x2A);

    uint32_t paperStart = PaperStartOnLine(64);
    _z80->t = paperStart + 1;  // phase8=5 (attribute), cellIndex=1

    EXPECT_EQ(_ula->GetFloatingBus(), 0x2A)
        << "Floating bus for line 64 attribute should use correct interleaved address";
}

TEST_F(IOContention_Test, ZX48k_FloatingBusReturnsFFAfterLine191)
{
    SetupZX48k();

    // Line 192 is below the visible screen area
    uint32_t paperStartLine192 = PaperStartOnLine(192);
    _z80->t = paperStartLine192;

    EXPECT_EQ(_ula->GetFloatingBus(), 0xFF)
        << "Floating bus should return 0xFF for lines beyond 191";
}

/// ===================== Floating Bus: Pentagon 4T discrete pipeline ====================

TEST_F(IOContention_Test, Pentagon_FloatingBus4TDiscretePipelineAllPhases)
{
    SetupPentagon();

    Memory& memory = *_context->pMemory;
    memory.DefaultBanksFor48k();

    // Pentagon uses DISCRETE LOGIC: continuous 4T fetch, NO shift gaps.
    // Every T-state during paper has VRAM data on the bus.
    memory.DirectWriteToZ80Memory(0x4000, 0xA0);  // cell 0 pixel
    memory.DirectWriteToZ80Memory(0x5800, 0xA1);  // cell 0 attribute
    memory.DirectWriteToZ80Memory(0x4001, 0xB0);  // cell 1 pixel
    memory.DirectWriteToZ80Memory(0x5801, 0xB1);  // cell 1 attribute
    memory.DirectWriteToZ80Memory(0x4002, 0xC0);  // cell 2 pixel
    memory.DirectWriteToZ80Memory(0x5802, 0xC1);  // cell 2 attribute

    uint32_t paperStart = PaperStartOnLine(0);

    // ── Cell 0 (tInPaper 0-3) ──
    // Phase 0 (pixel): tInPaper=0
    _z80->t = paperStart - 4;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xA0)
        << "phase4=0 should return pixel byte 0xA0 of cell 0";

    // Phase 1 (pixel): tInPaper=1
    _z80->t = paperStart - 3;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xA0)
        << "phase4=1 should return pixel byte 0xA0 of cell 0";

    // Phase 2 (attribute): tInPaper=2
    _z80->t = paperStart - 2;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xA1)
        << "phase4=2 should return attribute byte 0xA1 of cell 0";

    // Phase 3 (attribute): tInPaper=3
    _z80->t = paperStart - 1;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xA1)
        << "phase4=3 should return attribute byte 0xA1 of cell 0";

    // ── Cell 1 (tInPaper 4-7) ──
    // Phase 0 (pixel): tInPaper=4
    _z80->t = paperStart;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xB0)
        << "phase4=0 of cell 1 should return pixel byte 0xB0";

    // Phase 1 (pixel): tInPaper=5
    _z80->t = paperStart + 1;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xB0)
        << "phase4=1 of cell 1 should return pixel byte 0xB0";

    // Phase 2 (attribute): tInPaper=6
    _z80->t = paperStart + 2;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xB1)
        << "phase4=2 of cell 1 should return attribute byte 0xB1";

    // Phase 3 (attribute): tInPaper=7
    _z80->t = paperStart + 3;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xB1)
        << "phase4=3 of cell 1 should return attribute byte 0xB1";

    // ── Cell 2 (tInPaper 8-11) ──
    // Verify continuous behavior — no 0xFF shift gaps
    _z80->t = paperStart + 4;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xC0)
        << "phase4=0 of cell 2 should return pixel byte 0xC0";

    _z80->t = paperStart + 6;
    EXPECT_EQ(_ula->GetFloatingBus(), 0xC1)
        << "phase4=2 of cell 2 should return attribute byte 0xC1";
}

TEST_F(IOContention_Test, Pentagon_FloatingBusNeverReturnsFFDuringPaper)
{
    SetupPentagon();

    Memory& memory = *_context->pMemory;
    memory.DefaultBanksFor48k();

    // Pentagon discrete logic: ALL 8 phases in the first 2 cells return data.
    // No 0xFF during paper area (unlike Ferranti ULA which has shift gaps).
    memory.DirectWriteToZ80Memory(0x4000, 0xAA);
    memory.DirectWriteToZ80Memory(0x5800, 0xBB);
    memory.DirectWriteToZ80Memory(0x4001, 0xCC);
    memory.DirectWriteToZ80Memory(0x5801, 0xDD);

    uint32_t paperStart = PaperStartOnLine(0);

    // Check all 8 T-states across 2 cells — none should be 0xFF
    for (uint32_t i = 0; i < 8; i++)
    {
        _z80->t = paperStart - 4 + i;
        uint8_t val = _ula->GetFloatingBus();
        EXPECT_NE(val, 0xFF)
            << "Pentagon floating bus must not return 0xFF during paper at offset " << i;
    }
}

/// ===================== Floating Bus: ZX-128K ====================

TEST_F(IOContention_Test, ZX128k_FloatingBusWorks)
{
    SetupZX128k();
    _context->config.floatbus = 1;  // Enable for test

    Memory& memory = *_context->pMemory;
    memory.DefaultBanksFor48k();  // 128K test uses same VRAM layout

    // At paperStart+0 (phase8=4, pixel): cellIndex=1 → pixel addr 0x4001
    memory.DirectWriteToZ80Memory(0x4001, 0x7E);

    uint32_t paperStart = PaperStartOnLine(0);
    _z80->t = paperStart;

    EXPECT_EQ(_ula->GetFloatingBus(), 0x7E)
        << "ZX-128K floating bus should work with same 8T pipeline";
}
