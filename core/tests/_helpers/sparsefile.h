#pragma once

/// @file sparsefile.h
/// @brief Extending a test file to a large size without filling the disk.
///
/// A file grown by `std::filesystem::resize_file` holds no data on APFS, ext4 and the like: the tail is a hole.
/// NTFS allocates every byte unless the file is marked sparse first, so a "17 GiB of zeros" card image there
/// needs 17 GiB of free space (and fails without it). ResizeSparse marks the file sparse on Windows, then resizes.
///
/// Example: an empty SD card image of 17 GiB for a machine that reads sectors above 16 GiB:
///   { std::ofstream create(path, std::ios::binary); }
///   ASSERT_TRUE(testfiles::ResizeSparse(path, 17ull * 1024 * 1024 * 1024));

#include <cstdint>
#include <filesystem>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#endif

namespace testfiles
{

/// Grow (or cut) the existing file @p path to @p size; the grown part holds no disk space
/// @return false when the file cannot be marked sparse or resized
inline bool ResizeSparse(const std::filesystem::path& path, uint64_t size)
{
#ifdef _WIN32
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD returned = 0;
    const bool marked = DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr) != 0;
    CloseHandle(file);
    if (!marked)
        return false;
#endif
    std::error_code ec;
    std::filesystem::resize_file(path, size, ec);
    return !ec;
}

}  // namespace testfiles
