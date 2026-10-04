// libsam2695 - SHA-256 (FIPS 180-4), the bank identity stored in every state blob.
#include "bank/sha256.h"

#include <algorithm>
#include <cstring>

namespace sam2695
{

namespace
{

constexpr uint32_t kRound[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void Compress(uint32_t h[8], const uint8_t* block)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
               (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
    for (int i = 16; i < 64; i++)
    {
        const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++)
    {
        const uint32_t s1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = hh + s1 + ch + kRound[i] + w[i];
        const uint32_t s0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

} // namespace

void Sha256Stream::Reset()
{
    static constexpr uint32_t kInit[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::memcpy(_h, kInit, sizeof(_h));
    _used = 0;
    _total = 0;
}

void Sha256Stream::Update(const uint8_t* data, size_t size)
{
    _total += size;
    if (_used > 0)
    {
        const size_t take = std::min(size, sizeof(_block) - _used);
        std::memcpy(_block + _used, data, take);
        _used += take;
        data += take;
        size -= take;
        if (_used < sizeof(_block))
            return;
        Compress(_h, _block);
        _used = 0;
    }
    for (; size >= 64; data += 64, size -= 64)
        Compress(_h, data);
    if (size > 0)
    {
        std::memcpy(_block, data, size);
        _used = size;
    }
}

BankDigest Sha256Stream::Final()
{
    uint8_t tail[128] = {};
    std::memcpy(tail, _block, _used);
    tail[_used] = 0x80;
    const size_t tailLen = _used + 1 + 8 <= 64 ? 64 : 128;
    const uint64_t bits = _total * 8u;
    for (int i = 0; i < 8; i++)
        tail[tailLen - 1 - i] = static_cast<uint8_t>(bits >> (8 * i));
    Compress(_h, tail);
    if (tailLen == 128)
        Compress(_h, tail + 64);
    BankDigest out{};
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 4; j++)
            out[i * 4 + j] = static_cast<uint8_t>(_h[i] >> (24 - 8 * j));
    Reset();
    return out;
}

BankDigest Sha256(const uint8_t* data, size_t size)
{
    Sha256Stream s;
    if (size > 0)
        s.Update(data, size);
    return s.Final();
}

std::string DigestHex(const BankDigest& d)
{
    static const char* kHex = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (uint8_t b : d)
    {
        s.push_back(kHex[b >> 4]);
        s.push_back(kHex[b & 15]);
    }
    return s;
}

} // namespace sam2695
