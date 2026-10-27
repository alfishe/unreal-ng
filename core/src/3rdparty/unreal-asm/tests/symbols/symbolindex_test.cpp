// The index: exact and nearest-below lookups per address space, ranges, names and aliases, priorities between sets

#include <gtest/gtest.h>

#include "unrealasm/symbols/index.h"

using namespace unrealasm::symbols;

namespace
{
Location At(const char* space, uint32_t offset)
{
    Location l;
    EXPECT_TRUE(AddressSpace::Parse(space, l.space)) << space;
    l.offset = offset;
    return l;
}

Symbol Named(const char* name, const char* space, uint32_t offset)
{
    Symbol s;
    s.name = name;
    s.location = At(space, offset);
    return s;
}

SymbolIndex Build(std::vector<SymbolSet> sets)
{
    return SymbolIndex(std::make_shared<const std::vector<SymbolSet>>(std::move(sets)));
}
}  // namespace

TEST(SymbolIndex_Test, LookupsBySpace)
{
    SymbolSet set;
    set.id = "game";
    set.symbols = {Named("SCREEN", "cpu:main", 0x4000), Named("PLAYMUS", "ram3", 0), Named("LOOP", "ram3", 7),
                   Named("OTHER", "ram1", 0)};
    set.symbols[1].aliases = {"MUS_FRAME"};
    const SymbolIndex index = Build({set});
    EXPECT_EQ(index.Size(), 4u);
    ASSERT_NE(index.At(At("ram3", 0)), nullptr);
    EXPECT_EQ(index.At(At("ram3", 0))->name, "PLAYMUS");
    EXPECT_EQ(index.At(At("ram1", 0))->name, "OTHER");     // the same offset in another page is another symbol
    EXPECT_EQ(index.At(At("ram3", 1)), nullptr);
    EXPECT_EQ(index.At(At("ram4", 0)), nullptr);
    // PLAYMUS+5, LOOP+3; nothing within reach below the first symbol of a space
    Nearest n = index.NearestBelow(At("ram3", 5), 100);
    ASSERT_NE(n.symbol, nullptr);
    EXPECT_EQ(n.symbol->name, "PLAYMUS");
    EXPECT_EQ(n.distance, 5u);
    n = index.NearestBelow(At("ram3", 10), 100);
    EXPECT_EQ(n.symbol->name, "LOOP");
    EXPECT_EQ(n.distance, 3u);
    EXPECT_EQ(index.NearestBelow(At("ram3", 10), 2).symbol, nullptr);
    EXPECT_EQ(index.NearestBelow(At("cpu:main", 0x3FFF), 100).symbol, nullptr);
    const auto range = index.InRange(At("ram3", 0), 10);
    ASSERT_EQ(range.size(), 2u);
    EXPECT_EQ(range[1]->name, "LOOP");
    ASSERT_NE(index.Find("MUS_FRAME"), nullptr);
    EXPECT_EQ(index.Find("MUS_FRAME")->name, "PLAYMUS");
    EXPECT_EQ(index.Find("playmus"), nullptr);   // exact case by default
}

TEST(SymbolIndex_Test, PrioritiesCaseAndEnabled)
{
    SymbolSet low;
    low.id = "low";
    low.priority = 10;
    low.symbols = {Named("START", "rom0", 0), Named("SHARED", "cpu:main", 0x8000)};
    SymbolSet high;
    high.id = "high";
    high.priority = 200;
    high.caseRule = CaseRule::Fold;
    high.symbols = {Named("RESET", "rom0", 0), Named("Shared", "cpu:main", 0x9000)};
    SymbolSet off;
    off.id = "off";
    off.enabled = false;
    off.symbols = {Named("HIDDEN", "rom0", 0)};
    const SymbolIndex index = Build({low, high, off});
    const auto all = index.AllAt(At("rom0", 0));
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0]->name, "RESET");                  // the higher priority set first
    EXPECT_EQ(index.At(At("rom0", 0))->name, "RESET");
    EXPECT_EQ(index.NearestBelow(At("rom0", 3), 10).symbol->name, "RESET");
    EXPECT_EQ(index.Find("HIDDEN"), nullptr);          // a disabled set is not in the index
    EXPECT_EQ(index.Find("reset")->name, "RESET");     // the Fold set ignores case
    EXPECT_EQ(index.Find("SHARED")->location.offset, 0x9000u);   // the higher priority Fold name wins over the exact one
    // Disabled symbols are skipped as well
    SymbolSet one;
    one.id = "one";
    one.symbols = {Named("A", "cpu:main", 1)};
    one.symbols[0].enabled = false;
    EXPECT_EQ(Build({one}).Size(), 0u);
}
