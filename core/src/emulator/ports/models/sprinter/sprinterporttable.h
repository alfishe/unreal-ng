#pragma once

#include <cstdint>

/// @file sprinterporttable.h
/// @brief The Sprinter port table index (Sprinter tdd-ports-memory §3.1, hardware-reference §4.1):
/// one formula for the decoder (PortDecoder_Sprinter::LookupIndex) and for the automation
/// views of the table (DeviceState::SprinterPortTable / SprinterPortLookup), which ask for any
/// map, DOS state, direction and PN5, not only the current ones.
///
/// RAM page #40 holds 4 maps of 4 KB; inside a map the index is built from bus signals:
///
///   bit 11 PN5 (#7FFD bit 5)   bit 10 /DOS (1 = TR-DOS off)   bit 9 /WR (1 = IN, 0 = OUT)
///   bits 8-0: A15 A14 A6 A5 A13 A7 A2 A1 A0
///
/// Worked example (MAN p. 28): port #7785, write, DOS on, PN5 = 0, map 0:
/// A15..A13 = 0 1 1, A7 = 1, A6 A5 = 0 0, A2..A0 = 1 0 1 -> address bits #09D, index #009D.
namespace SprinterPortTable
{
constexpr uint16_t kMapSize = 0x1000;
constexpr uint16_t kMaps = 4;
/// Address bits per (map, PN5, DOS, direction) combination: A15 A14 A6 A5 A13 A7 A2 A1 A0
constexpr uint16_t kAddressCombinations = 0x200;

/// The 9 address bits of `port` as the PLD sees them (index bits 8-0)
constexpr uint16_t AddressBits(uint16_t port)
{
    return static_cast<uint16_t>(((port >> 14) & 0x03) << 7    // A15, A14
                                 | ((port >> 13) & 0x01) << 4  // A13
                                 | ((port >> 7) & 0x01) << 3   // A7
                                 | (port & 0x67));             // A6, A5, A2, A1, A0
}

/// The lowest port with these 9 address bits (the bits the PLD ignores are 0)
constexpr uint16_t ExamplePort(uint16_t bits)
{
    return static_cast<uint16_t>(((bits >> 7) & 0x03) << 14 | ((bits >> 4) & 0x01) << 13 | ((bits >> 3) & 0x01) << 7 |
                                 (bits & 0x67));
}

/// Offset in page #40: `map` 0-3 (CNF bits 4-3), `pn5` (#7FFD bit 5), `dosOff` (/DOS: 1 = TR-DOS off)
constexpr uint16_t Index(uint8_t map, bool pn5, bool dosOff, bool isRead, uint16_t port)
{
    return static_cast<uint16_t>((map & 0x03) << 12 | (pn5 ? 1 : 0) << 11 | (dosOff ? 1 : 0) << 10 | (isRead ? 1 : 0) << 9 |
                                 AddressBits(port));
}

static_assert(Index(0, false, false, false, 0x7785) == 0x009D, "MAN p. 28 worked example");
static_assert(ExamplePort(AddressBits(0x21BC)) == 0x20A4, "#21BC (IDE primary): A13 A7 A5 A2 kept, A12-A8 A4 A3 dropped");
}  // namespace SprinterPortTable
