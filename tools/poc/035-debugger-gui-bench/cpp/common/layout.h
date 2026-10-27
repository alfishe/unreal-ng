#pragma once

// Fixed layout and palette shared by every variant, so all of them draw the same picture (1600 x 1000).

#include <cstdint>

namespace bench::layout
{

struct Rect
{
    float x, y, w, h;
};

constexpr float kFontPx = 12.0f;       // DejaVu Sans Mono; titles always use this size (panel chrome)
constexpr float kLineH = 16.0f;        // line height at the default content font
constexpr float kTitleH = 24.0f;
constexpr float kPad = 8.0f;
// The font ships with the POC (assets/fonts, Bitstream Vera / DejaVu licence): the same glyphs on every platform
#ifndef POC035_FONT_PATH
#error "POC035_FONT_PATH must point to assets/fonts/DejaVuSansMono.ttf (set by CMakeLists.txt)"
#endif
constexpr const char* kFontPath = POC035_FONT_PATH;
constexpr const char* kFontFamily = "DejaVu Sans Mono";

// panels (outer rects, title bar included)
constexpr Rect kDisasm{8, 8, 440, kTitleH + 60 * kLineH + 2 * kPad};          // 440 x 984
constexpr Rect kHex{456, 8, 560, kTitleH + 32 * kLineH + 2 * kPad};            // 560 x 552
constexpr Rect kImages{1024, 8, 568, kTitleH + 2 * 224 + 3 * kPad};             // 568 x 496
constexpr Rect kTimeline{456, 568, 1136, 424};

// image cells inside kImages
constexpr float kImgW = 272, kImgH = 217.6f;  // 640:512 scaled to fit two per row

// timeline plot inside kTimeline
constexpr float kPlotX = kTimeline.x + kPad, kPlotY = kTimeline.y + kTitleH + kPad;
constexpr float kRulerH = 20.0f;
constexpr float kLaneH = (kTimeline.h - kTitleH - 2 * kPad - kRulerH) / 5.0f;


// Content text metrics: everything that follows the content font size (--font=PX). The panels keep their size;
// a larger font shows fewer rows (rows that do not fit are not drawn) and long rows are clipped at the panel edge.
struct TextMetrics
{
    float fontPx = kFontPx;
    float f = 1.0f;                      // fontPx / 12
    float lineH = kLineH;
    float hexAddrX = 0, hexBytesX = 48, hexByteStep = 22, hexChangedW = 18, hexAsciiX = 48 + 16 * 22 + 12;
    float disAddrX = 14, disBytesX = 54, disTextX = 150, disCommentX = 290;
    int hexRows = 32, disRows = 60;      // rows that fit the fixed panels
};

inline TextMetrics& MutableMetrics()
{
    static TextMetrics m;
    return m;
}
inline const TextMetrics& T() { return MutableMetrics(); }

inline void SetContentFont(float px)
{
    TextMetrics m;
    m.fontPx = px;
    m.f = px / kFontPx;
    m.lineH = float(int(px * 4.0f / 3.0f + 0.5f));
    m.hexAddrX = 0;
    m.hexBytesX = 48 * m.f;
    m.hexByteStep = 22 * m.f;
    m.hexChangedW = 18 * m.f;
    m.hexAsciiX = (48 + 16 * 22 + 12) * m.f;
    m.disAddrX = 14 * m.f;
    m.disBytesX = 54 * m.f;
    m.disTextX = 150 * m.f;
    m.disCommentX = 290 * m.f;
    m.hexRows = int((32 * kLineH) / m.lineH);
    m.disRows = int((60 * kLineH) / m.lineH);
    if (m.hexRows > 32) m.hexRows = 32;
    if (m.disRows > 60) m.disRows = 60;
    MutableMetrics() = m;
}

// palette (0xRRGGBB), dark theme of the mockups
constexpr uint32_t kBg = 0x0E1014, kPanel = 0x161A20, kLine = 0x2A303A, kTitle = 0xAEB6C0, kFg = 0xE6E8EB,
                   kMuted = 0x8E98A5, kAccent = 0xE8A33D, kChangedBg = 0x3A2E14, kPcRowBg = 0x2B2414, kBreak = 0xFF8A7A,
                   kPlayhead = 0xE8A33D, kLaneBg = 0x11141A;

inline float R(uint32_t c) { return ((c >> 16) & 255) / 255.f; }
inline float G(uint32_t c) { return ((c >> 8) & 255) / 255.f; }
inline float B(uint32_t c) { return (c & 255) / 255.f; }

}  // namespace bench::layout
