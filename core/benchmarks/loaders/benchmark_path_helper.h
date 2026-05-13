#pragma once

#include <filesystem>
#include <stdexcept>

#ifdef __APPLE__
#include <limits.h>
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

/// @brief Helper class for benchmark path resolution
/// @note Mirrors TestPathHelper from tests/_helpers but for benchmarks
class BenchmarkPathHelper
{
public:
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
        return fs::current_path();
    }

    static fs::path findProjectRoot(const fs::path& startPath = getExecutableDir())
    {
        fs::path current = startPath;

        if (!current.is_absolute())
        {
            current = fs::absolute(current);
        }

        int depth = 0;
        const int maxDepth = 15;

        while (depth < maxDepth)
        {
            bool hasTestData = fs::exists(current / "testdata");
            bool hasCore = fs::exists(current / "core");
            bool hasCMakeLists = fs::exists(current / "CMakeLists.txt");

            bool coreIsSourceDir = false;
            if (hasCore)
            {
                fs::path corePath = current / "core";
                coreIsSourceDir = fs::is_directory(corePath) && fs::exists(corePath / "src");
            }

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
