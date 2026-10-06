// Address spaces and kinds: every spelling of symbols/architecture.md §3.1 parses and formats back the same

#include <gtest/gtest.h>

#include "unrealasm/symbols/symbol.h"

using namespace unrealasm::symbols;

TEST(Symbol_Test, SpacesParseAndFormat)
{
    for (const char* text : {"cpu:main", "cpu:gs", "rom0", "rom2", "ram3", "ram255", "cache0", "vram", "cram", "eeprom", "const",
                             "port", "gs.rom0", "gs.ram1", "gs.vram"})
    {
        AddressSpace space;
        ASSERT_TRUE(AddressSpace::Parse(text, space)) << text;
        EXPECT_EQ(space.Format(), text);
    }
    AddressSpace ram3;
    ASSERT_TRUE(AddressSpace::Parse("ram3", ram3));
    EXPECT_EQ(ram3.kind, SpaceKind::Ram);
    EXPECT_EQ(ram3.page, 3);
    EXPECT_EQ(ram3.cpu, "main");
    EXPECT_EQ(ram3.Extent(), 0x4000u);
    AddressSpace gs;
    ASSERT_TRUE(AddressSpace::Parse("gs.rom0", gs));
    EXPECT_EQ(gs.cpu, "gs");
    EXPECT_EQ(gs.kind, SpaceKind::Rom);
    for (const char* bad : {"", "cpu:", "rom", "ram65536", "ram-1", ".rom0", "a b", "cpu"})
    {
        AddressSpace space;
        EXPECT_FALSE(AddressSpace::Parse(bad, space)) << bad;
    }
}

TEST(Symbol_Test, Kinds)
{
    SymbolKind kind = SymbolKind::Unknown;
    for (const SymbolKind k : {SymbolKind::Code, SymbolKind::Data, SymbolKind::Const, SymbolKind::Port, SymbolKind::Entry, SymbolKind::Local})
    {
        ASSERT_TRUE(ParseKind(KindName(k), kind));
        EXPECT_EQ(kind, k);
    }
    EXPECT_TRUE(ParseKind("CODE", kind));
    EXPECT_EQ(kind, SymbolKind::Code);
    EXPECT_FALSE(ParseKind("function", kind));
}
