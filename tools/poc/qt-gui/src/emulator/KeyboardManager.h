#pragma once

#include <Qt>
#include <QtGlobal>
#include <map>

#ifdef HAS_EMULATOR_CORE
#include "emulator/io/keyboard/keyboard.h"
#else
// Stub definitions when core is not available
enum ZXKeysEnum : uint8_t {
    ZXKEY_NONE = 0,
    ZXKEY_0, ZXKEY_1, ZXKEY_2, ZXKEY_3, ZXKEY_4,
    ZXKEY_5, ZXKEY_6, ZXKEY_7, ZXKEY_8, ZXKEY_9,
    ZXKEY_A, ZXKEY_B, ZXKEY_C, ZXKEY_D, ZXKEY_E,
    ZXKEY_F, ZXKEY_G, ZXKEY_H, ZXKEY_I, ZXKEY_J,
    ZXKEY_K, ZXKEY_L, ZXKEY_M, ZXKEY_N, ZXKEY_O,
    ZXKEY_P, ZXKEY_Q, ZXKEY_R, ZXKEY_S, ZXKEY_T,
    ZXKEY_U, ZXKEY_V, ZXKEY_W, ZXKEY_X, ZXKEY_Y, ZXKEY_Z,
    ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT,
    ZXKEY_SPACE, ZXKEY_ENTER,
    ZXKEY_EXT_LEFT, ZXKEY_EXT_RIGHT, ZXKEY_EXT_UP, ZXKEY_EXT_DOWN,
    ZXKEY_EXT_DELETE, ZXKEY_EXT_CAPSLOCK, ZXKEY_EXT_EDIT,
    ZXKEY_EXT_BREAK, ZXKEY_EXT_DOT, ZXKEY_EXT_COMMA,
    ZXKEY_EXT_PLUS, ZXKEY_EXT_MINUS, ZXKEY_EXT_MULTIPLY, ZXKEY_EXT_DIVIDE,
    ZXKEY_EXT_EQUAL, ZXKEY_EXT_BACKSLASH, ZXKEY_EXT_DBLQUOTE
};
#endif

class KeyboardManager
{
public:
    KeyboardManager() = default;
    ~KeyboardManager() = default;

    static quint8 mapQtKeyToEmulatorKey(int qtKey);
    static quint8 mapQtKeyToEmulatorKeyWithModifiers(int qtKey, Qt::KeyboardModifiers modifiers);

private:
    static std::map<quint32, ZXKeysEnum> _keyMap;
};
