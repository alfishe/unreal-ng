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

    /// One host key event for the emulator: the ZX key and the physical key.
    /// nullptr when the key is neither. Tracks the physical keys held down
    /// (see postHeldKeyReleases). An empty targetId broadcasts
    static KeyboardEvent* createKeyboardEvent(const QKeyEvent* event, KeyEventEnum type, const std::string& targetId);

    /// Post releases of the physical keys still held (the window lost the key
    /// events: focus change, full screen toggle), so a PS/2 machine sees them go up
    static void postHeldKeyReleases(const std::string& targetId);

protected:
    static std::map<quint32, ZXKeysEnum> _keyMap;
    static std::set<uint8_t> _heldPcKeys;
};

#endif // KEYBOARDMANAGER_H
