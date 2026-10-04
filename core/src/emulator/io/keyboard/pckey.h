#pragma once

/// @file pckey.h
/// @brief Physical PC keyboard keys and their PS/2 scan code set 2 bytes.
///
/// Machines with a PS/2 keyboard controller (ZX-Evo: the AVR logs raw set-2
/// scan codes for the Z80, and NedoOS reads only that log) need the PHYSICAL
/// key the user pressed, not the ZX matrix keys the host key was decomposed
/// into: host Up is Caps Shift + 7 on the matrix but `E0 75` on PS/2.
///
/// A PcKey travels next to the ZX key code from the host (KeyboardEvent),
/// is journaled once as its own TTD input event, and reaches the machine's
/// IPs2KeySink when one is attached (Keyboard::SetPs2Sink). Machines without a
/// sink never see it.
///
/// Also here: the platform tables that turn a host key code into a PcKey
/// (macOS virtual key, Windows set-1 scan code, Linux evdev key code), and the
/// automation mappings (ZX key -> PC keys, character -> PC keys, US layout).

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/io/keyboard/keyboard.h"  // ZXKeysEnum

/// Physical keys of a 105-key PC keyboard. Values are internal (never stored
/// in files); append only, so the automation key names stay stable.
enum class PcKey : uint8_t
{
    None = 0,

    // Letters
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // Digit row
    Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9, Digit0,

    // Function keys
    Function1, Function2, Function3, Function4, Function5, Function6,
    Function7, Function8, Function9, Function10, Function11, Function12,

    // Main block
    Escape, Backquote, Minus, Equal, Backspace, Tab, LeftBracket, RightBracket, Backslash,
    CapsLock, Semicolon, Quote, Enter, LeftShift, Comma, Period, Slash, RightShift,
    LeftCtrl, LeftGui, LeftAlt, Space, RightAlt, RightGui, Menu, RightCtrl,
    IntlBackslash,  ///< the ISO key between Left Shift and Z

    // Navigation block
    PrintScreen, ScrollLock, Pause,
    Insert, Home, PageUp, Delete, End, PageDown,
    Up, Left, Down, Right,

    // Keypad
    NumLock, KeypadDivide, KeypadMultiply, KeypadMinus, KeypadPlus, KeypadEnter, KeypadDecimal,
    Keypad0, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,

    Count
};

/// Something that consumes physical key events: the PS/2 keyboard controller
/// of a machine (ZX-Evo AVR). Called on the thread executing the machine, from
/// the one place that applies journaled input (live and replay alike)
class IPs2KeySink
{
public:
    virtual ~IPs2KeySink() = default;

    virtual void OnPcKey(PcKey key, bool pressed) = 0;
    /// Every key the sink sees as held is released (breaks sent), e.g. when
    /// automation releases all keys
    virtual void ReleaseAllPcKeys() = 0;

    /// The controller takes the place of the ZX matrix on the keyboard
    /// connector (Profi PROFI-XT: one keyboard on X9). The host keyboard's
    /// Auto route then goes to the controller alone, not to both: a host Shift
    /// would otherwise press Caps Shift on the matrix and Symbol Shift through
    /// the controller
    virtual bool ReplacesMatrix() const { return false; }
    /// Automation: the PC keys that stand for one ZX key on this controller.
    /// Default pckey::FromZxKey (the ZX-Evo AVR's reverse map)
    virtual std::vector<PcKey> PcKeysForZxKey(ZXKeysEnum key) const;
    /// Automation typing: the PC keys that type `c`; `zxKeys` are the ZX keys
    /// that type it on the matrix. Default pckey::FromCharacter (US layout)
    virtual std::vector<PcKey> PcKeysForCharacter(char c, const std::vector<ZXKeysEnum>& zxKeys) const;
    /// A short name for reports and the UI ("PROFI-XT firmware"); empty for an
    /// unnamed PS/2 controller
    virtual std::string ControllerName() const { return {}; }
};

namespace pckey
{
    /// Automation name: "a", "1", "f1", "up", "lshift", "kp_enter", ... (lower
    /// case); empty for None / out of range
    std::string Name(PcKey key);
    /// Name -> key, case-insensitive; also accepts a "pc." / "pc:" prefix.
    /// PcKey::None when unknown
    PcKey FromName(const std::string& name);

    /// Every automation name in table order (skips the empty None entry) -
    /// for listing endpoints (WebAPI GET /keyboard/keys) so PC-only keys like
    /// "f12" or "rshift" are discoverable, not just already-accepted
    std::vector<std::string> AllNames();

    /// PS/2 scan code set 2 bytes of a key press (make) or release (break):
    /// A -> 1C / F0 1C; Up -> E0 75 / E0 F0 75; Print Screen -> E0 12 E0 7C /
    /// E0 F0 7C E0 F0 12; Pause -> E1 14 77 E1 F0 14 F0 77 on press, nothing on
    /// release. Empty for None
    std::vector<uint8_t> Ps2Set2Bytes(PcKey key, bool pressed);

    /// PC/XT scan code set 1 bytes of a make or break, as an XT keyboard (or an
    /// AT keyboard in XT mode) sends them: A -> 1E / 9E (break = make | 80h);
    /// Up -> E0 48 / E0 C8; Print Screen -> E0 2A E0 37 / E0 B7 E0 AA; Pause
    /// -> E1 1D 45 E1 9D C5 on press, nothing on release. No fake shifts
    /// around the navigation keys. Empty for None
    std::vector<uint8_t> XtSet1Bytes(PcKey key, bool pressed);

    /// Host key codes -> physical key. PcKey::None when the code is not a key
    /// of the table. These are layout-independent: the same physical key gives
    /// the same PcKey under an English or a Russian layout
    PcKey FromMacVirtualKey(uint32_t virtualKey);   ///< macOS kVK_* (QKeyEvent::nativeVirtualKey)
    PcKey FromWindowsScanCode(uint32_t scanCode);   ///< set 1, bit 8 = extended (QKeyEvent::nativeScanCode)
    PcKey FromLinuxEvdev(uint32_t evdevCode);       ///< evdev KEY_*; X11 / Wayland keycode = evdev + 8

    /// Automation: the PC keys that stand for one ZX key (before the ZX key is
    /// decomposed into matrix keys). Caps Shift -> Left Shift and Symbol Shift
    /// -> Left Ctrl, as the ZX-Evo AVR maps them back; the cursor keys ->
    /// arrows; Delete -> Backspace; Break -> Escape; Edit -> Backquote; a
    /// symbol such as '+' -> Left Shift + Equal (US layout). Empty when the ZX
    /// key has no PC equivalent. Press in order, release in reverse
    std::vector<PcKey> FromZxKey(ZXKeysEnum key);

    /// Automation typing: the PC keys that type `c` on a US layout (a shifted
    /// character is Left Shift + the key). Empty for characters with no key
    std::vector<PcKey> FromCharacter(char c);
}
