#pragma once

// A scratch directory for a test of this binary: <project root>/scratch/<pid>/<tag>-<n>, removed when the process ends.
// Unique by process id and a process-wide counter - no clock, no random_device - and never in the OS temp directory
// (AGENTS.md: test artifacts live under scratch/). The root is found from this header's own path.

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace unreal_asm_tests
{
inline std::filesystem::path ProcessScratchDir()
{
    struct Guard
    {
        std::filesystem::path path;
        ~Guard()
        {
            if (std::getenv("UNREAL_TEST_KEEP_SCRATCH") != nullptr)
                return;
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };
    static const Guard guard = [] {
        namespace fs = std::filesystem;
        // core/src/3rdparty/unreal-asm/tests/scratchdir.h -> the project root is five levels up from the folder
        const fs::path root = (fs::path(__FILE__).parent_path() / ".." / ".." / ".." / ".." / "..").lexically_normal();
#ifdef _WIN32
        const long pid = static_cast<long>(_getpid());
#else
        const long pid = static_cast<long>(getpid());
#endif
        Guard g{root / "scratch" / ("asm-" + std::to_string(pid))};
        std::error_code ec;
        fs::create_directories(g.path, ec);
        return g;
    }();
    return guard.path;
}

/// A new empty directory <process scratch>/<tag>-<n>
inline std::filesystem::path UniqueDir(const std::string& tag)
{
    static std::atomic<uint64_t> counter{0};
    const std::filesystem::path dir = ProcessScratchDir() / (tag + "-" + std::to_string(++counter));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}
}  // namespace unreal_asm_tests
