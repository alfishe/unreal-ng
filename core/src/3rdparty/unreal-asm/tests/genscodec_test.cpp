// The GENS codec: five real sources (TR-DOS GENS4 ports) and files typed / loaded and saved in GENS3 and GENS4 in
// unreal-ng decode to the editor's listing, round-trip byte-exact, and the editor's blank compression reproduces every
// typed line; GENS1's end marker selects version 1 (research-gens.md).

#include <gtest/gtest.h>

#include <cstdio>

#include "codecs/gens/genscodec.h"
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
    const char* file;      ///< in testdata/gens/
    const char* stem;      ///< the expected listing <stem>.txt
    size_t lines;
    bool real;             ///< a real or typed source: the compression reproduces every line
};

const Sample kSamples[] = {
    {"WINDOW__WINDOW.$C", "WINDOW__WINDOW", 1004, true},          // window library (Gens-4D, 1993)
    {"HISOFT-C__64-A95.$C", "HISOFT-C__64-A95", 368, true},       // HiSoft C's 64-column driver
    {"ISC11VRG__ISCOP.C.$C", "ISC11VRG__ISCOP.C", 165, true},
    {"ZX_NET__ZX_NET1.$A", "ZX_NET__ZX_NET1", 116, true},         // saved as type A
    {"PF212__BOOT.A.$C", "PF212__BOOT.A", 42, true},
    {"typed-gens3-P-PROBE.bin", "typed-gens3-P-PROBE", 9, true},  // typed into GENS3, saved with P (the data block)
    {"typed-gens4-P-PROBE4.bin", "typed-gens4-P-PROBE4", 9, true},
    {"crafted-gens3-P-PROBE1.bin", "crafted-gens3-P-PROBE1", 8, false},   // many TABs, #7F-#FF, loaded and saved by GENS3
};

struct Input
{
    std::vector<uint8_t> data;
    CatalogHints hints;
};

Input Load(const Sample& sample)
{
    Input in;
    const std::vector<uint8_t> bytes = ReadTestData(std::string("gens/") + sample.file);
    const std::string name = sample.file;
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".bin") == 0)
    {
        in.data = bytes;
        return in;
    }
    containers::TrdosFile file;
    std::string error;
    EXPECT_TRUE(containers::ReadHobeta(bytes, file, error)) << name << ": " << error;
    in.data = file.data;
    in.hints = file.Hints();
    return in;
}

std::string Listing(const SourceDocument& document)
{
    std::string out;
    char number[8];
    for (const SourceLine& line : document.lines)
    {
        std::snprintf(number, sizeof(number), "%5d ", line.number);
        out += number + line.text + "\n";
    }
    return out;
}
}  // namespace

TEST(GensCodec_Test, SourcesDecodeAndRoundTrip)
{
    const codecs::GensCodec codec;
    for (const Sample& sample : kSamples)
    {
        const Input in = Load(sample);
        DecodeOptions options;
        options.catalog = in.hints;
        const DecodeResult decoded = codec.Decode(in.data, options);
        ASSERT_TRUE(decoded.ok) << sample.file;
        EXPECT_EQ(decoded.document.subversion, "2") << sample.file;
        EXPECT_EQ(decoded.document.lines.size(), sample.lines) << sample.file;
        EXPECT_EQ(Listing(decoded.document), ReadTestText(std::string("gens/") + sample.stem + ".txt")) << sample.file;
        EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, in.data) << sample.file << ": byte-exact";
    }
}

TEST(GensCodec_Test, CompressionReproducesTheStoredLines)
{
    const codecs::GensCodec codec;
    for (const Sample& sample : kSamples)
    {
        if (!sample.real)
            continue;
        const DecodeResult decoded = codec.Decode(Load(sample).data, {});
        for (const SourceLine& line : decoded.document.lines)
            EXPECT_TRUE(line.attrs.Empty()) << sample.file << ": " << line.text;   // no stored bytes kept
        // The text alone (no attributes) encodes to the same bytes
        SourceDocument plain = decoded.document;
        for (SourceLine& line : plain.lines)
            line.attrs = {};
        plain.attrs = {};
        EXPECT_EQ(codec.Encode(plain, {}).bytes, Load(sample).data) << sample.file;
    }
}

TEST(GensCodec_Test, LineRules)
{
    using codecs::GensCodec;
    auto bytes = [](const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); };
    // the first two blank runs become TABs, the third stays; trailing blanks go; ';' / '*' lines are kept
    EXPECT_EQ(GensCodec::Compress(bytes("LABEL  LD   A,(HL)   ; x")), bytes("LABEL\tLD\tA,(HL)   ; x"));
    EXPECT_EQ(GensCodec::Compress(bytes("A1 B1 C1 D1")), bytes("A1\tB1\tC1 D1"));
    EXPECT_EQ(GensCodec::Compress(bytes("   NOP   ")), bytes("\tNOP"));
    EXPECT_EQ(GensCodec::Compress(bytes(";FULL   COMMENT")), bytes(";FULL   COMMENT"));
    EXPECT_EQ(GensCodec::Compress(bytes("*D+  ")), bytes("*D+  "));
    // TAB stops 7, 12, 21, 25 in each 26-column row, then column 7 of the next row
    EXPECT_EQ(GensCodec::Expand(bytes("A\tB\tC\tD\tE\tF")), "A      B    C        D   E       F");
    EXPECT_EQ(GensCodec::Expand(bytes("\x60\x7F\x80")), "\xC2\xA3\xC2\xA9\xC2\xA0");   // £ © and the first block graphic
}

TEST(GensCodec_Test, Gens1EndMarkerAndRenumbering)
{
    const codecs::GensCodec codec;
    // GENS1: blanks as typed, #00 #00 after the last line
    const std::vector<uint8_t> gens1{10, 0, 'N', 'O', 'P', ' ', ' ', ';', 'x', 0x0D, 20, 0, ' ', 'R', 'E', 'T', 0x0D, 0, 0};
    const DecodeResult decoded = codec.Decode(gens1, {});
    ASSERT_TRUE(decoded.ok);
    EXPECT_EQ(decoded.document.subversion, "1");
    EXPECT_EQ(decoded.document.Text(), "NOP  ;x\n RET");
    EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, gens1);
    // to GENS2-4: compressed, no marker
    EncodeOptions to2;
    to2.subversion = "2";
    EXPECT_EQ(codec.Encode(decoded.document, to2).bytes,
              (std::vector<uint8_t>{10, 0, 'N', 'O', 'P', 0x09, ';', 'x', 0x0D, 20, 0, 0x09, 'R', 'E', 'T', 0x0D}));
    // a document from another format is numbered 10, 20, ...
    const EncodeResult fromText = codec.Encode(SourceDocument::FromText("        ORG  #8000\nL1      JR   L1"), {});
    EXPECT_TRUE(fromText.ok);
    EXPECT_EQ(fromText.bytes, (std::vector<uint8_t>{10, 0, 0x09, 'O', 'R', 'G', 0x09, '#', '8', '0', '0', '0', 0x0D,
                                                    20, 0, 'L', '1', 0x09, 'J', 'R', 0x09, 'L', '1', 0x0D}));
    // numbers another numbered format carried (a ZEUS source) are kept
    SourceDocument numbered = SourceDocument::FromText(" NOP\n RET");
    numbered.lines[0].number = 5;
    numbered.lines[1].number = 7;
    EXPECT_EQ(codec.Encode(numbered, {}).bytes, (std::vector<uint8_t>{5, 0, 0x09, 'N', 'O', 'P', 0x0D, 7, 0, 0x09, 'R', 'E', 'T', 0x0D}));
}

TEST(GensCodec_Test, Detection)
{
    const codecs::GensCodec codec;
    for (const Sample& sample : kSamples)
    {
        const Input in = Load(sample);
        const DetectResult detected = CodecRegistry::Builtin().Detect(in.data, in.hints);
        ASSERT_NE(detected.chosen, nullptr) << sample.file << ": " << detected.reason;
        EXPECT_EQ(detected.chosen->Info().id, "gens") << sample.file;
    }
    // a BASIC program (big-endian numbers, a length word) is no GENS source
    const std::vector<uint8_t> basic{0, 10, 5, 0, 0xF5, '"', 'A', '"', 0x0D};
    EXPECT_EQ(codec.Detect(basic, {}), 0);
}
