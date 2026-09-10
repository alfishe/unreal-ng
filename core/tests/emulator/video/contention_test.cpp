#include "stdafx.h"
#include "pch.h"

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
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
/// (70908 T/frame) - patterns are anchored via the contention engine's own
/// raster snapshot so the tests track the emulator's raster calibration.

namespace
{
constexpr uint8_t kPattern[8] = {6, 5, 4, 3, 2, 1, 0, 0};
}

/// Shared fixture logic; model injected by subclasses
class ContentionTestBase : public ::testing::Test
{
protected:
    const char* _modelName = "48K";

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

        const ContentionRaster& raster = _ula->GetRaster();
        _firstContendedT = raster.screenAreaStart + raster.screenLineAreaStart;
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
};

class Contention48K_Test : public ContentionTestBase
{
protected:
    Contention48K_Test() { _modelName = "48K"; }
};

class Contention128K_Test : public ContentionTestBase
{
protected:
    Contention128K_Test() { _modelName = "128K"; }
};

/// region <ZX-Spectrum 48K>

TEST_F(Contention48K_Test, MachineGeometry)
{
    const ContentionRaster& raster = _ula->GetRaster();
    EXPECT_EQ(raster.tstatesPerLine, 224u) << "48K: 224 T-states per line";
    EXPECT_EQ(raster.configFrameDuration, 69888u) << "48K: 224 * 312 lines";
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
    uint32_t line100 = raster.screenAreaStart + 100 * raster.tstatesPerLine + raster.screenLineAreaStart;
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

/// endregion </ZX-Spectrum 128K>

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
