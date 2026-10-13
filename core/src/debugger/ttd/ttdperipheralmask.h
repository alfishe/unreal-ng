#pragma once

/// @file ttdperipheralmask.h
/// @brief A set of peripheral ids (PeripheralId is a u8: 0..255).
///
/// The session file names the recorded devices by a mask. It was one u64, so
/// id 63 was the last device that could be named; this set holds every u8 id.
/// Four 64-bit words, word w holds ids 64*w .. 64*w+63. Files carry only the
/// words up to the highest one in use (WordCount), so a machine with ids
/// below 64 stores a single word, as before.
///
/// Worked example: ids {1, 5, 64} -> words[0] = 0x22, words[1] = 0x1,
/// WordCount() = 2; ids {1, 5} -> WordCount() = 1, Low() = 0x22.

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace ttd
{

class PeripheralMask
{
public:
    static constexpr size_t kWords = 4;
    static constexpr unsigned kIds = kWords * 64;

    constexpr PeripheralMask() = default;

    /// Ids 0..63 only (the former u64 mask)
    static constexpr PeripheralMask FromLow(uint64_t low)
    {
        PeripheralMask m;
        m._words[0] = low;
        return m;
    }

    constexpr void Set(uint8_t id) { _words[id >> 6] |= uint64_t(1) << (id & 63); }
    constexpr void Clear(uint8_t id) { _words[id >> 6] &= ~(uint64_t(1) << (id & 63)); }
    constexpr bool Test(uint8_t id) const { return ((_words[id >> 6] >> (id & 63)) & 1u) != 0; }

    constexpr bool Any() const
    {
        for (uint64_t w : _words)
            if (w != 0)
                return true;
        return false;
    }
    unsigned Count() const
    {
        unsigned n = 0;
        for (uint64_t w : _words)
            n += static_cast<unsigned>(std::popcount(w));
        return n;
    }

    /// Ids 0..63 as a u64 (the older automation surfaces' `peripheral_mask`)
    constexpr uint64_t Low() const { return _words[0]; }
    constexpr uint64_t Word(size_t index) const { return index < kWords ? _words[index] : 0; }
    constexpr void SetWord(size_t index, uint64_t value)
    {
        if (index < kWords)
            _words[index] = value;
    }

    /// Words a file must carry: up to the highest non-zero one, at least 1
    constexpr size_t WordCount() const
    {
        size_t n = kWords;
        while (n > 1 && _words[n - 1] == 0)
            --n;
        return n;
    }

    constexpr bool operator==(const PeripheralMask& other) const = default;
    constexpr PeripheralMask& operator|=(const PeripheralMask& other)
    {
        for (size_t i = 0; i < kWords; ++i)
            _words[i] |= other._words[i];
        return *this;
    }
    constexpr PeripheralMask operator|(const PeripheralMask& other) const
    {
        PeripheralMask m = *this;
        m |= other;
        return m;
    }

private:
    std::array<uint64_t, kWords> _words{};
};

}  // namespace ttd
