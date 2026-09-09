#pragma once

#ifndef COMMON_SHMHELPER_H
#define COMMON_SHMHELPER_H

/**
 * @file shmhelper.h
 * @brief Cross-Platform Shared Memory IPC Abstraction Subsystem
 *
 * @details
 * ## Overview & Purpose
 * `shmhelper.h` provides a unified, cross-platform C++ interface for creating,
 * opening, synchronizing, and destroying named shared memory segments in OS RAM.
 *
 * It acts as the core IPC (Inter-Process Communication) bridge between Unreal-NG
 * emulator components (e.g., VideoWall, Screen Viewer, Debugger) and external
 * monitoring/profiling tools (such as `process-workload.py` or diagnostic daemons).
 *
 * ## Key Architecture & Benefits
 * - **Zero Disk I/O**: Operates purely in volatile system RAM (kernel shared memory pages).
 * - **Cross-Platform Compatibility**:
 *   - **POSIX (macOS / Linux)**: Encapsulates `shm_open`, `ftruncate`, `mmap`, and `shm_unlink`.
 *   - **Windows (Win32)**: Encapsulates `CreateFileMappingA`, `MapViewOfFile`, and `OpenFileMappingA`
 *     (using the `Local\` namespace).
 * - **High Throughput**: Enables zero-copy binary layout publishing and high-frequency lock-free ringbuffers.
 * - **Interoperability**: Named shared memory segments can be read seamlessly by Python 3.8+
 *   via `multiprocessing.shared_memory.SharedMemory(name=...)`.
 *
 * ## Usage Examples
 *
 * ### Example 1: Producer Application (Creating and Writing to Shared Memory)
 * ```cpp
 * #include <common/shmhelper.h>
 * #include <cstring>
 *
 * struct MyStatusHeader {
 *     char magic[4];    // "VWST"
 *     uint32_t version; // 1
 *     uint32_t count;   // 4 tiles
 * };
 *
 * ipc::ShmHandle shm;
 * if (ipc::ShmCreate(shm, "unreal_videowall_status", 1024)) {
 *     auto* header = reinterpret_cast<MyStatusHeader*>(shm.data);
 *     std::memcpy(header->magic, "VWST", 4);
 *     header->version = 1;
 *     header->count = 4;
 *
 *     ipc::ShmSync(shm); // Ensure write visibility across process boundaries
 * }
 *
 * // Cleanup when exiting:
 * ipc::ShmClose(shm);
 * ```
 *
 * ### Example 2: Consumer Application in C++ (Attaching and Reading)
 * ```cpp
 * #include <common/shmhelper.h>

 * ipc::ShmHandle shm;
 * if (ipc::ShmOpen(shm, "unreal_videowall_status", 1024)) {
 *     const auto* header = reinterpret_cast<const MyStatusHeader*>(shm.data);
 *     if (std::memcmp(header->magic, "VWST", 4) == 0) {
 *         // Read live data from RAM...
 *     }
 *     ipc::ShmClose(shm);
 * }
 * ```
 *
 * ### Example 3: Consumer Application in Python (`process-workload.py`)
 * ```python
 * from multiprocessing.shared_memory import SharedMemory
 * import struct
 *
 * try:
 *     shm = SharedMemory(name="unreal_videowall_status", create=False)
 *     magic, version, tile_count = struct.unpack("<4sII", shm.buf[:12])
 *     shm.close()
 * except FileNotFoundError:
 *     pass
 * ```
 */

#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <semaphore.h>
#include <cerrno>
#include <cstring>
#include <ctime>
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif
#endif

namespace ipc {

/**
 * @brief Handle representing an active shared memory segment mapping.
 */
struct ShmHandle {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;  ///< Win32 file mapping handle
#else
    int fd = -1;                           ///< POSIX shared memory file descriptor
#endif
    void* data = nullptr;                 ///< Pointer to mapped memory block in virtual address space
    size_t size = 0;                      ///< Size of the shared memory region in bytes
    std::string name;                     ///< System name of the shared memory region
    bool isOwner = false;                 ///< True if this process created the segment (unlinks on close)
};

/**
 * @brief Creates a new named shared memory segment and maps it into the process address space.
 * @param shm Output handle holding allocation state and mapped memory pointer.
 * @param name Unique name of the shared memory segment (e.g., "unreal_videowall_status").
 * @param size Allocation size in bytes.
 * @return True on successful creation and mapping, false on failure.
 */
inline bool ShmCreate(ShmHandle& shm, const std::string& name, size_t size) {
    shm.name = name;
    shm.size = size;
    shm.isOwner = true;

#ifdef _WIN32
    std::string winName = "Local\\" + name;

    shm.handle = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE | SEC_COMMIT,
        static_cast<DWORD>(size >> 32),
        static_cast<DWORD>(size & 0xFFFFFFFF),
        winName.c_str()
    );

    if (shm.handle == nullptr) {
        return false;
    }

    shm.data = MapViewOfFile(
        shm.handle,
        FILE_MAP_ALL_ACCESS,
        0, 0,
        size
    );

    if (shm.data == nullptr) {
        CloseHandle(shm.handle);
        shm.handle = INVALID_HANDLE_VALUE;
        return false;
    }
#else
    std::string posixName = "/" + name;

    shm_unlink(posixName.c_str());

    shm.fd = shm_open(posixName.c_str(), O_CREAT | O_RDWR, 0666);
    if (shm.fd == -1) {
        return false;
    }

    if (ftruncate(shm.fd, static_cast<off_t>(size)) == -1) {
        close(shm.fd);
        shm_unlink(posixName.c_str());
        shm.fd = -1;
        return false;
    }

    shm.data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm.fd, 0);
    if (shm.data == MAP_FAILED) {
        close(shm.fd);
        shm_unlink(posixName.c_str());
        shm.fd = -1;
        shm.data = nullptr;
        return false;
    }
#endif

    return true;
}

/**
 * @brief Opens an existing named shared memory segment created by another process.
 * @param shm Output handle holding mapped state.
 * @param name Unique name of the shared memory segment.
 * @param size Expected size in bytes.
 * @return True on success, false if the segment does not exist or mapping fails.
 */
inline bool ShmOpen(ShmHandle& shm, const std::string& name, size_t size) {
    shm.name = name;
    shm.size = size;
    shm.isOwner = false;

#ifdef _WIN32
    std::string winName = "Local\\" + name;

    shm.handle = OpenFileMappingA(
        FILE_MAP_ALL_ACCESS,
        FALSE,
        winName.c_str()
    );

    if (shm.handle == nullptr) {
        return false;
    }

    shm.data = MapViewOfFile(
        shm.handle,
        FILE_MAP_ALL_ACCESS,
        0, 0,
        size
    );

    if (shm.data == nullptr) {
        CloseHandle(shm.handle);
        shm.handle = INVALID_HANDLE_VALUE;
        return false;
    }
#else
    std::string posixName = "/" + name;

    shm.fd = shm_open(posixName.c_str(), O_RDWR, 0666);
    if (shm.fd == -1) {
        return false;
    }

    shm.data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm.fd, 0);
    if (shm.data == MAP_FAILED) {
        close(shm.fd);
        shm.fd = -1;
        shm.data = nullptr;
        return false;
    }
#endif

    return true;
}

/**
 * @brief Unmaps the shared memory segment and releases associated OS handles.
 * @param shm Handle to close. If isOwner is true on POSIX systems, shm_unlink is also invoked.
 */
inline void ShmClose(ShmHandle& shm) {
    if (shm.data == nullptr) {
        return;
    }

#ifdef _WIN32
    UnmapViewOfFile(shm.data);
    if (shm.handle != INVALID_HANDLE_VALUE) {
        CloseHandle(shm.handle);
        shm.handle = INVALID_HANDLE_VALUE;
    }
#else
    munmap(shm.data, shm.size);
    if (shm.fd != -1) {
        close(shm.fd);
        shm.fd = -1;
    }
    if (shm.isOwner) {
        std::string posixName = "/" + shm.name;
        shm_unlink(posixName.c_str());
    }
#endif

    shm.data = nullptr;
    shm.size = 0;
}

/**
 * @brief Flushes memory writes to ensure visibility across process CPU cores.
 * @param shm Handle of memory region to synchronize.
 */
inline void ShmSync(ShmHandle& shm) {
    if (shm.data == nullptr) {
        return;
    }

#ifdef _WIN32
    FlushViewOfFile(shm.data, shm.size);
#else
    __sync_synchronize();
    msync(shm.data, shm.size, MS_SYNC | MS_INVALIDATE);
#endif
}

} // namespace ipc

#endif // COMMON_SHMHELPER_H
