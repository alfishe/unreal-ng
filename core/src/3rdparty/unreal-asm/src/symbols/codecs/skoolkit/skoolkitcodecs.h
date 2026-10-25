#pragma once

// SkoolKit (https://skoolkit.ca) files, for exchanging labels, comments and the code / data classification with a
// SkoolKit disassembly (symbols/formats.md §3):
//
//   skoolkit-ctl     a control file for sna2skool.py -c: block directives "c $HHHH Title" (b c g i s t u w),
//                    "@ $HHHH label=NAME", "@ $HHHH equ=NAME=VALUE", sub-block and N comments
//   skoolkit-skool   a skool file: entries (a comment block, then "c$HHHH DI ; comment" lines), "@label=NAME" over an
//                    instruction, "@equ=NAME=VALUE"; written as a head.skool of @equ lines (a skool file needs the
//                    instructions, which a symbol file does not have)
//
// What a symbol holds of a SkoolKit file: an entry (block) is a symbol at its address with the block's kind (c code,
// the others data), its size (to the next block) and its title as the comment; the block letter is kept as the trait
// "skoolkit:<letter>". An entry without a label gets SkoolKit's default label L<HHHH> (trait "skoolkit:auto", not
// written back as a label). A label inside an entry is a symbol of the entry's kind with the trait "skoolkit:point"
// and the instruction's (or the sub-block's, or the N) comment. @equ is a symbol at its value with the trait
// "skoolkit:equ". Not kept: descriptions (D), register lists (R), end comments (E), multi-line comments (M), loops (L),
// sub-block types and lengths, the other ASM directives, '>' lines, the instructions themselves.

#include "unrealasm/symbols/codec.h"

namespace unrealasm::symbols::codecs
{
class SkoolkitCtlCodec : public ISymbolCodec
{
public:
    SkoolkitCtlCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(const Probe& probe) const override;
    SymbolDecodeResult Decode(std::span<const uint8_t> bytes) const override;
    SymbolEncodeResult Encode(const SymbolFile& file, const SymbolEncodeOptions& options) const override;

private:
    CodecInfo _info;
};

class SkoolkitSkoolCodec : public ISymbolCodec
{
public:
    SkoolkitSkoolCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(const Probe& probe) const override;
    SymbolDecodeResult Decode(std::span<const uint8_t> bytes) const override;
    SymbolEncodeResult Encode(const SymbolFile& file, const SymbolEncodeOptions& options) const override;

private:
    CodecInfo _info;
};
}  // namespace unrealasm::symbols::codecs
