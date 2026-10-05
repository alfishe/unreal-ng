#pragma once

// ZX-ASM / ZAsm sources, every version: 2.4-2.6, 3.0-3.10, Lite 1.07, 3.15-4.20 (research-zxasm.md).
//
// No header: the file is the editor's text buffer. Lines end with #0D; #06 #80+n = n blanks; from 3.0 on a keyword is
// two bytes p, #20+index with p - 2 = bit 0 capitals, bit 1 one blank after it. Labels, numbers, expressions, strings
// and comments are CP866 text. The text is what the editor shows; every line keeps its bytes while its text is
// unchanged, an edited line is tokenized by the editor's rules (research-zxasm.md §4).

#include "unrealasm/codec.h"

namespace unrealasm::codecs
{
class ZxasmCodec : public ISourceCodec
{
public:
    ZxasmCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
    EncodeResult Encode(const SourceDocument& document, const EncodeOptions& options) const override;

    /// The version from the catalog, else the newest one whose editor writes the most lines back unchanged
    static std::string DetectVersion(std::span<const uint8_t> bytes, const CatalogHints& hints, std::vector<std::string>* consistent = nullptr);
    /// One line (without #0D) -> text as `version` shows it
    static std::string DecodeLine(std::span<const uint8_t> line, const std::string& version);
    /// Text -> one line the way `version`'s editor stores it
    static bool EncodeLine(const std::string& text, const std::string& version, std::vector<uint8_t>& out, std::string& error);

private:
    CodecInfo _info;
};
}  // namespace unrealasm::codecs
