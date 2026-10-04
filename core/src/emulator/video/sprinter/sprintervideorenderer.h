#pragma once

#include <cstdint>
#include <string>

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

    /// The pixel byte of one text / Spectrum square the video logic has already latched while the CPU
    /// rewrote it in video RAM. The PLD loads a symbol square's font byte into its shift register once,
    /// at the start of the square - of each 8-pixel half in a 640 square (VIDEO2.TDF LD_PIC at CT[5..3] = 0,
    /// or CT[4..2] = 0) - while it reads the attribute again every half T (DCOL, WR_COL). So a write that
    /// lands inside a square changes its attribute from there on, its pixels only from the next square.
    /// Visible pixels [x0, x1) of visible line `line` show `font`; line ~0: none (ScreenSprinter keeps it)
    struct FontLatch
    {
        uint32_t line = ~0u;
        uint32_t x0 = 0;
        uint32_t x1 = 0;
        uint8_t font = 0;
    };
    FontLatch fontLatch;

    /// HOLD (code #CB) as picture offsets (MAME m_hold)
    void SetHold(uint8_t hold)
    {
        holdX = (7 - static_cast<int32_t>(hold & 0x0F)) * 2;
        holdY = 7 - static_cast<int32_t>(hold >> 4);
    }
};

/// One square of the mode table, decoded by the renderer's rules (the classifier every report
/// shares: DeviceState::Sprinter's video summary, the per-square map, the screen text,
/// ScreenSprinter::DescribeScreenState, the GUI status bar and the video mapper's text layer).
/// The renderer draws by the same predicates (IsSymbol / IsBlank / IsBorder below).
///
///   m0 bit 4 = 0: graphics, bit 5 = 1: 320 (a byte per 2 pixels), 0: 640 (a nibble per pixel)
///   m0 bit 4 = 1: %1111 11xx blank (%1111 11x1: blank + frame INT), %1111 xxxx border,
///                 otherwise a symbol square, bit 5 = 1: 40 columns, 0: 80 columns (two characters)
///
/// A symbol square is either text or a Spectrum screen cell: the hardware has no separate Spectrum
/// mode (ALL_MODE bit 0 only turns on the Spectrum screen shadow, the CPU writes into video RAM; it does
/// not change what the beam draws). The ZX picture is the mode table the BIOS / launcher write: 32 x 24
/// ZX-40 squares (text 40, font block = the shadow's block) inside border squares. The renderer reads a
/// symbol square's font byte at row m1, columns block | m0 bits 7-6 << 3 | pixel row, and its attribute
/// at row m2, columns block | %11 << 3 | m0 bits 7-6. The shadow stores a Spectrum character cell's
/// eight bitmap bytes at row A7..A0, columns block | A12..A8 (third << 3 | pixel row) and its attribute
/// at the same row A7..A0, columns block | %110 << 2 | third: so a square shows a Spectrum cell exactly
/// when m1 = m2 (its bitmap and its own attribute) and m0 bits 7-6 name a third 0-2.
/// Text uses m1 as the character code and m2 as the attribute row: independent bytes.
///
/// Worked examples: m0 = #A2, m1 = #19, m2 = #00 is graphics 320 with palette 2, source column
/// #080 + 8 = #088, source row 24; m0 = #FD is blank with the frame INT; m0 = #70, m1 = m2 = #25 is
/// the Spectrum cell in the middle third (bits 7-6 = 1), character row 8 + (#25 >> 5) = 9, column 5
/// (pixels #4825.., attribute #5925 - the 128 menu's table, testdata zx-mode reference).
struct SprinterSquare
{
    enum class Kind : uint8_t
    {
        Graphics320,
        Graphics640,
        Text40,
        Text80,
        Spectrum,  ///< a ZX-40 square: one Spectrum character cell (bitmap + its attribute), 16 x 8 pixels
        Border,
        Blank,
        Count
    };

    Kind kind = Kind::Blank;
    uint8_t m0 = 0;
    uint8_t m1 = 0;
    uint8_t m2 = 0;

    /// The renderer's decisions on Mode0 (DrawSpan, SymbolPen, the video mapper)
    static constexpr bool IsSymbol(uint8_t m0) { return (m0 & 0x10) != 0; }
    static constexpr bool IsBlank(uint8_t m0) { return (m0 & 0xFC) == 0xFC; }
    static constexpr bool IsBorder(uint8_t m0) { return !IsBlank(m0) && (m0 >> 5) == 7; }
    /// A 40-column symbol square showing one Spectrum character cell (see above)
    static constexpr bool IsSpectrumCell(uint8_t m0, uint8_t m1, uint8_t m2)
    {
        return IsSymbol(m0) && !IsBlank(m0) && !IsBorder(m0) && (m0 & 0x20) && (m0 >> 6) < 3 && m1 == m2;
    }

    /// The kind of the square whose Line1 bytes are `mode` (Mode0..Mode2 at mode[0..2])
    static Kind Classify(const uint8_t* mode)
    {
        const uint8_t m0 = mode[0];
        if (!IsSymbol(m0))
            return (m0 & 0x20) ? Kind::Graphics320 : Kind::Graphics640;
        if (IsBlank(m0))
            return Kind::Blank;
        if (IsBorder(m0))
            return Kind::Border;
        if (IsSpectrumCell(m0, mode[1], mode[2]))
            return Kind::Spectrum;
        return (m0 & 0x20) ? Kind::Text40 : Kind::Text80;
    }
    /// The square at Line1 bytes `mode` (Mode0..Mode2 at mode[0..2])
    static SprinterSquare Decode(const uint8_t* mode)
    {
        SprinterSquare s;
        s.m0 = mode[0];
        s.m1 = mode[1];
        s.m2 = mode[2];
        s.kind = Classify(mode);
        return s;
    }

    bool IsGraphics() const { return kind == Kind::Graphics320 || kind == Kind::Graphics640; }
    bool IsText() const { return kind == Kind::Text40 || kind == Kind::Text80; }
    /// Drawn from the text palettes (paper / ink / flash): text and Spectrum squares
    bool UsesTextPalettes() const { return IsText() || kind == Kind::Spectrum; }
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
    /// Spectrum: the character cell shown (row 0-23, column 0-31 of the 256 x 192 screen)
    uint8_t ZxRow() const { return static_cast<uint8_t>((m0 >> 6) * 8 + (m1 >> 5)); }
    uint8_t ZxColumn() const { return static_cast<uint8_t>(m1 & 0x1F); }

    /// The character of text cell `half` (0 left, 1 right) of the square whose Line1 bytes are `line1`
    /// (Line2 = line1 + 1024): an 80-column square shows Line1's Mode1, then Line2's (the renderer takes
    /// every byte of the right half from Line2, its Mode0 too); a 40-column square one character and a
    /// space. False (code = #20) when that half is not a text character (Spectrum cells are bitmaps)
    static bool TextCode(const uint8_t* line1, unsigned half, uint8_t& code);

    /// One letter per kind for compact maps: G 320, g 640, T text 40, t text 80, Z Spectrum, B border,
    /// . blank, * blank + INT
    char Letter() const;
    /// "graphics_320" ... (JSON keys)
    static const char* Key(Kind kind);
    /// "graphics 320 x 256, 256 colors" ... (people)
    static const char* Name(Kind kind);
};

/// What the 640 x 256 picture (the 40 x 32 squares from (0, 0)) shows, by SprinterSquare: the one summary
/// behind DescribeScreenState (the GUI status bar), DeviceState::Sprinter's video summary and the
/// screen-text / OCR choice. Border and blank squares frame a picture, they do not mix modes: `mixed` =
/// more than one content kind (graphics 320 / 640, text 40 / 80, Spectrum).
///
/// Worked example: the 128 menu in the Spectrum mode - 768 Spectrum squares, 512 border: mode Spectrum,
/// not mixed, Brief(0) = "Spectrum 256x192, screen 5". Flex Navigator: 1280 graphics 640 squares.
struct SprinterPicture
{
    static constexpr uint8_t kColumns = 40;
    static constexpr uint8_t kRows = 32;

    int counts[static_cast<int>(SprinterSquare::Kind::Count)] = {};
    SprinterSquare::Kind mode = SprinterSquare::Kind::Blank;  ///< the dominant content kind (border / blank: none)
    bool mixed = false;

    int Count(SprinterSquare::Kind kind) const { return counts[static_cast<int>(kind)]; }
    static bool IsContent(SprinterSquare::Kind kind)
    {
        return kind != SprinterSquare::Kind::Border && kind != SprinterSquare::Kind::Blank &&
               kind != SprinterSquare::Kind::Count;
    }

    /// The picture squares of mode page `modePage` in `vram` (256 KB)
    static SprinterPicture Of(const uint8_t* vram, uint8_t modePage);
    /// A few words for a status bar: "Spectrum 256x192, screen 5", "640x256 16c", "text 80", "320x256 256c (mixed)";
    /// `textPage` = #7FFD bit 3 (the Spectrum screen shown: 5 or 7)
    std::string Brief(uint8_t textPage) const;
};

/// A picture with state that runs in beam order (the Game configuration's grid-offset register,
/// sprintergamevideo.h): its next pixel depends on every square the beam passed before, the blanking
/// included. ScreenSprinter runs it on every catch-up whether the frame is drawn or not (turbo
/// decimation, ScreenHQ off) and closes the frame at every frame start (InitFrame, before the TTD
/// checkpoint), so the state is the same however the emulator renders - it is machine state (TTD).
/// Positions are the frame number (EmulatorState::frame_counter) and base T of the frame: pixel = T x 4
class SprinterBeamVideo
{
public:
    virtual ~SprinterBeamVideo() = default;

    /// The configuration starts at frame `frame`, T `beamT` (the load ended there): the state is its power-up one
    virtual void Start(uint64_t frame, uint32_t beamT) = 0;
    /// Run the state up to T `toT` (exclusive) of frame `frame`; a later frame first closes the one the state is
    /// in. Visible pixels on the way go to `framebuffer` (736 x 288) and `planeB` when they are not null. A `toT`
    /// behind the state is ignored
    virtual void Advance(const SprinterVideoInputs& in, uint64_t frame, uint32_t toT, uint32_t* framebuffer,
                         uint16_t* planeB) = 0;
    /// Frame `frame` starts: run the state to the last pixel of the frame it is in (when that is an earlier one)
    virtual void CloseFrame(const SprinterVideoInputs& in, uint64_t frame) = 0;
    /// Draw frame T [fromT, toT) again from the state of the frame start with the video RAM of now (the batch
    /// renderer of ScreenHQ off, a TTD repaint); the state does not change
    virtual void Redraw(const SprinterVideoInputs& in, uint32_t fromT, uint32_t toT, uint32_t* framebuffer,
                        uint16_t* planeB) const = 0;
    /// The pen of every visible pixel (736 x 288) of the frame drawn from its start with the video RAM of now
    virtual void FramePens(const SprinterVideoInputs& in, uint16_t* pens) const = 0;
};

/// The standard configuration's picture. A configuration module with its own
/// renderer (the Game module: per-square grid offset, sprintergamevideo.h)
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
    /// DrawSpan that also writes what it drew as ZX DLSS plane B (Screen::kPlaneB*) into
    /// planeB[0 .. x1 - x0), in the same pass: a Spectrum square's pixel is screen role with its
    /// attribute, ink bit and ZX color index; border and blank squares are border role (the border
    /// color, blank = 0); text and graphics squares are 0 (not a ZX picture). Called instead of
    /// DrawSpan only while plane B is on. A module with its own DrawSpan overrides this one too
    virtual void DrawSpanPlaneB(const SprinterVideoInputs& in, uint32_t y, uint32_t x0, uint32_t x1, uint32_t* out,
                                uint16_t* planeB) const;

    /// The pen of one visible pixel (the debugger, tests); DrawSpan draws exactly these
    static uint32_t PenAt(const SprinterVideoInputs& in, uint32_t x, uint32_t y);

    /// Square coordinates of a visible pixel: a16 (0..895), b8 (0..lines-1)
    static uint32_t A16(const SprinterVideoInputs& in, uint32_t x);
    static uint32_t B8(const SprinterVideoInputs& in, uint32_t y);

    /// Pen of a graphics square (Line1 bytes `mode`) at square pixel (sub 0..15, row 0..7)
    static uint32_t GraphicsPen(const SprinterVideoInputs& in, const uint8_t* mode, uint32_t sub, uint32_t row);
    /// Pen of a text / border / blank square at square pixel (sub, row); `line1` = the Line1 bytes;
    /// `latchedFont` >= 0: the font byte the video logic latched (SprinterVideoInputs::FontLatch)
    static uint32_t SymbolPen(const SprinterVideoInputs& in, const uint8_t* line1, uint32_t sub, uint32_t row,
                              int latchedFont = -1);
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
