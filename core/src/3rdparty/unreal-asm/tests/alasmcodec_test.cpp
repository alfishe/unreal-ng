// The ALASM codec (versions 3.8 to 5.09): real sources from every era decode to the expected text, round-trip
// byte-exact, report the versions they are consistent with, and the canonical tokenizer (cnv2str of ALASM 5.09)
// reproduces the editor's bytes; conversion between versions; fresh files.

#include <gtest/gtest.h>

#include <algorithm>

#include "codecs/alasm/alasmcodec.h"
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
    std::vector<std::string> versions;   ///< what the file is consistent with
    size_t canonicalMisses;              ///< lines the canonical tokenizer writes differently (older editors, text imports)
};

const std::vector<std::string> kAll = {"3.8", "4.2", "4.42", "4.5", "4.44", "5.07"};

// research-alasm.md §5 explains each range and each miss
const Sample kSamples[] = {
    {"ZADACHA", kAll, 0},                                  // from the ALASM 3.8 disk; nothing version-specific
    {"SCR4MAKE", kAll, 1},                                 // a text import: trailing blank run kept
    {"128KDRV", {"4.2", "4.42", "4.5", "4.44", "5.07"}, 0},  // uses ELSE / DB-family codes 3.8 lacks
    {"fibo", {"4.42", "4.5", "4.44", "5.07"}, 0},
    {"SNAKE", {"4.5", "4.44", "5.07"}, 0},
    {"2Kolonki", {"4.44", "5.07"}, 0},
    {"RECPIC", {"5.07"}, 0},
    {"BUILD+", {"5.07"}, 2},                               // DISPLAY /D: an older editor tokenized the switch letter
    {"AL442nfo", {"4.2", "4.42", "4.5"}, 1},               // DEFM "string"
    {"AL444nfo", {"4.5"}, 1},                              // DD left as text: typed in a version without DD
};

containers::TrdosFile Unwrap(const std::string& name)
{
    containers::TrdosFile out;
    std::string error;
    EXPECT_TRUE(containers::ReadHobeta(ReadTestData("alasm/" + name + ".$H"), out, error)) << name << ": " << error;
    return out;
}
}  // namespace

TEST(AlasmCodec_Test, RealSourcesDecodeRoundTripAndReportTheirVersions)
{
    const codecs::AlasmCodec codec;
    for (const Sample& sample : kSamples)
    {
        const containers::TrdosFile file = Unwrap(sample.file);
        DecodeOptions options;
        options.catalog = file.Hints();
        const DecodeResult decoded = codec.Decode(file.data, options);
        ASSERT_TRUE(decoded.ok) << sample.file;
        EXPECT_EQ(decoded.document.format, "alasm");
        EXPECT_EQ(decoded.subversions, sample.versions) << sample.file;
        EXPECT_EQ(decoded.document.subversion, sample.versions.back()) << sample.file << ": the newest consistent version";
        EXPECT_EQ(decoded.document.Text() + "\n", ReadTestText(std::string("alasm/") + sample.file + ".txt")) << sample.file;
        EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, file.data) << sample.file << ": byte-exact";
    }
}

TEST(AlasmCodec_Test, CanonicalTokenizerReproducesTheEditorsBytes)
{
    const codecs::AlasmCodec codec;
    for (const Sample& sample : kSamples)
    {
        const auto data = Unwrap(sample.file).data;
        const DecodeResult decoded = codec.Decode(data, {});
        const codecs::alasm::Version& version = *codecs::alasm::FindVersion(decoded.document.subversion);
        size_t misses = 0;
        for (const SourceLine& line : decoded.document.lines)
        {
            std::vector<uint8_t> record;
            std::string error;
            ASSERT_TRUE(codecs::AlasmCodec::EncodeLine(line.text, version, record, error)) << error;
            misses += record != line.attrs.bytes;
        }
        EXPECT_EQ(misses, sample.canonicalMisses) << sample.file;
    }
}

TEST(AlasmCodec_Test, LineEncodingRules)
{
    const codecs::alasm::Version& v5 = *codecs::alasm::FindVersion("5.07");
    std::vector<uint8_t> record;
    std::string error;
    auto encode = [&](const std::string& text) {
        EXPECT_TRUE(codecs::AlasmCodec::EncodeLine(text, v5, record, error)) << error;
        return record;
    };
    // A mnemonic at the keyword column needs no blanks (the decoder pads to column 8); it takes one blank after it
    EXPECT_EQ(encode("        LD A,1"), (std::vector<uint8_t>{5, 0xD4, 0xF5, ',', '1'}));
    // A label, then the mnemonic at column 8: the blank run is dropped as well
    EXPECT_EQ(encode("LOOP    DJNZ LOOP"), (std::vector<uint8_t>{10, 'L', 'O', 'O', 'P', 0x8B, 'L', 'O', 'O', 'P'}));
    // Left of column 8 without blanks before: #FF keeps a keyword there, every keyword (A is at column 3)
    EXPECT_EQ(encode("LD A,1"), (std::vector<uint8_t>{7, 0xFF, 0xD4, 0xFF, 0xF5, ',', '1'}));
    // Lower case is not a keyword; comments and strings are literal, Russian in CP866
    EXPECT_EQ(encode("        ld a ;тест"), (std::vector<uint8_t>{12, 8, 'l', 'd', 1, 'a', 1, ';', 0xE2, 0xA5, 0xE1, 0xE2}));
    // DD: its hex operand is not split into register keywords
    EXPECT_EQ(encode("        DD BC"), (std::vector<uint8_t>{4, 0x96, 'B', 'C'}));
    for (const std::string text : {"        LD A,1", "LOOP    DJNZ LOOP", "LD A,1", "        ld a ;тест", "        DD BC"})
    {
        encode(text);
        EXPECT_EQ(codecs::AlasmCodec::DecodeLine(record, v5), text);
    }
    // Walking back from a line's end must reach its length byte first: a trailing one-blank run (#01) would stop the
    // walk one byte early, so the editor appends #FF
    EXPECT_EQ(encode("LABEL "), (std::vector<uint8_t>{8, 'L', 'A', 'B', 'E', 'L', 1, 0xFF}));
    EXPECT_EQ(codecs::AlasmCodec::DecodeLine(record, v5), "LABEL ");
}

TEST(AlasmCodec_Test, ReadingAsAnotherVersionKeepsTheBytes)
{
    // fibo uses IF0 (#D3); ALASM 3.8 has no keyword there: shown as U+F7D3, still written back exactly
    const codecs::AlasmCodec codec;
    const auto data = Unwrap("fibo").data;
    DecodeOptions as38;
    as38.subversion = "3.8";
    const DecodeResult decoded = codec.Decode(data, as38);
    EXPECT_EQ(decoded.document.subversion, "3.8");
    EXPECT_NE(decoded.document.Text().find("\xEF\x9F\x93"), std::string::npos) << "U+F7D3";
    EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, data);
}

TEST(AlasmCodec_Test, ConversionToAnotherVersionWarnsAboutLostKeywords)
{
    // AL442nfo spells #96 DEFM (ALASM 4.x); ALASM 5.07 has no DEFM: the word stays text and a warning says so
    const codecs::AlasmCodec codec;
    const DecodeResult decoded = codec.Decode(Unwrap("AL442nfo").data, {});
    ASSERT_EQ(decoded.document.subversion, "4.5");
    EncodeOptions to5;
    to5.subversion = "5.07";
    const EncodeResult encoded = codec.Encode(decoded.document, to5);
    ASSERT_TRUE(encoded.ok);
    EXPECT_TRUE(std::any_of(encoded.diagnostics.begin(), encoded.diagnostics.end(),
                            [](const Diagnostic& d) { return d.severity == Severity::Warning && d.message == "'DEFM' is not a keyword of ALASM 5.07: written as text"; }));
    DecodeOptions as5;
    as5.subversion = "5.07";
    EXPECT_EQ(codec.Decode(encoded.bytes, as5).document.Text(), decoded.document.Text()) << "the text is unchanged";
}

TEST(AlasmCodec_Test, FreshFileFromText)
{
    const codecs::AlasmCodec codec;
    SourceDocument document = SourceDocument::FromText("        ORG #6000\nSTART   LD A,7\n        OUT (#FE),A\n        RET", "alasm");
    document.name = "BORDER";
    const EncodeResult encoded = codec.Encode(document, {});
    ASSERT_TRUE(encoded.ok);
    ASSERT_GE(encoded.bytes.size(), codecs::AlasmCodec::kHeaderSize);
    EXPECT_EQ(std::string(encoded.bytes.begin(), encoded.bytes.begin() + 9), "BORDER  H");
    EXPECT_EQ(encoded.bytes[0x21] | (encoded.bytes[0x22] << 8), static_cast<int>(encoded.bytes.size() - codecs::AlasmCodec::kHeaderSize));
    EXPECT_EQ(codec.Detect(encoded.bytes, {}), 95);
    const DecodeResult decoded = codec.Decode(encoded.bytes, {});
    ASSERT_TRUE(decoded.ok);
    EXPECT_EQ(decoded.document.Text(), document.Text());
    EXPECT_EQ(decoded.document.name, "BORDER");
}

TEST(AlasmCodec_Test, Detection)
{
    const codecs::AlasmCodec codec;
    const containers::TrdosFile file = Unwrap("fibo");
    EXPECT_EQ(codec.Detect(file.data, file.Hints()), 95);
    EXPECT_EQ(codec.Detect(ReadTestData("tasm3/000LOAD.$A"), {}), 0);
    EXPECT_EQ(codec.Detect(ReadTestData("sjasmplus/hello.asm"), {}), 0);
    const DetectResult detected = CodecRegistry::Builtin().Detect(file.data, file.Hints());
    ASSERT_NE(detected.chosen, nullptr) << detected.reason;
    EXPECT_EQ(detected.chosen->Info().id, "alasm");
    EXPECT_EQ(codec.Info().subversions.size(), kAll.size());
}
