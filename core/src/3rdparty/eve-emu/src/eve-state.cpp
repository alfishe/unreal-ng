// eve-emu - stable snapshot and in-flight operation records (arch §7), regions API.
#include "eve-internal.h"

namespace EveLib
{

namespace
{

bool ValidState(const ControlState& state, const EveChip& chip)
{
    if (state.magic != kStateMagic)
    {
        SetLastError("EveLoadState: not an eve-emu state");
        return false;
    }
    if (state.version != kStateVersion || state.size != sizeof(ControlState))
    {
        SetLastError("EveLoadState: state version or size does not match this library");
        return false;
    }
    if (state.model != static_cast<uint32_t>(chip.table->model))
    {
        SetLastError("EveLoadState: state belongs to another chip model");
        return false;
    }
    return true;
}

} // namespace

void StateLoaded(EveChip& chip)
{
    DrawingInvalidate(chip);
    CoproStateLoaded(chip);
}

void MemoryRestored(EveChip& chip)
{
    DrawingInvalidate(chip);
}

} // namespace EveLib

using namespace EveLib;

extern "C" {

size_t EveStateSize(const EveChip* chip)
{
    (void)chip;
    return sizeof(ControlState);
}

void EveSaveState(const EveChip* chip, void* out)
{
    std::memcpy(out, &chip->state, sizeof(ControlState));
}

int EveLoadState(EveChip* chip, const void* in, size_t size)
{
    SetLastError("");
    if (in == nullptr || size != sizeof(ControlState))
    {
        SetLastError("EveLoadState: wrong state size");
        return 1;
    }
    ControlState state;
    std::memcpy(&state, in, sizeof(state));
    if (!ValidState(state, *chip))
        return 1;
    chip->state = state;
    StateLoaded(*chip);
    return 0;
}

size_t EveRegionCount(const EveChip* chip)
{
    (void)chip;
    return RegionCount;
}

void EveGetRegion(const EveChip* chip, size_t index, EveRegion* out)
{
    if (index >= RegionCount)
    {
        *out = EveRegion{};
        return;
    }
    const Region& region = chip->regions[index];
    out->name = region.name;
    out->base = region.base;
    out->size = region.size;
    out->dirty = region.dirty;
    out->pageCount = region.pageCount;
}

void EveClearDirty(EveChip* chip)
{
    for (Region& region : chip->regions)
        std::memset(region.dirty, 0, ((region.pageCount + kPagesPerWord - 1) / kPagesPerWord) * sizeof(uint64_t));
}

void EveMemoryRestored(EveChip* chip)
{
    MemoryRestored(*chip);
}

} // extern "C"
