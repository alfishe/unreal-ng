// The ZX-ASM codec, every version: 2.4-2.6 (text with blank runs), 3.0-3.10, Lite 1.07, 3.15-4.20. Real sources of
// each era decode to the expected text and round-trip byte-exact; the editor's tokenizer rules reproduce the stored
// bytes; versions from the catalog and from the bytes; conversion between versions.

#include <gtest/gtest.h>

#include <algorithm>

#include "codecs/zxasm/zxasmcodec.h"
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
    const char* file;                        ///< hobeta in testdata/zxasm
    std::vector<std::string> versions;       ///< consistent with, catalog included
    std::vector<std::string> fromStream;     ///< consistent with, bytes alone
    size_t canonicalMisses;
};

// research-zxasm.md §6
const Sample kSamples[] = {
    {"ZXASM2_4__a2.4_p.$C", {"2"}, {"2"}, 0},                                  // 2.4: C at #A1xx, no tokens
    {"ZASM2_6__a2.6_p.$C", {"2"}, {"2"}, 0},                                   // 2.6: C at #2020, ";*" editor line
    {"IG_10D1__ACEpd55e.$C", {"3.0"}, {"3.0", "lite", "3.15"}, 0},             // 3.0: C at 35151 (Info Guide #10)
    {"EPV_11__lx-800.$z", {"3.0"}, {"3.0", "lite", "3.15"}, 0},                // 3.01: type z, "as"
    {"ZASM_310__prn_des.$a", {"3.0", "lite", "3.15"}, {"3.0", "lite", "3.15"}, 0},   // 3.10: type a, "sm"
    {"Z33_F9__AboutMe.$a", {"3.0", "lite", "3.15"}, {"2", "3.0", "lite", "3.15"}, 0},
    {"C33_F9__ddoc2_p.$a", {"lite", "3.15"}, {"lite", "3.15"}, 0},              // uses PROJECT..DBW (#C6-#C9)
    {"ZASM315__fcnv1.$a", {"3.15"}, {"3.15"}, 0},                               // uses REPL..CHD (#CA-#CC)
    {"Z4_20__ovlib.$a", {"3.15"}, {"3.15"}, 1},                                 // 4.20
};

containers::TrdosFile Unwrap(const std::string& name)
{
    containers::TrdosFile out;
    std::string error;
    EXPECT_TRUE(containers::ReadHobeta(ReadTestData("zxasm/" + name), out, error)) << name << ": " << error;
    return out;
}

std::string Expected(const std::string& name)
{
    return ReadTestText("zxasm/" + name.substr(0, name.rfind('.')) + ".txt");
}
}  // namespace

TEST(ZxasmCodec_Test, RealSourcesOfEveryVersionDecodeAndRoundTrip)
{
    const codecs::ZxasmCodec codec;
    for (const Sample& sample : kSamples)
    {
        const containers::TrdosFile file = Unwrap(sample.file);
        DecodeOptions options;
        options.catalog = file.Hints();
        const DecodeResult decoded = codec.Decode(file.data, options);
        ASSERT_TRUE(decoded.ok) << sample.file;
        EXPECT_EQ(decoded.subversions, sample.versions) << sample.file;
        EXPECT_EQ(decoded.document.subversion, sample.versions.back()) << sample.file;
        EXPECT_EQ(decoded.document.Text() + "\n", Expected(sample.file)) << sample.file;
        EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, file.data) << sample.file << ": byte-exact";

        std::vector<std::string> fromStream;
        codecs::ZxasmCodec::DetectVersion(file.data, {}, &fromStream);
        EXPECT_EQ(fromStream, sample.fromStream) << sample.file << ": the bytes alone";
    }
}

TEST(ZxasmCodec_Test, EditorRulesReproduceTheStoredBytes)
{
    const codecs::ZxasmCodec codec;
    for (const Sample& sample : kSamples)
    {
        const containers::TrdosFile file = Unwrap(sample.file);
        DecodeOptions options;
        options.catalog = file.Hints();
        const DecodeResult decoded = codec.Decode(file.data, options);
        size_t misses = 0;
        for (const SourceLine& line : decoded.document.lines)
        {
            std::vector<uint8_t> bytes;
            std::string error;
            ASSERT_TRUE(codecs::ZxasmCodec::EncodeLine(line.text, decoded.document.subversion, bytes, error)) << error;
            misses += bytes != line.attrs.bytes;
        }
        EXPECT_EQ(misses, sample.canonicalMisses) << sample.file;
    }
}

TEST(ZxasmCodec_Test, LineRules)
{
    std::vector<uint8_t> out;
    std::string error;
    auto encode = [&](const std::string& text, const std::string& version) {
        EXPECT_TRUE(codecs::ZxasmCodec::EncodeLine(text, version, out, error)) << error;
        return out;
    };
    // Keywords: p = 2 + capitals + 2 * one-blank-after; #20 + index; blank runs #06 #80+n; a label stays text
    EXPECT_EQ(encode("Label  ld (hl),a:inc hl", "3.15"),
              (std::vector<uint8_t>{'L', 'a', 'b', 'e', 'l', 0x06, 0x82, 0x04, 0x20, 0x02, 0x7D, ',', 0x02, 0x7E, ':', 0x04, 0x2F, 0x02, 0x8C}));
    EXPECT_EQ(encode("      DBW #07,ManQuit", "3.15"),
              (std::vector<uint8_t>{0x06, 0x86, 0x05, 0xC9, '#', '0', '7', ',', 'M', 'a', 'n', 'Q', 'u', 'i', 't'}));
    // Lite has no REPL..CHD: CHD stays text there
    EXPECT_EQ(encode(" CHD \"f\"", "lite"), (std::vector<uint8_t>{' ', 'C', 'H', 'D', ' ', '"', 'f', '"'}));
    // After "(" no keyword starts ("(a..z)"); BC' keeps its apostrophe; a string keeps its first blank literal
    EXPECT_EQ(encode(";(a..z) BC' \"  x\"", "3.15"),
              (std::vector<uint8_t>{';', '(', 'a', '.', '.', 'z', ')', ' ', 0x03, 0x8A, '\'', ' ', '"', ' ', ' ', 'x', '"'}));
    // 2.x: no keywords, every blank run compressed (strings too)
    EXPECT_EQ(encode("        ld a,\"  x\"", "2"), (std::vector<uint8_t>{0x06, 0x88, 'l', 'd', ' ', 'a', ',', '"', 0x06, 0x82, 'x', '"'}));
}

TEST(ZxasmCodec_Test, ConversionWarnsAboutKeywordsTheTargetLacks)
{
    const codecs::ZxasmCodec codec;
    const containers::TrdosFile file = Unwrap("ZASM315__fcnv1.$a");
    DecodeOptions options;
    options.catalog = file.Hints();
    const DecodeResult decoded = codec.Decode(file.data, options);
    EncodeOptions to30;
    to30.subversion = "3.0";
    const EncodeResult encoded = codec.Encode(decoded.document, to30);
    ASSERT_TRUE(encoded.ok);
    EXPECT_TRUE(std::any_of(encoded.diagnostics.begin(), encoded.diagnostics.end(),
                            [](const Diagnostic& d) { return d.severity == Severity::Warning && d.message.find("ZX-ASM 3.0") != std::string::npos; }));
    DecodeOptions as30;
    as30.subversion = "3.0";
    EXPECT_EQ(codec.Decode(encoded.bytes, as30).document.Text(), decoded.document.Text()) << "the text is unchanged";
}

TEST(ZxasmCodec_Test, Detection)
{
    const codecs::ZxasmCodec codec;
    for (const Sample& sample : kSamples)
    {
        const containers::TrdosFile file = Unwrap(sample.file);
        EXPECT_EQ(codec.Detect(file.data, file.Hints()), 95) << sample.file;
        const DetectResult detected = CodecRegistry::Builtin().Detect(file.data, file.Hints());
        ASSERT_NE(detected.chosen, nullptr) << sample.file << ": " << detected.reason;
        EXPECT_EQ(detected.chosen->Info().id, "zxasm") << sample.file;
    }
    EXPECT_LT(codec.Detect(ReadTestData("text/source-cp866-cr.asm"), {}), 60) << "plain CR text is the text codec's";
    EXPECT_EQ(codec.Detect(ReadTestData("tasm/000LOAD.$A"), {}), 0);
}
