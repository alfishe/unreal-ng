// Normalization (DT-1, symbols/tdd.md §5) and the merge policies (DT-2) through the store; readers keep the index they
// took while the store changes

#include <gtest/gtest.h>

#include "unrealasm/symbols/store.h"

using namespace unrealasm;
using namespace unrealasm::symbols;

namespace
{
Symbol Named(const char* name, uint32_t offset, SymbolKind kind = SymbolKind::Unknown, uint32_t line = 0)
{
    Symbol s;
    s.name = name;
    s.location.offset = offset;
    s.kind = kind;
    s.provenance.line = line;
    return s;
}

AddressSpace Space(const char* text)
{
    AddressSpace s;
    EXPECT_TRUE(AddressSpace::Parse(text, s));
    return s;
}
}  // namespace

TEST(SymbolStore_Test, NormalizeSpacesBaseAndChecks)
{
    Symbol paged = Named("PAGED", 0x10);
    paged.location.space = Space("rom2");
    std::vector<Symbol> records = {Named("CPU", 0x4000), Named("LINES", 24, SymbolKind::Const), paged, Named("", 1, SymbolKind::Code, 4),
                                   Named("TWICE", 1, SymbolKind::Unknown, 5), Named("TWICE", 1, SymbolKind::Code, 6), Named("TWICE", 2, SymbolKind::Code, 7)};
    Diagnostics diagnostics;
    std::vector<Symbol> out = Normalize(records, {}, diagnostics);
    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(out[0].location.space.Format(), "cpu:main");
    EXPECT_EQ(out[1].location.space.Format(), "const");   // a constant without a space of its own
    EXPECT_EQ(out[2].location.space.Format(), "rom2");    // its own page is kept
    EXPECT_EQ(out[3].kind, SymbolKind::Code);             // the duplicate at the same place filled the kind
    ASSERT_EQ(diagnostics.size(), 2u);                    // no name (line 4), TWICE at another place (line 7)
    EXPECT_EQ(diagnostics[0].line, 4u);
    EXPECT_EQ(diagnostics[1].line, 7u);

    // --page ram3 --base 0x100: records without a page go to RAM 3; an offset past the page is dropped
    ImportOptions options;
    options.space = Space("ram3");
    options.base = 0x100;
    diagnostics.clear();
    out = Normalize({Named("A", 0), Named("B", 0x3F00), paged}, options, diagnostics);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].location.space.Format(), "ram3");
    EXPECT_EQ(out[0].location.offset, 0x100u);
    EXPECT_EQ(out[1].location.space.Format(), "rom2");
    EXPECT_EQ(out[1].location.offset, 0x110u);
    EXPECT_EQ(diagnostics.size(), 1u);
}

TEST(SymbolStore_Test, MergePolicies)
{
    auto fresh = [] {
        SymbolSet set;
        set.id = "s";
        set.symbols = {Named("SCORE", 0x5B00, SymbolKind::Data), Named("PLAY", 0xC000)};
        return set;
    };
    const std::vector<Symbol> incoming = {Named("SCORE", 0x5C00, SymbolKind::Data, 1), Named("MUSIC", 0xC000, SymbolKind::Code, 2),
                                          Named("NEW", 0x6000, SymbolKind::Code, 3), Named("PLAY", 0xC000, SymbolKind::Code, 4)};
    {
        SymbolSet set = fresh();
        ImportReport report;
        ASSERT_TRUE(Merge(set, incoming, MergePolicy::Both, report));
        EXPECT_EQ(report.added, 1u);
        EXPECT_EQ(report.aliased, 1u);
        EXPECT_EQ(report.updated, 1u);                     // PLAY got its kind
        ASSERT_EQ(report.conflicts.size(), 2u);
        EXPECT_EQ(report.conflicts[0].type, Conflict::Type::Moved);
        EXPECT_EQ(report.conflicts[0].resolution, "kept");
        EXPECT_EQ(report.conflicts[0].line, 1u);
        EXPECT_EQ(report.conflicts[1].other, "PLAY");
        EXPECT_EQ(set.symbols[0].location.offset, 0x5B00u);
        EXPECT_EQ(set.symbols[1].aliases, std::vector<std::string>{"MUSIC"});
        EXPECT_EQ(set.symbols[1].kind, SymbolKind::Code);
        EXPECT_EQ(set.symbols.size(), 3u);
    }
    {
        SymbolSet set = fresh();
        ImportReport report;
        ASSERT_TRUE(Merge(set, incoming, MergePolicy::Keep, report));
        EXPECT_EQ(report.skipped, 2u);
        EXPECT_TRUE(set.symbols[1].aliases.empty());
    }
    {
        SymbolSet set = fresh();
        ImportReport report;
        ASSERT_TRUE(Merge(set, incoming, MergePolicy::Replace, report));
        EXPECT_EQ(set.symbols[0].location.offset, 0x5C00u);   // SCORE moved
        EXPECT_EQ(set.symbols[1].name, "PLAY");               // MUSIC replaced PLAY, then record 4 replaced MUSIC
        EXPECT_EQ(set.symbols[1].kind, SymbolKind::Code);
        EXPECT_EQ(set.symbols.size(), 3u);
    }
    {
        SymbolSet set = fresh();
        ImportReport report;
        EXPECT_FALSE(Merge(set, incoming, MergePolicy::Fail, report));
        EXPECT_EQ(set, fresh());                              // unchanged
        EXPECT_EQ(report.conflicts.size(), 2u);
    }
}

TEST(SymbolStore_Test, StoreSetsAndIndexSnapshots)
{
    SymbolStore store;
    ImportOptions options;
    options.set = "game.sym";
    options.origin = {"file", "game.sym", ""};
    ImportReport report = store.Import({Named("START", 0x8000, SymbolKind::Code)}, options);
    ASSERT_TRUE(report.ok);
    EXPECT_EQ(report.set, "game.sym");
    const std::shared_ptr<const SymbolIndex> before = store.Index();
    ASSERT_NE(before->Find("START"), nullptr);

    report = store.Import({Named("LOOP", 0x8010, SymbolKind::Code)}, options);
    EXPECT_EQ(report.added, 1u);
    EXPECT_EQ(store.GetSet("game.sym")->symbols.size(), 2u);
    EXPECT_EQ(before->Find("LOOP"), nullptr);              // the old snapshot is unchanged and still valid
    EXPECT_NE(store.Index()->Find("LOOP"), nullptr);

    ASSERT_TRUE(store.SetEnabled("game.sym", false));
    EXPECT_EQ(store.Index()->Find("START"), nullptr);
    ASSERT_TRUE(store.SetEnabled("game.sym", true));
    ASSERT_TRUE(store.SetPriority("game.sym", 5));
    EXPECT_EQ(store.GetSet("game.sym")->priority, 5);
    EXPECT_FALSE(store.Drop("missing"));
    ASSERT_TRUE(store.Drop("game.sym"));
    EXPECT_TRUE(store.Sets().empty());
    EXPECT_NE(before->Find("START"), nullptr);

    // Fail leaves the store as it was
    store.Import({Named("A", 1)}, options);
    options.policy = MergePolicy::Fail;
    report = store.Import({Named("A", 2)}, options);
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(store.Index()->Find("A")->location.offset, 1u);
}
