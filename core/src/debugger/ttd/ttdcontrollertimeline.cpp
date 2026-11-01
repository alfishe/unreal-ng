/// @file ttdcontrollertimeline.cpp
/// @brief TimeTravelController::QueryTimeline: the timeline summary (TD-5,
/// docs/inprogress/2026-10-31-ttd-timeline-summary/design.md). Every fact comes from what the session stores - the
/// engine checkpoints and their journal cursors, the changed pieces, the coverage index, the input, marker, bookmark
/// and event journals, the port and media journals - so the call replays nothing and leaves the machine alone.

#include "stdafx.h"
#include "timetravelcontroller.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <utility>

#include "debugger/ttd/ttdcursorcell.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"

namespace ttd
{

namespace
{
/// Models whose picture is the ZX screen of one RAM page (step 1 of the plan); the others report `screen` missing
bool HasUlaScreen(MEM_MODEL model)
{
    switch (model)
    {
        case MM_SPECTRUM48:
        case MM_SPECTRUM128:
        case MM_PLUS2:
        case MM_PLUS2A:
        case MM_PLUS3:
        case MM_PENTAGON:
        case MM_SCORP:
        case MM_PROFSCORP:
            return true;
        default:
            return false;
    }
}

/// Most events reported; the rest are counted in the buckets only
constexpr size_t kMaxEvents = 1000;

/// Coverage keys are (page field << 14 | offset) under 2^23 (TTDCoverageIndex::kKeySpaceBits)
constexpr size_t kCoverageKeySlots = size_t{1} << 23;

const char* MarkerKind(TTDExternalEventKind kind)
{
    switch (kind)
    {
        case TTDExternalEventKind::TapeControl: return "marker:tape";
        case TTDExternalEventKind::DiskWrite: return "marker:disk";
        case TTDExternalEventKind::DebuggerEdit: return "marker:edit";
        case TTDExternalEventKind::HardwareReset: return "marker:reset";
        default: return "marker:other";
    }
}

/// A TapeControl marker that starts (1) or ends (0) playback; -1 leaves it as it was (rewind, seek)
int TapePlays(const TTDExternalEvent& e)
{
    if (e.kind != TTDExternalEventKind::TapeControl)
        return -1;
    const std::string reason(e.reason);
    if (reason == "tape play" || reason == "tape resume")
        return 1;
    if (reason == "tape stop" || reason == "tape pause" || reason == "tape park")
        return 0;
    return -1;
}

/// The port map's devices (rows grouped by name) and which one a port belongs to: the matching row with the most
/// decoded address bits, so a narrow decode wins over a partial one that overlaps it
class PortClassifier
{
public:
    static constexpr int16_t kNone = -1;

    PortClassifier(const PortDecoder* decoder, std::vector<TTDTimelineDevice>& devices) : _memo(65536, kUnknown)
    {
        if (!decoder)
            return;
        for (const PortMapEntry& e : decoder->getPortMapEntries())
        {
            const std::string name = e.device ? e.device : "";
            size_t d = 0;
            while (d < devices.size() && devices[d].name != name)
                ++d;
            if (d == devices.size())
                devices.push_back({name, 0});
            devices[d].tags |= e.tags;
            _rows.push_back({e.mask, e.match, static_cast<int16_t>(d), e.latch == PagingLatch::PFE});
        }
    }

    /// Device index of @p port, or kNone
    int16_t DeviceOf(uint16_t port)
    {
        int16_t& m = _memo[port];
        if (m == kUnknown)
        {
            m = kNone;
            int best = -1;
            for (const Row& r : _rows)
                if ((port & r.mask) == r.match && std::popcount(r.mask) > best)
                {
                    best = std::popcount(r.mask);
                    m = r.device;
                }
        }
        return m;
    }

    /// The ULA's #FE (keyboard, EAR, border, beeper): the device of the row bound to that latch
    int16_t UlaDevice() const
    {
        for (const Row& r : _rows)
            if (r.ula)
                return r.device;
        return kNone;
    }

private:
    static constexpr int16_t kUnknown = -2;
    struct Row
    {
        uint16_t mask;
        uint16_t match;
        int16_t device;
        bool ula;
    };
    std::vector<Row> _rows;
    std::vector<int16_t> _memo;
};
/// One frame's facts (the first pass); its devices and media slots in FrameDevices ranges
struct FrameFacts
{
    TTDTimelineFrame cut;
    uint64_t in = 0;
    uint64_t out = 0;
    uint64_t sectors = 0;
    uint64_t vectors = 0;
    uint32_t ramPieces = 0;
    uint32_t borderOuts = 0;
    uint32_t beeperToggles = 0;
    uint32_t writes = 0;
    uint32_t keyEvents = 0;
    uint32_t otherInput = 0;
    uint32_t netEvents = 0;
    bool borderChange = false;
    bool lengthChange = false;
    uint32_t firstDevice = 0;   ///< into the devices list
    uint32_t deviceCount = 0;
};

struct DeviceCount
{
    int32_t device;   ///< index into result.devices; -1 - slot: a media slot
    uint32_t in;
    uint32_t out;
};

/// Sums frames [a, b] (relative to the window) into @p into
void SumFrames(const std::vector<FrameFacts>& frames, const std::vector<DeviceCount>& devices, size_t a, size_t b,
               TTDTimelineBucket& into)
{
    for (size_t i = a; i <= b; ++i)
    {
        const FrameFacts& f = frames[i];
        into.ramPieces += f.ramPieces;
        into.screenFrames += f.cut.screen;
        into.haltedFrames += f.cut.halted;
        into.borderChanges += f.borderChange;
        into.pagingChanges += f.cut.paging;
        into.lengthChanges += f.lengthChange;
        into.in += f.in;
        into.out += f.out;
        into.sectors += f.sectors;
        into.vectors += f.vectors;
        into.keyEvents += f.keyEvents;
        into.otherInput += f.otherInput;
        into.netEvents += f.netEvents;
        into.ulaReads += f.cut.ulaReads;
        into.borderOuts += f.borderOuts;
        into.beeperToggles += f.beeperToggles;
        into.writes += f.writes;
        into.tapePlaying = into.tapePlaying || f.cut.tape;
        into.diskWritten = into.diskWritten || f.cut.diskWritten;
        for (uint32_t k = f.firstDevice; k < f.firstDevice + f.deviceCount; ++k)
        {
            const DeviceCount& d = devices[k];
            if (d.device >= 0)
            {
                into.deviceIn[static_cast<size_t>(d.device)] += d.in;
                into.deviceOut[static_cast<size_t>(d.device)] += d.out;
            }
            else
                into.mediaSectors[static_cast<size_t>(-1 - d.device)] += d.in;
        }
    }
}
}  // namespace

TTDTimelineResult TimeTravelController::QueryTimeline(const TTDTimelineOptions& options) const
{
    const auto started = std::chrono::steady_clock::now();
    const SessionOperation op{*this, SessionOperation::Kind::Read};
    TTDTimelineResult result;
    result.fps = TTDFrameRate(_context);

    // The frames with a checkpoint at both ends: what the session holds of them is whole
    const TimeTravelEngine& engine = *_engine;
    const size_t firstCp = engine.FirstCheckpoint();
    const size_t cpCount = engine.CheckpointCount();
    if (cpCount < firstCp + 2)
    {
        result.error = "no history";
        return result;
    }
    const uint64_t heldFrom = engine.Checkpoint(firstCp)->position.frame;
    const uint64_t heldTo = engine.Checkpoint(cpCount - 1)->position.frame - 1;
    const uint64_t fromFrame = std::max(options.fromFrame, heldFrom);
    uint64_t toFrame = std::min(options.toFrame, heldTo);
    if (fromFrame > toFrame)
    {
        result.error = "window outside the history (frames " + std::to_string(heldFrom) + ".." +
                       std::to_string(heldTo) + ")";
        return result;
    }
    const int64_t firstIndex = engine.CheckpointIndexOf({0, fromFrame, 0});
    if (firstIndex < 0)
    {
        result.error = "no checkpoint at frame " + std::to_string(fromFrame);
        return result;
    }
    // Overview buckets, when asked: an explicit size cuts the window at `limit` of them
    const size_t limit = std::clamp(options.limit, size_t{1}, size_t{500});
    uint64_t bucketSize = options.bucketSize;
    if (options.buckets && bucketSize != 0 && (toFrame - fromFrame) / bucketSize + 1 > limit)
    {
        toFrame = fromFrame + bucketSize * limit - 1;
        result.truncated = true;
    }
    if (bucketSize == 0)
        bucketSize = std::max<uint64_t>(1, (toFrame - fromFrame + limit) / limit);
    result.fromFrame = fromFrame;
    result.toFrame = toFrame;
    result.bucketSize = options.buckets ? bucketSize : 0;
    const size_t frameCount = static_cast<size_t>(toFrame - fromFrame + 1);

    // What this session cannot tell
    const MEM_MODEL model = _context ? _context->config.mem_model : MM_PENTAGON;
    const bool ulaScreen = HasUlaScreen(model);
    const bool coverage = _coverageIndex.SealedFrameCount(TTDCoverageKind::Executed) > 0;
    const bool writes = engine.Writes().Size() > 0;
    if (!coverage)
        result.missing.push_back({"coverage", "the coverage index is off"});
    if (!ulaScreen)
        result.missing.push_back({"screen", "the screen pages of this model are not mapped yet"});
    if (!writes)
        result.missing.push_back({"writes", "the write journal is off"});
    if (!options.ports)
        result.missing.push_back({"ports", "not asked (ports=false)"});

    uint32_t ramRegion = UINT32_MAX;
    const std::vector<TTDRegionDesc>& regions = engine.Regions();
    for (uint32_t r = 0; r < regions.size(); ++r)
        if (regions[r].id < TTDRegionId::DeviceStateFirst && regions[r].name == "ram")
        {
            ramRegion = r;
            break;
        }

    PortClassifier ports(options.ports && _context ? _context->pPortDecoder : nullptr, result.devices);
    const int16_t ula = ports.UlaDevice();
    for (const std::string& slot : engine.MediaReads().SlotNames())
        result.media.push_back({slot, 0});
    auto deviceHas = [&](int16_t d, PortTag family) {
        const uint32_t f = static_cast<uint32_t>(family);
        return d >= 0 && (result.devices[static_cast<size_t>(d)].tags & f) == f;
    };

    // Pass 1: every frame's facts, from its checkpoint (the start), the next one (what it changed, where the
    // journals stood) and the port and media records between them
    std::vector<FrameFacts> frames(frameCount);
    bool haltKnown = true;
    std::vector<DeviceCount> deviceCounts;
    std::vector<uint32_t> in(result.devices.size());
    std::vector<uint32_t> out(result.devices.size());
    std::vector<uint32_t> slotSectors(result.media.size());
    TTDPortJournal::ReadCache readCache;
    TTDPortJournal::ReadCache writeCache;
    TTDPortRecord rec;
    const TTDEngineCheckpoint* prev = firstIndex > static_cast<int64_t>(firstCp)
                                          ? engine.Checkpoint(static_cast<size_t>(firstIndex) - 1)
                                          : nullptr;
    for (size_t i = static_cast<size_t>(firstIndex); i + 1 < cpCount; ++i)
    {
        const TTDEngineCheckpoint* cp = engine.Checkpoint(i);
        const TTDEngineCheckpoint* next = engine.Checkpoint(i + 1);
        if (!cp || !next || cp->position.frame > toFrame)
            break;
        FrameFacts& f = frames[static_cast<size_t>(cp->position.frame - fromFrame)];
        f.in = next->busReadCursor - cp->busReadCursor;
        f.out = next->busWriteCursor - cp->busWriteCursor;
        f.sectors = next->mediaReadCursor - cp->mediaReadCursor;
        f.vectors = next->busVectorCursor - cp->busVectorCursor;
        // A HALT-paced frame: the INT that ends it was accepted while halted (the checkpoint itself is taken past
        // that acknowledge, so its own halted latch is already clear)
        f.cut.halted = next->haltedInterrupts > cp->haltedInterrupts;
        haltKnown = haltKnown && cp->haltKnown && next->haltKnown;
        f.cut.storage = static_cast<uint32_t>(f.sectors);
        if (prev)
        {
            f.borderChange = cp->chipset.border_attr != prev->chipset.border_attr;
            f.cut.paging = cp->chipset.p7FFD != prev->chipset.p7FFD;
            f.lengthChange = next->start - cp->start != cp->start - prev->start;
        }
        if (ramRegion != UINT32_MAX)
        {
            f.ramPieces = engine.ChangeCount(i + 1, ramRegion);
            if (ulaScreen && f.ramPieces > 0)
            {
                const uint32_t screen = Screen::GetVideoRAMPage(model, cp->chipset.p7FFD) * 4u;
                for (const auto& change : engine.ChangesAt(i + 1, ramRegion))
                    f.cut.screen = f.cut.screen || change.first == screen || change.first == screen + 1;
            }
        }
        f.firstDevice = static_cast<uint32_t>(deviceCounts.size());
        for (uint64_t k = cp->mediaReadCursor; k < next->mediaReadCursor && k < engine.MediaReads().Size(); ++k)
        {
            const uint16_t slot = engine.MediaReads().At(k).slot;
            if (slot < slotSectors.size())
                ++slotSectors[slot];
        }
        for (size_t m = 0; m < slotSectors.size(); ++m)
            if (slotSectors[m])
            {
                deviceCounts.push_back({-1 - static_cast<int32_t>(m), slotSectors[m], 0});
                slotSectors[m] = 0;
            }
        if (options.ports)
        {
            const TTDPortJournal& reads = engine.BusReads();
            const TTDPortJournal& outs = engine.BusWrites();
            result.portRecords += f.in + f.out;
            for (uint64_t k = std::max(cp->busReadCursor, reads.FirstIndex()); k < next->busReadCursor; ++k)
                if (reads.Get(k, rec, readCache))
                    if (const int16_t d = ports.DeviceOf(rec.port); d >= 0)
                        ++in[static_cast<size_t>(d)];
            uint8_t fe = cp->chipset.pFE;
            for (uint64_t k = std::max(cp->busWriteCursor, outs.FirstIndex()); k < next->busWriteCursor; ++k)
                if (outs.Get(k, rec, writeCache))
                {
                    const int16_t d = ports.DeviceOf(rec.port);
                    if (d < 0)
                        continue;
                    ++out[static_cast<size_t>(d)];
                    if (d == ula)
                    {
                        ++f.borderOuts;
                        f.beeperToggles += ((rec.value ^ fe) & 0x10) != 0;
                        fe = rec.value;
                    }
                }
            for (size_t d = 0; d < in.size(); ++d)
                if (in[d] || out[d])
                {
                    const int16_t dev = static_cast<int16_t>(d);
                    if (dev == ula)
                        f.cut.ulaReads = in[d];
                    if (deviceHas(dev, PortTag::Storage))
                        f.cut.storage += in[d] + out[d];
                    if (deviceHas(dev, PortTag::Sound))
                        f.cut.sound += out[d];
                    if (deviceHas(dev, PortTag::Network))
                        f.cut.net += in[d] + out[d];
                    deviceCounts.push_back({static_cast<int32_t>(d), in[d], out[d]});
                    in[d] = out[d] = 0;
                }
            f.cut.sound += f.beeperToggles;
        }
        f.deviceCount = static_cast<uint32_t>(deviceCounts.size()) - f.firstDevice;
        prev = cp;
    }
    if (!haltKnown)
    {
        result.missing.push_back({"halt", "the session was loaded from a file, which does not keep the HALT waits"});
        for (FrameFacts& f : frames)
            f.cut.halted = false;
    }
    auto frameOf = [&](uint64_t frame) -> FrameFacts* {
        return frame >= fromFrame && frame <= toFrame ? &frames[static_cast<size_t>(frame - fromFrame)] : nullptr;
    };

    // Coverage: distinct written keys per frame (what a burst is measured by)
    std::vector<TTDCoverageKey> frameKeys;
    if (coverage)
        for (size_t i = 0; i < frameCount; ++i)
            if (_coverageIndex.GetFrameKeys(TTDCoverageKind::Written, fromFrame + i, frameKeys))
                frames[i].cut.written = static_cast<uint32_t>(frameKeys.size());

    // The write journal, where it was on: a write at machine time t belongs to the frame whose start is before it
    if (writes)
    {
        std::vector<TTDMachineTime> starts;
        std::vector<uint64_t> starting;
        for (size_t i = firstCp; i < cpCount; ++i)
            if (const TTDEngineCheckpoint* cp = engine.Checkpoint(i))
            {
                starts.push_back(cp->start);
                starting.push_back(cp->position.frame);
            }
        engine.Writes().ForEach([&](const TTDWriteRecord& w) {
            const auto it = std::lower_bound(starts.begin(), starts.end(), TTDMachineTime{w.globalT});
            if (it == starts.begin() || w.isIo)
                return;
            if (FrameFacts* f = frameOf(starting[static_cast<size_t>(it - starts.begin()) - 1]))
                ++f->writes;
        });
    }

    // Input; the keys pressed, kept for the per-range lists
    std::vector<std::pair<uint64_t, uint8_t>> keyDowns;
    for (const TTDInputEvent& e : _inputJournal.Events())
        if (FrameFacts* f = frameOf(e.time.frame))
        {
            if (e.kind == TTDInputKind::Key)
            {
                if (!e.pressed)
                    continue;
                ++f->keyEvents;
                keyDowns.push_back({e.time.frame, e.key});
            }
            else if (HasNetRecord(e.kind) || e.kind == TTDInputKind::NetLinkReset)
            {
                ++f->netEvents;
                ++f->cut.net;
            }
            else if (e.kind != TTDInputKind::KeyboardReset)
                ++f->otherInput;
            f->cut.input = f->keyEvents + f->otherInput > 0;
        }

    // Markers: tape playing spans and disk writes; then the events list
    bool playing = false;
    uint64_t playFrom = 0;
    auto markPlaying = [&](uint64_t from, uint64_t to) {
        for (uint64_t frame = std::max(from, fromFrame); frame <= std::min(to, toFrame); ++frame)
            frameOf(frame)->cut.tape = true;
    };
    for (const TTDExternalEvent& e : _externalEvents.Events())
    {
        const int plays = TapePlays(e);
        if (plays == 1 && !playing)
        {
            playing = true;
            playFrom = e.time.frame;
        }
        else if (plays == 0 && playing)
        {
            playing = false;
            markPlaying(playFrom, e.time.frame);
        }
        if (FrameFacts* f = frameOf(e.time.frame))
        {
            f->cut.diskWritten = f->cut.diskWritten || e.kind == TTDExternalEventKind::DiskWrite;
            if (result.events.size() < kMaxEvents)
                result.events.push_back({e.time.frame, e.time.tInFrame, MarkerKind(e.kind), e.reason});
        }
    }
    if (playing)
        markPlaying(playFrom, toFrame);
    for (const TTDBookmark& bm : _bookmarks.Snapshot())
        if (frameOf(bm.time.frame) && result.events.size() < kMaxEvents)
            result.events.push_back({bm.time.frame, bm.time.tInFrame, "bookmark", bm.label});

    // The engine's own events: media changes, config cuts, frame lengths, replay sources
    const TTDEventLog& log = engine.Events();
    const TTDEngineCheckpoint* endCp = engine.Checkpoint(static_cast<size_t>(firstIndex) + frameCount);
    const TTDMachineTime logFrom = engine.Checkpoint(static_cast<size_t>(firstIndex))->start;
    const TTDMachineTime logTo = endCp ? endCp->start : UINT64_MAX;
    for (size_t k = log.CursorAt(logFrom); k < log.Count() && log.At(k).machineTime < logTo; ++k)
    {
        const TTDEvent& e = log.At(k);
        const char* kind = e.kind == TTDEventKind::MediaChange          ? "media"
                           : e.kind == TTDEventKind::ConfigChange       ? "config"
                           : e.kind == TTDEventKind::FrameLengthChange  ? "frame-length"
                           : e.kind == TTDEventKind::ReplaySourceChange ? "replay-source"
                                                                        : nullptr;
        TTDPosition at;
        if (!kind || result.events.size() >= kMaxEvents || !engine.PositionOf(e.machineTime, at))
            continue;
        std::string text;
        if (e.kind == TTDEventKind::MediaChange)
        {
            const std::vector<uint8_t>& bytes = engine.Payloads().Bytes(e.payload);
            text.assign(bytes.begin(), bytes.end());
        }
        else if (e.kind == TTDEventKind::ReplaySourceChange)
            text = e.args[0] == static_cast<uint8_t>(TTDReplaySource::RzxPlayback) ? "rzx" : "live";
        result.events.push_back({at.frame, static_cast<uint32_t>(at.tInFrame), kind, std::move(text)});
    }
    std::stable_sort(result.events.begin(), result.events.end(), [](const TTDTimelineEvent& a, const TTDTimelineEvent& b) {
        return a.frame != b.frame ? a.frame < b.frame : a.tInFrame < b.tInFrame;
    });

    // The phases, at frame precision
    std::vector<TTDTimelineFrame> cut(frameCount);
    for (size_t i = 0; i < frameCount; ++i)
        cut[i] = frames[i].cut;
    const double fps = result.fps > 0 ? result.fps : 50.0;
    const unsigned minFrames =
        std::max(1u, static_cast<unsigned>(std::lround(options.minPhaseMs * fps / 1000.0)));
    result.phases = CutTimelinePhases(cut, fromFrame, minFrames);

    // Each range's facts: the frames summed, the coverage keys united, the keys pressed. A range's distinct keys are
    // counted with a stamp per key (the key's last range), not by sorting the range's keys: one look per key
    std::vector<uint32_t> stamp(coverage ? kCoverageKeySlots : 0, 0);
    uint32_t generation = 0;
    std::vector<uint32_t> executedPerFrame(coverage ? frameCount : 0, 0);
    auto fill = [&](TTDTimelineBucket& b) {
        b.deviceIn.assign(result.devices.size(), 0);
        b.deviceOut.assign(result.devices.size(), 0);
        b.mediaSectors.assign(result.media.size(), 0);
        SumFrames(frames, deviceCounts, static_cast<size_t>(b.frameStart - fromFrame),
                  static_cast<size_t>(b.frameEnd - fromFrame), b);
        for (const auto& [frame, key] : keyDowns)
            if (frame >= b.frameStart && frame <= b.frameEnd)
            {
                const auto at = std::lower_bound(b.keys.begin(), b.keys.end(), key);
                if (at == b.keys.end() || *at != key)
                    b.keys.insert(at, key);
            }
        if (!coverage)
            return;
        for (const TTDCoverageKind kind : {TTDCoverageKind::Executed, TTDCoverageKind::Written, TTDCoverageKind::Read})
        {
            const uint32_t mark = ++generation;
            uint32_t distinct = 0;
            for (uint64_t frame = b.frameStart; frame <= b.frameEnd; ++frame)
            {
                if (!_coverageIndex.GetFrameKeys(kind, frame, frameKeys))
                    continue;
                if (kind == TTDCoverageKind::Executed)
                    executedPerFrame[static_cast<size_t>(frame - fromFrame)] = static_cast<uint32_t>(frameKeys.size());
                for (const TTDCoverageKey key : frameKeys)
                    if (key < kCoverageKeySlots && stamp[key] != mark)
                    {
                        stamp[key] = mark;
                        ++distinct;
                    }
            }
            (kind == TTDCoverageKind::Executed ? b.executed
             : kind == TTDCoverageKind::Written ? b.written
                                                : b.read) = distinct;
        }
    };
    for (TTDTimelinePhase& p : result.phases)
    {
        p.facts.frameStart = p.frameStart;
        p.facts.frameEnd = p.frameEnd;
        fill(p.facts);
        p.facts.tags = p.tags;
        // The busiest frame: the most distinct keys executed (coverage, counted by the fill), else the most port
        // records
        uint64_t busiest = 0;
        p.busiestFrame = p.frameStart;
        for (uint64_t frame = p.frameStart; frame <= p.frameEnd; ++frame)
        {
            const size_t i = static_cast<size_t>(frame - fromFrame);
            const uint64_t weight = coverage ? executedPerFrame[i] : frames[i].in + frames[i].out;
            if (weight > busiest)
            {
                busiest = weight;
                p.busiestFrame = frame;
            }
        }
    }
    if (options.buckets)
    {
        const size_t bucketCount = static_cast<size_t>((toFrame - fromFrame) / bucketSize + 1);
        result.buckets.resize(bucketCount);
        for (size_t b = 0; b < bucketCount; ++b)
        {
            TTDTimelineBucket& bucket = result.buckets[b];
            bucket.frameStart = fromFrame + b * bucketSize;
            bucket.frameEnd = std::min(bucket.frameStart + bucketSize - 1, toFrame);
            fill(bucket);
            // A bucket's tags: those of the phases it overlaps
            for (const TTDTimelinePhase& p : result.phases)
                if (p.frameStart <= bucket.frameEnd && p.frameEnd >= bucket.frameStart)
                    bucket.tags |= p.tags;
        }
    }

    result.elapsedNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started).count());
    return result;
}

}  // namespace ttd
