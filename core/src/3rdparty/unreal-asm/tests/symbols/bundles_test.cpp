#include <gtest/gtest.h>

#include "unrealasm/symbols/bundles.h"

using namespace unrealasm::symbols;

namespace
{
const char* kManifest = R"({ "format": "unreal-symbols-manifest", "version": 1,
  "bundles": [
    { "id": "rom:48k", "file": "48k_rom.map", "match": { "page_sha256": ["AA11"] }, "space": "rom", "except": ["spare"] },
    { "id": "sysvars:48k", "file": "48k_variables.map", "match": { "page_sha256": ["aa11", "bb22"] }, "space": "cpu:main" },
    { "id": "rom:sp-p8", "file": "sprinter/p8.map", "match": { "page": 8, "page_sha256": "cc33" } }
  ] })";
}  // namespace

TEST(Bundles_Test, TheManifestReads)
{
    BundleManifest manifest;
    std::string error;
    ASSERT_TRUE(ParseManifest(kManifest, manifest, error)) << error;
    ASSERT_EQ(manifest.bundles.size(), 3u);
    EXPECT_EQ(manifest.bundles[0].pageSha256[0], "aa11") << "digests compare in lower case";
    EXPECT_EQ(manifest.bundles[0].except, std::vector<std::string>{"spare"});
    EXPECT_EQ(manifest.bundles[2].page, 8);
    EXPECT_EQ(manifest.bundles[2].pageSha256, std::vector<std::string>{"cc33"});

    EXPECT_FALSE(ParseManifest(R"({"format":"other","bundles":[]})", manifest, error));
    EXPECT_FALSE(ParseManifest(R"({"format":"unreal-symbols-manifest","bundles":[{"id":"x","file":"f","match":{}}]})", manifest, error));
    EXPECT_NE(error.find("page_sha256"), std::string::npos);
    EXPECT_FALSE(ParseManifest("{", manifest, error));
}

TEST(Bundles_Test, ARomBundleAppliesToEveryPageThatMatches)
{
    BundleManifest manifest;
    std::string error;
    ASSERT_TRUE(ParseManifest(kManifest, manifest, error)) << error;

    // A 48K: page 0
    std::vector<BundleHit> hits = MatchBundles(manifest, {"aa11"});
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0].set, "bundle:rom:48k");
    EXPECT_EQ(hits[0].space, "rom0");
    EXPECT_EQ(hits[1].set, "bundle:sysvars:48k");
    EXPECT_EQ(hits[1].space, "cpu:main");

    // The same ROM twice (ZX-Evo's image): one set per page, the system variables once
    hits = MatchBundles(manifest, {"x", "AA11", "y", "aa11"});
    ASSERT_EQ(hits.size(), 3u);
    EXPECT_EQ(hits[0].set, "bundle:rom:48k:rom1");
    EXPECT_EQ(hits[1].set, "bundle:rom:48k:rom3");
    EXPECT_EQ(hits[1].space, "rom3");
    EXPECT_EQ(hits[2].page, 1);

    // A bundle tied to a page number: only that page
    hits = MatchBundles(manifest, {"cc33"});
    EXPECT_TRUE(hits.empty());
    std::vector<std::string> sprinter(9);
    sprinter[8] = "cc33";
    hits = MatchBundles(manifest, sprinter);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].space, "");
    EXPECT_EQ(hits[0].set, "bundle:rom:sp-p8");
}
