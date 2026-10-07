// The label file codecs (phase S2): each format reads what LabelManager read, writes what it reads back, folds page
// symbols where the format has no pages, and is told apart from the others by its content

#include <gtest/gtest.h>

#include "symbols/codecs/labelfiles/labelfilecodecs.h"
#include "unrealasm/symbols/codec.h"

using namespace unrealasm;
using namespace unrealasm::symbols;

namespace
{
std::vector<uint8_t> Bytes(const std::string& s)
{
    return std::vector<uint8_t>(s.begin(), s.end());
}

const ISymbolCodec& Codec(const char* id)
{
    const ISymbolCodec* codec = SymbolCodecRegistry::Builtin().Find(id);
    EXPECT_NE(codec, nullptr) << id;
    return *codec;
}

std::vector<Symbol> Read(const char* id, const std::string& text, Diagnostics* diagnostics = nullptr)
{
    const SymbolDecodeResult r = Codec(id).Decode(Bytes(text));
    EXPECT_TRUE(r.ok) << id;
    if (diagnostics)
        *diagnostics = r.diagnostics;
    return r.file.sets.empty() ? std::vector<Symbol>{} : r.file.sets[0].symbols;
}

std::string Write(const char* id, const std::vector<Symbol>& symbols, Diagnostics* diagnostics = nullptr)
{
    SymbolFile file;
    file.sets.push_back({});
    file.sets[0].symbols = symbols;
    const SymbolEncodeResult r = Codec(id).Encode(file, {});
    if (diagnostics)
        *diagnostics = r.diagnostics;
    return std::string(r.bytes.begin(), r.bytes.end());
}

/// The fields a format holds (provenance differs between two reads)
void ExpectSame(const std::vector<Symbol>& a, const std::vector<Symbol>& b, const char* id)
{
    ASSERT_EQ(a.size(), b.size()) << id;
    for (size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_EQ(a[i].name, b[i].name) << id;
        EXPECT_EQ(a[i].location, b[i].location) << id << " " << a[i].name;
        EXPECT_EQ(a[i].kind, b[i].kind) << id << " " << a[i].name;
        EXPECT_EQ(a[i].provenance.type, b[i].provenance.type) << id << " " << a[i].name;
        EXPECT_EQ(a[i].comment, b[i].comment) << id << " " << a[i].name;
    }
}

Symbol Paged(const char* name, const char* space, uint32_t offset)
{
    Symbol s;
    s.name = name;
    AddressSpace::Parse(space, s.location.space);
    s.location.offset = offset;
    return s;
}
}  // namespace

TEST(LabelFileCodecs_Test, UnrealMap)
{
    Diagnostics d;
    const std::vector<Symbol> s = Read("unreal-map",
                                       "--- banner ---\n0000  START  (CODE) ; entry\nROM1:0010  ROMCALL  (CODE)\nRAM2:C000  PAGED  (DATA) ; paged\n"
                                       "RAM7:4100  WIN1\nXYZ3:1234  ODD\nRAM300:C000  BIGBANK\n",
                                       &d);
    ASSERT_EQ(s.size(), 6u);
    EXPECT_EQ(s[0].kind, SymbolKind::Code);
    EXPECT_EQ(s[0].comment, "entry");
    EXPECT_EQ(s[1].location.space.Format(), "rom1");
    EXPECT_EQ(s[1].location.offset, 0x10u);
    EXPECT_EQ(s[2].location.space.Format(), "ram2");
    EXPECT_EQ(s[2].location.offset, 0u);
    EXPECT_EQ(s[2].window, 3);
    EXPECT_EQ(s[2].kind, SymbolKind::Data);
    EXPECT_EQ(s[3].window, 1);
    EXPECT_EQ(s[4].location.space.Format(), "cpu:main");   // an unknown bank prefix: the CPU address
    EXPECT_EQ(s[5].location.space.Format(), "ram0");       // past the last page
    ASSERT_EQ(d.size(), 3u);                                // the banner, XYZ3, RAM300
    EXPECT_EQ(d[0].line, 1u);
    EXPECT_EQ(s[1].provenance.line, 3u);
    EXPECT_EQ(Write("unreal-map", {s[0], s[2]}), "0000  START  (CODE) ; entry\nRAM2:C000  PAGED  (DATA) ; paged\n");
    ExpectSame(Read("unreal-map", Write("unreal-map", s)), s, "unreal-map");
}

TEST(LabelFileCodecs_Test, SimpleSymAndFolding)
{
    const std::vector<Symbol> s = Read("simple-sym", "; simple symbols\n1000 START\n1003 LOOP (data) ; loop\nABCD MIX (bss)\nFFFF TOP\n");
    ASSERT_EQ(s.size(), 4u);
    EXPECT_EQ(s[1].kind, SymbolKind::Data);
    EXPECT_EQ(s[2].kind, SymbolKind::Unknown);
    EXPECT_EQ(s[2].provenance.type, "bss");                 // a type that is no kind is kept
    EXPECT_EQ(s[3].location.offset, 0xFFFFu);                // #FFFF is an address like any other
    ExpectSame(Read("simple-sym", Write("simple-sym", s)), s, "simple-sym");
    // A page symbol in a format without pages: the CPU address of its window, reported
    Diagnostics d;
    const std::string text = Write("simple-sym", {Paged("R", "rom2", 0x3D2F), Paged("P", "ram3", 0x10), Paged("V", "vram", 1)}, &d);
    EXPECT_NE(text.find("3D2F R\n"), std::string::npos);
    EXPECT_NE(text.find("C010 P\n"), std::string::npos);
    EXPECT_EQ(text.find(" V"), std::string::npos);
    ASSERT_EQ(d.size(), 2u);                                 // V skipped, two folded
    EXPECT_EQ(d[1].severity, Severity::Info);
}

TEST(LabelFileCodecs_Test, UnrealUserL)
{
    Diagnostics d;
    const std::vector<Symbol> s = Read("unreal-l", "4000 BASIC\n05:C010 SCREEN_LINE\n0000 with blanks in it\nbad line\n", &d);
    ASSERT_EQ(s.size(), 3u);
    EXPECT_EQ(s[0].location.space.Format(), "ram1");        // the linear RAM address: page 1, offset 0
    EXPECT_EQ(s[0].location.offset, 0u);
    EXPECT_EQ(s[1].location.space.Format(), "ram5");
    EXPECT_EQ(s[1].location.offset, 0x10u);
    EXPECT_EQ(s[2].name, "with blanks in it");
    EXPECT_EQ(d.size(), 1u);
    EXPECT_EQ(Write("unreal-l", {s[1]}), "05:0010 SCREEN_LINE\n");   // as sjasmplus' LABELSLIST: the offset in the page
    const std::string text = Write("unreal-l", {Paged("CPU", "cpu:main", 0x8000)}, &d);
    EXPECT_TRUE(text.empty());                               // user.l has RAM pages only
    EXPECT_EQ(d.size(), 1u);
    ExpectSame(Read("unreal-l", Write("unreal-l", s)), s, "unreal-l");
}

TEST(LabelFileCodecs_Test, ViceSjasmZ88dk)
{
    Diagnostics d;
    std::vector<Symbol> s = Read("vice", "# VICE labels\nal C:0810 .start\nal C:C000 .player (DATA)\nal 1234 .noprefix\nal C:ZZZZ .bad\n", &d);
    ASSERT_EQ(s.size(), 3u);
    EXPECT_EQ(s[0].name, "start");                           // the dot is VICE's syntax
    EXPECT_EQ(s[1].kind, SymbolKind::Data);
    EXPECT_EQ(d.size(), 1u);
    EXPECT_EQ(Write("vice", {s[0]}), "al C:0810 .start\n");

    s = Read("sjasm-equ", "START EQU $8000\nDATA_PTR EQU $C000 ; (DATA)\nCONSTVAL EQU 0x0018 ; plain comment\nLABEL: EQU 0x0000C010\nbad EQU $XYZ\n", &d);
    ASSERT_EQ(s.size(), 4u);
    EXPECT_EQ(s[1].kind, SymbolKind::Data);
    EXPECT_EQ(s[2].comment, "plain comment");
    EXPECT_EQ(s[3].name, "LABEL");                           // sjasmplus writes NAME: EQU 0x0000HHHH
    EXPECT_EQ(s[3].location.offset, 0xC010u);
    EXPECT_EQ(d.size(), 1u);
    EXPECT_EQ(Write("sjasm-equ", {s[1]}), "DATA_PTR EQU $C000 ; (DATA)\n");
    ExpectSame(Read("sjasm-equ", Write("sjasm-equ", s)), s, "sjasm-equ");

    s = Read("z88dk-defc", "DEFC main = $8000\nDEFC buffer = $C000 ; (DATA)\ndefc lower = $1234\nDEFC noeq $1\n", &d);
    ASSERT_EQ(s.size(), 3u);
    EXPECT_EQ(s[2].name, "lower");
    EXPECT_EQ(d.size(), 1u);
    EXPECT_EQ(Write("z88dk-defc", {s[1]}), "DEFC buffer                          = $C000 ; (DATA)\n");   // z80asm -g's layout
    ExpectSame(Read("z88dk-defc", Write("z88dk-defc", s)), s, "z88dk-defc");
}

TEST(LabelFileCodecs_Test, Detection)
{
    const SymbolCodecRegistry& r = SymbolCodecRegistry::Builtin();
    auto chosen = [&](const std::string& text, const char* extension) {
        const SymbolDetectResult d = r.Detect(Bytes(text), extension);
        return d.chosen ? d.chosen->Info().id : "none: " + d.reason;
    };
    EXPECT_EQ(chosen("al C:0810 .start\nal C:0820 .loop\nal C:0830 .end\n", ""), "vice");
    EXPECT_EQ(chosen("A EQU $8000\nB EQU $8001\nC EQU $8002\n", ""), "sjasm-equ");
    EXPECT_EQ(chosen("DEFC a = $8000\nDEFC b = $8001\nDEFC c = $8002\n", ""), "z88dk-defc");
    EXPECT_EQ(chosen("0000 START (CODE)\n0008 ERROR-1 (CODE)\n0010 PRINT-A-1 (CODE)\n", ""), "unreal-map");
    EXPECT_EQ(chosen("05:0000 A\n05:0010 B\n07:0000 C\n", ""), "unreal-l");
    EXPECT_EQ(chosen(R"({"format": "unreal-symbols", "sets": []})", ""), "native");
    // "HHHH NAME" alone fits three formats: the extension decides, without one nothing is chosen
    const std::string plain = "1000 START\n1003 LOOP\n2000 INIT\n";
    EXPECT_EQ(chosen(plain, "sym"), "simple-sym");
    EXPECT_EQ(chosen(plain, "map"), "unreal-map");
    EXPECT_EQ(chosen(plain, "l"), "unreal-l");
    EXPECT_EQ(chosen(plain, "").rfind("none", 0), 0u);
}
