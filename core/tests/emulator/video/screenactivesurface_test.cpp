/// @file screenactivesurface_test.cpp
/// @brief Unit tests for Screen::GetActiveSurfaceRAMPages (P1-3, digest mode=active).
///
/// The helper decides which physical RAM pages the currently displayed video
/// surface lives on: ATM hardware modes follow the 7FFD-selected bit-plane pair
/// {videoPage - 4, videoPage} exactly like the DrawATM* renderers, everything
/// else keeps the classic screen pages (both latched pages on 128K-class
/// machines, page 5 alone otherwise).

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "emulator/video/screen.h"

namespace
{
std::vector<uint16_t> Pages(VideoModeEnum mode, uint8_t p7FFD, bool bankedZX)
{
    return Screen::GetActiveSurfaceRAMPages(mode, p7FFD, bankedZX);
}
}  // namespace

TEST(Screen_ActiveSurface_Test, ClassicModesKeepScreenPages)
{
    const std::vector<uint16_t> banked = Pages(M_ZX128, 0x00, true);
    ASSERT_EQ(banked.size(), 2u);
    EXPECT_EQ(banked[0], 5);
    EXPECT_EQ(banked[1], 7);

    const std::vector<uint16_t> unbanked = Pages(M_ZX48, 0x00, false);
    ASSERT_EQ(unbanked.size(), 1u);
    EXPECT_EQ(unbanked[0], 5);
}

TEST(Screen_ActiveSurface_Test, ClassicModesIgnoreScreenSelectBit)
{
    // The shadow-screen select (7FFD bit 3) does not narrow the ZX surface:
    // both latched pages stay in the digest set
    EXPECT_EQ(Pages(M_ZX128, 0x08, true).size(), 2u);
}

TEST(Screen_ActiveSurface_Test, AtmModesFollow7ffdBitplanePair)
{
    // videoPage = (p7FFD & 0x08) ? 7 : 5; bit-planes at {videoPage - 4, videoPage}
    const std::vector<uint16_t> lowBank = Pages(M_ATM16, 0x00, true);
    ASSERT_EQ(lowBank.size(), 2u);
    EXPECT_EQ(lowBank[0], 1);
    EXPECT_EQ(lowBank[1], 5);

    const std::vector<uint16_t> highBank = Pages(M_ATM16, 0x08, true);
    ASSERT_EQ(highBank.size(), 2u);
    EXPECT_EQ(highBank[0], 3);
    EXPECT_EQ(highBank[1], 7);
}

TEST(Screen_ActiveSurface_Test, EveryAtmHardwareModeUsesThePairLayout)
{
    const VideoModeEnum atmModes[] = {M_ATM16, M_ATMHR, M_ATMTX, M_ATMTL};
    for (VideoModeEnum mode : atmModes)
    {
        // p7FFD = 0x10: bit 3 clear -> {1, 5}; bankedZX is irrelevant for ATM modes
        const std::vector<uint16_t> pages = Pages(mode, 0x10, false);
        ASSERT_EQ(pages.size(), 2u) << "mode " << static_cast<int>(mode);
        EXPECT_EQ(pages[0], 1) << "mode " << static_cast<int>(mode);
        EXPECT_EQ(pages[1], 5) << "mode " << static_cast<int>(mode);
    }
}
