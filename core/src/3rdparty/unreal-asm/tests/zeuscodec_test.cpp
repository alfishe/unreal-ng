// The ZEUS codec: the ADS 2.0 sources and ZEUS v7.E's help (PHT ZEUS), five ZXDB "Zeus Routines" and a probe typed
// into ZEUS 1983 in unreal-ng decode to ZEUS's listing, round-trip byte-exact, and the version's tokenizer reproduces
// every stored line; the version from the token range and the tokenizer (research-zeus.md).

#include <gtest/gtest.h>

#include <cstdio>

#include "codecs/zeus/zeuscodec.h"
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
    const char* file;      ///< in testdata/zeus/
    const char* stem;      ///< the expected listing <stem>.txt
    const char* version;
    size_t lines;
};

const Sample kSamples[] = {
    {"ADS20SRC__CC0.bin", "ADS20SRC__CC0", "pht", 852},   // ADS 2.0, raw sectors: bytes after #FF #FF
    {"ADS20SRC__CC1.bin", "ADS20SRC__CC1", "pht", 854},
    {"ADS20SRC__CC2.bin", "ADS20SRC__CC2", "pht", 812},
    {"ADS20SRC__MAKE_ADS.bin", "ADS20SRC__MAKE_ADS", "pht", 4},
    {"ZEUS72ZK__ZEUShelp.$Z", "ZEUS72ZK__ZEUShelp", "pht", 491},   // ZEUS v7.E's help, type Z
    {"ZeusRoutines__ZeusGlitter.bin", "ZeusRoutines__ZeusGlitter", "1983", 140},   // tape data blocks
    {"ZeusRoutines__ZeusMultiplot.bin", "ZeusRoutines__ZeusMultiplot", "1983", 169},
    {"ZeusRoutines__ZeusPrint.bin", "ZeusRoutines__ZeusPrint", "1983", 398},
    {"ZeusRoutines__ZeusScrolling.bin", "ZeusRoutines__ZeusScrolling", "1983", 135},
    {"ZeusRoutines__ZeusSelect.bin", "ZeusRoutines__ZeusSelect", "1983", 179},
    {"typed-zeus1983-PROBE.bin", "typed-zeus1983-PROBE", "1983", 28},   // typed into ZEUS 1983: every tokenizer rule
    {"PRIMUS29__PRI.DOC.$C", "PRIMUS29__PRI.DOC", "primus", 311},   // Primus 2.9's manual in its own format: Russian letters
    {"typed-primus29-PROBE.bin", "typed-primus29-PROBE", "primus", 26},   // Primus 2.9's text buffer: L.X, L?Z, L$V words
};

struct Input
{
    std::vector<uint8_t> data;
    CatalogHints hints;
};

Input Load(const Sample& sample)
{
    Input in;
    const std::vector<uint8_t> bytes = ReadTestData(std::string("zeus/") + sample.file);
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
        std::snprintf(number, sizeof(number), "%05d ", line.number);
        out += number + line.text + "\n";
    }
    return out;
}

std::vector<uint8_t> Body(const std::string& text, const std::string& version)
{
    std::vector<uint8_t> body;
    std::string error;
    EXPECT_TRUE(codecs::ZeusCodec::EncodeBody(text, version, body, error)) << text << ": " << error;
    return body;
}
}  // namespace

TEST(ZeusCodec_Test, SourcesDecodeAndRoundTrip)
{
    const codecs::ZeusCodec codec;
    for (const Sample& sample : kSamples)
    {
        const Input in = Load(sample);
        DecodeOptions options;
        options.catalog = in.hints;
        const DecodeResult decoded = codec.Decode(in.data, options);
        ASSERT_TRUE(decoded.ok) << sample.file;
        EXPECT_EQ(decoded.document.subversion, sample.version) << sample.file;
        EXPECT_EQ(decoded.document.lines.size(), sample.lines) << sample.file;
        EXPECT_EQ(Listing(decoded.document), ReadTestText(std::string("zeus/") + sample.stem + ".txt")) << sample.file;
        EXPECT_EQ(codec.Encode(decoded.document, {}).bytes, in.data) << sample.file << ": byte-exact";
    }
}

TEST(ZeusCodec_Test, TokenizerReproducesTheStoredLines)
{
    const codecs::ZeusCodec codec;
    for (const Sample& sample : kSamples)
    {
        const DecodeResult decoded = codec.Decode(Load(sample).data, {});
        size_t misses = 0;
        for (const SourceLine& line : decoded.document.lines)
            misses += !line.attrs.Empty();   // no stored bytes kept: the tokenizer gives them
        EXPECT_EQ(misses, 0u) << sample.file;
    }
}

TEST(ZeusCodec_Test, LineRules)
{
    // blanks counted (1 -> #20, 2+ -> #0A n), the first matching keyword wins, keywords in comments and strings too
    EXPECT_EQ(Body("     LD (DE),A", "1983"), (std::vector<uint8_t>{0x0A, 0x05, 0xB3, '(', 0x94, ')', ',', 0x80}));
    EXPECT_EQ(Body(";LD A,B HL", "1983"), (std::vector<uint8_t>{';', 0xB3, 0x80, ',', 0x86, ' ', 0xA5}));
    EXPECT_EQ(Body("DEFM /LD A,B/", "1983"), (std::vector<uint8_t>{0x97, '/', 0xB3, 0x80, ',', 0x86, '/'}));
    // a word that is no keyword is copied whole; after '"' or '#' the word is text; lower case is text
    EXPECT_EQ(Body("HELD #DE ld", "1983"), (std::vector<uint8_t>{'H', 'E', 'L', 'D', ' ', '#', 'D', 'E', ' ', 'l', 'd'}));
    // '_' joins words in PHT ZEUS only; PHT renames DEFB to DB
    EXPECT_EQ(Body("X_A_Y", "1983"), (std::vector<uint8_t>{'X', '_', 0x80, '_', 'Y'}));
    EXPECT_EQ(Body("X_A_Y", "pht"), (std::vector<uint8_t>{'X', '_', 'A', '_', 'Y'}));
    EXPECT_EQ(Body("DB 1", "pht"), (std::vector<uint8_t>{0x96, '1'}));
    EXPECT_EQ(codecs::ZeusCodec::DecodeBody(std::vector<uint8_t>{0x96, '1'}, "1983"), "DEFB 1");
    // a keyword typed alone takes its blank from the screen pad; trailing blanks go
    EXPECT_EQ(Body("PUSH", "1983"), (std::vector<uint8_t>{0xC9}));
    EXPECT_EQ(Body("NOP   ", "1983"), (std::vector<uint8_t>{0xBB}));
}

TEST(ZeusCodec_Test, VersionsAndRenumbering)
{
    const codecs::ZeusCodec codec;
    // #E6 (PLACE) exists only in PHT ZEUS
    const std::vector<uint8_t> pht{10, 0, 0xE6, '1', 0, 0xFF, 0xFF};
    EXPECT_EQ(codecs::ZeusCodec::DetectVersions(pht).front(), "pht");
    // a document from another format is numbered 10, 20, ... and tokenized
    const EncodeResult fromText = codec.Encode(SourceDocument::FromText("ORG 30000\nRET"), {});
    EXPECT_TRUE(fromText.ok);
    EXPECT_EQ(fromText.bytes, (std::vector<uint8_t>{10, 0, 0xBF, '3', '0', '0', '0', '0', 0, 20, 0, 0xCC, 0, 0xFF, 0xFF}));
    // the same document as PHT ZEUS: DEFB is written as DB's code
    const DecodeResult decoded = codec.Decode(std::vector<uint8_t>{10, 0, 0x96, '1', 0, 0xFF, 0xFF}, {});
    EXPECT_EQ(decoded.document.subversion, "1983");
    EXPECT_EQ(decoded.document.Text(), "DEFB 1");
}

TEST(ZeusCodec_Test, Detection)
{
    for (const Sample& sample : kSamples)
    {
        const Input in = Load(sample);
        const DetectResult detected = CodecRegistry::Builtin().Detect(in.data, in.hints);
        ASSERT_NE(detected.chosen, nullptr) << sample.file << ": " << detected.reason;
        EXPECT_EQ(detected.chosen->Info().id, "zeus") << sample.file;
    }
    // music data whose bytes happen to frame increasing "lines" up to an #FF #FF: control bytes give it away
    const std::vector<uint8_t> music{0x01, 0x00, 0x05, 0x10, 0x00, 0x02, 0x00, 0x07, 0x00, 0x03, 0x00, 0x81, 0x00, 0xFF, 0xFF};
    EXPECT_LE(codecs::ZeusCodec().Detect(music, {}), 15);
}

TEST(ZeusCodec_Test, PrimusLettersAndWords)
{
    // Primus 2.9: Russian letters at #EB-#FF (KOI-8 order without the ones that look Latin), ? . @ _ $ inside words,
    // DISK in DISP's place
    EXPECT_EQ(Body("ACCEMБЛEP ЮЩИЙ", "primus"), (std::vector<uint8_t>{'A', 'C', 'C', 'E', 'M', 0xEC, 0xF3, 'E', 'P', ' ', 0xEB, 0xFD, 0xF1, 0xF2}));
    EXPECT_EQ(Body("L.X NOP", "primus"), (std::vector<uint8_t>{'L', '.', 'X', ' ', 0xBB}));
    EXPECT_EQ(Body("L.X NOP", "1983"), (std::vector<uint8_t>{0xB2, '.', 'X', ' ', 0xBB}));
    EXPECT_EQ(Body(" DISK LIB1", "primus"), (std::vector<uint8_t>{' ', 0x9B, 'L', 'I', 'B', '1'}));
    EXPECT_EQ(codecs::ZeusCodec::DecodeBody(std::vector<uint8_t>{0x9B, 0xFE, 0xFF}, "primus"), "DISK ЧЪ");
}
