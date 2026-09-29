#pragma once

/// @file memorydisk.h
/// @brief A zero-filled disk held in memory: tests, "new blank card / disk".

#include <memory>
#include <string>

#include "emulator/io/storage/iblockdevice.h"

class MemoryDisk : public IBlockDevice
{
public:
    explicit MemoryDisk(uint64_t sectors, bool writable = true);

    uint64_t SectorCount() const override { return _sectors; }
    bool ReadSector(uint64_t lba, uint8_t* dst) override;
    bool WriteSector(uint64_t lba, const uint8_t* src) override;
    bool IsWritable() const override { return _writable; }
    std::string Describe() const override { return "memory disk"; }
    uint64_t ContentId() const override { return _contentId; }

    void SetWritable(bool writable) { _writable = writable; }

    /// Direct access to the whole disk (sector n at n * 512)
    uint8_t* Data() { return _data.get(); }
    const uint8_t* Data() const { return _data.get(); }

private:
    uint64_t _sectors;
    bool _writable;
    std::unique_ptr<uint8_t[]> _data;  // value-initialized: zero fill without a per-byte loop
    uint64_t _contentId;
};
