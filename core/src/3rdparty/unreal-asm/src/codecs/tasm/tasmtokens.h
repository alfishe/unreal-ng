#pragma once

// TASM keyword tables of every version (research-tasm.md §3). Tokens #80-#F0; an instruction or directive carries its
// separating blank ("LD " already holds it); upper case, as TASM shows them. All versions share the code space; they differ in which codes exist and
// how three of them are named. "" = no keyword at that code.

#include <array>
#include <cstdint>
#include <string_view>

namespace unrealasm::codecs::tasm
{
constexpr uint8_t kFirstToken = 0x80;
constexpr uint8_t kLastToken = 0xF7;   // TASM 5.5 goes to #F7; the older tables end earlier

using TokenTable = std::array<std::string_view, kLastToken - kFirstToken + 1>;

/// TASM 3.0-3.5 (Rst7): #80-#E6
const TokenTable& Tasm3Tokens();
/// TASM 4.0 (XL Design) and 4.4 (KVA): TASM 3's table plus #E7-#F0 (SLI INF LX HX LY HY DB DM DS DW)
const TokenTable& Tasm40Tokens();
/// TASM 4.12 (Rst7): #80-#E4, with #97 DEFMAC, #9B DISPLAY, #9F ENDMAC
const TokenTable& Tasm412Tokens();
/// TASM 5.5 beta (XL Design, 1997): its own table, #80-#F7 (adds ELSE ENDIF ENDM ENDR IF IFDEF INCSEC MACRO PRINTF REPT;
/// #8A is a second C that its editor never writes). TASM 5.0 beta keeps the TASM 4.0 table
const TokenTable& Tasm55Tokens();
/// A register or condition name: an operand token (the others are commands)
bool IsOperandToken(std::string_view name);
}  // namespace unrealasm::codecs::tasm
