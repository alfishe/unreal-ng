#include "stdafx.h"

#include "emulator/io/tape/tapepulsegen.h"
#include "emulator/io/tape/tape.h"

/// region <Documentation>

/// See tapepulsegen.h — the engine (tape.cpp generateBitstream path) and the
/// offline tape-audio bridge share every pulse-generating line through this
/// translation unit.

/// endregion </Documentation>

uint64_t TapePulseGen::GenerateHalfPeriods(std::span<const uint8_t> data,
                                           uint32_t pilotHalfPeriod, uint32_t sync1, uint32_t sync2,
                                           uint32_t zeroHalfPeriod, uint32_t oneHalfPeriod,
                                           uint64_t pilotPulses, uint8_t bitsInLastByte,
                                           std::vector<uint32_t>& outHalfPeriods)
{
    uint64_t result = 0;
    const size_t len = data.size();

    if (bitsInLastByte == 0 || bitsInLastByte > 8)
    {
        bitsInLastByte = 8;  // defensive: TZX $11/$14 field is 1-8
    }

    // Calculate collection size to fit all edge time intervals
    size_t resultSize = 0;
    resultSize += static_cast<size_t>(pilotPulses);   // Pilot length is specified in pulses (half-periods), one edge each
    resultSize += 2;                                  // Two sync pulses at the end of pilot
    resultSize += (len == 0) ? 0 : ((len - 1) * 8 * 2 + bitsInLastByte * 2);  // Last byte may carry fewer than 8 bits (TZX $11/$14)
    outHalfPeriods.reserve(resultSize);

    /// region <Pilot tone + sync>

    if (pilotPulses > 0)
    {
        // Pilot length is specified in pulses (half-periods), matching the TAP
        // convention (header: 8063-8064 pulses, data: ~3220 pulses). Emitting
        // 2x here would double the real pilot duration (~10s instead of ~5s).
        for (uint64_t i = 0; i < pilotPulses; i++)
        {
            outHalfPeriods.push_back(pilotHalfPeriod);
            result += pilotHalfPeriod;
        }

        // Sync pulses at the end of pilot
        outHalfPeriods.push_back(sync1);
        outHalfPeriods.push_back(sync2);
        result += sync1;
        result += sync2;
    }

    /// endregion </Pilot tone + sync>

    /// region <Data bytes>

    for (size_t i = 0; i < len; i++)
    {
        // TZX $11/$14: only the first `bitsInLastByte` bits of the final byte
        // are part of the signal (MSB first)
        bool lastByte = (i == len - 1);
        uint8_t bitsInThisByte = lastByte ? bitsInLastByte : 8;

        for (uint8_t bitIndex = 0; bitIndex < bitsInThisByte; bitIndex++)
        {
            uint8_t bitMask = static_cast<uint8_t>(0x80 >> bitIndex);
            bool bit = (data[i] & bitMask) != 0;
            uint32_t bitEncoded = bit ? oneHalfPeriod : zeroHalfPeriod;

            // Each bit is encoded by two edges
            outHalfPeriods.push_back(bitEncoded);
            outHalfPeriods.push_back(bitEncoded);
            result += bitEncoded;
            result += bitEncoded;
        }
    }

    /// endregion </Data bytes>

    return result;
}

TapePulseGen::MaterializedPulses TapePulseGen::MaterializePulses(const TapeBlock& block, const TapeBlockDescriptor* descriptor)
{
    MaterializedPulses result;

    // Representation 3 (design §5.7) and empty control markers: no byte
    // payload means no byte encoding — the loader-supplied train IS the
    // content. A trailing pause hold-edge (loader convention: 1 ms == 3500
    // T-states) is split into pauseAfterMs so offline consumers see the gap
    // structurally instead of one giant edge.
    if (block.data.empty())
    {
        result.halfPeriods = block.edgePulseTimings;

        // A trailing run of pause hold-edges (loader convention: 1 ms == 3500
        // T-states) is split into pauseAfterMs so offline consumers see the gap
        // structurally instead of giant edges. The TZX loader appends one per
        // $20 pause merged into a representation-3 block, so this is a loop —
        // and no real signal half-period reaches 5 ms, which makes the
        // heuristic safe even without a descriptor.
        uint32_t pauseMs = 0;
        while (!result.halfPeriods.empty())
        {
            uint32_t last = result.halfPeriods.back();
            uint32_t lastMs = last / 3500;

            bool matchesDescriptorPause = descriptor != nullptr && descriptor->timing.pauseMs > 0 &&
                                          last == static_cast<uint64_t>(descriptor->timing.pauseMs) * 3500;
            bool looksLikeHoldEdge = lastMs >= 5;

            if (matchesDescriptorPause || looksLikeHoldEdge)
            {
                result.halfPeriods.pop_back();
                pauseMs += lastMs;
            }
            else
            {
                break;
            }
        }
        result.pauseAfterMs = pauseMs;

        for (uint32_t pulse : result.halfPeriods)
        {
            result.totalTStates += pulse;
        }
        result.totalTStates += static_cast<uint64_t>(result.pauseAfterMs) * 3500;
        result.playable = !result.halfPeriods.empty() || result.pauseAfterMs > 0;

        return result;
    }

    uint64_t signalTotal = 0;

    if (block.timing.has_value())
    {
        // Representation 2: byte payload with a Custom profile — TZX $11
        // turbo / $14 pure data. The profile mirrors the generator parameters
        // 1:1, so generation is a straight pass-through.
        const TapeTimingProfile& profile = *block.timing;
        signalTotal = GenerateHalfPeriods(block.data,
                                          profile.pilotHalfPeriod, profile.sync1, profile.sync2,
                                          profile.zeroHalfPeriod, profile.oneHalfPeriod,
                                          profile.pilotPulses, profile.bitsInLastByte,
                                          result.halfPeriods);
        result.pauseAfterMs = profile.pauseMs;
    }
    else
    {
        // Representation 1: ROM-standard encoding — TAP and TZX $10.
        bool isHeader = block.type == TAP_BLOCK_FLAG_HEADER;
        signalTotal = GenerateHalfPeriods(block.data,
                                          PILOT_TONE_HALF_PERIOD, PILOT_SYNCHRO_1, PILOT_SYNCHRO_2,
                                          ZERO_ENCODE_HALF_PERIOD, ONE_ENCODE_HALF_PERIOD,
                                          isHeader ? PILOT_DURATION_HEADER : PILOT_DURATION_DATA, 8,
                                          result.halfPeriods);
        result.romStandardTiming = true;

        // TZX $10 carries its own pause; the descriptor holds it as a hint
        // the live engine does not consume (design §5.4) — offline render
        // honors it, everything else keeps the TAP default.
        if (descriptor != nullptr && descriptor->timing.pauseMs > 0)
        {
            result.pauseAfterMs = descriptor->timing.pauseMs;
        }
        else
        {
            result.pauseAfterMs = 1000;
        }
    }

    result.totalTStates = signalTotal + static_cast<uint64_t>(result.pauseAfterMs) * 3500;
    result.playable = signalTotal > 0;

    return result;
}
