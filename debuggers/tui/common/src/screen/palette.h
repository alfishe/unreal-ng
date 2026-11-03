// palette.h - ZX attribute nibble -> RGB colour model (TDD-DBG-01 §1.3-§1.4).
//
// Attribute byte: [PAPER (4 bits)][INK (4 bits)], channel order inside a
// nibble is bit0=B bit1=R bit2=G bit3=I(bright). Themes set the normal and
// bright channel levels; the layout never depends on the theme.
#pragma once

#include <cstdint>

namespace dbg {

struct Rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

enum class Theme {
    UnrealClassic,  // canonical: normal 0xC0, bright 0xFF (§1.4)
    UnrealAlone,    // shipped 'alone' theme: normal 0xA0, bright 0xFF
};

class Palette {
public:
    explicit Palette(Theme theme = Theme::UnrealClassic);

    void SetTheme(Theme theme);
    Theme GetTheme() const { return theme_; }

    // Colour of ZX palette index 0..15 (bit3 = bright).
    Rgb ColorOf(int index) const;

private:
    Theme theme_;
    uint8_t normalLevel_;
    uint8_t brightLevel_;
};

// A grayed-out colour (a state that is not live): its luminance, dimmed into a dark band so the dialog over it stands
// out while every panel stays readable
inline Rgb GrayOf(Rgb c) {
    const int l = (77 * c.r + 150 * c.g + 29 * c.b) >> 8;
    const auto v = static_cast<uint8_t>(0x20 + l * 0x70 / 0xFF);
    return Rgb{v, v, v};
}

// Decode paper nibble (attribute >> 4) or ink nibble (attribute & 0x0F).
inline int PaperIndex(uint8_t attr) { return attr >> 4; }
inline int InkIndex(uint8_t attr) { return attr & 0x0F; }

}  // namespace dbg
