#include "pch.h"

#include "_helpers/testpathhelper.h"

#include <filesystem>
#include <fstream>

/// region <Per-process scratch directory>

TEST(TestPathHelper_Test, UniqueScratchPathLivesInProcessDirectory)
{
    const fs::path path(TestPathHelper::GetUniqueTestScratchPath("pathhelper-probe.bin"));
    const fs::path processDir = path.parent_path();

    // Parallel shards stay isolated via <scratch>/<pid>; the scratch root itself stays clean.
    // The root is <project>/scratch, or UNREAL_TEST_SCRATCH_DIR when the run moves it (the Linux Docker run)
    EXPECT_EQ(processDir.parent_path().lexically_normal(), TestPathHelper::GetScratchDir().lexically_normal());
    EXPECT_FALSE(processDir.filename().string().empty());
    EXPECT_EQ(processDir.filename().string().find_first_not_of("0123456789"), std::string::npos)
        << "process directory must be PID-named: " << processDir;

    // The artifact itself must not leak into the scratch root
    EXPECT_FALSE(fs::exists(processDir.parent_path() / path.filename()));

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "probe";
    out.close();
    EXPECT_TRUE(fs::exists(path)) << path;
}

TEST(TestPathHelper_Test, UniqueScratchPathCreatesNestedLeafDirectories)
{
    const fs::path path(TestPathHelper::GetUniqueTestScratchPath("nested/leaf/probe.bin"));
    EXPECT_EQ(path.parent_path().filename().string(), "leaf");

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "probe";
    out.close();
    EXPECT_TRUE(fs::exists(path)) << path;
}

TEST(TestPathHelper_Test, UniqueScratchPathSurvivesDirectoryRemoval)
{
    // Tests may remove_all() their own artifact subdirectory (scoped fixtures);
    // the next unique path must transparently re-create the missing directories
    const fs::path path(TestPathHelper::GetUniqueTestScratchPath("survive/probe.bin"));
    std::error_code ec;
    fs::remove_all(path.parent_path(), ec);
    ASSERT_FALSE(fs::exists(path.parent_path()));

    const fs::path again(TestPathHelper::GetUniqueTestScratchPath("survive/probe.bin"));
    std::ofstream out(again, std::ios::binary | std::ios::trunc);
    out << "probe";
    out.close();
    EXPECT_TRUE(fs::exists(again)) << again;
}

/// endregion </Per-process scratch directory>
