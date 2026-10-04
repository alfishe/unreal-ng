#pragma once

#include <cstddef>
#include <cstdint>

#include "emulator/video/sprinter/sprintervideorenderer.h"

/// @file sprintergamevideo.h
/// @brief The picture of the Sprinter's "Game" PLD configuration (the bitstream `GAME_00.ACX` / LDConf's
/// `GC.BIN` / "Thunder in the Deep"; Sprinter mame-gap-analysis V10, game-configuration.md).
///
/// There are no sources of this configuration, only the bitstream. The rules come from the author's own
/// description in `GAME_00\RELOAD.ASZ` (the demo that loads it) and from MAME's `screen_update_game`
/// (sprinter.cpp:499-545), and are checked against MAME's picture of the same programs:
///
///   - the mode table is the standard one (square (a, b): row 1 + 2a + #80 x RGMOD.0, column #300 + 4b),
///     but every square is graphics 320 x 256 colors: Mode0 bits 7-6 = palette, bits 1-0 = source column
///     bits 9-8, Mode1 = source column bits 7-0, Mode2 = source row. A square shows 8 x 8 pixels of the
///     virtual 1024 x 256 byte screen from that corner, one byte per 2 beam pixels (pen palette x 256 +
///     byte); the corner is any byte, so a square can start anywhere, not only on a multiple of 8.
///     Mode0 bits 7-5 = %111 is border (pen #400 + 9 x border) or, with bits 3-2 = %11, blank (#400);
///   - Mode0 bit 2 = "grid offset": when the beam finishes such a square, Mode3 is loaded into the
///     grid-offset register: bits 3-0 shift the grid left by 2 beam pixels each (one 320 pixel), bits 7-4
///     up by one line each. It acts from the next square on and stays until another such square
///     ("if bit 2 was not set, the offset REMAINS what was set before": a register in beam order, across
///     squares, lines and frames). Blank squares (Mode0 = %1111 11xx) have bit 2 set too, so the blanking
///     squares load their Mode3 (RELOAD.ASZ clears every Mode3 "including border and blank!" for this);
///     square 55 sets the offset of the next line's square 0 ("the offset of square 0 ... set in the 55th").
///
/// With the offset (ox, oy) the beam pixel (x, y) shows square coordinates a16 = (x + 2 ox - 48 - holdX)
/// mod 896, b8 = (y + oy - 16 - holdY) mod lines (the Standard origin, SprinterVideoRenderer::A16 / B8).
///
/// Where MAME differs, deliberately: MAME restarts each line with `lookback_scroll`, which never looks at
/// another square (gap analysis §3 item 8), and switches the offset in the middle of an offset square when the
/// physical square boundary falls inside it; here the register runs in beam order and switches at the end of
/// the square (the author's description). Source byte columns and rows wrap inside the 1024 x 256 virtual
/// screen (MAME's address runs on into the next row). Mode0 bits 5-4 (the author: "graphics / text, must
/// be 0" and "640 / 320 points") are not modeled: MAME draws every square 320, and every known program
/// (GAME_00, TEST_005, TEST_010, LDConf's SCROLL) sets bit 5 and clears bit 4.
///
/// Worked example: square 37 of a row has Mode0 = #65 (palette 1, bit 2, column bits #100), Mode3 = #88.
/// When the beam leaves it the offset becomes (8, 8): square 38 is fetched 16 beam pixels (one square)
/// further right and 8 lines (one square row) lower, so the picture from there on shows the squares one
/// column right and one row down - until a square with bit 2 changes it again (in RELOAD.ASZ the blanking
/// squares 40-55, Mode3 = 0, clear it before the next line).

/// The register and how far the beam has run it (TTD: the Game module's state, 16 bytes, no padding). The blob holds
/// the frame-start state (SprinterPldGame::SaveState: offset = frameOffset, beamT = 0): a TTD checkpoint is taken
/// right after the frame start closed the previous frame, and how far the screen has caught up inside a frame is not
/// machine state
struct SprinterGameVideoState
{
    uint8_t offset = 0;       ///< the grid-offset register (Mode3 format: bits 3-0 X, bits 7-4 Y)
    uint8_t frameOffset = 0;  ///< the register at the start of the frame (a redraw of the frame starts from it)
    uint8_t reserved[2] = {};
    uint32_t beamT = 0;       ///< base T of the frame the register has been run up to (frame pixel beamT x 4)
    uint64_t frame = 0;       ///< the frame (EmulatorState::frame_counter) beamT belongs to
};
static_assert(sizeof(SprinterGameVideoState) == 16, "SprinterGameVideoState must stay padding-free (TTD blob)");

/// The Game configuration's renderer. Unlike Standard its picture has state - the grid-offset register -
/// so it is also the beam-ordered video (SprinterBeamVideo) ScreenSprinter runs it through: the register
/// runs over every beam position, the blanking included, whether the frame is drawn or not
class SprinterGameVideo : public SprinterVideoRenderer, public SprinterBeamVideo
{
public:
    static constexpr uint32_t kFramePixelsPerT = 4;

    /// region <SprinterVideoRenderer>
    /// Visible pixels [x0, x1) of line y with the register of the frame start run up to (y, x0): a span on
    /// its own (tests, a debugger); ScreenSprinter draws through Advance / Redraw
    void DrawSpan(const SprinterVideoInputs& in, uint32_t y, uint32_t x0, uint32_t x1, uint32_t* out) const override;
    void DrawSpanPlaneB(const SprinterVideoInputs& in, uint32_t y, uint32_t x0, uint32_t x1, uint32_t* out,
                        uint16_t* planeB) const override;
    /// endregion </SprinterVideoRenderer>

    /// region <SprinterBeamVideo>
    void Start(uint64_t frame, uint32_t beamT) override;
    void Advance(const SprinterVideoInputs& in, uint64_t frame, uint32_t toT, uint32_t* framebuffer,
                 uint16_t* planeB) override;
    void CloseFrame(const SprinterVideoInputs& in, uint64_t frame) override;
    void Redraw(const SprinterVideoInputs& in, uint32_t fromT, uint32_t toT, uint32_t* framebuffer,
                uint16_t* planeB) const override;
    void FramePens(const SprinterVideoInputs& in, uint16_t* pens) const override;
    /// endregion </SprinterBeamVideo>

    /// The pen of square pixel (sub 0..15, row 0..7) of the square with mode bytes `mode` (Mode0..Mode2)
    static uint32_t Pen(const SprinterVideoInputs& in, const uint8_t* mode, uint32_t sub, uint32_t row);
    /// The video RAM address a graphics square pixel reads (the virtual 1024 x 256 screen wraps)
    static uint32_t PixelAddress(const uint8_t* mode, uint32_t sub, uint32_t row);

    /// Run the register `offset` over frame pixels [from, to) (pixel = line x 896 + x, the blanking included)
    /// without drawing (a square at a time); returns the register after `to`
    static uint8_t RunRegister(const SprinterVideoInputs& in, uint8_t offset, uint32_t from, uint32_t to);

    SprinterGameVideoState& State() { return _state; }
    const SprinterGameVideoState& State() const { return _state; }

private:
    /// RunRegister that also gives every visible pixel's pen to `sink(x, y, pen)` (defined in the .cpp)
    template <typename Sink>
    static uint8_t Run(const SprinterVideoInputs& in, uint8_t offset, uint32_t from, uint32_t to, Sink&& sink);

    SprinterGameVideoState _state;
};
