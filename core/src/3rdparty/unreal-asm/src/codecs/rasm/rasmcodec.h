#pragma once

// Roudoudou's rasm sources: a text file with the rasm dialect. Its own directives (REPEAT / REND, MEND, BUILDSNA, BANK ...) give
// the detection.

#include "codecs/text/textcodec.h"

namespace unrealasm::codecs
{
class RasmCodec : public TextCodec
{
public:
    RasmCodec();
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
};
}  // namespace unrealasm::codecs
