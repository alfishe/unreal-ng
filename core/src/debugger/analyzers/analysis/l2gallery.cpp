#include "debugger/analyzers/analysis/l2gallery.h"

#include <algorithm>

#include "3rdparty/lodepng/lodepng.h"
#include "emulator/video/zx/zxgeometry.h"

namespace analysis
{

namespace
{

constexpr uint32_t kWhite = 0xFFFFFFFF;
constexpr uint32_t kBlack = 0xFF000000;
constexpr uint32_t kGap = 0xFF808080;
constexpr uint32_t kClear = 0xFFC0C0C0;

/// ABGR colour of an attribute's ink / paper (bright applies to both; flash ignored)
uint32_t Ink(uint8_t attr)
{
    return ZxGeometry::kPalette[(attr >> 6) & 1][attr & 7];
}
uint32_t Paper(uint8_t attr)
{
    return ZxGeometry::kPalette[(attr >> 6) & 1][(attr >> 3) & 7];
}

/// The byte of @p region at @p offset: the end of the replay, else its start, else 0
uint8_t ByteOf(const L1Result& l1, const Region& region, uint32_t offset)
{
    const uint32_t at = KeyOffset(region.lo) + offset;
    if (at > 0x3FFF)
        return 0;
    const PhysKey key = MakeKey(KeyPage(region.lo), static_cast<uint16_t>(at));
    uint8_t value = 0;
    if (l1.EndByte(key, value) || l1.StartByte(key, value))
        return value;
    return 0;
}

/// An unscaled ABGR canvas
struct Canvas
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint32_t> pixels;

    Canvas(uint32_t w, uint32_t h, uint32_t fill) : width(w), height(h), pixels(static_cast<size_t>(w) * h, fill) {}
    void Set(uint32_t x, uint32_t y, uint32_t colour)
    {
        if (x < width && y < height)
            pixels[static_cast<size_t>(y) * width + x] = colour;
    }
};

bool Rows(const Region& r)
{
    return r.kind == RegionKind::Font || r.kind == RegionKind::Sprite || r.kind == RegionKind::Tile;
}

/// The geometry the gallery can draw
bool Drawable(const Region& r)
{
    const Geometry& g = r.geometry;
    if (Rows(r))
        return g.width != 0 && g.width % 8 == 0 && g.width <= 256 && g.height != 0 && g.height <= 256;
    if (r.kind == RegionKind::Screen)
        return (g.layout == "screen" && g.height != 0 && g.height <= 192) ||
               (g.layout == "attributes" && g.width != 0 && g.width <= 64 && g.height != 0 && g.height <= 64);
    if (r.kind == RegionKind::LevelMap)
        return g.width != 0 && g.width <= 256 && g.height != 0 && g.height <= 256;
    return false;
}

Canvas DrawFrames(const L1Result& l1, const Region& r, const GalleryOptions& options, uint16_t& frames)
{
    const Geometry& g = r.geometry;
    const uint32_t columns = g.width / 8u;
    const uint32_t across = g.mask ? 2u : 1u;
    const uint32_t rowStep = columns * across;
    const uint32_t frameBytes = rowStep * g.height + g.prefix;
    const uint32_t stride = g.stride != 0 ? g.stride : frameBytes;
    uint32_t count = g.frames != 0 ? g.frames : 1u;
    if (r.Size() >= frameBytes)
        count = std::min<uint32_t>(count, (r.Size() - frameBytes) / stride + 1u);
    else
        count = 0;
    count = std::min<uint32_t>(count, options.maxFrames);
    frames = static_cast<uint16_t>(count);
    const uint32_t perRow = std::max<uint32_t>(1u, std::min<uint32_t>(count, r.kind == RegionKind::Font ? 16u
                                                                                                        : options.columns));
    const uint32_t gridRows = count == 0 ? 0 : (count + perRow - 1u) / perRow;
    Canvas canvas(perRow * (g.width + 1u) + 1u, gridRows * (g.height + 1u) + 1u, kGap);
    for (uint32_t f = 0; f < count; ++f)
    {
        const uint32_t base = f * stride;
        const uint8_t attr = g.prefix != 0 ? ByteOf(l1, r, base) : 0x38;  // white paper, black ink
        const uint32_t ox = 1u + (f % perRow) * (g.width + 1u);
        const uint32_t oy = 1u + (f / perRow) * (g.height + 1u);
        for (uint32_t y = 0; y < g.height; ++y)
            for (uint32_t c = 0; c < columns; ++c)
            {
                const uint32_t at = base + g.prefix + y * rowStep + c * across;
                const uint8_t pixels = ByteOf(l1, r, at + (g.mask ? 1u : 0u));
                const uint8_t mask = g.mask ? ByteOf(l1, r, at) : 0;
                for (uint32_t bit = 0; bit < 8; ++bit)
                {
                    const bool ink = (pixels >> (7 - bit)) & 1u;
                    const bool clear = g.mask && ((mask >> (7 - bit)) & 1u) && !ink;
                    canvas.Set(ox + c * 8u + bit, oy + y, ink ? Ink(attr) : clear ? kClear : Paper(attr));
                }
            }
    }
    return canvas;
}

Canvas DrawScreen(const L1Result& l1, const Region& r)
{
    const uint32_t height = std::min<uint32_t>(r.geometry.height, r.Size() / 32u);
    Canvas canvas(256, height, kWhite);
    for (uint32_t y = 0; y < height; ++y)
    {
        const uint32_t row = ((y & 0xC0u) << 5) | ((y & 7u) << 8) | ((y & 0x38u) << 2);
        for (uint32_t c = 0; c < 32; ++c)
        {
            const uint8_t pixels = ByteOf(l1, r, row + c);
            for (uint32_t bit = 0; bit < 8; ++bit)
                if ((pixels >> (7 - bit)) & 1u)
                    canvas.Set(c * 8u + bit, y, kBlack);
        }
    }
    return canvas;
}

Canvas DrawCells(const L1Result& l1, const Region& r)
{
    const uint32_t width = r.geometry.width;
    const uint32_t height = std::min<uint32_t>(r.geometry.height, r.Size() / width);
    Canvas canvas(width * 8u, height * 8u, kWhite);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
        {
            const uint8_t attr = ByteOf(l1, r, y * width + x);
            for (uint32_t py = 0; py < 8; ++py)
                for (uint32_t px = 0; px < 8; ++px)
                {
                    const bool centre = px >= 2 && px < 6 && py >= 2 && py < 6;
                    canvas.Set(x * 8u + px, y * 8u + py, centre ? Ink(attr) : Paper(attr));
                }
        }
    return canvas;
}

}  // namespace

std::vector<const Region*> GalleryRegions(const L2Result& l2)
{
    std::vector<const Region*> out;
    for (const Region& r : l2.store.Regions())
        if (Drawable(r))
            out.push_back(&r);
    return out;
}

bool RenderGallery(const L1Result& l1, const Region& region, const GalleryOptions& options, GalleryImage& image,
                   std::string& error)
{
    image = GalleryImage();
    if (!Drawable(region))
    {
        error = std::string("no picture for a ") + RegionKindName(region.kind) + " with this geometry";
        return false;
    }
    GalleryOptions o = options;
    o.scale = std::clamp<uint16_t>(o.scale, 1, 8);
    o.columns = std::max<uint16_t>(o.columns, 1);
    o.maxFrames = std::max<uint16_t>(o.maxFrames, 1);
    uint16_t frames = 1;
    Canvas canvas = Rows(region) ? DrawFrames(l1, region, o, frames)
                    : region.geometry.layout == "screen" ? DrawScreen(l1, region)
                                                         : DrawCells(l1, region);
    if (canvas.width == 0 || canvas.height == 0 || frames == 0)
    {
        error = "the region holds no whole frame";
        return false;
    }
    const uint32_t w = canvas.width * o.scale;
    const uint32_t h = canvas.height * o.scale;
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4u);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
        {
            const uint32_t c = canvas.pixels[static_cast<size_t>(y / o.scale) * canvas.width + x / o.scale];
            uint8_t* p = rgba.data() + (static_cast<size_t>(y) * w + x) * 4u;
            p[0] = static_cast<uint8_t>(c);
            p[1] = static_cast<uint8_t>(c >> 8);
            p[2] = static_cast<uint8_t>(c >> 16);
            p[3] = static_cast<uint8_t>(c >> 24);
        }
    if (lodepng::encode(image.png, rgba, w, h) != 0 || image.png.empty())
    {
        image = GalleryImage();
        error = "the PNG encoder failed";
        return false;
    }
    image.width = w;
    image.height = h;
    image.frames = frames;
    return true;
}

}  // namespace analysis
