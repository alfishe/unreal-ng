#pragma once

// ALASM tokenized sources (TR-DOS type 'H'), every version from 3.8 to 5.09 (research-alasm.md).
//
// File: a 64-byte header (name, 'H', the source length at +#21, editor state at +#23, the signature
// F3 76 C7 DD FD ED B0 D9 at +#28), then lines [n][n-1 bytes], n counting itself. In a line: #01-#0F = that many
// blanks, #10 = the rest is literal text, ';' and '"' start literal text, #80-#FA = keywords (alasmtokens.h), #FF =
// no text (a "no padding" mark before a keyword, or tail bytes the editor adds so lines can be walked backwards).
// Literal bytes >= #80 are CP866.
//
// The text is what ALASM's editor shows (minus its blank padding to the screen width); the version decides how
// keywords are spelled. Decoding finds the version from the file (the newest version that tokenizes the file exactly
// as it is); encoding writes the version asked for. Byte-exact: every line keeps its bytes while its text is unchanged.

#include "codecs/alasm/alasmtokens.h"
#include "unrealasm/codec.h"

namespace unrealasm::codecs
{
class AlasmCodec : public ISourceCodec
{
public:
    AlasmCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
    EncodeResult Encode(const SourceDocument& document, const EncodeOptions& options) const override;

    /// One line (without its length byte) -> text as `version` shows it
    static std::string DecodeLine(std::span<const uint8_t> line, const alasm::Version& version);
    /// Text -> one line record (length byte included) the way `version`'s editor stores it; false with the reason
    static bool EncodeLine(const std::string& text, const alasm::Version& version, std::vector<uint8_t>& record, std::string& error);
    /// The line records of a file (header excluded); `rest` = the bytes after the last record
    static std::vector<std::span<const uint8_t>> Lines(std::span<const uint8_t> bytes, std::span<const uint8_t>& rest, bool& framingOk);

    static constexpr size_t kHeaderSize = 64;
    static constexpr size_t kSignatureOffset = 0x28;
    static constexpr size_t kLengthOffset = 0x21;

private:
    CodecInfo _info;
};
}  // namespace unrealasm::codecs
