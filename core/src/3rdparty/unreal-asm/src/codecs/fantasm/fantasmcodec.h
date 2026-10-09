#pragma once

// FantASM sources: a text file with the FantASM dialect. Detection is weak (DH / DZ / HEX, !opt, #pragma, STRUCT / ENUM
// ... END are what it alone has); `--from fantasm` is the normal way in.

#include "codecs/text/textcodec.h"

namespace unrealasm::codecs
{
class FantasmCodec : public TextCodec
{
public:
    FantasmCodec();
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
};
}  // namespace unrealasm::codecs
