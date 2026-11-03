// palette.cpp - canonical colour table (TDD-DBG-01 §1.4).
#include "screen/palette.h"

namespace dbg {

Palette::Palette(Theme theme) : theme_(theme) {
    SetTheme(theme);
}

void Palette::SetTheme(Theme theme) {
    theme_ = theme;
    normalLevel_ = (theme == Theme::UnrealAlone) ? 0xA0 : 0xC0;
    brightLevel_ = 0xFF;  // bright black stays black (index 8 -> all off)
}

Rgb Palette::ColorOf(int index) const {
    const uint8_t level = (index & 8) ? brightLevel_ : normalLevel_;
    return Rgb{
        static_cast<uint8_t>(index & 2 ? level : 0),
        static_cast<uint8_t>(index & 4 ? level : 0),
        static_cast<uint8_t>(index & 1 ? level : 0),
    };
}

}  // namespace dbg
