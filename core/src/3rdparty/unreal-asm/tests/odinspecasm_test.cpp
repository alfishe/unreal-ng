// The Odin document codec (.odn: ODS header, tokenized lines) and the Specasm (.s) frontend. The real Odin documents of
// testdata/odin must round-trip byte for byte; Specasm's programs are built by its own tools in
// tools/verification/unreal-asm/checks/specasmcheck.py.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "codecs/odin/odincodec.h"
#include "unrealasm/containers.h"
#include "testdata.h"
#include "unrealasm/dialect.h"
#include "unrealasm/registry.h"

using namespace unrealasm;
using unrealasm::testing::ReadTestData;

namespace
{
std::vector<std::string> Lines(const SourceDocument& d)
{
    std::vector<std::string> out;
    for (const SourceLine& l : d.lines)
        out.push_back(l.text);
    return out;
}
}  // namespace

TEST(OdinCodec_Test, TokenizesInstructionsOperandsAndImplicitBlanks)
{
    // LD A,B: LD ($A4), an implicit blank, A ($87), a comma, B ($80)
    EXPECT_EQ(codecs::OdinCodec::Tokenize("ld a,b"), (std::vector<uint8_t>{0xA4, 0x87, ',', 0x80}));
    // NEXTREG is the second spelling of $D6 (the sub-index follows the token)
    EXPECT_EQ(codecs::OdinCodec::Tokenize("nextreg 7,0"), (std::vector<uint8_t>{0xD6, 0x01, '7', ',', '0'}));
    // a label is not looked up as a token; blanks compress
    EXPECT_EQ(codecs::OdinCodec::Tokenize("start  nop"), (std::vector<uint8_t>{'s', 't', 'a', 'r', 't', 0x1F, 0xAA}));
    // a comment and a string are left alone
    EXPECT_EQ(codecs::OdinCodec::Tokenize("; ld a,b"), (std::vector<uint8_t>{';', 0x20, 'l', 'd', 0x20, 'a', ',', 'b'}));
}

TEST(OdinCodec_Test, ExpandIsTheInverse)
{
    bool valid = false;
    const std::vector<uint8_t> line = {0xA4, 0x87, ',', 0x80};
    EXPECT_EQ(codecs::OdinCodec::Expand(line, valid), "LD A,B");
    EXPECT_TRUE(valid);
    const std::vector<uint8_t> nextreg = {0xD6, 0x01, '7', ',', '0'};
    EXPECT_EQ(codecs::OdinCodec::Expand(nextreg, valid), "NEXTREG 7,0");
}

TEST(OdinCodec_Test, LongRunsOfBlanksUseTheCountForm)
{
    const std::vector<uint8_t> line = codecs::OdinCodec::Tokenize("a" + std::string(30, ' ') + "b");
    EXPECT_EQ(line, (std::vector<uint8_t>{'a', 0x0A, 30, 'b'}));
}

TEST(OdinCodec_Test, TheRealDocumentsRoundTripByteForByte)
{
    const CodecRegistry& registry = CodecRegistry::Builtin();
    const ISourceCodec* odin = registry.Find("odin");
    ASSERT_NE(odin, nullptr);
    for (const char* name : {"odin/learn.odn", "odin/plot.odn", "odin/version.odn"})
    {
        const std::vector<uint8_t> bytes = ReadTestData(name);
        ASSERT_FALSE(bytes.empty()) << name;
        EXPECT_GE(odin->Detect(bytes, {}), 90) << name;
        const DecodeResult decoded = odin->Decode(bytes, {});
        ASSERT_TRUE(decoded.ok) << name;
        EXPECT_EQ(odin->Encode(decoded.document, {}).bytes, bytes) << name;
    }
}

TEST(OdinCodec_Test, ThePlotDocumentReadsAsAssembler)
{
    const ISourceCodec* odin = CodecRegistry::Builtin().Find("odin");
    const std::vector<uint8_t> bytes = ReadTestData("odin/plot.odn");
    const std::vector<std::string> lines = Lines(odin->Decode(bytes, {}).document);
    ASSERT_GE(lines.size(), 5u);
    EXPECT_EQ(lines[0], "Start");
    EXPECT_EQ(lines[1], "    NEXTREG 7,0");
    EXPECT_EQ(lines[2], "    LD A,0");
    EXPECT_EQ(lines[3], "    LD (xpos),A");
}

TEST(OdinCodec_Test, OdinTextConvertsToSjasmplus)
{
    const ISourceCodec* odin = CodecRegistry::Builtin().Find("odin");
    const DecodeResult decoded = odin->Decode(ReadTestData("odin/plot.odn"), {});
    const ConvertResult r = Convert(decoded.document, "sjasmplus");
    EXPECT_TRUE(r.ok);
    const std::string text = r.document.Text();
    EXPECT_NE(text.find("NEXTREG 7,0"), std::string::npos) << text;
    EXPECT_NE(text.find("LD (xpos),A"), std::string::npos) << text;
}

namespace
{
ir::Program ParseSpecasm(const std::string& text)
{
    return DialectRegistry::Builtin().Frontend("specasm")->Parse(SourceDocument::FromText(text, "specasm")).program;
}

std::vector<std::string> SpecasmToSjasmplus(const std::string& text)
{
    const ConvertResult r = Convert(SourceDocument::FromText(text, "specasm"), "sjasmplus");
    std::vector<std::string> out;
    for (const SourceLine& l : r.document.lines)
    {
        const size_t from = l.text.find_first_not_of(" \t");
        if (from == std::string::npos || l.text[from] == ';')
            continue;
        std::string line;
        for (size_t k = from; k < l.text.size(); ++k)
            if (!((l.text[k] == ' ' || l.text[k] == '\t') && !line.empty() && line.back() == ' '))
                line.push_back(l.text[k] == '\t' ? ' ' : l.text[k]);
        out.push_back(line);
    }
    return out;
}

bool Has(const std::vector<std::string>& lines, const std::string& line)
{
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}
}  // namespace

TEST(SpecasmFrontend_Test, LabelsOnTheirOwnLineAndEqu)
{
    const ir::Program p = ParseSpecasm(".Main\n.Size equ 16\n  ld a, =Size*2\n");
    EXPECT_EQ(p.lines[0].label, "Main");
    EXPECT_EQ(p.lines[1].label, "Size");
    EXPECT_EQ(p.lines[1].statements[0].directive, ir::DirectiveKind::Equ);
    EXPECT_EQ(p.lines[2].statements[0].operands[1].expr.kind, ir::Expr::Kind::Binary);   // the = marker is dropped
}

TEST(SpecasmFrontend_Test, StringDirectives)
{
    const std::vector<std::string> lines = SpecasmToSjasmplus("\"hello\"  ; five\n\"open string\n@Hello@\n");
    EXPECT_TRUE(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.rfind("DB 'hello'", 0) == 0; }));
    EXPECT_TRUE(Has(lines, "DB 'open string'"));
    EXPECT_TRUE(Has(lines, "DB 5,'Hello'"));   // @ writes the length first
}

TEST(SpecasmFrontend_Test, ExpressionOperatorsShareOnePriorityForBitOperations)
{
    // & | ^ at one priority, left to right: 1 | 2 & 3 is (1 | 2) & 3
    const ir::Expr e = ParseSpecasm("  ld a, =1|2&3\n").lines[0].statements[0].operands[1].expr;
    EXPECT_EQ(e.op, ir::Op::And);
    EXPECT_EQ(e.args[0].op, ir::Op::Or);
}

TEST(SpecasmFrontend_Test, AlignNbrkAndIndexOffsets)
{
    const std::vector<std::string> lines = SpecasmToSjasmplus("  nbrk\n  ld a,(ix+-1)\n  ds 4, 'A'\n");
    EXPECT_TRUE(Has(lines, "NEXTREG 2,8"));
    EXPECT_TRUE(Has(lines, "LD A,(IX-1)") || Has(lines, "LD A,(IX+-1)"));
    EXPECT_TRUE(Has(lines, "DS 4,'A'"));
}

TEST(SpecasmFrontend_Test, TheCodecFindsTheTextForm)
{
    const ISourceCodec* specasm = CodecRegistry::Builtin().Find("specasm");
    ASSERT_NE(specasm, nullptr);
    const std::string text = ".Main\n  ld a, =10\n.loop\n  djnz loop\n";
    EXPECT_GT(specasm->Detect({reinterpret_cast<const uint8_t*>(text.data()), text.size()}, {}), 60);
    const std::string local = ".loop\n  djnz loop\n  ld a, 1 ; no marker\n";   // sjasmplus local labels look the same
    EXPECT_LE(specasm->Detect({reinterpret_cast<const uint8_t*>(local.data()), local.size()}, {}), 25);
    const std::string plain = "start: ld a,1\n";
    EXPECT_EQ(specasm->Detect({reinterpret_cast<const uint8_t*>(plain.data()), plain.size()}, {}), 0);
}

TEST(Plus3dos_Test, ReadsTheHeaderAndWritesItBack)
{
    containers::Plus3dosFile file;
    file.type = 3;
    file.start = 0x8000;
    file.data = {1, 2, 3, 4};
    const std::vector<uint8_t> bytes = containers::WritePlus3dos(file);
    ASSERT_EQ(bytes.size(), 128u + 4u);
    containers::Plus3dosFile back;
    std::string error;
    ASSERT_TRUE(containers::ReadPlus3dos(bytes, back, error)) << error;
    EXPECT_EQ(back.data, file.data);
    EXPECT_EQ(back.start, 0x8000);
    std::vector<uint8_t> broken = bytes;
    broken[20] ^= 1;   // the checksum no longer matches
    EXPECT_FALSE(containers::ReadPlus3dos(broken, back, error));
}

TEST(Plus3dos_Test, TheNextZeusSourcesAreZeusLineRecordsBehindAHeader)
{
    containers::Plus3dosFile file;
    std::string error;
    const std::vector<uint8_t> god = ReadTestData("zeus/ECHO.god");
    ASSERT_TRUE(containers::ReadPlus3dos(god, file, error)) << error;
    const ISourceCodec* zeus = CodecRegistry::Builtin().Find("zeus");
    ASSERT_NE(zeus, nullptr);
    const DecodeResult decoded = zeus->Decode(file.data, {});
    ASSERT_TRUE(decoded.ok);
    EXPECT_EQ(decoded.document.lines.size(), 18u);
    EXPECT_EQ(decoded.document.lines[0].text, "; ECHO dot command example");
    EXPECT_EQ(zeus->Encode(decoded.document, {}).bytes, file.data);
    // and back into the +3DOS wrapper
    file.data = zeus->Encode(decoded.document, {}).bytes;
    EXPECT_EQ(containers::WritePlus3dos(file).size(), 128u + file.data.size());
}
