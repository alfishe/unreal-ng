#pragma once

/// @file sessionwritemap.h
/// @brief Guest writes kept in memory on top of any disk: the medium
/// underneath is never modified.
///
/// Used for "write for this session only" (SD card WriteMode::Session, host
/// folder volumes). A write stores the sector in a sparse map; a write that
/// makes a sector equal to the medium again drops the entry, so software
/// that rewrites unchanged sectors costs nothing (DOSBox-X bios_disk.cpp
/// does the same). The result, medium plus changes, can be exported to a
/// plain image file.
///
/// Design: docs/inprogress/2026-09-21-profi/2026-09-25-ide-hdd-design.md §7.4.3,
/// shared with the SD card by tdd-storage-sd-ide-cd.md §1 (S3).

#include <array>
#include <map>
#include <memory>
#include <string>

#include "emulator/io/storage/iblockdevice.h"

class SessionWriteMap : public IBlockDevice
{
public:
    explicit SessionWriteMap(std::unique_ptr<IBlockDevice> base);

    uint64_t SectorCount() const override { return _base->SectorCount(); }
    bool ReadSector(uint64_t lba, uint8_t* dst) override;
    bool WriteSector(uint64_t lba, const uint8_t* src) override;
    bool IsWritable() const override { return true; }
    std::optional<BlockGeometry> NativeGeometry() const override { return _base->NativeGeometry(); }
    std::string Describe() const override { return _base->Describe() + " (session writes)"; }
    uint64_t ContentId() const override;

    IBlockDevice& Base() { return *_base; }
    const IBlockDevice& Base() const { return *_base; }

    /// Sectors that currently differ from the medium
    size_t ChangedSectors() const { return _sectors.size(); }

    /// Forget every change: reads show the medium again
    void Discard() { _sectors.clear(); }

    /// Is any sector in [first, first + count) changed
    bool ChangedIn(uint64_t first, uint64_t count) const
    {
        const auto it = _sectors.lower_bound(first);
        return it != _sectors.end() && it->first < first + count;
    }
    /// The changed sectors by number (a save writes them back)
    const std::map<uint64_t, std::array<uint8_t, kSectorSize>>& Changes() const { return _sectors; }

    /// A save replaced the medium's file: close the old medium first (a file
    /// open on Windows cannot be replaced), then put the new one under the
    /// changes. The layer has no medium in between: no reads then
    std::unique_ptr<IBlockDevice> ReleaseBase() { return std::move(_base); }
    void SetBase(std::unique_ptr<IBlockDevice> base) { _base = std::move(base); }

    /// Write the whole disk as the guest sees it (medium plus changes) to a
    /// plain image file. False on a host I/O error or an unreadable sector
    bool ExportTo(const std::string& path, std::string* error = nullptr);

private:
    std::unique_ptr<IBlockDevice> _base;
    std::map<uint64_t, std::array<uint8_t, kSectorSize>> _sectors;
};
