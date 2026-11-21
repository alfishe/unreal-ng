#include "gpupasstimes.h"

#include <QDebug>
#include <algorithm>

#include "gpu/gpudevice.h"

namespace
{

const char* PassName(GpuPassTimes::Pass pass)
{
    switch (pass)
    {
        case GpuPassTimes::Smooth:
            return "smooth motion";
        case GpuPassTimes::SmoothCpu:
            return "smooth motion (CPU)";
        default:
            return "picture";
    }
}
}  // namespace

bool GpuPassTimes::Init(gpu::Device* device)
{
    if (_ready)
        return true;
    // Per pass, slot and segment a begin and an end query
    constexpr int kQueries = static_cast<int>(PassCount * kSlots * kSegments * 2);
    if (!device || !device->CreateTimestamps(kQueries))
        return false;
    _device = device;
    std::lock_guard<std::mutex> lock(_mutex);
    int next = 0;
    for (PassState& state : _passes)
    {
        for (Slot& slot : state.queries)
        {
            for (size_t s = 0; s < kSegments; ++s)
            {
                slot.begin[s] = next++;
                slot.end[s] = next++;
            }
            slot.segments = 0;
            slot.pending = false;
        }
        state.next = 0;
        state.current = nullptr;
        state.open = state.skipped = state.inSegment = false;
        state.recent.assign(kWindow, 0.0);
        state.recentNext = 0;
        state.longest = 0.0;
        state.measured = 0;
    }
    _ready = true;
    return true;
}

void GpuPassTimes::Release(bool contextLost)
{
    if (!_ready)
        return;
    for (size_t p = 0; p < PassCount; ++p)
    {
        PassState& state = _passes[p];
        size_t pending = 0;
        for (Slot& slot : state.queries)
        {
            pending += slot.pending ? 1 : 0;
            slot = {};
        }
        state.current = nullptr;
        state.open = state.skipped = state.inSegment = false;
        // Lost with the context (a GPU reset): what the pass took until then, and how many were never finished
        if (contextLost)
            qWarning().nospace() << "GpuPassTimes: " << PassName(static_cast<Pass>(p)) << " pass at the loss: "
                                 << pending << " unfinished, " << state.measured << " measured, the longest "
                                 << state.longest << " ms";
    }
    _device->DestroyTimestamps(contextLost);
    _device = nullptr;
    _ready = false;
}

bool GpuPassTimes::Take(PassState& state)
{
    Slot& slot = state.queries[state.next];
    // Every slot still waits for the GPU: this one goes unmeasured
    if (slot.pending)
        return false;
    slot.segments = 0;
    state.current = &slot;
    return true;
}

void GpuPassTimes::Finish(PassState& state)
{
    if (!state.current)
        return;
    if (state.current->segments > 0)
    {
        state.current->pending = true;
        state.next = (state.next + 1) % kSlots;
    }
    state.current = nullptr;
}

void GpuPassTimes::Open(Pass pass)
{
    if (!_ready || pass == SmoothCpu)
        return;
    PassState& state = _passes[pass];
    if (state.open)
        Close(pass);
    state.open = true;
    state.skipped = !Take(state);
}

void GpuPassTimes::Close(Pass pass)
{
    if (!_ready || pass == SmoothCpu)
        return;
    PassState& state = _passes[pass];
    if (state.inSegment)
        End(pass);
    state.open = false;
    state.skipped = false;
    Finish(state);
}

void GpuPassTimes::Begin(Pass pass)
{
    if (!_ready || pass == SmoothCpu)
        return;
    PassState& state = _passes[pass];
    if (state.inSegment)
        return;
    if (!state.open && !Take(state))
        return;
    Slot* slot = state.current;
    if (!slot || slot->segments >= kSegments)
        return;
    _device->WriteTimestamp(slot->begin[slot->segments]);
    state.inSegment = true;
}

void GpuPassTimes::End(Pass pass)
{
    if (!_ready || pass == SmoothCpu)
        return;
    PassState& state = _passes[pass];
    if (!state.inSegment || !state.current)
        return;
    Slot& slot = *state.current;
    _device->WriteTimestamp(slot.end[slot.segments]);
    slot.segments++;
    state.inSegment = false;
    if (!state.open)
        Finish(state);
}

void GpuPassTimes::Add(Pass pass, PassState& state, double ms)
{
    if (ms >= kSlowMs)
        qWarning().nospace() << "GpuPassTimes: the " << PassName(pass) << " pass took " << ms << " ms"
                             << (pass == SmoothCpu ? "" : " on the GPU");
    std::lock_guard<std::mutex> lock(_mutex);
    if (state.recent.size() != kWindow)
        state.recent.assign(kWindow, 0.0);
    state.recent[state.recentNext] = ms;
    state.recentNext = (state.recentNext + 1) % kWindow;
    state.longest = std::max(state.longest, ms);
    state.measured++;
}

void GpuPassTimes::Record(Pass pass, double ms)
{
    Add(pass, _passes[pass], ms);
}

void GpuPassTimes::Collect()
{
    if (!_ready)
        return;
    for (size_t p = 0; p < PassCount; ++p)
    {
        PassState& state = _passes[p];
        // Oldest first: the GPU finishes them in order
        for (size_t i = 0; i < kSlots; ++i)
        {
            Slot& slot = state.queries[(state.next + i) % kSlots];
            if (!slot.pending)
                continue;
            uint64_t last = 0;
            if (!_device->ReadTimestamp(slot.end[slot.segments - 1], last))
                break;
            double ms = 0.0;
            for (size_t s = 0; s < slot.segments; ++s)
            {
                uint64_t begin = 0;
                uint64_t end = 0;
                if (_device->ReadTimestamp(slot.begin[s], begin) && _device->ReadTimestamp(slot.end[s], end))
                    ms += end > begin ? static_cast<double>(end - begin) / 1e6 : 0.0;
            }
            slot.pending = false;
            Add(static_cast<Pass>(p), state, ms);
        }
    }
}

GpuPassTimes::Times GpuPassTimes::Get(Pass pass) const
{
    Times out;
    std::vector<double> recent;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const PassState& state = _passes[pass];
        out.longestMs = state.longest;
        out.measured = state.measured;
        const size_t count = static_cast<size_t>(std::min<uint64_t>(state.measured, kWindow));
        recent.assign(state.recent.begin(), state.recent.begin() + static_cast<std::ptrdiff_t>(count));
    }
    if (recent.empty())
        return out;
    std::sort(recent.begin(), recent.end());
    out.p50Ms = recent[recent.size() / 2];
    out.maxMs = recent.back();
    return out;
}
