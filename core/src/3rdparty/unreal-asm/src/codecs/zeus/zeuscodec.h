#pragma once

// ZEUS (Crystal Computing 1983, and its ex-USSR descendants) tokenized sources (research-zeus.md):
//
// - a line is [number, 2 bytes little endian, 0..65534][line bytes][#00], numbers increasing; #FF #FF ends the file
// - line bytes: #20-#7F characters, #0A n = n blanks (0 = 256), #80+k keyword k of the version's table (a keyword
//   that takes operands holds its blank)
// - versions: "1983" (Crystal ZEUS and the ports that keep its table and tokenizer), "gg" (ZEUS from GG: DB DM DS DW,
//   INCBIN), "pht" (ZEUS 1.1 beta of Professional Hackers Tools and ZEUS v7.E: DB DM DS DW, INCLUDE, PLACE, and '_'
//   and other symbols join words)
//
// A line's text is what ZEUS lists after the number and its blank, other bytes in the Spectrum character set. A line's
// attributes hold its number and, when ZEUS's tokenizer would not give the stored bytes for the text, those bytes; the
// document's attributes the bytes after #FF #FF. Decoding then encoding gives the input bytes.

#include "unrealasm/codec.h"

namespace unrealasm::codecs
{
class ZeusCodec : public ISourceCodec
{
public:
    ZeusCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
    EncodeResult Encode(const SourceDocument& document, const EncodeOptions& options) const override;

    /// The versions the stored lines fit, the likeliest first ("" when the bytes are no ZEUS source)
    static std::vector<std::string> DetectVersions(std::span<const uint8_t> bytes);
    static std::string DecodeBody(std::span<const uint8_t> body, const std::string& version);
    /// Text -> line bytes by ZEUS's line entry (research-zeus.md §4); false with the reason when the text cannot be held
    static bool EncodeBody(const std::string& text, const std::string& version, std::vector<uint8_t>& body, std::string& error);
    /// The line number a decoded line carries (-1 when it has none)
    static int LineNumber(const SourceLine& line);

private:
    CodecInfo _info;
};
}  // namespace unrealasm::codecs
