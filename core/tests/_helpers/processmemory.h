#pragma once

/// @file processmemory.h
/// @brief The process's resident memory now, for tests that bound what an operation keeps in RAM.
/// `Settle()` first hands freed heap back to the system where the allocator allows (glibc), so a growth measured
/// from a baseline is what the operation holds, not what earlier tests left in the heap. A test pairs each bound
/// with a control that must grow (an unlimited session): when the control does not, the measurement cannot see the
/// difference on this platform and the test skips instead of passing blind.

#include <cstdint>
#include <cstdio>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef PSAPI_VERSION
#define PSAPI_VERSION 2  // GetProcessMemoryInfo from kernel32 (K32...): no psapi.lib to link
#endif
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <unistd.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#endif

namespace ProcessMemory
{
    /// Resident bytes (0: not known on this platform)
    inline uint64_t Resident()
    {
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS counters{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof counters))
            return counters.WorkingSetSize;
        return 0;
#elif defined(__APPLE__)
        mach_task_basic_info_data_t info{};
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
            return info.resident_size;
        return 0;
#elif defined(__linux__)
        unsigned long long size = 0, resident = 0;
        FILE* statm = std::fopen("/proc/self/statm", "r");
        if (!statm)
            return 0;
        const int read = std::fscanf(statm, "%llu %llu", &size, &resident);
        std::fclose(statm);
        return read == 2 ? resident * static_cast<uint64_t>(sysconf(_SC_PAGESIZE)) : 0;
#else
        return 0;
#endif
    }

    /// Give freed heap back where the allocator can
    inline void Settle()
    {
#if defined(__linux__) && defined(__GLIBC__)
        malloc_trim(0);
#endif
    }

    /// Resident growth from construction to `Growth()` (0 when it shrank)
    class Meter
    {
    public:
        Meter()
        {
            Settle();
            _base = Resident();
        }
        uint64_t Growth() const
        {
            const uint64_t now = Resident();
            return now > _base ? now - _base : 0;
        }
        bool Known() const { return _base != 0; }

    private:
        uint64_t _base = 0;
    };
}  // namespace ProcessMemory
