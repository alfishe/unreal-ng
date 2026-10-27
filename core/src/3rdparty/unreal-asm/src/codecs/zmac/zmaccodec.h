#pragma once

// George Phillips' zmac sources: a text file with the zmac dialect. Its own pseudo-ops (MACLIB, IMPORT, ASET, COND, DEFD ...) give
// the detection.

#include "codecs/text/textcodec.h"

namespace unrealasm::codecs
{
class ZmacCodec : public TextCodec
{
public:
    ZmacCodec();
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
};
}  // namespace unrealasm::codecs
