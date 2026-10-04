#pragma once

/// @file sprinterinput.h
/// @brief The Sprinter Sp2000's keyboard and serial mouse on the Z84C15 SIO
/// (Sprinter tdd-accel-sound-input §3; phase S4 input).
///
/// The board (hardware-reference §13; PLD SP2_1K30.TDF / KBD.TDF; MAME
/// sprinter.cpp:1987-2008):
///   - the AT keyboard's clock and data lines go to SIO channel A (KBD_CLKR to
///     /RXCA and /TXCA, KBD_DATR to RXDA; the clock as the receive clock, x1)
///     and, through the XA bus latch, to the PLD. The SIO receives the raw
///     set 2 scan codes (3-byte FIFO); BIOS SETUP and DSS poll channel A from
///     their frame interrupt handler (SETUP KEYSCAN, DSS keyinter.asm RESCAN)
///     and translate the codes themselves;
///   - nothing holds the keyboard off: the only path that could pull the
///     keyboard's clock or data low (DD16 KR1533TM9 Q3 / Q4 -> DD17C / DD17D
///     onto KBD_CLK / KBD_DAT) is written with KBD_CX = KBD_DX = GND by the
///     PLD (SP2_1K30.TDF:351-352, :729; the LED-command sender is commented
///     out), and the SIO's /RTSA, /DTRA, /W/RDYA go elsewhere. A byte that
///     finds the FIFO full overwrites its newest character (RR1 bit 5, see
///     Z84Sio): the bytes are lost on the board too. BIOS SETUP up to 3.05
///     and DSS up to 1.62.93 take one key event per frame INT and never look
///     at RR1; BIOS 3.06 / 3.07 and DSS 1.71 (community sources, 2024-02-18 /
///     -29; the check is in those binaries) drain the FIFO and, on an overrun,
///     empty it, send Error Reset and forget the shift state;
///   - the PLD decodes the same stream into the ZX matrix read at #FE (code
///     #40). Here the matrix comes from the host's ZX key events (Keyboard),
///     which travel next to the PC key (KeyboardEvent / PcKeyEvent, one
///     journaled input each);
///   - keyboard INT: with ALL_MODE bits 0 and 3 set, an INT (vector #FF, the
///     PLD's) when a byte arrives (MAME on_kbd_data);
///   - the PLD's keyboard block (KBD.TDF) decodes the wire itself, byte by
///     byte as each frame ends, not the SIO: Ctrl (#14), Alt (#11) and Shift
///     (#12, #59) flags set by their make and cleared after #F0; #71 (Delete,
///     keypad .) with Ctrl and Alt pulls the CPU's /RESET (KB_RESET; the PLD
///     keeps its configuration); every #07 (F12) not after #F0 toggles the
///     turbo switch when no Shift / Ctrl / Alt is down (KB_F12 -> TEST_SWITCH
///     -> TURBO_HAND), so the keyboard's typematic repeats of F12 toggle it
///     again, as on the board. An SIO overrun cannot touch it. MAME instead
///     toggles on the host F12 key ("TURBO", no repeats);
///   - the serial mouse on SIO channel B (1 200 baud, Microsoft protocol; SIO B
///     receives with CTC ZC/TO0 as its clock: the characters arrive only while
///     the software programs ~1 200 baud there, DSS 1.71: 875 kHz / 45 / 16;
///     DSS 1.71 reads it, INTMOUSE READ_M: three bytes synced on bit 6, no 'M'
///     identification) and the PLD's Kempston view of the same mouse (code #58,
///     #FADF / #FBDF / #FFDF; DSS 1.62.9x reads that). Both views read one set
///     of board mouse counters (X, Y, buttons) kept here. The board mouse is a
///     sink of the emulator's MouseManager (always fitted, whatever [INPUT]
///     Mouse= says about the optional Kempston interface), so the host window,
///     automation and TTD replay all reach it through the manager.
///
/// Time: base T-states (3.5 MHz) from the machine's cumulative counter. Bytes
/// are delivered lazily before every access to the SIO ports, and after every
/// instruction only while the keyboard INT is enabled and a byte is on the way
/// (NeedsStepHook): a machine whose keyboard is idle pays nothing per step.
///
/// Worked example: the user presses F4 at the IDE wait. The host's PcKeyEvent
/// is journaled and applied here; F4's make code #0C arrives at SIO A 917 us
/// later; SETUP's next frame INT reads RR0 bit 0 = 1, then #0C from #18, and
/// skips the drive. F12 instead: its #07 ends its frame 917 us after the press
/// and the PLD flips the turbo switch then; held past 500 ms, each typematic
/// #07 (every 92 ms) flips it again.

#include <atomic>
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

    /// region <IMouseSink: the board mouse (serial packets and the PLD's Kempston view)>
    /// Always fitted, whatever [INPUT] Mouse= says about the Kempston interface:
    /// the mouse is part of the board. Input moves the board's counters; the
    /// serial mouse samples them, the PLD's Kempston view reads them
    bool IsMouseFitted() const override { return true; }
    /// A program read the board mouse within kPolledWithinFrames frames: the PLD's Kempston view
    /// (code #58) or SIO B, the serial mouse's receiver (DSS 1.71 polls it from its frame INT).
    /// BIOS SETUP and a DSS without a mouse driver read neither: a click then captures nothing
    bool IsMouseInUse() const override;
    /// The board mouse: counters, the PLD's Kempston view, the serial line and SIO B's receiver
    MouseDeviceStatus DescribeMouse() const override;
    /// The view a program polls has not taken the last motion: the Kempston view's X or Y unread, or
    /// motion not yet sent as serial packets (counted only while that view is polled)
    bool HasUnreadMotion() const override;
    void OnMouseMotion(int dx, int dy) override;
    void OnMouseButtons(uint8_t activeLowMask) override;
    /// Neither view has a wheel (Microsoft two-button mouse; MAME's code #58 has no wheel nibble)
    void OnMouseWheel(int) override {}
    void OnMouseCounters(uint8_t x, uint8_t y) override;
    /// endregion

    /// The board mouse counters: X (+ right), Y (+ up), buttons (active low: D0 left, D1 right, D2 middle).
    /// TTD blob 31 holds them with the serial mouse's packet in flight
    struct BoardMouse
    {
        uint8_t x;
        uint8_t y;
        uint8_t buttons;
    };
    BoardMouse GetBoardMouse() const;
    void SetBoardMouse(const BoardMouse& mouse);  ///< TTD restore

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

    /// A byte will arrive that must raise the keyboard INT on time, or that the PLD acts on (#07, #71; a held F12 /
    /// Delete / keypad . repeating)
    bool NeedsStepHook() const { return (KeyboardIntEnabled() && _keyboard.Busy()) || PldActionPending(); }

    /// region <The PLD's keyboard block watching the wire (KBD.TDF)>
    static constexpr uint8_t kPldExt = 0x01;    ///< KB_EXT: the last byte was #E0
    static constexpr uint8_t kPldOff = 0x02;    ///< KB_OFF: the last byte other than #E0 was #F0
    static constexpr uint8_t kPldCtrl = 0x04;   ///< KB_CTRL
    static constexpr uint8_t kPldAlt = 0x08;    ///< KB_ALT
    static constexpr uint8_t kPldShift = 0x10;  ///< KB_SH
    uint8_t PldKeyboardFlags() const { return _pldKeyboard; }
    void SetPldKeyboardFlags(uint8_t flags) { _pldKeyboard = flags; }  ///< TTD restore
    /// A byte on its way (or a key repeating) the PLD acts on
    bool PldActionPending() const;
    /// endregion

    /// The machine reset (the cumulative clock restarted): keys stay held, times move to now
    void Rebase();
    /// Power-on: nothing held, nothing on the way
    void Clear();

    /// The board actions behind keys: Ctrl + Alt + Del (CPU reset), F12 (turbo switch)
    void SetResetHandler(std::function<void()> handler) { _onReset = std::move(handler); }
    void SetTurboSwitchHandler(std::function<void()> handler) { _onTurboSwitch = std::move(handler); }
    /// Called when NeedsStepHook may have changed (a key queued)
    void SetStepHookListener(std::function<void()> listener) { _onStepHookChange = std::move(listener); }

    /// The PLD's Kempston view of the board's mouse (code #58): A8 = 0 buttons
    /// (active low, D0 left, D1 right, D2 middle, D7-D3 = 1), else A10 = 0 X, else Y
    uint8_t ReadMouseView(uint16_t port) const;
    /// The same value without counting as a program's read (debug and automation)
    uint8_t PeekMouseView(uint16_t port) const;

    Ps2KeyboardStream& KeyboardStream() { return _keyboard; }
    MsSerialMouse& SerialMouse() { return _mouse; }

    /// SIO B's receive clock from CTC ZC/TO0 and its clock mode (WR4), in baud (0: no clock)
    double MouseReceiverBaud() const;
    /// That clock is within kBaudTolerance of the mouse's 1 200 baud: its characters are received
    bool MouseReceiverInTune() const;
    /// Statistics: mouse characters lost to a receive clock out of tune (not in TTD: no machine state)
    uint64_t MouseFramingErrors() const { return _mouseFramingErrors; }
    /// Statistics: mouse characters SIO B received (not in TTD)
    uint64_t MouseBytesReceived() const { return _mouseBytesReceived; }

    /// Statistics: bytes the SIO refused (FIFO full)
    uint64_t KeyboardOverruns() const { return _keyboardOverruns; }
    void SetKeyboardOverruns(uint64_t count) { _keyboardOverruns = count; }  ///< TTD restore

private:
    uint64_t Now() const;
    /// One byte's frame ended on the wire: the PLD's keyboard block decodes it
    void OnWireByte(uint8_t value);
    /// The board's mouse counters (one source for the serial and the Kempston view)
    void SampleMouse(uint8_t& x, uint8_t& y, uint8_t& buttons) const;

    /// Power-on counters: two different non-zero values, as the Kempston interface's
    /// (software infers "no mouse" from equal axes)
    static constexpr uint8_t kResetX = 31;
    static constexpr uint8_t kResetY = 85;

    EmulatorContext* _context;
    Z84Lib::Z84C15& _chip;
    SprinterIntSource& _intSource;
    const SprinterPldState& _pld;
    Ps2KeyboardStream _keyboard;
    MsSerialMouse _mouse;
    uint64_t _keyboardOverruns = 0;
    uint8_t _pldKeyboard = 0;  ///< kPld* flags
    uint64_t _mouseFramingErrors = 0;
    uint64_t _mouseBytesReceived = 0;
    /// Frames (EmulatorState::frame_counter) of the last program access to each view; polling for
    /// IsMouseInUse and glide pacing, not machine state (TTD does not save them)
    static constexpr uint64_t kNeverPolled = ~uint64_t{0};
    mutable std::atomic<uint64_t> _viewPollFrame{kNeverPolled};
    std::atomic<uint64_t> _serialPollFrame{kNeverPolled};
    /// Bit 1: X moved since the Kempston view's X was read, bit 2: Y
    mutable std::atomic<uint8_t> _viewUnread{0};
    uint64_t Frame() const;
    bool PolledLately(uint64_t pollFrame) const;
    /// An asynchronous receiver samples mid-bit: about half a bit over the 9.5 bits up to the stop bit, shared by
    /// both ends; 5 % is the usual budget
    static constexpr double kBaudTolerance = 0.05;
    // Written on the emulator thread by the manager (or by automation without TTD), read by the decoder:
    // atomics, the relative moves compare-and-swap loops (as the Kempston interface's counters)
    std::atomic<uint8_t> _mouseX{kResetX};
    std::atomic<uint8_t> _mouseY{kResetY};
    std::atomic<uint8_t> _mouseButtons{0xFF};
    std::function<void()> _onReset;
    std::function<void()> _onTurboSwitch;
    std::function<void()> _onStepHookChange;
};
