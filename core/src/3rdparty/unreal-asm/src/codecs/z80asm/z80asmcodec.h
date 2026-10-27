#pragma once

// z88dk z80asm sources: a text file with the z80asm dialect. Its own directives (SECTION, PUBLIC, EXTERN, DEFC,
// DEFVARS ...) give the detection; the plain Z80 text is left to the other codecs.

#include "codecs/text/textcodec.h"

namespace unrealasm::codecs
{
class Z80asmCodec : public TextCodec
{
public:
    Z80asmCodec();
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
};
}  // namespace unrealasm::codecs
