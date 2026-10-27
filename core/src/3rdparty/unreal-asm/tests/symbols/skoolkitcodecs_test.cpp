// The SkoolKit formats: control files (sna2skool.py -c) and skool files read and written, on files of the zx-reaper
// harness (testdata/symbols/skoolkit/) and on our own symbols; each is detected and round-trips what it holds

#include <gtest/gtest.h>

#include <algorithm>

#include "testdata.h"
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

const Symbol* Named(const SymbolSet& set, const std::string& name)
{
    const auto it = std::find_if(set.symbols.begin(), set.symbols.end(), [&](const Symbol& s) { return s.name == name; });
    return it == set.symbols.end() ? nullptr : &*it;
}

bool Has(const Symbol& s, const std::string& trait)
{
    return std::find(s.traits.begin(), s.traits.end(), trait) != s.traits.end();
}

/// The lines of a text without the comment lines ('#')
std::vector<std::string> DataLines(const std::string& text)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string line = text.substr(start, end - start);
        if (!line.empty() && line[0] != '#')
            out.push_back(line);
        start = end + 1;
    }
    return out;
}

/// Our symbols: a routine with a comment, a local label in it, a table, a constant, a ROM routine
SymbolFile Sample()
{
    SymbolFile f;
    f.sets.emplace_back();
    Symbol start;
    start.name = "start";
    start.location.offset = 0x8000;
    start.kind = SymbolKind::Code;
    start.comment = "entry, \"main\"";
    Symbol loop;
    loop.name = "loop";
    loop.location.offset = 0x8003;
    loop.kind = SymbolKind::Local;
    loop.parent = "start";
    Symbol table;
    table.name = "table";
    table.location.offset = 0x9000;
    table.kind = SymbolKind::Data;
    Symbol lines;
    lines.name = "LINES";
    lines.location.space.kind = SpaceKind::Constant;
    lines.location.offset = 24;
    lines.kind = SymbolKind::Const;
    Symbol print;
    print.name = "PRINT_A";
    print.location.space.kind = SpaceKind::Rom;
    print.location.offset = 0x0010;
    f.sets[0].symbols = {start, loop, table, lines, print};
    return f;
}
}  // namespace

TEST(SkoolkitCodecs_Test, ControlFileOfTheHarness)
{
    const std::vector<uint8_t> bytes = unrealasm::testing::ReadTestData("symbols/skoolkit/horace-game.ctl");
    const SymbolDecodeResult r = Codec("skoolkit-ctl").Decode(bytes);
    ASSERT_TRUE(r.ok);
    const SymbolSet& set = r.file.sets[0];
    // 14 blocks before the final "i $8000"; none has a label, so each gets SkoolKit's default one
    ASSERT_EQ(set.symbols.size(), 14u);
    EXPECT_EQ(set.symbols[0].name, "L5FB5");
    EXPECT_TRUE(Has(set.symbols[0], "skoolkit:s"));
    EXPECT_TRUE(Has(set.symbols[0], "skoolkit:auto"));
    EXPECT_EQ(set.symbols[0].kind, SymbolKind::Data);
    EXPECT_EQ(set.symbols[0].size, 0x6000u - 0x5FB5u);
    EXPECT_EQ(set.symbols[1].kind, SymbolKind::Code);
    const Symbol* text = Named(set, "L7EE8");
    ASSERT_NE(text, nullptr);
    EXPECT_TRUE(Has(*text, "skoolkit:t"));
    EXPECT_EQ(Named(set, "L7EEC")->size, 0x8000u - 0x7EECu);   // up to the ignored block
    // "@ $5FB5 start" / "org" have no place in the symbols: reported
    ASSERT_FALSE(r.diagnostics.empty());
    EXPECT_NE(r.diagnostics.back().message.find("@ x2"), std::string::npos);

    // Written back: the same blocks and the end, without the default labels
    const std::vector<std::string> written = DataLines(Text(Codec("skoolkit-ctl").Encode(r.file, {})));
    std::vector<std::string> original = DataLines(unrealasm::testing::ReadTestText("symbols/skoolkit/horace-game.ctl"));
    original.erase(std::remove_if(original.begin(), original.end(), [](const std::string& l) { return l[0] == '@'; }), original.end());
    EXPECT_EQ(written, original);
}

TEST(SkoolkitCodecs_Test, ControlFileFragment)
{
    const SymbolDecodeResult r = Codec("skoolkit-ctl").Decode(unrealasm::testing::ReadTestData("symbols/skoolkit/horace-6a17.ctl"));
    ASSERT_TRUE(r.ok);
    const SymbolSet& set = r.file.sets[0];
    ASSERT_EQ(set.symbols.size(), 2u);
    EXPECT_EQ(set.symbols[0].name, "L6A17");
    EXPECT_EQ(set.symbols[0].comment, "Unused code bytes (LD A,(IX+0); LD A,(IX+0); RET), never executed");
    EXPECT_EQ(set.symbols[0].size, 7u);
    const Symbol& exit = set.symbols[1];
    EXPECT_EQ(exit.name, "ExitMaze");
    EXPECT_EQ(exit.location.offset, 0x6A1Eu);
    EXPECT_EQ(exit.kind, SymbolKind::Code);
    EXPECT_EQ(exit.size, 4u);
    EXPECT_EQ(exit.comment, "Leave the maze through the exit arrows");
    EXPECT_EQ(exit.provenance.importer, "skoolkit-ctl");
    EXPECT_EQ(exit.provenance.line, 3u);  // the block line; the label line names it

    const std::string written = Text(Codec("skoolkit-ctl").Encode(r.file, {}));
    const std::vector<std::string> expected = {"b $6A17 Unused code bytes (LD A,(IX+0); LD A,(IX+0); RET), never executed",
                                               "c $6A1E Leave the maze through the exit arrows", "@ $6A1E label=ExitMaze",
                                               "i $6A22"};
    EXPECT_EQ(DataLines(written), expected);
    // The written file reads back to the same symbols
    const SymbolDecodeResult again = Codec("skoolkit-ctl").Decode(Bytes(written));
    ASSERT_EQ(again.file.sets[0].symbols.size(), 2u);
    for (size_t i = 0; i < 2; ++i)
    {
        EXPECT_EQ(again.file.sets[0].symbols[i].name, set.symbols[i].name);
        EXPECT_EQ(again.file.sets[0].symbols[i].kind, set.symbols[i].kind);
        EXPECT_EQ(again.file.sets[0].symbols[i].size, set.symbols[i].size);
        EXPECT_EQ(again.file.sets[0].symbols[i].comment, set.symbols[i].comment);
    }
}

TEST(SkoolkitCodecs_Test, ControlFileOfOurSymbols)
{
    const SymbolEncodeResult encoded = Codec("skoolkit-ctl").Encode(Sample(), {});
    ASSERT_TRUE(encoded.ok);
    EXPECT_EQ(encoded.written, 5u);
    const std::vector<std::string> expected = {"c $8000 entry, \"main\"", "@ $8000 label=start", "@ $8000 equ=LINES=$0018",
                                               "@ $8000 equ=PRINT_A=$0010", "@ $8003 label=loop", "b $9000", "@ $9000 label=table"};
    EXPECT_EQ(DataLines(Text(encoded)), expected);

    const SymbolDecodeResult r = Codec("skoolkit-ctl").Decode(encoded.bytes);
    const SymbolSet& set = r.file.sets[0];
    ASSERT_EQ(set.symbols.size(), 5u);
    EXPECT_EQ(Named(set, "start")->kind, SymbolKind::Code);
    EXPECT_EQ(Named(set, "start")->comment, "entry, \"main\"");
    EXPECT_EQ(Named(set, "start")->size, 0x1000u);
    EXPECT_EQ(Named(set, "loop")->location.offset, 0x8003u);
    EXPECT_EQ(Named(set, "loop")->kind, SymbolKind::Code);   // the kind of the block it is in
    EXPECT_TRUE(Has(*Named(set, "loop"), "skoolkit:point"));
    EXPECT_EQ(Named(set, "table")->kind, SymbolKind::Data);
    EXPECT_EQ(Named(set, "LINES")->location.offset, 24u);
    EXPECT_TRUE(Has(*Named(set, "PRINT_A"), "skoolkit:equ"));

    // Two names at one address: a control file holds one
    SymbolFile two = Sample();
    Symbol other = two.sets[0].symbols[0];
    other.name = "begin";
    two.sets[0].symbols.push_back(other);
    const SymbolEncodeResult clash = Codec("skoolkit-ctl").Encode(two, {});
    EXPECT_EQ(clash.written, 5u);
    EXPECT_TRUE(std::any_of(clash.diagnostics.begin(), clash.diagnostics.end(),
                            [](const Diagnostic& d) { return d.message.find("one per address") != std::string::npos; }));
}

TEST(SkoolkitCodecs_Test, SkoolFileOfTheHarness)
{
    const SymbolDecodeResult r = Codec("skoolkit-skool").Decode(unrealasm::testing::ReadTestData("symbols/skoolkit/sample.skool"));
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(r.diagnostics.empty());
    const SymbolSet& set = r.file.sets[0];
    // Two entries (one without a label), two labels inside one, five @equ
    ASSERT_EQ(set.symbols.size(), 9u);
    const Symbol* vocabulary = Named(set, "Vocabulary");
    ASSERT_NE(vocabulary, nullptr);
    EXPECT_EQ(vocabulary->location.offset, 0x6000u);
    EXPECT_EQ(vocabulary->kind, SymbolKind::Data);
    EXPECT_TRUE(Has(*vocabulary, "skoolkit:w"));
    EXPECT_EQ(vocabulary->comment, "Vocabulary index by first letter");
    const Symbol* tail = Named(set, "L672F");
    ASSERT_NE(tail, nullptr);
    EXPECT_EQ(tail->kind, SymbolKind::Code);
    EXPECT_EQ(tail->comment, "Tail of the guard loop of the routine at 66A4");
    const Symbol* store = Named(set, "CheckGuardContact_Store");
    ASSERT_NE(store, nullptr);
    EXPECT_EQ(store->location.offset, 0x6732u);
    EXPECT_EQ(store->kind, SymbolKind::Code);
    EXPECT_EQ(store->comment, "write the record back");
    EXPECT_EQ(Named(set, "CheckGuardContact_Next")->location.offset, 0x6735u);
    EXPECT_EQ(Named(set, "ROM_BEEPER")->location.offset, 0x03B5u);
    EXPECT_EQ(Named(set, "ATTRS")->location.offset, 0x5800u);

    // Written as a head.skool of @equ lines; read back: every name at its address, the default label left out
    const SymbolEncodeResult encoded = Codec("skoolkit-skool").Encode(r.file, {});
    const std::string head = Text(encoded);
    EXPECT_EQ(head.substr(0, 7), "@start\n");
    EXPECT_NE(head.find("@equ=CheckGuardContact_Store=$6732\n"), std::string::npos);
    EXPECT_NE(head.find("@equ=ROM_BEEPER=$03B5\n"), std::string::npos);
    EXPECT_EQ(head.find("L672F"), std::string::npos);
    EXPECT_EQ(encoded.written, 8u);
    const SymbolDecodeResult again = Codec("skoolkit-skool").Decode(encoded.bytes);
    ASSERT_EQ(again.file.sets[0].symbols.size(), 8u);
    EXPECT_EQ(Named(again.file.sets[0], "Vocabulary")->location.offset, 0x6000u);
}

TEST(SkoolkitCodecs_Test, SkoolDecimalAndQuotes)
{
    const std::string skool = "; Print a message\n"
                              "; Second line of the title\n"
                              ";\n"
                              "; Description.\n"
                              "@label=PRINT\n"
                              "c32768 LD HL,32775   ; the text\n"
                              "@label=MSG\n"
                              " 32771 DEFM \"a;b\"     ; quoted ; kept\n"
                              "                     ; continued comment\n"
                              "\n"
                              "i32774\n";
    const SymbolDecodeResult r = Codec("skoolkit-skool").Decode(Bytes(skool));
    EXPECT_TRUE(r.diagnostics.empty());
    const SymbolSet& set = r.file.sets[0];
    ASSERT_EQ(set.symbols.size(), 2u);
    EXPECT_EQ(set.symbols[0].name, "PRINT");
    EXPECT_EQ(set.symbols[0].location.offset, 32768u);
    EXPECT_EQ(set.symbols[0].size, 6u);
    EXPECT_EQ(set.symbols[0].comment, "Print a message Second line of the title");
    EXPECT_EQ(set.symbols[1].name, "MSG");
    EXPECT_EQ(set.symbols[1].comment, "quoted ; kept");
}

TEST(SkoolkitCodecs_Test, Detection)
{
    const auto& registry = SymbolCodecRegistry::Builtin();
    for (const char* file : {"symbols/skoolkit/horace-game.ctl", "symbols/skoolkit/horace-6a17.ctl"})
    {
        const std::vector<uint8_t> bytes = unrealasm::testing::ReadTestData(file);
        EXPECT_EQ(registry.Detect(bytes, "ctl").chosen, &Codec("skoolkit-ctl")) << file;
        EXPECT_EQ(registry.Detect(bytes, "").chosen, &Codec("skoolkit-ctl")) << file;
        EXPECT_EQ(Codec("skoolkit-skool").Detect({bytes, ""}), 0) << file;
    }
    const std::vector<uint8_t> skool = unrealasm::testing::ReadTestData("symbols/skoolkit/sample.skool");
    EXPECT_EQ(registry.Detect(skool, "skool").chosen, &Codec("skoolkit-skool"));
    EXPECT_EQ(registry.Detect(skool, "").chosen, &Codec("skoolkit-skool"));
    EXPECT_EQ(Codec("skoolkit-ctl").Detect({skool, ""}), 0);
    // Our own written files are detected too
    const SymbolEncodeResult ctl = Codec("skoolkit-ctl").Encode(Sample(), {});
    EXPECT_EQ(registry.Detect(ctl.bytes, "").chosen, &Codec("skoolkit-ctl"));
    // Other formats stay theirs
    const std::vector<uint8_t> map = Bytes("8000 start (CODE)\n9000 table (DATA)\n");
    EXPECT_EQ(Codec("skoolkit-ctl").Detect({map, ""}), 0);
    EXPECT_EQ(Codec("skoolkit-skool").Detect({map, ""}), 0);
}
