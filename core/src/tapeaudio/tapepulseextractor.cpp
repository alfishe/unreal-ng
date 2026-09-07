#include "stdafx.h"

#include "tapeaudio/tapepulseextractor.h"

#include <algorithm>
#include <cmath>

/// region <Documentation>

/// See tapepulseextractor.h. Two passes: (1) measure the signal envelope to
/// place the Schmitt band, (2) walk the samples latching edges, converting
/// sample deltas to T-states with the fixed rounding the recognizer's
/// tolerances are calibrated against ((delta * 3.5 MHz + rate/2) / rate).

/// endregion </Documentation>

TapePulseTrack TapePulseExtractor::Extract(std::span<const float> samples, uint32_t sampleRate,
                                           const TapePulseExtractorOptions& options)
{
    TapePulseTrack track;
    track.sampleRate = sampleRate;

    if (samples.empty() || sampleRate == 0)
    {
        return track;
    }

    // --- Pass 1: envelope. Center and half-range place the Schmitt band
    // independently of DC offset and input gain.
    float lo = samples[0];
    float hi = samples[0];
    for (float s : samples)
    {
        if (s < lo)
        {
            lo = s;
        }
        if (s > hi)
        {
            hi = s;
        }
    }

    const double center = (static_cast<double>(hi) + lo) / 2.0;
    const double range = (static_cast<double>(hi) - lo) / 2.0;
    track.signalRange = range;
    if (range < 1e-6)
    {
        return track;  // flat line: no tape signal
    }

    const double highThreshold = center + options.hysteresis * range;
    const double lowThreshold = center - options.hysteresis * range;

    // --- Pass 2: Schmitt trigger. An edge is latched at the first sample
    // that crosses the OPPOSITE band; the delta to the previous latched edge
    // is the half-period. `pending` carries the edge sample index; runts are
    // accumulated into the next half-period instead of emitted.
    const uint64_t gapThresholdT = uint64_t(options.gapThresholdMs) * TapeAudio::TSTATES_PER_MS;

    int state = 0;      // -1 latched low, +1 latched high, 0 before the first edge
    uint64_t prevEdge = 0;
    bool haveEdge = false;
    uint64_t runtCarryT = 0;

    for (size_t i = 0; i < samples.size(); i++)
    {
        const double s = samples[i];
        bool edge = false;

        if (state <= 0 && s > highThreshold)
        {
            state = 1;
            edge = true;
        }
        else if (state >= 0 && s < lowThreshold)
        {
            state = -1;
            edge = true;
        }

        if (!edge)
        {
            continue;
        }
        if (!haveEdge)
        {
            // Leading edge of the first pulse — its duration is leading
            // silence, which carries no tape content.
            prevEdge = i;
            haveEdge = true;
            continue;
        }

        const uint64_t deltaSamples = i - prevEdge;
        prevEdge = i;

        // Round-to-nearest T-state conversion; 64-bit safe for any gap.
        const uint64_t tstates = (deltaSamples * TapeAudio::TSTATE_HZ + sampleRate / 2) / sampleRate + runtCarryT;
        runtCarryT = 0;

        if (tstates < options.minSignalT)
        {
            // Runt: noise spike — fold into the next half-period.
            runtCarryT = tstates;
            continue;
        }

        // Gap entries longer than u32 (~20.5 min of silence) are clamped by
        // chaining a second entry — degenerate input, but it must not wrap.
        uint64_t remaining = tstates;
        do
        {
            const uint32_t entry = remaining > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(remaining);
            remaining -= entry;
            track.entries.push_back(entry);

            if (entry >= gapThresholdT)
            {
                track.gapCount++;
            }
            else
            {
                track.signalEdges++;
            }
        } while (remaining > 0);
    }

    // A capture that ends while the signal is held is TRAILING SILENCE —
    // the hold has no closing edge, so the loop above never emits it. Tapes
    // end in a pause by construction (every data block carries one); emit
    // the hold so the recognizer can consume it as the final block's pause
    // instead of losing the last bit's swallowed half. Holds shorter than
    // the gap threshold are partial half-periods and carry no content.
    if (haveEdge)
    {
        const uint64_t holdSamples = samples.size() - 1 - prevEdge;
        const uint64_t holdT = (holdSamples * TapeAudio::TSTATE_HZ + sampleRate / 2) / sampleRate + runtCarryT;
        if (holdT >= gapThresholdT)
        {
            uint64_t remaining = holdT;
            do
            {
                const uint32_t entry = remaining > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(remaining);
                remaining -= entry;
                track.entries.push_back(entry);
                track.gapCount++;
            } while (remaining > 0);
        }
    }

    return track;
}
