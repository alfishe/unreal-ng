// Tiny software 2D toolkit for status screens: anti-aliased discs/lines, gradients, 5x7 bitmap text.
#pragma once
#include <string>

#include "sdb/image.h"

namespace sdb::draw
{

struct Color
{
    std::uint8_t r = 0, g = 0, b = 0;
};

// ZX Spectrum palette (normal and BRIGHT).
inline constexpr Color kBlack{0, 0, 0}, kBlue{0, 0, 215}, kRed{215, 0, 0}, kMagenta{215, 0, 215}, kGreen{0, 215, 0},
    kCyan{0, 215, 215}, kYellow{215, 215, 0}, kWhite{215, 215, 215}, kBBlue{0, 0, 255}, kBRed{255, 0, 0},
    kBMagenta{255, 0, 255}, kBGreen{0, 255, 0}, kBCyan{0, 255, 255}, kBYellow{255, 255, 0}, kBWhite{255, 255, 255},
    kGrey{90, 90, 100}, kDark{14, 14, 26};

[[nodiscard]] Color Mix(Color a, Color b, float t);  // t=0 -> a, t=1 -> b
[[nodiscard]] Color Dim(Color c, float f);           // multiply brightness
[[nodiscard]] Color Hsv(float h, float s, float v);  // h in turns (wraps)

void Fill(Rgb& im, Color c);
void FillRect(Rgb& im, int x, int y, int w, int h, Color c);
void RectAlpha(Rgb& im, int x, int y, int w, int h, Color c, float alpha);
void Outline(Rgb& im, int x, int y, int w, int h, int t, Color c);
void GradH(Rgb& im, int x, int y, int w, int h, Color a, Color b);
void GradV(Rgb& im, int x, int y, int w, int h, Color a, Color b);
void RoundRect(Rgb& im, float x, float y, float w, float h, float r, Color c);
void Disc(Rgb& im, float cx, float cy, float r, Color c);
void Ring(Rgb& im, float cx, float cy, float r, float thickness, Color c);
void Line(Rgb& im, float x0, float y0, float x1, float y1, float thickness, Color c);
void Arc(Rgb& im, float cx, float cy, float r, float fromDeg, float toDeg, float thickness,
         Color c);  // 0 deg = +x, clockwise on screen
void Tri(Rgb& im, float x0, float y0, float x1, float y1, float x2, float y2, Color c);

[[nodiscard]] int TextWidth(const std::string& s, int scale);
int Text(Rgb& im, int x, int y, const std::string& s, int scale, Color c);
int TextCentered(Rgb& im, int cx, int y, const std::string& s, int scale, Color c);
int TextShadow(Rgb& im, int x, int y, const std::string& s, int scale, Color c, Color shadow);
[[nodiscard]] bool Renderable(const std::string& s);  // every char drawable (A-Z 0-9 space and a few symbols)
[[nodiscard]] std::string AllowedChars();

}  // namespace sdb::draw
