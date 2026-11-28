#pragma once

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

/// Owns the per-process scratch subdirectory (scratch/<pid>) that backs
/// TestPathHelper::GetUniqueTestScratchPath(). The directory is created on
/// first use; the guard lives as a function-local static, so its destructor
/// runs when the test binary leaves main() or calls exit() and removes the
/// whole subtree - per-test cleanup stays optional and nothing accumulates
/// in the scratch root any more. Crashes and SIGKILL bypass the destructor,
/// so every new process also sweeps directories whose owning PID is no
/// longer alive. The sweep only ever considers purely numeric directory
/// names directly under scratch/; named experiment folders are never touched.
/// Set UNREAL_TEST_KEEP_SCRATCH=1 to keep the directory for debugging.
class ScopedProcessScratchDir
{
public:
    explicit ScopedProcessScratchDir(const fs::path& scratchRoot)
    {
        _dir = scratchRoot / std::to_string(CurrentPid());
        std::error_code ec;
        fs::create_directories(_dir, ec);
        SweepStaleProcessDirs(scratchRoot);
    }

    ~ScopedProcessScratchDir()
    {
        if (std::getenv("UNREAL_TEST_KEEP_SCRATCH") != nullptr)
        {
            return;  // debugging aid: inspect what a run actually wrote
        }
        std::error_code ec;
        fs::remove_all(_dir, ec);
    }

    const fs::path& Path() const
    {
        return _dir;
    }

private:
    static uint64_t CurrentPid()
    {
#ifdef _WIN32
        return GetCurrentProcessId();
#else
        return static_cast<uint64_t>(getpid());
#endif
    }

    /// Best-effort liveness check. False negatives (reporting a live process
    /// as dead) must never happen: when in doubt, report the PID as alive so
    /// the sweep skips its directory.
    static bool IsProcessAlive(uint64_t pid)
    {
#ifdef _WIN32
        const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
        if (handle == nullptr)
        {
            // ERROR_INVALID_PARAMETER means "no such process"; anything else
            // (e.g. access denied) is conservatively treated as alive.
            return GetLastError() != ERROR_INVALID_PARAMETER;
        }
        CloseHandle(handle);
        return true;
#else
        if (kill(static_cast<pid_t>(pid), 0) == 0)
        {
            return true;
        }
        // ESRCH: no such process. EPERM: exists, but owned by another user.
        return errno != ESRCH;
#endif
    }

    static bool IsPidName(const std::string& name)
    {
        // PIDs fit in 10 digits on every supported platform; anything
        // longer or non-numeric is a named folder, not a process dir.
        return !name.empty() && name.size() <= 10 && name.find_first_not_of("0123456789") == std::string::npos;
    }

    /// Remove scratch/<pid>/ directories of processes that are no longer
    /// running (crashed or killed hard - normal exits clean up after
    /// themselves). Collects candidates first so the directory iterator is
    /// never advanced over an entry being removed.
    ///
    /// Processes started together sweep together: each stale directory is
    /// first claimed by a rename to sweep-<pid>-<own pid> (one process wins,
    /// the others skip it), and only its claimer removes it. Two remove_all
    /// over one tree lost entries under each other, and libstdc++'s
    /// remove_all on Windows (MinGW) then spun forever at full CPU in main().
    /// A claim left by a sweeper that died is claimed again.
    static void SweepStaleProcessDirs(const fs::path& scratchRoot)
    {
        std::vector<std::pair<fs::path, std::string>> stale;
        std::error_code ec;
        const std::string ownPid = std::to_string(CurrentPid());
        for (const auto& entry : fs::directory_iterator(scratchRoot, ec))
        {
            if (!entry.is_directory(ec))
            {
                continue;
            }
            const std::string name = entry.path().filename().string();
            std::string pid = name;
            std::string owner = name;
            if (name.rfind("sweep-", 0) == 0)
            {
                const size_t dash = name.find('-', 6);
                if (dash == std::string::npos)
                    continue;
                pid = name.substr(6, dash - 6);
                owner = name.substr(dash + 1);
            }
            if (!IsPidName(pid) || !IsPidName(owner) || owner == ownPid ||
                IsProcessAlive(std::strtoull(owner.c_str(), nullptr, 10)))
            {
                continue;
            }
            stale.emplace_back(entry.path(), pid);
        }
        for (const auto& [path, pid] : stale)
        {
            const fs::path claimed = scratchRoot / ("sweep-" + pid + "-" + ownPid);
            std::error_code removeEc;
            fs::rename(path, claimed, removeEc);
            if (removeEc)
            {
                continue;  // another process claimed it first
            }
            fs::remove_all(claimed, removeEc);
        }
    }

    fs::path _dir;
};

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
            // The module path keeps the drive spelling of the launch ("c:\..."); FileHelper::AbsolutePath reports it
            // upper-case, so every fixture path derived from here does too
            if (length >= 2 && path[1] == ':')
                path[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(path[0])));
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
        // relativePath is written with forward slashes in every caller (e.g.
        // "loaders/mgt/synthetic.img"). fs::path::operator/ parses that fine,
        // but .string() otherwise echoes the separators exactly as given -
        // make_preferred() normalizes the whole path to the platform's native
        // separator (a no-op on POSIX, backslash on Windows) so this matches
        // whatever any loader/DiskImage normalizes its own stored path to.
        fs::path fullPath = (root / "testdata" / relativePath).make_preferred();
        return fullPath.string();
    }

    /// @brief Get the scratch directory (<project_root>/scratch).
    /// Creates the directory if it does not exist.
    static fs::path GetScratchDir()
    {
        // UNREAL_TEST_SCRATCH_DIR moves the scratch directory off the project tree. The Linux Docker run needs it: on
        // a macOS bind mount (virtiofs) creating a file with mode 0200 fails, and std::filesystem::copy_file creates
        // its destination that way, so tests that copy test data into scratch fail with "Permission denied"
        // (docker/linux/README.md). Unset: <project root>/scratch, as always
        if (const char* override = std::getenv("UNREAL_TEST_SCRATCH_DIR"))
        {
            if (*override != '\0')
            {
                fs::path scratch(override);
                std::error_code ec;
                fs::create_directories(scratch, ec);
                return scratch;
            }
        }
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

    /// @brief Per-process scratch directory (<project_root>/scratch/<pid>).
    ///
    /// Created on first use and removed again when the process ends, so
    /// anything written below it disappears together with the test run.
    /// Exposed for tests that need to reason about the scratch layout.
    static fs::path GetProcessScratchDir()
    {
        static const ScopedProcessScratchDir guard(GetScratchDir());
        return guard.Path();
    }

    /// @brief Get a scratch path unique to this process, keeping the extension.
    ///
    /// Places the artifact in the per-process scratch directory
    /// (<scratch>/<pid>/) instead of suffixing the PID into the file name:
    /// parallel GTest shards run several copies of the test binary
    /// concurrently, and fixtures that create and tear down files or whole
    /// directories must not share names across processes - a shared name lets
    /// one shard delete another's files mid-test. The whole per-process
    /// directory is removed when the process exits (see
    /// ScopedProcessScratchDir), so no per-test cleanup is needed on the
    /// happy path and nothing piles up in the scratch root. The extension is
    /// preserved because loaders and managers dispatch on it. Always keep
    /// test artifacts under scratch/ (AGENTS.md), never in the OS temp
    /// directory.
    static std::string GetUniqueTestScratchPath(const std::string& leafName)
    {
        fs::path fullPath = GetProcessScratchDir() / leafName;
        if (fullPath.has_parent_path())
        {
            // Also transparently re-creates the process directory if a test
            // removed it with remove_all() on one of its artifact paths.
            std::error_code ec;
            fs::create_directories(fullPath.parent_path(), ec);
        }
        return fullPath.string();
    }

    /// @brief Get absolute path for a test scratch artifact.
    ///
    /// The same as GetUniqueTestScratchPath(): the path lies in the per-process scratch directory. It used to be
    /// <scratch>/<relativePath>, one path for every copy of the test binary - and parallel GTest shards are copies, so
    /// two tests of a suite that run in different shards wrote, deleted and read the same file (EmulatorSaveDisk_Test
    /// failed 21 of 24 runs of parallel copies). A test must not depend on what another process does to its files.
    static std::string GetTestScratchPath(const std::string& relativePath)
    {
        return GetUniqueTestScratchPath(relativePath);
    }
};
