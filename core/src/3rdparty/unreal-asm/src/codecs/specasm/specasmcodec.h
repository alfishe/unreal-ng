#pragma once

// Specasm sources in their text form (.s: what saexport writes). The editor's .x files are not text and are not read. Detection looks
// at the shape that is Specasm's alone: labels as `.Name` on a line of their own and `=` expression operands.

#include "codecs/text/textcodec.h"

namespace unrealasm::codecs
{
class SpecasmCodec : public TextCodec
{
public:
    SpecasmCodec();
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
};
}  // namespace unrealasm::codecs
