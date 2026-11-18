// The rasm frontend: the source assembled by the rasm engine, the IR written from what it put down (instructions with
// their labels, data as DB / DW, crunched sections as bytes, BUILDZX as rasm's snapshot), converted to sjasmplus. A
// source the engine refuses is converted line by line. The bytes against rasm and sjasmplus:
// tools/verification/unreal-asm/checks/rasmconvertcheck.py.

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "unrealasm/dialect.h"
#include "unrealasm/symbols/fromsource.h"

using namespace unrealasm;

namespace
{
FrontendResult Parse(const std::string& text)
{
    return DialectRegistry::Builtin().Frontend("rasm")->Parse(SourceDocument::FromText(text, "rasm"));
}

std::vector<std::string> Sjasmplus(const std::string& text)
{
    const ConvertResult r = Convert(SourceDocument::FromText(text, "rasm"), "sjasmplus");
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
        const size_t comment = line.find(" ;");
        if (comment != std::string::npos)
            line.erase(comment);
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
        out.push_back(line);
    }
    return out;
}

bool Has(const std::vector<std::string>& lines, const std::string& line)
{
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}

bool HasMessage(const FrontendResult& r, const std::string& part)
{
    return std::any_of(r.diagnostics.begin(), r.diagnostics.end(), [&](const Diagnostic& d) { return d.message.find(part) != std::string::npos; });
}

std::string Joined(const std::vector<std::string>& lines)
{
    std::string s;
    for (const auto& l : lines)
        s += l + "\n";
    return s;
}
}  // namespace

TEST(RasmFrontend_Test, InstructionsAndDataKeepTheirLabels)
{
    const auto lines = Sjasmplus("org #8000\nstart: ld hl,table : jr start\ntable: defw start,table+2\n");
    EXPECT_TRUE(Has(lines, "ORG #8000")) << Joined(lines);
    EXPECT_TRUE(Has(lines, "start LD HL,table")) << Joined(lines);
    EXPECT_TRUE(Has(lines, "JR start")) << Joined(lines);
    EXPECT_TRUE(Has(lines, "table DW start,table+2")) << Joined(lines);
}

TEST(RasmFrontend_Test, MacrosAndLoopsAreWrittenAsAssembled)
{
    const auto lines = Sjasmplus("macro m\n@x: nop\n jr @x\nmend\n m\n m\n repeat 3,cnt\n defb cnt*2\n rend\n");
    EXPECT_EQ(std::count(lines.begin(), lines.end(), "NOP") + std::count_if(lines.begin(), lines.end(), [](const std::string& l) {
                  return l.size() > 4 && l.compare(l.size() - 4, 4, " NOP") == 0;
              }),
              2)
        << Joined(lines);
    EXPECT_TRUE(Has(lines, "DB 2") && Has(lines, "DB 4") && Has(lines, "DB 6")) << Joined(lines);
    EXPECT_FALSE(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.find("MACRO") != std::string::npos; }));
}

TEST(RasmFrontend_Test, NumbersAndOperatorsAreRasms)
{
    const FrontendResult r = Parse(" defb #ff,$ff,0xff,%101,0b101,101b,@17,10 % 3, 10 mod 3, 6 and 3, 1 xor 3\n");
    ASSERT_FALSE(HasMessage(r, "line by line"));
    std::vector<int64_t> values;
    for (const auto& l : r.program.lines)
        for (const auto& s : l.statements)
            for (const auto& o : s.operands)
                values.push_back(o.expr.value);
    EXPECT_EQ(values, (std::vector<int64_t>{255, 255, 255, 5, 5, 5, 15, 1, 1, 2, 2}));
}

TEST(RasmFrontend_Test, EnhancedInstructionsAreData)
{
    // LD HL,SP is rasm's LD HL,0 : ADD HL,SP and LD BC,DE its LD B,D : LD C,E: several instructions, written as their bytes
    const auto lines = Sjasmplus(" ld hl,sp\n ld bc,de\n");
    EXPECT_TRUE(Has(lines, "DB #21,0,0,#39,#42,#4B") || (Has(lines, "DB #21,0,0,#39") && Has(lines, "DB #42,#4B"))) << Joined(lines);
}

TEST(RasmFrontend_Test, AliasesBecomeStandardMnemonics)
{
    const auto lines = Sjasmplus(" exa\n ex af,af'\n sl1 b\n");
    EXPECT_EQ(std::count(lines.begin(), lines.end(), "EX AF,AF'"), 2) << Joined(lines);
    EXPECT_TRUE(Has(lines, "SLL B") || Has(lines, "SLI B")) << Joined(lines);
}

TEST(RasmFrontend_Test, CrunchedSectionsAreTheirCrunchedBytes)
{
    const FrontendResult r = Parse("org #4000\nlz48\n defs 40,7\nlzclose\nfin: jr fin\n");
    EXPECT_FALSE(HasMessage(r, "not converted")) << "the crunched bytes are written";
    const auto lines = Sjasmplus("org #4000\nlz48\n defs 40,7\nlzclose\nfin: jr fin\n");
    EXPECT_TRUE(Has(lines, "fin JR fin")) << Joined(lines);
    EXPECT_FALSE(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.find("LZ48") != std::string::npos; }));
}

TEST(RasmFrontend_Test, SaveBecomesSavebin)
{
    const auto lines = Sjasmplus("org #8000\nbegin: nop\nsave \"x.bin\",begin,1\n");
    EXPECT_TRUE(Has(lines, "SAVEBIN \"x.bin\",#8000,1")) << Joined(lines);
}

TEST(RasmFrontend_Test, BuildZxWritesRasmsSnapshot)
{
    const auto lines = Sjasmplus("buildzx\nbankset 0\norg #8000\ngo: di : halt\nrun go,#fffe\n");
    EXPECT_TRUE(Has(lines, "OUTPUT \"rasmoutput.sna\"")) << Joined(lines);
    EXPECT_TRUE(Has(lines, "go DI")) << Joined(lines);
    EXPECT_TRUE(Has(lines, "OUTEND")) << Joined(lines);
}

TEST(RasmFrontend_Test, CpcMediaAreReportedNotConverted)
{
    const FrontendResult r = Parse("buildsna\nbank 0\n nop\n");
    EXPECT_TRUE(HasMessage(r, "not converted: CPC snapshot"));
}

TEST(RasmFrontend_Test, ASourceTheEngineRefusesIsConvertedLineByLine)
{
    // a file of a project read alone: its labels are elsewhere
    const FrontendResult r = Parse(" ld a,missing\nmacro copy (src, dst)\n@again: ld a,src\n jr @again\nmend\n");
    EXPECT_TRUE(HasMessage(r, "converted line by line"));
    ASSERT_GE(r.program.lines.size(), 2u);
    EXPECT_EQ(r.program.lines[0].statements[0].mnemonic, "ld");
    const ir::Statement& m = r.program.lines[1].statements[0];
    EXPECT_EQ(m.directive, ir::DirectiveKind::Macro);
    EXPECT_EQ(m.params, (std::vector<std::string>{"src", "dst"}));
}

TEST(RasmFrontend_Test, SymbolsHaveTheValuesAndLinesOfTheSource)
{
    const auto r = symbols::SymbolsFromSource(SourceDocument::FromText("org #8000\nstart:\n nop\nnext: jr start\n", "rasm"));
    ASSERT_TRUE(r.ok);
    std::map<std::string, std::pair<uint32_t, uint32_t>> got;
    for (const auto& s : r.set.symbols)
        got[s.name] = {static_cast<uint32_t>(s.location.offset), s.source.line};
    EXPECT_EQ(got["start"], std::make_pair(0x8000u, 2u));
    EXPECT_EQ(got["next"], std::make_pair(0x8001u, 4u));
}
