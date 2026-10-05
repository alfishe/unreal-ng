#pragma once

// TASM keyword tables of every version (research-tasm.md §3). Tokens #80-#F0; an instruction or directive carries its
// separating blank ("ld " already holds it). All versions share the code space; they differ in which codes exist and
// how three of them are named. "" = no keyword at that code.

#include <array>
#include <cstdint>
#include <string_view>

namespace unrealasm::codecs::tasm
{
constexpr uint8_t kFirstToken = 0x80;
constexpr uint8_t kLastToken = 0xF0;

using TokenTable = std::array<std::string_view, kLastToken - kFirstToken + 1>;

/// TASM 3.0-3.5 (Rst7): #80-#E6
const TokenTable& Tasm3Tokens();
/// TASM 4.0 (XL Design) and 4.4 (KVA): TASM 3's table plus #E7-#F0 (sli inf lx hx ly hy db dm ds dw)
const TokenTable& Tasm40Tokens();
/// TASM 4.12 (Rst7): #80-#E4, with #97 defmac, #9B display, #9F endmac
const TokenTable& Tasm412Tokens();
}  // namespace unrealasm::codecs::tasm
