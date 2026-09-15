#include <emulator/buildinfo.h>
#include <gtest/gtest.h>

#include <cstring>

// P0-4 (docs/inprogress/2026-09-14-automation-triage-gaps): the build
// fingerprint baked in by CMake must always be complete enough to attribute
// a triage session to a build. "unknown" is a legal value (git-less
// configure, detached HEAD) but never an EMPTY one - downstream JSON
// serialization and log grepping rely on the fields existing.

class BuildInfo_Test : public ::testing::Test
{
};

TEST_F(BuildInfo_Test, VersionIsNonEmpty)
{
    EXPECT_NE(buildinfo::kVersion, nullptr);
    EXPECT_GT(std::strlen(buildinfo::kVersion), 0u);
}

TEST_F(BuildInfo_Test, GitBranchIsNonEmpty)
{
    // "unknown" is acceptable (detached HEAD / no git); empty is a bug
    EXPECT_NE(buildinfo::kGitBranch, nullptr);
    EXPECT_GT(std::strlen(buildinfo::kGitBranch), 0u);
}

TEST_F(BuildInfo_Test, GitCommitIsNonEmpty)
{
    EXPECT_NE(buildinfo::kGitCommit, nullptr);
    EXPECT_GT(std::strlen(buildinfo::kGitCommit), 0u);
}

TEST_F(BuildInfo_Test, BuildTypeIsNonEmpty)
{
    EXPECT_NE(buildinfo::kBuildType, nullptr);
    EXPECT_GT(std::strlen(buildinfo::kBuildType), 0u);
}
