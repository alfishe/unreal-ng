#pragma once

/// @file mediareadtap.h
/// @brief The top of every block medium's stack: reads pass to the image, and
/// time travel records them (or, replaying, hands back the recorded bytes) -
/// IMediaReadJournal. Unbound or with no journal it only passes through.

#include <memory>
#include <string>

#include "emulator/io/storage/iblockdevice.h"
#include "emulator/media/mediareadjournal.h"

class MediaReadTap : public IBlockDevice
{
public:
    explicit MediaReadTap(std::unique_ptr<IBlockDevice> base) : _base(std::move(base)) {}

    /// @p journal: where the media manager keeps the current journal (null = none)
    void Bind(IMediaReadJournal* const* journal, std::string slot)
    {
        _journal = journal;
        _slot = std::move(slot);
    }

    uint64_t SectorCount() const override { return _base->SectorCount(); }
    bool ReadSector(uint64_t lba, uint8_t* dst) override
    {
        IMediaReadJournal* journal = _journal ? *_journal : nullptr;
        if (journal && journal->Playing() && journal->Play(_slot, lba, dst, kSectorSize))
            return true;
        const bool ok = _base->ReadSector(lba, dst);
        if (journal && ok && !journal->Playing())
            journal->Record(_slot, lba, dst, kSectorSize);
        return ok;
    }
    bool WriteSector(uint64_t lba, const uint8_t* src) override { return _base->WriteSector(lba, src); }
    bool IsWritable() const override { return _base->IsWritable(); }
    std::optional<BlockGeometry> NativeGeometry() const override { return _base->NativeGeometry(); }
    std::string Describe() const override { return _base->Describe(); }
    uint64_t ContentId() const override { return _base->ContentId(); }

    IBlockDevice& Base() { return *_base; }

private:
    std::unique_ptr<IBlockDevice> _base;
    IMediaReadJournal* const* _journal = nullptr;
    std::string _slot;
};
