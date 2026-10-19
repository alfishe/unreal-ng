#pragma once

/// @file nextkeyjoystick.h
/// @brief The Next's key joystick (membrane_stick.vhd): the two DB9 connectors press membrane keys.
///
/// A 64 x 6 map (keyjoy_64_6.coe; per side 32 entries: row 5:3, column 2:0) turns a pad bit into a key. NR #05 picks per
/// side where the bits start in the map: Sinclair 1 at 0, Sinclair 2 at 5, Cursor at 10 (bits 0-4: R L D U fire), the
/// user-defined keys at 16 (all 12 bits), Kempston at 21 (bits 5-11, the buttons the port has no place for) and MD at 24
/// (bits 8-11, Y Z X MODE). A program writes the entries 16-31 of a side: NR #28 bit 7 = 1 selects the joystick map,
/// bit 0 the address MSB, NR #29 the low byte (bit 4 = right side, bits 3:0 = the pad bit), each NR #2B write stores
/// bits 5:0 and steps the address (zxnext.vhd 6297-6322). The map is not reset: only the configuration loads it.
/// NR #0B bit 7 (the joystick I/O mode) turns the keys off.

#include <array>
#include <cstdint>

class NextKeyJoystick
{
public:
    static constexpr unsigned kEntries = 64;
    static constexpr uint8_t kNoKey = 0x07;  ///< column 7: no membrane column

    NextKeyJoystick();

    /// region <NR #28, #29, #2B>
    void WriteAddressHigh(uint8_t value);
    void WriteAddressLow(uint8_t value);
    void WriteData(uint8_t value);
    /// endregion

    /// The membrane columns (bits 6:0, active low) the pads hold on the rows a read selects (port bits 15:8, active low);
    /// mode = the NR #05 joystick type of the side (000 Sinclair 2, 001 Kempston 1, 010 Cursor, 011 Sinclair 1,
    /// 100 Kempston 2, 101 MD 1, 110 MD 2, 111 user defined)
    uint8_t Columns(uint8_t rows, uint16_t left, unsigned leftMode, uint16_t right, unsigned rightMode) const;

    uint8_t Entry(unsigned index) const { return index < kEntries ? _map[index] : kNoKey; }
    void SetEntry(unsigned index, uint8_t value)
    {
        if (index < kEntries)
            _map[index] = static_cast<uint8_t>(value & 0x3F);
    }

    /// The NR #28 / #29 state (the state image)
    uint16_t Address() const { return _address; }
    bool Selected() const { return _selected; }
    void Restore(uint16_t address, bool selected)
    {
        _address = static_cast<uint16_t>(address & 0x1FF);
        _selected = selected;
    }

private:
    std::array<uint8_t, kEntries> _map{};
    uint16_t _address = 0;  ///< nr_keymap_addr: 9 bits
    bool _selected = false;  ///< nr_keymap_sel: NR #2B writes the joystick map, not the PS/2 keymap
};
