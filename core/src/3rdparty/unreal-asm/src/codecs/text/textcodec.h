#pragma once

// The text codec: a source stored as text in a code page with line breaks. Byte-exact round trip: the code page,
// a UTF-8 byte-order mark, the line break of every line (mixed files too) and whether the last line ends with a
// break are kept in the document's attributes. Other text-based codecs (sjasmplus, ...) derive from it and share
// its attribute layout ("text-layout"), so a document moves between them without losing the layout.

#include "unrealasm/codec.h"

namespace unrealasm::codecs
{
class TextCodec : public ISourceCodec
{
public:
    TextCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
    EncodeResult Encode(const SourceDocument& document, const EncodeOptions& options) const override;

    static constexpr const char* kLayoutAttrs = "text-layout";

protected:
    explicit TextCodec(CodecInfo info) : _info(std::move(info)) {}
    /// The text score gate every text codec starts from: 0 when the bytes do not look like text
    static int TextGate(std::span<const uint8_t> bytes);

private:
    CodecInfo _info;
};
}  // namespace unrealasm::codecs
