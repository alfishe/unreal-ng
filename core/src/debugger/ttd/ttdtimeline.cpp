#include "ttdtimeline.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <set>
#include <utility>


namespace ttd
{

namespace
{
constexpr const char* kTagNames[kTimelineTagCount] = {"tape",   "disk",  "input", "net",   "sound",
                                                      "screen", "burst", "halt",  "paging"};

/// A loader's edge loop reads the ULA thousands of times a frame; a keyboard scan a few dozen
constexpr uint64_t kTapeReadsPerFrame = 200;
/// A burst writes at least 256 distinct addresses a frame (a floor against "4x a near-zero median": UMT23X's depacker
/// writes about 510 a frame for 14 frames) ...
constexpr uint64_t kBurstFloor = 256;
/// ... and this many times the recording's median frame
constexpr uint64_t kBurstOverMedian = 4;

/// Prefix sums of one per-frame value: Sum(a, b) over frames [a, b)
class Prefix
{
public:
    template <typename F>
    Prefix(size_t n, F value) : _sum(n + 1, 0)
    {
        for (size_t i = 0; i < n; ++i)
            _sum[i + 1] = _sum[i] + value(i);
    }
    uint64_t Sum(size_t a, size_t b) const { return _sum[b] - _sum[a]; }

private:
    std::vector<uint64_t> _sum;
};
}  // namespace

const char* TimelineTagName(unsigned bit)
{
    return bit < kTimelineTagCount ? kTagNames[bit] : "";
}

std::vector<std::string> TimelineTagNames(uint32_t tags)
{
    std::vector<std::string> names;
    for (unsigned bit = 0; bit < kTimelineTagCount; ++bit)
        if (tags & (1u << bit))
            names.emplace_back(kTagNames[bit]);
    return names;
}

std::vector<TTDTimelinePhase> CutTimelinePhases(const std::vector<TTDTimelineFrame>& frames, uint64_t firstFrame,
                                                unsigned minFrames)
{
    std::vector<TTDTimelinePhase> phases;
    const size_t n = frames.size();
    if (n == 0)
        return phases;
    minFrames = std::max(1u, minFrames);

    // Each frame's regime tags, measured over the window of minFrames centred on it: rates, so one key click or one
    // beeper blip does not make a phase of its own
    std::vector<uint32_t> written(n);
    for (size_t i = 0; i < n; ++i)
        written[i] = frames[i].written;
    std::nth_element(written.begin(), written.begin() + static_cast<std::ptrdiff_t>(n / 2), written.end());
    const uint64_t median = written[n / 2];
    const Prefix pWritten(n, [&](size_t i) { return frames[i].written; });
    const Prefix pUla(n, [&](size_t i) { return frames[i].tapeIn ? frames[i].ulaReads : 0u; });
    const Prefix pTape(n, [&](size_t i) { return frames[i].tape ? 1u : 0u; });
    const Prefix pDisk(n, [&](size_t i) { return frames[i].storage > 0 || frames[i].diskWritten ? 1u : 0u; });
    const Prefix pNet(n, [&](size_t i) { return frames[i].net > 0 ? 1u : 0u; });
    const Prefix pSound(n, [&](size_t i) { return frames[i].sound > 0 ? 1u : 0u; });
    const Prefix pScreen(n, [&](size_t i) { return frames[i].screen ? 1u : 0u; });
    const Prefix pHalt(n, [&](size_t i) { return frames[i].halted ? 1u : 0u; });
    const Prefix pPaging(n, [&](size_t i) { return frames[i].paging ? 1u : 0u; });
    const size_t half = minFrames / 2;
    auto regime = [&](size_t i) {
        const size_t a = i > half ? i - half : 0;
        const size_t b = std::min(n, a + minFrames);
        const uint64_t w = b - a;
        uint32_t tags = 0;
        auto quarter = [w](uint64_t count) { return count * 4 >= w && count > 0; };
        if (quarter(pTape.Sum(a, b)) || pUla.Sum(a, b) >= kTapeReadsPerFrame * w)
            tags |= kTimelineTape;
        if (quarter(pDisk.Sum(a, b)))
            tags |= kTimelineDisk;
        if (quarter(pNet.Sum(a, b)))
            tags |= kTimelineNet;
        if (quarter(pSound.Sum(a, b)))
            tags |= kTimelineSound;
        if (quarter(pScreen.Sum(a, b)))
            tags |= kTimelineScreen;
        const uint64_t wrote = pWritten.Sum(a, b);
        if (wrote >= kBurstFloor * w && wrote > kBurstOverMedian * median * w)
            tags |= kTimelineBurst;
        if (pHalt.Sum(a, b) * 2 > w)
            tags |= kTimelineHalt;
        if (quarter(pPaging.Sum(a, b)))
            tags |= kTimelinePaging;
        return tags;
    };

    // Runs of one regime, linked; the shortest under minFrames joins the neighbour closer in tags until none is left
    struct Run
    {
        size_t first;
        size_t last;
        uint32_t tags;
        size_t prev;
        size_t next;
    };
    constexpr size_t kNone = SIZE_MAX;
    std::vector<Run> runs;
    for (size_t i = 0; i < n; ++i)
    {
        const uint32_t tags = regime(i);
        if (!runs.empty() && runs.back().tags == tags)
            runs.back().last = i;
        else
        {
            runs.push_back({i, i, tags, runs.empty() ? kNone : runs.size() - 1, kNone});
            if (runs.size() > 1)
                runs[runs.size() - 2].next = runs.size() - 1;
        }
    }
    auto length = [&](size_t r) { return runs[r].last - runs[r].first + 1; };
    std::set<std::pair<size_t, size_t>> byLength;   // (length, first frame) -> the run whose first frame it is
    std::vector<size_t> runAt(n, kNone);
    for (size_t r = 0; r < runs.size(); ++r)
    {
        byLength.insert({length(r), runs[r].first});
        runAt[runs[r].first] = r;
    }
    // Run b joins run a (adjacent); a keeps its tags
    auto absorb = [&](size_t a, size_t b) {
        byLength.erase({length(a), runs[a].first});
        byLength.erase({length(b), runs[b].first});
        runAt[runs[b].first] = kNone;
        runAt[runs[a].first] = kNone;
        if (runs[b].first < runs[a].first)
        {
            runs[a].first = runs[b].first;
            runs[a].prev = runs[b].prev;
            if (runs[a].prev != kNone)
                runs[runs[a].prev].next = a;
        }
        else
        {
            runs[a].last = runs[b].last;
            runs[a].next = runs[b].next;
            if (runs[a].next != kNone)
                runs[runs[a].next].prev = a;
        }
        runAt[runs[a].first] = a;
        byLength.insert({length(a), runs[a].first});
    };
    while (!byLength.empty())
    {
        const auto [len, first] = *byLength.begin();
        if (len >= minFrames)
            break;
        const size_t r = runAt[first];
        const size_t prev = runs[r].prev;
        const size_t next = runs[r].next;
        if (prev == kNone && next == kNone)
            break;
        const int prevDiff = prev != kNone ? std::popcount(runs[r].tags ^ runs[prev].tags) : 1 << 30;
        const int nextDiff = next != kNone ? std::popcount(runs[r].tags ^ runs[next].tags) : 1 << 30;
        const size_t into = prevDiff <= nextDiff ? prev : next;
        const size_t other = into == prev ? next : prev;
        absorb(into, r);
        if (other != kNone && runs[other].tags == runs[into].tags)
            absorb(into, other);
    }

    // The first run alive: walk back from any
    size_t r = runAt[0];
    while (runs[r].prev != kNone)
        r = runs[r].prev;
    for (; r != kNone; r = runs[r].next)
    {
        TTDTimelinePhase p;
        p.frameStart = firstFrame + runs[r].first;
        p.frameEnd = firstFrame + runs[r].last;
        p.tags = runs[r].tags;
        for (size_t i = runs[r].first; i <= runs[r].last; ++i)
        {
            if (frames[i].input)
                p.tags |= kTimelineInput;
            if (frames[i].paging)
                p.tags |= kTimelinePaging;
        }
        phases.push_back(p);
    }
    return phases;
}

}  // namespace ttd
