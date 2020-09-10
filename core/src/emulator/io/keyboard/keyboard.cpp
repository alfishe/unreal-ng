#include "stdafx.h"

#include "common/logger.h"

#include "keyboard.h"
#include "common/bithelper.h"
#include "common/collectionhelper.h"
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"

/// region <Static>

const char* const MC_KEY_PRESSED = "KEY_PRESSED";
const char* const MC_KEY_RELEASED = "KEY_RELEASED";

ZXKeyMap Keyboard::_zxKeyMap(
{
    { ZXKEY_CAPS_SHIFT, Keyboard::_keys[0] },
    { ZXKEY_Z, Keyboard::_keys[1] },
    { ZXKEY_X, Keyboard::_keys[2] },
    { ZXKEY_C, Keyboard::_keys[3] },
    { ZXKEY_V, Keyboard::_keys[4] },

    { ZXKEY_A, Keyboard::_keys[5] },
    { ZXKEY_S, Keyboard::_keys[6] },
    { ZXKEY_D, Keyboard::_keys[7] },
    { ZXKEY_F, Keyboard::_keys[8] },
    { ZXKEY_G, Keyboard::_keys[9] },

    { ZXKEY_Q, Keyboard::_keys[10] },
    { ZXKEY_W, Keyboard::_keys[11] },
    { ZXKEY_E, Keyboard::_keys[12] },
    { ZXKEY_R, Keyboard::_keys[13] },
    { ZXKEY_T, Keyboard::_keys[14] },

    { ZXKEY_1, Keyboard::_keys[15] },
    { ZXKEY_2, Keyboard::_keys[16] },
    { ZXKEY_3, Keyboard::_keys[17] },
    { ZXKEY_4, Keyboard::_keys[18] },
    { ZXKEY_5, Keyboard::_keys[19] },

    { ZXKEY_0, Keyboard::_keys[20] },
    { ZXKEY_9, Keyboard::_keys[21] },
    { ZXKEY_8, Keyboard::_keys[22] },
    { ZXKEY_7, Keyboard::_keys[23] },
    { ZXKEY_6, Keyboard::_keys[24] },

    { ZXKEY_P, Keyboard::_keys[25] },
    { ZXKEY_O, Keyboard::_keys[26] },
    { ZXKEY_I, Keyboard::_keys[27] },
    { ZXKEY_U, Keyboard::_keys[28] },
    { ZXKEY_Y, Keyboard::_keys[29] },

    { ZXKEY_ENTER, Keyboard::_keys[30] },
    { ZXKEY_L, Keyboard::_keys[31] },
    { ZXKEY_K, Keyboard::_keys[32] },
    { ZXKEY_J, Keyboard::_keys[33] },
    { ZXKEY_H, Keyboard::_keys[34] },

    { ZXKEY_SPACE, Keyboard::_keys[35] },
    { ZXKEY_SYM_SHIFT, Keyboard::_keys[36] },
    { ZXKEY_M, Keyboard::_keys[37] },
    { ZXKEY_N, Keyboard::_keys[38] },
    { ZXKEY_B, Keyboard::_keys[39] },
});

// Extended key mappings
ZXExtendedKeyMap Keyboard::_zxExtendedKeyMap(
{
    { ZXKEY_EXT_LEFT, {ZXKEY_EXT_LEFT, ZXKEY_CAPS_SHIFT, ZXKEY_5 }},
    { ZXKEY_EXT_RIGHT, {ZXKEY_EXT_RIGHT, ZXKEY_CAPS_SHIFT, ZXKEY_8 }},
    { ZXKEY_EXT_UP, {ZXKEY_EXT_UP, ZXKEY_CAPS_SHIFT, ZXKEY_7 }},
    { ZXKEY_EXT_DOWN, {ZXKEY_EXT_DOWN, ZXKEY_CAPS_SHIFT, ZXKEY_6 }},
});

/// endregion </Static>


/// region <Constructors / Destructors>

Keyboard::Keyboard(EmulatorContext *context)
{
    _context = context;

    // Do explicit state reset on instantiation
    Reset();

    // Subscribe on MessageCenter events
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);
    ObserverCallbackMethod callbackOnKeyPressed = static_cast<ObserverCallbackMethod>(&Keyboard::OnKeyPressed);
    ObserverCallbackMethod callbackOnKeyReleased = static_cast<ObserverCallbackMethod>(&Keyboard::OnKeyReleased);
    messageCenter.AddObserver(MC_KEY_PRESSED, observerInstance, callbackOnKeyPressed);
    messageCenter.AddObserver(MC_KEY_RELEASED, observerInstance, callbackOnKeyReleased);
}

Keyboard::~Keyboard()
{
    // Unsubscribe from MessageCenter events
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);
    ObserverCallbackMethod callbackOnKeyPressed = static_cast<ObserverCallbackMethod>(&Keyboard::OnKeyPressed);
    ObserverCallbackMethod callbackOnKeyReleased = static_cast<ObserverCallbackMethod>(&Keyboard::OnKeyReleased);
    messageCenter.RemoveObserver(MC_KEY_PRESSED, observerInstance, callbackOnKeyPressed);
    messageCenter.RemoveObserver(MC_KEY_RELEASED, observerInstance, callbackOnKeyReleased);
}

/// endregion </Constructors / Destructors>

/// region <Keyboard control>

/// Handle system reset
void Keyboard::Reset()
{
    // Clear ZX-Spectrum keyboard matrix state (0xFF default state)
    memset(_keyboardMatrixState, 0xFF, sizeof (_keyboardMatrixState));

    // Clear list with pressed keys
    _keyboardPressedKeys.clear();
}

/// Register key press in keyboard matrix state
/// \param key ZX Spectrum key pressed
void Keyboard::PressKey(ZXKeysEnum key)
{
    KeyDescriptor keyDescriptor = _zxKeyMap[key];
    uint8_t matrixIndex = keyDescriptor.matrix_offset;
    uint8_t keyBits = ~keyDescriptor.mask | keyDescriptor.match;

    // Resetting the bit that corresponds to ZX-Spectrum key in half-row state byte
    _keyboardMatrixState[matrixIndex] &= keyBits;
}

void Keyboard::ReleaseKey(ZXKeysEnum key)
{
    KeyDescriptor keyDescriptor = _zxKeyMap[key];
    uint8_t matrixIndex = keyDescriptor.matrix_offset;
    uint8_t keyBits = ~keyDescriptor.mask | ~keyDescriptor.match;

    // Setting the bit that corresponds to ZX-Spectrum key in half-row state byte
    _keyboardMatrixState[matrixIndex] |= keyBits;
}

void Keyboard::TypeSymbol(char symbol)
{

}

void Keyboard::SendKeyCombination()
{

}

/// endregion </Keyboard control>

/// region <Helper methods>

bool Keyboard::IsExtendedKey(ZXKeysEnum key)
{
    bool result = key >= ZXKEY_EXT_CTRL;

    return result;
}

ZXKeysEnum Keyboard::GetExtendedKeyBase(ZXKeysEnum key)
{
    ZXKeysEnum result = ZXKEY_NONE;

    if (IsExtendedKey(key))
    {
        if (key_exists(_zxExtendedKeyMap, key))
        {
            KeyMapper mapper = _zxExtendedKeyMap[key];
            result = mapper.key;
        }
    }
    else
    {
        // Not really an extended key
        result = key;
    }

    return result;
}

ZXKeysEnum Keyboard::GetExtendedKeyModifier(ZXKeysEnum key)
{
    ZXKeysEnum result = ZXKEY_NONE;

    // Check extended keys - they all have modifiers
    if (IsExtendedKey(key) && key_exists(_zxExtendedKeyMap, key))
    {
        KeyMapper mapper = _zxExtendedKeyMap[key];
        result = mapper.modifier;
    }
    else
    {
        // But modifier keys are modifiers even if pressed alone
        if (key == ZXKEY_CAPS_SHIFT || key == ZXKEY_SYM_SHIFT)
        {
            result = key;
        }
    }

    return result;
}

uint8_t Keyboard::IncreaseKeyPressCounter(ZXKeysEnum key)
{
    uint8_t result = 0;

    if (IsExtendedKey(key))
    {
        throw std::logic_error("Only base keys can be processed. Split extended key to combination of base key + modifier key");
    }

    if (!key_exists(_keyboardPressedKeys, key))
    {
        _keyboardPressedKeys.insert( {key, 0} );
    }

    result = _keyboardPressedKeys[key] + 1;
    _keyboardPressedKeys[key] = result;

    return result;
}

uint8_t Keyboard::DecreaseKeyPressCounter(ZXKeysEnum key)
{
    uint8_t result = 0;

    if (IsExtendedKey(key))
    {
        throw std::logic_error("Only base keys can be processed. Split extended key to combination of base key + modifier key");
    }

    if (key_exists(_keyboardPressedKeys, key))
    {
        int keyAccessCounter = _keyboardPressedKeys[key] - 1;
        if (keyAccessCounter <= 0)
        {
            erase_from_collection(_keyboardPressedKeys, key);
            result = 0;
        }
        else
        {
            result = static_cast<uint8_t>(keyAccessCounter);
            _keyboardPressedKeys[key] = result;
        }
    }

    return result;
}

bool Keyboard::AnyKeyWithSimilarModifier(ZXKeysEnum key)
{
    bool result = false;

    // If no keys pressed - no chance to collide anyway
    if (_keyboardPressedKeys.size() > 0)
    {
        ZXKeysEnum modifier = GetExtendedKeyModifier(key);

        if (modifier != ZXKEY_NONE)
        {
            std::map<ZXKeysEnum, uint8_t>::iterator it;
            for (it = _keyboardPressedKeys.begin(); it != _keyboardPressedKeys.end(); it++) {
                ZXKeysEnum curKey = it->first;
                ZXKeysEnum curModifier = GetExtendedKeyModifier(curKey);

                if (curModifier == modifier)
                {
                    result = true;
                    break;
                }
            }
        }
    }

    return result;
}

/// endregion </Helper methods>

/// region <Handle keyboard events>

/// Respond on port IN request from Z80
/// \param port
/// \return
uint8_t Keyboard::HandlePort(uint16_t port)
{
    uint8_t result = 0xFF;

    /// region <Info>

    //    Port	    Dec	    Bin	                    Address line	D0	        D1	        D2	D3	D4
    //    $FEFE	    65278	%1111 1110 1111 1110	A8	            Caps shift	Z	        X	C	V
    //    $FDFE	    65022	%1111 1101 1111 1110	A9	            A	        S	        D	F	G
    //    $FBFE	    64510	%1111 1011 1111 1110	A10	            Q	        W	        E	R	T
    //    $F7FE	    63486	%1111 0111 1111 1110	A11	            1	        2	        3	4	5
    //    $EFFE	    61438	%1110 1111 1111 1110	A12	            0	        9	        8	7	6
    //    $DFFE	    57342	%1101 1111 1111 1110	A13	            P	        O	        I	U	Y
    //    $BFFE	    49150	%1011 1111 1111 1110	A14	            Ent	        L	        K	J	H
    //    $7FFE	    3766	%0111 1111 1111 1110	A15	            Spc	        Sym shift	M	N	B

    /// endregion </Info>

    uint8_t portFE = port & 0x00FF;     // Lower byte for the port. Should be always #FE
    uint8_t subport = port >> 8;        // Higher byte for the port. Should contain single zero bit indicating half-row of keys where key is pressed
    uint8_t subport_inv = ~subport;     // Inverted hi-byte (single set bit will point to pressed key half-row)
    uint8_t matrix_index = 0xFF;

    if (portFE == 0xFE)
    {
        // Find index of single set bit (corresponds to reset bit in high-byte of #FE port IN request)
        matrix_index = BitHelper::GetFirstSetBitPosition(subport_inv);

        if (matrix_index != 0xFF)
        {
            result = _keyboardMatrixState[matrix_index];
        }
    }
    else
    {
        LOGERROR("Keyboard cannot handle non #FE port");
        assert("Keyboard non-#FE port");
    }

    return result;
}

/// Translate host keyboard event to ZX-Spectrum
/// See: http://slady.net/Sinclair-ZX-Spectrum-keyboard/
///
/// \param key
/// \param isPressed
/// \param shift
/// \param ctrl
/// \param alt
void Keyboard::OnKey(ZXKeysEnum key, bool isPressed, bool shift, bool ctrl, bool alt)
{
    // Cursor keys:
    // Left Arrow   - Caps Shift + 5
    // Right Arrow  - Caps Shift + 8
    // Up Arrow     - Caps Shift + 7
    // Down Arrow   - Caps Shift + 6


}

/// endregion </Handle keyboard events>

/// region <Handle MessageCenter keyboard events>
void Keyboard::OnKeyPressed(int id, Message* message)
{
    if (message == nullptr || message->obj == nullptr)
        return;

    KeyboardEvent* event = dynamic_cast<KeyboardEvent*>(message->obj);
    if (event && event->eventType == KEY_PRESSED)
    {
        ZXKeysEnum zxKey = static_cast<ZXKeysEnum>(event->zxKeyCode);
        ZXKeysEnum zxBase = GetExtendedKeyBase(zxKey);
        ZXKeysEnum zxModifier = GetExtendedKeyModifier(zxKey);

        // Incorrect constant dictionary data
        if (zxBase == ZXKEY_NONE)
        {
            throw std::logic_error("zxBase not resolved");
        }

        // Modifier first
        if (zxModifier != ZXKEY_NONE)
        {
            IncreaseKeyPressCounter(zxModifier);
            PressKey(zxModifier);
        }

        // Base key afterwards
        IncreaseKeyPressCounter(zxBase);
        PressKey(zxBase);

        LOGINFO("OnKeyPressed: 0x%02X", zxKey);
        printf("OnKeyPressed: 0x%02X\n", zxKey);

#ifdef _DEBUG
        LOGDEBUG("%s", DumpKeyboardState().c_str());
        printf("%s\n", DumpKeyboardState().c_str());
        fflush(stdout);
#endif
    }
}

void Keyboard::OnKeyReleased(int id, Message* message)
{
    if (message == nullptr || message->obj == nullptr)
        return;

    KeyboardEvent* event = dynamic_cast<KeyboardEvent*>(message->obj);
    if (event && event->eventType == KEY_RELEASED)
    {
        ZXKeysEnum zxKey = static_cast<ZXKeysEnum>(event->zxKeyCode);
        ZXKeysEnum zxBase = GetExtendedKeyBase(zxKey);
        ZXKeysEnum zxModifier = GetExtendedKeyModifier(zxKey);

        // Incorrect constant dictionary data
        if (zxBase == ZXKEY_NONE)
        {
            throw std::logic_error("zxBase not resolved");
        }


        // Modifier first
        if (zxModifier != ZXKEY_NONE)
        {
            if (DecreaseKeyPressCounter(zxModifier) == 0)
            {
                ReleaseKey(zxModifier);
            }
        }

        // Base key afterwards
        if (DecreaseKeyPressCounter(zxBase) == 0)
        {
            ReleaseKey(zxBase);
        }

        LOGINFO("OnKeyReleased: 0x%02X", zxKey);
        printf("OnKeyReleased: 0x%02X\n", zxKey);

#ifdef _DEBUG
        LOGDEBUG("%s", DumpKeyboardState().c_str());
        printf("%s\n", DumpKeyboardState().c_str());
        fflush(stdout);
#endif
    }
}

/// endregion </Handle MessageCenter keyboard events>

/// region <Debug>
#ifdef _DEBUG
std::string Keyboard::DumpKeyboardState()
{
    std::string result;
    std::stringstream ss;

    for (int i = 0; i < 8; i++)
    {
        ss << "  " << StringHelper::FormatBinary(_keyboardMatrixState[i]) << std::endl;

        uint8_t keyRow = _keyboardMatrixState[i];

        for (int j = 0; j < 5; j++)
        {
            KeyDescriptor descriptor = _keys[i * 5 + j];

            if ((keyRow & descriptor.mask) == descriptor.match)
            {
                if (result.size() > 0)
                    result += ", ";

                result += StringHelper::Format("%s", descriptor.name);
            }
        }
    }

    result = "Matrix:\n" + ss.str() + result;

    return result;
}
#endif
/// endregion </Debug>
