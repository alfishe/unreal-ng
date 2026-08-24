#include "pch.h"
#include "stdafx.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"
#include "emulator/video/ulacontention.h"
#include "emulator/video/zx/screenzx.h"

/// Test fixture for ULA memory contention tests
class MemoryContention_Test : public ::testing::Test
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
        ASSERT_TRUE(_cpu->Init());
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
        // Detach screen from context so Core::Release doesn't double-delete
        if (_context)
            _context->pScreen = nullptr;

        delete _cpu;  // Core::Release will not delete screen since pScreen is null
        delete _context;
    }

    /// Set up ZX-48K timing model with correct config values
    void SetupZX48k()
    {
        CONFIG& config = _context->config;
        config.frame = 69888;
        config.t_line = 224;
        config.mem_model = MM_SPECTRUM48;

        _screen->SetVideoMode(M_ZX48);
    }

    /// Set up ZX-128K timing model
    void SetupZX128k()
    {
        CONFIG& config = _context->config;
        config.frame = 70908;
        config.t_line = 228;
        config.mem_model = MM_SPECTRUM128;

        _screen->SetVideoMode(M_ZX128);
    }

    /// Set up Pentagon timing model (no contention)
    void SetupPentagon()
    {
        CONFIG& config = _context->config;
        config.frame = 71680;
        config.t_line = 224;
        config.mem_model = MM_PENTAGON;

        _screen->SetVideoMode(M_PENTAGON128K);
    }

    /// Calculate the first t-state of paper on a given scanline
    /// screenAreaStart includes vSync + vBlank + screenOffsetTop (top border)
    /// Paper within line starts after hSync + hBlank + leftBorder prefix
    uint32_t PaperStartOnLine(int line)
    {
        const RasterDescriptor& rd = _screen->rasterDescriptors[_screen->GetVideoMode()];
        uint32_t tstatesPerLine = rd.pixelsPerLine / 2;
        // screenAreaStart = vsync + vblank + top border
        uint32_t screenAreaStart = tstatesPerLine * (rd.vSyncLines + rd.vBlankLines + rd.screenOffsetTop);
        // Paper prefix within each line: hSync + hBlank + left border
        uint32_t linePrefix = (rd.hSyncPixels + rd.hBlankPixels + rd.screenOffsetLeft) / 2;
        return screenAreaStart + linePrefix + (uint32_t)line * tstatesPerLine;
    }
};

/// =========== Contention Enabled / Disabled by Model ===========

TEST_F(MemoryContention_Test, Pentagon_NoContention)
{
    SetupPentagon();

    // Pentagon should never have contention, regardless of t-state position
    for (uint32_t t = 0; t < 71680; t += 1000)
    {
        _z80->t = t;
        EXPECT_EQ(_ula->GetContentionDelay(), 0)
            << "Pentagon should have no contention at t=" << t;
    }
}

TEST_F(MemoryContention_Test, ZX48k_ContentionEnabled)
{
    SetupZX48k();

    // Verify contention is enabled for ZX-48K by checking a t-state in the paper area
    uint32_t paperStart = PaperStartOnLine(0);
    _z80->t = paperStart;
    uint8_t delay = _ula->GetContentionDelay();

    // At offset 0 in the cell, delay should be 6 (the maximum)
    EXPECT_EQ(delay, 6);
}

TEST_F(MemoryContention_Test, ZX128k_ContentionEnabled)
{
    SetupZX128k();

    uint32_t paperStart = PaperStartOnLine(0);
    _z80->t = paperStart;
    uint8_t delay = _ula->GetContentionDelay();

    EXPECT_EQ(delay, 6);
}

/// =========== Contention Pattern Tests ===========

TEST_F(MemoryContention_Test, ZX48k_ContentionPatternMatchesULA)
{
    SetupZX48k();

    // The standard ZX contention pattern is {6,5,4,3,2,1,0,0}
    // repeating every 8 t-states within the paper area
    static const uint8_t expectedPattern[8] = {6, 5, 4, 3, 2, 1, 0, 0};

    uint32_t paperStart = PaperStartOnLine(10);  // Line 10

    for (int i = 0; i < 8; i++)
    {
        _z80->t = paperStart + i;
        uint8_t delay = _ula->GetContentionDelay();
        EXPECT_EQ(delay, expectedPattern[i])
            << "Contention pattern mismatch at cell offset " << i;
    }
}

TEST_F(MemoryContention_Test, ZX48k_ContentionPatternRepeats)
{
    SetupZX48k();

    // Pattern should repeat every 8 t-states
    static const uint8_t expectedPattern[8] = {6, 5, 4, 3, 2, 1, 0, 0};

    uint32_t paperStart = PaperStartOnLine(5);

    // Check pattern at offsets 0-7, then 8-15, then 16-23
    for (int cell = 0; cell < 3; cell++)
    {
        for (int i = 0; i < 8; i++)
        {
            _z80->t = paperStart + cell * 8 + i;
            uint8_t delay = _ula->GetContentionDelay();
            EXPECT_EQ(delay, expectedPattern[i])
                << "Pattern repeat failure at cell " << cell << " offset " << i;
        }
    }
}

TEST_F(MemoryContention_Test, ZX48k_ContentionNoDelayAtCellOffset6and7)
{
    SetupZX48k();

    // The last 2 t-states of each 8-t-state cell have zero delay
    uint32_t paperStart = PaperStartOnLine(50);

    _z80->t = paperStart + 6;
    EXPECT_EQ(_ula->GetContentionDelay(), 0);

    _z80->t = paperStart + 7;
    EXPECT_EQ(_ula->GetContentionDelay(), 0);
}

/// =========== Contention Outside Paper Area ===========

TEST_F(MemoryContention_Test, ZX48k_NoContentionInBlankArea)
{
    SetupZX48k();

    // t-state 0 is in the blank/vsync area, well before screen
    _z80->t = 0;
    EXPECT_EQ(_ula->GetContentionDelay(), 0);

    _z80->t = 100;
    EXPECT_EQ(_ula->GetContentionDelay(), 0);
}

TEST_F(MemoryContention_Test, ZX48k_NoContentionInTopBorder)
{
    SetupZX48k();

    const RasterDescriptor& rd = _screen->rasterDescriptors[_screen->GetVideoMode()];
    uint32_t tstatesPerLine = rd.pixelsPerLine / 2;
    uint32_t screenAreaStart = tstatesPerLine * (rd.vSyncLines + rd.vBlankLines);
    uint32_t topBorderStart = screenAreaStart - tstatesPerLine;  // Last line of top border

    _z80->t = topBorderStart;
    EXPECT_EQ(_ula->GetContentionDelay(), 0);
}

TEST_F(MemoryContention_Test, ZX48k_NoContentionInLeftBorder)
{
    SetupZX48k();

    // Access during screen area but in left border (before paper starts)
    uint32_t paperStart = PaperStartOnLine(10);
    _z80->t = paperStart - 1;  // Just before paper

    EXPECT_EQ(_ula->GetContentionDelay(), 0);
}

TEST_F(MemoryContention_Test, ZX48k_NoContentionInRightBorder)
{
    SetupZX48k();

    // Access during screen area but in right border (after paper ends)
    const RasterDescriptor& rd = _screen->rasterDescriptors[_screen->GetVideoMode()];
    uint32_t paperWidthTStates = rd.screenWidth / 2;  // 256 / 2 = 128 t-states

    uint32_t paperStart = PaperStartOnLine(10);
    uint32_t paperEnd = paperStart + paperWidthTStates;

    _z80->t = paperEnd;
    EXPECT_EQ(_ula->GetContentionDelay(), 0);
}

TEST_F(MemoryContention_Test, ZX48k_NoContentionInBottomBorder)
{
    SetupZX48k();

    const RasterDescriptor& rd = _screen->rasterDescriptors[_screen->GetVideoMode()];
    uint32_t tstatesPerLine = rd.pixelsPerLine / 2;
    uint32_t screenAreaEnd = tstatesPerLine * (rd.vSyncLines + rd.vBlankLines + rd.screenHeight);

    // First line of bottom border
    _z80->t = screenAreaEnd + 1;
    EXPECT_EQ(_ula->GetContentionDelay(), 0);
}

TEST_F(MemoryContention_Test, ZX48k_NoContentionInBottomBorderFarFromScreen)
{
    SetupZX48k();

    // Very end of frame, deep in bottom border
    _z80->t = 69000;
    EXPECT_EQ(_ula->GetContentionDelay(), 0);
}

/// =========== Contention across multiple lines ===========

TEST_F(MemoryContention_Test, ZX48k_ContentionConsistentAcrossLines)
{
    SetupZX48k();

    // The pattern should be identical on every scanline within the paper area
    static const uint8_t expectedPattern[8] = {6, 5, 4, 3, 2, 1, 0, 0};

    for (int line = 0; line < 192; line += 32)  // Sample lines
    {
        uint32_t paperStart = PaperStartOnLine(line);
        for (int i = 0; i < 8; i++)
        {
            _z80->t = paperStart + i;
            EXPECT_EQ(_ula->GetContentionDelay(), expectedPattern[i])
                << "Line " << line << " offset " << i;
        }
    }
}

/// =========== rd()/wd() integration with contention ===========

TEST_F(MemoryContention_Test, ZX48k_rdAddsContentionDelay)
{
    SetupZX48k();

    // Set up default memory banks for 48K
    _cpu->GetMemory()->DefaultBanksFor48k();

    uint32_t paperStart = PaperStartOnLine(10);
    _z80->t = paperStart;  // Cell offset 0, contention = 6

    uint32_t tBefore = _z80->t;

    // Read from contended address 0x4000
    _z80->rd(0x4000, false);

    // t should have advanced by: contention(6) + memory_read(3) = 9
    uint32_t tAfter = _z80->t;
    uint32_t advance = tAfter - tBefore;
    EXPECT_EQ(advance, 9u);
}

TEST_F(MemoryContention_Test, ZX48k_rdNoContentionForExecutionFetch)
{
    SetupZX48k();

    _cpu->GetMemory()->DefaultBanksFor48k();

    uint32_t paperStart = PaperStartOnLine(10);
    _z80->t = paperStart;

    uint32_t tBefore = _z80->t;

    // Read from contended address 0x4000 as execution fetch
    // Execution fetch should NOT trigger contention
    _z80->rd(0x4000, true);

    uint32_t tAfter = _z80->t;
    uint32_t advance = tAfter - tBefore;

    // Only memory read cost (3), no contention delay
    EXPECT_EQ(advance, 3u);
}

TEST_F(MemoryContention_Test, ZX48k_rdNoContentionForUncontendedAddress)
{
    SetupZX48k();

    _cpu->GetMemory()->DefaultBanksFor48k();

    uint32_t paperStart = PaperStartOnLine(10);
    _z80->t = paperStart;

    uint32_t tBefore = _z80->t;

    // Read from uncontended address 0x8000 (outside contended range)
    _z80->rd(0x8000, false);

    uint32_t tAfter = _z80->t;
    uint32_t advance = tAfter - tBefore;

    // Only memory read cost (3), no contention for address >= 0x8000
    EXPECT_EQ(advance, 3u);
}

TEST_F(MemoryContention_Test, ZX48k_rdNoContentionOutsidePaperArea)
{
    SetupZX48k();

    _cpu->GetMemory()->DefaultBanksFor48k();

    // Set t-state to blank area (no contention)
    _z80->t = 0;

    uint32_t tBefore = _z80->t;

    // Read from contended address during non-paper time
    _z80->rd(0x4000, false);

    uint32_t tAfter = _z80->t;
    uint32_t advance = tAfter - tBefore;

    // Only memory read cost (3), no contention outside paper
    EXPECT_EQ(advance, 3u);
}

TEST_F(MemoryContention_Test, ZX48k_wdAddsContentionDelay)
{
    SetupZX48k();

    _cpu->GetMemory()->DefaultBanksFor48k();

    uint32_t paperStart = PaperStartOnLine(20);
    _z80->t = paperStart + 1;  // Cell offset 1, contention = 5

    uint32_t tBefore = _z80->t;

    // Write to contended address 0x4000
    _z80->wd(0x4000, 0xAA);

    uint32_t tAfter = _z80->t;
    uint32_t advance = tAfter - tBefore;

    // t should have advanced by: contention(5) + memory_write(3) = 8
    EXPECT_EQ(advance, 8u);
}

TEST_F(MemoryContention_Test, ZX48k_wdNoContentionForUncontendedAddress)
{
    SetupZX48k();

    _cpu->GetMemory()->DefaultBanksFor48k();

    uint32_t paperStart = PaperStartOnLine(20);
    _z80->t = paperStart;

    uint32_t tBefore = _z80->t;

    // Write to uncontended address 0x8000
    _z80->wd(0x8000, 0xBB);

    uint32_t tAfter = _z80->t;
    uint32_t advance = tAfter - tBefore;

    EXPECT_EQ(advance, 3u);
}

TEST_F(MemoryContention_Test, Pentagon_rdNoContention)
{
    SetupPentagon();

    _cpu->GetMemory()->DefaultBanksFor48k();

    // Even during "paper area" equivalent, Pentagon has no contention
    _z80->t = 10000;

    uint32_t tBefore = _z80->t;

    _z80->rd(0x4000, false);

    uint32_t advance = _z80->t - tBefore;

    // Only memory read cost (3), no contention for Pentagon
    EXPECT_EQ(advance, 3u);
}

TEST_F(MemoryContention_Test, Pentagon_wdNoContention)
{
    SetupPentagon();

    _cpu->GetMemory()->DefaultBanksFor48k();

    _z80->t = 10000;

    uint32_t tBefore = _z80->t;

    _z80->wd(0x4000, 0x42);

    uint32_t advance = _z80->t - tBefore;

    EXPECT_EQ(advance, 3u);
}

/// =========== Contended address boundary tests ===========

TEST_F(MemoryContention_Test, ZX48k_rdContentionAt0x4000Boundary)
{
    SetupZX48k();

    _cpu->GetMemory()->DefaultBanksFor48k();

    uint32_t paperStart = PaperStartOnLine(10);
    _z80->t = paperStart;

    // 0x4000 is the first contended address
    uint32_t tBefore = _z80->t;
    _z80->rd(0x4000, false);
    EXPECT_GT(_z80->t - tBefore, 3u);  // Should have contention
}

TEST_F(MemoryContention_Test, ZX48k_rdContentionAt0x7FFFBoundary)
{
    SetupZX48k();

    _cpu->GetMemory()->DefaultBanksFor48k();

    uint32_t paperStart = PaperStartOnLine(10);
    _z80->t = paperStart;

    // 0x7FFF is the last contended address
    uint32_t tBefore = _z80->t;
    _z80->rd(0x7FFF, false);
    EXPECT_GT(_z80->t - tBefore, 3u);  // Should have contention
}

TEST_F(MemoryContention_Test, ZX48k_rdNoContentionAt0x3FFF)
{
    SetupZX48k();

    _cpu->GetMemory()->DefaultBanksFor48k();

    uint32_t paperStart = PaperStartOnLine(10);
    _z80->t = paperStart;

    // 0x3FFF is below contended range
    uint32_t tBefore = _z80->t;
    _z80->rd(0x3FFF, false);
    EXPECT_EQ(_z80->t - tBefore, 3u);  // No contention
}

TEST_F(MemoryContention_Test, ZX48k_rdNoContentionAt0x8000)
{
    SetupZX48k();

    _cpu->GetMemory()->DefaultBanksFor48k();

    uint32_t paperStart = PaperStartOnLine(10);
    _z80->t = paperStart;

    // 0x8000 is above contended range
    uint32_t tBefore = _z80->t;
    _z80->rd(0x8000, false);
    EXPECT_EQ(_z80->t - tBefore, 3u);  // No contention
}
