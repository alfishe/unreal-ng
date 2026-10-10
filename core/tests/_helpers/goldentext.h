#pragma once

/// @file goldentext.h
/// @brief Golden text files for report formats (design-automation-coverage.md R8): the text of a report is compared with
/// `core/tests/automation/golden/<group>/<name>.txt`. A change of the format is a visible diff in the file;
/// `UNREAL_UPDATE_GOLDEN=1` rewrites the files from the current output.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "_helpers/testpathhelper.h"

namespace GoldenText
{
inline std::filesystem::path PathOf(const std::string& group, const std::string& name)
{
    return TestPathHelper::FindProjectRoot() / "core" / "tests" / "automation" / "golden" / group / (name + ".txt");
}

/// Compare `actual` with the golden file; with UNREAL_UPDATE_GOLDEN set, write it instead
inline void Expect(const std::string& group, const std::string& name, const std::string& actual)
{
    const std::filesystem::path path = PathOf(group, name);
    if (std::getenv("UNREAL_UPDATE_GOLDEN") != nullptr)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << actual;
        return;
    }
    std::ifstream in(path, std::ios::binary);
    ASSERT_TRUE(in.good()) << "missing golden file " << path << " (UNREAL_UPDATE_GOLDEN=1 writes it)";
    const std::string expected((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(actual, expected) << "golden " << group << "/" << name << " differs (UNREAL_UPDATE_GOLDEN=1 rewrites it)";
}
}  // namespace GoldenText
