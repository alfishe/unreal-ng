// The strategy dialog's Qt-free logic: what a composite can take, the preselection, the request options

#include <gtest/gtest.h>

#include <algorithm>

#include "media/core/flattenchoice.h"

namespace
{
    StateNode Layers(const char* build, const char* writesSave, bool writableLayer, const char* descriptor = "/zx/disk.ucompose.yaml")
    {
        StateNode composite = StateNode::Object();
        composite["descriptor"] = descriptor;
        composite["build"] = build;
        composite["writesSave"] = writesSave;
        StateNode layers = StateNode::Array();
        StateNode layer = StateNode::Object();
        layer["name"] = "work";
        layer["writable"] = writableLayer;
        layers.push(layer);
        composite["layers"] = layers;
        StateNode reply = StateNode::Object();
        reply["ok"] = true;
        reply["layers"] = composite;
        return reply;
    }

    const FlattenOption& Option(const std::vector<FlattenOption>& options, FlattenStrategy s)
    {
        return *std::find_if(options.begin(), options.end(), [s](const FlattenOption& o) { return o.strategy == s; });
    }
}  // namespace

TEST(FlattenChoice_Test, WhatACompositeCanTake)
{
    const auto rebuild = FlattenOptionsFor(Layers("rebuild", "delta", false));
    EXPECT_TRUE(Option(rebuild, FlattenStrategy::Flat).available);
    EXPECT_TRUE(Option(rebuild, FlattenStrategy::Delta).available);
    EXPECT_FALSE(Option(rebuild, FlattenStrategy::Commit).available);
    EXPECT_NE(Option(rebuild, FlattenStrategy::Commit).reason.find("graft"), std::string::npos);
    EXPECT_FALSE(Option(rebuild, FlattenStrategy::WriteBack).available);
    EXPECT_NE(Option(rebuild, FlattenStrategy::WriteBack).reason.find("writable"), std::string::npos);

    const auto graft = FlattenOptionsFor(Layers("graft", "commit", true));
    EXPECT_TRUE(Option(graft, FlattenStrategy::Commit).available);
    EXPECT_TRUE(Option(graft, FlattenStrategy::WriteBack).available);

    const auto inlined = FlattenOptionsFor(Layers("graft", "delta", true, "(inline)"));
    EXPECT_FALSE(Option(inlined, FlattenStrategy::Delta).available);
    EXPECT_FALSE(Option(inlined, FlattenStrategy::WriteBack).available);
}

TEST(FlattenChoice_Test, PreselectionFollowsWritesSave)
{
    const StateNode commit = Layers("graft", "commit", false);
    EXPECT_EQ(PreselectedStrategy(FlattenOptionsFor(commit), commit), FlattenStrategy::Commit);
    const StateNode unavailable = Layers("rebuild", "commit", false);
    EXPECT_EQ(PreselectedStrategy(FlattenOptionsFor(unavailable), unavailable), FlattenStrategy::Delta)
        << "commit does not apply to a rebuild: the default";
    const StateNode inlined = Layers("rebuild", "delta", false, "(inline)");
    EXPECT_EQ(PreselectedStrategy(FlattenOptionsFor(inlined), inlined), FlattenStrategy::Flat);
}

TEST(FlattenChoice_Test, RequestOptions)
{
    FlattenChoice flat;
    flat.strategy = FlattenStrategy::Flat;
    flat.compact = true;
    EXPECT_EQ(FlattenRequestOptions(flat, /*plan*/ true), (std::map<std::string, std::string>{{"strategy", "flat"}, {"compact", "true"}}));
    FlattenChoice back;
    back.strategy = FlattenStrategy::WriteBack;
    back.keepBoth = true;
    EXPECT_EQ(FlattenRequestOptions(back, true),
              (std::map<std::string, std::string>{{"strategy", "write-back"}, {"plan", "true"}, {"onConflict", "keep-both"}}));
    FlattenChoice commit;
    commit.strategy = FlattenStrategy::Commit;
    commit.force = true;
    EXPECT_EQ(FlattenRequestOptions(commit, false), (std::map<std::string, std::string>{{"strategy", "commit"}, {"force", "true"}}));

    MediaPanelRow row;
    row.format = "graft-fat16";
    EXPECT_TRUE(IsCompositeRow(row));
    row.format = "raw";
    EXPECT_FALSE(IsCompositeRow(row));
}
