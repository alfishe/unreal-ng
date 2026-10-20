// Baseline JPEG encoder (sdb/jpeg.h). Stream layout, tables and entropy coding follow TooJpeg; the arithmetic follows
// libjpeg / libjpeg-turbo: fixed-point RGB -> YCbCr (jccolor.c), the "islow" integer DCT (jfdctint.c), quantisation
// with rounding as jcdctmgr.c (here by an exact reciprocal multiply), zero runs from a bit mask (jchuff.c). With the
// same quality the quantised coefficients equal libjpeg-turbo's, so the scan data is the same. Loops are shaped for the
// compiler's auto-vectoriser (lanes = the 8 rows or columns of a block, whole rows for the colour conversion).
// Measured against libjpeg-turbo and TooJpeg in POC 032 (tools/poc/032-streamdeck-jpeg-encoder).
//
// Derived from TooJpeg (toojpeg.cpp), Copyright (c) 2011-2016 Stephan Brumme, zlib licence:
//   This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held
//   liable for any damages arising from the use of this software. Permission is granted to anyone to use this
//   software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject
//   to the following restrictions:
//   1. The origin of this software must not be misrepresented; you must not claim that you wrote the original
//      software. If you use this software in a product, an acknowledgment in the product documentation would be
//      appreciated but is not required.
//   2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original
//      software.
//   3. This notice may not be removed or altered from any source distribution.
// Altered by unreal-ng (2026-10-20): rewritten around a buffered 64-bit bit writer, integer arithmetic from libjpeg
// (Independent JPEG Group, IJG licence; libjpeg-turbo, BSD-3-Clause / IJG) as listed above; 4:4:4 RGB only.
#include "sdb/jpeg.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

namespace sdb::jpeg
{
namespace
{

// ---------------------------------------------------------------- tables (JPEG standard, Annex K)

constexpr std::uint8_t kQuantLuma[64] = {16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
                                         14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
                                         18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
                                         49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
constexpr std::uint8_t kQuantChroma[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
                                           24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                                           99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                           99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};
// zig-zag position -> natural (row-major) position
constexpr std::uint8_t kZigZag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                                      12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                                      35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                                      58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
constexpr std::uint8_t kDcLumaBits[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
constexpr std::uint8_t kDcValues[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
constexpr std::uint8_t kAcLumaBits[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 125};
constexpr std::uint8_t kAcLumaValues[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
    0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3,
    0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3,
    0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA};
constexpr std::uint8_t kDcChromaBits[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
constexpr std::uint8_t kAcChromaBits[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 119};
constexpr std::uint8_t kAcChromaValues[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
    0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1,
    0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A,
    0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA,
    0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA};

struct Code
{
    std::uint16_t code = 0;
    std::uint8_t bits = 0;
};
using HuffTable = std::array<Code, 256>;

constexpr HuffTable MakeHuffTable(const std::uint8_t* counts, const std::uint8_t* values)
{
    HuffTable t{};
    int code = 0;
    for (int bits = 1; bits <= 16; ++bits)
    {
        for (int i = 0; i < counts[bits - 1]; ++i)
            t[*values++] = {static_cast<std::uint16_t>(code++), static_cast<std::uint8_t>(bits)};
        code <<= 1;
    }
    return t;
}
constexpr HuffTable kDcLuma = MakeHuffTable(kDcLumaBits, kDcValues);
constexpr HuffTable kAcLuma = MakeHuffTable(kAcLumaBits, kAcLumaValues);
constexpr HuffTable kDcChroma = MakeHuffTable(kDcChromaBits, kDcValues);
constexpr HuffTable kAcChroma = MakeHuffTable(kAcChromaBits, kAcChromaValues);

// ---------------------------------------------------------------- output

// Bytes go straight into the result vector; bits collect in a 64-bit accumulator and leave 32 at a time, with a fast
// path for words that hold no 0xFF byte (an 0xFF in the entropy-coded data is followed by a stuffed 0x00).
class Writer
{
public:
    explicit Writer(std::vector<std::uint8_t>& out) : _out(out) {}
    void Reserve(size_t n)
    {
        if (_pos + n > _out.size())
            _out.resize(std::max(_out.size() * 2, _pos + n));
    }
    void Byte(std::uint8_t b)
    {
        _out[_pos++] = b;
    }
    void Bytes(const std::uint8_t* p, size_t n)
    {
        Reserve(n);
        std::memcpy(&_out[_pos], p, n);
        _pos += n;
    }
    void Marker(std::uint8_t id, int length)
    {
        const std::uint8_t m[4] = {0xFF, id, static_cast<std::uint8_t>(length >> 8),
                                   static_cast<std::uint8_t>(length & 0xFF)};
        Bytes(m, 4);
    }
    // n in 1..32; space for the emitted bytes is reserved per block by the caller
    void Bits(std::uint32_t value, int n)
    {
        _acc = (_acc << n) | value;
        _count += n;
        if (_count < 32)
            return;
        _count -= 32;
        const auto word = static_cast<std::uint32_t>(_acc >> _count);
        const std::uint32_t inv = ~word;  // an 0xFF byte of word is a zero byte of inv
        if (((inv - 0x01010101u) & ~inv & 0x80808080u) == 0)
        {
            _out[_pos] = static_cast<std::uint8_t>(word >> 24);
            _out[_pos + 1] = static_cast<std::uint8_t>(word >> 16);
            _out[_pos + 2] = static_cast<std::uint8_t>(word >> 8);
            _out[_pos + 3] = static_cast<std::uint8_t>(word);
            _pos += 4;
            return;
        }
        for (int shift = 24; shift >= 0; shift -= 8)
        {
            const auto b = static_cast<std::uint8_t>(word >> shift);
            _out[_pos++] = b;
            if (b == 0xFF)
                _out[_pos++] = 0;
        }
    }
    void Flush()  // pad the last byte with 1 bits
    {
        Reserve(16);
        _acc = (_acc << 7) | 0x7F;
        _count += 7;
        while (_count >= 8)
        {
            _count -= 8;
            const auto b = static_cast<std::uint8_t>(_acc >> _count);
            _out[_pos++] = b;
            if (b == 0xFF)
                _out[_pos++] = 0;
        }
        _count = 0;
    }
    void Finish()
    {
        _out.resize(_pos);
    }

private:
    std::vector<std::uint8_t>& _out;
    size_t _pos = 0;
    std::uint64_t _acc = 0;
    int _count = 0;
};

// value -> its bit length and JPEG's code (one's complement for negatives)
inline Code ValueCode(int v)
{
    const auto magnitude = static_cast<std::uint32_t>(v < 0 ? -v : v);
    const int bits = 32 - std::countl_zero(magnitude);
    const int code = v + ((v >> 31) & ((1 << bits) - 1));
    return {static_cast<std::uint16_t>(code), static_cast<std::uint8_t>(bits)};
}

inline void PutSymbol(Writer& w, const Code& huff, const Code& value)
{
    w.Bits((static_cast<std::uint32_t>(huff.code) << value.bits) | value.code, huff.bits + value.bits);
}

// ---------------------------------------------------------------- arithmetic (libjpeg)

// RGB -> YCbCr, 16-bit fixed point (jccolor.c), then the level shift of jcdctmgr.c (-128): one row of `width`
// pixels, the right edge replicated up to `paddedWidth`.
constexpr int kScaleBits = 16;
constexpr std::int32_t kHalf = 1 << (kScaleBits - 1);
constexpr std::int32_t kRY = 19595, kGY = 38470, kBY = 7471;        // FIX(0.29900), FIX(0.58700), FIX(0.11400)
constexpr std::int32_t kRCb = -11059, kGCb = -21709, kBCb = 32768;  // -FIX(0.16874), -FIX(0.33126), FIX(0.5)
constexpr std::int32_t kRCr = 32768, kGCr = -27439, kBCr = -5329;   // FIX(0.5), -FIX(0.41869), -FIX(0.08131)

void ConvertRow(const std::uint8_t* rgb, int width, int paddedWidth, std::int16_t* y, std::int16_t* cb,
                std::int16_t* cr)
{
    // Cb, Cr: + 128 << 16 (CBCR_OFFSET) + ONE_HALF - 1, then - 128 after the shift: the two offsets cancel
    for (int x = 0; x < width; ++x)
    {
        const std::int32_t r = rgb[3 * x], g = rgb[3 * x + 1], b = rgb[3 * x + 2];
        y[x] = static_cast<std::int16_t>(((kRY * r + kGY * g + kBY * b + kHalf) >> kScaleBits) - 128);
        cb[x] = static_cast<std::int16_t>((kRCb * r + kGCb * g + kBCb * b + kHalf - 1) >> kScaleBits);
        cr[x] = static_cast<std::int16_t>((kRCr * r + kGCr * g + kBCr * b + kHalf - 1) >> kScaleBits);
    }
    for (int x = width; x < paddedWidth; ++x)
    {
        y[x] = y[width - 1];
        cb[x] = cb[width - 1];
        cr[x] = cr[width - 1];
    }
}

// One pass of the islow DCT (jfdctint.c, CONST_BITS 13, PASS1_BITS 2) over 8 lanes at once: in[i][lane] is sample i
// of the lane's 1-D signal, out[k][lane] its coefficient k. First = the row pass (scaled up by PASS1_BITS), else the
// column pass (scaled back). The lane loop is what the compiler vectorises.
constexpr int kConstBits = 13, kPass1Bits = 2;
constexpr std::int32_t kFix0298631336 = 2446, kFix0390180644 = 3196, kFix0541196100 = 4433, kFix0765366865 = 6270,
                       kFix0899976223 = 7373, kFix1175875602 = 9633, kFix1501321110 = 12299, kFix1847759065 = 15137,
                       kFix1961570560 = 16069, kFix2053119869 = 16819, kFix2562915447 = 20995, kFix3072711026 = 25172;

template <bool First>
void DctPass(const std::int32_t in[8][8], std::int32_t out[8][8])
{
    constexpr int oddShift = First ? kConstBits - kPass1Bits : kConstBits + kPass1Bits;
    constexpr std::int32_t oddRound = 1 << (oddShift - 1);
    for (int l = 0; l < 8; ++l)
    {
        const std::int32_t tmp0 = in[0][l] + in[7][l], tmp7 = in[0][l] - in[7][l];
        const std::int32_t tmp1 = in[1][l] + in[6][l], tmp6 = in[1][l] - in[6][l];
        const std::int32_t tmp2 = in[2][l] + in[5][l], tmp5 = in[2][l] - in[5][l];
        const std::int32_t tmp3 = in[3][l] + in[4][l], tmp4 = in[3][l] - in[4][l];

        const std::int32_t tmp10 = tmp0 + tmp3, tmp13 = tmp0 - tmp3, tmp11 = tmp1 + tmp2, tmp12 = tmp1 - tmp2;
        if constexpr (First)
        {
            out[0][l] = (tmp10 + tmp11) * (1 << kPass1Bits);
            out[4][l] = (tmp10 - tmp11) * (1 << kPass1Bits);
        }
        else
        {
            constexpr std::int32_t evenRound = 1 << (kPass1Bits - 1);
            out[0][l] = (tmp10 + tmp11 + evenRound) >> kPass1Bits;
            out[4][l] = (tmp10 - tmp11 + evenRound) >> kPass1Bits;
        }
        const std::int32_t z1 = (tmp12 + tmp13) * kFix0541196100;
        out[2][l] = (z1 + tmp13 * kFix0765366865 + oddRound) >> oddShift;
        out[6][l] = (z1 - tmp12 * kFix1847759065 + oddRound) >> oddShift;

        const std::int32_t z5 = (tmp4 + tmp6 + tmp5 + tmp7) * kFix1175875602;
        const std::int32_t o4 = tmp4 * kFix0298631336, o5 = tmp5 * kFix2053119869, o6 = tmp6 * kFix3072711026,
                           o7 = tmp7 * kFix1501321110;
        const std::int32_t a1 = -(tmp4 + tmp7) * kFix0899976223, a2 = -(tmp5 + tmp6) * kFix2562915447;
        const std::int32_t a3 = -(tmp4 + tmp6) * kFix1961570560 + z5, a4 = -(tmp5 + tmp7) * kFix0390180644 + z5;
        out[7][l] = (o4 + a1 + a3 + oddRound) >> oddShift;
        out[5][l] = (o5 + a2 + a4 + oddRound) >> oddShift;
        out[3][l] = (o6 + a2 + a3 + oddRound) >> oddShift;
        out[1][l] = (o7 + a1 + a4 + oddRound) >> oddShift;
    }
}

// Divisors of the islow DCT (quantval << 3) as exact reciprocals: floor((|x| + d/2) / d) = ((|x| + d/2) * r) >> 32
// for r = ceil(2^32 / d), since |x| + d/2 < 2^17 and d <= 2040 keep the error below 1/d.
struct Quantizer
{
    std::uint32_t half[64], recip[64];  // natural order
};

Quantizer MakeQuantizer(const std::uint8_t table[64])  // table in natural order
{
    Quantizer q{};
    for (int i = 0; i < 64; ++i)
    {
        const std::uint32_t d = static_cast<std::uint32_t>(table[i]) << 3;
        q.half[i] = d >> 1;
        q.recip[i] = static_cast<std::uint32_t>(((std::uint64_t{1} << 32) + d - 1) / d);
    }
    return q;
}

// samples (level-shifted, row-major, `stride` apart) -> quantised coefficients, natural order
void Transform(const std::int16_t* samples, int stride, const Quantizer& qz, std::int32_t coef[64])
{
    std::int32_t t[8][8], p1[8][8], p1t[8][8], p2[8][8];
    for (int r = 0; r < 8; ++r)  // transposed: the row pass runs with one lane per row
        for (int c = 0; c < 8; ++c)
            t[c][r] = samples[r * stride + c];
    DctPass<true>(t, p1);  // p1[k][row]
    for (int k = 0; k < 8; ++k)
        for (int r = 0; r < 8; ++r)
            p1t[r][k] = p1[k][r];
    DctPass<false>(p1t, p2);  // p2[u][k]: row-major coefficients
    const std::int32_t* flat = &p2[0][0];
    for (int i = 0; i < 64; ++i)
    {
        const std::int32_t x = flat[i], sign = x >> 31;
        const std::uint32_t magnitude = static_cast<std::uint32_t>((x ^ sign) - sign) + qz.half[i];
        const auto quotient = static_cast<std::int32_t>((std::uint64_t{magnitude} * qz.recip[i]) >> 32);
        coef[i] = (quotient ^ sign) - sign;
    }
}

int EncodeBlock(Writer& w, const std::int32_t coef[64], int lastDc, const HuffTable& dc, const HuffTable& ac)
{
    w.Reserve(64 * 8 + 16);  // 64 symbols of <= 27 bits, every byte stuffed at worst
    const int diff = coef[0] - lastDc;
    if (diff == 0)
        w.Bits(dc[0].code, dc[0].bits);
    else
    {
        const Code v = ValueCode(diff);
        PutSymbol(w, dc[v.bits], v);
    }
    std::uint64_t mask = 0;  // bit i = zig-zag coefficient i is not zero
    for (int i = 1; i < 64; ++i)
        mask |= static_cast<std::uint64_t>(coef[kZigZag[i]] != 0) << i;
    int prev = 0;
    while (mask)
    {
        const int i = std::countr_zero(mask);
        mask &= mask - 1;
        int run = i - prev - 1;
        for (; run >= 16; run -= 16)
            w.Bits(ac[0xF0].code, ac[0xF0].bits);  // ZRL: 16 zeros
        const Code v = ValueCode(coef[kZigZag[i]]);
        PutSymbol(w, ac[(run << 4) + v.bits], v);
        prev = i;
    }
    if (prev < 63)
        w.Bits(ac[0].code, ac[0].bits);  // EOB
    return coef[0];
}

void WriteHeaders(Writer& w, int width, int height, const std::uint8_t ql[64], const std::uint8_t qc[64])
{
    static constexpr std::uint8_t kJfif[20] = {0xFF, 0xD8, 0xFF, 0xE0, 0, 16, 'J', 'F', 'I', 'F',
                                               0,    1,    1,    0,    0, 1,  0,   1,   0,   0};
    w.Bytes(kJfif, sizeof kJfif);
    w.Marker(0xDB, 2 + 2 * 65);  // DQT, both tables in zig-zag order
    std::uint8_t dqt[130];
    dqt[0] = 0;
    dqt[65] = 1;
    for (int i = 0; i < 64; ++i)
    {
        dqt[1 + i] = ql[kZigZag[i]];
        dqt[66 + i] = qc[kZigZag[i]];
    }
    w.Bytes(dqt, sizeof dqt);
    w.Marker(0xC0, 2 + 6 + 9);  // SOF0: 8-bit, 3 components, all 1x1 (4:4:4), Y table 0, Cb / Cr table 1
    const std::uint8_t sof[15] = {8,
                                  static_cast<std::uint8_t>(height >> 8),
                                  static_cast<std::uint8_t>(height & 0xFF),
                                  static_cast<std::uint8_t>(width >> 8),
                                  static_cast<std::uint8_t>(width & 0xFF),
                                  3,
                                  1,
                                  0x11,
                                  0,
                                  2,
                                  0x11,
                                  1,
                                  3,
                                  0x11,
                                  1};
    w.Bytes(sof, sizeof sof);
    w.Marker(0xC4, 2 + 208 + 208);  // DHT, the four standard tables
    const std::uint8_t classes[4] = {0x00, 0x10, 0x01, 0x11};
    w.Bytes(&classes[0], 1);
    w.Bytes(kDcLumaBits, 16);
    w.Bytes(kDcValues, 12);
    w.Bytes(&classes[1], 1);
    w.Bytes(kAcLumaBits, 16);
    w.Bytes(kAcLumaValues, 162);
    w.Bytes(&classes[2], 1);
    w.Bytes(kDcChromaBits, 16);
    w.Bytes(kDcValues, 12);
    w.Bytes(&classes[3], 1);
    w.Bytes(kAcChromaBits, 16);
    w.Bytes(kAcChromaValues, 162);
    w.Marker(0xDA, 2 + 1 + 6 + 3);  // SOS: one interleaved scan, spectral 0..63
    static constexpr std::uint8_t kSos[10] = {3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0};
    w.Bytes(kSos, sizeof kSos);
}

}  // namespace

std::vector<std::uint8_t> Encode(const std::uint8_t* rgb, int width, int height, int quality)
{
    std::vector<std::uint8_t> out;
    if (!rgb || width <= 0 || height <= 0 || width > 65535 || height > 65535)
        return out;

    // libjpeg's quality scale (jcparam.c jpeg_quality_scaling), values clamped to 1..255 (baseline)
    const int q = std::clamp(quality, 1, 100);
    const int scale = q < 50 ? 5000 / q : 200 - q * 2;
    std::uint8_t ql[64], qc[64];
    for (int i = 0; i < 64; ++i)
    {
        ql[i] = static_cast<std::uint8_t>(std::clamp((kQuantLuma[i] * scale + 50) / 100, 1, 255));
        qc[i] = static_cast<std::uint8_t>(std::clamp((kQuantChroma[i] * scale + 50) / 100, 1, 255));
    }
    const Quantizer qzLuma = MakeQuantizer(ql), qzChroma = MakeQuantizer(qc);

    out.resize(static_cast<size_t>(width) * height / 2 + 1024);
    Writer w(out);
    WriteHeaders(w, width, height, ql, qc);

    // one strip of 8 rows at a time: Y, Cb, Cr planes, right edge replicated to a multiple of 8, bottom edge too
    const int paddedWidth = (width + 7) & ~7;
    std::vector<std::int16_t> planes(static_cast<size_t>(paddedWidth) * 8 * 3);
    std::int16_t* const py = planes.data();
    std::int16_t* const pcb = py + static_cast<size_t>(paddedWidth) * 8;
    std::int16_t* const pcr = pcb + static_cast<size_t>(paddedWidth) * 8;
    int lastY = 0, lastCb = 0, lastCr = 0;
    std::int32_t coef[64];
    for (int y0 = 0; y0 < height; y0 += 8)
    {
        for (int r = 0; r < 8; ++r)
        {
            const int y = std::min(y0 + r, height - 1);
            const size_t row = static_cast<size_t>(r) * paddedWidth;
            ConvertRow(rgb + static_cast<size_t>(y) * width * 3, width, paddedWidth, py + row, pcb + row, pcr + row);
        }
        for (int x0 = 0; x0 < paddedWidth; x0 += 8)
        {
            Transform(py + x0, paddedWidth, qzLuma, coef);
            lastY = EncodeBlock(w, coef, lastY, kDcLuma, kAcLuma);
            Transform(pcb + x0, paddedWidth, qzChroma, coef);
            lastCb = EncodeBlock(w, coef, lastCb, kDcChroma, kAcChroma);
            Transform(pcr + x0, paddedWidth, qzChroma, coef);
            lastCr = EncodeBlock(w, coef, lastCr, kDcChroma, kAcChroma);
        }
    }
    w.Flush();
    const std::uint8_t eoi[2] = {0xFF, 0xD9};
    w.Bytes(eoi, 2);
    w.Finish();
    return out;
}

}  // namespace sdb::jpeg
