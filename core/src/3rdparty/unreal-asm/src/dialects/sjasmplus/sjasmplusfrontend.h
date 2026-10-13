#pragma once

// sjasmplus -> IR (sjasmplus 1.23 documentation): labels from column 0 (an optional colon), statements after blanks
// and between ':' separators, C-like operator priorities, case-insensitive keywords, named macro parameters,
// DUP / REPT, WHILE, DISP / PHASE, numbers as #FF $FF 0xFF 0FFh %101 0b101. Directives the IR has no kind for
// (DEVICE, SAVEBIN, MODULE, IFDEF ...) are kept as text, which the sjasmplus backend writes back.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class SjasmplusFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "sjasmplus"; }
    FrontendResult Parse(const SourceDocument& source) const override;
};

/// An instruction sjasmplus repeats for its operands: one each (PUSH AF,BC; INC A,B; RST 0,8; JR $,$; SUB A,1 is SUB A
/// : SUB 1), a pair each (LD A,1,B,2; OUT (C),A,(C),B; JR NZ,$,Z,$), A with each (ADD A,1,B). None (the statement
/// alone): a form of its own (a fake SUB HL,DE, an undocumented RLC (IX+1),B or RES 0,(IX+1),A)
std::vector<ir::Statement> SjasmplusRepeated(const ir::Statement& s);

/// The bytes of DG / DEFG (eight characters a byte: - . _ a 0 bit, any other one a 1, blanks ignored) and DH / DEFH
/// (quoted pairs of hex digits); none when the text is not such data
std::optional<std::vector<uint8_t>> SjasmplusBitBytes(const std::string& word, const std::string& operands);
}  // namespace unrealasm::dialects
