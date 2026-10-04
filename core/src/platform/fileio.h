#pragma once

/// @file fileio.h
/// @brief Files for the TTD session (Phase 4): one that only grows at its
/// end and can be made durable (fsync / FlushFileBuffers), and one read at
/// any offset. Paths are UTF-8 (FileHelper's rule): POSIX passes them as is,
/// Windows converts them to UTF-16 for the wide API. One implementation per
/// system family: platform/posix, platform/windows.
///
/// Worked example: AppendFile::Create("~/.unreal-ng/ttd/rec/segment-0000.ttd")
/// creates the file (it must not exist), Write() appends a record, Sync()
/// returns once the record survives a power loss; RandomAccessFile::Open on
/// the same path reads it back with ReadAt(offset, ...).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace platform
{
    class AppendFile
    {
    public:
        virtual ~AppendFile() = default;
        /// Create a new file; null (with the reason) when it exists or cannot be created
        static std::unique_ptr<AppendFile> Create(const std::string& utf8Path, std::string* error = nullptr);

        /// Append; false on a write error (disk full, device gone)
        virtual bool Write(const void* data, size_t size) = 0;
        /// What was written so far survives a crash of the process and a power loss
        virtual bool Sync() = 0;
        virtual uint64_t Size() const = 0;
        virtual void Close() = 0;
    };

    class RandomAccessFile
    {
    public:
        virtual ~RandomAccessFile() = default;
        /// Open for reading (others may keep writing or delete it); null when it cannot be opened
        static std::unique_ptr<RandomAccessFile> Open(const std::string& utf8Path, std::string* error = nullptr);

        virtual uint64_t Size() const = 0;
        /// Read exactly @p size bytes at @p offset; false when the file is shorter or a read fails
        virtual bool ReadAt(uint64_t offset, void* out, size_t size) const = 0;
    };
}  // namespace platform
