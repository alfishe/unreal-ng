#include "stdafx.h"

#include "loaders/tape/writer_tzx.h"

#include "common/filehelper.h"
#include "tapeaudio/tapeaudioconfig.h"

#include <algorithm>

/// region <Documentation>

/// See writer_tzx.h. Layout reference (the same one LoaderTZX parses):
/// docs/file-formats/tape-images/tzx-tape.md — multi-byte fields LE.

/// endregion </Documentation>

namespace
{
    void Put8(std::vector<uint8_t>& out, uint8_t value)
    {
        out.push_back(value);
    }

    void Put16(std::vector<uint8_t>& out, uint16_t value)
    {
        out.push_back(static_cast<uint8_t>(value & 0xFF));
        out.push_back(static_cast<uint8_t>(value >> 8));
    }

    void Put24(std::vector<uint8_t>& out, uint32_t value)
    {
        Put16(out, static_cast<uint16_t>(value & 0xFFFF));
        out.push_back(static_cast<uint8_t>(value >> 16));
    }

    /// Signal half-periods are < 5 ms by the pause-split convention; entries
    /// at/above the threshold are gaps, emitted as $20 pauses.
    bool IsPauseEntry(uint32_t entry)
    {
        return entry >= TapeAudio::PAUSE_HOLD_THRESHOLD_MS * TapeAudio::TSTATES_PER_MS;
    }

    /// $12 requires a uniform run; anything else goes through the $13 path.
    bool IsUniformTone(const std::vector<uint32_t>& entries)
    {
        for (uint32_t entry : entries)
        {
            if (entry != entries[0])
            {
                return false;
            }
        }
        return true;
    }

    void EmitPulseSequenceChunk(std::vector<uint8_t>& out, const uint32_t* pulses, size_t count)
    {
        Put8(out, 0x13);
        Put8(out, static_cast<uint8_t>(count));
        for (size_t i = 0; i < count; i++)
        {
            Put16(out, static_cast<uint16_t>(pulses[i]));
        }
    }

    void EmitPause(std::vector<uint8_t>& out, uint32_t pauseMs)
    {
        Put8(out, 0x20);
        Put16(out, static_cast<uint16_t>(std::min<uint32_t>(pauseMs, 0xFFFF)));
    }

    void EmitPulseTrain(std::vector<uint8_t>& out, const std::vector<uint32_t>& entries)
    {
        // Signal runs → $13 chunks (≤ 255 u16 entries); gaps → $20 pauses.
        size_t chunkStart = SIZE_MAX;

        auto flushChunk = [&out, &entries, &chunkStart](size_t end)
        {
            if (chunkStart == SIZE_MAX)
            {
                return;
            }
            size_t at = chunkStart;
            while (at < end)
            {
                const size_t take = std::min<size_t>(end - at, 255);
                EmitPulseSequenceChunk(out, entries.data() + at, take);
                at += take;
            }
            chunkStart = SIZE_MAX;
        };

        for (size_t i = 0; i < entries.size(); i++)
        {
            if (IsPauseEntry(entries[i]))
            {
                flushChunk(i);
                EmitPause(out, entries[i] / TapeAudio::TSTATES_PER_MS);
            }
            else
            {
                if (chunkStart == SIZE_MAX)
                {
                    chunkStart = i;
                }
            }
        }
        flushChunk(entries.size());
    }
}

std::vector<uint8_t> TzxArchiveWriter::Write(const TapeImage& image)
{
    std::vector<uint8_t> out;
    out.reserve(image.blocks.size() * 32 + 16);

    // "ZXTape!" + soft-EOF + version 1.20 — the header LoaderTZX seeds back
    Put8(out, 'Z');
    Put8(out, 'X');
    Put8(out, 'T');
    Put8(out, 'a');
    Put8(out, 'p');
    Put8(out, 'e');
    Put8(out, '!');
    Put8(out, 0x1A);
    Put8(out, 0x01);
    Put8(out, 0x14);

    for (size_t i = 0; i < image.blocks.size(); i++)
    {
        const TapeBlock& block = image.blocks[i];
        const TapeBlockDescriptor* descriptor = i < image.descriptors.size() ? &image.descriptors[i] : nullptr;
        const uint16_t pauseMs = descriptor != nullptr ? descriptor->timing.pauseMs : 0;

        if (!block.data.empty())
        {
            if (!block.timing.has_value() && block.data.size() <= 0xFFFF)
            {
                // Representation 1 → $10 (pause is the catalog hint)
                Put8(out, 0x10);
                Put16(out, pauseMs);
                Put16(out, static_cast<uint16_t>(block.data.size()));
                out.insert(out.end(), block.data.begin(), block.data.end());
            }
            else
            {
                // Representation 2 → $11 (also the > 64 KiB ROM fallback with
                // the ROM timing set — the loader reads the same shape back)
                const bool romFallback = !block.timing.has_value();
                TapeTimingProfile romProfile;
                romProfile.pilotHalfPeriod = 2168;
                romProfile.sync1 = 667;
                romProfile.sync2 = 735;
                romProfile.zeroHalfPeriod = 855;
                romProfile.oneHalfPeriod = 1710;
                romProfile.pilotPulses = 3220;  // ROM data-block pilot; length unknown at this level
                const TapeTimingProfile& t = romFallback ? romProfile : *block.timing;
                const size_t emitted = std::min<size_t>(block.data.size(), 0xFFFFFF);

                Put8(out, 0x11);
                Put16(out, static_cast<uint16_t>(std::min<uint32_t>(t.pilotHalfPeriod, 0xFFFF)));
                Put16(out, static_cast<uint16_t>(std::min<uint32_t>(t.sync1, 0xFFFF)));
                Put16(out, static_cast<uint16_t>(std::min<uint32_t>(t.sync2, 0xFFFF)));
                Put16(out, static_cast<uint16_t>(std::min<uint32_t>(t.zeroHalfPeriod, 0xFFFF)));
                Put16(out, static_cast<uint16_t>(std::min<uint32_t>(t.oneHalfPeriod, 0xFFFF)));
                Put16(out, static_cast<uint16_t>(std::min<uint32_t>(t.pilotPulses, 0xFFFF)));
                Put8(out, t.bitsInLastByte == 0 ? 8 : t.bitsInLastByte);
                Put16(out, pauseMs);
                Put24(out, static_cast<uint32_t>(emitted));
                out.insert(out.end(), block.data.begin(), block.data.begin() + static_cast<ptrdiff_t>(emitted));
            }
        }
        else if (!block.edgePulseTimings.empty())
        {
            if (descriptor != nullptr && descriptor->kind == TapeBlockKindEnum::Tone &&
                IsUniformTone(block.edgePulseTimings) &&
                block.edgePulseTimings[0] <= 0xFFFF && block.edgePulseTimings.size() <= 0xFFFF)
            {
                // $12 Pure tone — the loader's own $12 shape
                Put8(out, 0x12);
                Put16(out, static_cast<uint16_t>(block.edgePulseTimings[0]));
                Put16(out, static_cast<uint16_t>(block.edgePulseTimings.size()));
            }
            else
            {
                EmitPulseTrain(out, block.edgePulseTimings);
            }
        }
        else if (descriptor != nullptr && descriptor->kind == TapeBlockKindEnum::Control)
        {
            // Stop-the-tape marker ($20 0) or an empty control leftover
            EmitPause(out, pauseMs);
        }
        // Anything else (empty, non-control) has no TZX representation and
        // no playback content — skipped, exactly as the loader would.
    }

    return out;
}

bool TzxArchiveWriter::Save(const TapeImage& image, const std::string& path, std::string& errorText)
{
    std::vector<uint8_t> bytes = Write(image);  // non-const: FileHelper takes uint8_t*
    if (!FileHelper::SaveBufferToFile(path, bytes.data(), bytes.size()))
    {
        errorText = "cannot write '" + path + "'";
        return false;
    }
    return true;
}
