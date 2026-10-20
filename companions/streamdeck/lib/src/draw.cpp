#include "sdb/draw.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sdb::draw
{
namespace
{
constexpr float kPi = 3.14159265f;

inline void BlendPx(Rgb& im, int x, int y, Color c, float a)
{
    if (x < 0 || y < 0 || x >= im.w || y >= im.h || a <= 0.f)
        return;
    a = std::min(a, 1.f);
    auto* p = im.At(x, y);
    p[0] = static_cast<std::uint8_t>(p[0] + (c.r - p[0]) * a);
    p[1] = static_cast<std::uint8_t>(p[1] + (c.g - p[1]) * a);
    p[2] = static_cast<std::uint8_t>(p[2] + (c.b - p[2]) * a);
}

struct Glyph
{
    char c;
    std::uint8_t r[7];
};
// 7 rows of 5 bits, bit4 = leftmost.
constexpr Glyph kGlyphs[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'-', {0, 0, 0, 0x1F, 0, 0, 0}},
    {':', {0, 0x04, 0, 0, 0, 0x04, 0}},
    {'.', {0, 0, 0, 0, 0, 0x0C, 0x0C}},
    {',', {0, 0, 0, 0, 0x0C, 0x04, 0x08}},
    {'>', {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10}},
    {'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
    {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
    {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},
    {'+', {0, 0x04, 0x04, 0x1F, 0x04, 0x04, 0}},
    {'=', {0, 0, 0x1F, 0, 0x1F, 0, 0}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0, 0x04}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0, 0x04}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {' ', {0, 0, 0, 0, 0, 0, 0}},
};
const Glyph* FindGlyph(char c)
{
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const auto& g : kGlyphs)
        if (g.c == c)
            return &g;
    return nullptr;
}
}  // namespace

Color Mix(Color a, Color b, float t)
{
    t = std::clamp(t, 0.f, 1.f);
    return {static_cast<std::uint8_t>(a.r + (b.r - a.r) * t), static_cast<std::uint8_t>(a.g + (b.g - a.g) * t),
            static_cast<std::uint8_t>(a.b + (b.b - a.b) * t)};
}
Color Dim(Color c, float f)
{
    auto s = [f](int v) { return static_cast<std::uint8_t>(std::clamp(v * f, 0.f, 255.f)); };
    return {s(c.r), s(c.g), s(c.b)};
}
Color Hsv(float h, float s, float v)
{
    h -= std::floor(h);
    const float i = std::floor(h * 6), f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    float r = 0, g = 0, b = 0;
    switch (static_cast<int>(i) % 6)
    {
        case 0:
            r = v;
            g = t;
            b = p;
            break;
        case 1:
            r = q;
            g = v;
            b = p;
            break;
        case 2:
            r = p;
            g = v;
            b = t;
            break;
        case 3:
            r = p;
            g = q;
            b = v;
            break;
        case 4:
            r = t;
            g = p;
            b = v;
            break;
        default:
            r = v;
            g = p;
            b = q;
            break;
    }
    return {static_cast<std::uint8_t>(r * 255), static_cast<std::uint8_t>(g * 255), static_cast<std::uint8_t>(b * 255)};
}

void Fill(Rgb& im, Color c)
{
    FillRect(im, 0, 0, im.w, im.h, c);
}

void FillRect(Rgb& im, int x, int y, int w, int h, Color c)
{
    const int x0 = std::max(0, x), x1 = std::min(im.w, x + w), y0 = std::max(0, y), y1 = std::min(im.h, y + h);
    for (int j = y0; j < y1; ++j)
        for (int i = x0; i < x1; ++i)
        {
            auto* p = im.At(i, j);
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
        }
}
void RectAlpha(Rgb& im, int x, int y, int w, int h, Color c, float a)
{
    for (int j = std::max(0, y); j < std::min(im.h, y + h); ++j)
        for (int i = std::max(0, x); i < std::min(im.w, x + w); ++i)
            BlendPx(im, i, j, c, a);
}
void Outline(Rgb& im, int x, int y, int w, int h, int t, Color c)
{
    FillRect(im, x, y, w, t, c);
    FillRect(im, x, y + h - t, w, t, c);
    FillRect(im, x, y, t, h, c);
    FillRect(im, x + w - t, y, t, h, c);
}
void GradH(Rgb& im, int x, int y, int w, int h, Color a, Color b)
{
    for (int i = 0; i < w; ++i)
        FillRect(im, x + i, y, 1, h, Mix(a, b, w > 1 ? static_cast<float>(i) / (w - 1) : 0.f));
}
void GradV(Rgb& im, int x, int y, int w, int h, Color a, Color b)
{
    for (int j = 0; j < h; ++j)
        FillRect(im, x, y + j, w, 1, Mix(a, b, h > 1 ? static_cast<float>(j) / (h - 1) : 0.f));
}

void RoundRect(Rgb& im, float x, float y, float w, float h, float r, Color c)
{
    r = std::min({r, w / 2, h / 2});
    for (int j = static_cast<int>(std::floor(y)); j < static_cast<int>(std::ceil(y + h)); ++j)
        for (int i = static_cast<int>(std::floor(x)); i < static_cast<int>(std::ceil(x + w)); ++i)
        {
            const float px = i + 0.5f, py = j + 0.5f;
            const float dx = std::max({x + r - px, px - (x + w - r), 0.f}),
                        dy = std::max({y + r - py, py - (y + h - r), 0.f});
            const float d = std::sqrt(dx * dx + dy * dy);
            float a = std::clamp(r - d + 0.5f, 0.f, 1.f);
            if (px < x || px > x + w || py < y || py > y + h)
                a = std::min(a, std::clamp(std::min({px - x, x + w - px, py - y, y + h - py}) + 0.5f, 0.f, 1.f));
            BlendPx(im, i, j, c, a);
        }
}

void Disc(Rgb& im, float cx, float cy, float r, Color c)
{
    for (int j = static_cast<int>(cy - r - 1); j <= static_cast<int>(cy + r + 1); ++j)
        for (int i = static_cast<int>(cx - r - 1); i <= static_cast<int>(cx + r + 1); ++i)
            BlendPx(im, i, j, c, r - std::hypot(i + 0.5f - cx, j + 0.5f - cy) + 0.5f);
}
void Ring(Rgb& im, float cx, float cy, float r, float t, Color c)
{
    for (int j = static_cast<int>(cy - r - t); j <= static_cast<int>(cy + r + t); ++j)
        for (int i = static_cast<int>(cx - r - t); i <= static_cast<int>(cx + r + t); ++i)
        {
            const float d = std::fabs(std::hypot(i + 0.5f - cx, j + 0.5f - cy) - r);
            BlendPx(im, i, j, c, t / 2 - d + 0.5f);
        }
}
void Line(Rgb& im, float x0, float y0, float x1, float y1, float t, Color c)
{
    const float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    const int minx = static_cast<int>(std::min(x0, x1) - t - 1), maxx = static_cast<int>(std::max(x0, x1) + t + 1);
    const int miny = static_cast<int>(std::min(y0, y1) - t - 1), maxy = static_cast<int>(std::max(y0, y1) + t + 1);
    for (int j = miny; j <= maxy; ++j)
        for (int i = minx; i <= maxx; ++i)
        {
            const float px = i + 0.5f - x0, py = j + 0.5f - y0;
            const float u = len2 > 0 ? std::clamp((px * dx + py * dy) / len2, 0.f, 1.f) : 0.f;
            BlendPx(im, i, j, c, t / 2 - std::hypot(px - u * dx, py - u * dy) + 0.5f);
        }
}
void Arc(Rgb& im, float cx, float cy, float r, float a0, float a1, float t, Color c)
{
    const int n = std::max(8, static_cast<int>(std::fabs(a1 - a0) / 6));
    float px = cx + r * std::cos(a0 * kPi / 180), py = cy + r * std::sin(a0 * kPi / 180);
    for (int k = 1; k <= n; ++k)
    {
        const float a = (a0 + (a1 - a0) * k / n) * kPi / 180;
        const float x = cx + r * std::cos(a), y = cy + r * std::sin(a);
        Line(im, px, py, x, y, t, c);
        px = x;
        py = y;
    }
}
void Tri(Rgb& im, float x0, float y0, float x1, float y1, float x2, float y2, Color c)
{
    const int minx = static_cast<int>(std::floor(std::min({x0, x1, x2}))) - 1,
              maxx = static_cast<int>(std::ceil(std::max({x0, x1, x2}))) + 1;
    const int miny = static_cast<int>(std::floor(std::min({y0, y1, y2}))) - 1,
              maxy = static_cast<int>(std::ceil(std::max({y0, y1, y2}))) + 1;
    auto edge = [](float ax, float ay, float bx, float by, float px, float py) {  // signed distance to edge
        const float ex = bx - ax, ey = by - ay;
        return ((px - ax) * ey - (py - ay) * ex) / std::max(1e-6f, std::hypot(ex, ey));
    };
    const float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    const float s = area < 0 ? -1.f : 1.f;
    for (int j = miny; j <= maxy; ++j)
        for (int i = minx; i <= maxx; ++i)
        {
            const float px = i + 0.5f, py = j + 0.5f;
            const float d = std::min({s * -edge(x0, y0, x1, y1, px, py), s * -edge(x1, y1, x2, y2, px, py),
                                      s * -edge(x2, y2, x0, y0, px, py)});
            BlendPx(im, i, j, c, d + 0.5f);
        }
}

int TextWidth(const std::string& s, int scale)
{
    return s.empty() ? 0 : static_cast<int>(s.size()) * 6 * scale - scale;
}

int Text(Rgb& im, int x, int y, const std::string& s, int scale, Color c)
{
    int cx = x;
    for (char ch : s)
    {
        if (const Glyph* g = FindGlyph(ch))
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (g->r[row] & (0x10 >> col))
                        FillRect(im, cx + col * scale, y + row * scale, scale, scale, c);
        cx += 6 * scale;
    }
    return cx - x - scale;
}
int TextCentered(Rgb& im, int cx, int y, const std::string& s, int scale, Color c)
{
    return Text(im, cx - TextWidth(s, scale) / 2, y, s, scale, c);
}
int TextShadow(Rgb& im, int x, int y, const std::string& s, int scale, Color c, Color shadow)
{
    Text(im, x + std::max(1, scale / 2), y + std::max(1, scale / 2), s, scale, shadow);
    return Text(im, x, y, s, scale, c);
}
bool Renderable(const std::string& s)
{
    if (s.empty())
        return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return FindGlyph(c) != nullptr; });
}
std::string AllowedChars()
{
    return "A-Z 0-9 space - : . , > < / % + = ! ? ( )";
}

}  // namespace sdb::draw
