#include "stdafx.h"

#include "ttdvdac2.h"

#include "emulator/platforms/tsconf/vdac2card.h"

#include <algorithm>
#include <bit>

#ifdef ENABLE_VDAC2
#include <eve/eve.h>
#endif

namespace ttd
{

size_t TTDVdac2Memory::TTDStateSize() const { return _card.TtdMemorySize(); }
void TTDVdac2Memory::TTDSaveState(uint8_t* dst) const { _card.TtdSaveMemory(dst); }
void TTDVdac2Memory::TTDSaveStateTo(std::vector<uint8_t>& out) const
{
    out.resize(_card.TtdMemorySize());
    out.resize(_card.TtdSaveMemory(out.data()));
}
void TTDVdac2Memory::TTDLoadState(const uint8_t* src) { _card.TtdLoadMemory(src); }
uint64_t TTDVdac2Memory::TTDHashState() const { return _card.TtdMemoryHash(); }

/// region <Time-travel engine regions>

#ifdef ENABLE_VDAC2

namespace
{
constexpr TTDRegionId kRegionIds[] = {TTDRegionId::Vdac2GraphicsMemory, TTDRegionId::Vdac2DisplayList0,
                                      TTDRegionId::Vdac2DisplayList1,   TTDRegionId::Vdac2Registers,
                                      TTDRegionId::Vdac2CommandFifo,    TTDRegionId::Vdac2Special,
                                      TTDRegionId::Vdac2Inflight};
}  // namespace

void TTDVdac2Memory::TTDRegions(std::vector<TTDDeviceRegion>& out)
{
    _regionCount = 0;
    EveChip* chip = _card.Chip();
    if (!chip)
        return;
    const size_t count = std::min(EveRegionCount(chip), kMaxRegions);
    for (size_t i = 0; i < count; ++i)
    {
        EveRegion region{};
        EveGetRegion(chip, i, &region);
        _trackers[i].Bind(region.base, region.size);

        TTDDeviceRegion r;
        r.desc.id = kRegionIds[i];
        r.desc.name = std::string("vdac2.") + region.name;
        r.desc.ownerType = static_cast<uint16_t>(PeripheralId::Vdac2Memory);
        r.desc.memory = region.base;
        r.desc.bytes = static_cast<uint32_t>(region.size);
        r.desc.pieces = _trackers[i].Pieces();
        // The chip rebuilds what it derives from its memory (as after v1's blob)
        r.desc.onRestored = [chip]() { EveMemoryRestored(chip); };
        r.tracker = &_trackers[i];
        out.push_back(r);
    }
    _regionCount = count;
}

void TTDVdac2Memory::TTDArmRegions(bool on)
{
    // The chip marks every write itself; arming only starts listening
    if (on && _card.Chip())
        EveClearDirty(_card.Chip());
    _armed = on;
}

void TTDVdac2Memory::TTDBeforeCapture()
{
    EveChip* chip = _card.Chip();
    if (!_armed || !chip)
        return;
    for (size_t i = 0; i < _regionCount; ++i)
    {
        EveRegion region{};
        EveGetRegion(chip, i, &region);
        const size_t words = (region.pageCount + 63) / 64;
        for (size_t w = 0; w < words; ++w)
            for (uint64_t bits = region.dirty[w]; bits != 0; bits &= bits - 1)
            {
                const size_t page = w * 64 + static_cast<size_t>(std::countr_zero(bits));
                if (page < region.pageCount)
                    _trackers[i].Mark(page * kTTDPieceSize);
            }
    }
    EveClearDirty(chip);
}

#else  // ENABLE_VDAC2

void TTDVdac2Memory::TTDRegions(std::vector<TTDDeviceRegion>&) { _regionCount = 0; }
void TTDVdac2Memory::TTDArmRegions(bool on) { _armed = on; }
void TTDVdac2Memory::TTDBeforeCapture() {}

#endif  // ENABLE_VDAC2

bool TTDVdac2Memory::TTDStateWithoutRegions(uint8_t& peripheralId, std::vector<uint8_t>& state) const
{
    if (_regionCount == 0)
        return false;   // no chip: the blob stays as it is
    peripheralId = static_cast<uint8_t>(PeripheralId::Vdac2Memory);
    state.resize(8);
    _card.TtdSaveMemoryHeader(state.data());
    return true;
}

bool TTDVdac2Memory::TTDLoadStateWithoutRegions(const uint8_t* state, size_t size)
{
    // The blob header only: the chip's memory is the engine's regions (EveMemoryRestored follows)
    return size == 8 && _regionCount != 0 && state != nullptr;
}

/// endregion </Time-travel engine regions>

size_t TTDVdac2::TTDStateSize() const { return _card.TtdStateSize(); }
void TTDVdac2::TTDSaveState(uint8_t* dst) const { _card.TtdSaveState(dst); }
void TTDVdac2::TTDLoadState(const uint8_t* src) { _card.TtdLoadState(src); }
uint64_t TTDVdac2::TTDHashState() const { return _card.TtdStateHash(); }

TTDDeviceDescriptor TTDVdac2::TTDDescribe() const
{
    TTDDeviceDescriptor d = TTDSerializable::TTDDescribe();
    d.timeFields = Vdac2Card::TtdTimeFields();   // the raster clocks at the blob's start
    return d;
}

}  // namespace ttd
