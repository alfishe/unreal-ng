#include "stdafx.h"
#include "pch.h"

#include <cstring>

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/rom.h"

/// @brief Regression guard for the Scorpion ZS-256 / ProfROM logical ROM page mapping.
///
/// Every shipped Scorpion bundle stores its four 16 KB pages in the order
/// BASIC 128 / 48K BASIC / Shadow Service Monitor / TR-DOS. The loader used to assign
/// the role pointers Service-first, which scrambled all four roles and left #0000
/// resolving to the wrong ROM on every boot. These tests pin both halves of the
/// contract: the page order actually present in the image, and the pointer assignment
/// derived from it.
class ScorpionRomMapping_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _memory = new Memory(_context);
        _context->pMemory = _memory;
    }

    void TearDown() override
    {
        if (_context != nullptr)
            _context->pMemory = nullptr;

        delete _memory;
        _memory = nullptr;

        delete _context;
        _context = nullptr;
    }

    /// @brief Load the shipped 64 KB bundle for the given model.
    /// @return false when the ROM file is not reachable from the test binary
    bool LoadScorpionBundle(MEM_MODEL model, const char* romPath)
    {
        _context->config.mem_model = model;
        std::strncpy(_context->config.scorp_rom_path, romPath, sizeof(_context->config.scorp_rom_path) - 1);
        std::strncpy(_context->config.prof_rom_path, romPath, sizeof(_context->config.prof_rom_path) - 1);

        ROM rom(_context);
        return rom.LoadROM();
    }
};

/// The four role pointers must land on pages 0..3 in the verified order.
TEST_F(ScorpionRomMapping_Test, ScorpionRolePointersFollowBundleOrder)
{
    if (!LoadScorpionBundle(MM_SCORP, "rom/scorpion.rom"))
        GTEST_SKIP() << "rom/scorpion.rom not reachable from the test binary";

    EXPECT_EQ(_memory->base_128_rom, _memory->ROMPageHostAddress(0)) << "page 0 is Scorpion BASIC 128";
    EXPECT_EQ(_memory->base_sos_rom, _memory->ROMPageHostAddress(1)) << "page 1 is 48K BASIC";
    EXPECT_EQ(_memory->base_sys_rom, _memory->ROMPageHostAddress(2)) << "page 2 is the Shadow Service Monitor";
    EXPECT_EQ(_memory->base_dos_rom, _memory->ROMPageHostAddress(3)) << "page 3 is TR-DOS";
}

/// ProfROM shares the layout - quadrant 0 repeats the same 4-page set.
TEST_F(ScorpionRomMapping_Test, ProfScorpionRolePointersFollowBundleOrder)
{
    if (!LoadScorpionBundle(MM_PROFSCORP, "rom/scorpion.rom"))
        GTEST_SKIP() << "rom/scorpion.rom not reachable from the test binary";

    EXPECT_EQ(_memory->base_128_rom, _memory->ROMPageHostAddress(0));
    EXPECT_EQ(_memory->base_sos_rom, _memory->ROMPageHostAddress(1));
    EXPECT_EQ(_memory->base_sys_rom, _memory->ROMPageHostAddress(2));
    EXPECT_EQ(_memory->base_dos_rom, _memory->ROMPageHostAddress(3));
}

/// The mapping above is only correct while the image really is in this order, so pin
/// the page contents too. A different bundle would otherwise silently re-scramble it.
TEST_F(ScorpionRomMapping_Test, ShippedBundlePageContentsIdentifyTheirRoles)
{
    if (!LoadScorpionBundle(MM_SCORP, "rom/scorpion.rom"))
        GTEST_SKIP() << "rom/scorpion.rom not reachable from the test binary";

    const uint8_t* basic128 = _memory->ROMPageHostAddress(0);
    const uint8_t* basic48 = _memory->ROMPageHostAddress(1);
    const uint8_t* service = _memory->ROMPageHostAddress(2);
    const uint8_t* trdos = _memory->ROMPageHostAddress(3);

    // Page 0 - Scorpion BASIC 128: DI / JP reset vector plus the boot banner
    EXPECT_EQ(basic128[0], 0xF3) << "BASIC 128 starts with DI";
    EXPECT_EQ(basic128[1], 0xC3) << "BASIC 128 continues with JP";

    // Page 1 - 48K BASIC: the classic F3 AF 11 FF FF entry sequence
    EXPECT_EQ(basic48[0], 0xF3);
    EXPECT_EQ(basic48[1], 0xAF);
    EXPECT_EQ(basic48[2], 0x11);
    EXPECT_EQ(basic48[3], 0xFF);
    EXPECT_EQ(basic48[4], 0xFF);

    // Page 2 - Shadow Service Monitor: entered at #0066 by the MNI button, so it has
    // no reset vector of its own (this is what distinguishes it from the BASIC pages)
    EXPECT_NE(service[0], 0xF3) << "the Service Monitor has no DI reset vector";

    // Page 3 - TR-DOS: version banner is present in the page
    const char* kTrdosBanner = "TR-DOS";
    bool trdosBannerFound = false;
    for (size_t offset = 0; offset + 6 <= PAGE_SIZE && !trdosBannerFound; offset++)
    {
        trdosBannerFound = std::memcmp(trdos + offset, kTrdosBanner, 6) == 0;
    }
    EXPECT_TRUE(trdosBannerFound) << "page 3 must contain the TR-DOS banner";
}
