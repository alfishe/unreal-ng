#pragma once

// GENS (HiSoft Devpac, 1983-87, and its TR-DOS ports) sources (research-gens.md): plain text, not tokenized.
//
// - a line is [number, 2 bytes little endian, 1..32767][text][#0D], numbers strictly increasing; no file header
// - GENS1 (version "1") stores blanks as typed and ends the file with #00 #00
// - GENS2 / GENS3 / GENS4 (version "2") have no end marker and compress a line when it is entered: the first two runs
//   of blanks become one TAB (#09) each, a run that ends the line is dropped, a line starting with ';' or '*' is kept
//
// A line's text is what the 32-column editor lists after the number: TABs expanded to the stops 7, 12, 21, 25 of each
// 26-column screen row, bytes in the Spectrum character set; its number is SourceLine::number. A line's attributes
// hold the stored bytes when the editor's compression of the text would not give them; the document's attributes the
// bytes after the last line. Decoding then encoding gives the input bytes.

#include "unrealasm/codec.h"

namespace unrealasm::codecs
{
class GensCodec : public ISourceCodec
{
public:
    GensCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
    EncodeResult Encode(const SourceDocument& document, const EncodeOptions& options) const override;

    /// GENS2-4's compression of a typed line (research-gens.md §3)
    static std::vector<uint8_t> Compress(std::span<const uint8_t> text);
    /// The stored text as the 32-column editor lists it (UTF-8, TABs expanded)
    static std::string Expand(std::span<const uint8_t> text);

private:
    CodecInfo _info;
};
}  // namespace unrealasm::codecs
