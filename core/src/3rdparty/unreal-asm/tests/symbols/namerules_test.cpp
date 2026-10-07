// DT-3 name rules and DT-4 page handling: every mangling step, collisions, parents, names kept from the target format,
// and fold / comment / drop through the codecs

#include <gtest/gtest.h>

#include "unrealasm/symbols/codec.h"
#include "unrealasm/symbols/namerules.h"

using namespace unrealasm;
using namespace unrealasm::symbols;

namespace
{
SymbolFile File(std::vector<std::string> names)
{
    SymbolFile f;
    f.sets.emplace_back();
    for (const std::string& n : names)
    {
        Symbol s;
        s.name = n;
        f.sets[0].symbols.push_back(s);
    }
    return f;
}

std::vector<std::string> Names(const SymbolFile& f)
{
    std::vector<std::string> out;
    for (const Symbol& s : f.sets[0].symbols)
        out.push_back(s.name);
    return out;
}
}  // namespace

TEST(NameRules_Test, Mangling)
{
    NameRules r;
    r.charset = Charset::Identifier;
    r.extra = "_";
    r.firstExtra = "_";
    r.reserveZ80 = true;
    r.maxLength = 12;
    SymbolFile f = File({"PRINT-A-1", "1st", "ld", "HL", "nz", "ok_name", "A_VERY_LONG_NAME_HERE", "PRINT_A_1", ""});
    f.sets[0].symbols[1].parent = "PRINT-A-1";
    const std::vector<Rename> renames = ApplyNameRules(f, r);
    EXPECT_EQ(f.sets[0].symbols[0].name, "PRINT_A_1");
    EXPECT_EQ(f.sets[0].symbols[1].name, "_1st");             // a digit may not start a name
    EXPECT_EQ(f.sets[0].symbols[1].parent, "PRINT_A_1");      // the parent follows its symbol
    EXPECT_EQ(f.sets[0].symbols[2].name, "ld_");              // instructions, registers, conditions are reserved
    EXPECT_EQ(f.sets[0].symbols[3].name, "HL_");
    EXPECT_EQ(f.sets[0].symbols[4].name, "nz_");
    EXPECT_EQ(f.sets[0].symbols[5].name, "ok_name");
    EXPECT_EQ(f.sets[0].symbols[6].name.size(), 12u);         // cut, with a hash of the original
    EXPECT_EQ(f.sets[0].symbols[6].name.substr(0, 7), "A_VERY_");
    EXPECT_EQ(f.sets[0].symbols[7].name, "PRINT_A_1_2");      // clashes with the first one's new name
    EXPECT_EQ(f.sets[0].symbols[8].name, "_");
    EXPECT_EQ(renames.size(), 8u);
    // Upper case targets; a name the target format itself wrote is kept
    NameRules upper;
    upper.upper = true;
    SymbolFile g = File({"start", "kept"});
    g.sets[0].symbols[1].provenance.importer = "x";
    ApplyNameRules(g, upper, "x");
    EXPECT_EQ(Names(g), (std::vector<std::string>{"START", "kept"}));
}

TEST(NameRules_Test, PagesFoldCommentDrop)
{
    SymbolFile f = File({"CPU", "PAGED"});
    AddressSpace::Parse("ram3", f.sets[0].symbols[1].location.space);
    f.sets[0].symbols[0].location.offset = 0x8000;
    f.sets[0].symbols[1].location.offset = 0x10;
    const ISymbolCodec& sym = *SymbolCodecRegistry::Builtin().Find("simple-sym");
    auto text = [&](Unrepresentable u) {
        SymbolEncodeOptions o;
        o.unrepresentable = u;
        const SymbolEncodeResult r = sym.Encode(f, o);
        return std::string(r.bytes.begin(), r.bytes.end());
    };
    EXPECT_NE(text(Unrepresentable::Fold).find("C010 PAGED"), std::string::npos);   // the usual window of page 3
    const std::string commented = text(Unrepresentable::Comment);
    EXPECT_NE(commented.find("; PAGED ram3:#0010"), std::string::npos);
    EXPECT_EQ(commented.find("C010 PAGED"), std::string::npos);
    const std::string dropped = text(Unrepresentable::Drop);
    EXPECT_EQ(dropped.find("PAGED"), std::string::npos);
    EXPECT_NE(dropped.find("8000 CPU"), std::string::npos);
    Unrepresentable u = Unrepresentable::Fold;
    EXPECT_TRUE(ParseUnrepresentable("comment", u));
    EXPECT_EQ(u, Unrepresentable::Comment);
    EXPECT_EQ(UnrepresentableName(Unrepresentable::Drop), "drop");
}

TEST(NameRules_Test, ExportRenamesForTheTarget)
{
    // A ROM map's names into sjasmplus: dashes are not taken there, the rename is reported and written as a comment
    SymbolFile f = File({"PRINT-A-1", "START"});
    f.sets[0].symbols[0].location.offset = 0x10;
    const SymbolEncodeResult r = SymbolCodecRegistry::Builtin().Find("sjasmplus-sym")->Encode(f, {});
    const std::string text(r.bytes.begin(), r.bytes.end());
    EXPECT_EQ(text, "; renamed PRINT-A-1 -> PRINT_A_1\nPRINT_A_1: EQU 0x00000010\nSTART: EQU 0x00000000\n");
    ASSERT_FALSE(r.diagnostics.empty());
    EXPECT_EQ(r.diagnostics[0].message, "renamed PRINT-A-1 -> PRINT_A_1");
}
