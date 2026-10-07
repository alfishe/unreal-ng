// The disassembler and debugger formats: IDA IDC and IDAPython scripts (written and read, hand-written forms read
// too), Ghidra's System.map import, MAME debugger comments; each round-trips what it holds and is detected

#include <gtest/gtest.h>

#include "unrealasm/symbols/codec.h"

using namespace unrealasm;
using namespace unrealasm::symbols;

namespace
{
const ISymbolCodec& Codec(const char* id)
{
    return *SymbolCodecRegistry::Builtin().Find(id);
}

std::vector<uint8_t> Bytes(const std::string& s)
{
    return std::vector<uint8_t>(s.begin(), s.end());
}

std::string Text(const SymbolEncodeResult& r)
{
    return std::string(r.bytes.begin(), r.bytes.end());
}

SymbolFile Sample()
{
    SymbolFile f;
    f.sets.emplace_back();
    Symbol a;
    a.name = "start";
    a.location.offset = 0x8000;
    a.kind = SymbolKind::Code;
    a.exported = true;
    a.comment = "entry, \"main\"";
    Symbol b;
    b.name = "table";
    b.location.offset = 0x9000;
    b.kind = SymbolKind::Data;
    Symbol c;
    c.name = "LINES";
    c.location.offset = 24;
    c.kind = SymbolKind::Const;
    f.sets[0].symbols = {a, b, c};
    return f;
}
}  // namespace

TEST(ScriptCodecs_Test, IdaScripts)
{
    const std::string idc = Text(Codec("ida-idc").Encode(Sample(), {}));
    EXPECT_NE(idc.find("#include <idc.idc>"), std::string::npos);
    EXPECT_NE(idc.find("    set_name(0x8000, \"start\", SN_NOWARN);"), std::string::npos);
    EXPECT_NE(idc.find("    set_cmt(0x8000, \"entry, \\\"main\\\"\", 0);"), std::string::npos);
    const std::string py = Text(Codec("ida-python").Encode(Sample(), {}));
    EXPECT_NE(py.find("import idc"), std::string::npos);
    EXPECT_NE(py.find("idc.set_name(0x9000, \"table\", idc.SN_NOWARN)"), std::string::npos);
    for (const std::string& script : {idc, py})
    {
        const SymbolDecodeResult r = Codec("ida-idc").Decode(Bytes(script));
        ASSERT_EQ(r.file.sets[0].symbols.size(), 3u);
        EXPECT_EQ(r.file.sets[0].symbols[0].name, "start");
        EXPECT_EQ(r.file.sets[0].symbols[0].comment, "entry, \"main\"");
        EXPECT_EQ(r.file.sets[0].symbols[1].location.offset, 0x9000u);
    }
    EXPECT_EQ(SymbolCodecRegistry::Builtin().Detect(Bytes(idc), "").chosen, &Codec("ida-idc"));
    EXPECT_EQ(SymbolCodecRegistry::Builtin().Detect(Bytes(py), "").chosen, &Codec("ida-python"));
    // Hand-written and older forms
    const SymbolDecodeResult old = Codec("ida-idc").Decode(Bytes("MakeName(0x4000, \"SCREEN\");\nMakeComm(0x4000, \"pixels\");\n"
                                                                 "ida_name.set_name(0xC000, 'PAGED')\nMakeNameEx(49153, \"NEXT\", 0);\n"));
    ASSERT_EQ(old.file.sets[0].symbols.size(), 3u);
    EXPECT_EQ(old.file.sets[0].symbols[0].comment, "pixels");
    EXPECT_EQ(old.file.sets[0].symbols[1].name, "PAGED");
    EXPECT_EQ(old.file.sets[0].symbols[2].location.offset, 0xC001u);
}

TEST(ScriptCodecs_Test, GhidraSystemMap)
{
    const std::string map = Text(Codec("ghidra").Encode(Sample(), {}));
    EXPECT_EQ(map, "8000 T start\n9000 D table\n0018 A LINES\n");   // T makes a function in Ghidra
    const SymbolDecodeResult r = Codec("ghidra").Decode(Bytes(map + "c000 t local_text\n4000 SCREEN\n"));
    ASSERT_EQ(r.file.sets[0].symbols.size(), 5u);
    EXPECT_EQ(r.file.sets[0].symbols[0].kind, SymbolKind::Code);
    EXPECT_EQ(r.file.sets[0].symbols[1].kind, SymbolKind::Data);
    EXPECT_EQ(r.file.sets[0].symbols[2].kind, SymbolKind::Const);
    EXPECT_FALSE(r.file.sets[0].symbols[3].exported);               // lower case: not exported
    EXPECT_EQ(r.file.sets[0].symbols[4].kind, SymbolKind::Unknown);  // the two-word form of the script
    EXPECT_EQ(Text(Codec("ghidra").Encode(r.file, {})), "8000 T start\n9000 D table\n0018 A LINES\nc000 t local_text\n4000 ? SCREEN\n");
    EXPECT_EQ(SymbolCodecRegistry::Builtin().Detect(Bytes(map), "").chosen, &Codec("ghidra"));
}

TEST(ScriptCodecs_Test, MameComments)
{
    const std::string cmd = Text(Codec("mame").Encode(Sample(), {}));
    EXPECT_EQ(cmd, "comadd 8000,start - entry  \"main\"\ncomadd 9000,table\ncomadd 0018,LINES\n");   // no commas in MAME's text
    const SymbolDecodeResult r = Codec("mame").Decode(Bytes(cmd + "// 10,PRINT-A-1\n"));
    ASSERT_EQ(r.file.sets[0].symbols.size(), 4u);
    EXPECT_EQ(r.file.sets[0].symbols[0].comment, "entry  \"main\"");
    EXPECT_EQ(r.file.sets[0].symbols[3].name, "PRINT-A-1");
    EXPECT_EQ(r.file.sets[0].symbols[3].location.offset, 0x10u);
    EXPECT_EQ(SymbolCodecRegistry::Builtin().Detect(Bytes(cmd), "").chosen, &Codec("mame"));
}
