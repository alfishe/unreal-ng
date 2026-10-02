#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>

/// @file sprintervideoram.h
/// @brief The Sprinter's 256 KB video RAM (Sprinter tdd-video §1, hardware-reference §6.2).
///
/// A write-only shadow of CPU writes: the graphics pages and the Spectrum
/// screen addressing put bytes here, the CPU never reads it back. Layout: 256
/// rows of 1 024 bytes; in each row #000-#2FF are free (screens, fonts),
/// #300-#39F hold the mode table and #3E0-#3FF the palettes (MAN §4.4).
///
/// The mode table: square (a, b) has 4 mode bytes at row 1 + 2a + #80 x PM,
/// column #300 + 4b (PM = RGMOD bit 0). A Mode0 byte with the pattern
/// %1111 11x1 (blank + INT) places the frame INT (SprinterIntSource), so a
/// write that changes such a byte notifies the INT source (MAME vram_w:1228).
class SprinterVideoRam
{
public:
    static constexpr size_t kSize = 256 * 1024;
    static constexpr uint32_t kRowBytes = 1024;
    static constexpr uint32_t kModeTableColumn = 0x300;
    static constexpr uint32_t kModeTableEnd = 0x3A0;
    static constexpr uint32_t kPaletteColumn = 0x3E0;

    SprinterVideoRam() : _data(new uint8_t[kSize]()) {}

    /// A CPU-side write (graphics page or Spectrum shadow)
    void Write(uint32_t addr, uint8_t value)
    {
        addr &= kSize - 1;
        const uint8_t old = _data[addr];
        _data[addr] = value;
        if (old != value && IsIntModeByte(addr) && (((old & 0xFC) == 0xFC) || ((value & 0xFC) == 0xFC)) && _onIntModeChange)
            _onIntModeChange();
    }

    uint8_t Read(uint32_t addr) const { return _data[addr & (kSize - 1)]; }
    uint8_t* Data() { return _data.get(); }
    const uint8_t* Data() const { return _data.get(); }

    /// Mode0 byte of square (a, b) in mode page `page` (RGMOD bit 0)
    uint8_t Mode0(uint8_t a, uint8_t b, uint8_t page) const
    {
        return _data[ModeAddress(a, b, page)];
    }
    static uint32_t ModeAddress(uint8_t a, uint8_t b, uint8_t page)
    {
        return (1u + 2u * a + 0x80u * (page & 1u)) * kRowBytes + kModeTableColumn + 4u * b;
    }

    /// A Mode0 byte: odd row (bit 10), column #300-#39F, a multiple of 4 (MAME: (offset & 0x403) == 0x400)
    static bool IsIntModeByte(uint32_t addr)
    {
        const uint32_t column = addr & (kRowBytes - 1);
        return column >= kModeTableColumn && column < kModeTableEnd && (addr & 0x403) == 0x400;
    }

    void Clear() { std::memset(_data.get(), 0, kSize); }

    /// Called when a write changes a mode byte to or from the blank + INT pattern
    void SetIntModeListener(std::function<void()> listener) { _onIntModeChange = std::move(listener); }

private:
    std::unique_ptr<uint8_t[]> _data;
    std::function<void()> _onIntModeChange;
};
