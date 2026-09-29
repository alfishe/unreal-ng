#pragma once

/// @file readonlyguard.h
/// @brief Any disk made read-only: reads pass through, every write fails. The
/// peripheral turns the failure into its own error (SD data response #0D, ATA
/// ABRT), so a read-only medium behaves the same in every drive.

#include <memory>
#include <string>

#include "emulator/io/storage/iblockdevice.h"

class ReadOnlyGuard : public IBlockDevice
{
public:
    explicit ReadOnlyGuard(std::unique_ptr<IBlockDevice> base) : _base(std::move(base)) {}

    uint64_t SectorCount() const override { return _base->SectorCount(); }
    bool ReadSector(uint64_t lba, uint8_t* dst) override { return _base->ReadSector(lba, dst); }
    bool WriteSector(uint64_t, const uint8_t*) override { return false; }
    bool IsWritable() const override { return false; }
    std::optional<BlockGeometry> NativeGeometry() const override { return _base->NativeGeometry(); }
    std::string Describe() const override { return _base->Describe() + " (read-only)"; }
    uint64_t ContentId() const override { return _base->ContentId(); }

    IBlockDevice& Base() { return *_base; }

private:
    std::unique_ptr<IBlockDevice> _base;
};
