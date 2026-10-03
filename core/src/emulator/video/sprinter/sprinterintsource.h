#pragma once

#include <cstdint>
#include <vector>

#include "emulator/cpu/z80.h"

class SprinterVideoRam;
class CovoxBlaster;

/// The Sprinter's /INT (Sprinter tdd-video §5): not a fixed position but the
/// mode table in video RAM decides it.
///
/// Rule (MAME update_int, sprinter.cpp:1278-1313; MAN §4.6): walk the squares
/// row by row in beam order; a square whose Mode0 byte matches %1111 11x1
/// (blank + INT) arms, and the first following square in the same row without
/// the pattern fires an INT on the last (8th) line of that square row. MAME puts
/// it at that square's beam column scr_a = a + 6 (2 square rows above b = 0 for
/// the top border).
///
/// The edge itself is the PLD's (VIDEO2.TDF, SP2_ACEX.TDF:744): INTT is clocked
/// by CT5, which rises 2 T into each 4-T square period, and INT_X is set on its
/// rising edge - the first square without the pattern, 2 T after the video logic
/// starts reading that square. The renderer reads square a at T 12 + 4a of the
/// line (HOLD #77), so the INT is at T 14 + 4a: kIntBeforeMameT = 10 T before
/// MAME's 4 x scr_a = 24 + 4a. MAME's place makes the INT-to-picture distance of
/// the Spectrum mode 17 980 T where the PLD gives 17 990 (Pentagon: 17 988), so
/// multicolor timed for a Pentagon raced 10 T late (Sprinter TODO, research-zx-mode
/// §7.1). The pulse lasts 32 T at 3.5 MHz.
///
/// Worked example: in a 320-line frame, row b = 30 has squares a = 40..45 with
/// Mode0 = #FD and a = 46 without. Beam row scr_b = 32, column scr_a = 52: MAME's
/// INT is on line 32 x 8 + 7 = 263 at pixel 52 x 16 = 832, base T-state
/// 263 x 224 + 832 / 4 = 59 120; the PLD's edge is 10 T earlier, at 59 110.
///
/// One INT per pulse: the acknowledge ends it (unverified - MAME keeps the line
/// for the full 32 T; see the Sprinter TODO). Positions are base T-states (3.5
/// MHz); the CPU's frame T-state is divided by the clock multiplier.
class SprinterIntSource : public IInterruptSource
{
public:
    static constexpr uint32_t kPulseTStates = 32;
    static constexpr uint32_t kLineTStates = 224;
    static constexpr uint8_t kSquareColumns = 56;
    /// The PLD's INT edge before MAME's beam position of the square (see above)
    static constexpr uint32_t kIntBeforeMameT = 10;

    SprinterIntSource(EmulatorContext* context, const SprinterVideoRam& vram);

    /// region <IInterruptSource>
    bool IsIntAsserted(uint32_t t) override;
    uint8_t AcknowledgeInterrupt(uint32_t t) override;
    /// endregion </IInterruptSource>

    /// The keyboard interrupt (ALL_MODE bits 0 and 3, MAME on_kbd_data): a byte
    /// from the keyboard arrived. The PLD's one INT flip-flop holds it until the
    /// acknowledge (PLD SP2_1K30.TDF INT_X), which answers #FF like the frame INT
    void LatchKeyboardInt() { _keyboardInt = true; }
    bool KeyboardIntLatched() const { return _keyboardInt; }

    /// The Covox-Blaster's half-ring request (PLD CBL_INT, ORed into INT_X): its own flip-flop,
    /// cleared by the same acknowledge (vector #FF)
    void SetCovoxBlaster(CovoxBlaster* cbl) { _cbl = cbl; }

    /// The mode table changed (a blank + INT byte, RGMOD bit 0, the frame length):
    /// the INT list is rebuilt at the next query
    void Invalidate() { _dirty = true; }
    /// RGMOD bit 0 and the frame height (320 or 312 lines)
    void SetModePage(uint8_t page);
    void SetFrameLines(uint16_t lines);
    void Reset();

    /// INT start positions in base T-states of the frame, ascending
    const std::vector<uint32_t>& Positions();

    /// region <State (TTD): the inputs of the INT list and the PLD's INT flip-flop; the list itself is rebuilt>
    uint8_t ModePage() const { return _modePage; }
    uint16_t FrameLines() const { return _frameLines; }
    /// The pulse the last acknowledge ended (frame x length + start), -1 = none
    int64_t AckedPulse() const { return _ackedPulse; }
    void RestoreState(uint8_t modePage, uint16_t frameLines, int64_t ackedPulse, bool keyboardInt)
    {
        _modePage = modePage & 1;
        _frameLines = frameLines;
        _ackedPulse = ackedPulse;
        _keyboardInt = keyboardInt;
        _dirty = true;
    }
    /// endregion

    /// MAME's list for a given mode page and height (tests, the debugger)
    static std::vector<uint32_t> ComputePositions(const SprinterVideoRam& vram, uint8_t modePage, uint16_t frameLines);

private:
    uint32_t Multiplier() const;
    /// The pulse covering base T-state `raster` of frame `frame` (id = frame x length + start), or -1
    int64_t PulseAt(uint64_t frame, uint32_t raster);

    EmulatorContext* _context;
    const SprinterVideoRam& _vram;
    std::vector<uint32_t> _positions;
    bool _dirty = true;
    uint8_t _modePage = 0;
    uint16_t _frameLines = 320;
    int64_t _ackedPulse = -1;
    bool _keyboardInt = false;
    CovoxBlaster* _cbl = nullptr;
};
