#include "keyboardmanager.h"

#include <QDebug>
#include <QKeyEvent>

#include "3rdparty/message-center/messagecenter.h"

std::set<uint8_t> KeyboardManager::_heldPcKeys;
std::multiset<uint8_t> KeyboardManager::_heldMatrixKeys;

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
    { Qt::Key_Backslash, ZXKEY_EXT_BACKSLASH },

    { Qt::Key_QuoteDbl, ZXKEY_EXT_DBLQUOTE },
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

    // No matrix code is the normal answer for PS/2-only keys (F-keys, Tab,
    // navigation cluster): postHostKey logs only when a key event has
    // neither the ZX nor the physical code

    return result;
}

quint8 KeyboardManager::mapQtKeyToEmulatorKeyWithModifiers(int qtKey, Qt::KeyboardModifiers modifiers)
{
    quint8 result = ZXKEY_NONE;
    
    // Map shifted number keys back to base keys
    // When SHIFT+1 is pressed, Qt sends Qt::Key_Exclam instead of Qt::Key_1
    // We need to map these back so SHIFT+1 works correctly in the ZX Spectrum
    if (modifiers & Qt::ShiftModifier)
    {
        switch (qtKey)
        {
            case Qt::Key_Exclam:        qtKey = Qt::Key_1; break;  // ! -> 1
            case Qt::Key_At:            qtKey = Qt::Key_2; break;  // @ -> 2  
            case Qt::Key_NumberSign:    qtKey = Qt::Key_3; break;  // # -> 3
            case Qt::Key_Dollar:        qtKey = Qt::Key_4; break;  // $ -> 4
            case Qt::Key_Percent:       qtKey = Qt::Key_5; break;  // % -> 5
            case Qt::Key_AsciiCircum:   qtKey = Qt::Key_6; break;  // ^ -> 6
            case Qt::Key_Ampersand:     qtKey = Qt::Key_7; break;  // & -> 7
            case Qt::Key_Asterisk:      qtKey = Qt::Key_8; break;  // * -> 8
            case Qt::Key_ParenLeft:     qtKey = Qt::Key_9; break;  // ( -> 9
            case Qt::Key_ParenRight:    qtKey = Qt::Key_0; break;  // ) -> 0
            default:
                break;
        }
    }
    
    result = mapQtKeyToEmulatorKey(qtKey);
    return result;
}

PcKey KeyboardManager::mapQtKeyToPcKey(int qtKey)
{
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
        return static_cast<PcKey>(static_cast<int>(PcKey::A) + (qtKey - Qt::Key_A));
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F12)
        return static_cast<PcKey>(static_cast<int>(PcKey::Function1) + (qtKey - Qt::Key_F1));

    switch (qtKey)
    {
        case Qt::Key_1: case Qt::Key_Exclam: return PcKey::Digit1;
        case Qt::Key_2: case Qt::Key_At: return PcKey::Digit2;
        case Qt::Key_3: case Qt::Key_NumberSign: return PcKey::Digit3;
        case Qt::Key_4: case Qt::Key_Dollar: return PcKey::Digit4;
        case Qt::Key_5: case Qt::Key_Percent: return PcKey::Digit5;
        case Qt::Key_6: case Qt::Key_AsciiCircum: return PcKey::Digit6;
        case Qt::Key_7: case Qt::Key_Ampersand: return PcKey::Digit7;
        case Qt::Key_8: case Qt::Key_Asterisk: return PcKey::Digit8;
        case Qt::Key_9: case Qt::Key_ParenLeft: return PcKey::Digit9;
        case Qt::Key_0: case Qt::Key_ParenRight: return PcKey::Digit0;
        case Qt::Key_Escape: return PcKey::Escape;
        case Qt::Key_QuoteLeft: case Qt::Key_AsciiTilde: return PcKey::Backquote;
        case Qt::Key_Minus: case Qt::Key_Underscore: return PcKey::Minus;
        case Qt::Key_Equal: case Qt::Key_Plus: return PcKey::Equal;
        case Qt::Key_Backspace: return PcKey::Backspace;
        case Qt::Key_Tab: case Qt::Key_Backtab: return PcKey::Tab;
        case Qt::Key_BracketLeft: case Qt::Key_BraceLeft: return PcKey::LeftBracket;
        case Qt::Key_BracketRight: case Qt::Key_BraceRight: return PcKey::RightBracket;
        case Qt::Key_Backslash: case Qt::Key_Bar: return PcKey::Backslash;
        case Qt::Key_CapsLock: return PcKey::CapsLock;
        case Qt::Key_Semicolon: case Qt::Key_Colon: return PcKey::Semicolon;
        case Qt::Key_Apostrophe: case Qt::Key_QuoteDbl: return PcKey::Quote;
        case Qt::Key_Return: return PcKey::Enter;
        case Qt::Key_Enter: return PcKey::KeypadEnter;
        case Qt::Key_Shift: return PcKey::LeftShift;
        case Qt::Key_Comma: case Qt::Key_Less: return PcKey::Comma;
        case Qt::Key_Period: case Qt::Key_Greater: return PcKey::Period;
        case Qt::Key_Slash: case Qt::Key_Question: return PcKey::Slash;
        case Qt::Key_Control: return PcKey::LeftCtrl;
        case Qt::Key_Meta: return PcKey::LeftGui;
        case Qt::Key_Alt: return PcKey::LeftAlt;
        case Qt::Key_AltGr: return PcKey::RightAlt;
        case Qt::Key_Space: return PcKey::Space;
        case Qt::Key_Menu: return PcKey::Menu;
        case Qt::Key_Print: return PcKey::PrintScreen;
        case Qt::Key_ScrollLock: return PcKey::ScrollLock;
        case Qt::Key_Pause: return PcKey::Pause;
        case Qt::Key_Insert: return PcKey::Insert;
        case Qt::Key_Home: return PcKey::Home;
        case Qt::Key_PageUp: return PcKey::PageUp;
        case Qt::Key_Delete: return PcKey::Delete;
        case Qt::Key_End: return PcKey::End;
        case Qt::Key_PageDown: return PcKey::PageDown;
        case Qt::Key_Up: return PcKey::Up;
        case Qt::Key_Left: return PcKey::Left;
        case Qt::Key_Down: return PcKey::Down;
        case Qt::Key_Right: return PcKey::Right;
        case Qt::Key_NumLock: return PcKey::NumLock;
        default: return PcKey::None;
    }
}

PcKey KeyboardManager::mapQtEventToPcKey(const QKeyEvent* event)
{
    if (!event)
        return PcKey::None;

    PcKey key = PcKey::None;
#if defined(Q_OS_MACOS)
    key = pckey::FromMacVirtualKey(event->nativeVirtualKey());
#elif defined(Q_OS_WIN)
    key = pckey::FromWindowsScanCode(event->nativeScanCode());
#elif defined(Q_OS_LINUX)
    // X11 and Wayland keycodes are evdev codes + 8
    if (event->nativeScanCode() >= 8)
        key = pckey::FromLinuxEvdev(event->nativeScanCode() - 8);
#endif
    if (key == PcKey::None)
        key = mapQtKeyToPcKey(event->key());
    return key;
}

void KeyboardManager::postHostKey(const QKeyEvent* event, KeyEventEnum type, const std::string& targetId)
{
    if (!event)
        return;

    const quint8 zxKey = mapQtKeyToEmulatorKeyWithModifiers(event->key(), event->modifiers());
    const PcKey pcKey = mapQtEventToPcKey(event);
    if (zxKey == ZXKEY_NONE && pcKey == PcKey::None)
    {
        qDebug() << QString("postHostKey: no ZX or physical mapping for qtKey: 0x%1 (%2)")
                        .arg(event->key(), 0, 16)
                        .arg(event->key());
        return;
    }

    // The AVR keymap keeps the two shifts apart (kbmap.c): left -> Caps Shift,
    // right -> Symbol Shift. The plain map cannot tell them (Qt::Key_Shift is
    // both), so the physical key decides - the matrix key RShift+F12 combos
    // are built on (TS-Conf: Right Shift + F12 enters the BIOS setup)
    quint8 matrixKey = zxKey;
    if (pcKey == PcKey::RightShift)
        matrixKey = ZXKEY_SYM_SHIFT;
    else if (pcKey == PcKey::LeftShift)
        matrixKey = ZXKEY_CAPS_SHIFT;

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    if (pcKey != PcKey::None)
    {
        if (type == KEY_PRESSED)
            _heldPcKeys.insert(static_cast<uint8_t>(pcKey));
        else
            _heldPcKeys.erase(static_cast<uint8_t>(pcKey));
        messageCenter.Post(type == KEY_PRESSED ? MC_PCKEY_PRESSED : MC_PCKEY_RELEASED,
                           new PcKeyEvent(static_cast<uint8_t>(pcKey), type, targetId));
    }
    if (matrixKey != ZXKEY_NONE)
    {
        if (type == KEY_PRESSED)
            _heldMatrixKeys.insert(matrixKey);
        else if (auto it = _heldMatrixKeys.find(matrixKey); it != _heldMatrixKeys.end())
            _heldMatrixKeys.erase(it);
        messageCenter.Post(type == KEY_PRESSED ? MC_KEY_PRESSED : MC_KEY_RELEASED,
                           new KeyboardEvent(matrixKey, type, targetId));
    }
}

void KeyboardManager::postHeldKeyReleases(const std::string& targetId)
{
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    for (uint8_t pcKey : _heldPcKeys)
        messageCenter.Post(MC_PCKEY_RELEASED, new PcKeyEvent(pcKey, KEY_RELEASED, targetId));
    _heldPcKeys.clear();
    for (uint8_t matrixKey : _heldMatrixKeys)
        messageCenter.Post(MC_KEY_RELEASED, new KeyboardEvent(matrixKey, KEY_RELEASED, targetId));
    _heldMatrixKeys.clear();
}

bool KeyboardManager::machineOwnsKey(const QKeyEvent* event, const Keyboard* keyboard)
{
    if (!event || !keyboard || !keyboard->RoutesToPs2())
        return false;
    const int key = event->key();
    if (key < Qt::Key_F1 || key > Qt::Key_F12)
        return false;
    // Keypad and Shift do not make a GUI shortcut; Ctrl / Alt / Cmd (Meta) do
    const Qt::KeyboardModifiers gui = Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;
    return (event->modifiers() & gui) == 0;
}
