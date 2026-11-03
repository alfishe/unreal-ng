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
#include <map>
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

/// The maps of the tiles a block copy put into a drawn buffer (the graphics detector's copied tile sets): the bytes
/// whose values were the index of a read inside the set (L1 indexes: tile = the byte, the copy from base + L x tile).
/// Runs of them, joined over gaps no code touched (no data access, no fetch), in scope, no other claim on them (a
/// buffer around the map aside) and whose bytes all hold tile numbers no larger than the largest one seen (a part of
/// the map the replay did not reach); a run a multiple of the copying loop's trips is that wide, any other a pool of
/// cells read from varying starts, one row. A run ends at a 16 K page boundary and at a byte code, text or an unused
/// claim holds (a map there would clash with it)
void CopiedTileMaps(DetectorContext& context)
{
    constexpr uint32_t kMinCells = 16;
    constexpr uint32_t kMaxGap = 128;
    // The sets first: claiming adds to the store, which would move the regions under a loop over them
    struct CopiedSet
    {
        uint16_t lo = 0;
        uint16_t hi = 0;
        uint16_t copier = 0;
    };
    std::vector<CopiedSet> sets;
    // Per Z80 address: 1 a claim a map cannot share (code, text, unused), 2 any other claim but a buffer's (a gap the
    // replay did not reach is no map's when something else explains it)
    std::vector<uint8_t> taken(0x10000, 0);
    for (const Region& r : context.out.store.Regions())
    {
        if (r.kind == RegionKind::Tile && r.detector == DetectorId::Graphics && r.geometry.stride != 0)
        {
            uint16_t copier = 0;
            for (const Evidence& e : r.evidence)
                if (e.what == "copy" && e.detail.rfind("tiles block-copied", 0) == 0)
                    copier = e.z80;
            if (copier != 0)
                sets.push_back(CopiedSet{r.z80, r.Z80End(), copier});
        }
        const bool fallback =
            r.detector == DetectorId::CodeMap && r.kind != RegionKind::Code && r.kind != RegionKind::Unused;
        if (r.kind == RegionKind::Untouched || r.kind == RegionKind::Buffer || r.title == kRomWriteSink || fallback)
            continue;
        const bool clash = r.kind == RegionKind::Code || r.kind == RegionKind::Text || r.kind == RegionKind::Unused;
        uint16_t z80 = 0;
        if (!context.Z80Of(r.lo, z80))
            continue;
        for (uint32_t i = 0; i < r.Size() && z80 + i <= 0xFFFF; ++i)
            if (clash || taken[z80 + i] == 0)
                taken[z80 + i] = clash ? 1 : 2;
    }
    for (const CopiedSet& set : sets)
    {
        const uint16_t copier = set.copier;
        std::map<uint16_t, uint64_t> cells;
        uint8_t largest = 0;
        for (const IndexUse& x : context.l1.indexes)
        {
            if (x.lo < set.lo || x.hi > set.hi || (x.selectorZ80 >= set.lo && x.selectorZ80 <= set.hi) ||
                !context.InScope(x.selectorZ80) || taken[x.selectorZ80] == 1)
                continue;
            cells[x.selectorZ80] += x.count;
            largest = std::max(largest, context.Start(x.selectorZ80));
        }
        // The innermost loop around the copy: its trips are the cells of one walk
        const Loop* walk = nullptr;
        for (const Loop& l : context.l1.loops)
            if (l.z80 <= copier && copier <= l.latchZ80 &&
                (walk == nullptr || l.latchZ80 - l.z80 < walk->latchZ80 - walk->z80))
                walk = &l;
        const uint32_t trips = walk != nullptr ? DominantTrips(*walk) : 0;
        const auto untouchedCell = [&](uint32_t at) {
            const auto z80 = static_cast<uint16_t>(at);
            const ByteUse& u = context.Use(z80);
            return at <= 0xFFFF && context.InScope(z80) && taken[at] == 0 && u.reads == 0 && u.writes == 0 &&
                   !DetectorContext::Code(u) && context.Start(z80) <= largest;
        };
        auto it = cells.begin();
        while (it != cells.end())
        {
            uint32_t lo = it->first, hi = it->first;
            uint64_t seen = 0;
            for (; it != cells.end(); ++it)
            {
                // One page: a cell in the next 16 K page starts a run of its own
                if ((static_cast<uint32_t>(it->first) >> 14) != (lo >> 14))
                    break;
                if (it->first > hi + 1)
                {
                    bool join = it->first - hi - 1 <= kMaxGap;
                    for (uint32_t at = hi + 1; join && at < it->first; ++at)
                        join = untouchedCell(at);
                    if (!join)
                        break;
                }
                hi = it->first;
                ++seen;
            }
            const uint32_t size = hi - lo + 1;
            if (size < kMinCells)
                continue;
            const bool rows = trips >= 2 && size % trips == 0;
            Region r;
            r.kind = RegionKind::LevelMap;
            r.detector = DetectorId::LevelMaps;
            r.status = seen == size ? RegionStatus::Observed : RegionStatus::Inferred;
            r.geometry.width = static_cast<uint16_t>(rows ? trips : size);
            r.geometry.height = static_cast<uint16_t>(rows ? size / trips : 1);
            r.geometry.frames = 1;
            r.geometry.stride = r.geometry.width;
            r.geometry.layout = "cells";
            r.confidence = confidence::FromCount(600, 850, seen, kMinCells);
            r.evidence.push_back(Evidence{"tiles", copier, seen,
                                          "each cell's value picked a tile of the set at " + Hex(set.lo) +
                                              " that the copy at " + Hex(copier) + " put into the buffer"});
            if (seen != size)
                r.evidence.push_back(Evidence{"gaps", static_cast<uint16_t>(lo), size - seen,
                                              "cells no code touched between them, tile numbers too"});
            if (!rows)
                r.evidence.push_back(Evidence{"pool", static_cast<uint16_t>(lo), trips,
                                              "not a multiple of the walk's " + std::to_string(trips) +
                                                  " cells: runs read from varying starts"});
            r.users = context.UsersOf(static_cast<uint16_t>(lo), static_cast<uint16_t>(hi));
            if (context.Claim(std::move(r), static_cast<uint16_t>(lo), static_cast<uint16_t>(hi)))
                for (uint32_t a = lo; a <= hi; ++a)
                    if (taken[a] == 0)
                        taken[a] = 2;
        }
    }
}

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
    CopiedTileMaps(context);
}

}  // namespace analysis
