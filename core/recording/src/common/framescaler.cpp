#include "framescaler.h"

#include <algorithm>
#include <cstring>

namespace FrameScaler
{
Layout ComputeLayout(uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH)
{
    Layout layout;
    if (srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0)
        return layout;

    const uint32_t fit = std::min(dstW / srcW, dstH / srcH);
    if (fit >= 1)
    {
        layout.scale = fit;
        layout.width = srcW * fit;
        layout.height = srcH * fit;
    }
    else
    {
        // Larger than the output: the biggest picture of the source's aspect that fits
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
    }
    layout.offsetX = (dstW - layout.width) / 2;
    layout.offsetY = (dstH - layout.height) / 2;
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

    if (layout.scale >= 1)
    {
        const uint32_t k = layout.scale;
        thread_local std::vector<uint32_t> swapped;
        for (uint32_t sy = 0; sy < srcH; sy++)
        {
            const uint32_t* srcRow = srcWords + static_cast<size_t>(sy) * srcW;
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
    }
    else
    {
        for (uint32_t y = 0; y < layout.height; y++)
        {
            const uint32_t* srcRow =
                srcWords + static_cast<size_t>(static_cast<uint64_t>(y) * srcH / layout.height) * srcW;
            uint32_t* dstRow = rowAt(layout.offsetY + y) + layout.offsetX;
            for (uint32_t x = 0; x < layout.width; x++)
            {
                const uint32_t p = srcRow[static_cast<size_t>(static_cast<uint64_t>(x) * srcW / layout.width)];
                dstRow[x] = swapRedBlue ? SwapRedBlue(p) : p;
            }
        }
    }
    return true;
}

namespace
{
/// Each source byte repeated k times (k >= 2); the common factors without a call per pixel
void ExpandBytes(const uint8_t* src, uint32_t count, uint32_t k, uint8_t* dst)
{
    switch (k)
    {
        case 2:
            for (uint32_t x = 0; x < count; x++)
            {
                dst[2 * x] = src[x];
                dst[2 * x + 1] = src[x];
            }
            break;
        case 3:
            for (uint32_t x = 0; x < count; x++)
            {
                dst[3 * x] = src[x];
                dst[3 * x + 1] = src[x];
                dst[3 * x + 2] = src[x];
            }
            break;
        case 4:
            for (uint32_t x = 0; x < count; x++)
            {
                const uint8_t v = src[x];
                uint8_t* d = dst + 4 * static_cast<size_t>(x);
                d[0] = v;
                d[1] = v;
                d[2] = v;
                d[3] = v;
            }
            break;
        default:
            for (uint32_t x = 0; x < count; x++)
                std::memset(dst + static_cast<size_t>(x) * k, src[x], k);
            break;
    }
}

inline uint8_t ClampByte(int v)
{
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

inline uint8_t LumaOf(int r, int g, int b)
{
    return ClampByte(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
}

inline void ChromaOf(int r, int g, int b, uint8_t& u, uint8_t& v)
{
    u = ClampByte(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
    v = ClampByte(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
}
}  // namespace

// Not-aliased pointers let the compiler vectorize the converters (an uint8_t* may alias anything)
#if defined(_MSC_VER)
#define FRAMESCALER_RESTRICT __restrict
#else
#define FRAMESCALER_RESTRICT __restrict__
#endif

/// SIMD-CANDIDATE(RGBA -> NV12): the 4K-to-4K case (the videowall grab) is bound by this scalar conversion, ~13 ms per
/// frame on an arm64 laptop; an SSE / NEON de-interleave + fixed-point multiply would cover every machine
void PackedToNv12(const uint8_t* rgba, size_t srcStride, uint32_t w, uint32_t h, uint8_t* yDst, size_t yStride,
                  uint8_t* uvDst, size_t uvStride)
{
    for (uint32_t y = 0; y < h; y++)
    {
        const uint8_t* FRAMESCALER_RESTRICT row = rgba + static_cast<size_t>(y) * srcStride;
        uint8_t* FRAMESCALER_RESTRICT out = yDst + static_cast<size_t>(y) * yStride;
        for (uint32_t x = 0; x < w; x++)
        {
            const int r = row[x * 4];
            const int g = row[x * 4 + 1];
            const int b = row[x * 4 + 2];
            out[x] = ClampByte(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
        }
    }
    for (uint32_t y = 0; y < h; y += 2)
    {
        const uint8_t* FRAMESCALER_RESTRICT row0 = rgba + static_cast<size_t>(y) * srcStride;
        const uint8_t* FRAMESCALER_RESTRICT row1 = rgba + static_cast<size_t>(y + 1) * srcStride;
        uint8_t* FRAMESCALER_RESTRICT out = uvDst + static_cast<size_t>(y / 2) * uvStride;
        for (uint32_t x = 0; x < w; x += 2)
        {
            const int r = (row0[x * 4] + row0[x * 4 + 4] + row1[x * 4] + row1[x * 4 + 4]) / 4;
            const int g = (row0[x * 4 + 1] + row0[x * 4 + 5] + row1[x * 4 + 1] + row1[x * 4 + 5]) / 4;
            const int b = (row0[x * 4 + 2] + row0[x * 4 + 6] + row1[x * 4 + 2] + row1[x * 4 + 6]) / 4;
            out[x] = ClampByte(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            out[x + 1] = ClampByte(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
    }
}

bool ScaleIntoNv12(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint8_t* yDst, size_t yStride, uint8_t* uvDst,
                   size_t uvStride, uint32_t dstW, uint32_t dstH)
{
    const Layout layout = ComputeLayout(srcW, srcH, dstW, dstH);
    if (!src || !yDst || !uvDst || layout.width == 0 || (dstW & 1) || (dstH & 1) || yStride < dstW ||
        uvStride < dstW)
        return false;

    if (layout.scale == 0)
    {
        // No integer factor (a picture above the output): scale to packed, convert the whole frame
        thread_local std::vector<uint8_t> packed;
        packed.resize(static_cast<size_t>(dstW) * dstH * 4);
        if (!ScaleInto(src, srcW, srcH, packed.data(), static_cast<size_t>(dstW) * 4, dstW, dstH, false))
            return false;
        PackedToNv12(packed.data(), static_cast<size_t>(dstW) * 4, dstW, dstH, yDst, yStride, uvDst, uvStride);
        return true;
    }

    const uint32_t k = layout.scale;
    const uint32_t picX0 = layout.offsetX;
    const uint32_t picX1 = layout.offsetX + layout.width;
    const uint32_t picY0 = layout.offsetY;
    const uint32_t picY1 = layout.offsetY + layout.height;

    // One output pixel per source pixel with the picture on even coordinates (the videowall's 4K grab in a 4K frame):
    // the 2x2 chroma blocks are whole source blocks, so the tight full-frame conversion runs on the picture
    // rectangle directly in the target - no per-block bookkeeping
    if (k == 1 && (picX0 & 1) == 0 && (picY0 & 1) == 0 && (srcW & 1) == 0 && (srcH & 1) == 0)
    {
        for (uint32_t y = 0; y < dstH; y++)
        {
            uint8_t* yRow = yDst + static_cast<size_t>(y) * yStride;
            if (y < picY0 || y >= picY1)
            {
                std::memset(yRow, 16, dstW);
            }
            else
            {
                std::memset(yRow, 16, picX0);
                std::memset(yRow + picX1, 16, dstW - picX1);
            }
        }
        for (uint32_t cy = 0; cy < dstH / 2; cy++)
        {
            uint8_t* uvRow = uvDst + static_cast<size_t>(cy) * uvStride;
            if (2 * cy < picY0 || 2 * cy >= picY1)
            {
                std::memset(uvRow, 128, dstW);
            }
            else
            {
                std::memset(uvRow, 128, picX0);
                std::memset(uvRow + picX1, 128, dstW - picX1);
            }
        }
        PackedToNv12(src, static_cast<size_t>(srcW) * 4, srcW, srcH, yDst + static_cast<size_t>(picY0) * yStride + picX0,
                     yStride, uvDst + static_cast<size_t>(picY0 / 2) * uvStride + picX0, uvStride);
        return true;
    }

    // ---- Y plane: one converted source row, repeated ----
    thread_local std::vector<uint8_t> lumaRow;
    lumaRow.resize(srcW);
    for (uint32_t y = 0; y < dstH; y++)
    {
        uint8_t* out = yDst + static_cast<size_t>(y) * yStride;
        if (y < picY0 || y >= picY1)
        {
            std::memset(out, 16, dstW);
            continue;
        }
        const uint32_t sy = (y - picY0) / k;
        if ((y - picY0) % k == 0)
        {
            const uint8_t* row = src + static_cast<size_t>(sy) * srcW * 4;
            std::memset(out, 16, picX0);
            uint8_t* d = out + picX0;
            if (k == 1)
            {
                // One output pixel per source pixel: convert straight into the target row
                for (uint32_t x = 0; x < srcW; x++)
                    d[x] = LumaOf(row[x * 4], row[x * 4 + 1], row[x * 4 + 2]);
            }
            else
            {
                for (uint32_t x = 0; x < srcW; x++)
                    lumaRow[x] = LumaOf(row[x * 4], row[x * 4 + 1], row[x * 4 + 2]);
                ExpandBytes(lumaRow.data(), srcW, k, d);
            }
            std::memset(out + picX1, 16, dstW - picX1);
        }
        else
        {
            // The rest of the block's rows: a copy of the first one
            std::memcpy(out, yDst + static_cast<size_t>(y - 1) * yStride, dstW);
        }
    }

    // ---- UV plane: the average under each 2x2 output block, computed once per distinct (source rows) pair ----
    // Source index under an output row / column; -1 = a bar (black)
    auto rowIndex = [&](uint32_t y) -> int { return (y < picY0 || y >= picY1) ? -1 : static_cast<int>((y - picY0) / k); };
    auto colIndex = [&](uint32_t x) -> int { return (x < picX0 || x >= picX1) ? -1 : static_cast<int>((x - picX0) / k); };

    const uint32_t chromaW = dstW / 2;
    thread_local std::vector<int> col0;
    thread_local std::vector<int> col1;
    col0.resize(chromaW);
    col1.resize(chromaW);
    for (uint32_t cx = 0; cx < chromaW; cx++)
    {
        col0[cx] = colIndex(2 * cx);
        col1[cx] = colIndex(2 * cx + 1);
    }

    thread_local std::vector<uint8_t> chromaU;
    thread_local std::vector<uint8_t> chromaV;
    chromaU.resize(srcW);
    chromaV.resize(srcW);

    int prevRow0 = -2;
    int prevRow1 = -2;
    for (uint32_t cy = 0; cy < dstH / 2; cy++)
    {
        const int r0 = rowIndex(2 * cy);
        const int r1 = rowIndex(2 * cy + 1);
        uint8_t* out = uvDst + static_cast<size_t>(cy) * uvStride;
        if (cy > 0 && r0 == prevRow0 && r1 == prevRow1)
        {
            std::memcpy(out, uvDst + static_cast<size_t>(cy - 1) * uvStride, dstW);
            continue;
        }
        prevRow0 = r0;
        prevRow1 = r1;

        // A pure row pair (both output rows under one source row): the chroma of a column pair inside one source
        // pixel is that pixel's own chroma, converted ONCE per source pixel here and looked up below
        const bool pureRow = r0 == r1 && r0 >= 0;
        if (pureRow)
        {
            const uint8_t* srcRow = src + static_cast<size_t>(r0) * srcW * 4;
            for (uint32_t x = 0; x < srcW; x++)
                ChromaOf(srcRow[x * 4], srcRow[x * 4 + 1], srcRow[x * 4 + 2], chromaU[x], chromaV[x]);
        }

        // Even factor and the picture on even coordinates: every 2x2 block is inside one source pixel, so the row is
        // the source row's (U,V) pairs repeated k / 2 times between two bars
        if (pureRow && (k & 1) == 0 && (picX0 & 1) == 0)
        {
            std::memset(out, 128, picX0);
            uint8_t* d = out + picX0;
            const uint32_t reps = k / 2;
            if (reps == 1)
            {
                for (uint32_t x = 0; x < srcW; x++)
                {
                    d[2 * x] = chromaU[x];
                    d[2 * x + 1] = chromaV[x];
                }
            }
            else
            {
                for (uint32_t x = 0; x < srcW; x++)
                    for (uint32_t r = 0; r < reps; r++)
                    {
                        d[0] = chromaU[x];
                        d[1] = chromaV[x];
                        d += 2;
                    }
            }
            std::memset(out + picX1, 128, dstW - picX1);
            continue;
        }

        for (uint32_t cx = 0; cx < chromaW; cx++)
        {
            const int c0 = col0[cx];
            const int c1 = col1[cx];
            if (pureRow && c0 == c1 && c0 >= 0)
            {
                out[2 * cx] = chromaU[c0];
                out[2 * cx + 1] = chromaV[c0];
                continue;
            }
            if (r0 < 0 && r1 < 0)
            {
                out[2 * cx] = 128;  // a bar row: black
                out[2 * cx + 1] = 128;
                continue;
            }

            int rSum = 0;
            int gSum = 0;
            int bSum = 0;
            auto add = [&](int sr, int sc) {
                if (sr < 0 || sc < 0)
                    return;  // a bar adds black (0, 0, 0)
                const uint8_t* px = src + (static_cast<size_t>(sr) * srcW + static_cast<size_t>(sc)) * 4;
                rSum += px[0];
                gSum += px[1];
                bSum += px[2];
            };
            add(r0, c0);
            add(r0, c1);
            add(r1, c0);
            add(r1, c1);
            ChromaOf(rSum / 4, gSum / 4, bSum / 4, out[2 * cx], out[2 * cx + 1]);
        }
    }
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

    if (layout.scale >= 1)
    {
        const uint32_t k = layout.scale;
        for (uint32_t sy = 0; sy < srcH; sy++)
        {
            uint32_t* firstRow = outWords + (static_cast<size_t>(layout.offsetY) + static_cast<size_t>(sy) * k) *
                                                dstStride + layout.offsetX;
            ExpandRow(srcWords + static_cast<size_t>(sy) * srcW, srcW, k, firstRow);
            for (uint32_t r = 1; r < k; r++)
                std::memcpy(firstRow + r * dstStride, firstRow, static_cast<size_t>(layout.width) * sizeof(uint32_t));
        }
    }
    else
    {
        // Nearest downscale; x mapping computed once per picture row
        for (uint32_t y = 0; y < layout.height; y++)
        {
            const uint32_t* srcRow = srcWords + static_cast<size_t>(static_cast<uint64_t>(y) * srcH / layout.height) * srcW;
            uint32_t* dstRow = outWords + (static_cast<size_t>(layout.offsetY) + y) * dstStride + layout.offsetX;
            for (uint32_t x = 0; x < layout.width; x++)
                dstRow[x] = srcRow[static_cast<size_t>(static_cast<uint64_t>(x) * srcW / layout.width)];
        }
    }

    return _output.data();
}
}  // namespace FrameScaler
