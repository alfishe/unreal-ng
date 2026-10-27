#pragma once

// STORM sources (X-Trade, 1997), versions 1.0beta (text from #C003) and 1.2-1.3i (text from #C00B) (research-storm.md).
//
// Lines are stored as [body][length] and walked backwards from the end of the file. A body is close to compiled code:
// one byte per keyword, the command often implied by its first operand (LD B,... is just the byte of B), numbers with a
// descriptor saying their form (hex / decimal byte or word, binary, character, label, $), labels packed 6 bits per
// character, single digits and $-12..$+12 as one byte. The text is the canonical layout STORM shows; every line keeps
// its bytes while its text is unchanged, an edited line is written by STORM's rules.

#include "unrealasm/codec.h"

namespace unrealasm::codecs
{
class StormCodec : public ISourceCodec
{
public:
    StormCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
    EncodeResult Encode(const SourceDocument& document, const EncodeOptions& options) const override;

    /// One line body (without its length byte) -> text
    static std::string DecodeLine(std::span<const uint8_t> body, bool* clean = nullptr);
    /// Text -> one line body; false with the reason when the text is not a line STORM can hold
    static bool EncodeLine(const std::string& text, std::vector<uint8_t>& body, std::string& error);
    /// The line records walked back from the end: (offset of the body, body length); `rest` = bytes before the first
    static std::vector<std::pair<size_t, size_t>> Lines(std::span<const uint8_t> bytes, size_t& rest, bool& framingOk);

private:
    CodecInfo _info;
};
}  // namespace unrealasm::codecs
