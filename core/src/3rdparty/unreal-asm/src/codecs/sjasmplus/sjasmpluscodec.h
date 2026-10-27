#pragma once

// sjasmplus sources (decision D-10: the first output target). A sjasmplus source is a text file; this codec is the
// text codec with the sjasmplus dialect and its detection by keywords that only sjasmplus has. Encoding keeps the
// document's original code page by default: string literals become bytes in the built binary, so changing the code
// page would change the program.

#include "codecs/text/textcodec.h"

namespace unrealasm::codecs
{
class SjasmplusCodec : public TextCodec
{
public:
    SjasmplusCodec();
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
};
}  // namespace unrealasm::codecs
