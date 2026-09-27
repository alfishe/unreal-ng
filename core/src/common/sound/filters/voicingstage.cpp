#include "voicingstage.h"

#include <algorithm>
#include <cstring>

VoicingStage::VoicingStage(size_t maxFrameSamples) : _maxFrameSamples(maxFrameSamples)
{
    for (std::vector<int16_t>& frame : _history)
        frame.assign(_maxFrameSamples * 2, 0);
}

void VoicingStage::setup(double sampleRate)
{
    _sampleRate = sampleRate;
    _filter.setup(sampleRate);
    invalidateHistory();
}

void VoicingStage::setPresetImmediate(Preset preset)
{
    _requested.store(static_cast<uint8_t>(preset), std::memory_order_release);
    _filter = FilterVoicing(_sampleRate, preset);
}

void VoicingStage::request(Preset preset)
{
    _requested.store(static_cast<uint8_t>(preset), std::memory_order_release);
}

void VoicingStage::reset()
{
    const Preset target = requested();
    if (target != _filter.preset())
        _filter = FilterVoicing(_sampleRate, target);
    else
        _filter.reset();
    invalidateHistory();
}

void VoicingStage::invalidateHistory()
{
    _historyCount = 0;
}

void VoicingStage::process(int16_t* interleaved, size_t frames)
{
    frames = std::min(frames, _maxFrameSamples);

    const Preset target = requested();
    if (target != _filter.preset())
    {
        switchTo(target, interleaved, frames);
        return;
    }

    pushHistory(interleaved, frames);
    _filter.processInt16(interleaved, frames);
}

void VoicingStage::pushHistory(const int16_t* interleaved, size_t frames)
{
    const size_t slot = (_historyCount == 0) ? 0 : (_historyNewest + 1) % HISTORY_FRAMES;
    std::memcpy(_history[slot].data(), interleaved, frames * 2 * sizeof(int16_t));
    _historyLength[slot] = frames;
    _historyNewest = slot;
    _historyCount = std::min(_historyCount + 1, HISTORY_FRAMES);
}

void VoicingStage::switchTo(Preset target, int16_t* interleaved, size_t frames)
{
    // Warm the new filter up over the history (oldest frame first), output
    // discarded, so it enters the switch frame in steady state
    FilterVoicing next(_sampleRate, target);
    if (!next.isBypass())
    {
        for (size_t k = 0; k < _historyCount; k++)
        {
            const size_t slot = (_historyNewest + HISTORY_FRAMES + 1 + k - _historyCount) % HISTORY_FRAMES;
            const int16_t* frame = _history[slot].data();
            for (size_t n = 0; n < _historyLength[slot]; n++)
            {
                next.process(0, frame[n * 2]);
                next.process(1, frame[n * 2 + 1]);
            }
        }
    }

    pushHistory(interleaved, frames);

    // Switch frame: linear crossfade old -> new, both filters fed the same input
    for (size_t n = 0; n < frames; n++)
    {
        const double t = (static_cast<double>(n) + 0.5) / static_cast<double>(frames);
        for (size_t ch = 0; ch < 2; ch++)
        {
            const double x = interleaved[n * 2 + ch];
            const double oldY = _filter.process(ch, x);
            const double newY = next.process(ch, x);
            interleaved[n * 2 + ch] = FilterVoicing::toInt16((1.0 - t) * oldY + t * newY);
        }
    }

    _filter = next;
}
