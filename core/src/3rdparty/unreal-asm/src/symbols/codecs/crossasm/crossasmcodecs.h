#pragma once

// Symbol files of the cross assemblers (symbols/formats.md §3, phase S3), checked against sjasmplus 1.24 and pasmo
// 0.5.5 output (testdata/symbols/):
//
//   sjasmplus-sym  NAME: EQU 0x0000HHHH               --sym and --exp; sorted by name
//   sjasmplus-sld  |SLD.data.version|1, then file|line|deffile|defline|page|value|type|data   (--sld, version 1)
//   sjasmplus-lst  the listing (--lst): a line defining a label gives its address; EQU with a number its value
//   pasmo          NAME<TAB>[<TAB>]EQU 0HHHHH          pasmo's symbol file (the third argument); sorted by name

#include "unrealasm/symbols/codec.h"

namespace unrealasm::symbols::codecs
{
class SjasmplusSymCodec : public ISymbolCodec
{
public:
    SjasmplusSymCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(const Probe& probe) const override;
    SymbolDecodeResult Decode(std::span<const uint8_t> bytes) const override;
    SymbolEncodeResult Encode(const SymbolFile& file, const SymbolEncodeOptions& options) const override;

private:
    CodecInfo _info;
};

class SjasmplusSldCodec : public ISymbolCodec
{
public:
    SjasmplusSldCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(const Probe& probe) const override;
    SymbolDecodeResult Decode(std::span<const uint8_t> bytes) const override;
    SymbolEncodeResult Encode(const SymbolFile& file, const SymbolEncodeOptions& options) const override;

private:
    CodecInfo _info;
};

class SjasmplusLstCodec : public ISymbolCodec
{
public:
    SjasmplusLstCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(const Probe& probe) const override;
    SymbolDecodeResult Decode(std::span<const uint8_t> bytes) const override;
    SymbolEncodeResult Encode(const SymbolFile& file, const SymbolEncodeOptions& options) const override;

    static constexpr size_t kSourceColumn = 24;   ///< sjasmplus' listing: the source text starts here (0-based)

private:
    CodecInfo _info;
};

class PasmoCodec : public ISymbolCodec
{
public:
    PasmoCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(const Probe& probe) const override;
    SymbolDecodeResult Decode(std::span<const uint8_t> bytes) const override;
    SymbolEncodeResult Encode(const SymbolFile& file, const SymbolEncodeOptions& options) const override;

private:
    CodecInfo _info;
};
}  // namespace unrealasm::symbols::codecs
