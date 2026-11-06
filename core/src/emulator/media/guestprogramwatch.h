#pragma once

/// @file guestprogramwatch.h
/// @brief Which program file the guest is reading from a block medium (the Next's SD card): a game started from
/// NextZXOS's Browser reaches the machine without the host loading any file, so nothing else could key its game
/// profile. A run of consecutive sectors read from inside one program file (NEX, SNX, SNA, TAP, ...) names the file;
/// its bytes go out as NC_GUEST_PROGRAM, and the controller manager treats it like a file it loaded itself.

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "emulator/io/storage/mediareadtap.h"

class EmulatorContext;

class GuestProgramWatch : public IMediaReadObserver
{
public:
    explicit GuestProgramWatch(EmulatorContext* context) : _context(context) {}

    /// Consecutive sectors read inside one file before it counts as the program being loaded: the directory and
    /// the FAT are read in runs too, but they belong to no file, and a header peek is a few sectors
    static constexpr uint32_t kRunSectors = 16;
    /// Larger files are not read as a whole (a disk image of a hard disk, a video)
    static constexpr uint64_t kMaxFileBytes = 16u * 1024 * 1024;

    void OnSectorRead(const std::string& slot, uint64_t lba, IBlockDevice& below) override;
    /// The medium of @p slot changed (inserted, ejected, written to): the file map is built again when needed
    void Forget(const std::string& slot);
    /// A medium went into or out of @p slot: everything known about it is dropped
    void Reset(const std::string& slot);

    /// Whether a file name is a program the machine can load (extension, case-insensitive)
    static bool IsProgramName(const std::string& name);

private:
    struct Extent
    {
        uint64_t first = 0;
        uint64_t last = 0;      ///< inclusive
        size_t file = 0;
    };
    struct File
    {
        std::string path;
        uint64_t size = 0;
    };
    struct SlotState
    {
        uint64_t nextLba = UINT64_MAX;
        uint64_t runStart = 0;  ///< the first sector of the current run of consecutive sectors
        uint32_t run = 0;
        bool indexed = false;
        std::vector<File> files;
        std::vector<Extent> extents;  ///< sorted by first
        size_t lastReported = SIZE_MAX;
        std::chrono::steady_clock::time_point lastReportedAt;
    };

    void BuildIndex(SlotState& state, IBlockDevice& below);

    EmulatorContext* _context = nullptr;
    std::mutex _mutex;
    std::map<std::string, SlotState> _slots;
};
