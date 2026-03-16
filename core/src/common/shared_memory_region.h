#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

/// Cross-platform shared memory region abstraction.
/// Producer creates a named region; consumer attaches to it by name.
/// Extracted from Memory::AllocateAndExportMemoryToMmap() for reuse.
class SharedMemoryRegion
{
public:
    SharedMemoryRegion() = default;
    ~SharedMemoryRegion();

    // Non-copyable, movable
    SharedMemoryRegion(const SharedMemoryRegion&) = delete;
    SharedMemoryRegion& operator=(const SharedMemoryRegion&) = delete;
    SharedMemoryRegion(SharedMemoryRegion&& other) noexcept;
    SharedMemoryRegion& operator=(SharedMemoryRegion&& other) noexcept;

    /// Create a new shared memory region (producer side).
    /// @param name Platform-appropriate name (POSIX: "/my_shm", Windows: "Local\\my_shm")
    /// @param size Region size in bytes
    /// @return true on success
    bool create(const std::string& name, size_t size);

    /// Attach to an existing shared memory region (consumer/observer side).
    /// @param name Shared memory name to attach to
    /// @return true on success
    bool attach(const std::string& name);

    /// Resize the region (producer only). Unmaps, resizes, remaps.
    /// @param newSize New size in bytes
    /// @return true on success, false if not owner or resize failed
    bool resize(size_t newSize);

    /// Close and unmap the region. If owner, also unlinks the shared memory object.
    void close();

    /// @return Pointer to the mapped memory, or nullptr if not mapped
    void* data() const { return _ptr; }

    /// @return Mapped region size in bytes
    size_t size() const { return _size; }

    /// @return Shared memory name
    const std::string& name() const { return _name; }

    /// @return true if this instance created (owns) the region
    bool isOwner() const { return _isOwner; }

    /// @return true if the region is currently mapped
    bool isMapped() const { return _ptr != nullptr; }

private:
    void reset();

    std::string _name;
    size_t _size = 0;
    void* _ptr = nullptr;
    bool _isOwner = false;

#ifdef _WIN32
    HANDLE _handle = INVALID_HANDLE_VALUE;
#else
    int _fd = -1;
#endif
};
