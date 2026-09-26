// frame-render.h - TextScreen -> FTXUI element for the terminal back-end.
//
// The painters fill an 80x30 (char, attr) grid with monitor character codes;
// this module turns that grid into an ftxui::Element: runs of equal-attribute
// cells become one text node coloured through the ZX palette. Monitor codes
// (CP437/CP866 space) are converted to UTF-8 here - the terminal is Unicode.
#pragma once

#include <ftxui/dom/elements.hpp>

#include "screen/palette.h"
#include "screen/textscreen.h"

namespace dbg {

ftxui::Element RenderFrame(const TextScreen& screen, const Palette& palette);

// Monitor code -> UTF-8 (exposed for tests). Unmapped codes become ".".
std::string Utf8OfCode(uint8_t code);

}  // namespace dbg
