// The STORM codec: real sources (STORM's own, demos, magazines) decode to the expected text and round-trip
// byte-exact; STORM's rules reproduce the stored lines (implied commands, number forms, packed labels, IX / IY
// offsets, sub-expressions); the version from the catalog's start.

#include <gtest/gtest.h>

#include <algorithm>

#include "codecs/storm/stormcodec.h"
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
    size_t lines;
    size_t canonicalMisses;   ///< lines STORM wrote in a form its own rules no longer produce
};

// research-storm.md §6
const Sample kSamples[] = {
    {"EMULTEST__EMUL", 86, 0},      // starts with an empty line
    {"ZX-FOR72__PLASM", 406, 0},    // Dark / X-Trade, comments with blank runs
    {"DEJAVU4__LDISCROL", 93, 0},
    {"GC131IGS__HMEM", 46, 0},
    {"STORM1_3__MAIN", 1038, 0},    // STORM's own source: sub-expressions, postfix operators
    {"STORM1_3__DPC", 1517, 5},     // five ":" separators stored with bit 6 set (#6A, #6C): same text, other bytes
};

containers::TrdosFile Unwrap(const std::string& name)
{
    containers::TrdosFile out;
    std::string error;
    EXPECT_TRUE(containers::ReadHobeta(ReadTestData("storm/" + name + ".$C"), out, error)) << name << ": " << error;
    return out;
}
}  // namespace

TEST(StormCodec_Test, RealSourcesDecodeAndRoundTrip)
{
    const codecs::StormCodec codec;
    for (const Sample& sample : kSamples)
    {
        const containers::TrdosFile file = Unwrap(sample.file);
        DecodeOptions options;
        options.catalog = file.Hints();
        const DecodeResult decoded = codec.Decode(file.data, options);
        ASSERT_TRUE(decoded.ok) << sample.file;
        EXPECT_TRUE(decoded.diagnostics.empty()) << sample.file << ": " << (decoded.diagnostics.empty() ? "" : decoded.diagnostics[0].message);
        EXPECT_EQ(decoded.document.subversion, "1.3") << "start #C00B";
        EXPECT_EQ(decoded.document.lines.size(), sample.lines) << sample.file;
        EXPECT_EQ(decoded.document.Text() + "\n", ReadTestText(std::string("storm/") + sample.file + ".txt")) << sample.file;
        EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, file.data) << sample.file << ": byte-exact";
    }
}

TEST(StormCodec_Test, StormsRulesReproduceTheStoredLines)
{
    const codecs::StormCodec codec;
    for (const Sample& sample : kSamples)
    {
        const DecodeResult decoded = codec.Decode(Unwrap(sample.file).data, {});
        size_t misses = 0;
        for (const SourceLine& line : decoded.document.lines)
        {
            std::vector<uint8_t> body;
            std::string error;
            ASSERT_TRUE(codecs::StormCodec::EncodeLine(line.text, body, error)) << sample.file << ": " << line.text << ": " << error;
            body.push_back(static_cast<uint8_t>(body.size()));
            misses += body != line.attrs.bytes;
        }
        EXPECT_EQ(misses, sample.canonicalMisses) << sample.file;
    }
}

TEST(StormCodec_Test, LineRules)
{
    std::vector<uint8_t> body;
    std::string error;
    auto encode = [&](const std::string& text) {
        EXPECT_TRUE(codecs::StormCodec::EncodeLine(text, body, error)) << text << ": " << error;
        return body;
    };
    // LD implied by its first operand; hex word with a descriptor; ":" separator
    EXPECT_EQ(encode("        LD HL,#5FFF:LD (#5CB2),HL"), (std::vector<uint8_t>{0x3E, 0x8D, 0xFF, 0x5F, 0x2A, 0xAD, 0xB2, 0x5C, 0x3E}));
    // A label definition (bit 6 on its second byte), CALL with a label, RET NZ
    EXPECT_EQ(encode("DLNA    CALL DLN:RET NZ"), (std::vector<uint8_t>{0xC3, 0x57, 0x19, 0x8C, 0x51, 0xC3, 0x17, 0x99, 0x2A, 0x52, 0x78}));
    // JR NZ implied, JR C explicit with the condition C (#7B), CP C with the register C (#31)
    EXPECT_EQ(encode("        JR NZ,$-3:JR C,$+2:CP C"), (std::vector<uint8_t>{0x78, 0xF0, 0x2A, 0x4E, 0x7B, 0xF5, 0x2A, 0x5C, 0x31}));
    // (IX+d) short form, a single digit, unary minus
    EXPECT_EQ(encode("        LD A,(IX+13),B,5,DE,-33"), (std::vector<uint8_t>{0x37, 0xB2, 0x0D, 0x30, 0xE2, 0x3D, 0x9A, 0x21}));
    // A comment: every blank run is one byte
    EXPECT_EQ(encode(";in: a - x"), (std::vector<uint8_t>{0x2F, 'i', 'n', ':', 0x01, 'a', 0x01, '-', 0x01, 'x', 0x00}));
    for (const std::string text : {"        LD HL,#5FFF:LD (#5CB2),HL", "DLNA    CALL DLN:RET NZ", "        JR NZ,$-3:JR C,$+2:CP C",
                                   "        LD A,(IX+13),B,5,DE,-33", ";in: a - x", "TBABUF  DS (HGT+1)*2"})
    {
        encode(text);
        EXPECT_EQ(codecs::StormCodec::DecodeLine(body), text);
    }
    // Labels start with a capital letter or _
    EXPECT_FALSE(codecs::StormCodec::EncodeLine("label   NOP", body, error));
}

TEST(StormCodec_Test, FreshFileAndVersion)
{
    const codecs::StormCodec codec;
    const SourceDocument document = SourceDocument::FromText("        ORG #8000\nSTART   LD A,7\n        OUT (#FE),A\n        RET", "storm");
    const EncodeResult encoded = codec.Encode(document, {});
    ASSERT_TRUE(encoded.ok);
    const DecodeResult decoded = codec.Decode(encoded.bytes, {});
    EXPECT_EQ(decoded.document.Text(), document.Text());
    EXPECT_EQ(decoded.subversions, (std::vector<std::string>{"1.0", "1.3"})) << "no catalog: the bytes are the same in both";
    DecodeOptions beta;
    beta.catalog.type = 'C';
    beta.catalog.start = 0xC003;
    EXPECT_EQ(codec.Decode(encoded.bytes, beta).document.subversion, "1.0");
}

TEST(StormCodec_Test, Detection)
{
    const codecs::StormCodec codec;
    for (const Sample& sample : kSamples)
    {
        const containers::TrdosFile file = Unwrap(sample.file);
        EXPECT_EQ(codec.Detect(file.data, file.Hints()), 95) << sample.file;
        const DetectResult detected = CodecRegistry::Builtin().Detect(file.data, file.Hints());
        ASSERT_NE(detected.chosen, nullptr) << sample.file << ": " << detected.reason;
        EXPECT_EQ(detected.chosen->Info().id, "storm") << sample.file;
    }
    EXPECT_LT(codec.Detect(ReadTestData("tasm/000LOAD.$A"), {}), 60);
    EXPECT_LT(codec.Detect(ReadTestData("text/source-cp866-cr.asm"), {}), 60);
}
