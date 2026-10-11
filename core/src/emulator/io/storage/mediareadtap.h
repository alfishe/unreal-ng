#pragma once

/// @file mediareadtap.h
/// @brief The top of every block medium's stack: reads pass to the image, and
/// time travel records them (or, replaying, hands back the recorded bytes) -
/// IMediaReadJournal. Unbound or with no journal it only passes through. Before
/// a write reaches the image the media manager hears of it (its undo log keeps
/// the sector as it was: IMediaWriteObserver).

#include <memory>
#include <string>
#include <utility>

#include "emulator/io/storage/iblockdevice.h"
#include "emulator/media/mediareadjournal.h"

/// Told before a guest write reaches a medium's image; @p below is the stack
/// under the tap (reading it records nothing)
class IMediaWriteObserver
{
public:
    virtual ~IMediaWriteObserver() = default;
    virtual void BeforeSectorWrite(const std::string& slot, uint64_t lba, IBlockDevice& below) = 0;
};

class MediaReadTap : public IBlockDevice
{
public:
    explicit MediaReadTap(std::unique_ptr<IBlockDevice> base) : _base(std::move(base)) {}

    /// @p journal: where the media manager keeps the current journal (null = none)
    /// @p writes: told before every write (null = none)
    void Bind(IMediaReadJournal* const* journal, std::string slot, IMediaWriteObserver* writes = nullptr)
    {
        _journal = journal;
        _slot = std::move(slot);
        _writes = writes;
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
    bool WriteSector(uint64_t lba, const uint8_t* src) override
    {
        if (_writes)
            _writes->BeforeSectorWrite(_slot, lba, *_base);
        return _base->WriteSector(lba, src);
    }
    uint64_t ZeroRun(uint64_t lba) override { return _base->ZeroRun(lba); }
    bool IsWritable() const override { return _base->IsWritable(); }
    std::optional<BlockGeometry> NativeGeometry() const override { return _base->NativeGeometry(); }
    std::string Describe() const override { return _base->Describe(); }
    uint64_t ContentId() const override { return _base->ContentId(); }

    IBlockDevice& Base() { return *_base; }
    /// Put a layer between the tap and the stack under it (a write-through medium becoming a session one, F13)
    template <typename Wrap>
    void WrapBase(Wrap wrap)
    {
        _base = wrap(std::move(_base));
    }

private:
    std::unique_ptr<IBlockDevice> _base;
    IMediaReadJournal* const* _journal = nullptr;
    std::string _slot;
    IMediaWriteObserver* _writes = nullptr;
};
