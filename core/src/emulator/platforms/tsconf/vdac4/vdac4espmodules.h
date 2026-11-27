#pragma once

/// @file vdac4espmodules.h
/// @brief The VDAC4 ESP's flash-side stores, kept in a host directory: the module cache (ZmCache) and the program
/// folder table (path.cfg, PathTable).
///
/// Ports of VDAC4 `ESP32 S3 Zero N8R8/Firmware` (MIT License, Copyright (c) andrewinsidelazarev):
///   include/zifi/zm_cache.hpp, src/zm_cache.cpp       -> EspModuleCache
///   include/zifi/path_table.hpp, src/path_table.cpp   -> EspPathTable
///   (src/elf_image.cpp is vdac4::EspElfImageProblem, esp/espelfloader.h: the loader's own check)
///
/// The directory stands for the ESP's LittleFS partition: `<dir>/zm/` holds the cached modules and their
/// `index.bin`, `<dir>/path.cfg` the folder table, written through `path.tmp` and a rename as the firmware does.
/// Like flash, these files are not part of the saved device state: time travel does not roll them back.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

struct EspModuleCacheEntry
{
    char name[13] = {};  // "PLAYER.ELF", empty: free slot
    uint32_t size = 0;
    uint32_t stamp = 0;  // FAT date << 16 | FAT time
    uint32_t used = 0;   // the cache's clock: when last taken
};

class EspModuleCache
{
public:
    static constexpr size_t kMaxEntries = 24;
    static constexpr size_t kBlockBytes = 4096;
    /// The LittleFS partition (0x1F0000) minus the firmware's 320 KB reserve
    static constexpr size_t kDefaultCapacity = 0x1F0000 - 320 * 1024;

    /// `directory` holds index.bin and the modules (created when missing)
    EspModuleCache(std::filesystem::path directory, size_t capacity = kDefaultCapacity);

    /// Read the index; a damaged or foreign one means an empty cache. Entries whose file is missing or of
    /// another length are dropped, files not in the index are removed
    void Load();
    static bool ValidName(const char* name);
    bool Has(const char* name, uint32_t size, uint32_t stamp) const;
    const EspModuleCacheEntry* Find(const char* name) const;
    /// Store a module; `busy` names the module that must not be evicted (the running app) or nullptr
    bool Store(const char* name, uint32_t stamp, const uint8_t* data, uint32_t size, const char* busy);
    /// The whole module (size from Find), marked as taken
    bool Read(const char* name, uint8_t* data, uint32_t size);
    /// Find + Read in one step, for another thread (the ESP runtime's worker: module_open). Every method locks
    /// the same mutex; a Find pointer is for the thread that changes the cache only
    bool ReadModule(const char* name, std::vector<uint8_t>& out);
    size_t Count() const;
    static size_t Footprint(size_t size);
    size_t FreeBytes() const;
    bool CanFit(uint32_t size, const char* busy) const;
    size_t Capacity() const { return _capacity; }

private:
    static constexpr uint32_t kMagic = 0x315A4D43;  // "CMZ1"
    static constexpr const char* kIndex = "index.bin";
    static constexpr size_t kIndexBytes = 8 + kMaxEntries * 28;

    EspModuleCacheEntry* Slot(const char* name);
    bool EvictOne(const char* busy);
    bool SaveIndex();
    void Drop(EspModuleCacheEntry* entry);

    // ZmStorage over the host directory
    bool StorageRead(const std::string& file, uint8_t* data, size_t size) const;
    bool StorageWrite(const std::string& file, const uint8_t* data, size_t size) const;
    void StorageRemove(const std::string& file) const;
    long StorageFileSize(const std::string& file) const;

    std::filesystem::path _directory;
    size_t _capacity;
    EspModuleCacheEntry _entries[kMaxEntries];
    uint32_t _clock = 0;
    mutable std::recursive_mutex _mutex;
};

struct EspPathEntry
{
    char name[13] = {};
    uint32_t serial = 0;
    uint32_t cluster = 0;
};

class EspPathTable
{
public:
    static constexpr size_t kMaxEntries = 64;
    static constexpr size_t kMaxName = 12;
    static constexpr size_t kMaxText = 256 + kMaxEntries * 32;

    static bool NormalizeName(const char* name, size_t length, char out[13]);

    void Clear() { _count = 0; }
    void Parse(const char* text, size_t length);
    /// The file's text; empty when it does not fit kMaxText
    std::string Format() const;
    bool Find(const char* name, uint32_t serial, uint32_t& cluster) const;
    bool Set(const char* name, uint32_t serial, uint32_t cluster);
    size_t Count() const { return _count; }

    /// path.cfg in `directory`: missing file = empty table
    bool LoadFile(const std::filesystem::path& directory);
    /// Through path.tmp and a rename
    bool SaveFile(const std::filesystem::path& directory) const;

private:
    int IndexOf(const char* name, uint32_t serial) const;
    void RemoveAt(size_t index);

    EspPathEntry _entries[kMaxEntries];
    size_t _count = 0;
};
