#pragma once

/// @file sprinterinput.h
/// @brief The Sprinter Sp2000's keyboard and serial mouse on the Z84C15 SIO
/// (Sprinter tdd-accel-sound-input §3; phase S4 input).
///
/// The board (hardware-reference §13; PLD SP2_1K30.TDF / KBD.TDF; MAME
/// sprinter.cpp:1987-2008):
///   - the AT keyboard's clock and data lines go to SIO channel A (the clock as
///     the receive clock, x1) and to the PLD. The SIO receives the raw set 2
///     scan codes (3-byte FIFO); BIOS 3.04 SETUP and DSS poll channel A from
///     their frame interrupt handler (SETUP KEYSCAN, DSS keyinter.asm RESCAN)
///     and translate the codes themselves. Nothing holds the keyboard off: a
///     byte that finds the FIFO full is an overrun (RR1 bit 5);
///   - the PLD decodes the same stream into the ZX matrix read at #FE (code
///     #40). Here the matrix comes from the host's ZX key events (Keyboard),
///     which travel next to the PC key (KeyboardEvent / PcKeyEvent, one
///     journaled input each);
///   - keyboard INT: with ALL_MODE bits 0 and 3 set, an INT (vector #FF, the
///     PLD's) when a byte arrives (MAME on_kbd_data);
///   - Ctrl + Alt + Del pulls the CPU's /RESET (KBD.TDF KB_RESET; the PLD keeps
///     its configuration); F12 without Shift / Ctrl / Alt toggles the hardware
///     turbo switch (KB_F12 -> TEST_SWITCH -> TURBO_HAND; MAME F12 "TURBO");
///   - the serial mouse on SIO channel B (1 200 baud, Microsoft protocol),
///     sampled from the Kempston counters - the PLD's Kempston view (code #58)
///     reads the same mouse.
///
/// Time: base T-states (3.5 MHz) from the machine's cumulative counter. Bytes
/// are delivered lazily before every access to the SIO ports, and after every
/// instruction only while the keyboard INT is enabled and a byte is on the way
/// (NeedsStepHook): a machine whose keyboard is idle pays nothing per step.
///
/// Worked example: the user presses F4 at the IDE wait. The host's PcKeyEvent
/// is journaled and applied here; F4's make code #0C arrives at SIO A 917 us
/// later; SETUP's next frame INT reads RR0 bit 0 = 1, then #0C from #18, and
/// skips the drive.

#include <cstdint>
#include <functional>

#include "emulator/io/keyboard/ps2keyboardstream.h"
#include "emulator/io/mouse/imousesink.h"
#include "emulator/io/mouse/msserialmouse.h"

class EmulatorContext;
class SprinterIntSource;
struct SprinterPldState;
namespace Z84Lib
{
class Z84C15;
}

class SprinterInput : public IPs2KeySink, public IMouseSink
{
public:
    /// `pld`: ALL_MODE is read from the decoder's PLD state (one copy of it)
    SprinterInput(EmulatorContext* context, Z84Lib::Z84C15& chip, SprinterIntSource& intSource, const SprinterPldState& pld);
    ~SprinterInput() override;
    SprinterInput(const SprinterInput&) = delete;
    SprinterInput& operator=(const SprinterInput&) = delete;

    /// region <IMouseSink: the serial mouse is part of the board>
    /// Always fitted, whatever [INPUT] Mouse= says about the Kempston port. Its
    /// input lands in the board's mouse counters (the Kempston device, also a
    /// sink of the manager), which the serial mouse samples
    bool IsMouseFitted() const override { return true; }
    void OnMouseMotion(int, int) override {}
    void OnMouseButtons(uint8_t) override {}
    void OnMouseWheel(int) override {}
    /// endregion

    /// region <IPs2KeySink: the host's physical keys (journaled input)>
    void OnPcKey(PcKey key, bool pressed) override;
    void ReleaseAllPcKeys() override;
    /// endregion

    /// ALL_MODE (bits 0 and 3: the keyboard INT) is about to change: bytes that
    /// arrived before the write are delivered under the old value. Call before
    /// storing the new value, then NotifyStepHookChange
    void BeforeAllModeWrite();
    bool KeyboardIntEnabled() const;

    /// Before an access to the Z84C15 port `lowByte`: the SIO channel it
    /// touches receives what arrived by now
    void BeforeChipAccess(uint8_t lowByte);
    /// Deliver everything due by now (the step hook, tests)
    void Advance();

    /// A byte will arrive that must raise the keyboard INT on time
    bool NeedsStepHook() const { return KeyboardIntEnabled() && _keyboard.Busy(); }

    /// The machine reset (the cumulative clock restarted): keys stay held, times move to now
    void Rebase();
    /// Power-on: nothing held, nothing on the way
    void Clear();

    /// The board actions behind keys: Ctrl + Alt + Del (CPU reset), F12 (turbo switch)
    void SetResetHandler(std::function<void()> handler) { _onReset = std::move(handler); }
    void SetTurboSwitchHandler(std::function<void()> handler) { _onTurboSwitch = std::move(handler); }
    /// Called when NeedsStepHook may have changed (a key queued)
    void SetStepHookListener(std::function<void()> listener) { _onStepHookChange = std::move(listener); }

    Ps2KeyboardStream& KeyboardStream() { return _keyboard; }
    MsSerialMouse& SerialMouse() { return _mouse; }

    /// Statistics: bytes the SIO refused (FIFO full)
    uint64_t KeyboardOverruns() const { return _keyboardOverruns; }
    void SetKeyboardOverruns(uint64_t count) { _keyboardOverruns = count; }  ///< TTD restore

private:
    uint64_t Now() const;

    EmulatorContext* _context;
    Z84Lib::Z84C15& _chip;
    SprinterIntSource& _intSource;
    const SprinterPldState& _pld;
    Ps2KeyboardStream _keyboard;
    MsSerialMouse _mouse;
    uint64_t _keyboardOverruns = 0;
    std::function<void()> _onReset;
    std::function<void()> _onTurboSwitch;
    std::function<void()> _onStepHookChange;
};
