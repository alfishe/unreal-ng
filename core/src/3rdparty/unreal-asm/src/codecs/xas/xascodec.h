#pragma once

// XAS (Max Petrov / Creator, STS, Mythos; 1996-97) tokenized sources, TR-DOS type 'X' (research-xas.md):
//
// - a 36-byte header: title (29), the cursor line's address, four editor-state bytes, the #01 start sentinel
// - lines, each ended by #0D (normal), #0C (marked red) or #09 (marked green); #00 ends the text
// - no blanks are stored: keywords and registers are bytes #80-#F6 (one table, the names of #C8-#CF and #F3-#F6 differ
//   per version), text outside strings and comments in capitals; the editor lays a line out in fields
//
// XAS ignores the catalog length (usually 0): the text runs to its #00 in the file's sectors, so the codec reads the
// sector slack too (CatalogHints::slack). A line's text is the line as the editor lays it out (label, command at the
// first tab stop, operands at the second, the commas it draws, lower case outside strings), on one text line. A
// line's attributes hold its end byte when it is not #0D and the stored bytes when XAS's packer would not write them
// for the text; the document's attributes the header and the bytes after the last line. Decoding then encoding gives
// the file's bytes (sector slack included).

#include "unrealasm/codec.h"

namespace unrealasm::codecs
{
class XasCodec : public ISourceCodec
{
public:
    XasCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
    EncodeResult Encode(const SourceDocument& document, const EncodeOptions& options) const override;

    /// A stored line as the editor lays it out (XAS 7.447 #8B22 without the row cut)
    static std::string DecodeBody(std::span<const uint8_t> body, const std::string& version);
    /// Text -> stored line by XAS's line packer (XAS 7.447 #889A); false with the reason where XAS refuses the line
    static bool EncodeBody(const std::string& text, const std::string& version, std::vector<uint8_t>& body, std::string& error);

private:
    CodecInfo _info;
};
}  // namespace unrealasm::codecs
