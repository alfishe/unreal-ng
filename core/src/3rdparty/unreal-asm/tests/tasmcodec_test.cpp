// The TASM codec (versions 3.x and 4.x): real TASM 3 sources decode to the expected text, round-trip byte-exact, and
// the canonical tokenizer alone (no kept attributes) reproduces TASM's own bytes; version detection by catalog fields
// and by the stream; conversion between the versions.

#include <gtest/gtest.h>

#include <algorithm>

#include "codecs/tasm/tasmcodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/registry.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
const char* const kSources[] = {"tasm3/000LOAD.$A", "tasm3/CALLLOAD.$A"};

containers::TrdosFile Unwrap(const char* file)
{
    containers::TrdosFile out;
    std::string error;
    EXPECT_TRUE(containers::ReadHobeta(ReadTestData(file), out, error)) << file << ": " << error;
    return out;
}

/// The bytes of the source stream up to and including its end marker (the rest of `length` is what TASM left there)
std::vector<uint8_t> Stream(const std::vector<uint8_t>& data)
{
    size_t p = 0;
    while (p < data.size() && data[p] != 0xFF)
        p += static_cast<size_t>(data[p]) + 2;
    return std::vector<uint8_t>(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(std::min(p + 1, data.size())));
}

SourceDocument WithoutAttributes(SourceDocument document)
{
    for (SourceLine& line : document.lines)
        line.attrs = {};
    document.attrs = {};
    return document;
}
}  // namespace

TEST(TasmCodec_Test, RealTasm3SourcesDecodeToTheExpectedText)
{
    const codecs::TasmCodec codec;
    for (const char* file : kSources)
    {
        const DecodeResult decoded = codec.Decode(Unwrap(file).data, {});
        ASSERT_TRUE(decoded.ok) << file;
        EXPECT_EQ(decoded.document.format, "tasm");
        EXPECT_EQ(decoded.document.subversion, "3");
        EXPECT_EQ(decoded.document.dialect, "tasm");
        const std::string expected = ReadTestText(std::string(file).replace(std::string(file).size() - 2, 2, "txt"));
        EXPECT_EQ(decoded.document.Text() + "\n", expected) << file;
    }
}

TEST(TasmCodec_Test, ByteExactRoundTrip)
{
    const codecs::TasmCodec codec;
    for (const char* file : kSources)
    {
        const auto data = Unwrap(file).data;
        const DecodeResult decoded = codec.Decode(data, {});
        ASSERT_TRUE(decoded.ok);
        EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, data) << file;
    }
}

TEST(TasmCodec_Test, CanonicalTokenizerReproducesTasmsOwnBytes)
{
    // The decoded text with every kept attribute dropped: the tokenizer alone must write what TASM 3 wrote
    const codecs::TasmCodec codec;
    for (const char* file : kSources)
    {
        const auto data = Unwrap(file).data;
        const DecodeResult decoded = codec.Decode(data, {});
        const EncodeResult encoded = codec.Encode(WithoutAttributes(decoded.document), {});
        ASSERT_TRUE(encoded.ok) << file;
        const auto expected = Stream(data);
        ASSERT_GE(encoded.bytes.size(), expected.size());
        EXPECT_EQ(std::vector<uint8_t>(encoded.bytes.begin(), encoded.bytes.begin() + static_cast<std::ptrdiff_t>(expected.size())), expected)
            << file;
    }
}

TEST(TasmCodec_Test, EditedLineIsTokenizedAndTheRestKept)
{
    const codecs::TasmCodec codec;
    const auto data = Unwrap(kSources[0]).data;
    DecodeResult decoded = codec.Decode(data, {});
    decoded.document.lines[1].text = "LOOP        djnz      LOOP ; again";
    const EncodeResult encoded = codec.Encode(decoded.document, {});
    ASSERT_TRUE(encoded.ok);
    const DecodeResult again = codec.Decode(encoded.bytes, {});
    ASSERT_TRUE(again.ok);
    EXPECT_EQ(again.document.Text(), decoded.document.Text());
    // The edited line uses the djnz token and space runs, the label and the comment stay literal
    const std::vector<uint8_t>& body = again.document.lines[1].attrs.bytes;
    EXPECT_EQ(std::string(body.begin(), body.begin() + 4), "LOOP");
    EXPECT_EQ(body[4], 0x0A);
    EXPECT_EQ(body[5], 8);
    EXPECT_GE(body[6], 0x80) << "djnz is a token";
}

TEST(TasmCodec_Test, Tasm4UsesItsOwnRunByte)
{
    const codecs::TasmCodec codec;
    const SourceDocument document = SourceDocument::FromText("        org       #6000\nSTART   ld        a,1\n        ret", "tasm");
    EncodeOptions to4;
    to4.subversion = "4";
    const EncodeResult encoded = codec.Encode(document, to4);
    ASSERT_TRUE(encoded.ok);
    EXPECT_EQ(encoded.bytes[1], 0x01) << "TASM 4 space run";
    EXPECT_EQ(encoded.bytes[2], 8);
    const DecodeResult decoded = codec.Decode(encoded.bytes, {});
    ASSERT_TRUE(decoded.ok);
    EXPECT_EQ(decoded.document.Text(), document.Text());
    EXPECT_EQ(decoded.document.subversion, "4") << "detected from the run byte";
}

TEST(TasmCodec_Test, TextTasmCannotHoldIsAnError)
{
    const codecs::TasmCodec codec;
    const EncodeResult encoded = codec.Encode(SourceDocument::FromText("        ld a,1 ; ёлка"), {});
    EXPECT_FALSE(encoded.ok);
    ASSERT_EQ(encoded.diagnostics.size(), 1u);
    EXPECT_EQ(encoded.diagnostics[0].line, 1u);
}

TEST(TasmCodec_Test, BrokenFramingIsReportedAndTheRestKept)
{
    const codecs::TasmCodec codec;
    const std::vector<uint8_t> bytes = {3, 'n', 'o', 'p', 3, 5, 'a', 'b', 0xFF};
    const DecodeResult decoded = codec.Decode(bytes, {});
    EXPECT_FALSE(decoded.ok);
    ASSERT_EQ(decoded.document.lines.size(), 1u);
    EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, bytes) << "the broken tail is written back as it was";
}

TEST(TasmCodec_Test, DetectionByCatalogAndByStream)
{
    const codecs::TasmCodec codec;
    const containers::TrdosFile file = Unwrap(kSources[0]);
    EXPECT_EQ(codec.Detect(file.data, file.Hints()), 95);
    EXPECT_GE(codec.Detect(file.data, {}), 60) << "no catalog: the framing holds";
    EXPECT_EQ(codec.Detect(ReadTestData("sjasmplus/hello.asm"), {}), 0);
    EXPECT_EQ(codecs::TasmCodec::DetectVersion(file.data, file.Hints()), "3");
    EXPECT_EQ(codecs::TasmCodec::DetectVersion(file.data, {}), "3") << "no catalog: the #0A runs tell TASM 3";
    CatalogHints tasm4Start = file.Hints();
    tasm4Start.start = 4096;
    EXPECT_EQ(codecs::TasmCodec::DetectVersion(file.data, tasm4Start), "4") << "the catalog's start comes first";

    const DetectResult detected = CodecRegistry::Builtin().Detect(file.data, file.Hints());
    ASSERT_NE(detected.chosen, nullptr) << detected.reason;
    EXPECT_EQ(detected.chosen->Info().id, "tasm");
}

TEST(TasmCodec_Test, SubVersionConversionThroughTheText)
{
    // TASM 3 -> TASM 4 -> TASM 3: the text survives, TASM 4 bytes use its run byte, and back in TASM 3 the bytes are
    // TASM 3's own (kept bytes belong to the other version, so every line is tokenized canonically)
    const codecs::TasmCodec codec;
    EncodeOptions to3, to4;
    to3.subversion = "3";
    to4.subversion = "4";
    for (const char* file : kSources)
    {
        const auto data = Unwrap(file).data;
        const DecodeResult original = codec.Decode(data, {});
        const EncodeResult as4 = codec.Encode(original.document, to4);
        ASSERT_TRUE(as4.ok) << file;
        EXPECT_EQ(as4.bytes[1], 0x01) << file;
        const DecodeResult decoded4 = codec.Decode(as4.bytes, {});
        ASSERT_TRUE(decoded4.ok);
        EXPECT_EQ(decoded4.document.subversion, "4");
        EXPECT_EQ(decoded4.document.Text(), original.document.Text());
        const EncodeResult back = codec.Encode(decoded4.document, to3);
        ASSERT_TRUE(back.ok);
        const auto expected = Stream(data);
        EXPECT_EQ(std::vector<uint8_t>(back.bytes.begin(), back.bytes.begin() + static_cast<std::ptrdiff_t>(expected.size())), expected) << file;
    }
}

TEST(TasmCodec_Test, KeywordMissingInTheTargetVersionIsReported)
{
    // "defm" is a TASM 3 keyword (#97); TASM 4 uses #97 for "defmac" and has no "defm": written as text, with a warning
    const codecs::TasmCodec codec;
    EncodeOptions to3, to4;
    to3.subversion = "3";
    to4.subversion = "4";
    const EncodeResult as3 = codec.Encode(SourceDocument::FromText("MSG     defm      \"HI\"", "tasm"), to3);
    ASSERT_TRUE(as3.ok);
    EXPECT_NE(std::find(as3.bytes.begin(), as3.bytes.end(), 0x97), as3.bytes.end()) << "defm is token #97 in TASM 3";
    const DecodeResult decoded = codec.Decode(as3.bytes, {});
    const EncodeResult as4 = codec.Encode(decoded.document, to4);
    ASSERT_TRUE(as4.ok);
    ASSERT_EQ(as4.diagnostics.size(), 1u);
    EXPECT_EQ(as4.diagnostics[0].severity, Severity::Warning);
    EXPECT_NE(as4.diagnostics[0].message.find("defm"), std::string::npos);
    EXPECT_EQ(codec.Decode(as4.bytes, {}).document.Text(), decoded.document.Text()) << "the text is unchanged";
}
