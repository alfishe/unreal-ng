#include "common/processstats.h"

#include <algorithm>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// windows.h first: psapi.h and tlhelp32.h need its types
#include <psapi.h>
#include <tlhelp32.h>
#else
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <libproc.h>
#include <mach/mach.h>
#elif defined(__linux__)
#include <cstdio>
#include <cstring>
#endif
#endif

namespace
{
#if defined(_WIN32)
uint64_t FileTimeUs(const FILETIME& time)
{
    ULARGE_INTEGER value;
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return value.QuadPart / 10;  // 100 ns units
}

uint32_t WindowsThreads()
{
    // The process list carries each process's thread count: one snapshot, no walk over every thread of the host
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    const DWORD self = GetCurrentProcessId();
    uint32_t threads = 0;
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32First(snapshot, &entry); more; more = Process32Next(snapshot, &entry))
        if (entry.th32ProcessID == self)
        {
            threads = static_cast<uint32_t>(entry.cntThreads);
            break;
        }
    CloseHandle(snapshot);
    return threads;
}
#else
uint64_t TimevalUs(const timeval& time)
{
    return static_cast<uint64_t>(time.tv_sec) * 1000000u + static_cast<uint64_t>(time.tv_usec);
}
#endif

#if defined(__linux__)
/// "VmRSS:     1234 kB" -> 1234 * 1024; the line's number for "Threads:"
void ReadProcStatus(ProcessSample& sample)
{
    FILE* file = std::fopen("/proc/self/status", "r");
    if (file == nullptr)
        return;
    char line[256];
    while (std::fgets(line, sizeof(line), file) != nullptr)
    {
        unsigned long long value = 0;
        if (std::sscanf(line, "VmRSS: %llu", &value) == 1)
            sample.residentBytes = value * 1024u;
        else if (std::sscanf(line, "VmHWM: %llu", &value) == 1)
            sample.peakResidentBytes = value * 1024u;
        else if (std::sscanf(line, "VmSize: %llu", &value) == 1)
            sample.virtualBytes = value * 1024u;
        else if (std::sscanf(line, "Threads: %llu", &value) == 1)
            sample.threads = static_cast<uint32_t>(value);
    }
    std::fclose(file);
}
#endif
}  // namespace

ProcessSample SampleProcess()
{
    ProcessSample sample;
#if defined(_WIN32)
    FILETIME creation;
    FILETIME exitTime;
    FILETIME kernel;
    FILETIME user;
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernel, &user))
    {
        sample.userTimeUs = FileTimeUs(user);
        sample.systemTimeUs = FileTimeUs(kernel);
        sample.cpuTimeUs = sample.userTimeUs + sample.systemTimeUs;
        sample.ok = true;
    }
    PROCESS_MEMORY_COUNTERS_EX memory;
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                             sizeof(memory)))
    {
        sample.residentBytes = static_cast<uint64_t>(memory.WorkingSetSize);
        sample.peakResidentBytes = static_cast<uint64_t>(memory.PeakWorkingSetSize);
        sample.virtualBytes = static_cast<uint64_t>(memory.PrivateUsage);
    }
    sample.threads = WindowsThreads();
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
    {
        sample.userTimeUs = TimevalUs(usage.ru_utime);
        sample.systemTimeUs = TimevalUs(usage.ru_stime);
        sample.cpuTimeUs = sample.userTimeUs + sample.systemTimeUs;
        sample.ok = true;
#if defined(__APPLE__)
        sample.peakResidentBytes = static_cast<uint64_t>(usage.ru_maxrss);  // bytes on macOS
#else
        sample.peakResidentBytes = static_cast<uint64_t>(usage.ru_maxrss) * 1024u;  // kilobytes elsewhere
#endif
    }
#if defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) ==
        KERN_SUCCESS)
    {
        sample.residentBytes = static_cast<uint64_t>(info.resident_size);
        sample.peakResidentBytes = std::max<uint64_t>(sample.peakResidentBytes, info.resident_size_max);
        sample.virtualBytes = static_cast<uint64_t>(info.virtual_size);
    }
    proc_taskinfo task{};
    if (proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &task, sizeof(task)) == static_cast<int>(sizeof(task)))
        sample.threads = static_cast<uint32_t>(task.pti_threadnum);
#elif defined(__linux__)
    ReadProcStatus(sample);
#endif
#endif
    return sample;
}

uint32_t HostLogicalCores()
{
    const unsigned cores = std::thread::hardware_concurrency();
    return cores > 0 ? cores : 1u;
}

ProcessStatsSampler& ProcessStatsSampler::Instance()
{
    static ProcessStatsSampler* sampler = new ProcessStatsSampler();  // never destroyed: read until exit
    return *sampler;
}

ProcessStatsSampler::ProcessStatsSampler(double intervalSeconds)
    : _interval(intervalSeconds > 0 ? intervalSeconds : kDefaultIntervalSeconds),
      _created(Clock::now()),
      _cores(HostLogicalCores())
{
}

ProcessStats ProcessStatsSampler::Read()
{
    const Clock::time_point now = Clock::now();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_have && std::chrono::duration<double>(now - _sampledAt).count() < _interval)
        {
            ProcessStats stats = _stats;
            stats.uptimeSeconds = std::chrono::duration<double>(now - _created).count();
            return stats;
        }
    }
    return ReadAt(now, SampleProcess());
}

ProcessStats ProcessStatsSampler::ReadAt(Clock::time_point now, const ProcessSample& sample)
{
    std::lock_guard<std::mutex> lock(_mutex);
    ProcessStats stats;
    stats.sample = sample;
    stats.cores = _cores;
    stats.uptimeSeconds = std::chrono::duration<double>(now - _created).count();
    if (_have && now > _sampledAt && sample.cpuTimeUs >= _last.cpuTimeUs)
    {
        const double seconds = std::chrono::duration<double>(now - _sampledAt).count();
        const double cpuSeconds = static_cast<double>(sample.cpuTimeUs - _last.cpuTimeUs) / 1e6;
        stats.intervalSeconds = seconds;
        stats.cpuPercent = seconds > 0 ? cpuSeconds / seconds * 100.0 : 0.0;
        stats.cpuPercentOfHost = stats.cpuPercent / static_cast<double>(_cores);
    }
    stats.samples = _stats.samples + 1;
    _stats = stats;
    _last = sample;
    _sampledAt = now;
    _have = true;
    return stats;
}
