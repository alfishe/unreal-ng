// POC 032 stage 2, step S1: TooJpeg (Stephan Brumme, zlib licence, see ../3rdparty/toojpeg/LICENSE
// and below) with only the output side changed: the stream goes into a vector instead of a per-byte callback, bits
// collect in a 64-bit buffer and leave 32 at a time (with a fast path for words without an 0xFF byte), the value bit
// length comes from std::countl_zero, zero runs from a bit mask with std::countr_zero (as libjpeg-turbo's jchuff.c),
// and the Huffman tables are built once at compile time. The arithmetic is TooJpeg's
// (float AAN DCT, float colour conversion per block). Must be byte-identical to TooJpeg (4:4:4, RGB, no comment).
//
// Derived from toojpeg.cpp, Copyright (c) 2011-2016 Stephan Brumme. This software is provided 'as-is', without any
// express or implied warranty. In no event will the authors be held liable for any damages arising from the use of
// this software. Permission is granted to anyone to use this software for any purpose, including commercial
// applications, and to alter it and redistribute it freely, subject to the following restrictions: 1. The origin of
// this software must not be misrepresented; you must not claim that you wrote the original software. If you use this
// software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original
// software. 3. This notice may not be removed or altered from any source distribution.
// Altered: unreal-ng, 2026-10-20 (the changes listed above).
#include "toojpegstep1.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

namespace poc032::step1
{
namespace
{

constexpr std::uint8_t kQuantLuma[64] = {16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,
                                         14, 13, 16, 24, 40,  57,  69,  56,  14, 17, 22, 29, 51,  87,  80,  62,
                                         18, 22, 37, 56, 68,  109, 103, 77,  24, 35, 55, 64, 81,  104, 113, 92,
                                         49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
constexpr std::uint8_t kQuantChroma[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
                                           24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                                           99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                           99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};
constexpr std::uint8_t kZigZagInv[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
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
using Table = std::array<Code, 256>;

constexpr Table MakeTable(const std::uint8_t* counts, const std::uint8_t* values)
{
    Table t{};
    int code = 0;
    for (int bits = 1; bits <= 16; ++bits)
    {
        for (int i = 0; i < counts[bits - 1]; ++i)
            t[*values++] = {static_cast<std::uint16_t>(code++), static_cast<std::uint8_t>(bits)};
        code <<= 1;
    }
    return t;
}
constexpr Table kDcLuma = MakeTable(kDcLumaBits, kDcValues);
constexpr Table kAcLuma = MakeTable(kAcLumaBits, kAcLumaValues);
constexpr Table kDcChroma = MakeTable(kDcChromaBits, kDcValues);
constexpr Table kAcChroma = MakeTable(kAcChromaBits, kAcChromaValues);

class Writer
{
public:
    explicit Writer(std::vector<std::uint8_t>& out) : _out(out) {}
    void Reserve(size_t n)  // room for n more bytes behind _pos
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
        Reserve(4);
        Byte(0xFF);
        Byte(id);
        Byte(static_cast<std::uint8_t>(length >> 8));
        Byte(static_cast<std::uint8_t>(length & 0xFF));
    }
    // n <= 32 bits; the caller reserved 8 bytes per 32 bits it may emit (stuffing doubles them at worst)
    void Bits(std::uint32_t value, int n)
    {
        _acc = (_acc << n) | value;
        _count += n;
        if (_count >= 32)
        {
            _count -= 32;
            const auto w = static_cast<std::uint32_t>(_acc >> _count);
            const std::uint32_t inv = ~w;  // a 0xFF byte in w is a zero byte in inv
            if (((inv - 0x01010101u) & ~inv & 0x80808080u) == 0)
            {
                _out[_pos] = static_cast<std::uint8_t>(w >> 24);
                _out[_pos + 1] = static_cast<std::uint8_t>(w >> 16);
                _out[_pos + 2] = static_cast<std::uint8_t>(w >> 8);
                _out[_pos + 3] = static_cast<std::uint8_t>(w);
                _pos += 4;
            }
            else
                for (int s = 24; s >= 0; s -= 8)
                {
                    const auto b = static_cast<std::uint8_t>(w >> s);
                    _out[_pos++] = b;
                    if (b == 0xFF)
                        _out[_pos++] = 0;
                }
        }
    }
    void Flush()  // TooJpeg's flush: seven 1 bits, then every whole byte
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

// value -> (bits of |value|, JPEG's one's-complement code for negatives)
inline Code ValueCode(int v)
{
    const auto mag = static_cast<std::uint32_t>(v < 0 ? -v : v);
    const int bits = 32 - std::countl_zero(mag);
    const int code = v + ((v >> 31) & ((1 << bits) - 1));
    return {static_cast<std::uint16_t>(code), static_cast<std::uint8_t>(bits)};
}

inline void Put(Writer& w, const Code& huff, const Code& val)
{
    w.Bits((static_cast<std::uint32_t>(huff.code) << val.bits) | val.code, huff.bits + val.bits);
}

void Dct(float* block, int stride)  // TooJpeg's AAN float DCT, unchanged
{
    const float SqrtHalfSqrt = 1.306562965f, InvSqrt = 0.707106781f, HalfSqrtSqrt = 0.382683432f,
                InvSqrtSqrt = 0.541196100f;
    float& b0 = block[0];
    float& b1 = block[1 * stride];
    float& b2 = block[2 * stride];
    float& b3 = block[3 * stride];
    float& b4 = block[4 * stride];
    float& b5 = block[5 * stride];
    float& b6 = block[6 * stride];
    float& b7 = block[7 * stride];
    const float add07 = b0 + b7, sub07 = b0 - b7, add16 = b1 + b6, sub16 = b1 - b6, add25 = b2 + b5, sub25 = b2 - b5,
                add34 = b3 + b4, sub34 = b3 - b4;
    const float add0347 = add07 + add34, sub07_34 = add07 - add34, add1256 = add16 + add25, sub16_25 = add16 - add25;
    b0 = add0347 + add1256;
    b4 = add0347 - add1256;
    const float z1 = (sub16_25 + sub07_34) * InvSqrt;
    b2 = sub07_34 + z1;
    b6 = sub07_34 - z1;
    const float sub23_45 = sub25 + sub34, sub12_56 = sub16 + sub25, sub01_67 = sub16 + sub07;
    const float z5 = (sub23_45 - sub01_67) * HalfSqrtSqrt;
    const float z2 = sub23_45 * InvSqrtSqrt + z5;
    const float z3 = sub12_56 * InvSqrt;
    const float z4 = sub01_67 * SqrtHalfSqrt + z5;
    const float z6 = sub07 + z3, z7 = sub07 - z3;
    b1 = z6 + z4;
    b7 = z6 - z4;
    b5 = z7 + z2;
    b3 = z7 - z2;
}

int EncodeBlock(Writer& w, float block[64], const float scaled[64], int lastDc, const Table& dc, const Table& ac)
{
    w.Reserve(64 * 8 + 16);
    for (int off = 0; off < 8; ++off)
        Dct(block + off * 8, 1);
    for (int off = 0; off < 8; ++off)
        Dct(block + off, 8);
    for (int i = 0; i < 64; ++i)
        block[i] *= scaled[i];
    const int dcValue = static_cast<int>(block[0] + (block[0] >= 0 ? +0.5f : -0.5f));
    int last = 0;
    std::int16_t q[64];
    for (int i = 1; i < 64; ++i)
    {
        const float v = block[kZigZagInv[i]];
        q[i] = static_cast<std::int16_t>(static_cast<int>(v + (v >= 0 ? +0.5f : -0.5f)));
        if (q[i] != 0)
            last = i;
    }
    const int diff = dcValue - static_cast<std::int16_t>(lastDc);
    if (diff == 0)
        w.Bits(dc[0].code, dc[0].bits);
    else
    {
        const Code v = ValueCode(diff);
        Put(w, dc[v.bits], v);
    }
    // zero runs from a bit mask of the non-zero coefficients (as libjpeg-turbo's jchuff.c): ZRL per 16 zeros, EOB
    std::uint64_t mask = 0;
    for (int i = 1; i <= last; ++i)
        mask |= static_cast<std::uint64_t>(q[i] != 0) << i;
    int prev = 0;
    while (mask)
    {
        const int i = std::countr_zero(mask);
        mask &= mask - 1;
        int run = i - prev - 1;
        for (; run >= 16; run -= 16)
            w.Bits(ac[0xF0].code, ac[0xF0].bits);
        const Code v = ValueCode(q[i]);
        Put(w, ac[(run << 4) + v.bits], v);
        prev = i;
    }
    if (last < 63)
        w.Bits(ac[0].code, ac[0].bits);
    return dcValue;
}

}  // namespace

std::vector<std::uint8_t> Encode(const std::uint8_t* px, int width, int height, int quality)
{
    std::vector<std::uint8_t> out;
    if (!px || width <= 0 || height <= 0 || width > 65535 || height > 65535)
        return out;
    out.resize(static_cast<size_t>(width) * height / 2 + 1024);
    Writer w(out);
    static constexpr std::uint8_t kHeader[20] = {0xFF, 0xD8, 0xFF, 0xE0, 0, 16, 'J', 'F', 'I', 'F',
                                                 0,    1,    1,    0,    0, 1,  0,   1,   0,   0};
    w.Bytes(kHeader, sizeof kHeader);

    const int qs = std::clamp(quality, 1, 100);
    const int scale = qs < 50 ? 5000 / qs : 200 - qs * 2;
    std::uint8_t ql[64], qc[64];
    for (int i = 0; i < 64; ++i)
    {
        ql[i] = static_cast<std::uint8_t>(std::clamp((kQuantLuma[kZigZagInv[i]] * scale + 50) / 100, 1, 255));
        qc[i] = static_cast<std::uint8_t>(std::clamp((kQuantChroma[kZigZagInv[i]] * scale + 50) / 100, 1, 255));
    }
    w.Marker(0xDB, 2 + 2 * 65);
    w.Reserve(130);
    w.Byte(0);
    w.Bytes(ql, 64);
    w.Byte(1);
    w.Bytes(qc, 64);

    w.Marker(0xC0, 2 + 6 + 9);
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

    w.Marker(0xC4, 2 + 208 + 208);
    const std::uint8_t tc0 = 0x00, tc1 = 0x10, tc2 = 0x01, tc3 = 0x11;
    w.Bytes(&tc0, 1);
    w.Bytes(kDcLumaBits, 16);
    w.Bytes(kDcValues, 12);
    w.Bytes(&tc1, 1);
    w.Bytes(kAcLumaBits, 16);
    w.Bytes(kAcLumaValues, 162);
    w.Bytes(&tc2, 1);
    w.Bytes(kDcChromaBits, 16);
    w.Bytes(kDcValues, 12);
    w.Bytes(&tc3, 1);
    w.Bytes(kAcChromaBits, 16);
    w.Bytes(kAcChromaValues, 162);

    w.Marker(0xDA, 2 + 1 + 6 + 3);
    static constexpr std::uint8_t kSos[10] = {3, 1, 0x00, 2, 0x11, 3, 0x11, 0, 63, 0};
    w.Bytes(kSos, sizeof kSos);

    static constexpr float kAan[8] = {1, 1.387039845f, 1.306562965f, 1.175875602f,
                                      1, 0.785694958f, 0.541196100f, 0.275899379f};
    float sl[64], sc[64];
    for (int i = 0; i < 64; ++i)
    {
        const int row = kZigZagInv[i] / 8, col = kZigZagInv[i] % 8;
        const float factor = 1 / (kAan[row] * kAan[col] * 8);
        sl[kZigZagInv[i]] = factor / ql[i];
        sc[kZigZagInv[i]] = factor / qc[i];
    }

    const int maxW = width - 1, maxH = height - 1;
    int lastY = 0, lastCb = 0, lastCr = 0;
    float y[64], cb[64], cr[64];
    for (int my = 0; my < height; my += 8)
        for (int mx = 0; mx < width; mx += 8)
        {
            for (int dy = 0; dy < 8; ++dy)
            {
                int col = std::min(mx, maxW);
                const int row = std::min(my + dy, maxH);
                for (int dx = 0; dx < 8; ++dx)
                {
                    const int pos = row * width + col;
                    if (col < maxW)
                        ++col;
                    const float r = px[3 * pos], g = px[3 * pos + 1], b = px[3 * pos + 2];
                    y[dy * 8 + dx] = (+0.299f * r + 0.587f * g + 0.114f * b) - 128;
                    cb[dy * 8 + dx] = -0.16874f * r - 0.33126f * g + 0.5f * b;
                    cr[dy * 8 + dx] = +0.5f * r - 0.41869f * g - 0.08131f * b;
                }
            }
            lastY = EncodeBlock(w, y, sl, lastY, kDcLuma, kAcLuma);
            lastCb = EncodeBlock(w, cb, sc, lastCb, kDcChroma, kAcChroma);
            lastCr = EncodeBlock(w, cr, sc, lastCr, kDcChroma, kAcChroma);
        }
    w.Flush();
    w.Reserve(2);
    w.Byte(0xFF);
    w.Byte(0xD9);
    w.Finish();
    return out;
}

}  // namespace poc032::step1
