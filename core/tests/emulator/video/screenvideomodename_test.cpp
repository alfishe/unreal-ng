/// @file screenvideomodename_test.cpp
/// @brief Regression tests for Screen::GetVideoModeName: every VideoModeEnum value
/// must map to a real name, never "Unknown". The name feeds the machine-identity
/// payload (EmulatorManager::GetMachineIdentity), screen_get_mode and the digest
/// active-surface report on every automation interface - a 128k/Pentagon/Scorpion
/// machine used to report "Unknown" before the missing cases were added.

#include <gtest/gtest.h>

#include <string>

#include <emulator/video/screen.h>

namespace
{
/// Values the enum can actually hold in a running machine (M_MAX is a sentinel)
constexpr VideoModeEnum kRealModes[] = {
    M_NUL,     M_ZX48,    M_ZX128,  M_PENTAGON128K, M_PMC,     M_P16,        M_P384,
    M_PHR,     M_TIMEX,   M_TS16,   M_TS256,        M_TSTX,    M_ATM16,      M_ATMHR,
    M_ATMTX,   M_ATMTL,   M_PROFI,  M_GMX,          M_BRD,     M_SCORPION,
};
}  // namespace

TEST(Screen_VideoModeName_Test, EveryRealModeHasAName)
{
    for (VideoModeEnum mode : kRealModes)
    {
        const std::string name = Screen::GetVideoModeName(mode);
        EXPECT_NE(name, "Unknown") << "mode value " << static_cast<int>(mode) << " has no name";
        EXPECT_FALSE(name.empty()) << "mode value " << static_cast<int>(mode) << " has an empty name";
    }
}

TEST(Screen_VideoModeName_Test, StandardModelModes)
{
    // The names the automation surfaces quote verbatim
    EXPECT_EQ(Screen::GetVideoModeName(M_ZX48), "ZX");
    EXPECT_EQ(Screen::GetVideoModeName(M_ZX128), "ZX128");
    EXPECT_EQ(Screen::GetVideoModeName(M_PENTAGON128K), "Pentagon128K");
    EXPECT_EQ(Screen::GetVideoModeName(M_SCORPION), "Scorpion");
    EXPECT_EQ(Screen::GetVideoModeName(M_ATM16), "ATM16");
}

TEST(Screen_VideoModeName_Test, OutOfRangeFallsBackToUnknown)
{
    EXPECT_EQ(Screen::GetVideoModeName(M_MAX), "Unknown");
    EXPECT_EQ(Screen::GetVideoModeName(static_cast<VideoModeEnum>(0xFF)), "Unknown");
}
