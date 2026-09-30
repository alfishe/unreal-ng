#include "stdafx.h"

#include "pckey.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace
{
    /// One row per PcKey, in enum order: automation name, set-2 make code,
    /// E0-prefixed (extended). Print Screen and Pause are multi-byte and handled
    /// separately (their code here is 0)
    struct KeyInfo
    {
        PcKey key;
        const char* name;
        uint8_t code;
        bool extended;
    };

    // clang-format off
    constexpr KeyInfo kKeys[] = {
        {PcKey::None, "", 0x00, false},

        {PcKey::A, "a", 0x1C, false}, {PcKey::B, "b", 0x32, false}, {PcKey::C, "c", 0x21, false},
        {PcKey::D, "d", 0x23, false}, {PcKey::E, "e", 0x24, false}, {PcKey::F, "f", 0x2B, false},
        {PcKey::G, "g", 0x34, false}, {PcKey::H, "h", 0x33, false}, {PcKey::I, "i", 0x43, false},
        {PcKey::J, "j", 0x3B, false}, {PcKey::K, "k", 0x42, false}, {PcKey::L, "l", 0x4B, false},
        {PcKey::M, "m", 0x3A, false}, {PcKey::N, "n", 0x31, false}, {PcKey::O, "o", 0x44, false},
        {PcKey::P, "p", 0x4D, false}, {PcKey::Q, "q", 0x15, false}, {PcKey::R, "r", 0x2D, false},
        {PcKey::S, "s", 0x1B, false}, {PcKey::T, "t", 0x2C, false}, {PcKey::U, "u", 0x3C, false},
        {PcKey::V, "v", 0x2A, false}, {PcKey::W, "w", 0x1D, false}, {PcKey::X, "x", 0x22, false},
        {PcKey::Y, "y", 0x35, false}, {PcKey::Z, "z", 0x1A, false},

        {PcKey::Digit1, "1", 0x16, false}, {PcKey::Digit2, "2", 0x1E, false}, {PcKey::Digit3, "3", 0x26, false},
        {PcKey::Digit4, "4", 0x25, false}, {PcKey::Digit5, "5", 0x2E, false}, {PcKey::Digit6, "6", 0x36, false},
        {PcKey::Digit7, "7", 0x3D, false}, {PcKey::Digit8, "8", 0x3E, false}, {PcKey::Digit9, "9", 0x46, false},
        {PcKey::Digit0, "0", 0x45, false},

        {PcKey::Function1, "f1", 0x05, false}, {PcKey::Function2, "f2", 0x06, false}, {PcKey::Function3, "f3", 0x04, false},
        {PcKey::Function4, "f4", 0x0C, false}, {PcKey::Function5, "f5", 0x03, false}, {PcKey::Function6, "f6", 0x0B, false},
        {PcKey::Function7, "f7", 0x83, false}, {PcKey::Function8, "f8", 0x0A, false}, {PcKey::Function9, "f9", 0x01, false},
        {PcKey::Function10, "f10", 0x09, false}, {PcKey::Function11, "f11", 0x78, false}, {PcKey::Function12, "f12", 0x07, false},

        {PcKey::Escape, "esc", 0x76, false}, {PcKey::Backquote, "backquote", 0x0E, false},
        {PcKey::Minus, "minus", 0x4E, false}, {PcKey::Equal, "equal", 0x55, false},
        {PcKey::Backspace, "backspace", 0x66, false}, {PcKey::Tab, "tab", 0x0D, false},
        {PcKey::LeftBracket, "lbracket", 0x54, false}, {PcKey::RightBracket, "rbracket", 0x5B, false},
        {PcKey::Backslash, "backslash", 0x5D, false}, {PcKey::CapsLock, "capslock", 0x58, false},
        {PcKey::Semicolon, "semicolon", 0x4C, false}, {PcKey::Quote, "quote", 0x52, false},
        {PcKey::Enter, "enter", 0x5A, false}, {PcKey::LeftShift, "lshift", 0x12, false},
        {PcKey::Comma, "comma", 0x41, false}, {PcKey::Period, "period", 0x49, false},
        {PcKey::Slash, "slash", 0x4A, false}, {PcKey::RightShift, "rshift", 0x59, false},
        {PcKey::LeftCtrl, "lctrl", 0x14, false}, {PcKey::LeftGui, "lgui", 0x1F, true},
        {PcKey::LeftAlt, "lalt", 0x11, false}, {PcKey::Space, "space", 0x29, false},
        {PcKey::RightAlt, "ralt", 0x11, true}, {PcKey::RightGui, "rgui", 0x27, true},
        {PcKey::Menu, "menu", 0x2F, true}, {PcKey::RightCtrl, "rctrl", 0x14, true},
        {PcKey::IntlBackslash, "intlbackslash", 0x61, false},

        {PcKey::PrintScreen, "printscreen", 0x00, true}, {PcKey::ScrollLock, "scrolllock", 0x7E, false},
        {PcKey::Pause, "pause", 0x00, false},
        {PcKey::Insert, "insert", 0x70, true}, {PcKey::Home, "home", 0x6C, true},
        {PcKey::PageUp, "pageup", 0x7D, true}, {PcKey::Delete, "delete", 0x71, true},
        {PcKey::End, "end", 0x69, true}, {PcKey::PageDown, "pagedown", 0x7A, true},
        {PcKey::Up, "up", 0x75, true}, {PcKey::Left, "left", 0x6B, true},
        {PcKey::Down, "down", 0x72, true}, {PcKey::Right, "right", 0x74, true},

        {PcKey::NumLock, "numlock", 0x77, false}, {PcKey::KeypadDivide, "kp_divide", 0x4A, true},
        {PcKey::KeypadMultiply, "kp_multiply", 0x7C, false}, {PcKey::KeypadMinus, "kp_minus", 0x7B, false},
        {PcKey::KeypadPlus, "kp_plus", 0x79, false}, {PcKey::KeypadEnter, "kp_enter", 0x5A, true},
        {PcKey::KeypadDecimal, "kp_decimal", 0x71, false},
        {PcKey::Keypad0, "kp_0", 0x70, false}, {PcKey::Keypad1, "kp_1", 0x69, false},
        {PcKey::Keypad2, "kp_2", 0x72, false}, {PcKey::Keypad3, "kp_3", 0x7A, false},
        {PcKey::Keypad4, "kp_4", 0x6B, false}, {PcKey::Keypad5, "kp_5", 0x73, false},
        {PcKey::Keypad6, "kp_6", 0x74, false}, {PcKey::Keypad7, "kp_7", 0x6C, false},
        {PcKey::Keypad8, "kp_8", 0x75, false}, {PcKey::Keypad9, "kp_9", 0x7D, false},
    };
    // clang-format on

    static_assert(sizeof(kKeys) / sizeof(kKeys[0]) == static_cast<size_t>(PcKey::Count),
                  "kKeys must have one row per PcKey, in enum order");

    constexpr bool RowsInEnumOrder()
    {
        for (size_t i = 0; i < sizeof(kKeys) / sizeof(kKeys[0]); i++)
        {
            if (static_cast<size_t>(kKeys[i].key) != i)
                return false;
        }
        return true;
    }
    static_assert(RowsInEnumOrder(), "kKeys rows must follow the PcKey order");

    const KeyInfo* InfoOf(PcKey key)
    {
        const size_t index = static_cast<size_t>(key);
        if (key == PcKey::None || index >= static_cast<size_t>(PcKey::Count))
            return nullptr;
        return &kKeys[index];
    }

    /// Sparse host-code tables: {host code, key}
    struct CodeMap
    {
        uint16_t code;
        PcKey key;
    };

    PcKey Lookup(const CodeMap* table, size_t size, uint32_t code)
    {
        for (size_t i = 0; i < size; i++)
        {
            if (table[i].code == code)
                return table[i].key;
        }
        return PcKey::None;
    }

    // clang-format off
    /// macOS Carbon kVK_* codes (HIToolbox Events.h): position-based, layout-independent
    constexpr CodeMap kMac[] = {
        {0x00, PcKey::A}, {0x01, PcKey::S}, {0x02, PcKey::D}, {0x03, PcKey::F}, {0x04, PcKey::H},
        {0x05, PcKey::G}, {0x06, PcKey::Z}, {0x07, PcKey::X}, {0x08, PcKey::C}, {0x09, PcKey::V},
        {0x0A, PcKey::IntlBackslash}, {0x0B, PcKey::B}, {0x0C, PcKey::Q}, {0x0D, PcKey::W},
        {0x0E, PcKey::E}, {0x0F, PcKey::R}, {0x10, PcKey::Y}, {0x11, PcKey::T},
        {0x12, PcKey::Digit1}, {0x13, PcKey::Digit2}, {0x14, PcKey::Digit3}, {0x15, PcKey::Digit4},
        {0x16, PcKey::Digit6}, {0x17, PcKey::Digit5}, {0x18, PcKey::Equal}, {0x19, PcKey::Digit9},
        {0x1A, PcKey::Digit7}, {0x1B, PcKey::Minus}, {0x1C, PcKey::Digit8}, {0x1D, PcKey::Digit0},
        {0x1E, PcKey::RightBracket}, {0x1F, PcKey::O}, {0x20, PcKey::U}, {0x21, PcKey::LeftBracket},
        {0x22, PcKey::I}, {0x23, PcKey::P}, {0x24, PcKey::Enter}, {0x25, PcKey::L}, {0x26, PcKey::J},
        {0x27, PcKey::Quote}, {0x28, PcKey::K}, {0x29, PcKey::Semicolon}, {0x2A, PcKey::Backslash},
        {0x2B, PcKey::Comma}, {0x2C, PcKey::Slash}, {0x2D, PcKey::N}, {0x2E, PcKey::M},
        {0x2F, PcKey::Period}, {0x30, PcKey::Tab}, {0x31, PcKey::Space}, {0x32, PcKey::Backquote},
        {0x33, PcKey::Backspace}, {0x35, PcKey::Escape}, {0x36, PcKey::RightGui}, {0x37, PcKey::LeftGui},
        {0x38, PcKey::LeftShift}, {0x39, PcKey::CapsLock}, {0x3A, PcKey::LeftAlt}, {0x3B, PcKey::LeftCtrl},
        {0x3C, PcKey::RightShift}, {0x3D, PcKey::RightAlt}, {0x3E, PcKey::RightCtrl},
        {0x41, PcKey::KeypadDecimal}, {0x43, PcKey::KeypadMultiply}, {0x45, PcKey::KeypadPlus},
        {0x47, PcKey::NumLock}, {0x4B, PcKey::KeypadDivide}, {0x4C, PcKey::KeypadEnter},
        {0x4E, PcKey::KeypadMinus}, {0x52, PcKey::Keypad0}, {0x53, PcKey::Keypad1},
        {0x54, PcKey::Keypad2}, {0x55, PcKey::Keypad3}, {0x56, PcKey::Keypad4}, {0x57, PcKey::Keypad5},
        {0x58, PcKey::Keypad6}, {0x59, PcKey::Keypad7}, {0x5B, PcKey::Keypad8}, {0x5C, PcKey::Keypad9},
        {0x60, PcKey::Function5}, {0x61, PcKey::Function6}, {0x62, PcKey::Function7}, {0x63, PcKey::Function3}, {0x64, PcKey::Function8},
        {0x65, PcKey::Function9}, {0x67, PcKey::Function11}, {0x69, PcKey::PrintScreen}, {0x6B, PcKey::ScrollLock},
        {0x6D, PcKey::Function10}, {0x6E, PcKey::Menu}, {0x6F, PcKey::Function12}, {0x71, PcKey::Pause},
        {0x72, PcKey::Insert}, {0x73, PcKey::Home}, {0x74, PcKey::PageUp}, {0x75, PcKey::Delete},
        {0x76, PcKey::Function4}, {0x77, PcKey::End}, {0x78, PcKey::Function2}, {0x79, PcKey::PageDown},
        {0x7A, PcKey::Function1}, {0x7B, PcKey::Left}, {0x7C, PcKey::Right}, {0x7D, PcKey::Down},
        {0x7E, PcKey::Up},
    };

    /// Windows: set-1 scan code, bit 8 = the extended (E0) flag, as Qt reports it
    constexpr CodeMap kWindows[] = {
        {0x01, PcKey::Escape}, {0x02, PcKey::Digit1}, {0x03, PcKey::Digit2}, {0x04, PcKey::Digit3},
        {0x05, PcKey::Digit4}, {0x06, PcKey::Digit5}, {0x07, PcKey::Digit6}, {0x08, PcKey::Digit7},
        {0x09, PcKey::Digit8}, {0x0A, PcKey::Digit9}, {0x0B, PcKey::Digit0}, {0x0C, PcKey::Minus},
        {0x0D, PcKey::Equal}, {0x0E, PcKey::Backspace}, {0x0F, PcKey::Tab}, {0x10, PcKey::Q},
        {0x11, PcKey::W}, {0x12, PcKey::E}, {0x13, PcKey::R}, {0x14, PcKey::T}, {0x15, PcKey::Y},
        {0x16, PcKey::U}, {0x17, PcKey::I}, {0x18, PcKey::O}, {0x19, PcKey::P},
        {0x1A, PcKey::LeftBracket}, {0x1B, PcKey::RightBracket}, {0x1C, PcKey::Enter},
        {0x1D, PcKey::LeftCtrl}, {0x1E, PcKey::A}, {0x1F, PcKey::S}, {0x20, PcKey::D}, {0x21, PcKey::F},
        {0x22, PcKey::G}, {0x23, PcKey::H}, {0x24, PcKey::J}, {0x25, PcKey::K}, {0x26, PcKey::L},
        {0x27, PcKey::Semicolon}, {0x28, PcKey::Quote}, {0x29, PcKey::Backquote}, {0x2A, PcKey::LeftShift},
        {0x2B, PcKey::Backslash}, {0x2C, PcKey::Z}, {0x2D, PcKey::X}, {0x2E, PcKey::C}, {0x2F, PcKey::V},
        {0x30, PcKey::B}, {0x31, PcKey::N}, {0x32, PcKey::M}, {0x33, PcKey::Comma}, {0x34, PcKey::Period},
        {0x35, PcKey::Slash}, {0x36, PcKey::RightShift}, {0x37, PcKey::KeypadMultiply},
        {0x38, PcKey::LeftAlt}, {0x39, PcKey::Space}, {0x3A, PcKey::CapsLock},
        {0x3B, PcKey::Function1}, {0x3C, PcKey::Function2}, {0x3D, PcKey::Function3}, {0x3E, PcKey::Function4}, {0x3F, PcKey::Function5},
        {0x40, PcKey::Function6}, {0x41, PcKey::Function7}, {0x42, PcKey::Function8}, {0x43, PcKey::Function9}, {0x44, PcKey::Function10},
        {0x45, PcKey::Pause}, {0x46, PcKey::ScrollLock}, {0x47, PcKey::Keypad7}, {0x48, PcKey::Keypad8},
        {0x49, PcKey::Keypad9}, {0x4A, PcKey::KeypadMinus}, {0x4B, PcKey::Keypad4}, {0x4C, PcKey::Keypad5},
        {0x4D, PcKey::Keypad6}, {0x4E, PcKey::KeypadPlus}, {0x4F, PcKey::Keypad1}, {0x50, PcKey::Keypad2},
        {0x51, PcKey::Keypad3}, {0x52, PcKey::Keypad0}, {0x53, PcKey::KeypadDecimal},
        {0x56, PcKey::IntlBackslash}, {0x57, PcKey::Function11}, {0x58, PcKey::Function12},
        {0x11C, PcKey::KeypadEnter}, {0x11D, PcKey::RightCtrl}, {0x135, PcKey::KeypadDivide},
        {0x137, PcKey::PrintScreen}, {0x138, PcKey::RightAlt}, {0x145, PcKey::NumLock},
        {0x147, PcKey::Home}, {0x148, PcKey::Up}, {0x149, PcKey::PageUp}, {0x14B, PcKey::Left},
        {0x14D, PcKey::Right}, {0x14F, PcKey::End}, {0x150, PcKey::Down}, {0x151, PcKey::PageDown},
        {0x152, PcKey::Insert}, {0x153, PcKey::Delete}, {0x15B, PcKey::LeftGui}, {0x15C, PcKey::RightGui},
        {0x15D, PcKey::Menu},
    };

    /// Linux input-event-codes.h KEY_* values (the main block follows set 1)
    constexpr CodeMap kLinux[] = {
        {1, PcKey::Escape}, {2, PcKey::Digit1}, {3, PcKey::Digit2}, {4, PcKey::Digit3}, {5, PcKey::Digit4},
        {6, PcKey::Digit5}, {7, PcKey::Digit6}, {8, PcKey::Digit7}, {9, PcKey::Digit8}, {10, PcKey::Digit9},
        {11, PcKey::Digit0}, {12, PcKey::Minus}, {13, PcKey::Equal}, {14, PcKey::Backspace}, {15, PcKey::Tab},
        {16, PcKey::Q}, {17, PcKey::W}, {18, PcKey::E}, {19, PcKey::R}, {20, PcKey::T}, {21, PcKey::Y},
        {22, PcKey::U}, {23, PcKey::I}, {24, PcKey::O}, {25, PcKey::P}, {26, PcKey::LeftBracket},
        {27, PcKey::RightBracket}, {28, PcKey::Enter}, {29, PcKey::LeftCtrl}, {30, PcKey::A}, {31, PcKey::S},
        {32, PcKey::D}, {33, PcKey::F}, {34, PcKey::G}, {35, PcKey::H}, {36, PcKey::J}, {37, PcKey::K},
        {38, PcKey::L}, {39, PcKey::Semicolon}, {40, PcKey::Quote}, {41, PcKey::Backquote},
        {42, PcKey::LeftShift}, {43, PcKey::Backslash}, {44, PcKey::Z}, {45, PcKey::X}, {46, PcKey::C},
        {47, PcKey::V}, {48, PcKey::B}, {49, PcKey::N}, {50, PcKey::M}, {51, PcKey::Comma},
        {52, PcKey::Period}, {53, PcKey::Slash}, {54, PcKey::RightShift}, {55, PcKey::KeypadMultiply},
        {56, PcKey::LeftAlt}, {57, PcKey::Space}, {58, PcKey::CapsLock}, {59, PcKey::Function1}, {60, PcKey::Function2},
        {61, PcKey::Function3}, {62, PcKey::Function4}, {63, PcKey::Function5}, {64, PcKey::Function6}, {65, PcKey::Function7}, {66, PcKey::Function8},
        {67, PcKey::Function9}, {68, PcKey::Function10}, {69, PcKey::NumLock}, {70, PcKey::ScrollLock},
        {71, PcKey::Keypad7}, {72, PcKey::Keypad8}, {73, PcKey::Keypad9}, {74, PcKey::KeypadMinus},
        {75, PcKey::Keypad4}, {76, PcKey::Keypad5}, {77, PcKey::Keypad6}, {78, PcKey::KeypadPlus},
        {79, PcKey::Keypad1}, {80, PcKey::Keypad2}, {81, PcKey::Keypad3}, {82, PcKey::Keypad0},
        {83, PcKey::KeypadDecimal}, {86, PcKey::IntlBackslash}, {87, PcKey::Function11}, {88, PcKey::Function12},
        {96, PcKey::KeypadEnter}, {97, PcKey::RightCtrl}, {98, PcKey::KeypadDivide}, {99, PcKey::PrintScreen},
        {100, PcKey::RightAlt}, {102, PcKey::Home}, {103, PcKey::Up}, {104, PcKey::PageUp},
        {105, PcKey::Left}, {106, PcKey::Right}, {107, PcKey::End}, {108, PcKey::Down},
        {109, PcKey::PageDown}, {110, PcKey::Insert}, {111, PcKey::Delete}, {119, PcKey::Pause},
        {125, PcKey::LeftGui}, {126, PcKey::RightGui}, {127, PcKey::Menu},
    };
    // clang-format on

    template <size_t N>
    PcKey Lookup(const CodeMap (&table)[N], uint32_t code)
    {
        return Lookup(table, N, code);
    }

    /// US layout: the unshifted and shifted character of each printable key
    struct CharKey
    {
        char plain;
        char shifted;
        PcKey key;
    };

    constexpr CharKey kUsLayout[] = {
        {'1', '!', PcKey::Digit1}, {'2', '@', PcKey::Digit2}, {'3', '#', PcKey::Digit3},
        {'4', '$', PcKey::Digit4}, {'5', '%', PcKey::Digit5}, {'6', '^', PcKey::Digit6},
        {'7', '&', PcKey::Digit7}, {'8', '*', PcKey::Digit8}, {'9', '(', PcKey::Digit9},
        {'0', ')', PcKey::Digit0}, {'-', '_', PcKey::Minus}, {'=', '+', PcKey::Equal},
        {'[', '{', PcKey::LeftBracket}, {']', '}', PcKey::RightBracket}, {'\\', '|', PcKey::Backslash},
        {';', ':', PcKey::Semicolon}, {'\'', '"', PcKey::Quote}, {',', '<', PcKey::Comma},
        {'.', '>', PcKey::Period}, {'/', '?', PcKey::Slash}, {'`', '~', PcKey::Backquote},
        {' ', ' ', PcKey::Space},
    };
}  // namespace

namespace pckey
{

std::string Name(PcKey key)
{
    const KeyInfo* info = InfoOf(key);
    return info ? std::string(info->name) : std::string();
}

PcKey FromName(const std::string& name)
{
    std::string lower;
    lower.reserve(name.size());
    for (char c : name)
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (lower.rfind("pc.", 0) == 0 || lower.rfind("pc:", 0) == 0)
        lower.erase(0, 3);
    if (lower.empty())
        return PcKey::None;

    for (const KeyInfo& info : kKeys)
    {
        if (info.key != PcKey::None && lower == info.name)
            return info.key;
    }
    return PcKey::None;
}

std::vector<uint8_t> Ps2Set2Bytes(PcKey key, bool pressed)
{
    if (key == PcKey::PrintScreen)
    {
        // Sent as a fake Left Shift around the key code
        if (pressed)
            return {0xE0, 0x12, 0xE0, 0x7C};
        return {0xE0, 0xF0, 0x7C, 0xE0, 0xF0, 0x12};
    }
    if (key == PcKey::Pause)
    {
        // Make only: the key has no break code
        if (pressed)
            return {0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77};
        return {};
    }

    const KeyInfo* info = InfoOf(key);
    if (!info)
        return {};

    std::vector<uint8_t> bytes;
    if (info->extended)
        bytes.push_back(0xE0);
    if (!pressed)
        bytes.push_back(0xF0);
    bytes.push_back(info->code);
    return bytes;
}

PcKey FromMacVirtualKey(uint32_t virtualKey)
{
    return Lookup(kMac, virtualKey);
}

PcKey FromWindowsScanCode(uint32_t scanCode)
{
    return Lookup(kWindows, scanCode & 0x1FF);
}

PcKey FromLinuxEvdev(uint32_t evdevCode)
{
    return Lookup(kLinux, evdevCode);
}

std::vector<PcKey> FromZxKey(ZXKeysEnum key)
{
    // By name, not by value: ZXKeysEnum is not in letter order (ZXKEY_I = 0x48, ZXKEY_H = 0x49)
    switch (key)
    {
        case ZXKEY_A: return {PcKey::A};
        case ZXKEY_B: return {PcKey::B};
        case ZXKEY_C: return {PcKey::C};
        case ZXKEY_D: return {PcKey::D};
        case ZXKEY_E: return {PcKey::E};
        case ZXKEY_F: return {PcKey::F};
        case ZXKEY_G: return {PcKey::G};
        case ZXKEY_H: return {PcKey::H};
        case ZXKEY_I: return {PcKey::I};
        case ZXKEY_J: return {PcKey::J};
        case ZXKEY_K: return {PcKey::K};
        case ZXKEY_L: return {PcKey::L};
        case ZXKEY_M: return {PcKey::M};
        case ZXKEY_N: return {PcKey::N};
        case ZXKEY_O: return {PcKey::O};
        case ZXKEY_P: return {PcKey::P};
        case ZXKEY_Q: return {PcKey::Q};
        case ZXKEY_R: return {PcKey::R};
        case ZXKEY_S: return {PcKey::S};
        case ZXKEY_T: return {PcKey::T};
        case ZXKEY_U: return {PcKey::U};
        case ZXKEY_V: return {PcKey::V};
        case ZXKEY_W: return {PcKey::W};
        case ZXKEY_X: return {PcKey::X};
        case ZXKEY_Y: return {PcKey::Y};
        case ZXKEY_Z: return {PcKey::Z};
        case ZXKEY_1: return {PcKey::Digit1};
        case ZXKEY_2: return {PcKey::Digit2};
        case ZXKEY_3: return {PcKey::Digit3};
        case ZXKEY_4: return {PcKey::Digit4};
        case ZXKEY_5: return {PcKey::Digit5};
        case ZXKEY_6: return {PcKey::Digit6};
        case ZXKEY_7: return {PcKey::Digit7};
        case ZXKEY_8: return {PcKey::Digit8};
        case ZXKEY_9: return {PcKey::Digit9};
        case ZXKEY_0: return {PcKey::Digit0};
        case ZXKEY_ENTER: return {PcKey::Enter};
        case ZXKEY_SPACE: return {PcKey::Space};
        case ZXKEY_CAPS_SHIFT: return {PcKey::LeftShift};
        case ZXKEY_SYM_SHIFT: return {PcKey::LeftCtrl};

        case ZXKEY_EXT_UP: return {PcKey::Up};
        case ZXKEY_EXT_DOWN: return {PcKey::Down};
        case ZXKEY_EXT_LEFT: return {PcKey::Left};
        case ZXKEY_EXT_RIGHT: return {PcKey::Right};
        case ZXKEY_EXT_DELETE: return {PcKey::Backspace};
        case ZXKEY_EXT_BREAK: return {PcKey::Escape};
        case ZXKEY_EXT_EDIT: return {PcKey::Backquote};
        case ZXKEY_EXT_CAPSLOCK: return {PcKey::CapsLock};

        case ZXKEY_EXT_DOT: return FromCharacter('.');
        case ZXKEY_EXT_COMMA: return FromCharacter(',');
        case ZXKEY_EXT_PLUS: return FromCharacter('+');
        case ZXKEY_EXT_MINUS: return FromCharacter('-');
        case ZXKEY_EXT_MULTIPLY: return FromCharacter('*');
        case ZXKEY_EXT_DIVIDE: return FromCharacter('/');
        case ZXKEY_EXT_EQUAL: return FromCharacter('=');
        case ZXKEY_EXT_BAR: return FromCharacter('|');
        case ZXKEY_EXT_BACKSLASH: return FromCharacter('\\');
        case ZXKEY_EXT_DBLQUOTE: return FromCharacter('"');

        default: return {};
    }
}

std::vector<PcKey> FromCharacter(char c)
{
    if (c >= 'a' && c <= 'z')
        return {static_cast<PcKey>(static_cast<int>(PcKey::A) + (c - 'a'))};
    if (c >= 'A' && c <= 'Z')
        return {PcKey::LeftShift, static_cast<PcKey>(static_cast<int>(PcKey::A) + (c - 'A'))};
    if (c == '\n' || c == '\r')
        return {PcKey::Enter};
    if (c == '\t')
        return {PcKey::Tab};
    if (c == '\b')
        return {PcKey::Backspace};

    for (const CharKey& entry : kUsLayout)
    {
        if (c == entry.plain)
            return {entry.key};
        if (c == entry.shifted)
            return {PcKey::LeftShift, entry.key};
    }
    return {};
}

}  // namespace pckey
