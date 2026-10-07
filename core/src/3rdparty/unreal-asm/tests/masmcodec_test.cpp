// The MASM codec: MASM 1.1's own source (LS2, M1+, M2+ from MASM_SRC) and files typed and saved in the 1.0 demo,
// 1.1, 2.0 and 3.0 editors in unreal-ng decode to the text the editors show, round-trip byte-exact, and MASM's
// tokenizer reproduces every stored line; the version from the framing and the catalog (research-masm.md).

#include <gtest/gtest.h>

#include "codecs/masm/masmcodec.h"
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
    const char* version;
    size_t lines;
};

const Sample kSamples[] = {
    {"MASM_SRC__LS2", "1.1", 2009},       // MASM 1.1's own source
    {"MASM_SRC__M1", "1.1", 1493},
    {"MASM_SRC__M2", "1.1", 1717},
    {"typed-masm11__t1", "1.1", 34},      // every printable character around keywords
    {"typed-masm20__NONAME", "2.0", 9},   // 2.0's greedy tokenizer, #01-#1F blank runs
    {"typed-masm30__T3", "3.0", 9},       // 3.0's cursor-line header
    {"typed-masmdemo__DM", "1.0", 8},     // the 1.0 demo: its catalog length leaves the end marker out
};

containers::TrdosFile Unwrap(const std::string& name)
{
    containers::TrdosFile out;
    std::string error;
    EXPECT_TRUE(containers::ReadHobeta(ReadTestData("masm/" + name + ".$a"), out, error)) << name << ": " << error;
    return out;
}
}  // namespace

TEST(MasmCodec_Test, SourcesDecodeAndRoundTrip)
{
    const codecs::MasmCodec codec;
    for (const Sample& sample : kSamples)
    {
        const containers::TrdosFile file = Unwrap(sample.file);
        DecodeOptions options;
        options.catalog = file.Hints();
        const DecodeResult decoded = codec.Decode(file.data, options);
        ASSERT_TRUE(decoded.ok) << sample.file;
        EXPECT_EQ(decoded.document.subversion, sample.version) << sample.file;
        EXPECT_EQ(decoded.document.lines.size(), sample.lines) << sample.file;
        EXPECT_EQ(decoded.document.Text() + "\n", ReadTestText(std::string("masm/") + sample.file + ".txt")) << sample.file;
        EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, file.data) << sample.file << ": byte-exact";
    }
}

TEST(MasmCodec_Test, TokenizerReproducesTheStoredLines)
{
    const codecs::MasmCodec codec;
    for (const Sample& sample : kSamples)
    {
        const containers::TrdosFile file = Unwrap(sample.file);
        DecodeOptions options;
        options.catalog = file.Hints();
        const DecodeResult decoded = codec.Decode(file.data, options);
        size_t misses = 0;
        for (const SourceLine& line : decoded.document.lines)
        {
            std::vector<uint8_t> body;
            std::string error;
            ASSERT_TRUE(codecs::MasmCodec::EncodeBody(line.text, sample.version, body, error)) << sample.file << ": " << line.text << ": " << error;
            misses += body != line.attrs.bytes;
        }
        EXPECT_EQ(misses, 0u) << sample.file;
    }
}

TEST(MasmCodec_Test, LineRules)
{
    std::vector<uint8_t> body;
    std::string error;
    // 1.1: LD + blank is one keyword, 8 blanks #0A #08, registers are keywords; a keyword typed in lower case is text
    ASSERT_TRUE(codecs::MasmCodec::EncodeBody("        LD      A,(HL)", "1.1", body, error));
    EXPECT_EQ(body, (std::vector<uint8_t>{0x0A, 0x08, 0xAA, 0x0A, 0x05, 0x80, ',', '(', 0x91, ')'}));
    ASSERT_TRUE(codecs::MasmCodec::EncodeBody("        ld a", "1.1", body, error));
    EXPECT_EQ(body, (std::vector<uint8_t>{0x0A, 0x08, 'l', 'd', ' ', 'a'}));
    // 2.0 / 3.0: a blank run of 2-32 is one byte #01-#1F
    ASSERT_TRUE(codecs::MasmCodec::EncodeBody("        LD      A,(HL)", "3.0", body, error));
    EXPECT_EQ(body, (std::vector<uint8_t>{0x07, 0xAA, 0x04, 0x80, ',', '(', 0x91, ')'}));
}

TEST(MasmCodec_Test, Detection)
{
    const codecs::MasmCodec codec;
    for (const Sample& sample : kSamples)
    {
        const containers::TrdosFile file = Unwrap(sample.file);
        const DetectResult detected = CodecRegistry::Builtin().Detect(file.data, file.Hints());
        ASSERT_NE(detected.chosen, nullptr) << sample.file << ": " << detected.reason;
        EXPECT_EQ(detected.chosen->Info().id, "masm") << sample.file;
    }
    // TASM 3 shares the 1.x framing but saves type A (and writes an empty line as #00 #00)
    EXPECT_LT(codec.Detect(ReadTestData("tasm/000LOAD.$A"), Unwrap("MASM_SRC__M1").Hints()), 96);
    containers::TrdosFile tasm;
    std::string error;
    ASSERT_TRUE(containers::ReadHobeta(ReadTestData("tasm/000LOAD.$A"), tasm, error));
    EXPECT_LT(codec.Detect(tasm.data, tasm.Hints()), 20);
}
