#pragma once

#include <cstddef>
#include <cstdint>

/// @file sprinterzxports.h
/// @brief The Spectrum ports whose decode tells the Sprinter's ZX modes apart (tdd-zx-mode.md §12): the ZX
/// mode report lists their table codes, the PLD journal notes when a port table write changes one.
///
/// The PLD decodes A15 A14 A13 A7 A6 A5 A2 A1 A0 (sprinterporttable.h), so every #xxFD port with the same
/// A15-A13 is one table entry: #01FD and #1FFD are the same index, #C0FD is #DFFD. The list holds one
/// port per A15-A13 combination plus the spellings programs use.
///
/// Worked example: Across the Edge writes OUT (#01FD) - the index of #1FFD. In SP.ZX (CNF #07) the code
/// #C0 reaches the #1FFD latch; in P128.ZX (CNF #4E, bit 6 "SC clean") the same code stores the cell and
/// the latch stays 0.
namespace SprinterZxPorts
{
struct KeyPort
{
    uint16_t port;
    const char* label;
};

inline constexpr KeyPort kKeyPorts[] = {
    {0x7FFD, "#7FFD (128K paging)"},
    {0x1FFD, "#1FFD (Scorpion paging)"},
    {0x01FD, "#01FD (= #1FFD to the PLD: A12-A8 not decoded)"},
    {0x3FFD, "#3FFD"},
    {0x5FFD, "#5FFD"},
    {0x9FFD, "#9FFD"},
    {0xBFFD, "#BFFD (AY data)"},
    {0xDFFD, "#DFFD (also #C0FD)"},
    {0xFFFD, "#FFFD (AY register)"},
    {0x00FE, "#FE (keyboard, border, beeper, tape)"},
    {0x001F, "#1F (Kempston joystick; WD1793 status in TR-DOS)"},
};
inline constexpr size_t kKeyPortCount = sizeof(kKeyPorts) / sizeof(kKeyPorts[0]);

/// The address bits the PLD decodes (A15 A14 A13 A7 A6 A5 A2 A1 A0): a TTD port-events mask that finds
/// every spelling of a port the table treats as one
inline constexpr uint16_t kDecodedAddressMask = 0xE0E7;

/// KeyPortDecodes layout: [map 0-3][dos on 0 / off 1][read 1 / write 0][port]
inline constexpr size_t DecodeIndex(uint8_t map, bool dosOff, bool isRead, size_t port)
{
    return ((static_cast<size_t>(map & 3) * 2 + (dosOff ? 1 : 0)) * 2 + (isRead ? 1 : 0)) * kKeyPortCount + port;
}
inline constexpr size_t kDecodeCount = 4 * 2 * 2 * kKeyPortCount;
}  // namespace SprinterZxPorts
