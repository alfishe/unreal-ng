#pragma once

// Laser Genius (Oasis Software, 1986): the assembler-editor's tokenized source (research-laser-genius.md). The text is
// stored in paragraphs: a 2-byte line number, the statements' tokens, #F7. A statement has no end byte: a label (#F1
// name), a mnemonic or a directive starts the next one. Mnemonics are tokens #2C.., a mnemonic with its first operand
// one token #82-#C3 (LD A, = #B5); registers #05-#2A; numbers keep their radix and width (#FD byte, #FC word decimal,
// #FF / #FE hex, #F9 / #F8 binary, #FB / #FA octal); names end with bit 7 on the last character; operators #C7-#E8;
// commas and closing parentheses are not stored. Comments run to #F0. A file on disk starts with #AF, the length, the
// address of the text and four characters of the name; on tape the blocks of 2048 bytes are joined (containers/tape).
// Phoenix statements (the hash extensions' language) use tokens this codec does not know: the rest of such a
// paragraph is kept as a raw line.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "unrealasm/codec.h"

namespace unrealasm::codecs
{
class LaserGeniusCodec : public ISourceCodec
{
public:
    LaserGeniusCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
    EncodeResult Encode(const SourceDocument& document, const EncodeOptions& options) const override;

    struct Statement
    {
        std::string text;
        size_t offset = 0;   ///< where its tokens start in the bytes
        size_t length = 0;
        bool raw = false;    ///< tokens this codec does not know (the rest of the paragraph)
    };
    struct Paragraph
    {
        uint16_t number = 0;
        std::vector<Statement> statements;
    };

    /// The paragraphs of the text in `bytes` from `pos` up to `end` (or the #FFFF end marker); `used` = the bytes
    /// read; false when the bytes are no Laser Genius text (a truncated last paragraph is dropped with `truncated`)
    static bool ReadParagraphs(std::span<const uint8_t> bytes, size_t pos, size_t end, std::vector<Paragraph>& out, size_t& used, bool& truncated, int& rawCount);
    /// The tokens of one statement's text (no paragraph number, no #F7); false with the reason
    static bool EncodeStatement(const std::string& text, std::vector<uint8_t>& out, std::string& error);

private:
    CodecInfo _info;
};
}  // namespace unrealasm::codecs
