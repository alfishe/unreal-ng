/// @file screendigest_test.cpp
/// @brief Unit tests for the ScreenDigest FNV-1a engine (M7f / M9).
///
/// Uses MemoryCUT with a 48K bank layout (screen 0 = physical RAM page 5,
/// mapped at 0x4000) and checks the digest oracle against a manual FNV-1a
/// fold over the very same bytes: full-page coverage, determinism, change
/// detection for screen writes (first and last byte of the page), page
/// independence, the Z80-range variant, and the border MixValue step.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screendigest.h"

class ScreenDigest_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _memory = new MemoryCUT(_context);
        // 48K layout: Z80 0x4000-0x7FFF maps physical RAM page 5 (screen 0)
        _memory->DefaultBanksFor48k();
    }

    void TearDown() override
    {
        delete _memory;
        delete _context;
    }

    /// Manual FNV-1a 64 fold — the oracle DigestRAMPage/DigestZ80Range must match
    static uint64_t ManualFold(const uint8_t* bytes, size_t count)
    {
        uint64_t hash = ScreenDigest::kInitialValue;
        for (size_t i = 0; i < count; i++)
        {
            hash ^= bytes[i];
            hash *= ScreenDigest::kPrime;
        }
        return hash;
    }

    EmulatorContext* _context = nullptr;
    MemoryCUT* _memory = nullptr;
};

TEST_F(ScreenDigest_Test, DigestRAMPage_MatchesManualFoldOverFullPage)
{
    const uint8_t* page = _memory->RAMPageAddress(ScreenDigest::kScreen0RAMPage);
    ASSERT_NE(page, nullptr);

    EXPECT_EQ(ScreenDigest::DigestRAMPage(_memory, ScreenDigest::kScreen0RAMPage),
              ManualFold(page, ScreenDigest::kRAMPageSize));
}

TEST_F(ScreenDigest_Test, DigestRAMPage_IsDeterministic)
{
    uint64_t first = ScreenDigest::DigestRAMPage(_memory, ScreenDigest::kScreen0RAMPage);
    uint64_t second = ScreenDigest::DigestRAMPage(_memory, ScreenDigest::kScreen0RAMPage);
    EXPECT_EQ(first, second);
}

TEST_F(ScreenDigest_Test, DigestRAMPage_ChangesOnScreenWrite)
{
    uint64_t before = ScreenDigest::DigestRAMPage(_memory, ScreenDigest::kScreen0RAMPage);
    uint64_t shadowBefore = ScreenDigest::DigestRAMPage(_memory, ScreenDigest::kScreen1RAMPage);

    _memory->DirectWriteToZ80Memory(0x4000, 0xFF); // first byte of screen 0

    EXPECT_NE(ScreenDigest::DigestRAMPage(_memory, ScreenDigest::kScreen0RAMPage), before);
    // Physical pages are independent: page 7 did not move
    EXPECT_EQ(ScreenDigest::DigestRAMPage(_memory, ScreenDigest::kScreen1RAMPage), shadowBefore);
}

TEST_F(ScreenDigest_Test, DigestRAMPage_CoversLastByteOfPage)
{
    uint64_t before = ScreenDigest::DigestRAMPage(_memory, ScreenDigest::kScreen0RAMPage);

    _memory->DirectWriteToZ80Memory(0x4000 + ScreenDigest::kRAMPageSize - 1, 0x01);

    EXPECT_NE(ScreenDigest::DigestRAMPage(_memory, ScreenDigest::kScreen0RAMPage), before);
}

TEST_F(ScreenDigest_Test, DigestRAMPage_PagesWithDifferentContentsDiffer)
{
    // Write through the physical page pointers so paging state cannot matter
    uint8_t* page0 = _memory->RAMPageAddress(ScreenDigest::kScreen0RAMPage);
    uint8_t* page1 = _memory->RAMPageAddress(ScreenDigest::kScreen1RAMPage);
    ASSERT_NE(page0, nullptr);
    ASSERT_NE(page1, nullptr);
    page0[0] = 0x00;
    page1[0] = 0x01;

    EXPECT_NE(ScreenDigest::DigestRAMPage(_memory, ScreenDigest::kScreen0RAMPage),
              ScreenDigest::DigestRAMPage(_memory, ScreenDigest::kScreen1RAMPage));
}

TEST_F(ScreenDigest_Test, DigestZ80Range_MatchesManualFold)
{
    for (uint16_t i = 0; i < 16; i++)
    {
        _memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x4000 + i), static_cast<uint8_t>(i));
    }

    const uint8_t* page = _memory->RAMPageAddress(ScreenDigest::kScreen0RAMPage);
    EXPECT_EQ(ScreenDigest::DigestZ80Range(_memory, 0x4000, 0x400F), ManualFold(page, 16));
}

TEST_F(ScreenDigest_Test, DigestZ80Range_SingleByteIsOneMixStep)
{
    _memory->DirectWriteToZ80Memory(0x4000, 0xAB);

    EXPECT_EQ(ScreenDigest::DigestZ80Range(_memory, 0x4000, 0x4000),
              ScreenDigest::MixValue(ScreenDigest::kInitialValue, 0xAB));
}

TEST_F(ScreenDigest_Test, MixValue_XorsThenMultipliesByPrime)
{
    constexpr uint64_t kDigest = 0x1122334455667788ull;
    EXPECT_EQ(ScreenDigest::MixValue(kDigest, 0xFE), (kDigest ^ 0xFEull) * ScreenDigest::kPrime);
}
