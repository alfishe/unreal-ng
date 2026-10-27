#pragma once

// MASM (Master Assembler, KSA Software & *AIG*, Moscow 1995-96) tokenized sources, TR-DOS type 'a' (research-masm.md):
//
// - 1.0 demo, 1.1 / 1.3: TASM 3's framing [n][n body bytes][n], an empty line one #00, #FF ends; #0A n = n blanks
// - 2.0 TURBO: #FF, lines ending in #00, #FF; a byte #01-#1F = 2-32 blanks
// - 3.0 MACRO: #FF lo hi #FF (the cursor line), then as 2.0
//
// In a body #20-#7F are ASCII and #80 and up keywords of the version's table (registers first, then directives and
// instructions; a keyword that takes operands holds its blank). The bytes after the end marker (the rest of the last
// sector) are kept as the file's attributes, every line its body bytes: decoding then encoding gives the input bytes.

#include "unrealasm/codec.h"

namespace unrealasm::codecs
{
class MasmCodec : public ISourceCodec
{
public:
    MasmCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
    EncodeResult Encode(const SourceDocument& document, const EncodeOptions& options) const override;

    /// The versions whose framing reads the whole stream, the likeliest first ("" none)
    static std::vector<std::string> DetectVersions(std::span<const uint8_t> bytes, const CatalogHints& hints);
    static std::string DecodeBody(std::span<const uint8_t> body, const std::string& version);
    /// Text -> body by MASM's tokenizer (research-masm.md §5); false with the reason when the text cannot be held
    static bool EncodeBody(const std::string& text, const std::string& version, std::vector<uint8_t>& body, std::string& error);

private:
    CodecInfo _info;
};
}  // namespace unrealasm::codecs
