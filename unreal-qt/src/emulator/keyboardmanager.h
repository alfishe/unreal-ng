#pragma once

#ifndef KEYBOARDMANAGER_H
#define KEYBOARDMANAGER_H

#include <Qt>
#include <QtGlobal>

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
    static quint8 mapQtKeyToEmulatorKey(int qtKey);
    static quint8 mapQtKeyToEmulatorKeyWithModifiers(int qtKey, Qt::KeyboardModifiers modifiers);

    /// The physical key of a host key event, from the platform's native code
    /// (macOS virtual key, Windows scan code, Linux evdev keycode): the same key
    /// under any keyboard layout. Falls back to the Qt key on other platforms
    static PcKey mapQtEventToPcKey(const QKeyEvent* event);
    /// Layout-dependent fallback: Qt key -> physical key (US layout)
    static PcKey mapQtKeyToPcKey(int qtKey);

    /// One host key for the emulator, as two messages: the ZX key (MC_KEY_*,
    /// the matrix) and the physical key (MC_PCKEY_*, a PS/2 keyboard, joystick
    /// bindings); each goes to its own handler, the machine's route gates them.
    /// Tracks the physical keys held down (see postHeldKeyReleases). An empty
    /// targetId broadcasts
    static void postHostKey(const QKeyEvent* event, KeyEventEnum type, const std::string& targetId);

    /// Post releases of every key still held (the window lost the key events:
    /// focus change, full screen toggle): the physical keys, so a PS/2 machine
    /// sees them go up, and the ZX matrix keys, whose press counters would
    /// otherwise keep the key down even through a later press and release
    static void postHeldKeyReleases(const std::string& targetId);
    /// The keys posted as held (tests)
    static size_t heldPcKeyCount() { return _heldPcKeys.size(); }
    static size_t heldMatrixKeyCount() { return _heldMatrixKeys.size(); }

    /// Menu shortcuts and PC keyboards (docs/features/keyboard.md "Function keys on PC-keyboard machines"):
    /// while the emulator view has focus, a bare function key F1-F12 (Shift allowed; no Ctrl / Alt / Cmd)
    /// belongs to the machine when the host keyboard reaches its PS/2 keyboard (route ps2 or both; Auto on
    /// the ZX-Evo, TS-Conf, ATM Turbo 2+ and the Sprinter: BIOS SETUP's F4 skip, DSS and Flex Navigator use
    /// them). The screen accepts the key's ShortcutOverride, so the menu's bare F-key shortcuts (speed
    /// F1-F4, Start / Pause / Resume F5-F7, debugger F8-F11) do not fire and the key reaches the machine.
    /// Matrix-only machines (and the route "matrix") keep the shortcuts; Ctrl / Alt / Cmd combinations stay
    /// with the GUI everywhere
    static bool machineOwnsKey(const QKeyEvent* event, const Keyboard* keyboard);

protected:
    static std::map<quint32, ZXKeysEnum> _keyMap;
    static std::set<uint8_t> _heldPcKeys;
    /// One entry per matrix press posted and not released (a key forwarded twice counts twice, as the
    /// keyboard's press counters do)
    static std::multiset<uint8_t> _heldMatrixKeys;
};

#endif // KEYBOARDMANAGER_H
