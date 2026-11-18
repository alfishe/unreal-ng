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

        /// Append; false on a write error (disk full, device gone): LastError() says which
        virtual bool Write(const void* data, size_t size) = 0;
        /// What was written so far survives a crash of the process and a power loss
        virtual bool Sync() = 0;
        virtual uint64_t Size() const = 0;
        virtual void Close() = 0;

        /// Why the last Write or Sync failed, as the system words it ("write: No space left on device"); empty
        /// while none did
        const std::string& LastError() const { return _lastError; }
        /// The last failure was a full disk or an exhausted quota (ENOSPC, EDQUOT; ERROR_DISK_FULL,
        /// ERROR_HANDLE_DISK_FULL): the only failure a caller may call "disk full"
        bool LastErrorNoSpace() const { return _lastErrorNoSpace; }

    protected:
        std::string _lastError;
        bool _lastErrorNoSpace = false;
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
