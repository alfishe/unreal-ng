#pragma once

// TASM 3.x and 4.x tokenized sources (TR-DOS type 'A'; design: source-formats.md, research-tasm.md).
//
// Stream: one record per line, [length n][n body bytes][n again]; a length byte #FF ends the source (the files seen
// end with #FF #FF). In a body: #20-#7E are ASCII; the run byte followed by a count is that many blanks (TASM 3: #0A,
// TASM 4: #01); #80-#F0 are tokens. Anything else (other control bytes, #F1-#FF) is kept as U+F700 + byte.
//
// Byte-exact: every line keeps its body bytes as attributes and is written back from them while its text is
// unchanged; an edited or foreign line is tokenized canonically (canonical rules: research-tasm.md §4).

#include "codecs/tasm/tasmtokens.h"
#include "unrealasm/codec.h"

namespace unrealasm::codecs
{
class TasmCodec : public ISourceCodec
{
public:
    /// version 3 or 4
    explicit TasmCodec(int version);
    const CodecInfo& Info() const override { return _info; }
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
    EncodeResult Encode(const SourceDocument& document, const EncodeOptions& options) const override;

    /// One line body -> text (no framing)
    std::string DecodeBody(std::span<const uint8_t> body) const;
    /// Text -> one line body by the canonical rules; false with the reason when the text cannot be held
    bool EncodeBody(const std::string& text, std::vector<uint8_t>& body, std::string& error) const;

private:
    /// Line records walked from the start: their count when the framing holds to an end marker, else 0
    static size_t WalkFraming(std::span<const uint8_t> bytes, size_t* runs01, size_t* runs0A);

    int _version;
    uint8_t _runByte;
    const tasm::TokenTable& _tokens;
    CodecInfo _info;
};
}  // namespace unrealasm::codecs
