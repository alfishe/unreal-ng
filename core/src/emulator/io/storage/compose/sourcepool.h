#pragma once

/// @file sourcepool.h
/// @brief Everything a composite medium reads its file data from, opened once
/// and shared by every layer that names it: host files (read through a small
/// LRU of open streams) and source devices (FAT / ISO images, from phase C3).
/// Design: docs/inprogress/2026-10-05-media-multisource/tdd.md §2 (SourcePool),
/// NFR-M3 / NFR-M4.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <list>
#include <memory>
#include <string>
#include <vector>

class IBlockDevice;

class SourcePool
{
public:
    /// Open host streams kept at once, across every layer (NFR-M4)
    static constexpr size_t kMaxOpenFiles = 8;

    /// Register a host file; `size` is the size the folder scan saw
    uint32_t AddHostFile(const std::filesystem::path& hostPath, uint64_t size);
    /// Register a source device; returns its index for FileData::source. A
    /// non-empty `key` (a canonical image path, plus a partition) lets later
    /// layers find the same device instead of opening it again (NFR-M3)
    uint16_t AddDevice(std::shared_ptr<IBlockDevice> device, std::string key = {});
    /// The device registered under `key`, or -1
    int FindDevice(const std::string& key) const;
    std::shared_ptr<IBlockDevice> DevicePtr(uint16_t index) const { return _devices[index]; }
    const std::string& DeviceKey(uint16_t index) const { return _deviceKeys[index]; }

    /// Read up to `wanted` bytes of host file `file` at `offset` into `dst`.
    /// Returns the bytes read; a file that shrank or vanished since the scan
    /// reads short (once per file a warning is recorded)
    size_t ReadHost(uint32_t file, uint64_t offset, uint8_t* dst, size_t wanted);

    IBlockDevice& Device(uint16_t index) { return *_devices[index]; }
    size_t DeviceCount() const { return _devices.size(); }
    size_t HostFileCount() const { return _hostFiles.size(); }
    /// A host file as the folder scan saw it (S4 conflict checks)
    const std::filesystem::path& HostPath(uint32_t file) const { return _hostFiles[file].path; }
    uint64_t HostSize(uint32_t file) const { return _hostFiles[file].size; }
    size_t OpenStreams() const { return _open.size(); }

    /// Problems met while serving reads (a host file that shrank or vanished)
    const std::vector<std::string>& Warnings() const { return _warnings; }

private:
    struct HostFile
    {
        std::filesystem::path path;
        uint64_t size = 0;
        bool warned = false;
    };
    struct OpenFile
    {
        uint32_t file;
        std::ifstream stream;
    };

    std::vector<HostFile> _hostFiles;
    std::vector<std::shared_ptr<IBlockDevice>> _devices;
    std::vector<std::string> _deviceKeys;
    std::list<OpenFile> _open;  ///< most recently used first
    std::vector<std::string> _warnings;
};
