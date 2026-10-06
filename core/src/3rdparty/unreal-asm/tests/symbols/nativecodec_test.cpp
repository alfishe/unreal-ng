// The native symbol file: every field survives encode -> decode, the bytes are stable, members this version does not
// know are written back, the formats.md §5 example reads, and broken files report instead of crashing

#include <gtest/gtest.h>

#include "symbols/codecs/native/nativecodec.h"
#include "unrealasm/symbols/codec.h"

using namespace unrealasm;
using namespace unrealasm::symbols;

namespace
{
std::vector<uint8_t> Bytes(const std::string& s)
{
    return std::vector<uint8_t>(s.begin(), s.end());
}

SymbolFile Sample()
{
    SymbolFile file;
    SymbolSet set;
    set.id = "game.sym";
    set.title = "Game symbols (sjasmplus)";
    set.origin = {"file", "game.sym", "3f1c"};
    set.priority = 150;
    set.caseRule = CaseRule::Fold;
    Symbol play;
    play.name = "PLAYMUS";
    AddressSpace::Parse("ram3", play.location.space);
    play.kind = SymbolKind::Code;
    play.size = 412;
    play.module = "music";
    play.source = {"music.asm", 12, 3};
    play.comment = "plays one frame; \"quoted\"";
    play.aliases = {"MUS_FRAME"};
    play.provenance = {"sjasmplus-sld", "|music.asm|12||3|49152|F|PLAYMUS", 7};
    Symbol loop;
    loop.name = "PLAYMUS.loop";
    AddressSpace::Parse("ram3", loop.location.space);
    loop.location.offset = 7;
    loop.kind = SymbolKind::Local;
    loop.parent = "PLAYMUS";
    loop.enabled = false;
    Symbol russian;
    russian.name = "\xD0\x9C\xD0\x95\xD0\x9D\xD0\xAE";   // МЕНЮ
    AddressSpace::Parse("gs.rom0", russian.location.space);
    russian.location.offset = 0x123;
    set.symbols = {play, loop, russian};
    SymbolSet second;
    second.id = "rom:48k";
    second.enabled = false;
    file.sets = {set, second};
    return file;
}
}  // namespace

TEST(NativeCodec_Test, RoundTripIsIdentity)
{
    const codecs::NativeCodec codec;
    const SymbolFile file = Sample();
    const SymbolEncodeResult encoded = codec.Encode(file, {});
    ASSERT_TRUE(encoded.ok);
    EXPECT_EQ(encoded.written, 3u);
    const SymbolDecodeResult decoded = codec.Decode(encoded.bytes);
    ASSERT_TRUE(decoded.ok) << (decoded.diagnostics.empty() ? "" : decoded.diagnostics[0].message);
    EXPECT_EQ(decoded.file, file);
    EXPECT_EQ(codec.Encode(decoded.file, {}).bytes, encoded.bytes);   // stable bytes (NFR-5)
    // One symbol per line, fields in a fixed order, unknown fields left out
    const std::string text(encoded.bytes.begin(), encoded.bytes.end());
    EXPECT_NE(text.find(R"(        {"name":"PLAYMUS.loop","space":"ram3","offset":7,"kind":"local","scope":{"parent":"PLAYMUS"},"enabled":false})"),
              std::string::npos)
        << text;
    EXPECT_EQ(SymbolCodecRegistry::Builtin().Detect(encoded.bytes, "json").chosen, SymbolCodecRegistry::Builtin().Find("native"));
}

TEST(NativeCodec_Test, ReadsTheDesignExampleAndKeepsUnknownMembers)
{
    const std::string text = R"j({
  "format": "unreal-symbols",
  "version": 1,
  "generator": "unreal-ng 2026-10-05",
  "future": {"x": [1, 2]},
  "sets": [
    {
      "id": "game.sym",
      "title": "Game symbols (sjasmplus)",
      "origin": { "kind": "file", "where": "game.sym", "sha256": "3f1c..." },
      "priority": 100,
      "enabled": true,
      "color": "red",
      "symbols": [
        { "name": "PLAYMUS", "space": "ram3", "offset": 0, "kind": "code", "size": 412,
          "module": "music", "source": { "file": "music.asm", "line": 12 },
          "comment": "plays one frame", "aliases": ["MUS_FRAME"],
          "provenance": { "importer": "sjasmplus-sld", "raw": "|music.asm|12||3|49152|F|PLAYMUS" }, "bank_hint": 3 },
        { "name": "PLAYMUS.loop", "space": "ram3", "offset": 7, "kind": "local",
          "scope": { "parent": "PLAYMUS" } }
      ]
    }
  ]
})j";
    const codecs::NativeCodec codec;
    const SymbolDecodeResult decoded = codec.Decode(Bytes(text));
    ASSERT_TRUE(decoded.ok);
    ASSERT_EQ(decoded.file.sets.size(), 1u);
    const SymbolSet& set = decoded.file.sets[0];
    ASSERT_EQ(set.symbols.size(), 2u);
    EXPECT_EQ(set.symbols[0].size, 412u);
    EXPECT_EQ(set.symbols[0].source.line, 12u);
    EXPECT_EQ(set.symbols[0].extra, R"({"bank_hint":3})");
    EXPECT_EQ(set.symbols[1].parent, "PLAYMUS");
    EXPECT_EQ(set.extra, R"({"color":"red"})");
    EXPECT_EQ(decoded.file.extra, R"({"future":{"x":[1,2]}})");
    // ... and writes them back
    const SymbolDecodeResult again = codec.Decode(codec.Encode(decoded.file, {}).bytes);
    EXPECT_EQ(again.file, decoded.file);
}

TEST(NativeCodec_Test, BrokenFilesReport)
{
    const codecs::NativeCodec codec;
    EXPECT_FALSE(codec.Decode(Bytes("{\"format\":\"something\"}")).ok);
    EXPECT_FALSE(codec.Decode(Bytes("not json")).ok);
    // One bad symbol is reported with its place; the others are read
    const SymbolDecodeResult partly = codec.Decode(Bytes(
        R"({"format":"unreal-symbols","sets":[{"id":"a","symbols":[{"name":"OK","offset":1},{"name":"BAD","offset":-1},{"name":"SP","space":"nowhere!"}]}]})"));
    EXPECT_FALSE(partly.ok);
    ASSERT_EQ(partly.file.sets.size(), 1u);
    ASSERT_EQ(partly.file.sets[0].symbols.size(), 1u);
    EXPECT_EQ(partly.file.sets[0].symbols[0].location.space.Format(), "cpu:main");   // no space: the CPU view
    ASSERT_EQ(partly.diagnostics.size(), 2u);
    EXPECT_NE(partly.diagnostics[0].message.find("a symbol 2"), std::string::npos);
    EXPECT_EQ(codec.Detect({Bytes("{\"format\": \"unreal-symbols\"}"), ""}), 100);
    EXPECT_EQ(codec.Detect({Bytes("al C:4000 .SCREEN"), ""}), 0);
}
