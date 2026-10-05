// The text codec: byte-exact round trip of every corpus file (code pages, line ends, byte-order mark, no final
// break, mixed ends, invalid bytes, empty lines and files), the decoded text, and canonical re-encoding.

#include <gtest/gtest.h>

#include "codecs/text/textcodec.h"
#include "testdata.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
const char* kCorpus[] = {
    "text/source-cp866-lf.asm",  "text/source-cp866-crlf.asm",  "text/source-cp866-cr.asm",
    "text/source-koi8r-lf.asm",  "text/source-koi8r-crlf.asm",  "text/source-koi8r-cr.asm",
    "text/source-cp1251-lf.asm", "text/source-cp1251-crlf.asm", "text/source-cp1251-cr.asm",
    "text/source-utf8-lf.asm",   "text/source-utf8-crlf.asm",   "text/source-utf8-cr.asm",
    "text/bom-utf8.asm", "text/no-final-break.asm", "text/mixed-ends.asm", "text/invalid-utf8.asm",
    "text/empty-lines.asm", "text/empty.asm",
};
}  // namespace

TEST(TextCodec_Test, EveryCorpusFileRoundTripsByteExact)
{
    const codecs::TextCodec codec;
    for (const char* file : kCorpus)
    {
        const auto bytes = ReadTestData(file);
        const DecodeResult decoded = codec.Decode(bytes, {});
        ASSERT_TRUE(decoded.ok) << file;
        const EncodeResult encoded = codec.Encode(decoded.document, {});
        ASSERT_TRUE(encoded.ok) << file;
        EXPECT_EQ(encoded.bytes, bytes) << file;
    }
}

TEST(TextCodec_Test, DecodedTextIsTheSameInEveryCodePage)
{
    const codecs::TextCodec codec;
    std::string expected = ReadTestText("text/source.utf8.txt");
    expected.pop_back();   // the final '\n'
    for (const char* file : {"text/source-cp866-crlf.asm", "text/source-koi8r-cr.asm", "text/source-cp1251-lf.asm", "text/source-utf8-lf.asm"})
        EXPECT_EQ(codec.Decode(ReadTestData(file), {}).document.Text(), expected) << file;
}

TEST(TextCodec_Test, RecordsCodePageAndLineEnd)
{
    const codecs::TextCodec codec;
    const DecodeResult decoded = codec.Decode(ReadTestData("text/source-koi8r-crlf.asm"), {});
    EXPECT_EQ(decoded.document.codePage, encoding::CodePage::Koi8r);
    EXPECT_EQ(decoded.document.lineEnd, encoding::LineEnd::CrLf);
    EXPECT_EQ(decoded.document.format, "text");
}

TEST(TextCodec_Test, ForcedCodePageAndCanonicalReencoding)
{
    const codecs::TextCodec codec;
    DecodeOptions forced;
    forced.codePage = encoding::CodePage::Cp866;
    const DecodeResult decoded = codec.Decode(ReadTestData("text/source-cp866-crlf.asm"), forced);
    EncodeOptions toKoi8;
    toKoi8.codePage = encoding::CodePage::Koi8r;
    toKoi8.lineEnd = encoding::LineEnd::Cr;
    const EncodeResult encoded = codec.Encode(decoded.document, toKoi8);
    ASSERT_TRUE(encoded.ok);
    EXPECT_EQ(encoded.bytes, ReadTestData("text/source-koi8r-cr.asm")) << "a code page and line-end conversion";
}

TEST(TextCodec_Test, InvalidBytesAreWarnedAndKept)
{
    const codecs::TextCodec codec;
    DecodeOptions utf8;
    utf8.codePage = encoding::CodePage::Utf8;   // auto-detection would read #FF #FE as CP1251 letters
    const auto bytes = ReadTestData("text/invalid-utf8.asm");
    const DecodeResult decoded = codec.Decode(bytes, utf8);
    ASSERT_FALSE(decoded.diagnostics.empty());
    EXPECT_EQ(decoded.diagnostics.front().severity, Severity::Warning);
    EXPECT_EQ(decoded.diagnostics.front().line, 1u);
    EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, bytes) << "the invalid bytes are written back as they were";
}

TEST(TextCodec_Test, UnmappableCharacterIsAnError)
{
    const codecs::TextCodec codec;
    SourceDocument document = SourceDocument::FromText("\tDB \"€\"\n");
    EncodeOptions options;
    options.codePage = encoding::CodePage::Cp866;
    const EncodeResult encoded = codec.Encode(document, options);
    EXPECT_FALSE(encoded.ok);
    ASSERT_FALSE(encoded.diagnostics.empty());
    EXPECT_EQ(encoded.diagnostics.front().line, 1u);
}
