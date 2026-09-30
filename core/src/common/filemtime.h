#pragma once

/// @file filemtime.h
/// @brief A file or folder's modification time as seconds since 1970-01-01
/// UTC, straight from the OS API.
///
/// std::filesystem's file_time_type is tied to a clock whose epoch the
/// standard leaves unspecified. Toolchains without C++20 file_clock
/// to_sys/from_sys have to measure the epoch offset from a pair of now()
/// calls - which races the scheduler on a loaded machine and poisons every
/// conversion until the process exits (seen as whole-second mtime drift).
/// The native APIs speak Unix time directly, so no clock arithmetic and no
/// measurement exist here at all.

#include <cstdint>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#endif

/// Seconds since 1970-01-01 UTC of the path's modification time (symlinks
/// are followed, directories included). False when the OS cannot stat the
/// path; seconds is untouched then
inline bool GetMTimeUnixSeconds(const std::filesystem::path& path, int64_t& seconds)
{
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info))
        return false;
    ULARGE_INTEGER ticks;
    ticks.LowPart = info.ftLastWriteTime.dwLowDateTime;
    ticks.HighPart = info.ftLastWriteTime.dwHighDateTime;
    seconds = static_cast<int64_t>(ticks.QuadPart / 10000000ULL - 11644473600ULL);
    return true;
#else
    struct stat st;
    if (::stat(path.c_str(), &st) != 0)
        return false;
    seconds = static_cast<int64_t>(st.st_mtime);
    return true;
#endif
}

/// Sets the path's modification time from seconds since 1970-01-01 UTC; the
/// access time is left alone. False when the OS refuses the write
inline bool SetMTimeUnixSeconds(const std::filesystem::path& path, int64_t seconds)
{
#if defined(_WIN32)
    const uint64_t ticks = static_cast<uint64_t>(seconds + 11644473600LL) * 10000000ULL;
    FILETIME time;
    time.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFULL);
    time.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    // FILE_FLAG_BACKUP_SEMANTICS: without it folders cannot be opened
    const HANDLE handle = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    const BOOL ok = SetFileTime(handle, nullptr, nullptr, &time);
    CloseHandle(handle);
    return ok != FALSE;
#else
    struct timespec times[2] = {};
    times[0].tv_nsec = UTIME_OMIT;  // the access time stays as it is
    times[1].tv_sec = static_cast<time_t>(seconds);
    return ::utimensat(AT_FDCWD, path.c_str(), times, 0) == 0;
#endif
}
