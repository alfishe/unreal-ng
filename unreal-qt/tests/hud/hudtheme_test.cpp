/// @file hudtheme_test.cpp
/// @brief The HUD themes' automation words (hud_theme, hud.theme): one per theme, each back to its theme.

#include <gtest/gtest.h>

#include <set>
#include <string>

#include "hudtheme.h"

TEST(HudTheme_Test, EveryThemeHasAWordThatComesBack)
{
    std::set<std::string> words;
    for (HudThemeId id : HudTheme::AvailableThemes())
    {
        const std::string key = HudTheme::ThemeKey(id);
        EXPECT_TRUE(words.insert(key).second) << key << ": one word per theme";
        HudThemeId back = HudThemeId::DarkGlass;
        ASSERT_TRUE(HudTheme::ThemeFromKey(key, back)) << key;
        EXPECT_EQ(back, id) << key;
    }
    EXPECT_EQ(words.size(), 6u);
    EXPECT_EQ(HudTheme::ThemeKey(HudThemeId::RetroZX), "retro_zx");
    HudThemeId none = HudThemeId::Cyberpunk;
    EXPECT_FALSE(HudTheme::ThemeFromKey("Retro ZX", none)) << "the words, not the display names";
    EXPECT_EQ(none, HudThemeId::Cyberpunk);
}
