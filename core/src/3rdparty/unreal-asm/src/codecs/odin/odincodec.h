#pragma once

// Odin (Matt Davies' editor / assembler for the ZX Spectrum Next) documents, the .odn files (Odin's book/src/memory.md,
// "Document format"): the magic 'ODS' and a version byte (0), $FF and an empty first line, then the lines each ending with
// $00 and a final $FF. Inside a line: $0A-$20 is a run of 23 .. 1 blanks, $21-$7F are characters, $01-$03 select the 2nd..4th
// spelling of the keyword token that follows (DB / DEFB), $80-$FE are keyword tokens: the first token of a line is looked up
// in the table of mnemonics and directives, the later ones in the table of registers, conditions and operators. A document's
// text is the line's characters with every token spelled out in upper case. A line whose bytes are not what encoding its
// text gives (a different run of blanks, a keyword left as letters) keeps them in its attributes; the bytes after the final
// $FF are the document's. Decoding then encoding gives the input bytes.

#include "unrealasm/codec.h"

namespace unrealasm::codecs
{
class OdinCodec : public ISourceCodec
{
public:
    OdinCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
    EncodeResult Encode(const SourceDocument& document, const EncodeOptions& options) const override;

    /// The tokenized bytes of a line of text (ASCII / Spectrum characters): what Odin's editor stores for it
    static std::vector<uint8_t> Tokenize(const std::string& text);
    /// The text of a tokenized line
    static std::string Expand(std::span<const uint8_t> line, bool& valid);

private:
    CodecInfo _info;
};
}  // namespace unrealasm::codecs
