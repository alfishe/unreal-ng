// The TASM 3 / 4 codecs: real TASM 3 sources decode to the expected text, round-trip byte-exact, and the canonical
// tokenizer alone (no kept attributes) reproduces TASM's own bytes; detection by catalog fields and by the stream.

#include <gtest/gtest.h>

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
    const codecs::TasmCodec codec(3);
    for (const char* file : kSources)
    {
        const DecodeResult decoded = codec.Decode(Unwrap(file).data, {});
        ASSERT_TRUE(decoded.ok) << file;
        EXPECT_EQ(decoded.document.format, "tasm3");
        EXPECT_EQ(decoded.document.dialect, "tasm");
        const std::string expected = ReadTestText(std::string(file).replace(std::string(file).size() - 2, 2, "txt"));
        EXPECT_EQ(decoded.document.Text() + "\n", expected) << file;
    }
}

TEST(TasmCodec_Test, ByteExactRoundTrip)
{
    const codecs::TasmCodec codec(3);
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
    const codecs::TasmCodec codec(3);
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
    const codecs::TasmCodec codec(3);
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
    const codecs::TasmCodec tasm4(4);
    const SourceDocument document = SourceDocument::FromText("        org       #6000\nSTART   ld        a,1\n        ret", "tasm");
    const EncodeResult encoded = tasm4.Encode(document, {});
    ASSERT_TRUE(encoded.ok);
    EXPECT_EQ(encoded.bytes[1], 0x01) << "TASM 4 space run";
    EXPECT_EQ(encoded.bytes[2], 8);
    const DecodeResult decoded = tasm4.Decode(encoded.bytes, {});
    ASSERT_TRUE(decoded.ok);
    EXPECT_EQ(decoded.document.Text(), document.Text());
    EXPECT_EQ(decoded.document.subversion, "4.x");
}

TEST(TasmCodec_Test, TextTasmCannotHoldIsAnError)
{
    const codecs::TasmCodec codec(3);
    const EncodeResult encoded = codec.Encode(SourceDocument::FromText("        ld a,1 ; ёлка"), {});
    EXPECT_FALSE(encoded.ok);
    ASSERT_EQ(encoded.diagnostics.size(), 1u);
    EXPECT_EQ(encoded.diagnostics[0].line, 1u);
}

TEST(TasmCodec_Test, BrokenFramingIsReportedAndTheRestKept)
{
    const codecs::TasmCodec codec(3);
    const std::vector<uint8_t> bytes = {3, 'n', 'o', 'p', 3, 5, 'a', 'b', 0xFF};
    const DecodeResult decoded = codec.Decode(bytes, {});
    EXPECT_FALSE(decoded.ok);
    ASSERT_EQ(decoded.document.lines.size(), 1u);
    EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, bytes) << "the broken tail is written back as it was";
}

TEST(TasmCodec_Test, DetectionByCatalogAndByStream)
{
    const codecs::TasmCodec tasm3(3), tasm4(4);
    const containers::TrdosFile file = Unwrap(kSources[0]);
    EXPECT_EQ(tasm3.Detect(file.data, file.Hints()), 95);
    EXPECT_LE(tasm4.Detect(file.data, file.Hints()), 20);
    EXPECT_GE(tasm3.Detect(file.data, {}), 60) << "no catalog: the #0A runs tell TASM 3";
    EXPECT_LE(tasm4.Detect(file.data, {}), 40);
    EXPECT_EQ(tasm3.Detect(ReadTestData("sjasmplus/hello.asm"), {}), 0);

    const DetectResult detected = CodecRegistry::Builtin().Detect(file.data, file.Hints());
    ASSERT_NE(detected.chosen, nullptr) << detected.reason;
    EXPECT_EQ(detected.chosen->Info().id, "tasm3");
}

TEST(TasmCodec_Test, SubVersionConversionThroughTheText)
{
    // TASM 3 -> TASM 4 -> TASM 3: the text survives, TASM 4 bytes use its run byte, and back in TASM 3 the bytes are
    // TASM 3's own (the kept attributes belong to the other codec, so every line is tokenized canonically)
    const codecs::TasmCodec tasm3(3), tasm4(4);
    for (const char* file : kSources)
    {
        const auto data = Unwrap(file).data;
        const DecodeResult original = tasm3.Decode(data, {});
        const EncodeResult as4 = tasm4.Encode(original.document, {});
        ASSERT_TRUE(as4.ok) << file;
        EXPECT_EQ(as4.bytes[1], 0x01) << file;
        const DecodeResult decoded4 = tasm4.Decode(as4.bytes, {});
        ASSERT_TRUE(decoded4.ok);
        EXPECT_EQ(decoded4.document.Text(), original.document.Text());
        const EncodeResult back = tasm3.Encode(decoded4.document, {});
        ASSERT_TRUE(back.ok);
        const auto expected = Stream(data);
        EXPECT_EQ(std::vector<uint8_t>(back.bytes.begin(), back.bytes.begin() + static_cast<std::ptrdiff_t>(expected.size())), expected) << file;
    }
}
