#include "sdb/svg.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

namespace sdb
{
namespace
{
// Rasterises to RGBA; returns empty on failure.
std::vector<unsigned char> Raster(const std::string& file, int size, draw::Color fg, float pad, int& outW)
{
    NSVGimage* img = nsvgParseFromFile(file.c_str(), "px", 96.0f);
    if (!img)
        return {};
    std::vector<unsigned char> rgba;
    if (img->width > 0 && img->height > 0)
    {
        const unsigned int abgr =
            0xFF000000u | (static_cast<unsigned>(fg.b) << 16) | (static_cast<unsigned>(fg.g) << 8) | fg.r;
        for (NSVGshape* s = img->shapes; s; s = s->next)
        {  // monochrome icons: every paint becomes fg
            if (s->fill.type == NSVG_PAINT_COLOR)
                s->fill.color = abgr;
            if (s->stroke.type == NSVG_PAINT_COLOR)
                s->stroke.color = abgr;
        }
        const float inner = size * (1.f - 2 * pad);
        const float scale = inner / std::max(img->width, img->height);
        const float tx = (size - img->width * scale) / 2, ty = (size - img->height * scale) / 2;
        NSVGrasterizer* r = nsvgCreateRasterizer();
        rgba.assign(static_cast<size_t>(size) * size * 4, 0);
        nsvgRasterize(r, img, tx, ty, scale, rgba.data(), size, size, size * 4);
        nsvgDeleteRasterizer(r);
        bool any = false;
        for (size_t i = 3; i < rgba.size(); i += 4)
            if (rgba[i])
            {
                any = true;
                break;
            }
        if (!any)
            rgba.clear();
    }
    nsvgDelete(img);
    outW = size;
    return rgba;
}
}  // namespace

bool DrawSvg(Rgb& im, const std::string& file, int x0, int y0, int size, draw::Color fg, float pad)
{
    int w = 0;
    const auto rgba = Raster(file, size, fg, pad, w);
    if (rgba.empty())
        return false;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const int dx = x0 + x, dy = y0 + y;
            if (dx < 0 || dy < 0 || dx >= im.w || dy >= im.h)
                continue;
            const unsigned char* s = &rgba[(static_cast<size_t>(y) * size + x) * 4];
            const float a = s[3] / 255.f;
            if (a <= 0)
                continue;
            auto* p = im.At(dx, dy);
            for (int c = 0; c < 3; ++c)
                p[c] = static_cast<std::uint8_t>(p[c] + (s[c] - p[c]) * a);  // nanosvg output is not premultiplied
        }
    return true;
}

std::optional<Rgb> RenderSvg(const std::string& file, int size, draw::Color fg, draw::Color bg, float pad)
{
    Rgb im(size, size, bg.r, bg.g, bg.b);
    if (!DrawSvg(im, file, 0, 0, size, fg, pad))
        return std::nullopt;
    return im;
}

}  // namespace sdb
