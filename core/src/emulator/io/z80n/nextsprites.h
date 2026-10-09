#pragma once

/// @file nextsprites.h
/// @brief The Next's sprite engine (video/sprites.vhd, research-fpga-vhdl.md section 10): 128 sprites of five attribute
/// bytes, 16K of pattern RAM (64 patterns of 16 x 16 at 8 bits, or 128 of 4 bits), the upload ports #303B / #57 / #5B
/// and the NextREG mirrors #34-#39 / #75-#79, relative (composite / unified) sprites, scaling, mirror and rotate.
/// DrawLine paints one 320 x 256 grid line (sprite coordinates: the paper starts at 32, 32); the renderer doubles it.
/// The collision and "too many sprites" flags are set by drawing, read-and-cleared through port #303B.

#include <cstdint>

class NextVideoRegs;

class NextSprites
{
public:
    static constexpr unsigned kSprites = 128;
    static constexpr unsigned kPatternBytes = 16384;
    static constexpr unsigned kGridWidth = 320;
    /// The per-line time budget as a count of cycles: one per sprite plus one per drawn pixel of a sprite on the line.
    /// An estimate (the 28 MHz cost per sprite is not read from the VHDL yet): 3.02.02 and later draw at least 100
    /// sprites a line
    static constexpr unsigned kLineBudget = 1792;

    /// What DrawLine writes for each grid pixel
    struct Pixel
    {
        uint8_t index = 0;  ///< the 8-bit palette index
        bool opaque = false;
    };

    void Reset();

    /// region <Ports>
    uint8_t ReadStatus();                  ///< #303B: bit 1 too many sprites, bit 0 collision; clears both
    void WriteSlotSelect(uint8_t value);   ///< #303B: sprite (6:0), pattern half (7)
    void WriteAttribute(uint8_t value);    ///< #57
    void WritePattern(uint8_t value);      ///< #5B
    /// endregion

    /// region <NextREG>
    void WriteMirrorSprite(uint8_t value, bool tied);   ///< NR #34
    uint8_t ReadMirrorSprite() const { return _mirror & 0x7F; }
    void WriteMirrorAttribute(unsigned byte, uint8_t value, bool increment, bool tied);  ///< NR #35-#39 / #75-#79
    /// endregion

    /// Draw grid line `y` (0..255). `control` is NR #15; the clip window is the sprites' (NR #19)
    void DrawLine(unsigned y, uint8_t control, const NextVideoRegs& regs, Pixel* out, uint8_t transparentIndex);

    uint8_t Attribute(unsigned sprite, unsigned byte) const { return _attr[sprite & 127][byte % 5]; }
    uint8_t PatternByte(unsigned offset) const { return _pattern[offset & (kPatternBytes - 1)]; }

private:
    struct Effective
    {
        int x = 0, y = 0;
        uint8_t palette = 0;
        bool xMirror = false, yMirror = false, rotate = false, visible = false, fourBit = false;
        uint8_t xScale = 0, yScale = 0;
        uint8_t pattern = 0;  ///< 7 bits: pattern(5:0) << 1 | N6
    };
    struct Anchor
    {
        bool type1 = false, h4bit = false, visible = false, rotate = false, xMirror = false, yMirror = false;
        int x = 0, y = 0;
        uint8_t pattern = 0, palette = 0, xScale = 0, yScale = 0;
    };

    bool Extended(unsigned s) const { return (_attr[s][3] & 0x40) != 0; }
    bool Relative(unsigned s) const { return Extended(s) && (_attr[s][4] & 0xC0) == 0x40; }
    Effective Decode(unsigned s) const;
    void Resolve(unsigned s, const Anchor& anchor, Effective& out) const;
    void UpdateAnchor(unsigned s, Anchor& anchor) const;
    void DrawSprite(const Effective& sprite, unsigned y, const int clip[4], bool overBorder, Pixel* out, bool* occupied,
                    uint8_t transparentIndex);
    void SetAttribute(unsigned sprite, unsigned byte, uint8_t value) { _attr[sprite & 127][byte] = value; }

    uint8_t _attr[kSprites][5] = {};
    uint8_t _pattern[kPatternBytes] = {};
    uint8_t _slot = 0;
    uint8_t _byte = 0;
    uint16_t _patternOffset = 0;
    uint8_t _patternHalf = 0;
    uint8_t _mirror = 0;
    bool _collision = false;
    bool _tooMany = false;
};
