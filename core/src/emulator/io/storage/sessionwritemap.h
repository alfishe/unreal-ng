#pragma once

/// @file sessionwritemap.h
/// @brief Guest writes kept on top of any disk: the medium underneath is
/// never modified.
///
/// Used for "write for this session only" (SD card WriteMode::Session, host
/// folder volumes, composites, blank media). A write stores the sector; a
/// write that makes a sector equal to the medium again drops it, so software
/// that rewrites unchanged sectors costs nothing (DOSBox-X bios_disk.cpp does
/// the same). The result, medium plus changes, can be exported to a plain
/// image file.
///
/// The changed sectors live in two tiers (C10d): in memory up to a limit
/// (`SetDefaultMemoryLimit`, [MEDIA] SessionMemoryLimit, 128 MiB), then in a
/// spill file on disk, in 64 KiB slots, one per chunk of 128 sectors, the
/// chunks written longest ago first. A sector is in one tier at a time.
///
/// Design: docs/inprogress/2026-09-21-profi/2026-09-25-ide-hdd-design.md §7.4.3,
/// shared with the SD card by tdd-storage-sd-ide-cd.md §1 (S3); the spill:
/// docs/inprogress/2026-10-05-media-multisource/phases/c10d-session-spill.md.

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "emulator/io/storage/changeview.h"
#include "emulator/io/storage/iblockdevice.h"

class SessionSpillFile;

class SessionWriteMap : public IBlockDevice, public IChangeView
{
public:
    static constexpr uint64_t kChunkSectors = 128;  ///< a spill slot: 64 KiB
    /// What a sector held in memory costs (the data and its map node; measured, c10-sparse-memory.md §8.2)
    static constexpr uint64_t kHotEntryBytes = kSectorSize + 48;

    /// The memory limit new sessions get (0: none) and the folder their spill files go to (empty: the system temp
    /// folder). Process-wide: [MEDIA] SessionMemoryLimit / SpillFolder of the last loaded config
    static void SetDefaultMemoryLimit(uint64_t bytes);
    static uint64_t DefaultMemoryLimit();
    static void SetSpillFolder(const std::string& folder);
    static std::string SpillFolder();
    /// Chunks moved to spill files by every session of the process so far (tests, reports)
    static uint64_t TotalSpilledChunks();

    explicit SessionWriteMap(std::unique_ptr<IBlockDevice> base);
    ~SessionWriteMap() override;

    uint64_t SectorCount() const override { return _base->SectorCount(); }
    bool ReadSector(uint64_t lba, uint8_t* dst) override;
    bool WriteSector(uint64_t lba, const uint8_t* src) override;
    bool IsWritable() const override { return true; }
    /// The base's run, cut at the next changed sector (a changed sector itself: 0)
    uint64_t ZeroRun(uint64_t lba) override;
    std::optional<BlockGeometry> NativeGeometry() const override { return _base->NativeGeometry(); }
    std::string Describe() const override { return _base->Describe() + " (session writes)"; }
    uint64_t ContentId() const override;

    IBlockDevice& Base() { return *_base; }
    const IBlockDevice& Base() const { return *_base; }

    // IChangeView: the changed sectors of both tiers
    size_t ChangedSectors() const override { return _hot.size() + _spilledSectors; }
    std::optional<uint64_t> NextChanged(uint64_t lba) const override;
    bool ReadChanged(uint64_t lba, uint8_t* dst) const override;

    /// Forget every change: reads show the medium again
    void Discard();

    /// Moves with every write that changes the layer (a saved session delta records it)
    uint64_t Generation() const { return _generation; }

    /// This session's memory limit (0: none); a lower one spills at once
    void SetMemoryLimit(uint64_t bytes);
    uint64_t MemoryLimit() const { return _limit; }
    /// Bytes the in-memory tier holds, sectors and bytes in the spill file
    uint64_t HotBytes() const { return _hot.size() * kHotEntryBytes; }
    size_t SpilledSectors() const { return _spilledSectors; }
    /// What the bookkeeping of the spill tier and the write order holds in memory (an estimate by node sizes)
    uint64_t IndexBytes() const;
    /// The spill file ("" before the first spill; on POSIX it is already unlinked)
    std::string SpillPath() const;
    /// A spill could not be written (disk full, no folder): the sectors stay in memory, over the limit
    bool SpillFailed() const { return _spillFailed; }

    /// A save replaced the medium's file: close the old medium first (a file
    /// open on Windows cannot be replaced), then put the new one under the
    /// changes. The layer has no medium in between: no reads then
    std::unique_ptr<IBlockDevice> ReleaseBase() { return std::move(_base); }
    void SetBase(std::unique_ptr<IBlockDevice> base) { _base = std::move(base); }

    /// Write the whole disk as the guest sees it (medium plus changes) to a
    /// plain image file. False on a host I/O error or an unreadable sector
    bool ExportTo(const std::string& path, std::string* error = nullptr);

private:
    struct Spilled
    {
        uint64_t slot = 0;
        uint64_t mask[2] = {0, 0};  ///< bit i: sector i of the chunk is in the slot
        bool Has(uint64_t i) const { return (mask[i / 64] >> (i % 64)) & 1; }
        void Set(uint64_t i) { mask[i / 64] |= 1ull << (i % 64); }
        void Clear(uint64_t i) { mask[i / 64] &= ~(1ull << (i % 64)); }
        bool Empty() const { return !mask[0] && !mask[1]; }
    };

    void Touch(uint64_t chunk);
    void Spill();
    bool SpillChunk(uint64_t chunk);
    /// Drop a sector from the spill tier (it was written again); its data, when wanted, first
    void Unspill(uint64_t lba);
    /// A changed sector into or out of the content hash (XOR: the same call does both)
    void ToggleHash(uint64_t lba, const uint8_t* data);

    std::unique_ptr<IBlockDevice> _base;
    std::map<uint64_t, std::array<uint8_t, kSectorSize>> _hot;
    std::map<uint64_t, Spilled> _spilled;  ///< chunk -> slot and mask
    size_t _spilledSectors = 0;
    std::vector<uint64_t> _freeSlots;
    uint64_t _nextSlot = 0;
    std::unique_ptr<SessionSpillFile> _file;
    bool _spillFailed = false;
    uint64_t _limit = 0;

    // Write order of the chunks with sectors in memory: (chunk, stamp); a stale entry's stamp is not the chunk's
    std::deque<std::pair<uint64_t, uint64_t>> _order;
    std::unordered_map<uint64_t, uint64_t> _lastWrite;
    uint64_t _stamp = 0;

    uint64_t _contentHash = 0;  ///< XOR over the changed sectors of a hash of (lba, data)
    uint64_t _generation = 0;
};
