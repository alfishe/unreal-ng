#include "framescaler.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace FrameScaler
{
Layout ComputeLayout(uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH)
{
    Layout layout;
    if (srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0)
        return layout;

    // Fit: the biggest picture of the source's aspect inside the output
    if (static_cast<uint64_t>(srcW) * dstH <= static_cast<uint64_t>(srcH) * dstW)
    {
        layout.height = dstH;
        layout.width = std::max<uint32_t>(1, static_cast<uint32_t>(static_cast<uint64_t>(srcW) * dstH / srcH));
    }
    else
    {
        layout.width = dstW;
        layout.height = std::max<uint32_t>(1, static_cast<uint32_t>(static_cast<uint64_t>(srcH) * dstW / srcW));
    }
    layout.offsetX = (dstW - layout.width) / 2;
    layout.offsetY = (dstH - layout.height) / 2;

    // An exact whole multiple (1080p in 4K: 2x): every pixel is a k x k block, the fast row-repeat path
    if (layout.width % srcW == 0 && layout.height % srcH == 0 && layout.width / srcW == layout.height / srcH)
        layout.scale = layout.width / srcW;
    return layout;
}

namespace
{
inline bool SameLayout(const Layout& a, const Layout& b)
{
    return a.scale == b.scale && a.width == b.width && a.height == b.height && a.offsetX == b.offsetX &&
           a.offsetY == b.offsetY;
}

/// Opaque black over the whole output (alpha 0xFF in any channel order: the channel that is alpha is the last byte
/// in RGBA / BGRA, and the colors are zero)
void FillBars(std::vector<uint8_t>& out)
{
    uint32_t* words = reinterpret_cast<uint32_t*>(out.data());
    const uint8_t black[4] = {0, 0, 0, 0xFF};
    uint32_t word;
    std::memcpy(&word, black, sizeof(word));
    std::fill(words, words + out.size() / 4, word);
}

/// SIMD-CANDIDATE(frame scaler row expand): the k-fold pixel repeat is a store-bound scalar loop; the compiler
/// vectorizes the fixed-width fills for small k, an SSE / NEON zip-and-store would cover every k
void ExpandRow(const uint32_t* src, uint32_t srcW, uint32_t k, uint32_t* dst)
{
    switch (k)
    {
        case 1:
            std::memcpy(dst, src, static_cast<size_t>(srcW) * sizeof(uint32_t));
            break;
        case 2:
            for (uint32_t x = 0; x < srcW; x++)
            {
                const uint32_t p = src[x];
                dst[2 * x] = p;
                dst[2 * x + 1] = p;
            }
            break;
        case 4:
            for (uint32_t x = 0; x < srcW; x++)
            {
                const uint32_t p = src[x];
                uint32_t* d = dst + 4 * static_cast<size_t>(x);
                d[0] = p;
                d[1] = p;
                d[2] = p;
                d[3] = p;
            }
            break;
        default:
            for (uint32_t x = 0; x < srcW; x++)
                std::fill_n(dst + static_cast<size_t>(x) * k, k, src[x]);
            break;
    }
}
}  // namespace

namespace
{
constexpr uint32_t kOpaqueBlack = 0xFF000000u;  // little-endian bytes 00 00 00 FF, the same in RGBA and BGRA

/// R,G,B,A <-> B,G,R,A on 32-bit words (little-endian): compilers turn the loop into NEON / SSE shuffles
inline uint32_t SwapRedBlue(uint32_t p)
{
    return (p & 0xFF00FF00u) | ((p >> 16) & 0xFFu) | ((p & 0xFFu) << 16);
}

void SwapRow(const uint32_t* src, uint32_t* dst, uint32_t count)
{
    for (uint32_t x = 0; x < count; x++)
        dst[x] = SwapRedBlue(src[x]);
}
}  // namespace

namespace
{
/// The picture of `layout` into rows `strideWords` words apart (row 0 of the output at `dst`); the bars are the
/// caller's.
/// An exact multiple repeats each converted source row k times; any other size samples nearest, a source row is
/// gathered once and the output rows that map to the same source row are a memcpy of it
void DrawPicture(const uint32_t* src, uint32_t srcW, uint32_t srcH, const Layout& layout, uint32_t* dst,
                 size_t strideWords, bool swapRedBlue)
{
    auto rowAt = [&](uint32_t y) { return dst + static_cast<size_t>(y) * strideWords; };

    if (layout.scale >= 1)
    {
        const uint32_t k = layout.scale;
        thread_local std::vector<uint32_t> swapped;
        for (uint32_t sy = 0; sy < srcH; sy++)
        {
            const uint32_t* srcRow = src + static_cast<size_t>(sy) * srcW;
            if (swapRedBlue)
            {
                swapped.resize(srcW);
                SwapRow(srcRow, swapped.data(), srcW);
                srcRow = swapped.data();
            }
            uint32_t* firstRow = rowAt(layout.offsetY + sy * k) + layout.offsetX;
            ExpandRow(srcRow, srcW, k, firstRow);
            for (uint32_t r = 1; r < k; r++)
                std::memcpy(rowAt(layout.offsetY + sy * k + r) + layout.offsetX, firstRow,
                            static_cast<size_t>(layout.width) * sizeof(uint32_t));
        }
        return;
    }

    thread_local std::vector<uint32_t> columnMap;
    columnMap.resize(layout.width);
    for (uint32_t x = 0; x < layout.width; x++)
        columnMap[x] = static_cast<uint32_t>(static_cast<uint64_t>(x) * srcW / layout.width);

    uint32_t previousSource = UINT32_MAX;
    for (uint32_t y = 0; y < layout.height; y++)
    {
        const uint32_t sy = static_cast<uint32_t>(static_cast<uint64_t>(y) * srcH / layout.height);
        uint32_t* out = rowAt(layout.offsetY + y) + layout.offsetX;
        if (sy == previousSource)
        {
            std::memcpy(out, rowAt(layout.offsetY + y - 1) + layout.offsetX,
                        static_cast<size_t>(layout.width) * sizeof(uint32_t));
            continue;
        }
        previousSource = sy;
        const uint32_t* row = src + static_cast<size_t>(sy) * srcW;
        if (swapRedBlue)
        {
            for (uint32_t x = 0; x < layout.width; x++)
                out[x] = SwapRedBlue(row[columnMap[x]]);
        }
        else
        {
            for (uint32_t x = 0; x < layout.width; x++)
                out[x] = row[columnMap[x]];
        }
    }
}
}  // namespace

bool ScaleInto(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint8_t* dst, size_t dstStride, uint32_t dstW,
               uint32_t dstH, bool swapRedBlue)
{
    const Layout layout = ComputeLayout(srcW, srcH, dstW, dstH);
    if (!src || !dst || layout.width == 0 || dstStride < static_cast<size_t>(dstW) * 4)
        return false;

    auto rowAt = [&](uint32_t y) { return reinterpret_cast<uint32_t*>(dst + static_cast<size_t>(y) * dstStride); };
    const uint32_t* srcWords = reinterpret_cast<const uint32_t*>(src);

    // Bars: the bands above and below whole, the strips left and right of the picture row by row
    for (uint32_t y = 0; y < dstH; y++)
    {
        if (y < layout.offsetY || y >= layout.offsetY + layout.height)
        {
            std::fill_n(rowAt(y), dstW, kOpaqueBlack);
        }
        else
        {
            uint32_t* row = rowAt(y);
            std::fill_n(row, layout.offsetX, kOpaqueBlack);
            const uint32_t right = layout.offsetX + layout.width;
            std::fill_n(row + right, dstW - right, kOpaqueBlack);
        }
    }

    DrawPicture(srcWords, srcW, srcH, layout, reinterpret_cast<uint32_t*>(dst), dstStride / 4, swapRedBlue);
    return true;
}

void Scaler::Reset()
{
    _output.clear();
    _output.shrink_to_fit();
    _dstW = _dstH = 0;
    _barsFor = Layout();
}

const uint8_t* Scaler::Scale(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH)
{
    const Layout layout = ComputeLayout(srcW, srcH, dstW, dstH);
    if (!src || layout.width == 0)
        return nullptr;

    // New output size or a picture of another shape: reallocate / redraw the bars once
    if (dstW != _dstW || dstH != _dstH || _output.size() != static_cast<size_t>(dstW) * dstH * 4)
    {
        _output.resize(static_cast<size_t>(dstW) * dstH * 4);
        _dstW = dstW;
        _dstH = dstH;
        _barsFor = Layout();
        FillBars(_output);
        _barsFor = layout;
    }
    else if (!SameLayout(layout, _barsFor))
    {
        FillBars(_output);
        _barsFor = layout;
    }

    const uint32_t* srcWords = reinterpret_cast<const uint32_t*>(src);
    uint32_t* outWords = reinterpret_cast<uint32_t*>(_output.data());
    const size_t dstStride = dstW;

    DrawPicture(srcWords, srcW, srcH, layout, outWords, dstStride, false);

    return _output.data();
}
}  // namespace FrameScaler
