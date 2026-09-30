#pragma once

/// @file scratchfolder.h
/// @brief A folder in the test scratch area, unique to this process, removed
/// when the object goes. Files are created from UTF-8 relative paths with a
/// chosen modification time (seconds since 1970 UTC).

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"

class ScratchFolder
{
public:
    explicit ScratchFolder(const char* name) : _path(FileHelper::ToFsPath(TestPathHelper::GetUniqueTestScratchPath(name)))
    {
        std::error_code ec;
        std::filesystem::remove_all(_path, ec);
        std::filesystem::create_directories(_path);
    }
    ~ScratchFolder()
    {
        std::error_code ec;
        std::filesystem::remove_all(_path, ec);
    }
    ScratchFolder(const ScratchFolder&) = delete;
    ScratchFolder& operator=(const ScratchFolder&) = delete;

    const std::filesystem::path& Path() const { return _path; }

    std::filesystem::path File(const std::string& relative, const std::string& contents, int64_t mtimeUtc = 1767268800)
    {
        const std::filesystem::path path = _path / FileHelper::ToFsPath(relative);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary | std::ios::trunc) << contents;
        SetTime(path, mtimeUtc);
        return path;
    }

    std::filesystem::path Folder(const std::string& relative)
    {
        const std::filesystem::path path = _path / FileHelper::ToFsPath(relative);
        std::filesystem::create_directories(path);
        return path;
    }

    static void SetTime(const std::filesystem::path& path, int64_t mtimeUtc)
    {
        using FileTime = std::filesystem::file_time_type;
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L && (!defined(_MSC_VER) || _MSC_VER >= 1930) // VS2019 claims the macro but lacks to_sys/from_sys
        const auto sys = std::chrono::sys_seconds(std::chrono::seconds(mtimeUtc));
        std::filesystem::last_write_time(path, std::chrono::file_clock::from_sys(sys));
#elif defined(_MSC_VER)
        // Older MSVC STL lacks file_clock::from_sys; file clock epoch is 1601-01-01
        const auto ticks =
            std::chrono::duration_cast<FileTime::duration>(std::chrono::seconds(mtimeUtc + 11644473600LL));
        std::filesystem::last_write_time(path, FileTime(ticks));
#else
        // Older libstdc++/libc++: derive the clock offset from the two clocks' "now"
        const auto offset = FileTime::clock::now().time_since_epoch() -
                            std::chrono::duration_cast<FileTime::duration>(
                                std::chrono::system_clock::now().time_since_epoch());
        const auto ticks = std::chrono::duration_cast<FileTime::duration>(std::chrono::seconds(mtimeUtc)) + offset;
        std::filesystem::last_write_time(path, FileTime(ticks));
#endif
    }

private:
    std::filesystem::path _path;
};
