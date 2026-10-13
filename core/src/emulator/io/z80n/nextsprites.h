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
    /// The engine's time per line in 28 MHz clocks (sprites.vhd, measured on the RTL in GHDL: tools/poc/029-next-sprite-engine-timing): 4 per
    /// 7 MHz pixel of the line, 448 pixels on the 48K / Pentagon timing (1792), 456 on the 128K / +3 (1824). One clock for S_START,
    /// one per sprite for S_QUALIFY and one per pixel of a sprite on the line for S_PROCESS (16 << x scale; a sprite starting at
    /// 320..494 costs one, one starting at 320 - w..319 stops at the edge). "Too many" is set when the engine is not idle at the next
    /// line (the clocks used >= the line's), and a pixel after the line's last clock is not drawn
    static constexpr unsigned kLineBudget = 1792;

    /// What DrawLine writes for each grid pixel
    struct Pixel
    {
        uint8_t index = 0;  ///< the 8-bit palette index
        uint8_t sprite = 0; ///< the sprite number (0-127) that painted it
        bool opaque = false;
    };

    void Reset();       ///< a hard reset: the configuration load clears the attribute and pattern RAMs too
    void SoftReset();   ///< `reset` of sprites.vhd: the indices, the mirror and the status flags; the RAMs stay

    /// region <Ports>
    uint8_t ReadStatus();                  ///< #303B: bit 1 too many sprites, bit 0 collision; clears both
    void WriteSlotSelect(uint8_t value);   ///< #303B: sprite (6:0), pattern half (7)
    void WriteAttribute(uint8_t value);    ///< #57
    void WritePattern(uint8_t value);      ///< #5B
    /// endregion

    /// region <NextREG>
    void WriteMirrorSprite(uint8_t value, bool tied);   ///< NR #34
    /// NR #34: with NR #09 bit 4 (tie) the mirror is the port's sprite number (sprites.vhd 607-610: it follows every #303B write and every
    /// #57 write that completes a sprite), else what NR #34 / NR #75-#79 left
    uint8_t ReadMirrorSprite(bool tied) const { return static_cast<uint8_t>((tied ? _slot : _mirror) & 0x7F); }
    uint8_t ReadMirrorSprite() const { return _mirror & 0x7F; }
    /// The tie went off: the mirror keeps the sprite number it had followed
    void ReleaseMirror() { _mirror = static_cast<uint8_t>((_mirror & 0x80) | _slot); }
    void WriteMirrorAttribute(unsigned byte, uint8_t value, bool increment, bool tied);  ///< NR #35-#39 / #75-#79
    /// endregion

    /// Run the engine for grid line `y` (0..255) and paint what is inside the clip window (NR #19) into `out` (not touched when the
    /// sprites are off: NR #15 bit 0 gates the output, the engine and its flags run). `control` is NR #15, `lineClocks` the
    /// line period in 28 MHz clocks (kLineBudget)
    void DrawLine(unsigned y, uint8_t control, const NextVideoRegs& regs, Pixel* out, uint8_t transparentIndex, unsigned lineClocks = kLineBudget);

    /// region <Const view for the next_sprites report: nothing here clears a flag or steps an upload index (design-automation-coverage.md R3)>
    /// The state the sprite engine fills a line's buffer from changes with every write: a snapshot taken at the fill is valid while the
    /// epoch is the same (ScreenNext, POC 028: the engine fills paper row R during row R - 1, in index order)
    uint32_t Epoch() const { return _epoch; }
    void Touch() { _epoch++; }
    /// The 28 MHz clocks the engine needs for row y: one per sprite to qualify and the process clocks (16 << x scale, fewer at the right
    /// edge) of each one drawn (sprites.vhd S_QUALIFY / S_PROCESS; the last drawn sprite ends the visible fill)
    unsigned FillClocks(unsigned y) const;
    /// The flags a drawing of a snapshot set go to this (live) object, the snapshot's are cleared
    void MergeFlags(NextSprites& from)
    {
        _collision = _collision || from._collision;
        _tooMany = _tooMany || from._tooMany;
        from._collision = from._tooMany = false;
    }
    void ClearFlags() { _collision = _tooMany = false; }

    bool CollisionFlag() const { return _collision; }
    bool TooManyFlag() const { return _tooMany; }
    uint8_t UploadSlot() const { return _slot; }
    /// One sprite as the engine sees it: the five attribute bytes decoded, a relative sprite resolved against its anchor
    struct Info
    {
        bool extended = false;  ///< the fifth attribute byte is in use
        bool relative = false;  ///< a relative sprite (composite / unified group member)
        int anchor = -1;        ///< relative: the index of the sprite it hangs on
        bool unified = false;   ///< anchor: unified type (members inherit its mirror, rotate and scale)
        int x = 0, y = 0;       ///< 9 bit, the anchor's offset applied for a relative sprite
        int offsetX = 0, offsetY = 0;  ///< relative: the signed offsets as written
        bool relativePattern = false, relativePalette = false;
        unsigned pattern = 0;   ///< 8-bit sprite: 0-63; 4-bit sprite: 0-127
        uint8_t palette = 0;
        bool xMirror = false, yMirror = false, rotate = false, visible = false, fourBit = false;
        unsigned xScale = 1, yScale = 1;  ///< 1, 2, 4 or 8
    };
    void Describe(Info (&out)[kSprites]) const;
    /// endregion

    uint8_t Attribute(unsigned sprite, unsigned byte) const { return _attr[sprite & 127][byte % 5]; }
    uint8_t PatternByte(unsigned offset) const { return _pattern[offset & (kPatternBytes - 1)]; }

private:
    friend struct NextStateCodec;  // the TTD state codec (nextstatecodec.cpp)
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
    bool PatternPixel(const Effective& sprite, int patternCol, int rowIndex, uint8_t transparentIndex, uint8_t& index) const;
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
    uint32_t _epoch = 0;
};
