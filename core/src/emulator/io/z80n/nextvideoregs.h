#pragma once

/// @file nextvideoregs.h
/// @brief The NextREG state of the video units that software reads back and restores (registers.txt): the four clip
/// windows with their write indices (NR #18-#1C) and the eight 9-bit palettes with the palette index / control
/// registers (NR #40-#44). The renderer (N6) reads it; this holds the registers only.

#include <cstdint>

class NextVideoRegs
{
public:
    static constexpr unsigned kClipWindows = 4;  ///< Layer 2, sprites, ULA/LoRes, tilemap (NR #18-#1B)
    static constexpr unsigned kPalettes = 8;     ///< ULA 1/2, Layer 2 1/2, sprites 1/2, tilemap 1/2 in NR #43 bits 6:4 order

    void Reset();

    /// region <Clip windows>
    /// A write stores at the window's index and advances it; reads do not advance. NR #1C reads the four indices,
    /// a write of 1 in bits 3:0 resets the tilemap / ULA / sprite / Layer 2 index
    void WriteClip(unsigned window, uint8_t value);
    uint8_t ReadClip(unsigned window) const { return _clip[window][_clipIndex[window]]; }
    void WriteClipControl(uint8_t value);
    uint8_t ReadClipControl() const;
    uint8_t Clip(unsigned window, unsigned coordinate) const { return _clip[window][coordinate & 3]; }
    /// endregion

    /// region <Palettes>
    void WritePaletteIndex(uint8_t value);
    uint8_t PaletteIndex() const { return _index; }
    void WritePaletteValue8(uint8_t value);   ///< NR #41: RRRGGGBB, the low blue bit is B1 | B0
    void WritePaletteValue9(uint8_t value);   ///< NR #44: two writes; the second carries the low blue bit (and the Layer 2 priority)
    void WritePaletteControl(uint8_t value);  ///< NR #43
    void WriteUlaNextFormat(uint8_t value) { _ulaNext = value; }
    uint8_t ReadPaletteValue8() const;
    uint8_t ReadPaletteValue9() const;  ///< the second byte only
    uint8_t PaletteControl() const { return _control; }
    uint8_t UlaNextFormat() const { return _ulaNext; }
    /// The 9-bit colour (bit 8 = Layer 2 priority is kept in bit 9)
    uint16_t PaletteEntry(unsigned palette, unsigned index) const { return _palette[palette & 7][index & 255]; }
    /// endregion

private:
    unsigned Selected() const { return (_control >> 4) & 7; }
    void Advance();

    uint8_t _clip[kClipWindows][4] = {};
    uint8_t _clipIndex[kClipWindows] = {};
    uint16_t _palette[kPalettes][256] = {};
    uint8_t _index = 0;
    uint8_t _control = 0;
    uint8_t _ulaNext = 0x07;
    uint8_t _first = 0;
    bool _haveFirst = false;
};
