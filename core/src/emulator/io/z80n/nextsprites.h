#pragma once

/// @file nextsprites.h
/// @brief The Next's sprite engine (video/sprites.vhd, research-fpga-vhdl.md section 10): 128 sprites of five attribute
/// bytes, 16K of pattern RAM (64 patterns of 16 x 16 at 8 bits, or 128 of 4 bits), the upload ports #303B / #57 / #5B
/// and the NextREG mirrors #34-#39 / #75-#79, relative (composite / unified) sprites, scaling, mirror and rotate.
/// Fill runs the engine for one line into a line buffer (sprite coordinates: the paper starts at 32, 32), Output paints what the
/// output stage (NR #15 bit 0, the clip window) lets through; the renderer doubles it. The collision and "too many sprites" flags are
/// events with the machine clock of the engine's step (PostFill), read-and-cleared through port #303B at the clock of the read.

#include <cstdint>
#include <limits>
#include <vector>

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

    /// One line of the engine's buffer (sprites.vhd: it fills one while the video reads the other)
    struct LineBuffer
    {
        Pixel pixel[kGridWidth];
        bool any = false;  ///< false: no sprite is visible, nothing was written
    };

    void Reset();       ///< a hard reset: the configuration load clears the attribute and pattern RAMs too
    void SoftReset();   ///< `reset` of sprites.vhd: the indices, the mirror and the status flags; the RAMs stay

    /// region <Ports>
    /// #303B at machine clock `clock` (28 MHz clocks of the frame, the PostFill clocks): bit 1 too many sprites, bit 0 collision, set by
    /// the events before the clock; clears them (an event on the read's own clock is lost, a later one stays for the next read)
    uint8_t ReadStatus(int64_t clock);
    uint8_t ReadStatus();                  ///< #303B with every event so far: bit 1 too many sprites, bit 0 collision; clears both
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

    /// Run the engine for line `vcount` (spr_cur_vcount, 0..511: the grid's lines are 0..255) into `out`: every opaque pixel of x 0..319,
    /// NR #15 bit 6 (zero on top, the `control` byte) decides which of two stays. `collisions` gets the engine clock (from S_START = 1) of
    /// each write onto a pixel already there; returns true when the engine is still busy after `lineClocks` (the "too many" flag)
    bool Fill(unsigned vcount, uint8_t control, uint8_t transparentIndex, unsigned lineClocks, LineBuffer& out, std::vector<uint16_t>& collisions) const;
    /// The output stage for grid line `y`: what is inside the clip window (NR #19) into `out` (not touched when the sprites are off: NR #15
    /// bit 0 gates the output only). `control` is NR #15
    void Output(const LineBuffer& line, unsigned y, uint8_t control, const NextVideoRegs& regs, Pixel* out) const;
    /// Fill and Output at once, the flags set at once (a renderer with no machine clock: the tests)
    void DrawLine(unsigned y, uint8_t control, const NextVideoRegs& regs, Pixel* out, uint8_t transparentIndex, unsigned lineClocks = kLineBudget);
    /// The events of a fill that started at machine clock `start` (Fill's ticks added to it, the overtime on the next pass's start) at
    /// machine clock `now`: those before it are kept as set flags, the later ones with their clocks
    void PostFill(int64_t start, const std::vector<uint16_t>& collisions, bool overtime, unsigned lineClocks, int64_t now);
    /// The machine clock moved by `delta` (a new frame: its clocks count from its start)
    void ShiftClock(int64_t delta);
    bool AnyVisible() const;

    /// region <Const view for the next_sprites report: nothing here clears a flag or steps an upload index (design-automation-coverage.md R3)>
    /// The state the sprite engine fills a line's buffer from changes with every write: a snapshot taken at the fill is valid while the
    /// epoch is the same (ScreenNext, POC 028: the engine fills paper row R during row R - 1, in index order)
    uint32_t Epoch() const { return _epoch; }
    void Touch() { _epoch++; }
    /// The 28 MHz clocks the engine needs for row y: one per sprite to qualify and the process clocks (16 << x scale, fewer at the right
    /// edge) of each one drawn (sprites.vhd S_QUALIFY / S_PROCESS; the last drawn sprite ends the visible fill)
    unsigned FillClocks(unsigned y) const;
    void ClearFlags();

    bool CollisionFlag() const { return _collision || !_pendingCollision.empty(); }
    bool TooManyFlag() const { return _tooMany || !_pendingTooMany.empty(); }
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
    static constexpr int64_t kNever = std::numeric_limits<int64_t>::min();
    bool _collision = false;  ///< set by an event before the last clock that posted or read
    bool _tooMany = false;
    std::vector<int64_t> _pendingCollision;  ///< the later events, sorted (at most a line's)
    std::vector<int64_t> _pendingTooMany;
    int64_t _lastRead = kNever;  ///< the clock of the last #303B read: an event up to it was cleared by it
    uint32_t _epoch = 0;
};
