// The cross assemblers' symbol files, on what sjasmplus 1.24 and pasmo 0.5.5 wrote for testdata/symbols/*/labels.asm:
// every label read with its page, kind and scope where the format has them; the --sym and pasmo files written back
// byte for byte; SLD and the listing round-trip what they hold; each format detected from its content

#include <gtest/gtest.h>

#include <map>

#include "testdata.h"
#include "unrealasm/symbols/codec.h"

using namespace unrealasm;
using namespace unrealasm::symbols;
using unrealasm::testing::ReadTestData;

namespace
{
const ISymbolCodec& Codec(const char* id)
{
    const ISymbolCodec* codec = SymbolCodecRegistry::Builtin().Find(id);
    EXPECT_NE(codec, nullptr) << id;
    return *codec;
}

std::map<std::string, Symbol> ByName(const SymbolDecodeResult& r)
{
    std::map<std::string, Symbol> out;
    for (const SymbolSet& set : r.file.sets)
        for (const Symbol& s : set.symbols)
            out[s.name] = s;
    return out;
}

void ExpectSameHeld(const SymbolFile& a, const SymbolFile& b, const char* id)
{
    ASSERT_EQ(a.sets.size(), b.sets.size());
    ASSERT_EQ(a.sets[0].symbols.size(), b.sets[0].symbols.size()) << id;
    for (size_t i = 0; i < a.sets[0].symbols.size(); ++i)
    {
        const Symbol& x = a.sets[0].symbols[i];
        const Symbol& y = b.sets[0].symbols[i];
        EXPECT_EQ(x.name, y.name) << id;
        EXPECT_EQ(x.location, y.location) << id << " " << x.name;
        EXPECT_EQ(x.kind, y.kind) << id << " " << x.name;
        EXPECT_EQ(x.parent, y.parent) << id << " " << x.name;
        EXPECT_EQ(x.module, y.module) << id << " " << x.name;
        EXPECT_EQ(x.window, y.window) << id << " " << x.name;
    }
}
}  // namespace

TEST(CrossAsmCodecs_Test, SjasmplusSymWritesTheSameBytes)
{
    const std::vector<uint8_t> bytes = ReadTestData("symbols/sjasmplus/labels.sym");
    const SymbolDecodeResult r = Codec("sjasmplus-sym").Decode(bytes);
    ASSERT_TRUE(r.ok);
    const auto s = ByName(r);
    ASSERT_EQ(s.size(), 11u);
    EXPECT_EQ(s.at("player.play.frame").location.offset, 0x8010u);
    EXPECT_EQ(s.at("q?mark").location.offset, 0x800Cu);
    EXPECT_EQ(Codec("sjasmplus-sym").Encode(r.file, {}).bytes, bytes);
    EXPECT_EQ(SymbolCodecRegistry::Builtin().Detect(bytes, "sym").chosen, &Codec("sjasmplus-sym"));
}

TEST(CrossAsmCodecs_Test, SjasmplusSld)
{
    const std::vector<uint8_t> bytes = ReadTestData("symbols/sjasmplus/labels.sld");
    const SymbolDecodeResult r = Codec("sjasmplus-sld").Decode(bytes);
    ASSERT_TRUE(r.ok);
    const auto s = ByName(r);
    ASSERT_EQ(s.size(), 11u);   // the module brackets are no symbols
    EXPECT_EQ(s.at("SCREEN").kind, SymbolKind::Const);
    EXPECT_EQ(s.at("SCREEN").location.offset, 0x4000u);
    EXPECT_EQ(s.at("start").kind, SymbolKind::Code);              // an instruction was traced at it
    EXPECT_EQ(s.at("data_tab").kind, SymbolKind::Data);           // nothing traced: data
    EXPECT_EQ(s.at("start").location.space.Format(), "ram2");     // #8000 is page 2 on the 128K device
    EXPECT_EQ(s.at("start").window, 2);
    EXPECT_EQ(s.at("start").source.line, 6u);
    EXPECT_EQ(s.at("start").source.file, "labels.asm");
    EXPECT_EQ(s.at("start.loop").kind, SymbolKind::Local);
    EXPECT_EQ(s.at("start.loop").parent, "start");
    EXPECT_EQ(s.at("player.play.frame").parent, "player.play");
    EXPECT_EQ(s.at("player.play.frame").module, "player");
    EXPECT_EQ(s.at("paged1").location.space.Format(), "ram1");
    EXPECT_EQ(s.at("paged1").location.offset, 0u);
    EXPECT_EQ(s.at("paged1").window, 3);
    EXPECT_EQ(s.at("paged3").location.space.Format(), "ram3");
    EXPECT_EQ(s.at("paged1.inner").location.offset, 1u);
    // Written and read again: every field SLD holds survives
    const SymbolEncodeResult written = Codec("sjasmplus-sld").Encode(r.file, {});
    const SymbolDecodeResult again = Codec("sjasmplus-sld").Decode(written.bytes);
    ExpectSameHeld(r.file, again.file, "sjasmplus-sld");
    EXPECT_EQ(SymbolCodecRegistry::Builtin().Detect(bytes, "").chosen, &Codec("sjasmplus-sld"));
}

TEST(CrossAsmCodecs_Test, SjasmplusListing)
{
    const std::vector<uint8_t> bytes = ReadTestData("symbols/sjasmplus/labels.lst");
    const SymbolDecodeResult r = Codec("sjasmplus-lst").Decode(bytes);
    ASSERT_TRUE(r.ok);
    const auto s = ByName(r);
    ASSERT_EQ(s.size(), 11u);
    // The same names and values as the --sym file sjasmplus wrote
    const auto sym = ByName(Codec("sjasmplus-sym").Decode(ReadTestData("symbols/sjasmplus/labels.sym")));
    for (const auto& [name, symbol] : sym)
    {
        ASSERT_TRUE(s.count(name)) << name;
        EXPECT_EQ(s.at(name).location.offset, symbol.location.offset) << name;
    }
    EXPECT_EQ(s.at("SCREEN").kind, SymbolKind::Const);
    EXPECT_EQ(s.at("start").kind, SymbolKind::Code);
    EXPECT_EQ(s.at("data_tab").kind, SymbolKind::Data);
    EXPECT_EQ(s.at("player.play.frame").parent, "player.play");
    EXPECT_EQ(s.at("start").source.file, "labels.asm");
    EXPECT_EQ(s.at("start").provenance.line, 7u);
    // A listing written from the symbols reads back to the same names and values
    const SymbolDecodeResult again = Codec("sjasmplus-lst").Decode(Codec("sjasmplus-lst").Encode(r.file, {}).bytes);
    const auto s2 = ByName(again);
    ASSERT_EQ(s2.size(), s.size());
    for (const auto& [name, symbol] : s)
        EXPECT_EQ(s2.at(name).location, symbol.location) << name;
    EXPECT_EQ(SymbolCodecRegistry::Builtin().Detect(bytes, "").chosen, &Codec("sjasmplus-lst"));
}

TEST(CrossAsmCodecs_Test, PasmoWritesTheSameBytes)
{
    for (const char* file : {"symbols/pasmo/labels.symbol", "symbols/pasmo/labels.pub"})
    {
        const std::vector<uint8_t> bytes = ReadTestData(file);
        const SymbolDecodeResult r = Codec("pasmo").Decode(bytes);
        ASSERT_TRUE(r.ok) << file;
        EXPECT_EQ(Codec("pasmo").Encode(r.file, {}).bytes, bytes) << file;
        EXPECT_EQ(SymbolCodecRegistry::Builtin().Detect(bytes, "").chosen, &Codec("pasmo")) << file;
    }
    const auto s = ByName(Codec("pasmo").Decode(ReadTestData("symbols/pasmo/labels.symbol")));
    ASSERT_EQ(s.size(), 7u);
    EXPECT_EQ(s.at("play").location.offset, 0x800Cu);
    EXPECT_EQ(s.at("00000000").location.offset, 0x800Eu);   // pasmo names a PROC's LOCAL label itself
}
