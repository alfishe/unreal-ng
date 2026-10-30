/// The level map detector: the bytes a tile loop's outer loop walked one a pass (media.h: Blit::mapLoop), one cell
/// each. Width: 32 cells when the map (or a copy of it) went to the attribute file or an attribute back buffer, or
/// when its tiles were drawn into display pixels and its size is a multiple of 32 (a screen-wide map); otherwise
/// the walking loop's dominant trip count. Height: the size over the width. Maps side by side: when one walk (an
/// instance of the loop) reads more than one row, every walk began on the lattice of that span inside the run and
/// drew to the same place (its tile writes began at one address), each span is a map of its own (Hungry Horace's four
/// mazes, one 768-byte walk each); a walk of one row is a row, not a map, and walks drawn to different places are
/// parts of one map (Manic Miner draws a cavern's halves in two walks). A map inside a buffer is also claimed in each of the buffer's sources (the levels it was copied from).

#include <algorithm>
#include <cstdlib>
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

uint32_t DominantTrips(const Loop& loop)
{
    uint64_t most = 0;
    uint32_t trips = 0;
    for (const auto& [t, instances] : loop.trips)
        if (instances > most)
        {
            most = instances;
            trips = t;
        }
    return trips;
}

/// Every walk of the map loop drew over one place: the tile writes of the whole run cover less than two walks
/// worth of tiles (maps drawn in turn over one screen area; the parts of one map go to different places)
bool OneDestination(const media::Blit& b, uint32_t walk)
{
    std::vector<std::pair<PhysKey, PhysKey>> written;
    for (const Stride& s : b.loop->strides)
        if (s.write && std::binary_search(b.writers.begin(), b.writers.end(), s.z80))
            for (const Interval& i : s.ranges)
                written.emplace_back(i.lo, i.hi);
    if (written.empty())
        return false;
    std::sort(written.begin(), written.end());
    uint64_t bytes = 0;
    PhysKey lo = written.front().first;
    PhysKey hi = written.front().second;
    for (const auto& [l, h] : written)
    {
        if (l > hi + 1)
        {
            bytes += hi - lo + 1;
            lo = l;
        }
        hi = std::max(hi, h);
    }
    bytes += hi - lo + 1;
    return bytes < 2ull * walk * b.frameBytes;
}

/// The bytes one walk of the map loop read, when the run [lo, lo + size) holds several such walks side by side, each
/// drawn to the same place; else @p size (one map)
uint32_t MapSpan(const media::Blit& b, uint16_t lo, uint32_t size, uint32_t width)
{
    const Stride& read = *b.mapRead;
    if (!read.constant || std::abs(read.stride) != 1)
        return size;
    const uint32_t walk = DominantTrips(*b.mapLoop);
    if (!OneDestination(b, walk) || walk <= width || walk % width != 0 || walk >= size || size % walk != 0)
        return size;
    const uint32_t hi = lo + size - 1;
    for (const uint16_t start : read.starts)
    {
        // A backward walk begins at the last byte of its map
        const uint32_t first = read.stride > 0 ? start : static_cast<uint32_t>(start) + 1 - walk;
        if (start >= lo && start <= hi && (first - lo) % walk != 0)
            return size;
    }
    return walk;
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
        for (const auto& [runLo, runHi] : media::Runs(context, b.mapRead->ranges, 0))
        {
            const uint32_t runSize = static_cast<uint32_t>(runHi) - runLo + 1;
            if (runSize < 16 || (runLo >> 14) != (runHi >> 14) || claimed.count({runLo, runHi}) != 0)
                continue;
            claimed.insert({runLo, runHi});
            // Attributes: the map is an attribute buffer, or a copy took it there
            bool attributes = display.At(runLo) == media::kAttributes;
            for (const media::DisplayCopy& c : display.copies)
                attributes =
                    attributes || (c.type == media::kAttributes && c.src <= runLo && c.src + c.length - 1 >= runHi);
            const uint32_t width = (attributes || b.toPixels) && runSize % 32 == 0 ? 32 : DominantTrips(*b.mapLoop);
            if (width == 0 || runSize % width != 0)
                continue;
            const uint32_t size = MapSpan(b, runLo, runSize, width);
            for (uint32_t at = runLo; at + size - 1 <= runHi; at += size)
            {
                const auto lo = static_cast<uint16_t>(at);
                const auto hi = static_cast<uint16_t>(at + size - 1);
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
                if (size != runSize)
                    r.evidence.push_back(Evidence{"walks", lo, runSize / size,
                                                  "one walk of the map loop reads " + std::to_string(size) +
                                                      " bytes; the walks began on that lattice"});
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
                        const uint32_t from = static_cast<uint32_t>(instance) + (lo - buffer.lo);
                        const uint32_t end = from + size - 1;
                        if (end > 0xFFFF ||
                            claimed.count({static_cast<uint16_t>(from), static_cast<uint16_t>(end)}) != 0)
                            continue;
                        claimed.insert({static_cast<uint16_t>(from), static_cast<uint16_t>(end)});
                        Region copy = r;
                        copy.evidence.push_back(Evidence{
                            "copy", buffer.lo, 0, "copied to the buffer at " + Hex(buffer.lo) + ", walked there"});
                        copy.users = context.UsersOf(static_cast<uint16_t>(from), static_cast<uint16_t>(end));
                        context.Claim(std::move(copy), static_cast<uint16_t>(from), static_cast<uint16_t>(end));
                    }
                }
            }
        }
    }
}

}  // namespace analysis
