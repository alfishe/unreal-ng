#pragma once
#include "stdafx.h"

#include "3rdparty/message-center/messagecenter.h"

class EmulatorContext;

/// region <Structs and Enums>

constexpr uint8_t KEYS_COUNT = 40;

enum KeyEventEnum : uint8_t
{
    KEY_PRESSED,
    KEY_RELEASED
};

extern const char* const MC_KEY_PRESSED;
extern const char* const MC_KEY_RELEASED;

// 40 Buttons for original ZX-Spectrum
enum ZXKeysEnum : uint8_t
{
    ZXKEY_NONE          = 0x00,     // Non-existent key, used in mapping
    ZXKEY_CAPS_SHIFT    = 0x04,
    ZXKEY_SYM_SHIFT     = 0x05,
    ZXKEY_ENTER         = 0x0A,
    ZXKEY_SPACE         = 0x20,
    ZXKEY_0             = 0x30,
    ZXKEY_1             = 0x31,
    ZXKEY_2             = 0x32,
    ZXKEY_3             = 0x33,
    ZXKEY_4             = 0x34,
    ZXKEY_5             = 0x35,
    ZXKEY_6             = 0x36,
    ZXKEY_7             = 0x37,
    ZXKEY_8             = 0x38,
    ZXKEY_9             = 0x39,
    ZXKEY_A             = 0x41,
    ZXKEY_B             = 0x42,
    ZXKEY_C             = 0x43,
    ZXKEY_D             = 0x44,
    ZXKEY_E             = 0x45,
    ZXKEY_F             = 0x46,
    ZXKEY_G             = 0x47,
    ZXKEY_I             = 0x48,
    ZXKEY_H             = 0x49,
    ZXKEY_J             = 0x4A,
    ZXKEY_K             = 0x4B,
    ZXKEY_L             = 0x4C,
    ZXKEY_M             = 0x4D,
    ZXKEY_N             = 0x4E,
    ZXKEY_O             = 0x4F,
    ZXKEY_P             = 0x50,
    ZXKEY_Q             = 0x51,
    ZXKEY_R             = 0x52,
    ZXKEY_S             = 0x53,
    ZXKEY_T             = 0x54,
    ZXKEY_U             = 0x55,
    ZXKEY_V             = 0x56,
    ZXKEY_W             = 0x57,
    ZXKEY_X             = 0x58,
    ZXKEY_Y             = 0x59,
    ZXKEY_Z             = 0x5A,

    // Extended keys for 128k and newer models (combination of existing keys)
    // See mappings: http://slady.net/Sinclair-ZX-Spectrum-keyboard/
    ZXKEY_EXT_CTRL      = 0x80,
    ZXKEY_EXT_UP        = 0x81,       // ZXKEY_CAPS_SHIFT + ZXKEY_7
    ZXKEY_EXT_DOWN      = 0x82,       // ZXKEY_CAPS_SHIFT + ZXKEY_6
    ZXKEY_EXT_LEFT      = 0x83,       // ZXKEY_CAPS_SHIFT + ZXKEY_5
    ZXKEY_EXT_RIGHT     = 0x84,       // ZXKEY_CAPS_SHIFT + ZXKEY_8
    ZXKEY_EXT_DELETE    = 0x85,       // ZXKEY_CAPS_SHIFT + ZXKEY_0
    ZXKEY_EXT_BREAK     = 0x86,       // ZXKEY_CAPS_SHIFT + ZXKEY_SPACE
    ZXKEY_EXT_DOT       = 0x87,       // ZXKEY_SYM_SHIFT  + ZXKEY_M       '.'
    ZXKEY_EXT_COMMA     = 0x88,       // ZXKEY_SYM_SHIFT  + ZXKEY_N       ','
    ZXKEY_EXT_PLUS      = 0x89,       // ZXKEY_SYM_SHIFT  + ZXKEY_K       '+'
    ZXKEY_EXT_MINUS     = 0x8A,       // ZXKEY_SYM_SHIFT  + ZXKEY_J       '-'
    ZXKEY_EXT_MULTIPLY  = 0x8B,       // ZXKEY_SYM_SHIFT  + ZXKEY_B       '*'
    ZXKEY_EXT_DIVIDE    = 0x8C,       // ZXKEY_SYM_SHIFT  + ZXKEY_V       '/'
    ZXKEY_EXT_BAR       = 0x8D,       // ZXKEY_SYM_SHIFT  + ZXKEY_S       '|'
    ZXKEY_EXT_CAPSLOCK  = 0x8E,       // ZXKEY_CAPS_SHIFT + ZXKEY_2
};

// Standardized keys to map host input events
enum KeysEnum : uint8_t
{
    KEY_LEFT,
    KEY_RIGHT,
    KEY_UP,
    KEY_DOWN
};

struct KeyDescriptor
{
    ZXKeysEnum key;
    uint8_t mask;
    uint8_t match;
    uint16_t port;

    uint8_t matrix_offset;

    const char* name;
};


struct KeyMapper
{
    ZXKeysEnum extendedKey;     // Key we're mapping to combination
    ZXKeysEnum modifier;        // Symbol Shift or Caps Shift modifier
    ZXKeysEnum key;             // Additional key to press
};

typedef std::map<ZXKeysEnum, KeyDescriptor> ZXKeyMap;
typedef std::map<ZXKeysEnum, KeyMapper> ZXExtendedKeyMap;

class KeyboardEvent : public MessagePayload
{
public:
    uint8_t zxKeyCode = 0x00;
    KeyEventEnum eventType;

public:
    KeyboardEvent(uint8_t zxKey, KeyEventEnum type) : MessagePayload() { this->zxKeyCode = zxKey; this->eventType = type; };
    virtual ~KeyboardEvent() {};
};

/// endregion </Structs and Enums>

/// See: http://www.breakintoprogram.co.uk/computers/zx-spectrum/keyboard
/// See: https://www.salkin.co.uk/~wiki/index.php/Spectrum_Keyboard
/// See: http://slady.net/Sinclair-ZX-Spectrum-keyboard/
class Keyboard : public Observer
{
    /// region <Constants>

    //    Port	    Dec	    Bin	                    Address line	D0	        D1	        D2	D3	D4
    //    $FEFE	    65278	%1111 1110 1111 1110	A8	            Caps shift	Z	        X	C	V
    //    $FDFE	    65022	%1111 1101 1111 1110	A9	            A	        S	        D	F	G
    //    $FBFE	    64510	%1111 1011 1111 1110	A10	            Q	        W	        E	R	T
    //    $F7FE	    63486	%1111 0111 1111 1110	A11	            1	        2	        3	4	5
    //    $EFFE	    61438	%1110 1111 1111 1110	A12	            0	        9	        8	7	6
    //    $DFFE	    57342	%1101 1111 1111 1110	A13	            P	        O	        I	U	Y
    //    $BFFE	    49150	%1011 1111 1111 1110	A14	            Ent	        L	        K	J	H
    //    $7FFE	    32766	%0111 1111 1111 1110	A15	            Spc	        Sym shift	M	N	B

    static constexpr KeyDescriptor _keys[KEYS_COUNT] =
    {
        { ZXKEY_CAPS_SHIFT,   0b0001'1111, 0b0001'1110, 0xFEFE, 0, "ZXKEY_CAPS_SHIFT" },
        { ZXKEY_Z,            0b0001'1111, 0b0001'1101, 0xFEFE, 0, "ZXKEY_Z"},
        { ZXKEY_X,            0b0001'1111, 0b0001'1011, 0xFEFE, 0, "ZXKEY_X" },
        { ZXKEY_C,            0b0001'1111, 0b0001'0111, 0xFEFE, 0, "ZXKEY_C" },
        { ZXKEY_V,            0b0001'1111, 0b0000'1111, 0xFEFE, 0, "ZDKEY_V" },

        { ZXKEY_A,            0b0001'1111, 0b0001'1110, 0xFDFE, 1, "ZXKEY_A" },
        { ZXKEY_S,            0b0001'1111, 0b0001'1101, 0xFDFE, 1, "ZXKEY_S" },
        { ZXKEY_D,            0b0001'1111, 0b0001'1011, 0xFDFE, 1, "ZXKEY_D" },
        { ZXKEY_F,            0b0001'1111, 0b0001'0111, 0xFDFE, 1, "ZXKEY_F"},
        { ZXKEY_G,            0b0001'1111, 0b0000'1111, 0xFDFE, 1, "ZXKEY_G"},

        { ZXKEY_Q,            0b0001'1111, 0b0001'1110, 0xFBFE, 2, "ZXKEY_Q" },
        { ZXKEY_W,            0b0001'1111, 0b0001'1101, 0xFBFE, 2, "ZXKEY_W" },
        { ZXKEY_E,            0b0001'1111, 0b0001'1011, 0xFBFE, 2, "ZXKEY_E" },
        { ZXKEY_R,            0b0001'1111, 0b0001'0111, 0xFBFE, 2, "ZXKEY_R"},
        { ZXKEY_T,            0b0001'1111, 0b0000'1111, 0xFBFE, 2, "ZXKEY_T" },

        { ZXKEY_1,            0b0001'1111, 0b0001'1110, 0xF7FE, 3, "ZXKEY_1" },
        { ZXKEY_2,            0b0001'1111, 0b0001'1101, 0xF7FE, 3, "ZXKEY_2" },
        { ZXKEY_3,            0b0001'1111, 0b0001'1011, 0xF7FE, 3, "ZXKEY_3" },
        { ZXKEY_4,            0b0001'1111, 0b0001'0111, 0xF7FE, 3, "ZXKEY_4" },
        { ZXKEY_5,            0b0001'1111, 0b0000'1111, 0xF7FE, 3, "ZXKEY_5" },

        { ZXKEY_0,            0b0001'1111, 0b0001'1110, 0xEFFE, 4, "ZXKEY_0" },
        { ZXKEY_9,            0b0001'1111, 0b0001'1101, 0xEFFE, 4, "ZXKEY_9" },
        { ZXKEY_8,            0b0001'1111, 0b0001'1011, 0xEFFE, 4, "ZXKEY_8" },
        { ZXKEY_7,            0b0001'1111, 0b0001'0111, 0xEFFE, 4, "ZXKEY_7" },
        { ZXKEY_6,            0b0001'1111, 0b0000'1111, 0xEFFE, 4, "ZXKEY_6" },

        { ZXKEY_P,            0b0001'1111, 0b0001'1110, 0xDFFE, 5, "ZXKEY_P" },
        { ZXKEY_O,            0b0001'1111, 0b0001'1101, 0xDFFE, 5, "ZXKEY_O" },
        { ZXKEY_I,            0b0001'1111, 0b0001'1011, 0xDFFE, 5, "ZXKEY_I" },
        { ZXKEY_U,            0b0001'1111, 0b0001'0111, 0xDFFE, 5, "ZXKEY_U" },
        { ZXKEY_Y,            0b0001'1111, 0b0000'1111, 0xDFFE, 5, "ZXKEY_Y" },

        { ZXKEY_ENTER,        0b0001'1111, 0b0001'1110, 0xBFFE, 6, "ZXKEY_ENTER" },
        { ZXKEY_L,            0b0001'1111, 0b0001'1101, 0xBFFE, 6, "ZXKEY_L" },
        { ZXKEY_K,            0b0001'1111, 0b0001'1011, 0xBFFE, 6, "ZXKEY_K" },
        { ZXKEY_J,            0b0001'1111, 0b0001'0111, 0xBFFE, 6, "ZXKEY_J" },
        { ZXKEY_H,            0b0001'1111, 0b0000'1111, 0xBFFE, 6, "ZXKEY_H" },

        { ZXKEY_SPACE,        0b0001'1111, 0b0001'1110, 0x7FFE, 7, "ZXKEY_SPACE" },
        { ZXKEY_SYM_SHIFT,    0b0001'1111, 0b0001'1101, 0x7FFE, 7, "ZXKEY_SYM_SHIFT" },
        { ZXKEY_M,            0b0001'1111, 0b0001'1011, 0x7FFE, 7, "ZXKEY_M" },
        { ZXKEY_N,            0b0001'1111, 0b0001'0111, 0x7FFE, 7, "ZXKEY_N" },
        { ZXKEY_B,            0b0001'1111, 0b0000'1111, 0x7FFE, 7, "ZXKEY_B" }
    };

    // Mapping special symbols to key combinations
    // See: http://slady.net/Sinclair-ZX-Spectrum-keyboard/
    /*
    static constexpr KeyMapper mapper[] =
    {
        { ',', ZXKEY_SYM_SHIFT, ZXKEY_N },
        { '.', ZXKEY_SYM_SHIFT, ZXKEY_M },
        { ':', ZXKEY_SYM_SHIFT, ZXKEY_Z },
        { ';', ZXKEY_SYM_SHIFT, ZXKEY_O },
        { '!', ZXKEY_SYM_SHIFT, ZXKEY_1 },
        { '?', ZXKEY_SYM_SHIFT, ZXKEY_C },
        { '"', ZXKEY_SYM_SHIFT, ZXKEY_P },
        { '\'', ZXKEY_SYM_SHIFT, ZXKEY_7 },
        { '#', ZXKEY_SYM_SHIFT, ZXKEY_3 },
        { '$', ZXKEY_SYM_SHIFT, ZXKEY_4 },
        //{ '<BRITISH POUND>', ZXKEY_SYM_SHIFT, ZXKEY_X },
        { '%', ZXKEY_SYM_SHIFT, ZXKEY_4 },
        { '&', ZXKEY_SYM_SHIFT, ZXKEY_6 },
        { '@', ZXKEY_SYM_SHIFT, ZXKEY_Z },
        //{ '<copyright>', ZXKEY_SYM_SHIFT, ZXKEY_P },
        { '+', ZXKEY_SYM_SHIFT, ZXKEY_K },
        { '-', ZXKEY_SYM_SHIFT, ZXKEY_J },
        { '*', ZXKEY_SYM_SHIFT, ZXKEY_B },
        { '/', ZXKEY_SYM_SHIFT, ZXKEY_V },
        { '}', ZXKEY_SYM_SHIFT, ZXKEY_S },
        { '\\', ZXKEY_SYM_SHIFT, ZXKEY_D },
        { '(', ZXKEY_SYM_SHIFT, ZXKEY_8 },
        { ')', ZXKEY_SYM_SHIFT, ZXKEY_9 },
        { '[', ZXKEY_SYM_SHIFT, ZXKEY_Y },
        { ']', ZXKEY_SYM_SHIFT, ZXKEY_U },
        { '{', ZXKEY_SYM_SHIFT, ZXKEY_F },
        { '}', ZXKEY_SYM_SHIFT, ZXKEY_G },
        { '<', ZXKEY_SYM_SHIFT, ZXKEY_R },
        { '>', ZXKEY_SYM_SHIFT, ZXKEY_T },
        { '=', ZXKEY_SYM_SHIFT, ZXKEY_L },
        //{ '<=', ZXKEY_SYM_SHIFT, ZXKEY_Q },
        //{ '>=', ZXKEY_SYM_SHIFT, ZXKEY_E },
        { '^', ZXKEY_SYM_SHIFT, ZXKEY_H },
        { '_', ZXKEY_SYM_SHIFT, ZXKEY_O},
        { '~', ZXKEY_SYM_SHIFT, ZXKEY_A },

        //{ '<Left arrow>', ZXKEY_CAPS_SHIFT, ZXKEY_5 },
        //{ '<Right arrow>', ZXKEY_CAPS_SHIFT, ZXKEY_8 },
        //{ '<Up arrow>', ZXKEY_CAPS_SHIFT, ZXKEY_6 },
        //{ '<Down arrow>', ZXKEY_CAPS_SHIFT, ZXKEY_6 },
    };
    */

    /// endregion </Constants>

    /// region <Fields>
protected:
    static ZXKeyMap _zxKeyMap;
    static ZXExtendedKeyMap _zxExtendedKeyMap;

protected:
    EmulatorContext* _context;

    uint8_t _keyboardMatrixState[8];
    std::map<ZXKeysEnum, uint8_t> _keyboardPressedKeys;

    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    Keyboard() = delete;    // Disable default constructor. C++ 11 feature
    Keyboard(EmulatorContext* context);
    virtual ~Keyboard();
    /// endregion </Constructors / Destructors>

    /// region <Keyboard control>
public:
    void Reset();
    void PressKey(ZXKeysEnum key);
    void ReleaseKey(ZXKeysEnum key);
    void TypeSymbol(char symbol);
    void SendKeyCombination();
    /// endregion </Keyboard control>

    /// region <Helper methods>
protected:
    bool IsExtendedKey(ZXKeysEnum key);
    ZXKeysEnum GetExtendedKeyBase(ZXKeysEnum key);
    ZXKeysEnum GetExtendedKeyModifier(ZXKeysEnum key);

    uint8_t IncreaseKeyPressCounter(ZXKeysEnum key);
    uint8_t DecreaseKeyPressCounter(ZXKeysEnum key);

    bool AnyKeyWithSimilarModifier(ZXKeysEnum key);

    /// endregion </Helper methods>

    /// region <Handle keyboard events>
public:
    uint8_t HandlePort(uint16_t port);                                                  // Respond on port IN request from Z80
    void OnKey(ZXKeysEnum key, bool isPressed, bool shift, bool ctrl, bool alt);        // Translate host keyboard event to ZX-Spectrum
    /// endregion </Handle keyboard events>

    /// region <Handle MessageCenter keyboard events>
public:
    void OnKeyPressed(int id, Message* message);
    void OnKeyReleased(int id, Message* message);
    /// endregion </Handle MessageCenter keyboard events>

    /// region <Debug>
#ifdef _DEBUG
public:
    std::string DumpKeyboardState();
#endif
    /// endregion </Debug>
};
