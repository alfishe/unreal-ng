#pragma once

// TASM tokenized sources (TR-DOS type 'A'), every version the library knows: 3.0-3.5, 4.0 XLD / 4.4 KVA, 4.12
// (research-tasm.md).
//
// Stream: one record per line, [length n][n body bytes][n again]; a length byte #FF ends the source (files end with
// #FF #FF). In a body: #20-#7E are ASCII; blanks are #0A n (3.x, 4.0) or a direct count #02-#1F (4.12); #80-#F0 are
// keywords of the version's table. Anything else is kept as U+F700 + byte.
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

    /// The version of a stream: from the catalog's start field, else the newest version whose tokenizer writes the most
    /// lines back unchanged; "" = not TASM. `consistent` gets every version with that much evidence
    static std::string DetectVersion(std::span<const uint8_t> bytes, const CatalogHints& hints, std::vector<std::string>* consistent = nullptr);
    /// One line body -> text (no framing) as `version` shows it
    static std::string DecodeBody(std::span<const uint8_t> body, const std::string& version);
    /// Text -> one line body by the canonical rules of `version`; false with the reason when the text cannot be held
    static bool EncodeBody(const std::string& text, const std::string& version, std::vector<uint8_t>& body, std::string& error);

private:
    /// The line bodies walked from the start; `ended` = the walk reached the end marker
    static std::vector<std::span<const uint8_t>> Bodies(std::span<const uint8_t> bytes, bool& ended);

    CodecInfo _info;
};
}  // namespace unrealasm::codecs
