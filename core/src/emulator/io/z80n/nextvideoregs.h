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
    static constexpr unsigned kPalettes = 8;     ///< NR #43 bits 6:4 select: 0 ULA, 1 Layer 2, 2 sprites, 3 tilemap; bit 6 the second palette of each

    /// A hard reset (the FPGA configuration is loaded again) restores the power-on colours too; a soft one keeps the palette
    /// memory: zxnext.vhd resets the palette index, control and stored byte, never the palette RAMs (the dpram2 entities have no reset)
    void Reset(bool palettes = true);

    /// region <Clip windows>
    /// A write stores at the window's index and advances it; reads do not advance. NR #1C reads the four indices,
    /// a write of 1 in bits 3:0 resets the tilemap / ULA / sprite / Layer 2 index
    void WriteClip(unsigned window, uint8_t value);
    uint8_t ReadClip(unsigned window) const { return _clip[window][_clipIndex[window]]; }
    void WriteClipControl(uint8_t value);
    uint8_t ReadClipControl() const;
    uint8_t Clip(unsigned window, unsigned coordinate) const { return _clip[window][coordinate & 3]; }
    /// endregion

    /// region <Display ports>
    /// Port #FF (Timex) and NR #69 bits 5:0 are one latch; port #123B bit 1 and NR #69 bit 7 the Layer 2 enable; NR #69
    /// bit 6 the shadow ULA screen (an alias of #7FFD bit 3)
    void WritePortFf(uint8_t value) { _portFf = value & 0x3F; }
    uint8_t PortFf() const { return _portFf; }
    /// Port #123B: bit 4 = 0 sets the enable (1), write map (0), read map (2), shadow bank (3) and segment (7:6); bit 4 = 1
    /// sets the bank offset (2:0) of the CPU mapping
    void WritePort123b(uint8_t value)
    {
        if (value & 0x10)
            _l2Offset = value & 7;
        else
            _port123b = value & 0xCF;
    }
    uint8_t Layer2Offset() const { return _l2Offset; }
    bool Layer2MapWrite() const { return (_port123b & 0x01) != 0; }
    bool Layer2MapRead() const { return (_port123b & 0x04) != 0; }
    bool Layer2MapShadow() const { return (_port123b & 0x08) != 0; }
    uint8_t Layer2MapSegment() const { return static_cast<uint8_t>(_port123b >> 6); }
    uint8_t Port123b() const { return _port123b; }
    void WriteDisplayControl(uint8_t value)
    {
        _portFf = value & 0x3F;
        _shadowAlias = (value & 0x40) != 0;
        _port123b = static_cast<uint8_t>((_port123b & ~0x02) | ((value & 0x80) ? 0x02 : 0));
    }
    uint8_t ReadDisplayControl() const
    {
        return static_cast<uint8_t>(((_port123b & 0x02) ? 0x80 : 0) | (_shadowAlias ? 0x40 : 0) | _portFf);
    }
    bool Layer2Enabled() const { return (_port123b & 0x02) != 0; }
    bool ShadowAlias() const { return _shadowAlias; }
    /// endregion

    /// region <Palettes>
    void WritePaletteIndex(uint8_t value);
    uint8_t PaletteIndex() const { return _index; }
    bool PaletteSubIndex() const { return _haveFirst; }  ///< nr_palette_sub_idx: the next NR #44 write is the second byte
    void WritePaletteValue8(uint8_t value);   ///< NR #41: RRRGGGBB, the low blue bit is B1 | B0
    void WritePaletteValue9(uint8_t value);   ///< NR #44: two writes; the second carries the low blue bit (and the Layer 2 priority)
    void WritePaletteControl(uint8_t value);  ///< NR #43
    void WriteUlaNextFormat(uint8_t value) { _ulaNext = value; }
    uint8_t ReadPaletteValue8() const;
    uint8_t ReadPaletteValue9() const;  ///< the second byte only
    /// NR #28 reads back the byte the last first write of NR #44 stored (nr_stored_palette_value, zxnext.vhd 6004), not what was written to NR #28
    uint8_t StoredPaletteValue() const { return _first; }
    uint8_t PaletteControl() const { return _control; }
    uint8_t UlaNextFormat() const { return _ulaNext; }
    /// region <ULA+> ports #BF3B (select) / #FF3B (data): mode 0 = a CLUT entry (entries 192-255 of the ULA palette the NR #43 write select
    /// names: bit 6 the second one), mode 1 = the enable bit (shared with NR #68 bit 3, kept by the board). The byte is GGGRRRBB
    void WriteUlaPlusSelect(uint8_t value)
    {
        _ulapMode = static_cast<uint8_t>(value >> 6);
        if (_ulapMode == 0)
            _ulapIndex = value & 0x3F;
    }
    uint8_t UlaPlusMode() const { return _ulapMode; }
    uint8_t UlaPlusIndex() const { return _ulapIndex; }
    void WriteUlaPlusPalette(uint8_t value);
    uint8_t ReadUlaPlusPalette() const;
    /// endregion

    /// The 9-bit colour (bit 8 = Layer 2 priority is kept in bit 9). Bits 11:10 are the two priority bits of the second byte of NR #44
    /// as stored (every palette keeps them and reads them back, only Layer 2's bit 9 reaches the picture): not part of the colour
    uint16_t PaletteEntry(unsigned palette, unsigned index) const { return _palette[palette & 7][index & 255] & 0x3FF; }
    /// endregion

private:
    friend struct NextStateCodec;  // the TTD state codec (nextstatecodec.cpp)
    unsigned Selected() const { return (_control >> 4) & 7; }
    void Advance();

    uint8_t _clip[kClipWindows][4] = {};
    uint8_t _clipIndex[kClipWindows] = {};
    uint16_t _palette[kPalettes][256] = {};
    uint8_t _index = 0;
    uint8_t _control = 0;
    uint8_t _ulapMode = 0;
    uint8_t _ulapIndex = 0;
    uint8_t _ulaNext = 0x07;
    uint8_t _portFf = 0;
    uint8_t _port123b = 0;
    uint8_t _l2Offset = 0;
    bool _shadowAlias = false;
    uint8_t _first = 0;
    bool _haveFirst = false;
};
