/// The level map detector: the bytes a tile loop's outer loop walked one a pass (media.h: Blit::mapLoop), one cell
/// each. Width: 32 cells when the map (or a copy of it) went to the attribute file or an attribute back buffer, or
/// when its tiles were drawn into display pixels and its size is a multiple of 32 (a screen-wide map); otherwise
/// the walking loop's dominant trip count. Height: the size over the width. A map inside a buffer is also claimed
/// in each of the buffer's sources (the levels it was copied from).

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <tuple>

#include "debugger/analyzers/analysis/detectors/media.h"

namespace analysis
{

namespace
{

std::string Hex(uint16_t v)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", v);
    return text;
}

}  // namespace

void DetectLevelMaps(DetectorContext& context)
{
    const media::DisplayInfo display = media::FindDisplay(context);
    const std::vector<media::Blit> blits = media::FindBlits(context, display);
    std::set<std::pair<uint16_t, uint16_t>> claimed;
    const std::vector<media::BufferCopy> buffers = media::Buffers(context);

    for (const media::Blit& b : blits)
    {
        if (b.mapLoop == nullptr || b.mapRead == nullptr)
            continue;
        for (const auto& [lo, hi] : media::Runs(context, b.mapRead->ranges, 0))
        {
            const uint32_t size = static_cast<uint32_t>(hi) - lo + 1;
            if (size < 16 || (lo >> 14) != (hi >> 14) || claimed.count({lo, hi}) != 0)
                continue;
            claimed.insert({lo, hi});
            // Attributes: the map is an attribute buffer, or a copy took it there
            bool attributes = display.At(lo) == media::kAttributes;
            for (const media::DisplayCopy& c : display.copies)
                attributes = attributes || (c.type == media::kAttributes && c.src <= lo && c.src + c.length - 1 >= hi);
            uint32_t width = 0;
            if ((attributes || b.toPixels) && size % 32 == 0)
                width = 32;
            else
            {
                uint64_t most = 0;
                for (const auto& [trips, instances] : b.mapLoop->trips)
                    if (instances > most)
                    {
                        most = instances;
                        width = trips;
                    }
            }
            if (width == 0 || size % width != 0)
                continue;
            Region r;
            r.kind = RegionKind::LevelMap;
            r.detector = DetectorId::LevelMaps;
            r.status = RegionStatus::Observed;
            r.geometry.width = static_cast<uint16_t>(width);
            r.geometry.height = static_cast<uint16_t>(size / width);
            r.geometry.frames = 1;
            r.geometry.stride = static_cast<uint16_t>(width);
            r.geometry.layout = "cells";
            r.confidence = attributes ? 900 : 850;
            r.evidence.push_back(Evidence{"map walk", b.mapLoop->z80, b.mapLoop->passes,
                                          "one cell a pass, read at " + Hex(b.mapRead->z80)});
            r.evidence.push_back(Evidence{"tiles", b.loop->z80, b.instances,
                                          "each cell drawn by the tile loop at " + Hex(b.loop->z80)});
            if (attributes)
                r.evidence.push_back(Evidence{"attributes", lo, 0, "the map's bytes went to the attribute file"});
            Region live = r;
            live.users = context.UsersOf(lo, hi);
            context.Claim(std::move(live), lo, hi);
            for (const media::BufferCopy& buffer : buffers)
            {
                if (!(buffer.lo <= lo && buffer.hi >= hi))
                    continue;
                for (const uint16_t instance : buffer.instances)
                {
                    const uint32_t at = static_cast<uint32_t>(instance) + (lo - buffer.lo);
                    const uint32_t end = at + size - 1;
                    if (end > 0xFFFF || claimed.count({static_cast<uint16_t>(at), static_cast<uint16_t>(end)}) != 0)
                        continue;
                    claimed.insert({static_cast<uint16_t>(at), static_cast<uint16_t>(end)});
                    Region copy = r;
                    copy.evidence.push_back(Evidence{"copy", buffer.lo, 0,
                                                     "copied to the buffer at " + Hex(buffer.lo) + ", walked there"});
                    copy.users = context.UsersOf(static_cast<uint16_t>(at), static_cast<uint16_t>(end));
                    context.Claim(std::move(copy), static_cast<uint16_t>(at), static_cast<uint16_t>(end));
                }
            }
        }
    }
}

}  // namespace analysis
