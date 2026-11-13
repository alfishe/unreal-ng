/// @file ttdcontrollerbranches.cpp
/// @brief TimeTravelController: branches (docs/inprogress/2026-11-08-ttd-branches/architecture.md §4-5). A resume
/// in the past forks a branch that keeps the old future; the machine reads the line that owns the moment it stands
/// on (ViewBranch), running forward it follows the active lineage across forks (FollowLineageAtBoundary).

#include "stdafx.h"
#include "timetravelcontroller.h"

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "common/modulelogger.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/readoutschanged.h"

namespace ttd
{

namespace
{
uint64_t HostMicrosNow()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
}
}  // namespace

void TimeTravelController::ResetBranches()
{
    // The active line's state is what the session code that follows clears or replaces: it becomes the trunk
    if (_viewId != 0 && _viewId < _lines.size() && _lines[_viewId])
        std::swap(_lines[0], _lines[_viewId]);
    _lines.resize(1);
    _line = _lines.front().get();
    _branches.Reset(TTDNewGuid());
    _tip = 0;
    _viewId = 0;
    _lastVisited.assign(1, std::nullopt);
    _nextBranchMetaSet = false;
    _forceFork = false;
    PublishLineage();
}

uint32_t TimeTravelController::BranchHolding(const TTDTimePoint& time) const
{
    if (_lines.size() <= 1)
        return kTTDTrunk;
    return TTDBranchTable::OwnerOf(_branches.Lineage(_tip), {0, time.frame, time.tInFrame});
}

void TimeTravelController::WalkLineageBack(uint64_t beforeT,
                                           const std::function<bool(uint64_t lineStartT, uint64_t upToT)>& onLine)
{
    if (_lines.size() <= 1)
    {
        onLine(0, beforeT);
        return;
    }
    const uint32_t first = OwnerOnLineage(TimePointAt(beforeT).frame);
    if (first != _viewId)
        ViewBranch(first);
    uint64_t upTo = beforeT;
    for (;;)
    {
        const TTDBranchInfo* branch = _branches.Get(_viewId);
        const bool trunk = _viewId == kTTDTrunk || !branch;
        const uint64_t lineStart = trunk || _line->timeline.empty() ? 0 : GlobalT(_line->timeline.front().time);
        if (!onLine(lineStart, upTo) || trunk)
            break;
        upTo = lineStart;
        ViewBranch(branch->parent);
    }
    ViewOwnerOfPosition();
}

const TTDFrameTable& TimeTravelController::LineageFrames(uint64_t frame) const
{
    if (_lines.size() <= 1)
        return _engine->Frames();
    const TTDFrameTable* frames = _engine->LineFrames(OwnerOnLineage(frame));
    return frames && !frames->Empty() ? *frames : _engine->Frames();
}

const TTDFrameTable& TimeTravelController::LineageFramesAtT(uint64_t t) const
{
    if (_lines.size() <= 1)
        return _engine->Frames();
    // The deepest lineage line whose history starts at or before t
    for (uint32_t id = _tip;; id = _engine->LineParent(id))
    {
        const TTDFrameTable* frames = _engine->LineFrames(id);
        TTDMachineTime start = 0;
        if (id == kTTDTrunk || !frames)
            return frames && !frames->Empty() ? *frames : _engine->Frames();
        if (!frames->Empty() && frames->Start(frames->FirstFrame(), start) && start <= t)
            return *frames;
    }
}

void TimeTravelController::ViewLineageIndex(size_t index)
{
    if (_lines.size() <= 1)
        return;
    uint32_t id = _tip;
    while (id != kTTDTrunk && index < _engine->LineFirstCheckpoint(id))
        id = _engine->LineParent(id);
    if (id != _viewId)
        ViewBranch(id);
}

const TTDCheckpoint* TimeTravelController::ViewLineageCheckpoint(size_t index)
{
    if (index < LineageFirstIndex())
        return nullptr;
    ViewLineageIndex(index);
    const size_t i = index - _engine->LineFirstCheckpoint(_viewId);
    return i < _line->timeline.size() ? &_line->timeline[i] : nullptr;
}

int64_t TimeTravelController::ViewLineageIndexAtOrBefore(const TTDTimePoint& t)
{
    if (_lines.size() > 1)
    {
        const uint32_t owner = OwnerOnLineage(t.frame);
        if (owner != _viewId)
            ViewBranch(owner);
    }
    const int64_t local = TimelineIndexAtOrBefore(t);
    return local < 0 ? -1 : local + static_cast<int64_t>(_engine->LineFirstCheckpoint(_viewId));
}

void TimeTravelController::ViewOwnerOfPosition()
{
    if (_lines.size() <= 1)
        return;
    const uint32_t owner = OwnerOnLineage(CurrentPosition().frame);
    if (owner != _viewId)
        ViewBranch(owner);
}

const TTDCheckpoint* TimeTravelController::LineageCheckpointAtFrame(uint64_t frame) const
{
    const ControllerLine& line = _lines.size() <= 1 ? *_line : *_lines[OwnerOnLineage(frame)];
    const auto it = std::lower_bound(line.timeline.begin(), line.timeline.end(), frame,
                                     [](const TTDCheckpoint& cp, uint64_t f) { return cp.time.frame < f; });
    return it != line.timeline.end() && it->time.frame == frame ? &*it : nullptr;
}

const TTDCoverageIndex& TimeTravelController::CoverageOfFrame(uint64_t frame) const
{
    return _lines.size() <= 1 ? _line->coverageIndex : _lines[OwnerOnLineage(frame)]->coverageIndex;
}

bool TimeTravelController::LineageHasCoverage(TTDCoverageKind kind) const
{
    if (_lines.size() <= 1)
        return _line->coverageIndex.SealedFrameCount(kind) > 0;
    for (uint32_t id = _tip;; id = _engine->LineParent(id))
    {
        if (_lines[id] && _lines[id]->coverageIndex.SealedFrameCount(kind) > 0)
            return true;
        if (id == kTTDTrunk || !_engine->HasLine(id))
            return false;
    }
}

bool TimeTravelController::LineageCoveredRange(TTDCoverageKind kind, uint64_t& first, uint64_t& last) const
{
    if (_lines.size() <= 1)
        return _line->coverageIndex.CoveredRange(kind, first, last);
    bool any = false;
    uint64_t upTo = UINT64_MAX;   // a parent's frames from its child's fork on are not the lineage's
    for (uint32_t id = _tip;; id = _engine->LineParent(id))
    {
        uint64_t f = 0, l = 0;
        if (_lines[id] && _lines[id]->coverageIndex.CoveredRange(kind, f, l))
        {
            l = std::min(l, upTo);
            if (f <= l && upTo != 0)
            {
                first = any ? std::min(first, f) : f;
                last = any ? std::max(last, l) : l;
                any = true;
            }
        }
        if (id == kTTDTrunk || !_engine->HasLine(id))
            return any;
        const uint64_t fork = _engine->LineFork(id).frame;
        upTo = fork == 0 ? 0 : fork - 1;
    }
}

void TimeTravelController::ViewOwnerOfFrame(uint64_t frame)
{
    if (_lines.size() <= 1)
        return;
    const uint32_t owner = OwnerOnLineage(frame);
    if (owner != _viewId)
        ViewBranch(owner);
}

std::vector<TTDInputEvent> TimeTravelController::LineageInputEvents() const
{
    if (_lines.size() <= 1)
        return _line->inputJournal.Events();
    std::vector<uint32_t> chain;
    for (uint32_t id = _tip;; id = _engine->LineParent(id))
    {
        chain.push_back(id);
        if (id == kTTDTrunk || !_engine->HasLine(id))
            break;
    }
    std::reverse(chain.begin(), chain.end());
    std::vector<TTDInputEvent> out;
    for (size_t k = 0; k < chain.size(); ++k)
    {
        const ControllerLine& line = *_lines[chain[k]];
        const bool last = k + 1 == chain.size();
        const TTDTimePoint until = last ? TTDTimePoint{UINT64_MAX, 0} : _lines[chain[k + 1]]->timeline.front().time;
        for (const TTDInputEvent& e : line.inputJournal.Events())
            if ((last || e.time < until) && (k == 0 || !(e.time < line.timeline.front().time)))
                out.push_back(e);
    }
    return out;
}

void TimeTravelController::PublishLineage()
{
    std::vector<TTDLineageSpan> lineage;
    if (_tip != kTTDTrunk)
        lineage = _branches.Lineage(_tip);
    const std::lock_guard<std::mutex> lock(_lineageViewLock);
    _lineageView = std::move(lineage);
}

bool TimeTravelController::OnActiveLineage(const TTDBookmark& bookmark) const
{
    const std::lock_guard<std::mutex> lock(_lineageViewLock);
    if (_lineageView.empty())
        return bookmark.branch == kTTDTrunk;
    return TTDBranchTable::OwnerOf(_lineageView, {0, bookmark.time.frame, bookmark.time.tInFrame}) == bookmark.branch;
}

uint32_t TimeTravelController::OwnerOnLineage(uint64_t frame) const
{
    uint32_t id = _tip;
    while (id != kTTDTrunk && frame < _engine->LineFork(id).frame)
        id = _engine->LineParent(id);
    return id;
}

size_t TimeTravelController::LineageCheckpointCount() const
{
    return _engine->LineFirstCheckpoint(_tip) - _engine->LineFirstCheckpoint(kTTDTrunk) + _lines[_tip]->timeline.size();
}

bool TimeTravelController::HasHistoryAfter(const TTDTimePoint& cut) const
{
    if (_viewId != _tip)
        return true;
    if (!_line->timeline.empty() && _line->timeline.back().time.frame > cut.frame)
        return true;
    const std::vector<TTDInputEvent>& inputs = _line->inputJournal.Events();
    if (!inputs.empty() && cut < inputs.back().time)
        return true;
    const std::vector<TTDExternalEvent>& markers = _line->externalEvents.Events();
    return !markers.empty() && cut < markers.back().time;
}

void TimeTravelController::ViewBranch(uint32_t id)
{
    if (id == _viewId || id >= _lines.size() || !_lines[id])
        return;
    _engine->ViewLine(id);
    _line = _lines[id].get();
    _viewId = id;
    // Every record up to a line's end is in its engine line already: the shadow cursors stand at the ends
    _shadowEvents.input = _line->inputJournal.Size();
    _shadowEvents.external = _line->externalEvents.Size();
    SyncMediaReadJournal();
    SyncPortJournalHook();
}

bool TimeTravelController::FollowLineageAtBoundary(uint64_t frame)
{
    if (_viewId == _tip)
        return false;
    // The child of the viewed line on the active lineage, and where it forks
    uint32_t next = _tip;
    while (next != kTTDTrunk && _engine->LineParent(next) != _viewId)
        next = _engine->LineParent(next);
    if (next == kTTDTrunk || frame != _engine->LineFork(next).frame)
        return false;
    // At the boundary of the fork frame the machine equals the fork checkpoint on both lines: the replay goes on
    // with the branch's records from there (the same state restored, every journal armed at its start)
    ViewBranch(next);
    RestoreCheckpointForReplay(_line->timeline.front());
    return true;
}

bool TimeTravelController::ForkBranchAt(const TTDTimePoint& from, const TTDTimePoint& cut)
{
    const int64_t keep = TimelineIndexAtOrBefore(from);
    if (keep < 0)
        return false;
    const TTDCheckpoint& kept = _line->timeline[static_cast<size_t>(keep)];
    const size_t keepEngine = _engine->FirstCheckpoint() + static_cast<size_t>(keep);
    const TTDTimePoint forkAt = cut.frame == kept.time.frame ? cut : TTDTimePoint{kept.time.frame, 0};
    const uint32_t parent = _viewId;

    TTDBranchMeta meta = _nextBranchMetaSet ? std::move(_nextBranchMeta) : TTDBranchMeta{};
    _nextBranchMetaSet = false;
    if (meta.reason.empty())
        meta.reason = "resume";
    if (meta.createdBy.empty())
        meta.createdBy = "user";
    // The default name says where the branch leaves: the time from the trunk's start, "+00:01:25.150" (the
    // owner's choice 2026-11-10; a counter like "resume 3" told nothing). Two forks in one millisecond get " (2)"
    if (meta.name.empty())
    {
        const std::vector<TTDCheckpoint>& trunk = _lines.front()->timeline;
        const uint64_t start = trunk.empty() ? forkAt.frame : trunk.front().time.frame;
        const uint64_t frames = forkAt.frame >= start ? forkAt.frame - start : 0;
        const EmulatorState* state = _context ? &_context->emulatorState : nullptr;
        const uint64_t hz = state && state->base_z80_frequency ? state->base_z80_frequency : 3'500'000u;
        const uint64_t frameT = _context ? _context->config.frame : 71'680u;
        const uint64_t inFrame = state ? state->CpuToBaseT(forkAt.tInFrame) : forkAt.tInFrame;
        const uint64_t ms = (frames * frameT + inFrame) * 1000u / hz;   // integers: one name on every platform
        char text[32];
        std::snprintf(text, sizeof(text), "+%02llu:%02llu:%02llu.%03llu",
                      static_cast<unsigned long long>(ms / 3'600'000u),
                      static_cast<unsigned long long>(ms / 60'000u % 60u),
                      static_cast<unsigned long long>(ms / 1000u % 60u), static_cast<unsigned long long>(ms % 1000u));
        meta.name = text;
    }
    meta.createdHostMicros = HostMicrosNow();

    // The table first: it refuses a fork it cannot hold (the limit) before the engine is touched
    std::string error;
    TTDBranchTable table = _branches;
    const TTDTimePoint tipEnd = TimelineEnd();
    table.SetEnd(_tip, {_tip, tipEnd.frame, tipEnd.tInFrame});
    const std::optional<uint32_t> id =
        table.Create(parent, {parent, forkAt.frame, forkAt.tInFrame}, TTDNewGuid(), meta, error);
    if (!id)
    {
        MLOGWARNING("TimeTravelController::ForkBranchAt — no branch: %s", error.c_str());
        return false;
    }

    // The checkpoint before the fork's is the parent's (the next capture measures its frame from it)
    const TTDEngineCheckpoint* prev = keepEngine > _engine->FirstCheckpoint() ? _engine->Checkpoint(keepEngine - 1) : nullptr;
    const uint64_t prevStart = prev ? prev->start : 0;
    const bool hadPrev = prev != nullptr;

    // The recording's folder holds the history it wrote, complete: the open file gets the rest and its index on the
    // line it was written from; the new branch's files start at the next capture (storage.md §4.2)
    FinishLineFiles();
    uint32_t line = 0;
    if (!_engine->ForkAt(keepEngine, {0, forkAt.frame, forkAt.tInFrame}, line, error))
    {
        MLOGWARNING("TimeTravelController::ForkBranchAt — the engine cannot fork: %s", error.c_str());
        return false;
    }
    if (line != *id)
        MLOGWARNING("TimeTravelController::ForkBranchAt — engine line %u for branch %u", line, *id);
    _branches = std::move(table);
    if (_shadowFolder && !_shadowFilesStopped)
    {
        // The folder's index has the branch before its first file: a crash between leaves a branch without files
        std::vector<uint8_t> record = BranchRecordBytes(line);
        _shadowParams.lineMeta[line] = record;
        if (!_shadowFolder->NoteBranchCreated(
                TTDBranchCreateRecord(line, parent, forkAt.frame, forkAt.tInFrame, record), &error))
            MLOGWARNING("TimeTravelController::ForkBranchAt — the folder's branch index: %s", error.c_str());
    }

    // The controller's line: the fork checkpoint and the fork frame's records up to the cut
    auto fresh = std::make_unique<ControllerLine>();
    fresh->timeline.push_back(kept);
    _blobBytes += BlobBytes(kept);
    const TTDTimePoint after{forkAt.frame, forkAt.tInFrame + 1};
    fresh->inputJournal.Assign(_line->inputJournal.Events(), _line->inputJournal.NetInputs(),
                               _line->inputJournal.Payload());
    fresh->inputJournal.DropBefore(kept.time);
    fresh->inputJournal.DropAfter(forkAt);
    std::unordered_map<size_t, size_t> markers;   // parent marker index -> the new line's
    const std::vector<TTDExternalEvent> events = _line->externalEvents.SnapshotEvents();
    for (size_t k = 0; k < events.size(); ++k)
        if (!(events[k].time < kept.time) && events[k].time < after)
        {
            markers[k] = fresh->externalEvents.Size();
            fresh->externalEvents.Record(events[k]);
        }
    for (const auto& [index, bytes] : _line->toolEditPayloads)
        if (auto it = markers.find(index); it != markers.end())
            fresh->toolEditPayloads[it->second] = bytes;
    // Coverage starts at the fork; its frame is a hole unless the fork lies on the checkpoint itself
    const bool atCheckpoint = _seekLandedOnCheckpoint && forkAt.frame == kept.time.frame;
    fresh->coverageIndex.DropFramesFrom(forkAt.frame, !atCheckpoint);
    if (_enableWriteJournal)
        fresh->writeJournal = std::make_unique<TTDWriteJournal>(_writeJournalBytes, true);
    fresh->forkInputs = fresh->inputJournal.Size();
    fresh->forkMarkers = fresh->externalEvents.Size();

    if (_lines.size() <= line)
        _lines.resize(line + 1);
    _lines[line] = std::move(fresh);
    _lastVisited.resize(_lines.size());
    _lastVisited[parent] = from;
    _line = _lines[line].get();
    _viewId = line;
    _tip = line;
    PublishLineage();
    if (const TTDBranchInfo* created = _branches.Get(line))
        NoteBranchEvent("created", *created);

    // The engine's cursors into the controller's journals, at the new line's ends
    _shadowEvents.input = _line->inputJournal.Size();
    _shadowEvents.external = _line->externalEvents.Size();
    for (auto it = _shadowFacts.begin(); it != _shadowFacts.end();)
    {
        if (cut < it->at)
        {
            _engine->Payloads().Release(it->ev.payload);
            it = _shadowFacts.erase(it);
        }
        else
            ++it;
    }
    _shadowEvents.facts = 0;
    _engine->Writes().SetSegments(JournalSegments());
    const TTDEngineCheckpoint* cp = _engine->Checkpoint(keepEngine);
    _shadowLastLength = hadPrev ? cp->start - prevStart : 0;
    _shadowLastStart = cp->start;
    _shadowLastBase = cp->chipset.t_states;
    _shadowMediaKnown = false;
    if (!_shadowArmed)
    {
        _shadowDeviceRegions.clear();
        _peripherals.CollectRegions(_shadowDeviceRegions);
        ArmShadowRegions(true);
        _shadowRescan = true;
        _shadowRomSignature = ComputeRomSignature();
    }
    NoteTimelineEnd();
    MLOGINFO("TimeTravelController — branch %u '%s' forks from %u at (frame=%llu, tInFrame=%u)", line,
             _branches.Get(line)->meta.name.c_str(), parent, static_cast<unsigned long long>(forkAt.frame),
             static_cast<unsigned>(forkAt.tInFrame));
    return true;
}

bool TimeTravelController::SwitchBranch(uint32_t branch, const std::optional<TTDTimePoint>& at, std::string& error)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    // Leaving an active branch that recorded nothing since its fork removes it (§4.6), before the table is read
    if (branch != _tip && _branches.Get(branch))
        DropEmptyTip(TTDTimePoint{UINT64_MAX, 0});
    const TTDBranchInfo* info = _branches.Get(branch);
    if (!info || branch >= _lines.size() || !_lines[branch])
    {
        error = "branch " + std::to_string(branch) + " does not exist";
        return false;
    }
    if (_line->timeline.empty())
    {
        error = "no history";
        return false;
    }
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    // The active branch's end as it stands (it grew while it recorded)
    const TTDTimePoint tipEnd = TimelineEnd();
    _branches.SetEnd(_tip, {_tip, tipEnd.frame, tipEnd.tInFrame});
    const TTDTimePoint branchEnd{info->end.frame, static_cast<uint32_t>(info->end.tInFrame)};
    TTDTimePoint target = branchEnd;
    // The last visited moment, at most the end: a seek to the end's frame start stops on the first instruction
    // boundary after it, a few T past an end that lies on that start
    if (branch < _lastVisited.size() && _lastVisited[branch] && *_lastVisited[branch] < branchEnd)
        target = *_lastVisited[branch];
    if (at)
        target = *at;
    // The moment must lie on the branch's lineage
    if (branchEnd < target)
    {
        error = "the moment " + std::to_string(target.frame) + ":" + std::to_string(target.tInFrame) +
                " lies after the branch's end " + std::to_string(branchEnd.frame) + ":" +
                std::to_string(branchEnd.tInFrame);
        return false;
    }
    if (_viewId < _lastVisited.size())
        _lastVisited[_viewId] = CurrentPosition();
    // The open file ends on the branch it was written from; the target's recording opens its next one
    if (branch != _tip)
        FinishLineFiles();
    const uint32_t oldTip = _tip;
    const TTDTimePoint oldPausedEnd = _pausedEnd;
    const TTDTimePoint oldStoppedEnd = _stoppedEnd;
    const bool oldStoppedValid = _stoppedEndValid;
    // The end belongs to the branch now: running to it continues its recording (a paused one), or stops there
    _tip = branch;
    PublishLineage();
    _pausedEnd = branchEnd;
    _stoppedEnd = branchEnd;
    _stoppedEndValid = true;
    if (!SeekToInternal(target, nullptr))
    {
        _tip = oldTip;
        PublishLineage();
        _pausedEnd = oldPausedEnd;
        _stoppedEnd = oldStoppedEnd;
        _stoppedEndValid = oldStoppedValid;
        ViewBranch(OwnerOnLineage(CurrentPosition().frame));
        error = "the moment cannot be reached";
        return false;
    }
    _lastVisited.resize(_lines.size());
    _lastVisited[branch] = target;
    if (const TTDBranchInfo* switched = _branches.Get(branch))
        NoteBranchEvent("switched", *switched);
    return true;
}

bool TimeTravelController::DeleteBranch(uint32_t branch, bool force, std::vector<uint32_t>& removed,
                                        std::string& error)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    std::vector<uint32_t> protect;
    for (const TTDLineageSpan& s : _branches.Lineage(_tip))
        protect.push_back(s.branch);
    const TTDBranchInfo* asked = _branches.Get(branch);
    const TTDBranchInfo before = asked ? *asked : TTDBranchInfo{};
    if (!_branches.Delete(branch, protect, force, removed, error))
        return false;
    ReleaseBranchLines(removed);
    NoteBranchEvent("deleted", before, static_cast<uint32_t>(removed.size()));
    error.clear();
    return true;
}

bool TimeTravelController::UpdateBranch(uint32_t branch, const TTDBranchUpdate& update, std::string& error)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    if (!_branches.Get(branch))
    {
        error = "branch " + std::to_string(branch) + " does not exist";
        return false;
    }
    if (update.name)
    {
        if (update.name->empty())
        {
            error = "a branch name cannot be empty";
            return false;
        }
        if (!_branches.Rename(branch, *update.name, error))
            return false;
    }
    if (update.note)
        _branches.SetNote(branch, *update.note);
    if (update.tags || !update.addTags.empty() || !update.removeTags.empty())
        _branches.UpdateTags(branch, update.tags ? &*update.tags : nullptr, update.addTags, update.removeTags);
    if (update.pinned)
        _branches.SetPinned(branch, *update.pinned);
    if (const TTDBranchInfo* updated = _branches.Get(branch))
        NoteBranchEvent("updated", *updated);
    error.clear();
    return true;
}

void TimeTravelController::ReleaseBranchLines(const std::vector<uint32_t>& removed)
{
    std::string error;
    // Their files: the delete records first, then the files (a removed line's open file is abandoned)
    if (_shadowWriter && std::find(removed.begin(), removed.end(), _shadowWriterLine) != removed.end())
        _shadowWriter.reset();
    if (_shadowFolder && !removed.empty())
    {
        if (!_shadowFolder->DeleteLines(removed, &error))
            MLOGWARNING("TimeTravelController::DeleteBranch — the folder keeps the files: %s", error.c_str());
        for (uint32_t id : removed)
        {
            _shadowLinesWithFiles.erase(id);
            _shadowParams.lineMeta.erase(id);
        }
        error.clear();
    }
    for (uint32_t id : removed)   // children before their parents (TTDBranchTable::Subtree)
    {
        if (id < _lines.size() && _lines[id])
        {
            for (const TTDCheckpoint& cp : _lines[id]->timeline)
                _blobBytes -= BlobBytes(cp);
            _lines[id].reset();
        }
        if (_engine->HasLine(id) && !_engine->ReleaseLine(id, error))
            MLOGWARNING("TimeTravelController::DeleteBranch — engine line %u: %s", id, error.c_str());
        if (id < _lastVisited.size())
            _lastVisited[id].reset();
    }
    if (_bookmarks.RemoveOnBranches(removed) != 0)
        NotifyReadoutsChanged(_context, "ttd.bookmarks");
}

bool TimeTravelController::TipIsEmpty() const
{
    if (_tip == kTTDTrunk || _tip >= _lines.size() || !_lines[_tip])
        return false;
    const TTDBranchInfo* info = _branches.Get(_tip);
    const ControllerLine& line = *_lines[_tip];
    // A branch asked for by name (branch-create) or pinned stays: somebody wants it, recorded or not
    if (info && (info->meta.reason == "create" || info->meta.reason.rfind("create:", 0) == 0 || info->meta.pinned))
        return false;
    if (!info || !info->children.empty() || line.timeline.size() != 1 || line.inputJournal.Size() != line.forkInputs ||
        line.externalEvents.Size() != line.forkMarkers)
        return false;
    const TTDTimePoint fork{info->fork.frame, static_cast<uint32_t>(info->fork.tInFrame)};
    const TTDTimePoint end = _state == TTDSessionState::Recording && !_recordingPaused ? CurrentPosition() : TimelineEnd();
    return !(fork < end);
}

bool TimeTravelController::DropEmptyTip(const TTDTimePoint& leavingTo)
{
    if (!TipIsEmpty())
        return false;
    const uint32_t empty = _tip;
    const TTDBranchInfo* info = _branches.Get(empty);
    const TTDTimePoint fork{info->fork.frame, static_cast<uint32_t>(info->fork.tInFrame)};
    if (leavingTo == fork)
        return false;   // staying where it forks: the branch may still record
    const uint32_t parent = info->parent;
    const TTDBranchInfo* parentInfo = _branches.Get(parent);
    if (!parentInfo)
        return false;
    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    const TTDTimePoint parentEnd{parentInfo->end.frame, static_cast<uint32_t>(parentInfo->end.tInFrame)};
    std::vector<uint32_t> protect;
    for (const TTDLineageSpan& s : _branches.Lineage(parent))
        protect.push_back(s.branch);
    const TTDBranchInfo before = *info;
    std::vector<uint32_t> removed;
    std::string error;
    if (!_branches.Delete(empty, protect, /*force=*/true, removed, error))
    {
        MLOGWARNING("TimeTravelController — the empty branch %u stays: %s", empty, error.c_str());
        return false;
    }
    // The parent is active again with its end: running to it continues its recording (a paused one)
    _tip = parent;
    PublishLineage();
    ViewBranch(parent);
    _pausedEnd = parentEnd;
    _stoppedEnd = parentEnd;
    _stoppedEndValid = true;
    ReleaseBranchLines(removed);
    NoteBranchEvent("deleted", before, static_cast<uint32_t>(removed.size()));
    MLOGINFO("TimeTravelController — the empty branch %u went (nothing recorded after its fork)", empty);
    return true;
}

void TimeTravelController::NoteBranchEvent(const char* event, const TTDBranchInfo& branch, uint32_t removed)
{
    ++_branchEvent.seq;
    _branchEvent.event = event;
    _branchEvent.branch = branch.id;
    _branchEvent.guid = branch.guid;
    _branchEvent.name = branch.meta.name;
    _branchEvent.removed = removed;
}

TTDBranchesPublished TimeTravelController::PublishedBranches() const
{
    TTDBranchesPublished out;
    std::lock_guard<std::mutex> lock(_publishedMutex);
    out.view = _published.branches;
    out.active = _published.activeBranch;
    out.viewed = _published.viewedBranch;
    out.event = _published.branchEvent;
    return out;
}

}  // namespace ttd
