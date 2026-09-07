#pragma once

#include <filesystem>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef __APPLE__
#include <limits.h>
#include <mach-o/dyld.h>
#endif

#ifndef _WIN32
#include <unistd.h>
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
    static fs::path GetExecutableDir()
    {
#ifdef _WIN32
        char path[MAX_PATH];
        DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
        if (length > 0 && length < MAX_PATH)
        {
            return fs::path(path).parent_path();
        }
#endif
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

    /// @brief Backwards-compatible camelCase wrapper.
    static fs::path getExecutableDir()
    {
        return GetExecutableDir();
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
    static fs::path FindProjectRoot(const fs::path& startPath = GetExecutableDir())
    {
        fs::path current = startPath;

        // Ensure we start with an absolute path
        if (!current.is_absolute())
        {
            current = fs::absolute(current);
        }

        // Strip "." and ".." segments: some launchers (IDE runners, PowerShell's
        // Process.Start) exec the test binary via a "./"-containing path, and
        // _NSGetExecutablePath echoes it verbatim - without normalization every
        // derived fixture path would carry a stray "/./" segment
        current = current.lexically_normal();

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

    /// @brief Backwards-compatible camelCase wrapper.
    static fs::path findProjectRoot(const fs::path& startPath = GetExecutableDir())
    {
        return FindProjectRoot(startPath);
    }

    static std::string GetTestDataPath(const std::string& relativePath)
    {
        fs::path root = FindProjectRoot();
        fs::path fullPath = root / "testdata" / relativePath;
        return fullPath.string();
    }

    /// @brief Get the scratch directory (<project_root>/scratch).
    /// Creates the directory if it does not exist.
    static fs::path GetScratchDir()
    {
        fs::path scratch = FindProjectRoot() / "scratch";
        std::error_code ec;
        fs::create_directories(scratch, ec);
        return scratch;
    }

    /// @brief Backwards-compatible camelCase wrapper.
    static fs::path getScratchDir()
    {
        return GetScratchDir();
    }

    /// @brief Get absolute path for a test scratch artifact.
    static std::string GetTestScratchPath(const std::string& relativePath)
    {
        fs::path fullPath = GetScratchDir() / relativePath;
        if (fullPath.has_parent_path())
        {
            std::error_code ec;
            fs::create_directories(fullPath.parent_path(), ec);
        }
        return fullPath.string();
    }

    /// @brief Get a scratch path unique to this process, keeping the extension.
    ///
    /// Inserts the PID into the filename stem ("test.tzx" -> "test_29987.tzx"):
    /// parallel GTest shards run several copies of the test binary concurrently,
    /// and fixtures that create and tear down files or whole directories must not
    /// share names across processes - a shared name lets one shard delete
    /// another's files mid-test. The extension is preserved because loaders and
    /// managers dispatch on it. Always keep test artifacts under scratch/
    /// (AGENTS.md), never in the OS temp directory.
    static std::string GetUniqueTestScratchPath(const std::string& leafName)
    {
#ifdef _WIN32
        const DWORD pid = GetCurrentProcessId();
#else
        const pid_t pid = getpid();
#endif
        const fs::path leaf(leafName);
        return GetTestScratchPath(leaf.stem().string() + "_" + std::to_string(pid) + leaf.extension().string());
    }
};
