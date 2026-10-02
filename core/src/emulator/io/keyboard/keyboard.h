#pragma once
#include "stdafx.h"

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/platform.h"

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
class Keyboard : public Observer
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_IO;
    const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_KEYBOARD;
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Constants>

    // [0:5] Lowest 5 bits are used for key half-rows polling
    // [6]   Bit 6 - Audio in (EAR input)
    // [7]   Bit 7 - not used

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

    /// Host (MessageCenter) key events ignored: a ZX-Poly group member takes
    /// its keys from the group, at frame boundaries, in step with the others
    bool _hostInputGated = false;

    /// The machine's PS/2 keyboard controller (ZX-Evo AVR), null on machines
    /// without one: physical key events are neither journaled nor applied there
    IPs2KeySink* _ps2Sink = nullptr;

    /// The host keyboard's route (config [INPUT] HostKeyboard=, runtime SetHostRoute)
    HostKeyboardRoute _hostRoute = HostKeyboardRoute::Auto;

    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    Keyboard() = delete;    // Disable default constructor. C++ 11 feature
    Keyboard(EmulatorContext* context);
    virtual ~Keyboard();
    /// endregion </Constructors / Destructors>

    /// region <Keyboard control>
public:
    /// Matrix + pressed-key counters. Not a TTD peripheral (the journal
    /// replays input forward); captured only so a throwaway TTD display
    /// render can hand the live keyboard back untouched.
    struct InputState
    {
        uint8_t matrix[8];
        std::map<ZXKeysEnum, uint8_t> pressedKeys;
    };

    void Reset();
    InputState CaptureInputState() const;
    void RestoreInputState(const InputState& state);
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

    /// region <Helper methods>
public:
    /// The matrix key an extended key presses besides its shift (LEFT -> 5,
    /// '"' -> P); a plain key is its own matrix key. Shifts map to themselves.
    ZXKeysEnum GetMatrixKey(ZXKeysEnum key) { return getExtendedKeyBase(key); }

protected:
    bool isExtendedKey(ZXKeysEnum key);
    ZXKeysEnum getExtendedKeyBase(ZXKeysEnum key);
    ZXKeysEnum getExtendedKeyModifier(ZXKeysEnum key);

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