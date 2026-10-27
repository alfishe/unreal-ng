#include "emulator/io/z80n/nextkeyjoystick.h"

namespace
{
/// keyjoy_64_6.coe, one side: Sinclair 1 (0-4), Sinclair 2 (5-9), Cursor (10-14), then the user-defined keys (none)
constexpr uint8_t kInitialSide[32] = {
    0x23, 0x24, 0x22, 0x21, 0x20,  // Sinclair 1: 7 6 8 9 0 (row 4)
    0x19, 0x18, 0x1A, 0x1B, 0x1C,  // Sinclair 2: 2 1 3 4 5 (row 3)
    0x22, 0x1C, 0x24, 0x23, 0x20,  // Cursor: 8 5 6 7 0
    0x3F,                          //
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
};

struct Span
{
    unsigned start;     ///< joy_addr_start
    unsigned firstBit;  ///< joy_bit_count_start
    unsigned lastBit;   ///< joy_bit_count_end
};

Span SpanOf(unsigned mode)
{
    switch (mode & 7)
    {
        case 3: return {0, 0, 4};    // Sinclair 1
        case 0: return {5, 0, 4};    // Sinclair 2
        case 2: return {10, 0, 4};   // Cursor
        case 7: return {16, 0, 11};  // user defined
        case 1:
        case 4: return {21, 5, 11};  // Kempston
        default: return {24, 8, 11}; // MD
    }
}
}  // namespace

NextKeyJoystick::NextKeyJoystick()
{
    for (unsigned side = 0; side < 2; side++)
        for (unsigned i = 0; i < 32; i++)
            _map[side * 32 + i] = kInitialSide[i];
}

void NextKeyJoystick::WriteAddressHigh(uint8_t value)
{
    _selected = (value & 0x80) != 0;
    _address = static_cast<uint16_t>((_address & 0xFF) | ((value & 1u) << 8));
}

void NextKeyJoystick::WriteAddressLow(uint8_t value)
{
    _address = static_cast<uint16_t>((_address & 0x100) | value);
}

void NextKeyJoystick::WriteData(uint8_t value)
{
    // A = i_keymap_addr(4) & '1' & i_keymap_addr(3 downto 0): only the entries 16-31 of a side are writable
    if (_selected)
        _map[((_address >> 4) & 1u) * 32 + 16 + (_address & 0x0F)] = static_cast<uint8_t>(value & 0x3F);
    _address = static_cast<uint16_t>((_address + 1) & 0x1FF);
}

uint8_t NextKeyJoystick::Columns(uint8_t rows, uint16_t left, unsigned leftMode, uint16_t right, unsigned rightMode) const
{
    uint8_t columns = 0x7F;
    if ((left | right) == 0)
        return columns;
    const auto press = [&](unsigned side, uint16_t state, unsigned mode) {
        const Span span = SpanOf(mode);
        for (unsigned bit = span.firstBit; bit <= span.lastBit; bit++)
        {
            if (!(state & (1u << bit)))
                continue;
            const uint8_t entry = _map[side * 32 + span.start + (bit - span.firstBit)];
            const unsigned row = entry >> 3, column = entry & 7u;
            if (column < 7 && !(rows & (1u << row)))
                columns = static_cast<uint8_t>(columns & ~(1u << column));
        }
    };
    press(0, left, leftMode);
    press(1, right, rightMode);
    return columns;
}
