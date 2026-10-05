#pragma once

// TASM tokenized sources, every version the library knows: 3.x and 4.x (TR-DOS type 'A'; research-tasm.md).
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
    TasmCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
    EncodeResult Encode(const SourceDocument& document, const EncodeOptions& options) const override;

    /// The version of a stream: from the catalog's start field, else from the space-run byte; "" = not TASM
    static std::string DetectVersion(std::span<const uint8_t> bytes, const CatalogHints& hints);
    /// One line body -> text (no framing) as `version` shows it
    static std::string DecodeBody(std::span<const uint8_t> body, const std::string& version);
    /// Text -> one line body by the canonical rules of `version`; false with the reason when the text cannot be held
    static bool EncodeBody(const std::string& text, const std::string& version, std::vector<uint8_t>& body, std::string& error);

private:
    /// Line records walked from the start: their count when the framing holds to an end marker, else 0
    static size_t WalkFraming(std::span<const uint8_t> bytes, size_t* runs01, size_t* runs0A);

    CodecInfo _info;
};
}  // namespace unrealasm::codecs
