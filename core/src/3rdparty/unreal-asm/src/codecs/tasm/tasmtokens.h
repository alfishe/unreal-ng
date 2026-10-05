#pragma once

// TASM token tables (research-tasm.md of the design). TASM 3.x: tokens #80-#F0 with the separating blank inside the
// token ("ld " already carries the blank), space runs #0A n. TASM 4.x: the same table with three codes reassigned
// (#97 defmac, #9B display, #9F endmac) and space runs #01 n.

#include <array>
#include <cstdint>
#include <string_view>

namespace unrealasm::codecs::tasm
{
constexpr uint8_t kFirstToken = 0x80;
constexpr uint8_t kLastToken = 0xF0;

using TokenTable = std::array<std::string_view, kLastToken - kFirstToken + 1>;

const TokenTable& Tasm3Tokens();
const TokenTable& Tasm4Tokens();
}  // namespace unrealasm::codecs::tasm
