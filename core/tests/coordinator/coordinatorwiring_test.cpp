/// @file coordinatorwiring_test.cpp
/// @brief The coordinator's sources include only their own headers and the standard library (requirements N7).
/// The library's include path already refuses "emulator/..." headers; this catches the relative way around it
/// ("../emulator.h") and a header that wanders into the directory from elsewhere.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <string>

#include "_helpers/testpathhelper.h"

namespace fs = std::filesystem;

TEST(CoordinatorWiring_Test, TheLibraryIncludesOnlyItsOwnHeaders)
{
    const fs::path dir = TestPathHelper::FindProjectRoot() / "core" / "src" / "emulator" / "coordinator";
    ASSERT_TRUE(fs::is_directory(dir)) << dir;

    std::set<std::string> own;
    for (const auto& entry : fs::directory_iterator(dir))
        own.insert(entry.path().filename().string());

    const std::regex quoted(R"re(^\s*#\s*include\s*"([^"]+)")re");
    int sources = 0;
    for (const auto& entry : fs::directory_iterator(dir))
    {
        const std::string ext = entry.path().extension().string();
        if (ext != ".h" && ext != ".cpp")
            continue;
        ++sources;
        std::ifstream in(entry.path());
        std::string line;
        while (std::getline(in, line))
        {
            std::smatch m;
            if (std::regex_search(line, m, quoted))
            {
                EXPECT_TRUE(own.count(m[1].str()) == 1)
                    << entry.path().filename().string() << " includes \"" << m[1].str()
                    << "\": the coordinator talks to the emulator through ports.h only";
            }
        }
    }
    EXPECT_GE(sources, 8);
}
