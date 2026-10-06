#pragma once

/// @file sparsememorydisk.h
/// @brief A disk held in memory that stores only what was written: 64 KiB
/// chunks allocated by the first non-zero write, freed when written back to
/// zeros. A blank card or hard disk costs nothing until the guest formats it.
/// Design: docs/inprogress/2026-10-05-media-multisource/phases/c10-sparse-memory.md §3.

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "emulator/io/storage/iblockdevice.h"

class SparseMemoryDisk : public IBlockDevice
{
public:
    static constexpr uint64_t kChunkSectors = 128;  ///< 64 KiB

    explicit SparseMemoryDisk(uint64_t sectors);

    uint64_t SectorCount() const override { return _sectors; }
    bool ReadSector(uint64_t lba, uint8_t* dst) override;
    bool WriteSector(uint64_t lba, const uint8_t* src) override;
    bool IsWritable() const override { return true; }
    std::string Describe() const override { return "memory disk"; }
    uint64_t ContentId() const override { return _contentId; }
    /// Up to the next stored chunk
    uint64_t ZeroRun(uint64_t lba) override;

    /// The memory the stored chunks take
    uint64_t StoredBytes() const { return _chunks.size() * kChunkSectors * kSectorSize; }

private:
    uint64_t _sectors;
    uint64_t _contentId;
    std::map<uint64_t, std::unique_ptr<uint8_t[]>> _chunks;  ///< chunk index -> 64 KiB
};
