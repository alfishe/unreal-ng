#pragma once
// Compatibility shim: the 5x7 font and rect helpers now live in libsdb (sdb/draw.hpp).
#include <string>

#include "sdb/draw.h"

namespace cli
{
inline bool Renderable(const std::string& s)
{
    return sdb::draw::Renderable(s);
}
inline void FillRect(sdb::Rgb& im, int x, int y, int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    sdb::draw::FillRect(im, x, y, w, h, {r, g, b});
}
inline int DrawText(sdb::Rgb& im, int x, int y, const std::string& s, int scale, std::uint8_t r, std::uint8_t g,
                    std::uint8_t b)
{
    return sdb::draw::Text(im, x, y, s, scale, {r, g, b});
}
}  // namespace cli
