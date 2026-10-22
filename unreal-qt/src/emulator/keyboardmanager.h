#pragma once

#ifndef KEYBOARDMANAGER_H
#define KEYBOARDMANAGER_H

#include <Qt>
#include <QtGlobal>

#include <optional>
#include <set>
#include <string>

#include "common/collectionhelper.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/keyboard/pckey.h"

class QKeyEvent;

class KeyboardManager
{
public:
    KeyboardManager();
    ~KeyboardManager();

public:
    /// region <Host modifier layout (docs/features/keyboard.md "Host keys on macOS")>

    /// Qt on macOS (AA_MacDontSwapCtrlAndMeta not set, the default) reports the Command key as
    /// Qt::Key_Control / ControlModifier and the physical Control key as Qt::Key_Meta / MetaModifier.
    /// Everything that needs the physical key goes through these helpers - the one place for the swap
    /// (the host keyboard, the mouse capture release key).
    /// True on a macOS build unless a test injects the platform (setMacKeyboardForTests)
    static bool macKeyboard();
    /// Tests: simulate the macOS (true) or the PC (false) modifier layout on any OS; nullopt = the build's own
    static void setMacKeyboardForTests(std::optional<bool> mac) { _macKeyboardOverride = mac; }

    /// The Qt key in PC terms: Key_Control = the physical Control key, Key_Meta = the Command key on
    /// macOS (the Windows / Super key elsewhere). Identity off macOS; the swap is its own inverse
    static int physicalQtKey(int qtKey);
    /// The modifiers in PC terms (ControlModifier = the physical Control key), as physicalQtKey
    static Qt::KeyboardModifiers physicalModifiers(Qt::KeyboardModifiers modifiers);

    /// The macOS Command key is a host key: Cmd+Tab, Cmd+Q, Cmd+F and other app shortcuts send nothing to the
    /// machine. Set, Command reaches the machine as the PC Win / GUI key (PS/2 E0 1F / E0 27), for software
    /// that wants it. QSettings "Keyboard/MacCommandKey" = host (default) | gui. No effect off macOS
    static bool commandKeyToGuest() { return _commandKeyToGuest; }
    static void setCommandKeyToGuest(bool on) { _commandKeyToGuest = on; }

    /// endregion </Host modifier layout>

    static quint8 mapQtKeyToEmulatorKey(int qtKey);
    static quint8 mapQtKeyToEmulatorKeyWithModifiers(int qtKey, Qt::KeyboardModifiers modifiers);

    /// The physical key of a host key event, from the platform's native code
    /// (macOS virtual key, Windows scan code, Linux evdev keycode): the same key
    /// under any keyboard layout. Falls back to the Qt key on other platforms
    static PcKey mapQtEventToPcKey(const QKeyEvent* event);
    /// The ZX key of a physical key whose Qt key a non-Latin layout hides (a Cyrillic letter on the K key):
    /// letters, digits and the punctuation keys the ZX extended keys use (US positions). ZXKEY_NONE for others
    static quint8 mapPcKeyToEmulatorKey(PcKey pcKey);
    /// A key whose ZX key comes from its position whatever the layout prints on it: the digit row (Shift+2 is
    /// CAPS LOCK on a German keyboard too) and the keypad / * - +
    static bool positionalKey(PcKey pcKey);
    /// Layout-dependent fallback: Qt key (as Qt reports it) -> physical key (US layout), the macOS
    /// Control / Command swap undone (physicalQtKey)
    static PcKey mapQtKeyToPcKey(int qtKey);

    /// One host key for the emulator, as two messages: the ZX key (MC_KEY_*,
    /// the matrix) and the physical key (MC_PCKEY_*, a PS/2 keyboard, joystick
    /// bindings); each goes to its own handler, the machine's route gates them.
    /// Tracks the physical keys held down (see postHeldKeyReleases). An empty
    /// targetId broadcasts. macOS: the Command key and keys pressed while it is held stay with the host
    /// (commandKeyToGuest); the physical Control key is the machine's Ctrl
    static void postHostKey(const QKeyEvent* event, KeyEventEnum type, const std::string& targetId);

    /// Post releases of every key still held (the window lost the key events:
    /// focus change, full screen toggle): the physical keys, so a PS/2 machine
    /// sees them go up, and the ZX matrix keys, whose press counters would
    /// otherwise keep the key down even through a later press and release
    static void postHeldKeyReleases(const std::string& targetId);
    /// The keys posted as held (tests)
    static size_t heldPcKeyCount() { return _heldPcKeys.size(); }
    static size_t heldMatrixKeyCount() { return _heldMatrixKeys.size(); }
    /// A key held on the host keyboard (the on-screen keyboard lights it)
    static bool pcKeyHeld(PcKey key) { return _heldPcKeys.count(static_cast<uint8_t>(key)) != 0; }
    /// The matrix keys held on the host keyboard as posted: an extended key (host Left: ZXKEY_EXT_LEFT) as itself
    static const std::multiset<uint8_t>& heldMatrixKeys() { return _heldMatrixKeys; }

    /// App shortcuts and the machine's keys (docs/features/keyboard.md "App shortcuts and the machine's keys"):
    /// while the emulator view has focus, the machine owns
    /// - Ctrl + any key without Alt, on every machine: Ctrl is Symbol Shift on the matrix and Ctrl on a PC
    ///   keyboard, so Ctrl+O / Ctrl+S / Ctrl+Q type (';', SS+S, '<=') instead of opening, saving or quitting;
    /// - where the host keyboard reaches a PS/2 keyboard (route ps2 or both; Auto on the ZX-Evo, TS-Conf, ATM
    ///   Turbo 2+ and the Sprinter): Alt + any key (no menu mnemonics: DSS, Flex Navigator, Wild Commander use
    ///   them) and the bare function keys F1-F12 (Shift allowed: BIOS SETUP's F4 skip, F-key file managers).
    /// The screen accepts the key's ShortcutOverride, so the shortcut does not fire and the key reaches the
    /// machine. The full screen key (fullScreenSequence) and Cmd / Win combinations stay with the GUI everywhere
    static bool machineOwnsKey(const QKeyEvent* event, const Keyboard* keyboard);
    /// View > Full Screen: Cmd+F on macOS, Alt+Enter elsewhere (Ctrl+F is Symbol Shift + F, TO in 48K BASIC)
    static QKeyCombination fullScreenKey();
    /// The event is the full screen key (Alt + the keypad Enter too)
    static bool isFullScreenKey(const QKeyEvent* event);

protected:
    /// The macOS Command key (left or right) on a macOS modifier layout
    static bool isCommandKey(const QKeyEvent* event, PcKey pcKey);

    static std::map<quint32, ZXKeysEnum> _keyMap;
    static std::optional<bool> _macKeyboardOverride;
    static bool _commandKeyToGuest;
    static std::set<uint8_t> _heldPcKeys;
    /// One entry per matrix press posted and not released (a key forwarded twice counts twice, as the
    /// keyboard's press counters do)
    static std::multiset<uint8_t> _heldMatrixKeys;
};

#endif // KEYBOARDMANAGER_H
