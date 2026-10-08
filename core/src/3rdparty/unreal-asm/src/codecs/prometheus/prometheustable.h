#pragma once

// PROMETHEUS' instruction table: what a source record's opcode and information byte stand for

#include <array>
#include <cstdint>
#include <string_view>

namespace unrealasm::codecs::prometheus
{
struct Record
{
    uint8_t opcode;
    uint8_t prefix;              ///< the information byte's bits 7-4: CB, ED, DD, FD (DD+FD = a pseudo-instruction)
    uint8_t storage;             ///< bits 2-0: 0 none, 1 byte, 2 word, 3 relative, 4 (ix+d), 5 (ix+d),n, 6 rst, 7 pseudo
    std::string_view line;       ///< "ld bc,N", "rlc (ix+d)", "defb" (pseudo-instructions: their word and N)
};

extern const std::array<Record, 686> kRecords;
}  // namespace unrealasm::codecs::prometheus
