/// @file ttdcontrollerpastreads.cpp
/// @brief Reads of the past that do not move the machine (PLAN #122, docs/inprogress/2026-11-12-re-api-efficiency):
/// TimeTravelController::ReadMemorySeries (S1: memory over many frames in one forward walk of the checkpoints),
/// ReadScreenAt (V2: a past frame's picture and screens) and CheckpointMoment (E4: where the CPU stood at a frame's
/// checkpoint).

#include "stdafx.h"

#include "timetravelcontroller.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_map>

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/video/screen.h"

namespace ttd
{

namespace
{
/// A piece no range has asked for yet
constexpr uint32_t kUntracked = 0xFFFFFFFEu;

/// Beyond this many checkpoints between two frames read, the tracked pieces are looked up again at the target
/// (VersionAt) instead of following every change in between
constexpr size_t kResyncDistance = 64;

/// Where one CPU window's 16 KB comes from at a checkpoint: a stored region (its byte offset there), or the ROM
struct WindowSource
{
    bool rom = false;
    uint32_t region = 0;
    size_t start = 0;            ///< !rom: byte offset of the window's first byte in the region
    const uint8_t* romBytes = nullptr;
};

/// The ascending frames a query asks for, one at a time
class FrameCursor
{
public:
    explicit FrameCursor(const TTDSeriesQuery& q) : _q(q), _next(q.frames.empty() ? q.fromFrame : 0) {}
    bool Next(uint64_t& frame)
    {
        if (!_q.frames.empty())
        {
            if (_index >= _q.frames.size())
                return false;
            frame = _q.frames[_index++];
            return true;
        }
        if (_done || _next > _q.toFrame)
            return false;
        frame = _next;
        const uint64_t step = std::max<uint64_t>(_q.step, 1);
        if (_q.toFrame - _next < step)
            _done = true;
        else
            _next += step;
        return true;
    }

private:
    const TTDSeriesQuery& _q;
    uint64_t _next = 0;
    size_t _index = 0;
    bool _done = false;
};
}  // namespace

/// region <Memory series>

bool TimeTravelController::ResolveSeriesQuery(TTDSeriesQuery& query, uint64_t& first, uint64_t& last,
                                              std::string& error)
{
    const size_t firstIndex = LineageFirstIndex();
    const size_t endIndex = LineageEndIndex();
    const TTDCheckpoint* firstCp = _line->timeline.empty() ? nullptr : ViewLineageCheckpoint(firstIndex);
    const TTDCheckpoint* lastCp = firstCp && endIndex > firstIndex ? ViewLineageCheckpoint(endIndex - 1) : nullptr;
    if (!firstCp || !lastCp)
    {
        error = "no recorded history";
        return false;
    }
    first = firstCp->time.frame;
    last = lastCp->time.frame;
    if (query.frames.empty())
    {
        if (query.fromStart)
            query.fromFrame = first;
        if (query.toEnd)
            query.toFrame = last;
        query.fromStart = query.toEnd = false;
    }
    const uint64_t lo = query.frames.empty() ? query.fromFrame : query.frames.front();
    const uint64_t hi = query.frames.empty() ? query.toFrame : query.frames.back();
    if (lo < first || hi > last || lo > hi)
    {
        error = "frames " + std::to_string(lo) + ".." + std::to_string(hi) + " are outside the history (frames " +
                std::to_string(first) + ".." + std::to_string(last) + ")";
        return false;
    }
    return true;
}

bool TimeTravelController::MemorySeriesRestores(const TTDSeriesQuery& asked)
{
    const SessionOperation read{*this, SessionOperation::Kind::Read};
    if (_state == TTDSessionState::Recording)
        return false;   // the series pauses the recording first and reads the flushed store
    bool anyCpu = false;
    for (const TTDSeriesRange& r : asked.ranges)
        anyCpu = anyCpu || r.cpu;
    Memory* memory = _context ? _context->pMemory : nullptr;
    if (!anyCpu || !memory || !_engine || _line->timeline.empty())
        return false;
    const LineageViewGuard lineageView{*this};
    TTDSeriesQuery query = asked;
    uint64_t first = 0, last = 0;
    std::string error;
    if (!ResolveSeriesQuery(query, first, last, error))
        return false;
    const std::vector<TTDRegionDesc>& regions = _engine->Regions();
    const uint8_t* block = memory->BlockAt(0);
    const uint8_t* romStart = memory->ROMBase();
    const uint8_t* romEnd = romStart + size_t(MAX_ROM_PAGES) * PAGE_SIZE;
    FrameCursor frames(query);
    uint64_t frame = 0;
    while (frames.Next(frame))
    {
        const int64_t index = ViewLineageIndexAtOrBefore({frame, 0});
        const TTDCheckpoint* cp = index >= 0 ? ViewLineageCheckpoint(size_t(index)) : nullptr;
        if (!cp)
            return false;   // outside the history: refused
        if (!cp->cpuWindows.valid || cp->time.frame != frame)
            return true;
        for (uint8_t w = 0; w < 4; ++w)
        {
            const uint8_t* read = block + cp->cpuWindows.offset[w];
            bool found = read >= romStart && read + 0x4000 <= romEnd;
            for (uint32_t r = 0; r < regions.size() && !found; ++r)
                found = regions[r].memory && read >= regions[r].memory &&
                        read + 0x4000 <= regions[r].memory + regions[r].bytes &&
                        size_t(read - regions[r].memory) % kTTDPieceSize == 0;
            if (!found)
                return true;
        }
    }
    return false;
}

TTDSeriesResult TimeTravelController::ReadMemorySeries(const TTDSeriesQuery& asked)
{
    TTDSeriesResult result;
    const bool restores = MemorySeriesRestores(asked);
    const bool recording = _state == TTDSessionState::Recording;
    // Store reads only: a read of the session. A restore (or pausing a recording for it) changes the machine for a
    // while and puts it back
    const SessionOperation op{*this, restores || recording ? SessionOperation::Kind::Change : SessionOperation::Kind::Read};
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    Memory* memory = _context ? _context->pMemory : nullptr;
    if (!_engine || !memory || _line->timeline.empty())
    {
        result.error = "no recorded history";
        return result;
    }
    if (asked.ranges.empty())
    {
        result.error = "no range to read";
        return result;
    }

    const LineageViewGuard lineageView{*this};
    TTDSeriesQuery query = asked;
    if (!ResolveSeriesQuery(query, result.firstFrame, result.lastFrame, result.error))
        return result;

    const std::vector<TTDRegionDesc>& regions = _engine->Regions();
    for (const TTDSeriesRange& r : query.ranges)
        if (!r.cpu && (r.region >= regions.size() || r.offset + r.length > regions[r.region].bytes))
        {
            result.error = "a range lies outside its memory";
            return result;
        }

    // The pieces the ranges read, per region: their version at the checkpoint the walk stands on, and the bytes of
    // the version decoded last (a piece unchanged since the frame before is not decoded again)
    std::vector<std::vector<uint32_t>> version(regions.size());
    struct Decoded
    {
        uint32_t id = kUntracked;
        std::array<uint8_t, kTTDPieceSize> bytes{};
    };
    std::vector<std::unordered_map<uint32_t, Decoded>> decoded(regions.size());
    size_t at = SIZE_MAX;              // the engine index the versions are of
    uint32_t atLine = UINT32_MAX;      // the engine line viewed there
    bool failed = false;

    auto track = [&](uint32_t r, uint32_t p) -> uint32_t {
        std::vector<uint32_t>& v = version[r];
        if (v.empty())
            v.assign(regions[r].pieces, kUntracked);
        if (v[p] == kUntracked)
        {
            ViewLineageIndex(at);
            v[p] = _engine->VersionAt(at, r, p);
        }
        return v[p];
    };
    auto resync = [&](size_t index, uint32_t onlyRegion) {
        ViewLineageIndex(index);
        for (uint32_t r = 0; r < version.size(); ++r)
        {
            if (onlyRegion != UINT32_MAX && r != onlyRegion)
                continue;
            for (uint32_t p = 0; p < version[r].size(); ++p)
                if (version[r][p] != kUntracked)
                    version[r][p] = _engine->VersionAt(index, r, p);
        }
    };
    // Move the tracked versions to engine index @p target (at or after the current one)
    auto moveTo = [&](size_t target) {
        if (at == target)
            return;
        if (at == SIZE_MAX || target < at || target - at > kResyncDistance)
        {
            at = target;
            resync(target, UINT32_MAX);
            atLine = _engine->ActiveLine();
            return;
        }
        for (size_t k = at + 1; k <= target; ++k)
        {
            ViewLineageIndex(k);
            if (!_engine->EnsureResident(k))
            {
                failed = true;
                return;
            }
            if (_engine->ActiveLine() != atLine)
            {
                // Another line of the lineage (a branch's fork): its own tables, looked up there
                at = k;
                resync(k, UINT32_MAX);
                atLine = _engine->ActiveLine();
                continue;
            }
            at = k;
            for (uint32_t r = 0; r < version.size(); ++r)
            {
                if (version[r].empty())
                    continue;
                if (_engine->HasFullTable(k, r))
                {
                    resync(k, r);   // a baseline or a full table: everything as the table says
                    continue;
                }
                if (_engine->ChangeCount(k, r) == 0)
                    continue;
                for (const auto& [piece, id] : _engine->ChangesAt(k, r))
                    if (piece < version[r].size() && version[r][piece] != kUntracked)
                        version[r][piece] = id;
            }
        }
    };
    // The bytes of region @p r from @p offset, @p length of them, at the current index, into @p out
    auto readRegion = [&](uint32_t r, size_t offset, size_t length, uint8_t* out) -> bool {
        while (length > 0)
        {
            const uint32_t p = static_cast<uint32_t>(offset / kTTDPieceSize);
            const size_t inPiece = offset % kTTDPieceSize;
            const size_t n = std::min(length, kTTDPieceSize - inPiece);
            const uint32_t id = track(r, p);
            if (id == TimeTravelEngine::kAbsent)
            {
                result.notRecorded = true;
                result.error = regions[r].name + " offset " + std::to_string(size_t(p) * kTTDPieceSize) +
                               ": the session had not recorded it by then";
                return false;
            }
            Decoded& d = decoded[r][p];
            if (d.id != id)
            {
                if (!_engine->PieceStore().Decode(id, d.bytes.data()))
                {
                    d.id = kUntracked;
                    result.error = regions[r].name + " offset " + std::to_string(size_t(p) * kTTDPieceSize) +
                                   ": the stored piece is damaged";
                    return false;
                }
                d.id = id;
                ++result.piecesDecoded;
            }
            std::memcpy(out, d.bytes.data() + inPiece, n);
            out += n;
            offset += n;
            length -= n;
        }
        return true;
    };

    const uint8_t* block = memory->BlockAt(0);
    const uint8_t* romStart = memory->ROMBase();
    const uint8_t* romEnd = romStart + size_t(MAX_ROM_PAGES) * PAGE_SIZE;
    // The four windows' sources at a checkpoint; false when the store cannot show them (a restore reads them)
    auto windowsOf = [&](const TTDCheckpoint& cp, std::array<WindowSource, 4>& out) -> bool {
        if (!cp.cpuWindows.valid)
            return false;
        for (uint8_t w = 0; w < 4; ++w)
        {
            const uint8_t* read = block + cp.cpuWindows.offset[w];
            WindowSource& src = out[w];
            src = WindowSource{};
            bool found = false;
            for (uint32_t r = 0; r < regions.size() && !found; ++r)
            {
                const TTDRegionDesc& desc = regions[r];
                if (!desc.memory || read < desc.memory || read + 0x4000 > desc.memory + desc.bytes)
                    continue;
                src.start = size_t(read - desc.memory);
                if (src.start % kTTDPieceSize != 0)
                    return false;
                src.region = r;
                found = true;
            }
            if (!found)
            {
                if (read < romStart || read + 0x4000 > romEnd)
                    return false;
                src.rom = true;   // the ROM is not recorded: it reads as it is (as memory-at does)
                src.romBytes = read;
            }
        }
        return true;
    };

    size_t rowBytes = 0;
    for (const TTDSeriesRange& r : query.ranges)
        rowBytes += r.length;

    const TTDSessionState stateBefore = _state;
    LiveStateSnapshot saved;
    bool machineSaved = false;
    const KnownMemoryScope knownMemory{*this};   // consecutive restores write only what changed
    std::vector<uint8_t> previous;
    size_t bytesOut = 0;
    std::array<WindowSource, 4> windows;
    FrameCursor frames(query);
    uint64_t frame = 0;
    while (!failed && frames.Next(frame))
    {
        const int64_t index = ViewLineageIndexAtOrBefore({frame, 0});
        const TTDCheckpoint* cp = index >= 0 ? ViewLineageCheckpoint(size_t(index)) : nullptr;
        if (!cp)
        {
            result.error = "frame " + std::to_string(frame) + " is before the history";
            failed = true;
            break;
        }
        const size_t cpIndex = size_t(index);
        const bool exact = cp->time.frame == frame;
        moveTo(cpIndex);
        if (failed)
        {
            result.error = "the checkpoint of frame " + std::to_string(frame) + ": its segment cannot be read back from its file";
            break;
        }

        TTDSeriesRow row;
        row.frame = frame;
        row.atFrame = cp->time.frame;
        row.atTinframe = static_cast<uint32_t>(CheckpointStartT(*cp) - GlobalT(cp->time));
        row.bytes.resize(rowBytes);
        uint8_t* out = row.bytes.data();
        bool cpuFromStore = false;
        bool cpuChecked = false;
        for (const TTDSeriesRange& range : query.ranges)
        {
            if (!range.cpu)
            {
                if (!readRegion(range.region, size_t(range.offset), range.length, out))
                {
                    failed = true;
                    break;
                }
                out += range.length;
                continue;
            }
            if (!cpuChecked)
            {
                const TTDCheckpoint* viewed = ViewLineageCheckpoint(cpIndex);
                cpuFromStore = viewed && exact && windowsOf(*viewed, windows);
                cpuChecked = true;
                if (!cpuFromStore)
                {
                    if (!restores)
                    {
                        result.error = "frame " + std::to_string(frame) + ": the CPU's mapping is not stored there";
                        failed = true;
                        break;
                    }
                    // The checkpoint restored: the machine at the frame's start, read through its own mapping
                    if (!machineSaved)
                    {
                        SaveLiveState(saved);
                        machineSaved = true;
                    }
                    ReplayModeScope replay(*this);
                    RestoreCheckpointForReplay(*viewed);
                    replay.Exit();
                    row.restored = true;
                    ++result.restoredFrames;
                }
            }
            if (!cpuFromStore)
            {
                for (uint32_t i = 0; i < range.length; ++i)
                    out[i] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(range.offset + i));
            }
            else
            {
                // Window by window: each run of addresses inside one window is one stretch of its source
                for (uint32_t i = 0; i < range.length;)
                {
                    const uint32_t addr = static_cast<uint32_t>(range.offset) + i;
                    const uint32_t inWindow = addr & 0x3FFF;
                    const uint32_t n = std::min<uint32_t>(range.length - i, 0x4000 - inWindow);
                    const WindowSource& src = windows[(addr >> 14) & 3];
                    if (src.rom)
                        std::memcpy(out + i, src.romBytes + inWindow, n);
                    else if (!readRegion(src.region, src.start + inWindow, n, out + i))
                    {
                        failed = true;
                        break;
                    }
                    i += n;
                }
            }
            if (failed)
                break;
            out += range.length;
        }
        if (failed)
            break;
        ++result.framesRead;

        // changes_only: a row only where a range differs from the frame read before (the first frame: always)
        if (query.changesOnly)
        {
            uint32_t changed = 0;
            size_t pos = 0;
            for (size_t r = 0; r < query.ranges.size(); ++r)
            {
                const size_t n = query.ranges[r].length;
                if (!previous.empty() && std::memcmp(previous.data() + pos, row.bytes.data() + pos, n) != 0)
                    changed |= r < 32 ? (1u << r) : 0x80000000u;
                pos += n;
            }
            const bool first = previous.empty();
            const bool differs = !first && previous != row.bytes;
            previous = row.bytes;
            if (!first && !differs)
                continue;
            row.changed = changed;
        }
        if (result.rows.size() >= query.maxRows || (!result.rows.empty() && bytesOut + rowBytes > query.maxBytes))
        {
            result.truncated = true;
            result.nextFrame = frame;
            break;
        }
        bytesOut += rowBytes;
        result.rows.push_back(std::move(row));
    }

    if (machineSaved)
    {
        ReplayModeScope restore(*this);
        RestoreLiveState(saved);
        restore.Exit();
        SetState(stateBefore);
    }
    result.ok = !failed;
    if (!result.ok)
        result.rows.clear();
    return result;
}

/// endregion </Memory series>

/// region <Screen at a frame>

TTDScreenAtResult TimeTravelController::ReadScreenAt(uint64_t frame)
{
    // Replays the frame and puts the machine back: an operation that changes it for a while
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    TTDScreenAtResult result;
    result.frame = frame;
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    Memory* memory = _context ? _context->pMemory : nullptr;
    Screen* screen = _context ? _context->pScreen : nullptr;
    if (_line->timeline.empty() || !memory || !screen || !_context->pEmulator || !_context->pCore)
    {
        result.error = "no recorded history";
        return result;
    }
    const LineageViewGuard lineageView{*this};
    ViewOwnerOfFrame(frame);
    const TTDCheckpoint* cp = LineageCheckpointAtFrame(frame);
    const TTDCheckpoint* next = LineageCheckpointAtFrame(frame + 1);
    if (!cp || !next)
    {
        const size_t endIndex = LineageEndIndex();
        const TTDCheckpoint* first = ViewLineageCheckpoint(LineageFirstIndex());
        const TTDCheckpoint* last = endIndex > LineageFirstIndex() ? ViewLineageCheckpoint(endIndex - 1) : nullptr;
        const uint64_t lo = first ? first->time.frame : 0;
        const uint64_t hi = last && last->time.frame > lo ? last->time.frame - 1 : lo;
        result.error = "frame " + std::to_string(frame) + " has not ended inside the history (the frames that ended: " +
                       std::to_string(lo) + ".." + std::to_string(hi) + ")";
        return result;
    }
    ViewOwnerOfFrame(frame);
    if (const std::optional<TTDExternalEvent> barrier = FirstBarrierBetween(cp->time, TTDTimePoint{frame, FrameSpan()}))
    {
        result.barrier = true;
        result.error = "a replay barrier (" + std::string(barrier->reason) + ") at frame " +
                       std::to_string(barrier->time.frame) + " cuts the frame";
        return result;
    }

    const TTDSessionState stateBefore = _state;
    LiveStateSnapshot saved;
    SaveLiveState(saved);

    // The frame's final picture, as positioning by frame number shows it (ComposeDisplay puts the machine back and
    // leaves only the pixels)
    ComposeDisplay(/*frameTarget=*/true, frame);
    auto picture = std::make_shared<FrameSnapshot>();
    const bool pictured = screen->SnapshotLive(*picture, /*emulationParked=*/true, /*timeoutMs=*/0);  // parked: direct copy

    // The frame replayed from its start: the screen shown at the start and where inside it the shown screen changed
    const MEM_MODEL model = _context->config.mem_model;
    auto shownPage = [&]() { return Screen::GetVideoRAMPage(model, _context->emulatorState.p7FFD); };
    result.shadowCapable = Screen::HasShadowScreen(model);
    {
        ReplayModeScope replay(*this);
        ViewOwnerOfFrame(frame);
        RestoreCheckpointForReplay(*LineageCheckpointAtFrame(frame));
        result.p7FFDAtStart = _context->emulatorState.p7FFD;
        result.pageAtStart = shownPage();
        result.shownAtStart = result.pageAtStart == 7 ? 1 : 0;
        result.border = _context->emulatorState.pFE & 0b0000'0111;
        uint16_t page = result.pageAtStart;
        const uint32_t span = FrameSpan();
        for (uint32_t k = 1; k < kScreenAtSamples && _context->emulatorState.frame_counter == frame; ++k)
        {
            RunToTInFrame(static_cast<uint32_t>(uint64_t(span) * k / kScreenAtSamples));
            if (_context->emulatorState.frame_counter != frame)
                break;
            const uint16_t now = shownPage();
            if (now != page)
            {
                result.flips.emplace_back(TInFrameNow(), now == 7 ? 1 : 0);
                page = now;
            }
        }
        // The frame's end is the next frame's checkpoint: the screens as memory-at reads them there
        ViewOwnerOfFrame(frame + 1);
        RestoreCheckpointForReplay(*LineageCheckpointAtFrame(frame + 1));
        result.p7FFDAtEnd = _context->emulatorState.p7FFD;
        result.pageAtEnd = shownPage();
        result.shownAtEnd = result.pageAtEnd == 7 ? 1 : 0;
        if (page != result.pageAtEnd)
            result.flips.emplace_back(FrameSpan(), result.shownAtEnd);   // a change after the last sample
        constexpr size_t kScreenBytes = 6912;
        if (const uint8_t* normal = memory->RAMPageAddress(5))
            result.normal.assign(normal, normal + kScreenBytes);
        if (result.shadowCapable)
            if (const uint8_t* shadow = memory->RAMPageAddress(7))
                result.shadow.assign(shadow, shadow + kScreenBytes);
        RestoreLiveState(saved);
        replay.Exit();
    }
    SetState(stateBefore);
    if (!pictured)
    {
        result.error = "the frame's picture could not be read";
        return result;
    }
    result.picture = std::move(picture);
    result.ok = true;
    return result;
}

/// endregion </Screen at a frame>

bool TimeTravelController::CheckpointMoment(uint64_t frame, TTDTimePoint& at)
{
    const SessionOperation read{*this, SessionOperation::Kind::Read};
    const TTDCheckpoint* cp = LineageCheckpointAtFrame(frame);
    if (!cp)
        return false;
    at = {frame, static_cast<uint32_t>(CheckpointStartT(*cp) - GlobalT(cp->time))};
    return true;
}

}  // namespace ttd
