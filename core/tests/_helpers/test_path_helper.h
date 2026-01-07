#pragma once

#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

class TestPathHelper
{
public:
    /// @brief Helper function to find the project root directory.
    ///
    /// This function is intended for use in tests and development tools to locate
    /// the root of the project structure, enabling reliable relative path navigation
    /// to artifacts like test data. It searches upwards from the start path.
    ///
    /// @param startPath The starting path for the search. Defaults to current working directory.
    /// @return The filesystem path to the project root.
    /// @throws std::runtime_error if the project root cannot be found within the search depth.
    static fs::path findProjectRoot(const fs::path& startPath = fs::current_path())
    {
        fs::path current = startPath;
        int depth = 0;
        const int maxDepth = 10;  // Prevent infinite loops

        // Look up the directory tree until we find the project root
        while (depth < maxDepth)
        {
            // Check if this directory contains the characteristic project markers
            bool hasTestData = fs::exists(current / "testdata");
            bool hasCore = fs::exists(current / "core");
            bool hasCMakeLists = fs::exists(current / "CMakeLists.txt");

            // Additional check: make sure core is a directory with source files, not just a build output
            bool coreIsSourceDir = false;
            if (hasCore)
            {
                fs::path corePath = current / "core";
                // Check if core/src exists (indicating it's the source tree, not build output)
                coreIsSourceDir = fs::is_directory(corePath) && fs::exists(corePath / "src");
            }

            // If we find testdata, a proper core source directory, and CMakeLists.txt, this is the project root
            if (hasTestData && coreIsSourceDir && hasCMakeLists)
            {
                return current;
            }

            if (!current.has_parent_path() || current == current.parent_path())
            {
                break;
            }

            current = current.parent_path();
            depth++;
        }

        // If we reach here, we couldn't find the project root
        throw std::runtime_error("Could not find project root directory");
    }

    static std::string GetTestDataPath(const std::string& relativePath)
    {
        fs::path root = findProjectRoot();
        fs::path fullPath = root / "testdata" / relativePath;
        return fullPath.string();
    }
};
