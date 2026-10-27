#pragma once

// pasmo sources: a text file; this codec is the text codec with the pasmo dialect. Pasmo has few directives of its own
// (PROC / ENDP / LOCAL / IRP / EXITM / .SHIFT / .ERROR / .WARNING, ## pasting), so detection is weak and an explicit
// `--from pasmo` / `--codec pasmo` is the normal way in.

#include "codecs/text/textcodec.h"

namespace unrealasm::codecs
{
class PasmoCodec : public TextCodec
{
public:
    PasmoCodec();
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
};
}  // namespace unrealasm::codecs
