#pragma once

// The native symbol file *.usym.json (symbols/formats.md §5, proposal P-2): every field of the model, several sets,
// members this version does not know kept and written back. Encoding is deterministic: one symbol per line, fields in
// a fixed order, a field left out when it is unknown.

#include "unrealasm/symbols/codec.h"

namespace unrealasm::symbols::codecs
{
class NativeCodec : public ISymbolCodec
{
public:
    NativeCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(const Probe& probe) const override;
    SymbolDecodeResult Decode(std::span<const uint8_t> bytes) const override;
    SymbolEncodeResult Encode(const SymbolFile& file, const SymbolEncodeOptions& options) const override;

    static constexpr int kVersion = 1;

private:
    CodecInfo _info;
};
}  // namespace unrealasm::symbols::codecs
