#pragma once

#include <cstdint>

class SprinterVideoRam;

/// @file sprintervideorenderer.h
/// @brief How the Sprinter's standard PLD configuration turns video RAM into
/// pixels (Sprinter tdd-video §3), and the hook a configuration module uses to
/// draw its own picture (tdd-ports-memory §6.1 hook 3).
///
/// Geometry (tdd-video §2). The beam draws 896 pixels a line at 14 MHz (4 per
/// base T-state) and 320 or 312 lines. Pixel (x, y) of the 736x288 visible
/// window is beam line y, T (x / 4) of the line - the frame origin MAME uses,
/// where the INT positions of the mode table are measured. The 640x256
/// picture starts at (48, 16). Every visible pixel belongs to one square of
/// the mode table, border squares included:
///
///   a16 = (x - 48 - holdX) mod 896   a = a16 / 16   (column 0..55, 16 pixels)
///   b8  = (y - 16 - holdY) mod lines b = b8 / 8     (row 0..39, 8 lines)
///
/// with HOLD (code #CB) = v: holdX = (7 - (v & #0F)) x 2, holdY = 7 - (v >> 4)
/// (MAME sprinter.cpp:850-852; #77 = no shift).
///
/// A square (mode bytes m0 m1 m2 at row 1 + 2a + #80 x RGMOD.0, column #300 + 4b;
/// the Line2 set one row lower) is drawn as (MAME draw_tile / draw_symbol,
/// sprinter.cpp:430-497; the PLD VIDEO2.TDF agrees where checked):
///
///   graphics (m0 bit 4 = 0): palette m0 >> 6, source column (m0 & #0F) << 6 |
///     (m1 & 7) << 3, source row (m1 >> 3) << 3; 8 bytes x 8 rows per square.
///     320 (m0 bit 5 = 1): one byte per 2 pixels, pen = palette x 256 + byte.
///     640 (bit 5 = 0): one nibble per pixel, the HIGH nibble first (MAME; the
///     PLD shows the high nibble in the first half of the 7 MHz period).
///     m2 bit 2 = low-res: 2x2 pixels from the quarter (m2 bit 0, bit 1).
///   text (bit 4 = 1): font byte at (m1 << 10) | (m0 & #0F) << 6 | 7FFD.3 << 5 |
///     (m0 >> 6) << 3 | row, attribute at (m2 << 10) | (m0 & #0F) << 6 |
///     7FFD.3 << 5 | %11000 | m0 >> 6; pen = #400 + attribute + #100 x bit +
///     #200 x flash (frame counter bit 4): the text palettes paper / ink / flash
///     paper / flash ink. 320 (bit 5 = 1): a font bit is 2 pixels; 640: two
///     characters per square, the right one from the Line2 bytes.
///   m0 = %1111 xxxx: border (pen #400 + 9 x border colour, the #FE bits 0-2);
///     m0 = %1111 11xx: blank (pen #400, text paper colour 0).
///
/// Worked example: square (5, 3) in mode page 0 with m0 = #A2 (graphics 320,
/// palette 2, source column #080), m1 = #19 (column += 8, row 24); with no
/// HOLD shift its first visible pixel is (48 + 80, 16 + 24) = (128, 40): pen
/// 2 x 256 + VRAM[24 x 1024 + #88], and pixel 129 repeats it.
struct SprinterVideoInputs
{
    const uint8_t* vram = nullptr;     ///< 256 KB
    const uint32_t* palette = nullptr; ///< 2 048 pens, RGBA (SprinterVideoRam::Palette)
    uint8_t modePage = 0;              ///< RGMOD bit 0
    uint8_t border = 0;                ///< border colour 0-7
    uint8_t textPage = 0;              ///< #7FFD bit 3 (the font / attribute block bit)
    bool flash = false;                ///< frame counter bit 4
    int32_t holdX = 0;                 ///< pixels, from HOLD
    int32_t holdY = 0;                 ///< lines, from HOLD
    uint16_t lines = 320;              ///< frame height: 320 or 312

    /// HOLD (code #CB) as picture offsets (MAME m_hold)
    void SetHold(uint8_t hold)
    {
        holdX = (7 - static_cast<int32_t>(hold & 0x0F)) * 2;
        holdY = 7 - static_cast<int32_t>(hold >> 4);
    }
};

/// One square of the mode table, decoded by the renderer's rules (the classifier every report
/// shares: DeviceState::Sprinter's video summary, the per-square map, the screen text,
/// ScreenSprinter::DescribeScreenState and the video mapper's text layer).
///
///   m0 bit 4 = 0: graphics, bit 5 = 1: 320 (a byte per 2 pixels), 0: 640 (a nibble per pixel)
///   m0 bit 4 = 1: %1111 11xx blank (%1111 11x1: blank + frame INT), %1111 xxxx border,
///                 otherwise text, bit 5 = 1: 40 columns, 0: 80 columns (two characters)
///
/// Worked example: m0 = #A2, m1 = #19, m2 = #00 is graphics 320 with palette 2, source
/// column #080 + 8 = #088, source row 24; m0 = #FD is blank with the frame INT.
struct SprinterSquare
{
    enum class Kind : uint8_t
    {
        Graphics320,
        Graphics640,
        Text40,
        Text80,
        Border,
        Blank,
        Count
    };

    Kind kind = Kind::Blank;
    uint8_t m0 = 0;
    uint8_t m1 = 0;
    uint8_t m2 = 0;

    static Kind Classify(uint8_t m0)
    {
        if (!(m0 & 0x10))
            return (m0 & 0x20) ? Kind::Graphics320 : Kind::Graphics640;
        if ((m0 & 0xFC) == 0xFC)
            return Kind::Blank;
        if ((m0 >> 5) == 7)
            return Kind::Border;
        return (m0 & 0x20) ? Kind::Text40 : Kind::Text80;
    }
    /// The square at Line1 bytes `mode` (Mode0..Mode2 at mode[0..2])
    static SprinterSquare Decode(const uint8_t* mode)
    {
        SprinterSquare s;
        s.m0 = mode[0];
        s.m1 = mode[1];
        s.m2 = mode[2];
        s.kind = Classify(s.m0);
        return s;
    }

    bool IsGraphics() const { return kind == Kind::Graphics320 || kind == Kind::Graphics640; }
    bool IsText() const { return kind == Kind::Text40 || kind == Kind::Text80; }
    /// Blank with the frame INT mark (%1111 11x1, SprinterIntSource)
    bool IntArmed() const { return (m0 & 0xFD) == 0xFD; }
    /// Graphics: palette 0-3 (pens palette x 256 + value)
    uint8_t Palette() const { return static_cast<uint8_t>(m0 >> 6); }
    /// Graphics: the source byte column and pixel row of the square's first byte (video RAM row x 1024 + column)
    uint16_t SourceColumn() const { return static_cast<uint16_t>(((m0 & 0x0F) << 6) | ((m1 & 0x07) << 3)); }
    uint16_t SourceRow() const { return static_cast<uint16_t>((m1 >> 3) << 3); }
    /// Graphics: 2x2 pixels from one quarter (m2 bit 2), the quarter (m2 bits 1-0: bit 0 right, bit 1 lower)
    bool LowRes() const { return IsGraphics() && (m2 & 0x04) != 0; }
    uint8_t Quarter() const { return static_cast<uint8_t>(m2 & 0x03); }

    /// The character of text cell `half` (0 left, 1 right) of the square whose Line1 bytes are `line1`
    /// (Line2 = line1 + 1024): an 80-column square shows Line1's Mode1, then Line2's (the renderer takes
    /// every byte of the right half from Line2, its Mode0 too); a 40-column square one character and a
    /// space. False (code = #20) when that half is not a text character
    static bool TextCode(const uint8_t* line1, unsigned half, uint8_t& code);

    /// One letter per kind for compact maps: G 320, g 640, T text 40, t text 80, B border, . blank, * blank + INT
    char Letter() const;
    /// "graphics_320" ... (JSON keys)
    static const char* Key(Kind kind);
    /// "graphics 320 x 256, 256 colors" ... (people)
    static const char* Name(Kind kind);
};

/// The standard configuration's picture. A configuration module with its own
/// renderer (a later Game module: per-square scroll, MAME sprinter.cpp:499-545)
/// derives from it and overrides DrawSpan; ScreenSprinter asks the active module
class SprinterVideoRenderer
{
public:
    static constexpr uint32_t kLinePixels = 896;
    static constexpr uint32_t kVisibleWidth = 736;
    static constexpr uint32_t kVisibleLines = 288;
    static constexpr uint32_t kBorderLeft = 48;
    static constexpr uint32_t kBorderTop = 16;
    static constexpr uint32_t kPenText = 0x400;

    virtual ~SprinterVideoRenderer() = default;

    /// Visible pixels [x0, x1) of visible line y into out[0 .. x1 - x0)
    virtual void DrawSpan(const SprinterVideoInputs& in, uint32_t y, uint32_t x0, uint32_t x1, uint32_t* out) const;

    /// The pen of one visible pixel (the debugger, tests); DrawSpan draws exactly these
    static uint32_t PenAt(const SprinterVideoInputs& in, uint32_t x, uint32_t y);

    /// Square coordinates of a visible pixel: a16 (0..895), b8 (0..lines-1)
    static uint32_t A16(const SprinterVideoInputs& in, uint32_t x);
    static uint32_t B8(const SprinterVideoInputs& in, uint32_t y);

    /// Pen of a graphics square (Line1 bytes `mode`) at square pixel (sub 0..15, row 0..7)
    static uint32_t GraphicsPen(const SprinterVideoInputs& in, const uint8_t* mode, uint32_t sub, uint32_t row);
    /// Pen of a text / border / blank square at square pixel (sub, row); `line1` = the Line1 bytes
    static uint32_t SymbolPen(const SprinterVideoInputs& in, const uint8_t* line1, uint32_t sub, uint32_t row);
    /// The mode bytes a text pixel uses: Line2 for the right half of a 640 text square
    static const uint8_t* SymbolMode(const uint8_t* line1, uint32_t sub)
    {
        return (!(line1[0] & 0x20) && (sub & 8)) ? line1 + 1024 : line1;
    }
    /// VRAM addresses of a text pixel's font byte and attribute
    static uint32_t FontAddress(const SprinterVideoInputs& in, const uint8_t* mode, uint32_t row);
    static uint32_t AttrAddress(const SprinterVideoInputs& in, const uint8_t* mode);
    /// VRAM address of a graphics pixel's byte
    static uint32_t GraphicsAddress(const uint8_t* mode, uint32_t sub, uint32_t row);

    /// The standard renderer (stateless, shared)
    static const SprinterVideoRenderer& Standard();
};
