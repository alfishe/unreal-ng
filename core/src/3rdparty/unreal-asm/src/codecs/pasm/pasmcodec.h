#pragma once

// Power Assembler (PASM, Oleg Sergeyev, 1995) sources: text with CR LF line ends, saved as TR-DOS CODE files ("the old
// TASM format": research-power-assembler.md). This codec is the text codec with the pasm dialect and its detection by
// what only PASM writes: DB / DW with DUP (value DUP count), ITXT / IBIN (a text / code file read while compiling);
// ENT without an operand and SLI add to the score.

#include "codecs/text/textcodec.h"

namespace unrealasm::codecs
{
class PasmCodec : public TextCodec
{
public:
    PasmCodec();
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
};
}  // namespace unrealasm::codecs
