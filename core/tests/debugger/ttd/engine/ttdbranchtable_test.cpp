/// @file ttdbranchtable_test.cpp
/// @brief The branch table (debugger/ttd/engine/ttdbranchtable.h): identities,
/// names, metadata, delete rules, lineage and the lookups the timeline and the
/// automation planes use. Cases T-B1...T-B8 of
/// docs/inprogress/2026-11-08-ttd-branches/test-strategy.md §2.1.

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "debugger/ttd/engine/ttdbranchtable.h"

using namespace ttd;

namespace
{
TTDPosition At(uint64_t frame, uint64_t t = 0)
{
    TTDPosition p;
    p.frame = frame;
    p.tInFrame = t;
    return p;
}

TTDBranchMeta Named(const std::string& name)
{
    TTDBranchMeta m;
    m.name = name;
    return m;
}

/// The worked example of ttdbranchtable.h: trunk to 300, branch 1 at {120, 500} to 180, branch 2 at {150, 0} on 1
struct Tree
{
    TTDBranchTable table;
    uint32_t one = 0, two = 0;

    Tree()
    {
        table.Reset(TTDNewGuid());
        table.SetEnd(kTTDTrunk, At(300));
        std::string error;
        one = table.Create(kTTDTrunk, At(120, 500), TTDNewGuid(), Named("resume 1"), error).value();
        table.SetEnd(one, At(180));
        two = table.Create(one, At(150), TTDNewGuid(), Named("resume 2"), error).value();
        table.SetEnd(two, At(200));
    }
};
}  // namespace

/// T-B1: lookups by id, GUID and name; names unique among live branches
TEST(TTDBranchTable_Test, LookupsAndUniqueNames)
{
    Tree t;
    EXPECT_EQ(t.table.Count(), 3u);
    const TTDBranchInfo* one = t.table.Get(t.one);
    ASSERT_NE(one, nullptr);
    EXPECT_EQ(t.table.FindGuid(one->guid), one);
    EXPECT_EQ(t.table.FindName("resume 1"), one);
    EXPECT_EQ(t.table.FindName("trunk"), &t.table.Trunk());
    EXPECT_EQ(t.table.FindName("nothing"), nullptr);

    std::string error;
    const uint32_t clash = t.table.Create(kTTDTrunk, At(10), TTDNewGuid(), Named("resume 1"), error).value();
    EXPECT_EQ(t.table.Get(clash)->meta.name, "resume 1 (2)");
    EXPECT_FALSE(t.table.Rename(clash, "resume 1", error)) << "a name is unique";
    EXPECT_TRUE(t.table.Rename(clash, "boss", error)) << error;
    EXPECT_EQ(t.table.FindName("boss")->id, clash);
    EXPECT_EQ(t.table.FindName("resume 1 (2)"), nullptr) << "the old name is free again";

    EXPECT_FALSE(t.table.Create(kTTDTrunk, At(5), one->guid, Named("x"), error)) << "a GUID is unique";

    TTDGuid parsed{};
    ASSERT_TRUE(TTDGuidFromString(TTDGuidToString(one->guid), parsed));
    EXPECT_EQ(parsed, one->guid);
    EXPECT_FALSE(TTDGuidFromString("12", parsed));
    EXPECT_FALSE(TTDGuidFromString(std::string(33, 'a'), parsed));
    EXPECT_NE(TTDNewGuid(), TTDNewGuid());
}

/// T-B2: ids are never reused; the limit refuses with the prune hint
TEST(TTDBranchTable_Test, IdsAreNeverReusedAndTheLimitRefuses)
{
    Tree t;
    std::vector<uint32_t> removed;
    std::string error;
    ASSERT_TRUE(t.table.Delete(t.two, {}, false, removed, error)) << error;
    const uint32_t next = t.table.Create(kTTDTrunk, At(50), TTDNewGuid(), {}, error).value();
    EXPECT_EQ(next, 3u) << "id 2 stays unused";
    EXPECT_EQ(t.table.Get(t.two), nullptr);

    TTDBranchTable full;
    for (uint32_t i = 1; i < kTTDMaxBranches; ++i)
        ASSERT_TRUE(full.Create(kTTDTrunk, At(0), TTDNewGuid(), {}, error)) << i << ": " << error;
    EXPECT_FALSE(full.Create(kTTDTrunk, At(0), TTDNewGuid(), {}, error));
    EXPECT_NE(error.find("branch-prune"), std::string::npos) << error;
}

/// T-B3: lineage spans and the owner of a position, at the forks' edges
TEST(TTDBranchTable_Test, LineageAndOwner)
{
    Tree t;
    const std::vector<TTDLineageSpan> lineage = t.table.Lineage(t.two);
    ASSERT_EQ(lineage.size(), 3u);
    EXPECT_EQ(lineage[0].branch, kTTDTrunk);
    EXPECT_EQ(lineage[1].branch, t.one);
    EXPECT_EQ(lineage[2].branch, t.two);
    EXPECT_TRUE(lineage[0].to.frame == 120 && lineage[0].to.tInFrame == 500);
    EXPECT_EQ(lineage[2].to.frame, 200u);

    EXPECT_EQ(TTDBranchTable::OwnerOf(lineage, At(0)), kTTDTrunk);
    EXPECT_EQ(TTDBranchTable::OwnerOf(lineage, At(120, 499)), kTTDTrunk) << "before the fork: the parent's";
    EXPECT_EQ(TTDBranchTable::OwnerOf(lineage, At(120, 500)), t.one) << "at the fork: the branch's own";
    EXPECT_EQ(TTDBranchTable::OwnerOf(lineage, At(149, 70000)), t.one);
    EXPECT_EQ(TTDBranchTable::OwnerOf(lineage, At(150)), t.two);
    EXPECT_EQ(TTDBranchTable::OwnerOf(lineage, At(1000)), t.two);

    EXPECT_TRUE(t.table.IsInLineage(kTTDTrunk, t.two));
    EXPECT_TRUE(t.table.IsInLineage(t.one, t.two));
    EXPECT_FALSE(t.table.IsInLineage(t.two, t.one));

    // A deep lineage: 50 levels
    TTDBranchTable deep;
    deep.SetEnd(kTTDTrunk, At(10000));
    uint32_t last = kTTDTrunk;
    std::string error;
    for (uint64_t i = 1; i <= 50; ++i)
    {
        last = deep.Create(last, At(i * 100), TTDNewGuid(), {}, error).value();
        deep.SetEnd(last, At(i * 100 + 150));
    }
    const std::vector<TTDLineageSpan> long_ = deep.Lineage(last);
    ASSERT_EQ(long_.size(), 51u);
    for (uint64_t i = 0; i <= 50; ++i)
        EXPECT_EQ(TTDBranchTable::OwnerOf(long_, At(i * 100 + 50)), long_[i].branch) << i;

    // A fork must lie in the parent's own history
    EXPECT_FALSE(t.table.Create(t.two, At(140), TTDNewGuid(), {}, error)) << "before branch 2's fork: branch 1's";
    EXPECT_FALSE(t.table.Create(t.one, At(181), TTDNewGuid(), {}, error)) << "after branch 1's end";
}

/// T-B4: children in a frame range (the fork marks on a visible part of the timeline), several at one position
TEST(TTDBranchTable_Test, ChildrenInARange)
{
    TTDBranchTable table;
    table.SetEnd(kTTDTrunk, At(1000));
    std::string error;
    std::vector<uint32_t> ids;
    for (uint64_t f : {500, 100, 300, 300, 900})
        ids.push_back(table.Create(kTTDTrunk, At(f), TTDNewGuid(), {}, error).value());
    std::vector<uint32_t> out;
    table.ChildrenIn(kTTDTrunk, 0, 1000, out);
    EXPECT_EQ(out, (std::vector<uint32_t>{ids[1], ids[2], ids[3], ids[0], ids[4]})) << "by fork, then by id";
    table.ChildrenIn(kTTDTrunk, 300, 500, out);
    EXPECT_EQ(out, (std::vector<uint32_t>{ids[2], ids[3], ids[0]}));
    table.ChildrenIn(kTTDTrunk, 301, 499, out);
    EXPECT_TRUE(out.empty());
}

/// T-B5: filters for the branch list and for pruning
TEST(TTDBranchTable_Test, Filters)
{
    TTDBranchTable table;
    table.SetEnd(kTTDTrunk, At(5000));
    std::string error;
    auto make = [&](uint64_t fork, uint64_t len, const char* by, const char* run, std::vector<std::string> tags,
                    uint64_t micros, bool pinned) {
        TTDBranchMeta m;
        m.createdBy = by;
        m.run = run;
        m.tags = std::move(tags);
        m.createdHostMicros = micros;
        m.pinned = pinned;
        const uint32_t id = table.Create(kTTDTrunk, At(fork), TTDNewGuid(), m, error).value();
        table.SetEnd(id, At(fork + len));
        return id;
    };
    const uint32_t a = make(100, 200, "analyzer", "run-7", {"exp", "fire"}, 10, false);
    const uint32_t b = make(200, 20, "analyzer", "run-7", {"exp"}, 20, true);
    const uint32_t c = make(300, 2000, "mcp:claude", "", {"fire", "fire"}, 30, false);

    TTDBranchFilter f;
    f.run = "run-7";
    EXPECT_EQ(table.Select(f), (std::vector<uint32_t>{a, b}));
    f = {};
    f.createdBy = "mcp";
    EXPECT_EQ(table.Select(f), (std::vector<uint32_t>{c}));
    f = {};
    f.tags = {"fire"};
    EXPECT_EQ(table.Select(f), (std::vector<uint32_t>{a, c}));
    EXPECT_EQ(table.Get(c)->meta.tags, (std::vector<std::string>{"fire"})) << "tags unique";
    f = {};
    f.run = "run-7";
    f.pinned = false;
    EXPECT_EQ(table.Select(f), (std::vector<uint32_t>{a}));
    f = {};
    f.maxLength = 199;
    f.includeTrunk = false;
    EXPECT_EQ(table.Select(f), (std::vector<uint32_t>{b}));
    f = {};
    f.createdAfter = 15;
    f.createdBefore = 30;
    EXPECT_EQ(table.Select(f), (std::vector<uint32_t>{b}));
    f = {};
    f.run = "none";
    EXPECT_TRUE(table.Select(f).empty());

    ASSERT_TRUE(table.UpdateTags(a, nullptr, {"best", "exp"}, {"fire"}));
    EXPECT_EQ(table.Get(a)->meta.tags, (std::vector<std::string>{"best", "exp"}));
    const std::vector<std::string> only{"x"};
    ASSERT_TRUE(table.UpdateTags(a, &only, {}, {}));
    EXPECT_EQ(table.Get(a)->meta.tags, only);
}

/// T-B6: delete rules
TEST(TTDBranchTable_Test, DeleteRules)
{
    Tree t;
    std::vector<uint32_t> removed;
    std::string error;
    EXPECT_FALSE(t.table.Delete(kTTDTrunk, {}, false, removed, error)) << "the trunk stays";
    EXPECT_FALSE(t.table.Delete(t.one, {kTTDTrunk, t.one, t.two}, false, removed, error))
        << "a branch of the active lineage";
    EXPECT_FALSE(t.table.Delete(t.one, {t.two}, false, removed, error)) << "the active branch among the descendants";
    t.table.SetPinned(t.two, true);
    EXPECT_FALSE(t.table.Delete(t.one, {}, false, removed, error)) << "a pinned descendant";
    EXPECT_NE(error.find("resume 2"), std::string::npos) << error;

    ASSERT_TRUE(t.table.Delete(t.one, {}, true, removed, error)) << error;
    EXPECT_EQ(removed, (std::vector<uint32_t>{t.two, t.one})) << "children first";
    EXPECT_EQ(t.table.Count(), 1u);
    EXPECT_FALSE(t.table.HasBranches());
    EXPECT_TRUE(t.table.Trunk().children.empty());
    EXPECT_EQ(t.table.FindName("resume 1"), nullptr);
    EXPECT_FALSE(t.table.Delete(t.one, {}, false, removed, error)) << "already gone";
}

/// T-B7: metadata changes are seen by the lookups and the filters at once
TEST(TTDBranchTable_Test, MetadataUpdates)
{
    Tree t;
    std::string error;
    ASSERT_TRUE(t.table.SetNote(t.one, "the boss dies here"));
    ASSERT_TRUE(t.table.SetPinned(t.one, true));
    EXPECT_EQ(t.table.Get(t.one)->meta.note, "the boss dies here");
    TTDBranchFilter f;
    f.pinned = true;
    EXPECT_EQ(t.table.Select(f), (std::vector<uint32_t>{t.one}));
    EXPECT_EQ(t.table.UniqueName("resume 2"), "resume 2 (2)");
    EXPECT_EQ(t.table.UniqueName("fresh"), "fresh");
    // The counters go on from the names there ("resume 1", "resume 2": a loaded session's table)
    EXPECT_EQ(t.table.NextCounter("resume"), 3u);
    EXPECT_EQ(t.table.NextCounter("resume"), 4u);
    EXPECT_EQ(t.table.NextCounter("edit"), 1u);
    EXPECT_EQ(t.table.NextCounter("resum"), 1u) << "a name's kind is the whole word";
}

/// T-B8: a session that never forks builds no index
TEST(TTDBranchTable_Test, NoIndexWithoutBranches)
{
    TTDBranchTable table;
    const TTDGuid trunk = TTDNewGuid();
    table.Reset(trunk);
    EXPECT_FALSE(table.Indexed());
    EXPECT_FALSE(table.HasBranches());
    EXPECT_EQ(table.FindGuid(trunk), &table.Trunk());
    EXPECT_EQ(table.Lineage(kTTDTrunk).size(), 1u);
    std::string error;
    ASSERT_TRUE(table.Create(kTTDTrunk, At(0), TTDNewGuid(), {}, error)) << error;
    EXPECT_TRUE(table.Indexed());
}
