#include "stdafx.h"

#include "nextextendedinput.h"

#include <algorithm>

namespace
{
/// keymaps.vhd, "U.S. PS/2 ASSIGNMENTS": Left Alt = EXTEND, Right Alt = GRAPH, ; ' , . as themselves, the arrows, Caps Lock, Backspace = DELETE,
/// Esc = BREAK, ` = EDIT, Tab = TRUE VIDEO, \ = INV VIDEO
struct Mapping
{
    PcKey key;
    NextExtendedInput::Key bit;
};
constexpr Mapping kMap[] = {
    {PcKey::LeftAlt, NextExtendedInput::kExtend},     {PcKey::Up, NextExtendedInput::kUp},
    {PcKey::CapsLock, NextExtendedInput::kCapsLock},  {PcKey::RightAlt, NextExtendedInput::kGraph},
    {PcKey::Tab, NextExtendedInput::kTrueVideo},      {PcKey::Backslash, NextExtendedInput::kInvVideo},
    {PcKey::Escape, NextExtendedInput::kBreak},       {PcKey::Backquote, NextExtendedInput::kEdit},
    {PcKey::Semicolon, NextExtendedInput::kSemicolon}, {PcKey::Quote, NextExtendedInput::kQuote},
    {PcKey::Comma, NextExtendedInput::kComma},        {PcKey::Period, NextExtendedInput::kPeriod},
    {PcKey::Backspace, NextExtendedInput::kDelete},   {PcKey::Right, NextExtendedInput::kRight},
    {PcKey::Left, NextExtendedInput::kLeft},          {PcKey::Down, NextExtendedInput::kDown},
};

/// The membrane's extended keys as ZX keys and the two matrix keys each presses (membrane.vhd 157-238, the ex register's bits by row)
struct Membrane
{
    ZXKeysEnum key;
    NextExtendedInput::Key bit;
    ZXKeysEnum shift;
    ZXKeysEnum base;
};
constexpr Membrane kMembrane[] = {
    {ZXKEY_EXT_EXTEND, NextExtendedInput::kExtend, ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT},
    {ZXKEY_EXT_UP, NextExtendedInput::kUp, ZXKEY_CAPS_SHIFT, ZXKEY_7},
    {ZXKEY_EXT_CAPSLOCK, NextExtendedInput::kCapsLock, ZXKEY_CAPS_SHIFT, ZXKEY_2},
    {ZXKEY_EXT_GRAPH, NextExtendedInput::kGraph, ZXKEY_CAPS_SHIFT, ZXKEY_9},
    {ZXKEY_EXT_TRUEVIDEO, NextExtendedInput::kTrueVideo, ZXKEY_CAPS_SHIFT, ZXKEY_3},
    {ZXKEY_EXT_INVVIDEO, NextExtendedInput::kInvVideo, ZXKEY_CAPS_SHIFT, ZXKEY_4},
    {ZXKEY_EXT_BREAK, NextExtendedInput::kBreak, ZXKEY_CAPS_SHIFT, ZXKEY_SPACE},
    {ZXKEY_EXT_EDIT, NextExtendedInput::kEdit, ZXKEY_CAPS_SHIFT, ZXKEY_1},
    {ZXKEY_EXT_SEMICOLON, NextExtendedInput::kSemicolon, ZXKEY_SYM_SHIFT, ZXKEY_O},
    {ZXKEY_EXT_DBLQUOTE, NextExtendedInput::kQuote, ZXKEY_SYM_SHIFT, ZXKEY_P},
    {ZXKEY_EXT_COMMA, NextExtendedInput::kComma, ZXKEY_SYM_SHIFT, ZXKEY_N},
    {ZXKEY_EXT_DOT, NextExtendedInput::kPeriod, ZXKEY_SYM_SHIFT, ZXKEY_M},
    {ZXKEY_EXT_DELETE, NextExtendedInput::kDelete, ZXKEY_CAPS_SHIFT, ZXKEY_0},
    {ZXKEY_EXT_RIGHT, NextExtendedInput::kRight, ZXKEY_CAPS_SHIFT, ZXKEY_8},
    {ZXKEY_EXT_LEFT, NextExtendedInput::kLeft, ZXKEY_CAPS_SHIFT, ZXKEY_5},
    {ZXKEY_EXT_DOWN, NextExtendedInput::kDown, ZXKEY_CAPS_SHIFT, ZXKEY_6},
};

const Membrane* FindMembrane(ZXKeysEnum key)
{
    for (const Membrane& m : kMembrane)
        if (m.key == key)
            return &m;
    return nullptr;
}

const char* const kKeyNames[NextExtendedInput::kKeyCount] = {"extend", "up",   "caps_lock", "graph", "true_video", "inv_video", "break", "edit",
                                                             ";",      "\"",   ",",         ".",     "delete",     "right",     "left",  "down"};
}  // namespace

NextExtendedInput::NextExtendedInput()
{
    // X, Y, Z and MODE on the keypad corners
    _padKeys[0].store(static_cast<uint8_t>(PcKey::Keypad3), std::memory_order_relaxed);  // MODE
    _padKeys[1].store(static_cast<uint8_t>(PcKey::Keypad9), std::memory_order_relaxed);  // Y
    _padKeys[2].store(static_cast<uint8_t>(PcKey::Keypad1), std::memory_order_relaxed);  // Z
    _padKeys[3].store(static_cast<uint8_t>(PcKey::Keypad7), std::memory_order_relaxed);  // X
}

void NextExtendedInput::OnPcKey(PcKey key, bool pressed)
{
    for (const Mapping& m : kMap)
    {
        if (m.key != key)
            continue;
        const uint16_t bit = static_cast<uint16_t>(1u << m.bit);
        if (pressed)
            _keys.fetch_or(bit, std::memory_order_relaxed);
        else
            _keys.fetch_and(static_cast<uint16_t>(~bit), std::memory_order_relaxed);
    }
    for (unsigned button = 0; button < kPadButtons; button++)
    {
        if (_padKeys[button].load(std::memory_order_relaxed) != static_cast<uint8_t>(key))
            continue;
        const uint8_t bit = static_cast<uint8_t>(1u << button);
        if (pressed)
            _padLeft.fetch_or(bit, std::memory_order_relaxed);
        else
            _padLeft.fetch_and(static_cast<uint8_t>(~bit), std::memory_order_relaxed);
    }
}

bool NextExtendedInput::OwnsExtendedKey(ZXKeysEnum key) const
{
    return FindMembrane(key) != nullptr;
}

void NextExtendedInput::OnExtendedKey(ZXKeysEnum key, bool pressed)
{
    const Membrane* m = FindMembrane(key);
    if (!m)
        return;
    const uint16_t bit = static_cast<uint16_t>(1u << m->bit);
    AdvanceScan();
    if (pressed)
    {
        _membrane.fetch_or(bit, std::memory_order_relaxed);
        _scans[0] = static_cast<uint16_t>(_scans[0] | bit);
    }
    else
    {
        // a key released mid-scan was still seen by this scan
        _membrane.fetch_and(static_cast<uint16_t>(~bit), std::memory_order_relaxed);
    }
}

void NextExtendedInput::ReleaseExtendedKeys()
{
    _membrane.store(0, std::memory_order_relaxed);
    _scans = {};
}

void NextExtendedInput::AdvanceScan() const
{
    // every read is a scan of its own without a clock
    const uint64_t scan = _now28 ? _now28() / kScanClocks : _scan + 1;
    if (scan == _scan)
        return;
    const uint16_t held = MembraneKeys();
    if (scan < _scan)
    {
        // the clock went back (a state load without the scan): start the history over
        _scans = {};
        _scans[0] = held;
    }
    else
    {
        // the scans since the last one saw the keys as held now (a change goes through here first)
        const uint64_t steps = std::min<uint64_t>(scan - _scan, kScanDepth);
        for (uint64_t i = 0; i < steps; i++)
        {
            for (unsigned k = kScanDepth - 1; k > 0; k--)
                _scans[k] = _scans[k - 1];
            _scans[0] = held;
        }
    }
    _scan = scan;
}

uint8_t NextExtendedInput::ExtendedMatrixRow(uint8_t row) const
{
    if (MembraneKeys() == 0 && (_scans[0] | _scans[1] | _scans[2] | _scans[3]) == 0)
        return 0xFF;
    if (_cancel && _cancel())
    {
        // i_cancel_extended_entries clears the ex registers
        _scans = {};
        _scans[0] = MembraneKeys();
        return 0xFF;
    }
    AdvanceScan();
    // membrane.vhd 189-190, 232: a key's own matrix key comes from the previous scan; its shift from any of the last four (one scan ahead of
    // the key and two after it)
    const uint16_t keys = _scans[1];
    const uint16_t shifted = static_cast<uint16_t>(_scans[0] | _scans[1] | _scans[2] | _scans[3]);
    uint8_t result = 0xFF;
    auto press = [&](ZXKeysEnum key) {
        uint8_t keyRow = 0, keyBit = 0;
        if (Keyboard::MatrixPosition(key, keyRow, keyBit) && keyRow == row)
            result &= static_cast<uint8_t>(~(1u << keyBit));
    };
    for (const Membrane& m : kMembrane)
    {
        const uint16_t bit = static_cast<uint16_t>(1u << m.bit);
        if (shifted & bit)
            press(m.shift);
        // EXTEND's second key is SYMBOL SHIFT: a shift too
        if (((m.base == ZXKEY_SYM_SHIFT) ? shifted : keys) & bit)
            press(m.base);
    }
    return result;
}

void NextExtendedInput::ReleaseAllPcKeys()
{
    _keys.store(0, std::memory_order_relaxed);
    _padLeft.store(0, std::memory_order_relaxed);
}

uint8_t NextExtendedInput::ReadB0() const
{
    const uint16_t k = Keys();
    auto bit = [&](unsigned n) -> unsigned { return (k >> n) & 1u; };
    // ; " , . UP DOWN LEFT RIGHT
    return static_cast<uint8_t>((bit(kSemicolon) << 7) | (bit(kQuote) << 6) | (bit(kComma) << 5) | (bit(kPeriod) << 4) | (bit(kUp) << 3) |
                                (bit(kDown) << 2) | (bit(kLeft) << 1) | bit(kRight));
}

uint8_t NextExtendedInput::ReadB1() const
{
    const uint16_t k = Keys();
    auto bit = [&](unsigned n) -> unsigned { return (k >> n) & 1u; };
    // DELETE EDIT BREAK INV TRU GRAPH CAPSLOCK EXTEND
    return static_cast<uint8_t>((bit(kDelete) << 7) | (bit(kEdit) << 6) | (bit(kBreak) << 5) | (bit(kInvVideo) << 4) | (bit(kTrueVideo) << 3) |
                                (bit(kGraph) << 2) | (bit(kCapsLock) << 1) | bit(kExtend));
}

uint8_t NextExtendedInput::ReadB2() const
{
    // zxnext.vhd: i_JOY_RIGHT(10:8) & i_JOY_RIGHT(11) & i_JOY_LEFT(10:8) & i_JOY_LEFT(11) = X Z Y MODE for each side
    auto side = [](uint8_t b) -> unsigned {
        return ((b & kPadX) ? 8u : 0u) | ((b & kPadZ) ? 4u : 0u) | ((b & kPadY) ? 2u : 0u) | ((b & kPadMode) ? 1u : 0u);
    };
    return static_cast<uint8_t>((side(Pad(false)) << 4) | side(Pad(true)));
}

PcKey NextExtendedInput::PadKey(uint8_t button) const
{
    return button < kPadButtons ? static_cast<PcKey>(_padKeys[button].load(std::memory_order_relaxed)) : PcKey::None;
}

void NextExtendedInput::SetPadKey(uint8_t button, PcKey key)
{
    if (button < kPadButtons)
        _padKeys[button].store(static_cast<uint8_t>(key), std::memory_order_relaxed);
}

const char* NextExtendedInput::KeyName(unsigned bit)
{
    return bit < kKeyCount ? kKeyNames[bit] : "";
}
