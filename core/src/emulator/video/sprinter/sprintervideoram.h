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
///
/// The palettes: pen p = k x 256 + n (palette k = 0..7, entry n = 0..255) is
/// the 3 bytes at row n, column #3E0 + 4k: **red, green, blue** in that order
/// (S2 decision, hardware-reference §4.5: the PLD puts byte 0 of a 4-byte group
/// in the RAM bank it drives as RED, SP2_1K30.TDF:455-466 / VIDEO2.TDF MODE0 =
/// VDM3; BIOS 3.04's palette function #A4 stores its B,G,R input reversed,
/// page 8 #0E10). Pens 0-1023 are the four graphics palettes, 1024-2047 the
/// text palettes paper / ink / flash paper / flash ink (MAME's pen numbers).
/// A write into the palette columns refreshes the pen's RGBA here, so the
/// renderer reads a ready colour (MAME vram_w:1238-1243).
///
/// Worked example: the BIOS sets text paper colour 1 to CGA blue by calling #A4
/// with the bytes #A8,#00,#00 (B,G,R); video RAM gets #00,#00,#A8 at row 1,
/// column #3F0, and pen #401 becomes RGBA #FFA80000 (A,B,G,R): blue.
class SprinterVideoRam
{
public:
    static constexpr size_t kSize = 256 * 1024;
    static constexpr uint32_t kRowBytes = 1024;
    static constexpr uint32_t kModeTableColumn = 0x300;
    static constexpr uint32_t kModeTableEnd = 0x3A0;
    static constexpr uint32_t kPaletteColumn = 0x3E0;
    static constexpr uint32_t kPens = 8 * 256;
    static constexpr uint32_t kOpaqueBlack = 0xFF000000u;

    SprinterVideoRam() : _data(new uint8_t[kSize]()), _palette(new uint32_t[kPens])
    {
        ResetPalette();
    }

    /// A CPU-side write (graphics page or Spectrum shadow)
    void Write(uint32_t addr, uint8_t value)
    {
        addr &= kSize - 1;
        const uint8_t old = _data[addr];
        if (old == value)
            return;
        // The beam draws up to this moment with the old byte first (MAME update_now, vram_w:1225)
        if (_beforeChange)
            _beforeChange();
        _data[addr] = value;
        // Columns #000-#2FF (screens, fonts: almost every write) need nothing more: one test
        if ((addr & (kRowBytes - 1)) >= kModeTableColumn) [[unlikely]]
            TableWrite(addr, old, value);
    }

    uint8_t Read(uint32_t addr) const { return _data[addr & (kSize - 1)]; }
    /// Raw storage. A direct write into the palette columns must be followed by RefreshPalette()
    uint8_t* Data() { return _data.get(); }
    const uint8_t* Data() const { return _data.get(); }

    /// RGBA (0xAABBGGRR, the framebuffer format) of the 2 048 pens
    const uint32_t* Palette() const { return _palette.get(); }
    uint32_t Pen(uint32_t pen) const { return _palette[pen & (kPens - 1)]; }
    /// Pen number of a palette byte's group: k x 256 + row (MAME: BIT(offset, 2, 3) * 256 + (offset >> 10))
    static uint32_t PenOf(uint32_t addr) { return ((addr >> 2) & 7u) * 256u + ((addr >> 10) & 0xFFu); }
    /// The video RAM address of pen `pen`'s red byte
    static uint32_t PenAddress(uint32_t pen)
    {
        return (pen & 0xFFu) * kRowBytes + kPaletteColumn + 4u * ((pen >> 8) & 7u);
    }

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

    void Clear()
    {
        std::memset(_data.get(), 0, kSize);
        ResetPalette();
    }

    /// Rebuild every pen from the palette bytes (after direct writes through Data())
    void RefreshPalette()
    {
        for (uint32_t pen = 0; pen < kPens; pen++)
            UpdatePen(PenAddress(pen));
    }

    /// Called when a write changes a mode byte to or from the blank + INT pattern
    void SetIntModeListener(std::function<void()> listener) { _onIntModeChange = std::move(listener); }
    /// Called before a write changes any byte (the renderer catches up with the beam)
    void SetBeforeChangeListener(std::function<void()> listener) { _beforeChange = std::move(listener); }
    /// Called after a write changed a mode table byte (#300-#39F) or a palette byte (#3E0-#3FF):
    /// (address, isPalette) - the video change log counts them (videowritelog.h)
    void SetTableWriteListener(std::function<void(uint32_t, bool)> listener) { _onTableWrite = std::move(listener); }

private:
    /// A changed byte in columns #300-#3FF: the frame INT, the pen, the change log
    void TableWrite(uint32_t addr, uint8_t old, uint8_t value)
    {
        const uint32_t column = addr & (kRowBytes - 1);
        if (IsIntModeByte(addr) && (((old & 0xFC) == 0xFC) || ((value & 0xFC) == 0xFC)) && _onIntModeChange)
            _onIntModeChange();
        const bool palette = column >= kPaletteColumn;
        if (palette)
            UpdatePen(addr);
        if (_onTableWrite && (palette || column < kModeTableEnd))
            _onTableWrite(addr, palette);
    }

    void ResetPalette()
    {
        for (uint32_t pen = 0; pen < kPens; pen++)
            _palette[pen] = kOpaqueBlack;
    }

    void UpdatePen(uint32_t addr)
    {
        const uint32_t group = addr & ~3u;
        _palette[PenOf(addr)] = kOpaqueBlack | (static_cast<uint32_t>(_data[group + 2]) << 16) |
                                (static_cast<uint32_t>(_data[group + 1]) << 8) | _data[group];
    }

    std::unique_ptr<uint8_t[]> _data;
    std::unique_ptr<uint32_t[]> _palette;
    std::function<void()> _onIntModeChange;
    std::function<void()> _beforeChange;
    std::function<void(uint32_t, bool)> _onTableWrite;
};
