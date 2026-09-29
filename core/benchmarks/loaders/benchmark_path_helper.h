#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

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

    /// End the benchmark process with an error. For setup failures that would
    /// otherwise let a benchmark measure the wrong workload: SkipWithError only
    /// prints, and the benchmark runner still exits 0
    [[noreturn]] static void FailSetup(const std::string& message)
    {
        std::fprintf(stderr, "FATAL: benchmark setup failed: %s\n", message.c_str());
        std::fflush(stderr);
        std::exit(EXIT_FAILURE);
    }

    /// Absolute path of a testdata file a benchmark cannot run without. A missing
    /// file (or an unresolvable project root) fails the whole run instead of
    /// silently benchmarking a machine with nothing loaded - which runs faster
    /// and looks like a real result
    static std::string RequireTestDataFile(const std::string& relativePath)
    {
        std::string path;
        try
        {
            path = GetTestDataPath(relativePath);
        }
        catch (const std::exception& e)
        {
            FailSetup(std::string("cannot resolve testdata/") + relativePath + ": " + e.what());
        }

        if (!fs::is_regular_file(path))
            FailSetup("required test data file not found: " + path);

        return path;
    }
};
