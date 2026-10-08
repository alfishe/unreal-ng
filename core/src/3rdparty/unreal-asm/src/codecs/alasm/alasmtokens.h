#pragma once

// ALASM keyword tables of every version (research-alasm.md §3). All versions share one code space: a byte #80-#FA is
// looked up in the mnemonic table while no keyword has been seen on the line, in the register table after that (so
// #9F is ELSE as a mnemonic and (BC) as an operand). Versions differ only in which mnemonic codes exist and how a few
// of them are spelled.

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace unrealasm::codecs::alasm
{
constexpr uint8_t kFirstMnemonic = 0x80;
constexpr uint8_t kLastMnemonic = 0xE6;
constexpr uint8_t kFirstRegister = 0x9F;
constexpr uint8_t kLastRegister = 0xFA;

using MnemonicTable = std::array<std::string_view, kLastMnemonic - kFirstMnemonic + 1>;   ///< "" = no keyword
using RegisterTable = std::array<std::string_view, kLastRegister - kFirstRegister + 1>;   ///< "" = no keyword

struct Version
{
    std::string_view id;       ///< "3.8", "4.2", "4.42", "4.5", "4.44", "5.0", "5.05", "5.07"
    std::string_view title;    ///< "ALASM 5.07-5.09"
    MnemonicTable mnemonics;
};

/// Every version, oldest first (the order "newest compatible" detection relies on)
const std::vector<Version>& Versions();
/// The version with this id; null when unknown
const Version* FindVersion(std::string_view id);
/// The operand keywords (the same in every version)
const RegisterTable& Registers();
}  // namespace unrealasm::codecs::alasm
