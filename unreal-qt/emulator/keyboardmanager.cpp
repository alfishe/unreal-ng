#include "keyboardmanager.h"

#include <QDebug>

/// Populate mapping from Qt keycodes to unified emulator format
std::map<quint32, ZXKeysEnum> KeyboardManager::_keyMap =
{
    { Qt::Key_0, ZXKEY_0 },
    { Qt::Key_1, ZXKEY_1 },
    { Qt::Key_2, ZXKEY_2 },
    { Qt::Key_3, ZXKEY_3 },
    { Qt::Key_4, ZXKEY_4 },
    { Qt::Key_5, ZXKEY_5 },
    { Qt::Key_6, ZXKEY_6 },
    { Qt::Key_7, ZXKEY_7 },
    { Qt::Key_8, ZXKEY_8 },
    { Qt::Key_9, ZXKEY_9 },

    { Qt::Key_A, ZXKEY_A },
    { Qt::Key_B, ZXKEY_B },
    { Qt::Key_C, ZXKEY_C },
    { Qt::Key_D, ZXKEY_D },
    { Qt::Key_E, ZXKEY_E },
    { Qt::Key_F, ZXKEY_F },
    { Qt::Key_G, ZXKEY_G },
    { Qt::Key_H, ZXKEY_H },
    { Qt::Key_I, ZXKEY_I },
    { Qt::Key_J, ZXKEY_J },
    { Qt::Key_K, ZXKEY_K },
    { Qt::Key_L, ZXKEY_L },
    { Qt::Key_M, ZXKEY_M },
    { Qt::Key_N, ZXKEY_N },
    { Qt::Key_O, ZXKEY_O },
    { Qt::Key_P, ZXKEY_P },
    { Qt::Key_Q, ZXKEY_Q },
    { Qt::Key_R, ZXKEY_R },
    { Qt::Key_S, ZXKEY_S },
    { Qt::Key_T, ZXKEY_T },
    { Qt::Key_U, ZXKEY_U },
    { Qt::Key_V, ZXKEY_V },
    { Qt::Key_W, ZXKEY_W },
    { Qt::Key_X, ZXKEY_X },
    { Qt::Key_Y, ZXKEY_Y },
    { Qt::Key_Z, ZXKEY_Z },

    { Qt::Key_Control, ZXKEY_SYM_SHIFT },   // Ctrl on PC keyboard
    { Qt::Key_Meta, ZXKEY_SYM_SHIFT },      // Ctrl on Apple keyboard
    { Qt::Key_Shift, ZXKEY_CAPS_SHIFT },

    { Qt::Key_Space, ZXKEY_SPACE },
    { Qt::Key_Return, ZXKEY_ENTER },
    { Qt::Key_Enter, ZXKEY_ENTER },

    // Extended keys (combination of <modifier> + <base key>)
    { Qt::Key_Left, ZXKEY_EXT_LEFT },
    { Qt::Key_Right, ZXKEY_EXT_RIGHT },
    { Qt::Key_Up, ZXKEY_EXT_UP },
    { Qt::Key_Down, ZXKEY_EXT_DOWN },

    { Qt::Key_Backspace, ZXKEY_EXT_DELETE },
    { Qt::Key_CapsLock, ZXKEY_EXT_CAPSLOCK },
    { Qt::Key_QuoteLeft, ZXKEY_EXT_EDIT },

    { Qt::Key_Escape, ZXKEY_EXT_BREAK },
    { Qt::Key_Period, ZXKEY_EXT_DOT },
    { Qt::Key_Comma, ZXKEY_EXT_COMMA },
    { Qt::Key_Plus, ZXKEY_EXT_PLUS },
    { Qt::Key_Minus, ZXKEY_EXT_MINUS },
    { Qt::Key_multiply, ZXKEY_EXT_MULTIPLY },
    { Qt::Key_division, ZXKEY_EXT_DIVIDE },
    { Qt::Key_Equal, ZXKEY_EXT_EQUAL },
    // { Qt::Key_division, ZXKEY_EXT_BAR }, '|'
    { Qt::Key_Backslash, ZXKEY_EXT_BACKSLASH }
};

KeyboardManager::KeyboardManager()
{

}

KeyboardManager::~KeyboardManager()
{

}

quint8 KeyboardManager::mapQtKeyToEmulatorKey(int qtKey)
{
    quint8 result = ZXKEY_NONE;
    quint32 key = static_cast<quint32>(qtKey);

    if (key_exists(KeyboardManager::_keyMap, key))
    {
        result = static_cast<quint8>(_keyMap[key]);
    }
    else
    {
        qDebug() << "mapQtKeyToEmulatorKey: unknown mapping for qtKey: " << qtKey;
    }

    return result;
}
