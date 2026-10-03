#pragma once

/// @file hostwritehold.h
/// @brief The top of a write-through block stack: passes every access to the
/// image file, except while held (a time-travel replay runs, FR-20). Held,
/// guest writes stay in memory - the replayed program reads what it wrote -
/// and nothing reaches the host file; released, the held sectors are written
/// to the file, the state of that moment (a persist refused during a replay
/// is retried when the gate opens). Design: docs/inprogress/2026-09-25-ttd-v2-migration/
/// phase-3-replay-inputs-tdd.md §4.7

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

#include "emulator/io/storage/iblockdevice.h"

class HostWriteHold : public IBlockDevice
{
public:
    explicit HostWriteHold(std::unique_ptr<IBlockDevice> base) : _base(std::move(base)) {}

    uint64_t SectorCount() const override { return _base->SectorCount(); }
    bool ReadSector(uint64_t lba, uint8_t* dst) override
    {
        auto it = _held.find(lba);
        if (it == _held.end())
            return _base->ReadSector(lba, dst);
        std::memcpy(dst, it->second.data(), kSectorSize);
        return true;
    }
    bool WriteSector(uint64_t lba, const uint8_t* src) override
    {
        if (!_holding)
            return _base->WriteSector(lba, src);
        if (lba >= _base->SectorCount() || !_base->IsWritable())
            return false;   // what the file would answer
        std::memcpy(_held[lba].data(), src, kSectorSize);
        return true;
    }
    bool IsWritable() const override { return _base->IsWritable(); }
    std::optional<BlockGeometry> NativeGeometry() const override { return _base->NativeGeometry(); }
    std::string Describe() const override { return _base->Describe(); }
    uint64_t ContentId() const override { return _base->ContentId(); }

    /// Hold host writes (true) or release them (false): releasing writes every
    /// held sector to the file. False when one of them could not be written
    bool SetHolding(bool holding)
    {
        _holding = holding;
        if (holding)
            return true;
        bool ok = true;
        for (const auto& [lba, bytes] : _held)
            ok = _base->WriteSector(lba, bytes.data()) && ok;
        _held.clear();
        return ok;
    }
    bool Holding() const { return _holding; }
    size_t HeldSectors() const { return _held.size(); }

private:
    std::unique_ptr<IBlockDevice> _base;
    bool _holding = false;
    std::unordered_map<uint64_t, std::array<uint8_t, kSectorSize>> _held;
};
