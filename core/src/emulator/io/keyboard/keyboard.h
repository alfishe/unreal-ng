#pragma once
#include "stdafx.h"

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/platform.h"

#include <set>

class EmulatorContext;
class ModuleLogger;

/// region <Structs and Enums>

constexpr uint8_t KEYS_COUNT = 40;

enum KeyEventEnum : uint8_t
{
    KEY_PRESSED,
    KEY_RELEASED
};

extern const char* const MC_KEY_PRESSED;
extern const char* const MC_KEY_RELEASED;
/// A physical PC key (PcKeyEvent): its own topic, so the matrix and the PS/2
/// side are separate handlers, each with its own gate (HostKeyboardRoute)
extern const char* const MC_PCKEY_PRESSED;
extern const char* const MC_PCKEY_RELEASED;

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

    // The Orel BK-08's (LSY256) matrix keys beyond the 40: columns KD5 and KD7 of its eight rows (keyboard schematic
    // БУ5.103.474), read on port #FE bits 5 and 7 (the ROM's KEY-SCAN masks with AND #BF). Plain matrix keys, taken
    // only on a machine with that keyboard (Keyboard::HasBk08Matrix)
    ZXKEY_BK08_RUS      = 0x60,     // A8  D5  РУС
    ZXKEY_BK08_SHIFT    = 0x61,     // A8  D7  РЕГ (both keys)
    ZXKEY_BK08_BACKSLASH= 0x62,     // A9  D5  \ |
    ZXKEY_BK08_SLASH    = 0x63,     // A9  D7  / ? Ё
    ZXKEY_BK08_COMMA    = 0x64,     // A10 D5  , < Б
    ZXKEY_BK08_PERIOD   = 0x65,     // A10 D7  . > Ю
    ZXKEY_BK08_TILDE    = 0x66,     // A11 D5  ` ~ (left of 1)
    ZXKEY_BK08_TAB      = 0x67,     // A11 D7  ТАБ
    ZXKEY_BK08_MINUS    = 0x68,     // A12 D5  - _
    ZXKEY_BK08_EQUALS   = 0x69,     // A12 D7  = +
    ZXKEY_BK08_LBRACKET = 0x6A,     // A13 D5  [ { Х
    ZXKEY_BK08_RBRACKET = 0x6B,     // A13 D7  ] } Ъ
    ZXKEY_BK08_SEMICOLON= 0x6C,     // A14 D5  ; : Ж
    ZXKEY_BK08_QUOTE    = 0x6D,     // A14 D7  ' " Э
    ZXKEY_BK08_RA       = 0x6E,     // A15 D5  Р/А (both keys)
    ZXKEY_BK08_CAPS     = 0x6F,     // A15 D7  ЗГЛ

    // Extended keys for 128k and newer models (combination of existing keys)
    // See mappings: http://slady.net/Sinclair-ZX-Spectrum-keyboard/
    ZXKEY_EXT_CTRL      = 0x80,
    ZXKEY_EXT_UP,                   // ZXKEY_CAPS_SHIFT + ZXKEY_7
    ZXKEY_EXT_DOWN,                 // ZXKEY_CAPS_SHIFT + ZXKEY_6
    ZXKEY_EXT_LEFT,                 // ZXKEY_CAPS_SHIFT + ZXKEY_5
    ZXKEY_EXT_RIGHT,                // ZXKEY_CAPS_SHIFT + ZXKEY_8

    ZXKEY_EXT_DELETE,               // ZXKEY_CAPS_SHIFT + ZXKEY_0
    ZXKEY_EXT_BREAK,                // ZXKEY_CAPS_SHIFT + ZXKEY_SPACE
    ZXKEY_EXT_EDIT,                 // ZXKEY_CAPS_SHIFT + ZXKEY_1

    ZXKEY_EXT_DOT,                  // ZXKEY_SYM_SHIFT  + ZXKEY_M       '.'
    ZXKEY_EXT_COMMA,                // ZXKEY_SYM_SHIFT  + ZXKEY_N       ','
    ZXKEY_EXT_PLUS,                 // ZXKEY_SYM_SHIFT  + ZXKEY_K       '+'
    ZXKEY_EXT_MINUS,                // ZXKEY_SYM_SHIFT  + ZXKEY_J       '-'
    ZXKEY_EXT_MULTIPLY,             // ZXKEY_SYM_SHIFT  + ZXKEY_B       '*'
    ZXKEY_EXT_DIVIDE,               // ZXKEY_SYM_SHIFT  + ZXKEY_V       '/'
    ZXKEY_EXT_EQUAL,                // ZXKEY_SYM_SHIFT  + ZXKEY_L       '='
    ZXKEY_EXT_BAR,                  // ZXKEY_SYM_SHIFT  + ZXKEY_S       '|'
    ZXKEY_EXT_BACKSLASH,            // ZXKEY_SYM_SHIFT  + ZXKEY_D       '\'
    ZXKEY_EXT_CAPSLOCK,             // ZXKEY_CAPS_SHIFT + ZXKEY_2

    ZXKEY_EXT_DBLQUOTE,             // ZXKEY_SYM_SHIFT + ZXKEY_P        '"'

    // The rest of the ZX Next's extended keys (membrane.vhd, columns 5 and 6)
    ZXKEY_EXT_EXTEND,               // ZXKEY_CAPS_SHIFT + ZXKEY_SYM_SHIFT
    ZXKEY_EXT_GRAPH,                // ZXKEY_CAPS_SHIFT + ZXKEY_9
    ZXKEY_EXT_TRUEVIDEO,            // ZXKEY_CAPS_SHIFT + ZXKEY_3
    ZXKEY_EXT_INVVIDEO,             // ZXKEY_CAPS_SHIFT + ZXKEY_4
    ZXKEY_EXT_SEMICOLON,            // ZXKEY_SYM_SHIFT  + ZXKEY_O       ';'

    // Symbol Shift symbols of the matrix a host keyboard has keys for
    ZXKEY_EXT_APOSTROPHE,           // ZXKEY_SYM_SHIFT  + ZXKEY_7       '\''
    ZXKEY_EXT_COLON,                // ZXKEY_SYM_SHIFT  + ZXKEY_Z       ':'
    ZXKEY_EXT_QUESTION,             // ZXKEY_SYM_SHIFT  + ZXKEY_C       '?'
    ZXKEY_EXT_LESS,                 // ZXKEY_SYM_SHIFT  + ZXKEY_R       '<'
    ZXKEY_EXT_GREATER,              // ZXKEY_SYM_SHIFT  + ZXKEY_T       '>'
    ZXKEY_EXT_UNDERSCORE,           // ZXKEY_SYM_SHIFT  + ZXKEY_0       '_'
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

// Physical PC keys (pckey.h): only the declarations the keyboard needs
enum class PcKey : uint8_t;
class IPs2KeySink;

/// The keys a machine's keyboard has beyond the 40 of the matrix, which press matrix keys of their own accord: the
/// ZX Next's membrane columns 5 and 6 (membrane.vhd: EXTEND, GRAPH, the arrows, DELETE, ; " , . ...). Such a key is
/// one event, not its shift combination: the machine sees it apart (NR #B0 / #B1) and decides which matrix keys it
/// presses (ExtendedMatrixRow; the Next's NR #68 bit 4 cancels them). Called on the thread executing the machine,
/// from the place that applies journaled input
class IExtendedKeySink
{
public:
    virtual ~IExtendedKeySink() = default;

    /// The extended ZX key is one of this keyboard's own keys
    virtual bool OwnsExtendedKey(ZXKeysEnum key) const = 0;
    virtual void OnExtendedKey(ZXKeysEnum key, bool pressed) = 0;
    /// The matrix keys the held extended keys press on a half-row (0 = A8 ... 7 = A15), active low
    virtual uint8_t ExtendedMatrixRow(uint8_t row) const = 0;
    virtual void ReleaseExtendedKeys() = 0;
};

class KeyboardEvent : public MessagePayload
{
public:
    std::string targetEmulatorId;  // Specific emulator target (empty = broadcast to all)
    uint8_t zxKeyCode = 0x00;
    KeyEventEnum eventType;

public:
    // Existing constructor (backward compatible - broadcasts to all instances)
    KeyboardEvent(uint8_t zxKey, KeyEventEnum type) : MessagePayload() 
    { 
        this->zxKeyCode = zxKey; 
        this->eventType = type; 
    }
    
    // New constructor with target emulator ID for selective routing
    KeyboardEvent(uint8_t zxKey, KeyEventEnum type, const std::string& targetId) : MessagePayload() 
    { 
        this->zxKeyCode = zxKey; 
        this->eventType = type; 
        this->targetEmulatorId = targetId;
    }

    virtual ~KeyboardEvent() {};
};

/// A physical PC key from the host (MC_PCKEY_PRESSED / MC_PCKEY_RELEASED): the
/// front end posts it beside the ZX key event of the same host key. Machines
/// with a PS/2 keyboard (ZX-Evo AVR, ATM Turbo 2+ controller) and joystick key
/// bindings take it; a key with no ZX equivalent (F1, Home) has only this event
class PcKeyEvent : public MessagePayload
{
public:
    std::string targetEmulatorId;  // Specific emulator target (empty = broadcast to all)
    uint8_t pcKeyCode = 0x00;      // PcKey
    KeyEventEnum eventType;

    PcKeyEvent(uint8_t pcKey, KeyEventEnum type, const std::string& targetId) : MessagePayload()
    {
        this->pcKeyCode = pcKey;
        this->eventType = type;
        this->targetEmulatorId = targetId;
    }
    virtual ~PcKeyEvent() {};
};

/// Which side of the machine's keyboard the host keyboard reaches ([INPUT]
/// HostKeyboard=): the ZX matrix, the PS/2 controller, or both. Auto = both
/// where a PS/2 controller exists, the matrix elsewhere
enum class HostKeyboardRoute : uint8_t
{
    Auto = 0,
    Matrix = 1,
    Ps2 = 2,
    Both = 3,
};

/// endregion </Structs and Enums>

/// Handles keyboard events (key press/release events) coming from host via MessageCenter
/// and convert them to ZX-Spectrum key matrix polled by #xxFE ports
/// @See: http://www.breakintoprogram.co.uk/computers/zx-spectrum/keyboard
/// @See: https://www.salkin.co.uk/~wiki/index.php/Spectrum_Keyboard
/// @See: http://slady.net/Sinclair-ZX-Spectrum-keyboard/
class Keyboard : public Observer, public ttd::TTDSerializable
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_IO;
    const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_KEYBOARD;
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Constants>

    // [0:4] Lowest 5 bits are used for key half-rows polling
    // [5]   Bit 5 - not used (the Orel BK-08: a sixth key per half-row, _bk08Keys)
    // [6]   Bit 6 - Audio in (EAR input)
    // [7]   Bit 7 - not used (the Orel BK-08: a seventh key per half-row)

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

    /// The Orel BK-08's 16 extra positions (ZXKEY_BK08_*): bit 5 and bit 7 of each half-row
    static constexpr uint8_t BK08_KEYS_COUNT = 16;
    static constexpr KeyDescriptor _bk08Keys[BK08_KEYS_COUNT] =
    {
        { ZXKEY_BK08_RUS,       0b1010'0000, 0b1000'0000, 0xFEFE, 0, "ZXKEY_BK08_RUS" },
        { ZXKEY_BK08_SHIFT,     0b1010'0000, 0b0010'0000, 0xFEFE, 0, "ZXKEY_BK08_SHIFT" },
        { ZXKEY_BK08_BACKSLASH, 0b1010'0000, 0b1000'0000, 0xFDFE, 1, "ZXKEY_BK08_BACKSLASH" },
        { ZXKEY_BK08_SLASH,     0b1010'0000, 0b0010'0000, 0xFDFE, 1, "ZXKEY_BK08_SLASH" },
        { ZXKEY_BK08_COMMA,     0b1010'0000, 0b1000'0000, 0xFBFE, 2, "ZXKEY_BK08_COMMA" },
        { ZXKEY_BK08_PERIOD,    0b1010'0000, 0b0010'0000, 0xFBFE, 2, "ZXKEY_BK08_PERIOD" },
        { ZXKEY_BK08_TILDE,     0b1010'0000, 0b1000'0000, 0xF7FE, 3, "ZXKEY_BK08_TILDE" },
        { ZXKEY_BK08_TAB,       0b1010'0000, 0b0010'0000, 0xF7FE, 3, "ZXKEY_BK08_TAB" },
        { ZXKEY_BK08_MINUS,     0b1010'0000, 0b1000'0000, 0xEFFE, 4, "ZXKEY_BK08_MINUS" },
        { ZXKEY_BK08_EQUALS,    0b1010'0000, 0b0010'0000, 0xEFFE, 4, "ZXKEY_BK08_EQUALS" },
        { ZXKEY_BK08_LBRACKET,  0b1010'0000, 0b1000'0000, 0xDFFE, 5, "ZXKEY_BK08_LBRACKET" },
        { ZXKEY_BK08_RBRACKET,  0b1010'0000, 0b0010'0000, 0xDFFE, 5, "ZXKEY_BK08_RBRACKET" },
        { ZXKEY_BK08_SEMICOLON, 0b1010'0000, 0b1000'0000, 0xBFFE, 6, "ZXKEY_BK08_SEMICOLON" },
        { ZXKEY_BK08_QUOTE,     0b1010'0000, 0b0010'0000, 0xBFFE, 6, "ZXKEY_BK08_QUOTE" },
        { ZXKEY_BK08_RA,        0b1010'0000, 0b1000'0000, 0x7FFE, 7, "ZXKEY_BK08_RA" },
        { ZXKEY_BK08_CAPS,      0b1010'0000, 0b0010'0000, 0x7FFE, 7, "ZXKEY_BK08_CAPS" },
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

    /// Keyboard-joystick overlay (controller manager, retro-devices §5): the keys every Sinclair / Cursor / keys
    /// joystick holds, active low per half-row, ANDed into the matrix read. Wired in parallel with the keys, so a
    /// released stick never releases a key the user holds. Owned by ControllerManager (its TTD state), 0xFF = none
    uint8_t _overlay[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    /// Host (MessageCenter) key events ignored: a ZX-Poly group member takes
    /// its keys from the group, at frame boundaries, in step with the others
    bool _hostInputGated = false;

    /// The machine's PS/2 keyboard controller (ZX-Evo AVR), null on machines
    /// without one: physical key events are neither journaled nor applied there
    IPs2KeySink* _ps2Sink = nullptr;

    /// The machine's keys beyond the matrix (the Next's membrane), null on the others
    IExtendedKeySink* _extendedSink = nullptr;
    /// Host presses of the extended keys the sink owns, per key (host state, as _keyboardPressedKeys)
    std::map<ZXKeysEnum, uint8_t> _hostExtendedKeys;
    /// Host Symbol Shift symbols held down that let go of Caps Shift (HoldBackCapsShift), one entry per press
    std::multiset<ZXKeysEnum> _capsShiftHeldBack;

    /// The host keyboard's route (config [INPUT] HostKeyboard=, runtime SetHostRoute)
    HostKeyboardRoute _hostRoute = HostKeyboardRoute::Auto;
    bool _hostInputDetached = false;   // DetachHostInput: the owner's thread only

    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    Keyboard() = delete;    // Disable default constructor. C++ 11 feature
    Keyboard(EmulatorContext* context);
    virtual ~Keyboard();
    /// endregion </Constructors / Destructors>

    /// region <Keyboard control>
public:
    /// Matrix + pressed-key counters: the keyboard's state (TTD: KeyboardMatrix,
    /// engine decision 37 - key changes are journal events, the state they
    /// built is in every checkpoint). Also captured so a throwaway TTD display
    /// render can hand the live keyboard back untouched.
    struct InputState
    {
        uint8_t matrix[8];
        std::map<ZXKeysEnum, uint8_t> pressedKeys;
    };

    void Reset();
    InputState CaptureInputState() const;
    void RestoreInputState(const InputState& state);

    /// region <TTD (PeripheralId::KeyboardMatrix)>
    /// Blob: version, the 8 matrix rows, a pair count that is 0 (the host's
    /// pressed-key counters are host state, not the machine's: 2026-10-08).
    /// Blobs written before carry (key, count) pairs after it; they load the
    /// matrix and leave the counters alone. Variable size kept for them
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    bool TTDVariableSize() const override { return true; }
    void TTDSaveStateTo(std::vector<uint8_t>& out) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "KeyboardMatrix"; }
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::KeyboardMatrix; }
    uint64_t TTDHashState() const override;
    /// endregion </TTD>
    void PressKey(ZXKeysEnum key);
    void ReleaseKey(ZXKeysEnum key);
    void TypeSymbol(char symbol);
    void SendKeyCombination();
    /// endregion </Keyboard control>

    /// region <PS/2 (physical keys)>
public:
    /// Attach the machine's PS/2 keyboard controller (the port decoder of a
    /// ZX-Evo does it after construction); nullptr detaches
    void SetPs2Sink(IPs2KeySink* sink) { _ps2Sink = sink; }
    IPs2KeySink* GetPs2Sink() const { return _ps2Sink; }
    bool HasPs2Sink() const { return _ps2Sink != nullptr; }
    /// Attach the machine's keys beyond the matrix (the Next's port decoder); nullptr detaches
    void SetExtendedKeySink(IExtendedKeySink* sink) { _extendedSink = sink; }
    IExtendedKeySink* GetExtendedKeySink() const { return _extendedSink; }

    /// Something on this machine consumes the physical key: the PS/2 controller (every key), or the
    /// Kempston joystick when the key is bound to a button and the machine decodes the joystick port.
    /// A key nobody wants is neither journaled nor applied
    bool WantsPcKey(PcKey key) const;

    /// Apply one physical key event (the TTD input apply point, live and
    /// replay): to the PS/2 controller and to the joystick binding, whichever the machine has
    void ApplyPcKey(PcKey key, bool pressed);
    /// Release every physical key the controller holds and every joystick button bound to a key
    /// (automation "release all")
    void ReleaseAllPcKeys();

    /// The host keyboard's route: as set, and as in force (Auto resolved).
    /// Gates host and automation input; the TTD journal records what passed
    void SetHostRoute(HostKeyboardRoute route) { _hostRoute = route; }
    HostKeyboardRoute GetHostRoute() const { return _hostRoute; }
    HostKeyboardRoute EffectiveHostRoute() const;
    bool RoutesToMatrix() const { return (static_cast<uint8_t>(EffectiveHostRoute()) & 1) != 0; }
    bool RoutesToPs2() const { return _ps2Sink && (static_cast<uint8_t>(EffectiveHostRoute()) & 2) != 0; }
    static bool ParseHostRoute(const char* text, HostKeyboardRoute& out);
    /// The one entry every interface uses: a route by name (auto | matrix |
    /// ps2 | both). Refused while TTD records: the route decides what enters
    /// the journal, a change mid-recording would replay differently
    bool RequestHostRoute(const std::string& name, std::string& error);
    static const char* HostRouteName(HostKeyboardRoute route);
    /// endregion </PS/2 (physical keys)>

    /// region <Keyboard-joystick overlay>
public:
    /// Replace the overlay (8 half-rows, active low); ControllerManager recomputes it from its connectors
    void SetOverlay(const uint8_t rows[8]);
    uint8_t OverlayRow(uint8_t row) const { return row < 8 ? _overlay[row] : 0xFF; }
    /// The matrix as the keys hold it (8 half-rows, active low, without the overlay): every press, from the host,
    /// automation or the on-screen keyboard
    uint8_t MatrixRow(uint8_t row) const { return row < 8 ? _keyboardMatrixState[row] : 0xFF; }
    /// Where a plain matrix key sits: its half-row (0 = A8 ... 7 = A15) and its bit. False for ZXKEY_NONE and the
    /// extended keys (shift combinations, no position of their own)
    static bool MatrixPosition(ZXKeysEnum key, uint8_t& row, uint8_t& bit);
    /// endregion </Keyboard-joystick overlay>

    /// region <Orel BK-08 matrix>
public:
    /// One of the Orel BK-08's extra matrix keys (bits 5 and 7 of a half-row)
    static bool IsBk08Key(ZXKeysEnum key) { return key >= ZXKEY_BK08_RUS && key <= ZXKEY_BK08_CAPS; }
    /// The machine's keyboard has the BK-08's extra columns (LSY256). Elsewhere a BK-08 key presses nothing: bits 5
    /// and 7 of #FE stay as the machine drives them
    bool HasBk08Matrix() const;
    /// endregion </Orel BK-08 matrix>

    /// region <Helper methods>
public:
    /// The matrix key an extended key presses besides its shift (LEFT -> 5,
    /// '"' -> P); a plain key is its own matrix key. Shifts map to themselves.
    ZXKeysEnum GetMatrixKey(ZXKeysEnum key) { return getExtendedKeyBase(key); }
    /// The shift an extended key presses (LEFT -> Caps Shift, '"' -> Symbol Shift); ZXKEY_NONE for a plain key,
    /// a shift for itself
    ZXKeysEnum GetMatrixModifier(ZXKeysEnum key) { return getExtendedKeyModifier(key); }

protected:
    bool isExtendedKey(ZXKeysEnum key);
    ZXKeysEnum getExtendedKeyBase(ZXKeysEnum key);
    ZXKeysEnum getExtendedKeyModifier(ZXKeysEnum key);

    /// A host key that is a Symbol Shift symbol ('"' '+' ':' '<' ... on a host keyboard) is typed with the host Shift
    /// on most layouts, and the host Shift holds Caps Shift: CAPS SHIFT + SYMBOL SHIFT + P is EXTEND MODE + P, not '"'.
    /// While such a key is down, Caps Shift is let go (journaled as a release) and pressed again after the last of
    /// them goes up, if the host still holds it. Host bookkeeping only, as the press counters
    void HoldBackCapsShift(ZXKeysEnum key, bool pressed);

    uint8_t increaseKeyPressCounter(ZXKeysEnum key);
    uint8_t decreaseKeyPressCounter(ZXKeysEnum key);

    bool anyKeyWithSimilarModifier(ZXKeysEnum key);

    /// endregion </Helper methods>

    /// region <Handle keyboard events>
public:
    uint8_t HandlePortIn(uint16_t port);                                                // Respond on port IN request from Z80
    void OnKey(ZXKeysEnum key, bool isPressed, bool shift, bool ctrl, bool alt);        // Translate host keyboard event to ZX-Spectrum
    /// endregion </Handle keyboard events>

    /// region <Handle MessageCenter keyboard events>
public:
    void OnKeyPressed(int id, Message* message);
    void OnPcKeyPressed(int id, Message* message);
    void OnPcKeyReleased(int id, Message* message);
    void OnKeyReleased(int id, Message* message);

    /// Ignore host key events (see _hostInputGated). PressKey/ReleaseKey still work
    void SetHostInputGated(bool gated) { _hostInputGated = gated; }
    bool IsHostInputGated() const { return _hostInputGated; }

    /// Stop taking the front end's key events for good: the handlers reach the debug manager and the TTD controller,
    /// so the emulator detaches them before those go. Waits for a handler in progress (RemoveObserver)
    void DetachHostInput();

protected:
    bool IsHostInputSuppressed() const;               // TTD journal owns input
    void SubmitHostKey(ZXKeysEnum key, bool pressed);   // via the TTD live-input gateway
    void SubmitHostPcKey(PcKey key, bool pressed);      // same gateway; only a key the machine wants (WantsPcKey)
    /// endregion </Handle MessageCenter keyboard events>

    /// region <Debug>
#ifdef _DEBUG
public:
    std::string DumpKeyboardState();
#endif
    /// endregion </Debug>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class KeyboardCUT : public Keyboard
{
public:
    KeyboardCUT(EmulatorContext* context) : Keyboard(context) {};

    using Keyboard::_zxKeyMap;
    using Keyboard::_zxExtendedKeyMap;
    using Keyboard::_context;
    using Keyboard::_keyboardMatrixState;
    using Keyboard::_keyboardPressedKeys;

    using Keyboard::isExtendedKey;
    using Keyboard::getExtendedKeyBase;
    using Keyboard::getExtendedKeyModifier;

    using Keyboard::increaseKeyPressCounter;
    using Keyboard::decreaseKeyPressCounter;

    using Keyboard::anyKeyWithSimilarModifier;
};

#endif // _CODE_UNDER_TEST