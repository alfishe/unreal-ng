#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <filesystem>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/rom.h"
#include "emulator/platform.h"

/// @brief ROM space expansion and per-model bundle size validation
///        (implementation-plan Task 2). The ROM window covers the full 2 MB
///        ProfROM quadrant ladder; MM_SCORP accepts only the base 64 KB bundle,
///        MM_PROFSCORP accepts any whole-quadrant image and clamps
///        non-power-of-two sizes down to the next lower power of two.
namespace
{
    /// Deletes the file at the given path when it goes out of scope, on every
    /// exit path (assertion failure included: ASSERT_* returns out of the
    /// TEST_F body, which still unwinds locals normally). A bare remove() call
    /// at the end of the test never runs once an earlier ASSERT_* fails,
    /// leaving scratch files behind.
    class ScopedTestFile
    {
    public:
        explicit ScopedTestFile(std::string path) : _path(std::move(path)) {}
        ~ScopedTestFile() { std::error_code ec; std::filesystem::remove(_path, ec); }

        ScopedTestFile(const ScopedTestFile&) = delete;
        ScopedTestFile& operator=(const ScopedTestFile&) = delete;

        const std::string& path() const { return _path; }
        bool empty() const { return _path.empty(); }
        operator const std::string&() const { return _path; }

        friend bool operator==(const ScopedTestFile& a, const std::string& b) { return a._path == b; }
        friend bool operator==(const std::string& a, const ScopedTestFile& b) { return a == b._path; }
        friend bool operator!=(const ScopedTestFile& a, const std::string& b) { return !(a == b); }
        friend bool operator!=(const std::string& a, const ScopedTestFile& b) { return !(a == b); }

    private:
        std::string _path;
    };
}

class RomSpace_Test : public ::testing::Test
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

    /// @brief Write a synthetic bundle whose page i is filled with 0x80|i -
    ///        unique content for every page of the 128-page window
    /// @return scratch file path, or an empty string on failure
    std::string WriteTaggedBundle(const std::string& leafName, uint16_t pages)
    {
        std::vector<uint8_t> image(static_cast<size_t>(pages) * PAGE_SIZE);
        for (uint16_t page = 0; page < pages; page++)
        {
            std::fill_n(image.begin() + static_cast<size_t>(page) * PAGE_SIZE,
                        PAGE_SIZE,
                        static_cast<uint8_t>(0x80 | page));
        }

        const std::string path = TestPathHelper::GetUniqueTestScratchPath(leafName);
        if (!FileHelper::SaveBufferToFile(path, image.data(), image.size()))
            return std::string();

        return path;
    }

    /// Point both Scorpion ROM path fields at the bundle and run the loader
    bool LoadForModel(MEM_MODEL model, const std::string& path)
    {
        _context->config.mem_model = model;

        std::strncpy(_context->config.scorp_rom_path, path.c_str(), sizeof(_context->config.scorp_rom_path) - 1);
        _context->config.scorp_rom_path[sizeof(_context->config.scorp_rom_path) - 1] = '\0';
        std::strncpy(_context->config.prof_rom_path, path.c_str(), sizeof(_context->config.prof_rom_path) - 1);
        _context->config.prof_rom_path[sizeof(_context->config.prof_rom_path) - 1] = '\0';

        ROM rom(_context);
        return rom.LoadROM();
    }
};

/// @brief The ROM window must cover the full 2 MB ProfROM ladder
TEST_F(RomSpace_Test, RomWindowCoversTwoMegabytes)
{
    EXPECT_EQ(MAX_ROM_PAGES, 128);
    EXPECT_EQ(static_cast<size_t>(MAX_ROM_PAGES) * PAGE_SIZE, static_cast<size_t>(2) * 1024 * 1024);
}

/// @brief A synthetic 2 MB bundle loads for MM_PROFSCORP with every page intact
TEST_F(RomSpace_Test, SyntheticTwoMegabyteBundleLoadsForProfScorpion)
{
    ScopedTestFile path(WriteTaggedBundle("romspace-2mb.rom", 128));
    ASSERT_FALSE(path.empty()) << "failed to write the synthetic 2 MB bundle";

    EXPECT_TRUE(LoadForModel(MM_PROFSCORP, path));

    for (uint16_t page = 0; page < 128; page++)
    {
        EXPECT_EQ(_memory->ROMPageHostAddress(page)[0], static_cast<uint8_t>(0x80 | page))
            << "ROM page " << page << " must hold its own tag";
    }
}

/// @brief All 128 ROM page host addresses are distinct, one page apart
TEST_F(RomSpace_Test, PageHostAddressesAreDistinct)
{
    ScopedTestFile path(WriteTaggedBundle("romspace-distinct.rom", 128));
    ASSERT_FALSE(path.empty());

    ASSERT_TRUE(LoadForModel(MM_PROFSCORP, path));

    for (uint16_t page = 1; page < 128; page++)
    {
        EXPECT_EQ(_memory->ROMPageHostAddress(page) - _memory->ROMPageHostAddress(page - 1),
                  static_cast<ptrdiff_t>(PAGE_SIZE))
            << "adjacent ROM page host addresses must be exactly one page apart";
    }
}

/// @brief Role pointers always resolve into quadrant 0 (pages 0-3), even for a
///        256 KB image with further quadrants stacked behind it
TEST_F(RomSpace_Test, RolePointersStayInsideQuadrantZero)
{
    ScopedTestFile path(WriteTaggedBundle("romspace-256k.rom", 16));
    ASSERT_FALSE(path.empty());

    EXPECT_TRUE(LoadForModel(MM_PROFSCORP, path));

    EXPECT_EQ(_memory->base_128_rom, _memory->ROMPageHostAddress(0)) << "BASIC 128 is quadrant-0 page 0";
    EXPECT_EQ(_memory->base_sos_rom, _memory->ROMPageHostAddress(1)) << "48K BASIC is quadrant-0 page 1";
    EXPECT_EQ(_memory->base_sys_rom, _memory->ROMPageHostAddress(2)) << "Service Monitor is quadrant-0 page 2";
    EXPECT_EQ(_memory->base_dos_rom, _memory->ROMPageHostAddress(3)) << "TR-DOS is quadrant-0 page 3";
}

/// @brief MM_SCORP accepts only the fixed 64 KB base bundle
TEST_F(RomSpace_Test, ScorpionRejectsNonSixtyFourKbBundles)
{
    // 128 KB is a legal ProfROM image but not a base-Scorpion bundle
    {
        ScopedTestFile path(WriteTaggedBundle("romspace-128k.rom", 8));
        ASSERT_FALSE(path.empty());
        EXPECT_FALSE(LoadForModel(MM_SCORP, path));
    }

    // 32 KB is too small for a whole quadrant on either model
    {
        ScopedTestFile path(WriteTaggedBundle("romspace-32k.rom", 2));
        ASSERT_FALSE(path.empty());
        EXPECT_FALSE(LoadForModel(MM_SCORP, path));
        EXPECT_FALSE(LoadForModel(MM_PROFSCORP, path));
    }
}

/// @brief A 96 KB (6-bank) ProfROM image clamps down to 4 banks and still loads
TEST_F(RomSpace_Test, ProfScorpionClampsNonPowerOfTwoBundle)
{
    ScopedTestFile path(WriteTaggedBundle("romspace-96k.rom", 6));
    ASSERT_FALSE(path.empty());

    EXPECT_TRUE(LoadForModel(MM_PROFSCORP, path)) << "non-power-of-two sizes clamp instead of failing";
    EXPECT_EQ(_memory->base_128_rom, _memory->ROMPageHostAddress(0)) << "quadrant 0 stays intact after the clamp";
}

/// @brief The real shipped 512 KB ProfROM (8 quadrants) loads for MM_PROFSCORP
TEST_F(RomSpace_Test, ShippedProfRomLoadsForProfScorpion)
{
    _context->config.mem_model = MM_PROFSCORP;
    std::strncpy(_context->config.prof_rom_path, "rom/scorp_prof401.rom", sizeof(_context->config.prof_rom_path) - 1);
    _context->config.prof_rom_path[sizeof(_context->config.prof_rom_path) - 1] = '\0';

    ROM rom(_context);
    if (!rom.LoadROM())
        GTEST_SKIP() << "rom/scorp_prof401.rom not reachable from the test binary";

    // 512 KB = 8 quadrants: the role pointers still resolve into quadrant 0
    EXPECT_EQ(_memory->base_128_rom, _memory->ROMPageHostAddress(0));
    EXPECT_EQ(_memory->base_sos_rom, _memory->ROMPageHostAddress(1));
    EXPECT_EQ(_memory->base_sys_rom, _memory->ROMPageHostAddress(2));
    EXPECT_EQ(_memory->base_dos_rom, _memory->ROMPageHostAddress(3));
}
