#include "portreadwatch.h"

const char* PortReadClassName(PortReadClass cls)
{
    static const char* const names[kPortReadClasses] = {"kempston", "kempston2", "fuller", "kmouse",
                                                        "row.fe",   "row.fd",    "row.fb", "row.f7",
                                                        "row.ef",   "row.df",    "row.bf", "row.7f",
                                                        "rows.multi"};
    const size_t index = static_cast<size_t>(cls);
    return index < kPortReadClasses ? names[index] : "";
}

void PortReadWatch::EndFrame()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _ring[_frames % kPortReadWindow] = _frame;
    ++_frames;
    _frame.fill(0);
}

void PortReadWatch::Reset()
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (auto& frame : _ring)
        frame.fill(0);
    _frames = 0;
    _frame.fill(0);
}

PortReadEvidence PortReadWatch::Evidence() const
{
    PortReadEvidence evidence;
    std::lock_guard<std::mutex> lock(_mutex);
    evidence.framesObserved = _frames;
    evidence.window = _frames < kPortReadWindow ? _frames : kPortReadWindow;
    std::array<uint32_t, kPortReadClasses> reads{};
    for (uint32_t i = 0; i < evidence.window; ++i)
        for (size_t cls = 0; cls < kPortReadClasses; ++cls)
            if (const uint16_t count = _ring[i][cls]; count != 0)
            {
                ++evidence.framesWithReads[cls];
                reads[cls] += count;
            }
    if (evidence.window != 0)
        for (size_t cls = 0; cls < kPortReadClasses; ++cls)
            evidence.readsPerFrame[cls] = static_cast<float>(reads[cls]) / static_cast<float>(evidence.window);
    return evidence;
}
