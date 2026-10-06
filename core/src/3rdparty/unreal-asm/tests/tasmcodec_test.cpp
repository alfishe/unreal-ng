// The TASM codec, every version: 2.0 (text), 3.0-3.5, 4.0 XLD / 4.4 KVA, 4.12. Real sources of each version decode to the expected
// text and round-trip byte-exact; the canonical tokenizer alone (no kept bytes) reproduces TASM's own bytes; the
// version comes from the catalog's start field, or from the stream; conversion between the versions.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>

#include "codecs/tasm/tasmcodec.h"
#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/registry.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;
using unrealasm::testing::ReadTestText;

namespace
{
struct Sample
{
    const char* file;
    const char* version;                     ///< from the catalog's start field
    std::vector<std::string> fromStream;     ///< what the bytes alone are consistent with
};

// research-tasm.md §6
const Sample kSamples[] = {
    {"PRINTHL", "3", {"3", "4.0"}},          // Legend of Kyrandia, start 39221
    {"APEAR", "3", {"3", "4.0"}},
    {"000LOAD", "4.0", {"4.0"}},             // start 40872: saved by TASM 4.0 XLD / 4.4 (uses a 4.0-only keyword)
    {"CALLLOAD", "4.0", {"3", "4.0"}},
    {"TABLES_L", "4.0", {"4.0"}},            // General Sound 1.04 sources: db / dw (#ED, #F0) exist only in 4.0
    {"SGEN_ASM", "4.0", {"4.0"}},            // lx (#E9)
    {"EXAMPLES", "4.12", {"4.12"}},          // the TASM 4.12 disk's own examples: direct blank counts
    {"SINUS", "4.12", {"4.12"}},
    {"SNAKE", "4.12", {"4.12"}},
    {"ODNO", "4.12", {"4.12"}},              // start 71 (4.12 keeps the editor's line there)
};

containers::TrdosFile Unwrap(const std::string& name)
{
    containers::TrdosFile out;
    std::string error;
    EXPECT_TRUE(containers::ReadHobeta(ReadTestData("tasm/" + name + ".$A"), out, error)) << name << ": " << error;
    return out;
}

/// The bytes of the source stream up to and including its end marker
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

std::vector<uint8_t> Prefix(const std::vector<uint8_t>& bytes, size_t size)
{
    return std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(std::min(size, bytes.size())));
}
}  // namespace

TEST(TasmCodec_Test, RealSourcesOfEveryVersionDecodeAndRoundTrip)
{
    const codecs::TasmCodec codec;
    for (const Sample& sample : kSamples)
    {
        const containers::TrdosFile file = Unwrap(sample.file);
        DecodeOptions options;
        options.catalog = file.Hints();
        const DecodeResult decoded = codec.Decode(file.data, options);
        ASSERT_TRUE(decoded.ok) << sample.file;
        EXPECT_EQ(decoded.document.format, "tasm");
        EXPECT_EQ(decoded.document.subversion, sample.version) << sample.file;
        EXPECT_EQ(decoded.subversions, std::vector<std::string>{sample.version}) << sample.file;
        EXPECT_EQ(decoded.document.Text() + "\n", ReadTestText(std::string("tasm/") + sample.file + ".txt")) << sample.file;
        EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, file.data) << sample.file << ": byte-exact";
    }
}

TEST(TasmCodec_Test, VersionFromTheStreamAlone)
{
    for (const Sample& sample : kSamples)
    {
        std::vector<std::string> consistent;
        const std::string version = codecs::TasmCodec::DetectVersion(Unwrap(sample.file).data, {}, &consistent);
        EXPECT_EQ(consistent, sample.fromStream) << sample.file;
        EXPECT_EQ(version, sample.fromStream.back()) << sample.file << ": the newest consistent version";
    }
}

TEST(TasmCodec_Test, CanonicalTokenizerReproducesTasmsOwnBytes)
{
    // The decoded text with every kept byte dropped: the tokenizer alone writes what TASM wrote
    const codecs::TasmCodec codec;
    for (const Sample& sample : kSamples)
    {
        const auto data = Unwrap(sample.file).data;
        DecodeOptions options;
        options.subversion = sample.version;
        const DecodeResult decoded = codec.Decode(data, options);
        SourceDocument plain = WithoutAttributes(decoded.document);
        plain.format = "tasm";
        plain.subversion = sample.version;
        const EncodeResult encoded = codec.Encode(plain, {});
        ASSERT_TRUE(encoded.ok) << sample.file;
        const auto expected = Stream(data);
        EXPECT_EQ(Prefix(encoded.bytes, expected.size()), expected) << sample.file;
    }
}

TEST(TasmCodec_Test, KeywordsAreTokenizedEverywhere)
{
    // TASM replaces a keyword word, in any case, wherever it stands: in a label field, a string, a comment; it shows
    // keywords in capitals
    const codecs::TasmCodec codec;
    EncodeOptions to40;
    to40.subversion = "4.0";
    const EncodeResult encoded = codec.Encode(SourceDocument::FromText("include FILE\n        defb \"(c)\" ;ld a,(hl)", "tasm"), to40);
    ASSERT_TRUE(encoded.ok);
    const std::vector<uint8_t> line1 = {5, 0xE5, 'F', 'I', 'L', 'E', 5};
    EXPECT_EQ(Prefix(encoded.bytes, line1.size()), line1) << "include at column 0";
    const std::vector<uint8_t> line2 = {0x0A, 8, 0x96, '"', '(', 0x89, ')', '"', ' ', ';', 0xB3, 0x80, ',', '(', 0xA5, ')'};
    EXPECT_TRUE(std::search(encoded.bytes.begin(), encoded.bytes.end(), line2.begin(), line2.end()) != encoded.bytes.end())
        << "c in the string, ld / a / hl in the comment";
}

TEST(TasmCodec_Test, EditedLineIsTokenizedAndTheRestKept)
{
    const codecs::TasmCodec codec;
    const auto data = Unwrap("000LOAD").data;
    DecodeResult decoded = codec.Decode(data, {});
    decoded.document.lines[1].text = "LOOP        DJNZ      LOOP ; again";
    const EncodeResult encoded = codec.Encode(decoded.document, {});
    ASSERT_TRUE(encoded.ok);
    const DecodeResult again = codec.Decode(encoded.bytes, {});
    ASSERT_TRUE(again.ok);
    EXPECT_EQ(again.document.Text(), decoded.document.Text());
    const std::vector<uint8_t>& body = again.document.lines[1].attrs.bytes;
    EXPECT_EQ(std::string(body.begin(), body.begin() + 4), "LOOP");
    EXPECT_EQ(body[4], 0x0A);
    EXPECT_EQ(body[5], 8);
    EXPECT_GE(body[6], 0x80) << "djnz is a token";
    EXPECT_EQ(Prefix(encoded.bytes, 2), Prefix(data, 2)) << "the first line is kept";
}

TEST(TasmCodec_Test, Tasm412CountsBlanksDirectly)
{
    const codecs::TasmCodec codec;
    const SourceDocument document = SourceDocument::FromText("        ORG     #6000\nSTART   LD      A,1\n        RET", "tasm");
    EncodeOptions to412;
    to412.subversion = "4.12";
    const EncodeResult encoded = codec.Encode(document, to412);
    ASSERT_TRUE(encoded.ok);
    EXPECT_EQ(encoded.bytes[1], 8) << "eight blanks: one byte #08";
    std::vector<std::string> consistent;
    EXPECT_EQ(codecs::TasmCodec::DetectVersion(encoded.bytes, {}, &consistent), "4.12");
    const DecodeResult decoded = codec.Decode(encoded.bytes, {});
    EXPECT_EQ(decoded.document.Text(), document.Text());
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
    const containers::TrdosFile file = Unwrap("000LOAD");
    EXPECT_EQ(codec.Detect(file.data, file.Hints()), 95);
    EXPECT_GE(codec.Detect(file.data, {}), 60) << "no catalog: the framing holds";
    EXPECT_EQ(codec.Detect(ReadTestData("sjasmplus/hello.asm"), {}), 0);
    CatalogHints asTasm3 = file.Hints();
    asTasm3.start = 39221;
    EXPECT_EQ(codecs::TasmCodec::DetectVersion(file.data, asTasm3), "3") << "the catalog's start comes first";
    const DetectResult detected = CodecRegistry::Builtin().Detect(file.data, file.Hints());
    ASSERT_NE(detected.chosen, nullptr) << detected.reason;
    EXPECT_EQ(detected.chosen->Info().id, "tasm");
}

TEST(TasmCodec_Test, ConversionBetweenVersions)
{
    // 4.0 -> 4.12 -> 4.0: the text survives, 4.12 counts blanks directly, back in 4.0 the bytes are TASM's own
    const codecs::TasmCodec codec;
    EncodeOptions to40, to412;
    to40.subversion = "4.0";
    to412.subversion = "4.12";
    for (const char* name : {"000LOAD", "CALLLOAD"})
    {
        const auto data = Unwrap(name).data;
        const DecodeResult original = codec.Decode(data, {});
        const EncodeResult as412 = codec.Encode(original.document, to412);
        ASSERT_TRUE(as412.ok) << name;
        const DecodeResult decoded412 = codec.Decode(as412.bytes, {});
        EXPECT_EQ(decoded412.document.subversion, "4.12");
        EXPECT_EQ(decoded412.document.Text(), original.document.Text());
        const EncodeResult back = codec.Encode(decoded412.document, to40);
        ASSERT_TRUE(back.ok);
        const auto expected = Stream(data);
        EXPECT_EQ(Prefix(back.bytes, expected.size()), expected) << name;
    }
}

TEST(TasmCodec_Test, KeywordMissingInTheTargetVersionIsReported)
{
    // DB (#ED) exists in TASM 4.0 only: written into TASM 3 it stays text, with a warning
    const codecs::TasmCodec codec;
    const DecodeResult decoded = codec.Decode(Unwrap("TABLES_L").data, {});
    ASSERT_EQ(decoded.document.subversion, "4.0");
    EncodeOptions to3;
    to3.subversion = "3";
    const EncodeResult as3 = codec.Encode(decoded.document, to3);
    ASSERT_TRUE(as3.ok);
    EXPECT_TRUE(std::any_of(as3.diagnostics.begin(), as3.diagnostics.end(),
                            [](const Diagnostic& d) { return d.message == "'DB' is not a keyword of TASM 3: written as text"; }));
    DecodeOptions as3Reading;
    as3Reading.subversion = "3";
    EXPECT_EQ(codec.Decode(as3.bytes, as3Reading).document.Text(), decoded.document.Text()) << "the text is unchanged";
}

TEST(TasmCodec_Test, Tasm20IsTextWithEditorTabs)
{
    // TASM 2.0 keeps plain text (CR LF), its editor turning a blank run that reaches a tab stop into TABs. T20SRC was
    // typed into TASM 2.0 in the emulator and saved (type C, start 38750)
    const codecs::TasmCodec codec;
    containers::TrdosFile file;
    std::string error;
    ASSERT_TRUE(containers::ReadHobeta(ReadTestData("tasm/T20SRC.$C"), file, error)) << error;
    EXPECT_EQ(codec.Detect(file.data, file.Hints()), 95);
    const DetectResult detected = CodecRegistry::Builtin().Detect(file.data, file.Hints());
    ASSERT_NE(detected.chosen, nullptr) << detected.reason;
    EXPECT_EQ(detected.chosen->Info().id, "tasm");

    DecodeOptions options;
    options.catalog = file.Hints();
    const DecodeResult decoded = codec.Decode(file.data, options);
    ASSERT_TRUE(decoded.ok);
    EXPECT_EQ(decoded.document.subversion, "2.0");
    EXPECT_EQ(decoded.document.Text() + "\n", ReadTestText("tasm/T20SRC.txt"));
    EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, file.data) << "byte-exact";

    // The editor's tab rule, from the bytes TASM 2.0 wrote
    SourceDocument plain = WithoutAttributes(decoded.document);
    plain.format = "tasm";
    plain.subversion = "2.0";
    EXPECT_EQ(codec.Encode(plain, {}).bytes, file.data) << "the canonical encoder writes what TASM 2.0 wrote";
    EncodeOptions to20;
    to20.subversion = "2.0";
    const EncodeResult one = codec.Encode(SourceDocument::FromText("1234567 X\nA                       B\nX  Y", "tasm"), to20);
    EXPECT_EQ(std::string(one.bytes.begin(), one.bytes.end()), "1234567\tX\r\nA\t\t\tB\r\nX  Y\r\n");

    // Into TASM 4.0: re-tokenized; TASM 4.0 then shows the keywords in capitals, the rest is unchanged
    EncodeOptions to40;
    to40.subversion = "4.0";
    const EncodeResult as40 = codec.Encode(decoded.document, to40);
    ASSERT_TRUE(as40.ok);
    DecodeOptions as40Reading;
    as40Reading.subversion = "4.0";
    auto upper = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return text;
    };
    const std::string as40Text = codec.Decode(as40.bytes, as40Reading).document.Text();
    EXPECT_EQ(upper(as40Text), upper(decoded.document.Text()));
    EXPECT_NE(as40Text.find("start   LD A,7      ; BORDER"), std::string::npos) << as40Text.substr(0, 80);
}
