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
    if (chip.vdac4)
        UpdateMemoryMap(chip, false);
    DrawingInvalidate(chip);
    AsyncTakeDrawingState(chip, true); // the loaded handles, context and metrics
    CoproStateLoaded(chip);
}

void MemoryRestored(EveChip& chip)
{
    AsyncDropPendingTag(chip); // the host wrote REG_TAG back too
    DrawingInvalidate(chip);
}

} // namespace EveLib

using namespace EveLib;

extern "C" {

size_t EveStateSize(const EveChip* chip)
{
    // VDAC4: the profile's block follows the control state
    return sizeof(ControlState) + (chip->vdac4 ? sizeof(Vdac4State) : 0);
}

void EveSaveState(const EveChip* chip, void* out)
{
    std::memcpy(out, &chip->state, sizeof(ControlState));
    if (chip->asyncDrawing)
    {
        // The drawing's part of the state is the renderer's: the same bytes as with the drawing on this thread
        const EveChip& drawn = DrawingChip(*chip);
        ControlState& saved = *static_cast<ControlState*>(out);
        std::memcpy(saved.handles, drawn.state.handles, sizeof(saved.handles));
        saved.context = drawn.state.context;
        saved.metrics = drawn.state.metrics;
    }
    if (chip->vdac4)
        std::memcpy(static_cast<uint8_t*>(out) + sizeof(ControlState), &chip->vdac4State, sizeof(Vdac4State));
}

int EveLoadState(EveChip* chip, const void* in, size_t size)
{
    SetLastError("");
    if (in == nullptr || size != EveStateSize(chip))
    {
        SetLastError("EveLoadState: wrong state size");
        return 1;
    }
    ControlState state;
    std::memcpy(&state, in, sizeof(state));
    if (!ValidState(state, *chip))
        return 1;
    if (chip->vdac4)
    {
        Vdac4State v;
        std::memcpy(&v, static_cast<const uint8_t*>(in) + sizeof(ControlState), sizeof(v));
        if (v.magic != kVdac4StateMagic || v.size != sizeof(Vdac4State))
        {
            SetLastError("EveLoadState: VDAC4 block version or size does not match this library");
            return 1;
        }
        chip->vdac4State = v;
    }
    chip->state = state;
    StateLoaded(*chip);
    return 0;
}

size_t EveRegionCount(const EveChip* chip)
{
    return chip->regionCount;
}

void EveGetRegion(const EveChip* chip, size_t index, EveRegion* out)
{
    if (index >= chip->regionCount)
    {
        *out = EveRegion{};
        return;
    }
    if (index == RegionReg)
        AsyncResolveTag(*chip); // the region's bytes hold REG_TAG
    const Region& region = chip->regions[index];
    out->name = region.name;
    out->base = region.base;
    out->size = region.size;
    out->dirty = region.dirty;
    out->pageCount = region.pageCount;
}

void EveClearDirty(EveChip* chip)
{
    for (uint32_t i = 0; i < chip->regionCount; ++i)
    {
        Region& region = chip->regions[i];
        std::memset(region.dirty, 0, ((region.pageCount + kPagesPerWord - 1) / kPagesPerWord) * sizeof(uint64_t));
    }
}

void EveMemoryRestored(EveChip* chip)
{
    MemoryRestored(*chip);
}

} // extern "C"
