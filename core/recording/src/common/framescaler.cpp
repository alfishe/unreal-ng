#include "framescaler.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#if defined(__ARM_NEON) || defined(_M_ARM64)
#include <arm_neon.h>
#define FRAMESCALER_NEON 1
#define FRAMESCALER_SSE2 0
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define FRAMESCALER_NEON 0
#define FRAMESCALER_SSE2 1
#else
#define FRAMESCALER_NEON 0
#define FRAMESCALER_SSE2 0
#endif

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

/// The k-fold pixel repeat. Measured 2026-11-21: the compiler's vectorized fills for k = 2..4 beat NEON
/// vst2q / vst3q / vst4q; broadcast stores for k >= 5 halve the row on arm64 but are 5-8 % of a 4K frame (the
/// repeated-row copies and the bars are the memory-bound rest) and slower on x86: no hand-written kernel
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
        case 3:
            for (uint32_t x = 0; x < srcW; x++)
            {
                const uint32_t p = src[x];
                uint32_t* d = dst + 3 * static_cast<size_t>(x);
                d[0] = p;
                d[1] = p;
                d[2] = p;
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
    uint32_t x = 0;
#if FRAMESCALER_NEON
    // 16 pixels a step: the channel planes swapped as whole registers
    for (; x + 16 <= count; x += 16)
    {
        const uint8x16x4_t p = vld4q_u8(reinterpret_cast<const uint8_t*>(src + x));
        uint8x16x4_t q;
        q.val[0] = p.val[2];
        q.val[1] = p.val[1];
        q.val[2] = p.val[0];
        q.val[3] = p.val[3];
        vst4q_u8(reinterpret_cast<uint8_t*>(dst + x), q);
    }
#elif FRAMESCALER_SSE2
    const __m128i keep = _mm_set1_epi32(static_cast<int>(0xFF00FF00u));
    const __m128i low = _mm_set1_epi32(0xFF);
    for (; x + 4 <= count; x += 4)
    {
        const __m128i p = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + x));
        const __m128i q = _mm_or_si128(_mm_and_si128(p, keep),
                                       _mm_or_si128(_mm_and_si128(_mm_srli_epi32(p, 16), low),
                                                    _mm_slli_epi32(_mm_and_si128(p, low), 16)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + x), q);
    }
#endif
    for (; x < count; x++)
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
            uint32_t* firstRow = rowAt(layout.offsetY + sy * k) + layout.offsetX;
            if (swapRedBlue && k == 1)
            {
                SwapRow(srcRow, firstRow, srcW);  // straight into the output, no second pass
            }
            else
            {
                if (swapRedBlue)
                {
                    swapped.resize(srcW);
                    SwapRow(srcRow, swapped.data(), srcW);
                    srcRow = swapped.data();
                }
                ExpandRow(srcRow, srcW, k, firstRow);
            }
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

// Not-aliased pointers let the compiler vectorize the scalar tails (an uint8_t* may alias anything)
#if defined(_MSC_VER)
#define FRAMESCALER_RESTRICT __restrict
#else
#define FRAMESCALER_RESTRICT __restrict__
#endif

// The row kernels in NEON (arm64) / SSE2 (every x86-64), the scalar formulas for the tails and other targets. Integer
// arithmetic only, the same operations as LumaOf / ChromaOf: the output is byte-identical on every machine. The ranges
// make the clamps unnecessary: luma is 16..235, chroma 16..240, and every sum fits 16 bits (luma unsigned up to 56228,
// chroma signed within +-28688)
namespace
{
/// Y of `count` packed R,G,B,A pixels (B,G,R,A with `Bgra`: only the channel positions differ)
template <bool Bgra = false>
void LumaRow(const uint8_t* FRAMESCALER_RESTRICT rgba, uint32_t count, uint8_t* FRAMESCALER_RESTRICT out)
{
    constexpr int R = Bgra ? 2 : 0;
    constexpr int B = Bgra ? 0 : 2;
    uint32_t x = 0;
#if FRAMESCALER_NEON
    for (; x + 16 <= count; x += 16)
    {
        const uint8x16x4_t p = vld4q_u8(rgba + static_cast<size_t>(x) * 4);
        uint16x8_t lo = vmull_u8(vget_low_u8(p.val[R]), vdup_n_u8(66));
        lo = vmlal_u8(lo, vget_low_u8(p.val[1]), vdup_n_u8(129));
        lo = vmlal_u8(lo, vget_low_u8(p.val[B]), vdup_n_u8(25));
        uint16x8_t hi = vmull_u8(vget_high_u8(p.val[R]), vdup_n_u8(66));
        hi = vmlal_u8(hi, vget_high_u8(p.val[1]), vdup_n_u8(129));
        hi = vmlal_u8(hi, vget_high_u8(p.val[B]), vdup_n_u8(25));
        // (sum + 128) >> 8: the rounding narrow
        const uint8x16_t y = vcombine_u8(vrshrn_n_u16(lo, 8), vrshrn_n_u16(hi, 8));
        vst1q_u8(out + x, vaddq_u8(y, vdupq_n_u8(16)));
    }
#elif FRAMESCALER_SSE2
    const __m128i low = _mm_set1_epi32(0x00FF00FF);
    // Per pixel: byte 0 in the low half (R, or B with Bgra), byte 2 in the high half
    const __m128i coefRB = _mm_set1_epi32(Bgra ? ((66 << 16) | 25) : ((25 << 16) | 66));
    const __m128i coefGA = _mm_set1_epi32(129);              // G low, A high (x 0)
    const __m128i half = _mm_set1_epi32(128);
    auto luma4 = [&](const uint8_t* p) {
        const __m128i px = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        const __m128i rb = _mm_and_si128(px, low);
        const __m128i ga = _mm_srli_epi16(px, 8);
        const __m128i sum = _mm_add_epi32(_mm_madd_epi16(rb, coefRB), _mm_madd_epi16(ga, coefGA));
        return _mm_srli_epi32(_mm_add_epi32(sum, half), 8);
    };
    for (; x + 16 <= count; x += 16)
    {
        const uint8_t* p = rgba + static_cast<size_t>(x) * 4;
        const __m128i a = _mm_packs_epi32(luma4(p), luma4(p + 16));
        const __m128i b = _mm_packs_epi32(luma4(p + 32), luma4(p + 48));
        const __m128i y = _mm_add_epi8(_mm_packus_epi16(a, b), _mm_set1_epi8(16));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + x), y);
    }
#endif
    for (; x < count; x++)
        out[x] = LumaOf(rgba[x * 4 + R], rgba[x * 4 + 1], rgba[x * 4 + B]);
}

/// U and V of each of `count` pixels (into two arrays)
void ChromaRow(const uint8_t* FRAMESCALER_RESTRICT rgba, uint32_t count, uint8_t* FRAMESCALER_RESTRICT u,
               uint8_t* FRAMESCALER_RESTRICT v)
{
    uint32_t x = 0;
#if FRAMESCALER_NEON
    auto chroma8 = [](uint16x8_t r16, uint16x8_t g16, uint16x8_t b16, uint8x8_t& uOut, uint8x8_t& vOut) {
        const int16x8_t r = vreinterpretq_s16_u16(r16);
        const int16x8_t g = vreinterpretq_s16_u16(g16);
        const int16x8_t b = vreinterpretq_s16_u16(b16);
        int16x8_t cu = vmulq_n_s16(b, 112);
        cu = vmlaq_n_s16(cu, r, -38);
        cu = vmlaq_n_s16(cu, g, -74);
        int16x8_t cv = vmulq_n_s16(r, 112);
        cv = vmlaq_n_s16(cv, g, -94);
        cv = vmlaq_n_s16(cv, b, -18);
        const int16x8_t half = vdupq_n_s16(128);
        cu = vaddq_s16(vshrq_n_s16(vaddq_s16(cu, half), 8), half);
        cv = vaddq_s16(vshrq_n_s16(vaddq_s16(cv, half), 8), half);
        uOut = vmovn_u16(vreinterpretq_u16_s16(cu));
        vOut = vmovn_u16(vreinterpretq_u16_s16(cv));
    };
    for (; x + 16 <= count; x += 16)
    {
        const uint8x16x4_t p = vld4q_u8(rgba + static_cast<size_t>(x) * 4);
        uint8x8_t u0, v0, u1, v1;
        chroma8(vmovl_u8(vget_low_u8(p.val[0])), vmovl_u8(vget_low_u8(p.val[1])), vmovl_u8(vget_low_u8(p.val[2])), u0,
                v0);
        chroma8(vmovl_u8(vget_high_u8(p.val[0])), vmovl_u8(vget_high_u8(p.val[1])),
                vmovl_u8(vget_high_u8(p.val[2])), u1, v1);
        vst1q_u8(u + x, vcombine_u8(u0, u1));
        vst1q_u8(v + x, vcombine_u8(v0, v1));
    }
#elif FRAMESCALER_SSE2
    const __m128i low = _mm_set1_epi32(0x00FF00FF);
    const __m128i coefURB = _mm_set1_epi32(static_cast<int>((112u << 16) | static_cast<uint16_t>(-38)));
    const __m128i coefUGA = _mm_set1_epi32(static_cast<uint16_t>(-74));
    const __m128i coefVRB = _mm_set1_epi32(static_cast<int>((static_cast<uint32_t>(static_cast<uint16_t>(-18)) << 16) | 112u));
    const __m128i coefVGA = _mm_set1_epi32(static_cast<uint16_t>(-94));
    const __m128i half = _mm_set1_epi32(128);
    auto chroma4 = [&](const uint8_t* p, __m128i& cu, __m128i& cv) {
        const __m128i px = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        const __m128i rb = _mm_and_si128(px, low);
        const __m128i ga = _mm_srli_epi16(px, 8);
        cu = _mm_add_epi32(_mm_madd_epi16(rb, coefURB), _mm_madd_epi16(ga, coefUGA));
        cv = _mm_add_epi32(_mm_madd_epi16(rb, coefVRB), _mm_madd_epi16(ga, coefVGA));
        cu = _mm_add_epi32(_mm_srai_epi32(_mm_add_epi32(cu, half), 8), half);
        cv = _mm_add_epi32(_mm_srai_epi32(_mm_add_epi32(cv, half), 8), half);
    };
    for (; x + 16 <= count; x += 16)
    {
        const uint8_t* p = rgba + static_cast<size_t>(x) * 4;
        __m128i u0, v0, u1, v1, u2, v2, u3, v3;
        chroma4(p, u0, v0);
        chroma4(p + 16, u1, v1);
        chroma4(p + 32, u2, v2);
        chroma4(p + 48, u3, v3);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(u + x),
                         _mm_packus_epi16(_mm_packs_epi32(u0, u1), _mm_packs_epi32(u2, u3)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(v + x),
                         _mm_packus_epi16(_mm_packs_epi32(v0, v1), _mm_packs_epi32(v2, v3)));
    }
#endif
    for (; x < count; x++)
        ChromaOf(rgba[x * 4], rgba[x * 4 + 1], rgba[x * 4 + 2], u[x], v[x]);
}

/// The interleaved U,V row of NV12 from two pixel rows: one pair per 2x2 block, of the block's average color
/// (`width` even). B,G,R,A pixels with `Bgra`
template <bool Bgra = false>
void ChromaBlockRow(const uint8_t* FRAMESCALER_RESTRICT row0, const uint8_t* FRAMESCALER_RESTRICT row1, uint32_t width,
                    uint8_t* FRAMESCALER_RESTRICT out)
{
    constexpr int R = Bgra ? 2 : 0;
    constexpr int B = Bgra ? 0 : 2;
    uint32_t x = 0;
#if FRAMESCALER_NEON
    for (; x + 16 <= width; x += 16)
    {
        const uint8x16x4_t a = vld4q_u8(row0 + static_cast<size_t>(x) * 4);
        const uint8x16x4_t b = vld4q_u8(row1 + static_cast<size_t>(x) * 4);
        // The four pixels of a block summed (at most 1020), / 4
        const int16x8_t r = vreinterpretq_s16_u16(vshrq_n_u16(vpadalq_u8(vpaddlq_u8(a.val[R]), b.val[R]), 2));
        const int16x8_t g = vreinterpretq_s16_u16(vshrq_n_u16(vpadalq_u8(vpaddlq_u8(a.val[1]), b.val[1]), 2));
        const int16x8_t bl = vreinterpretq_s16_u16(vshrq_n_u16(vpadalq_u8(vpaddlq_u8(a.val[B]), b.val[B]), 2));
        int16x8_t cu = vmulq_n_s16(bl, 112);
        cu = vmlaq_n_s16(cu, r, -38);
        cu = vmlaq_n_s16(cu, g, -74);
        int16x8_t cv = vmulq_n_s16(r, 112);
        cv = vmlaq_n_s16(cv, g, -94);
        cv = vmlaq_n_s16(cv, bl, -18);
        const int16x8_t half = vdupq_n_s16(128);
        cu = vaddq_s16(vshrq_n_s16(vaddq_s16(cu, half), 8), half);
        cv = vaddq_s16(vshrq_n_s16(vaddq_s16(cv, half), 8), half);
        uint8x8x2_t uv;
        uv.val[0] = vmovn_u16(vreinterpretq_u16_s16(cu));
        uv.val[1] = vmovn_u16(vreinterpretq_u16_s16(cv));
        vst2_u8(out + x, uv);
    }
#elif FRAMESCALER_SSE2
    const __m128i low = _mm_set1_epi32(0x00FF00FF);
    // 16-bit lanes of a block's (R, B) twice: U = -38 R + 112 B in the first dword, V = 112 R - 18 B in the second;
    // (B, R) with Bgra
    const __m128i coefRB = Bgra ? _mm_set_epi16(112, -18, -38, 112, 112, -18, -38, 112)
                                : _mm_set_epi16(-18, 112, 112, -38, -18, 112, 112, -38);
    const __m128i coefGA = _mm_set_epi16(0, -94, 0, -74, 0, -94, 0, -74);
    const __m128i half = _mm_set1_epi32(128);
    // Two blocks (4 pixels of each row) -> U0, V0, U1, V1 as dwords
    auto blocks2 = [&](const uint8_t* p0, const uint8_t* p1) {
        const __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p0));
        const __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p1));
        __m128i rb = _mm_add_epi16(_mm_and_si128(a, low), _mm_and_si128(b, low));
        __m128i ga = _mm_add_epi16(_mm_srli_epi16(a, 8), _mm_srli_epi16(b, 8));
        rb = _mm_add_epi16(rb, _mm_srli_epi64(rb, 32));  // the left pixel + the right one, in the low dword
        ga = _mm_add_epi16(ga, _mm_srli_epi64(ga, 32));
        rb = _mm_shuffle_epi32(_mm_srli_epi16(rb, 2), _MM_SHUFFLE(2, 2, 0, 0));
        ga = _mm_shuffle_epi32(_mm_srli_epi16(ga, 2), _MM_SHUFFLE(2, 2, 0, 0));
        const __m128i sum = _mm_add_epi32(_mm_madd_epi16(rb, coefRB), _mm_madd_epi16(ga, coefGA));
        return _mm_add_epi32(_mm_srai_epi32(_mm_add_epi32(sum, half), 8), half);
    };
    for (; x + 16 <= width; x += 16)
    {
        const uint8_t* p0 = row0 + static_cast<size_t>(x) * 4;
        const uint8_t* p1 = row1 + static_cast<size_t>(x) * 4;
        const __m128i a = _mm_packs_epi32(blocks2(p0, p1), blocks2(p0 + 16, p1 + 16));
        const __m128i b = _mm_packs_epi32(blocks2(p0 + 32, p1 + 32), blocks2(p0 + 48, p1 + 48));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + x), _mm_packus_epi16(a, b));
    }
#endif
    for (; x < width; x += 2)
    {
        const int r = (row0[x * 4 + R] + row0[x * 4 + 4 + R] + row1[x * 4 + R] + row1[x * 4 + 4 + R]) / 4;
        const int g = (row0[x * 4 + 1] + row0[x * 4 + 5] + row1[x * 4 + 1] + row1[x * 4 + 5]) / 4;
        const int b = (row0[x * 4 + B] + row0[x * 4 + 4 + B] + row1[x * 4 + B] + row1[x * 4 + 4 + B]) / 4;
        ChromaOf(r, g, b, out[x], out[x + 1]);
    }
}
}  // namespace

namespace
{
/// The chroma plane of a picture whose output pixel (x, y) is source pixel (col[x], row[y]), -1 = a bar (black): any
/// nearest mapping, whole factor or not. Each 2x2 block is the average of its four source pixels, as the full-frame
/// conversion of the scaled picture computes it. A source row is reduced once to the R, G, B sums of the two pixels
/// under each chroma column; a block row is then the sum of two such rows, / 4, through the vector chroma kernel
class ChromaPlaneBuilder
{
public:
    ChromaPlaneBuilder(const uint8_t* src, uint32_t srcW, const int* col0, const int* col1, uint32_t chromaW)
        : _src(src), _srcW(srcW), _col0(col0), _col1(col1), _chromaW(chromaW)
    {
        for (Slot& slot : _slots)
        {
            slot.row = -2;
            slot.sums.resize(static_cast<size_t>(chromaW) * 3);
        }
    }

    /// One U,V row from the source rows under the block's two output rows
    void BuildRow(int row0, int row1, uint8_t* out)
    {
        const uint16_t* a = Sums(row0);
        const uint16_t* b = Sums(row1);
        const size_t n = _chromaW;
        ChromaFromSums(a, a + n, a + 2 * n, b, b + n, b + 2 * n, _chromaW, out);
    }

private:
    struct Slot
    {
        int row;
        std::vector<uint16_t> sums;  ///< R sums, then G, then B: one per chroma column
    };

    /// The sums of a source row, from the two most recent ones (rows advance) or computed into the older slot
    const uint16_t* Sums(int row)
    {
        for (Slot& slot : _slots)
            if (slot.row == row)
                return slot.sums.data();
        Slot& slot = _slots[_next];
        _next ^= 1;
        slot.row = row;
        uint16_t* r = slot.sums.data();
        uint16_t* g = r + _chromaW;
        uint16_t* b = g + _chromaW;
        if (row < 0)
        {
            std::fill(slot.sums.begin(), slot.sums.end(), uint16_t{0});  // a bar row adds black
            return r;
        }
        const uint8_t* line = _src + static_cast<size_t>(row) * _srcW * 4;
        for (uint32_t cx = 0; cx < _chromaW; cx++)
        {
            const int c0 = _col0[cx];
            const int c1 = _col1[cx];
            unsigned sr = 0, sg = 0, sb = 0;
            if (c0 >= 0)
            {
                const uint8_t* p = line + static_cast<size_t>(c0) * 4;
                sr += p[0];
                sg += p[1];
                sb += p[2];
            }
            if (c1 >= 0)
            {
                const uint8_t* p = line + static_cast<size_t>(c1) * 4;
                sr += p[0];
                sg += p[1];
                sb += p[2];
            }
            r[cx] = static_cast<uint16_t>(sr);
            g[cx] = static_cast<uint16_t>(sg);
            b[cx] = static_cast<uint16_t>(sb);
        }
        return r;
    }

    /// (sum of row 0 + sum of row 1) / 4 per channel -> interleaved U, V (the sums are at most 2 x 2 x 255)
    static void ChromaFromSums(const uint16_t* FRAMESCALER_RESTRICT r0, const uint16_t* FRAMESCALER_RESTRICT g0,
                               const uint16_t* FRAMESCALER_RESTRICT b0, const uint16_t* FRAMESCALER_RESTRICT r1,
                               const uint16_t* FRAMESCALER_RESTRICT g1, const uint16_t* FRAMESCALER_RESTRICT b1,
                               uint32_t count, uint8_t* FRAMESCALER_RESTRICT out)
    {
        uint32_t x = 0;
#if FRAMESCALER_NEON
        for (; x + 8 <= count; x += 8)
        {
            const int16x8_t r = vreinterpretq_s16_u16(vshrq_n_u16(vaddq_u16(vld1q_u16(r0 + x), vld1q_u16(r1 + x)), 2));
            const int16x8_t g = vreinterpretq_s16_u16(vshrq_n_u16(vaddq_u16(vld1q_u16(g0 + x), vld1q_u16(g1 + x)), 2));
            const int16x8_t b = vreinterpretq_s16_u16(vshrq_n_u16(vaddq_u16(vld1q_u16(b0 + x), vld1q_u16(b1 + x)), 2));
            int16x8_t cu = vmulq_n_s16(b, 112);
            cu = vmlaq_n_s16(cu, r, -38);
            cu = vmlaq_n_s16(cu, g, -74);
            int16x8_t cv = vmulq_n_s16(r, 112);
            cv = vmlaq_n_s16(cv, g, -94);
            cv = vmlaq_n_s16(cv, b, -18);
            const int16x8_t half = vdupq_n_s16(128);
            cu = vaddq_s16(vshrq_n_s16(vaddq_s16(cu, half), 8), half);
            cv = vaddq_s16(vshrq_n_s16(vaddq_s16(cv, half), 8), half);
            uint8x8x2_t uv;
            uv.val[0] = vmovn_u16(vreinterpretq_u16_s16(cu));
            uv.val[1] = vmovn_u16(vreinterpretq_u16_s16(cv));
            vst2_u8(out + 2 * static_cast<size_t>(x), uv);
        }
#elif FRAMESCALER_SSE2
        const __m128i half = _mm_set1_epi16(128);
        auto load = [](const uint16_t* p) { return _mm_loadu_si128(reinterpret_cast<const __m128i*>(p)); };
        for (; x + 8 <= count; x += 8)
        {
            const __m128i r = _mm_srli_epi16(_mm_add_epi16(load(r0 + x), load(r1 + x)), 2);
            const __m128i g = _mm_srli_epi16(_mm_add_epi16(load(g0 + x), load(g1 + x)), 2);
            const __m128i b = _mm_srli_epi16(_mm_add_epi16(load(b0 + x), load(b1 + x)), 2);
            __m128i cu = _mm_add_epi16(_mm_mullo_epi16(b, _mm_set1_epi16(112)),
                                       _mm_add_epi16(_mm_mullo_epi16(r, _mm_set1_epi16(-38)),
                                                     _mm_mullo_epi16(g, _mm_set1_epi16(-74))));
            __m128i cv = _mm_add_epi16(_mm_mullo_epi16(r, _mm_set1_epi16(112)),
                                       _mm_add_epi16(_mm_mullo_epi16(g, _mm_set1_epi16(-94)),
                                                     _mm_mullo_epi16(b, _mm_set1_epi16(-18))));
            cu = _mm_add_epi16(_mm_srai_epi16(_mm_add_epi16(cu, half), 8), half);
            cv = _mm_add_epi16(_mm_srai_epi16(_mm_add_epi16(cv, half), 8), half);
            const __m128i u8 = _mm_packus_epi16(cu, cu);
            const __m128i v8 = _mm_packus_epi16(cv, cv);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 2 * static_cast<size_t>(x)), _mm_unpacklo_epi8(u8, v8));
        }
#endif
        for (; x < count; x++)
            ChromaOf((r0[x] + r1[x]) / 4, (g0[x] + g1[x]) / 4, (b0[x] + b1[x]) / 4, out[2 * x], out[2 * x + 1]);
    }

    const uint8_t* _src;
    uint32_t _srcW;
    const int* _col0;
    const int* _col1;
    uint32_t _chromaW;
    Slot _slots[2];
    int _next = 0;
};
}  // namespace

namespace
{
template <bool Bgra>
void PackedToNv12Rows(const uint8_t* rgba, size_t srcStride, uint32_t w, uint32_t h, uint8_t* yDst, size_t yStride,
                      uint8_t* uvDst, size_t uvStride)
{
    for (uint32_t y = 0; y < h; y++)
        LumaRow<Bgra>(rgba + static_cast<size_t>(y) * srcStride, w, yDst + static_cast<size_t>(y) * yStride);
    for (uint32_t y = 0; y < h; y += 2)
        ChromaBlockRow<Bgra>(rgba + static_cast<size_t>(y) * srcStride, rgba + static_cast<size_t>(y + 1) * srcStride,
                             w, uvDst + static_cast<size_t>(y / 2) * uvStride);
}
}  // namespace

void PackedToNv12(const uint8_t* rgba, size_t srcStride, uint32_t w, uint32_t h, uint8_t* yDst, size_t yStride,
                  uint8_t* uvDst, size_t uvStride, bool bgra)
{
    if (bgra)
        PackedToNv12Rows<true>(rgba, srcStride, w, h, yDst, yStride, uvDst, uvStride);
    else
        PackedToNv12Rows<false>(rgba, srcStride, w, h, yDst, yStride, uvDst, uvStride);
}

namespace
{
/// The NV12 picture at a factor that is not whole (352x288 in 3840x2160 is 7.5x): the same nearest mapping as
/// DrawPicture - output pixel (x, y) of the picture is source pixel (x * srcW / width, y * srcH / height) - with the
/// color conversion done once per distinct source row, and the 2x2 chroma blocks averaged under the output pixels
/// exactly as the full-frame conversion does (byte-identical to scaling to RGBA and converting the whole frame)
bool ScaleIntoNv12Fit(const uint8_t* src, uint32_t srcW, uint32_t srcH, const Layout& layout, uint8_t* yDst,
                      size_t yStride, uint8_t* uvDst, size_t uvStride, uint32_t dstW, uint32_t dstH)
{
    const uint32_t picX0 = layout.offsetX;
    const uint32_t picX1 = layout.offsetX + layout.width;
    const uint32_t picY0 = layout.offsetY;
    const uint32_t picY1 = layout.offsetY + layout.height;

    // Source column under each output column of the picture; source row under each output row (-1 = a bar)
    thread_local std::vector<uint32_t> columnMap;
    columnMap.resize(layout.width);
    for (uint32_t x = 0; x < layout.width; x++)
        columnMap[x] = static_cast<uint32_t>(static_cast<uint64_t>(x) * srcW / layout.width);
    auto rowIndex = [&](uint32_t y) -> int {
        return (y < picY0 || y >= picY1)
                   ? -1
                   : static_cast<int>(static_cast<uint64_t>(y - picY0) * srcH / layout.height);
    };
    auto colIndex = [&](uint32_t x) -> int { return (x < picX0 || x >= picX1) ? -1 : static_cast<int>(columnMap[x - picX0]); };

    // ---- Y plane: a source row converted once, gathered once, repeated for the output rows that map to it ----
    thread_local std::vector<uint8_t> lumaRow;
    lumaRow.resize(srcW);
    int previousRow = -2;
    for (uint32_t y = 0; y < dstH; y++)
    {
        uint8_t* out = yDst + static_cast<size_t>(y) * yStride;
        const int sy = rowIndex(y);
        if (sy < 0)
        {
            std::memset(out, 16, dstW);
            previousRow = -2;
            continue;
        }
        if (sy == previousRow)
        {
            std::memcpy(out, yDst + static_cast<size_t>(y - 1) * yStride, dstW);
            continue;
        }
        previousRow = sy;
        const uint8_t* row = src + static_cast<size_t>(sy) * srcW * 4;
        LumaRow(row, srcW, lumaRow.data());
        std::memset(out, 16, picX0);
        uint8_t* d = out + picX0;
        for (uint32_t x = 0; x < layout.width; x++)
            d[x] = lumaRow[columnMap[x]];
        std::memset(out + picX1, 16, dstW - picX1);
    }

    // ---- UV plane ----
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
    ChromaPlaneBuilder chroma(src, srcW, col0.data(), col1.data(), chromaW);
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
        if (r0 < 0 && r1 < 0)
            std::memset(out, 128, dstW);  // a bar row: black
        else
            chroma.BuildRow(r0, r1, out);
    }
    return true;
}
}  // namespace

bool ScaleIntoNv12(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint8_t* yDst, size_t yStride, uint8_t* uvDst,
                   size_t uvStride, uint32_t dstW, uint32_t dstH)
{
    const Layout layout = ComputeLayout(srcW, srcH, dstW, dstH);
    if (!src || !yDst || !uvDst || layout.width == 0 || (dstW & 1) || (dstH & 1) || yStride < dstW ||
        uvStride < dstW)
        return false;

    if (layout.scale == 0)
        return ScaleIntoNv12Fit(src, srcW, srcH, layout, yDst, yStride, uvDst, uvStride, dstW, dstH);

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
                LumaRow(row, srcW, d);
            }
            else
            {
                LumaRow(row, srcW, lumaRow.data());
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
    ChromaPlaneBuilder chroma(src, srcW, col0.data(), col1.data(), chromaW);

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
        // Even factor and the picture on even coordinates: every 2x2 block is inside one source pixel, so the row is
        // the source row's (U,V) pairs, converted once per source pixel, repeated k / 2 times between two bars
        if (r0 == r1 && r0 >= 0 && (k & 1) == 0 && (picX0 & 1) == 0)
        {
            ChromaRow(src + static_cast<size_t>(r0) * srcW * 4, srcW, chromaU.data(), chromaV.data());
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

        // Odd factor or the picture on odd coordinates: blocks straddle source pixels or bars
        if (r0 < 0 && r1 < 0)
            std::memset(out, 128, dstW);  // a bar row: black
        else
            chroma.BuildRow(r0, r1, out);
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

    DrawPicture(srcWords, srcW, srcH, layout, outWords, dstStride, false);

    return _output.data();
}
}  // namespace FrameScaler
