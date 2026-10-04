// libsam2695 - exact 64 x 64 -> 128-bit products and quotients for the time axes.
//
// Host time t (ticks at hostTickRate H) maps to grid index n of a rate R by n = t * R / H. Products
// reach 2^64 after a few hours of host time, so they are formed in 128 bits. Portable C++ (no
// __int128, which MSVC lacks); only used per MIDI byte and per UART sample, never per audio sample.
#pragma once

#include <cstdint>

namespace sam2695
{

struct U128
{
    uint64_t hi = 0;
    uint64_t lo = 0;
};

inline U128 Mul64(uint64_t a, uint64_t b)
{
    const uint64_t aLo = a & 0xFFFFFFFFu, aHi = a >> 32;
    const uint64_t bLo = b & 0xFFFFFFFFu, bHi = b >> 32;
    const uint64_t ll = aLo * bLo;
    const uint64_t lh = aLo * bHi;
    const uint64_t hl = aHi * bLo;
    const uint64_t hh = aHi * bHi;
    const uint64_t mid = (ll >> 32) + (lh & 0xFFFFFFFFu) + (hl & 0xFFFFFFFFu);
    U128 r;
    r.lo = (ll & 0xFFFFFFFFu) | (mid << 32);
    r.hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    return r;
}

inline bool Less(const U128& a, const U128& b)
{
    return a.hi != b.hi ? a.hi < b.hi : a.lo < b.lo;
}

// floor(n / d) and the remainder; saturates the quotient at UINT64_MAX on overflow.
inline uint64_t Div128(const U128& n, uint64_t d, uint64_t* remainder = nullptr)
{
    if (n.hi >= d)
    {
        if (remainder != nullptr)
            *remainder = 0;
        return UINT64_MAX;
    }
    uint64_t rem = n.hi;
    uint64_t q = 0;
    for (int i = 63; i >= 0; i--)
    {
        const uint64_t carry = rem >> 63;
        rem = (rem << 1) | ((n.lo >> i) & 1u);
        q <<= 1;
        if (carry != 0 || rem >= d)
        {
            rem -= d;
            q |= 1u;
        }
    }
    if (remainder != nullptr)
        *remainder = rem;
    return q;
}

// floor(a * b / c)
inline uint64_t MulDivFloor(uint64_t a, uint64_t b, uint64_t c)
{
    return Div128(Mul64(a, b), c);
}

// ceil(a * b / c)
inline uint64_t MulDivCeil(uint64_t a, uint64_t b, uint64_t c)
{
    uint64_t rem = 0;
    const uint64_t q = Div128(Mul64(a, b), c, &rem);
    return rem != 0 && q != UINT64_MAX ? q + 1 : q;
}

} // namespace sam2695
