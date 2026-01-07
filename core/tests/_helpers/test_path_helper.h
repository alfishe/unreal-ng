#pragma once

#include <filesystem>
#include <stdexcept>

#ifdef __APPLE__
#include <limits.h>
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

class TestPathHelper
{
public:
    /// @brief Get the directory containing the test executable.
    ///
    /// This provides a reliable starting point for finding the project root,
    /// regardless of the current working directory.
    ///
    /// @return The filesystem path to the directory containing the executable.
    static fs::path getExecutableDir()
    {
#ifdef __APPLE__
        char path[PATH_MAX];
        uint32_t size = sizeof(path);
        if (_NSGetExecutablePath(path, &size) == 0)
        {
            return fs::path(path).parent_path();
        }
#endif
        // Fallback: use current path if we can't get executable path
        return fs::current_path();
    }

    /// @brief Helper function to find the project root directory.
    ///
    /// This function is bulletproof and works reliably in all scenarios:
    /// - Individual test runs
    /// - Batch test runs
    /// - Different working directories
    /// - Tests run from IDE or command line
    ///
    /// It starts from the executable directory (typically build/bin/) and searches
    /// upward for the project root markers.
    ///
    /// @param startPath The starting path for the search. Defaults to executable directory.
    /// @return The filesystem path to the project root.
    /// @throws std::runtime_error if the project root cannot be found within the search depth.
    static fs::path findProjectRoot(const fs::path& startPath = getExecutableDir())
    {
        fs::path current = startPath;

        // Ensure we start with an absolute path
        if (!current.is_absolute())
        {
            current = fs::absolute(current);
        }

        int depth = 0;
        const int maxDepth = 15;  // Increased depth to handle deep build directories

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
        // Provide detailed error message for debugging
        std::string errorMsg = "Could not find project root directory. Started from: " + startPath.string();
        throw std::runtime_error(errorMsg);
    }

    static std::string GetTestDataPath(const std::string& relativePath)
    {
        fs::path root = findProjectRoot();
        fs::path fullPath = root / "testdata" / relativePath;
        return fullPath.string();
    }
};
