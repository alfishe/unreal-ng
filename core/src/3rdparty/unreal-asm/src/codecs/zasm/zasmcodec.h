#pragma once

// Megatokio's zasm sources: a text file with the zasm dialect. The #-directives (#target, #code, #data, #local ...) give
// the detection.

#include "codecs/text/textcodec.h"

namespace unrealasm::codecs
{
class ZasmCodec : public TextCodec
{
public:
    ZasmCodec();
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
};
}  // namespace unrealasm::codecs
