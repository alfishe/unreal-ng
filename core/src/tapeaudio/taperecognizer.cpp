#include "stdafx.h"

#include "tapeaudio/taperecognizer.h"

#include "tapeaudio/tapeaudioconfig.h"

#include <algorithm>
#include <cmath>

/// region <Documentation>

/// See taperecognizer.h. Recognition constants and tolerances:
///   - ROM timing set: pilot 2168, sync 667/735, '0' 855, '1' 1710 T.
///   - ±25% shape tolerance: one sample at 44.1 kHz is ~79 T (~9% of the
///     shortest ROM half-period); 25% absorbs edge jitter plus mild tape
///     speed wobble without ever confusing 855 from 1710 (2x apart).
///   - Tight classification tolerance (max(6.25%, 85 T)) for the $10 vs $11
///     decision only — it compares medians, which are jitter-stable, so the
///     loose shape tolerance would absorb near-ROM turbo as "standard".
///   - Pilot runs of >= 8 similar pulses frame a block (ROM uses 8064/3223;
///     custom loaders use far fewer, 8 stays above random data similarity).
/// All measurements are medians — immune to single-pulse outliers.

/// endregion </Documentation>

namespace
{
    // ROM timing constants (tape.cpp / TZX spec $10 semantics)
    constexpr uint32_t ROM_PILOT = 2168;
    constexpr uint32_t ROM_SYNC1 = 667;
    constexpr uint32_t ROM_SYNC2 = 735;
    constexpr uint32_t ROM_ZERO = 855;
    constexpr uint32_t ROM_ONE = 1710;

    constexpr size_t MIN_PILOT_RUN = 8;
    constexpr uint32_t MAX_PULSE_SEQUENCE = 255;   // TZX $13 count is u8

    bool IsGap(uint32_t entry)
    {
        return entry >= TapeAudio::PAUSE_HOLD_THRESHOLD_MS * TapeAudio::TSTATES_PER_MS;
    }

    /// Within ±25%: the recognition tolerance everywhere timings are compared.
    bool Similar(uint32_t a, uint32_t b)
    {
        return a <= b + b / 4 && b <= a + a / 4;
    }

    /// Classification tolerance: max(6.25%, 85 T). Distinct from the ±25%
    /// shape tolerance on purpose — romTimings decides $10 vs $11 by
    /// comparing class MEDIANS, which are stable against pulse jitter; 25%
    /// here would absorb near-ROM turbo (a 2000 T pilot is 7.7% off ROM and
    /// must stay turbo). The 85 T floor covers one sample of edge
    /// quantization for the short sync pulses, whose class medians come
    /// from just two measured entries (667 T can extract as 635..714).
    bool NearRom(uint32_t measured, uint32_t romValue)
    {
        const uint32_t tolerance = std::max<uint32_t>(romValue / 16, 85);
        return measured + tolerance >= romValue && romValue + tolerance >= measured;
    }

    uint32_t Median(std::vector<uint32_t> values)
    {
        std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
        return values[values.size() / 2];
    }

    /// Lookahead: do >= MIN_PILOT_RUN similar non-gap entries start at `i`?
    bool PilotRunStartsAt(std::span<const uint32_t> track, size_t i, uint32_t* outPeriod = nullptr)
    {
        size_t run = 1;
        while (i + run < track.size() && !IsGap(track[i + run]) && Similar(track[i + run], track[i]))
        {
            run++;
        }

        if (run >= MIN_PILOT_RUN)
        {
            if (outPeriod != nullptr)
            {
                std::vector<uint32_t> window(track.begin() + i, track.begin() + i + run);
                *outPeriod = Median(window);
            }
            return true;
        }
        return false;
    }

    /// Data-region variant of the pilot lookahead: does a NEW block's pilot
    /// start at `i`? A candidate is a run of >= MIN_PILOT_RUN uniform halves
    /// that is distinctly away from BOTH established data levels (min and
    /// max of the halves accepted so far) AND followed by a sync-like pair —
    /// two pulses clearly shorter than the run, the first also away from
    /// both levels. Runs of equal data bits sit AT an established level, and
    /// a run that introduces the second level is followed by halves AT the
    /// first level — neither matches; a genuine next-block pilot is away
    /// from everything before it.
    bool PilotBoundaryAfterData(std::span<const uint32_t> track, size_t i,
                                uint32_t establishedMin, uint32_t establishedMax)
    {
        size_t run = 1;
        while (i + run < track.size() && !IsGap(track[i + run]) && Similar(track[i + run], track[i]))
        {
            run++;
        }
        if (run < MIN_PILOT_RUN)
        {
            return false;
        }

        const uint32_t period = track[i];
        if (Similar(period, establishedMin) || Similar(period, establishedMax))
        {
            return false;
        }

        const size_t s = i + run;
        if (s + 1 >= track.size() || IsGap(track[s]) || IsGap(track[s + 1]))
        {
            return false;  // run into the track end or silence — not a pilot+sync
        }
        const uint32_t shorterThanPilot = period - period / 4;
        if (track[s] >= shorterThanPilot || track[s + 1] >= shorterThanPilot)
        {
            return false;
        }
        return !Similar(track[s], establishedMin) && !Similar(track[s], establishedMax);
    }

    /// Length of the pilot run starting at `i` (entries within tolerance of
    /// the run's first entry — a pilot is uniform by construction).
    size_t PilotRunLength(std::span<const uint32_t> track, size_t i)
    {
        size_t run = 1;
        while (i + run < track.size() && !IsGap(track[i + run]) && Similar(track[i + run], track[i]))
        {
            run++;
        }
        return run;
    }

    struct DecodedData
    {
        std::vector<uint8_t> bytes;
        size_t consumedTo = 0;       // one past the last consumed entry
        uint32_t zeroHalf = 0;       // measured medians
        uint32_t oneHalf = 0;
        size_t bitCount = 0;
        bool endedOnGap = false;
    };

    /// Stage 2: decode the bit-pair region starting at `start`. Consumes
    /// pairs of similar half-periods; classification threshold bootstraps on
    /// the min/max of the collected halves and refines once on medians.
    DecodedData DecodeData(std::span<const uint32_t> track, size_t start)
    {
        DecodedData result;

        // Collect the pair halves first — the whole region is classified
        // together, so a slow bit at the start cannot poison the threshold.
        // The established levels (min/max of accepted halves) let the pilot
        // boundary check tell a next-block pilot from equal-bit runs.
        std::vector<uint32_t> halves;
        uint32_t minHalf = 0xFFFFFFFFu;
        uint32_t maxHalf = 0;
        size_t acceptedPairs = 0;
        uint32_t loneHalf = 0;  // final bit's first half, swallowed by the pause
        size_t i = start;
        bool endedOnGap = false;

        while (i + 1 < track.size())
        {
            const uint32_t a = track[i];
            const uint32_t b = track[i + 1];

            if (IsGap(a))
            {
                endedOnGap = true;
                break;
            }
            if (IsGap(b))
            {
                // The pause hold swallows the final bit's SECOND half — the
                // level is held through the silence, so no edge ever ends
                // it. Both halves of a ZX bit are equal: keep the first half
                // as the bit's timing sample and end the region at the gap,
                // which the caller consumes as this block's pause.
                loneHalf = a;
                endedOnGap = true;
                i += 1;
                break;
            }
            if (!Similar(a, b))
            {
                break;  // not an equal pair — data region over
            }
            if (acceptedPairs >= 1 && PilotBoundaryAfterData(track, i, minHalf, maxHalf))
            {
                break;  // next block's pilot — stop before consuming it
            }

            const uint32_t half = std::min(a, b);
            halves.push_back(half);
            minHalf = std::min(minHalf, half);
            maxHalf = std::max(maxHalf, half);
            acceptedPairs++;
            i += 2;
        }

        if (loneHalf != 0)
        {
            halves.push_back(loneHalf);  // one entry == one bit in this scheme
        }

        // Assemble and classify
        if (!halves.empty())
        {
            uint32_t lo = *std::min_element(halves.begin(), halves.end());
            uint32_t hi = *std::max_element(halves.begin(), halves.end());

            if (hi > lo && hi > lo + lo / 2)  // two distinct levels required
            {
                // Bootstrap threshold, then refine on class medians
                double threshold = std::sqrt(static_cast<double>(lo) * hi);
                for (int pass = 0; pass < 2; pass++)
                {
                    std::vector<uint32_t> zeros;
                    std::vector<uint32_t> ones;
                    for (uint32_t half : halves)
                    {
                        (half < threshold ? zeros : ones).push_back(half);
                    }
                    if (zeros.empty() || ones.empty())
                    {
                        break;
                    }
                    uint32_t zeroMedian = Median(zeros);
                    uint32_t oneMedian = Median(ones);
                    threshold = std::sqrt(static_cast<double>(zeroMedian) * oneMedian);
                    result.zeroHalf = zeroMedian;
                    result.oneHalf = oneMedian;
                }

                if (result.zeroHalf != 0 && result.oneHalf != 0)
                {
                    // Bits: MSB first, two equal halves per bit
                    uint8_t current = 0;
                    int bitPos = 7;
                    for (uint32_t half : halves)
                    {
                        const int bit = half < threshold ? 0 : 1;
                        if (bit)
                        {
                            current = static_cast<uint8_t>(current | (1u << bitPos));
                        }
                        if (bitPos == 0)
                        {
                            result.bytes.push_back(current);
                            current = 0;
                            bitPos = 7;
                        }
                        else
                        {
                            bitPos--;
                        }
                        result.bitCount++;
                    }
                    if (bitPos != 7)
                    {
                        result.bytes.push_back(current);  // trailing partial byte, zero-padded
                    }
                    result.consumedTo = i;
                    result.endedOnGap = endedOnGap;
                }
            }
        }

        return result;
    }

    uint32_t EntryToMs(uint32_t entry)
    {
        return entry / TapeAudio::TSTATES_PER_MS;
    }

    /// Consume the gap entry at `i` into a pause hint (u16 clamp of $10/$11).
    uint32_t ConsumePauseAt(std::span<const uint32_t> track, size_t i, bool* consumed)
    {
        if (i < track.size() && IsGap(track[i]))
        {
            *consumed = true;
            uint64_t ms = (uint64_t(track[i]) + TapeAudio::TSTATES_PER_MS / 2) / TapeAudio::TSTATES_PER_MS;
            return static_cast<uint32_t>(std::min<uint64_t>(ms, 0xFFFF));
        }
        *consumed = false;
        return 0;
    }
}

std::vector<RecognizedBlock> TapeRecognizer::Recognize(std::span<const uint32_t> track)
{
    std::vector<RecognizedBlock> blocks;
    size_t i = 0;

    while (i < track.size())
    {
        if (IsGap(track[i]))
        {
            RecognizedBlock block;
            block.kind = RecognizedBlockKind::PauseGap;
            block.pauseMs = std::min(EntryToMs(track[i]), 0xFFFFu);
            blocks.push_back(block);
            i++;
            continue;
        }

        // --- Stage 1: pilot?
        uint32_t pilotPeriod = 0;
        const size_t pilotRun = PilotRunStartsAt(track, i, &pilotPeriod) ? PilotRunLength(track, i) : 0;

        if (pilotRun >= MIN_PILOT_RUN)
        {
            const size_t afterPilot = i + pilotRun;
            const bool hasSync = afterPilot + 1 < track.size() &&
                                 !IsGap(track[afterPilot]) && !IsGap(track[afterPilot + 1]) &&
                                 track[afterPilot] < pilotPeriod - pilotPeriod / 4 &&
                                 track[afterPilot + 1] < pilotPeriod - pilotPeriod / 4;

            if (hasSync)
            {
                const uint32_t sync1 = track[afterPilot];
                const uint32_t sync2 = track[afterPilot + 1];
                DecodedData data = DecodeData(track, afterPilot + 2);

                if (data.bytes.size() >= 2)
                {
                    const bool romTimings = NearRom(pilotPeriod, ROM_PILOT) && NearRom(sync1, ROM_SYNC1) &&
                                             NearRom(sync2, ROM_SYNC2) && NearRom(data.zeroHalf, ROM_ZERO) &&
                                             NearRom(data.oneHalf, ROM_ONE);
                    const bool byteAligned = data.bitCount % 8 == 0;

                    RecognizedBlock block;
                    block.data = std::move(data.bytes);

                    uint8_t parity = 0;
                    for (uint8_t byte : block.data)
                    {
                        parity ^= byte;
                    }
                    block.checksumValid = parity == 0;
                    if (!block.checksumValid)
                    {
                        block.notes.push_back("checksum mismatch — imported as heard");
                    }

                    bool consumedGap = false;
                    block.pauseMs = ConsumePauseAt(track, data.consumedTo, &consumedGap);

                    if (romTimings && byteAligned)
                    {
                        block.kind = RecognizedBlockKind::StandardBlock;
                        block.timing.profile = TapeSpeedProfileEnum::StandardRom;
                        block.timing.pauseMs = static_cast<uint16_t>(block.pauseMs);
                    }
                    else
                    {
                        block.kind = RecognizedBlockKind::TurboBlock;
                        block.timing.profile = TapeSpeedProfileEnum::Custom;
                        block.timing.pilotHalfPeriod = romTimings ? ROM_PILOT : pilotPeriod;
                        block.timing.sync1 = sync1;
                        block.timing.sync2 = sync2;
                        block.timing.zeroHalfPeriod = data.zeroHalf;
                        block.timing.oneHalfPeriod = data.oneHalf;
                        block.timing.pilotPulses = static_cast<uint32_t>(pilotRun);
                        block.timing.bitsInLastByte = data.bitCount % 8 == 0 ? 8 : static_cast<uint8_t>(data.bitCount % 8);
                        block.timing.pauseMs = static_cast<uint16_t>(block.pauseMs);
                        if (!byteAligned)
                        {
                            block.notes.push_back("bit count not byte-aligned — encoded as turbo with used-bits count");
                        }
                    }

                    blocks.push_back(std::move(block));
                    i = consumedGap ? data.consumedTo + 1 : data.consumedTo;
                    continue;
                }

                // Pilot but no decodable data: the pilot itself is a tone.
                RecognizedBlock block;
                block.kind = RecognizedBlockKind::PureTone;
                block.tonePeriod = pilotPeriod;
                block.tonePulses = static_cast<uint32_t>(pilotRun);
                blocks.push_back(std::move(block));
                i = afterPilot;
                continue;
            }

            // Pilot-shaped run with no sync: tone.
            RecognizedBlock block;
            block.kind = RecognizedBlockKind::PureTone;
            block.tonePeriod = pilotPeriod;
            block.tonePulses = static_cast<uint32_t>(pilotRun);
            blocks.push_back(std::move(block));
            i = afterPilot;
            continue;
        }

        // --- No pilot: try pilotless data (some loaders omit the pilot)
        DecodedData data = DecodeData(track, i);
        if (data.bytes.size() >= 2)
        {
            RecognizedBlock block;
            block.kind = RecognizedBlockKind::TurboBlock;
            block.data = std::move(data.bytes);

            uint8_t parity = 0;
            for (uint8_t byte : block.data)
            {
                parity ^= byte;
            }
            block.checksumValid = parity == 0;
            block.notes.push_back("no pilot detected — encoded as turbo with zero pilot pulses");

            bool consumedGap = false;
            block.pauseMs = ConsumePauseAt(track, data.consumedTo, &consumedGap);

            block.timing.profile = TapeSpeedProfileEnum::Custom;
            block.timing.zeroHalfPeriod = data.zeroHalf;
            block.timing.oneHalfPeriod = data.oneHalf;
            block.timing.pilotPulses = 0;
            block.timing.sync1 = 0;
            block.timing.sync2 = 0;
            block.timing.bitsInLastByte = data.bitCount % 8 == 0 ? 8 : static_cast<uint8_t>(data.bitCount % 8);
            block.timing.pauseMs = static_cast<uint16_t>(block.pauseMs);

            blocks.push_back(std::move(block));
            i = consumedGap ? data.consumedTo + 1 : data.consumedTo;
            continue;
        }

        // --- Faithful fallback: pulse sequence chunks until a gap or pilot
        size_t j = i;
        std::vector<uint32_t> chunk;
        while (j < track.size() && !IsGap(track[j]) && !PilotRunStartsAt(track, j) && chunk.size() < MAX_PULSE_SEQUENCE)
        {
            chunk.push_back(track[j]);
            j++;
        }
        if (chunk.empty())
        {
            // Degenerate: a lone entry before a pilot run boundary — take it
            // so the walk always advances.
            chunk.push_back(track[j]);
            j++;
        }

        RecognizedBlock block;
        block.kind = RecognizedBlockKind::PulseSequence;
        block.pulses = std::move(chunk);
        blocks.push_back(std::move(block));
        i = j;
    }

    return blocks;
}
