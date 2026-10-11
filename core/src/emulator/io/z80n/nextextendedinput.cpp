#include "stdafx.h"

#include "nextextendedinput.h"

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
