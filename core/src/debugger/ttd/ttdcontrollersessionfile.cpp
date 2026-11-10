/// @file ttdcontrollersessionfile.cpp
/// @brief TimeTravelController's session files (Phase 5, C4b): the engine's
/// session file format (engine/ttdsessionfile.h) with the controller's holder
/// streams (ttdsessionfacts.h). A v1 .ttd file is refused with a reason: its
/// reader went with v1 (Phase 6), and before the release no file format is
/// kept compatible.

#include <algorithm>
#include <chrono>
#include <functional>
#include <cstring>
#include <iterator>
#include <random>
#include <sstream>
#include <unordered_map>

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "debugger/ttd/engine/ttdbytes.h"
#include "debugger/ttd/engine/ttdcontainer.h"
#include "debugger/ttd/engine/ttdsessionfile.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/ttddirtytracker.h"
#include "debugger/ttd/ttddumpformat.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/ttdsessionfacts.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/slots/slotmanager.h"

namespace ttd
{

namespace
{
/// The engine's writer appends to an ITTDByteSink: a std::ostream here
class OStreamSink : public ITTDByteSink
{
public:
    explicit OStreamSink(std::ostream& out) : _out(out) {}
    bool Write(const uint8_t* data, size_t size) override
    {
        _out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        _size += size;
        return static_cast<bool>(_out);
    }
    bool Sync() override
    {
        _out.flush();
        return static_cast<bool>(_out);
    }
    uint64_t Size() const override { return _size; }

private:
    std::ostream& _out;
    uint64_t _size = 0;
};

/// A whole stream as a byte source (the container reader seeks)
bool ReadAll(std::istream& in, std::vector<uint8_t>& bytes, std::string& err)
{
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad())
    {
        err = "stream read failed";
        return false;
    }
    return true;
}

/// Both formats start with "TTDD"; the schema after it tells them apart (v1: 1 or 3, the container: 2)
bool IsV1File(const std::vector<uint8_t>& bytes)
{
    return bytes.size() >= 6 && std::memcmp(bytes.data(), dump::kMagic, 4) == 0 &&
           dump::IsReadableSchema(static_cast<uint16_t>(bytes[4] | (bytes[5] << 8)));
}

constexpr const char* kV1Refused =
    "a v1 .ttd file: this build reads session files in the engine's format only (record the session again)";

/// A branch's record in a file (the branch table's create, TTD branches storage.md §3.1): identity, metadata, end
constexpr uint8_t kBranchRecordVersion = 1;

void WriteBranchRecord(TTDByteWriter& w, const TTDBranchInfo& b)
{
    w.U8(kBranchRecordVersion);
    w.Raw(b.guid.data(), b.guid.size());
    w.Str(b.meta.name);
    w.U64(b.meta.createdHostMicros);
    w.Str(b.meta.createdBy);
    w.Str(b.meta.run);
    w.Str(b.meta.reason);
    w.Varint(b.meta.tags.size());
    for (const std::string& tag : b.meta.tags)
        w.Str(tag);
    w.Str(b.meta.note);
    w.U8(b.meta.pinned ? 1 : 0);
    w.Varint(b.end.frame);
    w.Varint(b.end.tInFrame);
}

bool ReadBranchRecord(TTDByteReader& r, TTDGuid& guid, TTDBranchMeta& meta, TTDPosition& end)
{
    uint8_t version = 0, pinned = 0;
    uint64_t tags = 0;
    if (!r.U8(version) || version != kBranchRecordVersion || !r.Raw(guid.data(), guid.size()) || !r.Str(meta.name) ||
        !r.U64(meta.createdHostMicros) || !r.Str(meta.createdBy) || !r.Str(meta.run) || !r.Str(meta.reason) ||
        !r.Varint(tags) || tags > r.Left())
        return false;
    meta.tags.resize(static_cast<size_t>(tags));
    for (std::string& tag : meta.tags)
        if (!r.Str(tag))
            return false;
    if (!r.Str(meta.note) || !r.U8(pinned) || !r.Varint(end.frame) || !r.Varint(end.tInFrame))
        return false;
    meta.pinned = pinned != 0;
    return true;
}
}  // namespace

std::vector<TTDHolderStream> TimeTravelController::SessionHolderStreams(bool declareAll) const
{
    TTDSessionFacts facts;
    facts.modelId = _loadedFromFile ? _sessionModelId
                                    : static_cast<uint8_t>(_context ? _context->config.mem_model : 0);
    facts.modelRamPages = static_cast<uint16_t>(_modelRamPages);
    facts.romSignature = _loadedFromFile ? _loadedRomSignature : _liveRomSignature;
    facts.capturedAtUnixMs = _loadedFromFile ? _capturedAtUnixMs
                                             : static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                                         std::chrono::system_clock::now().time_since_epoch())
                                                                         .count());
    facts.recordedBy = _loadedFromFile ? _loadedRecordedBy
                                       : (_context && _context->pEmulator ? _context->pEmulator->GetId() : std::string());
    facts.notRecorded = NotRecorded();
    facts.inputHistoryComplete = _inputHistoryComplete;
    facts.portJournalValid = _portJournalValid;
    facts.portJournalOffReason = _portJournalOffReason;

    std::vector<TTDHolderStream> streams;
    streams.push_back({holderstream::kFacts, "controller-facts", EncodeSessionFacts(facts)});
    // The trunk's coverage (each branch's goes with its own records)
    std::vector<uint8_t> coverageBytes = CoverageBytes(*_lines.front());
    if (!coverageBytes.empty() || declareAll)
        streams.push_back({holderstream::kCoverage, "coverage", std::move(coverageBytes)});
    streams.push_back({holderstream::kBookmarks, "bookmarks", EncodeBookmarks(_bookmarks)});
    return streams;
}

bool TimeTravelController::SerializeSession(std::ostream& out, std::string& err, bool lineageOnly)
{
    const SessionOperation op{*this, SessionOperation::Kind::Read};
    if (_line->timeline.empty() || !_engine->IsSessionOpen())
    {
        err = "no session to save";
        return false;
    }
    // While recording, the current frame's journals go in too
    if (_state == TTDSessionState::Recording)
        FlushToEngine();

    TTDSessionSaveParams params;
    params.createdMicros = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
    std::random_device random;
    for (uint8_t& b : params.uuid)
        b = static_cast<uint8_t>(random());
    params.holderStreams = SessionHolderStreams(/*declareAll=*/false);
    if (_branches.HasBranches() && lineageOnly)
        AddLineageToSave(params);
    else if (_branches.HasBranches())
        AddBranchesToSave(params);

    OStreamSink sink(out);
    return TTDSessionFile::Save(*_engine, sink, err, params);
}

void TimeTravelController::AddLineageToSave(TTDSessionSaveParams& params)
{
    params.allLines = false;
    params.lineageOf = _tip;
    // The lineage's bookmarks: each on a branch of it, before the next branch's fork (or on the tip)
    std::vector<uint32_t> chain;
    for (uint32_t b = _tip;;)
    {
        const TTDBranchInfo* info = _branches.Get(b);
        if (!info)
            break;
        chain.insert(chain.begin(), b);
        if (b == kTTDTrunk)
            break;
        b = info->parent;
    }
    TTDBookmarkJournal lineage;
    for (TTDBookmark mark : _bookmarks.Snapshot())
    {
        const auto at = std::find(chain.begin(), chain.end(), mark.branch);
        if (at == chain.end())
            continue;
        if (at + 1 != chain.end())
        {
            const TTDPosition fork = _branches.Get(*(at + 1))->fork;
            if (TTDTimePoint{fork.frame, static_cast<uint32_t>(fork.tInFrame)} < mark.time)
                continue;
        }
        mark.branch = kTTDTrunk;
        lineage.Add(mark);
    }
    std::vector<TTDHolderStream> streams;
    for (TTDHolderStream& h : params.holderStreams)
        if (h.id == holderstream::kBookmarks)
            streams.push_back({h.id, h.name, EncodeBookmarks(lineage)});
        else if (h.id != holderstream::kCoverage || _tip == kTTDTrunk)
            streams.push_back(std::move(h));
    params.holderStreams = std::move(streams);
}

std::vector<uint8_t> TimeTravelController::CoverageBytes(const ControllerLine& line) const
{
    std::vector<uint8_t> bytes;
    if (line.coverageIndex.SealedFrameCount(TTDCoverageKind::Executed) > 0 ||
        line.coverageIndex.SealedFrameCount(TTDCoverageKind::Written) > 0 ||
        line.coverageIndex.SealedFrameCount(TTDCoverageKind::Read) > 0)
    {
        std::ostringstream coverage;
        if (line.coverageIndex.Serialize(coverage))
        {
            const std::string text = coverage.str();
            bytes.assign(text.begin(), text.end());
        }
    }
    return bytes;
}

void TimeTravelController::AddBranchesToSave(TTDSessionSaveParams& params)
{
    // The file numbers the live lines densely in id order (TTDSessionWriter::WriteAllLines): the same numbering
    // for the parents, the active branch and the bookmarks
    std::unordered_map<uint32_t, uint32_t> local;
    for (uint32_t id = 0; id < _engine->LineIds(); ++id)
        if (_engine->HasLine(id))
            local.emplace(id, static_cast<uint32_t>(local.size()));
    const TTDTimePoint tipEnd = SessionEndPosition();
    _branches.SetEnd(_tip, {_tip, tipEnd.frame, tipEnd.tInFrame});
    for (const auto& [id, number] : local)
    {
        const TTDBranchInfo* info = _branches.Get(id);
        if (id == kTTDTrunk || !info || id >= _lines.size() || !_lines[id])
            continue;
        TTDByteWriter w;
        WriteBranchRecord(w, *info);
        params.lineMeta[id] = std::move(w.bytes);
        std::vector<uint8_t> coverage = CoverageBytes(*_lines[id]);
        if (!coverage.empty())
            params.lineHolderStreams[id].push_back({holderstream::kCoverage, "coverage", std::move(coverage)});
    }
    // Bookmarks with the line that holds them: the trunk's in its stream (all an old reader sees), each branch's
    // in its twin
    std::map<uint32_t, TTDBookmarkJournal> marks;
    for (const TTDBookmark& b : _bookmarks.Snapshot())
        if (local.count(b.branch))
            marks[b.branch].Add(b);
    for (TTDHolderStream& h : params.holderStreams)
        if (h.id == holderstream::kBookmarks)
            h.bytes = EncodeBookmarks(marks[kTTDTrunk]);
    for (const auto& [id, journal] : marks)
        if (id != kTTDTrunk && !journal.IsEmpty())
            params.lineHolderStreams[id].push_back({holderstream::kBookmarks, "bookmarks", EncodeBookmarks(journal)});
    // The session's branch state: the active branch, the trunk's record
    TTDByteWriter state;
    state.U8(1);
    state.Varint(local.count(_tip) ? local[_tip] : 0);
    WriteBranchRecord(state, _branches.Trunk());
    params.holderStreams.push_back({holderstream::kBranchState, "branch-state", std::move(state.bytes)});
}

std::unique_ptr<TimeTravelEngine> TimeTravelController::LoadEngineSession(const ITTDByteSource& source,
                                                                           TTDSessionFacts& facts,
                                                                           std::vector<uint8_t>& coverage,
                                                                           TTDBookmarkJournal& bookmarks,
                                                                           std::string& err)
{
    auto loaded = std::make_unique<TimeTravelEngine>();
    TTDSessionLoadReport report;
    if (!TTDSessionFile::Load(*loaded, source, err, &report))
        return nullptr;
    if (!report.complete)
        MLOGWARNING("TimeTravelController::DeserializeSession — loaded %zu checkpoints, stopped early: %s",
                    report.checkpoints, report.stoppedAt.c_str());

    const auto factsIt = report.holderStreams.find(holderstream::kFacts);
    if (factsIt == report.holderStreams.end() || !DecodeSessionFacts(factsIt->second, facts))
    {
        err = "the file does not say which machine recorded it (no controller facts)";
        return nullptr;
    }
    if (_context)
    {
        // A checkpoint is the memory and chipset of one machine: another model
        // restores into the wrong layout, another ROM set maps recorded pages
        // onto other code
        const uint8_t currentModel = static_cast<uint8_t>(_context->config.mem_model);
        if (facts.modelId != currentModel)
        {
            err = "model mismatch: file was recorded on model id " + std::to_string(facts.modelId) +
                  ", this emulator is model id " + std::to_string(currentModel) +
                  " - load it into an instance of the recorded model";
            return nullptr;
        }
        const uint64_t currentSignature = ComputeRomSignature();
        if (facts.romSignature != 0 && currentSignature != 0 && facts.romSignature != currentSignature)
        {
            err = "ROM set mismatch: session recorded against ROM signature 0x" + HashToString(facts.romSignature) +
                  ", this machine has 0x" + HashToString(currentSignature) +
                  " (load the ROM set the session was recorded with)";
            return nullptr;
        }
    }

    // The machine's serializers and memory, as a recording would have them
    std::string registrationError;
    if (!RegisterModelPeripherals(&registrationError))
    {
        err = "cannot load session: " + registrationError;
        return nullptr;
    }
    _modelRamPages = facts.modelRamPages;

    // Slot-set guard (ZX-bus slots SL-5, on the engine since the slots' TTD follow-up): the cards the session
    // recorded against this machine's, per slot position, with every difference listed - before the binding, which
    // only asks that each recorded device has a live counterpart of its id (a session without a card would load
    // where one is fitted, the live card's state kept; the socket's `ay` and `ts` boards share an id, the device
    // table's instance names the board). Then the slot-built cards' and the board's own checks (the MultiSound's
    // MIDI bank, the Sprinter's ISA population) read the baseline checkpoint's device states
    if (_context && loaded->CheckpointCount() > loaded->FirstCheckpoint())
    {
        const SlotManager::TtdDeviceSet recorded = SlotManager::TtdDeviceSet::Of(loaded->Devices(), facts.notRecorded);
        TTDDeviceStates states;
        std::vector<uint8_t> state;
        const std::vector<TTDDeviceEntry>& devices = loaded->Devices().Entries();
        for (size_t k = 0; k < devices.size(); ++k)
            if (loaded->DeviceStateAt(loaded->FirstCheckpoint(), static_cast<uint32_t>(k), state))
                states.emplace(devices[k].descriptor.Key(), state);
        std::string why;
        const bool matches =
            _context->pSlotManager
                ? _context->pSlotManager->TtdSessionMatches(recorded, states, _peripherals, why)
                : SlotManager::TtdSessionMatchesWithoutSlots(_context, recorded, states, _peripherals, why);
        if (!matches)
        {
            err = why;
            return nullptr;
        }
    }

    std::string unbound;
    if (loaded->BindLive(LiveRegions(), _peripherals.DeviceEntries(), &unbound) != 0)
    {
        err = "this machine cannot take the session: " + unbound;
        return nullptr;
    }

    if (const auto it = report.holderStreams.find(holderstream::kCoverage); it != report.holderStreams.end())
        coverage = it->second;
    // Branches: built into the tree by CommitLoadedSession
    _loadedBranches = {};
    _loadedBranches.records = std::move(report.lineMeta);
    for (auto& [line, streams] : report.lineHolderStreams)
    {
        if (const auto it = streams.find(holderstream::kCoverage); it != streams.end())
            _loadedBranches.coverage[line] = std::move(it->second);
        if (const auto it = streams.find(holderstream::kBookmarks); it != streams.end())
        {
            TTDBookmarkJournal marks;
            if (DecodeBookmarks(it->second, marks))
                _loadedBranches.bookmarks[line] = marks.Snapshot();
            else
                MLOGWARNING("TimeTravelController::DeserializeSession — the bookmarks of branch %u could not be read; "
                            "loaded without",
                            line);
        }
    }
    if (const auto it = report.holderStreams.find(holderstream::kBranchState); it != report.holderStreams.end())
        _loadedBranches.state = it->second;
    for (const std::string& note : report.notes)
        if (note.rfind("branch ", 0) == 0)
            MLOGWARNING("TimeTravelController::DeserializeSession — %s", note.c_str());
    if (const auto it = report.holderStreams.find(holderstream::kBookmarks);
        it != report.holderStreams.end() && !DecodeBookmarks(it->second, bookmarks))
        MLOGWARNING("TimeTravelController::DeserializeSession — the bookmarks could not be read; loaded without");
    return loaded;
}

void TimeTravelController::CommitLoadedSession(std::unique_ptr<TimeTravelEngine> loaded, const TTDSessionFacts& facts,
                                               const std::vector<uint8_t>& coverage, TTDBookmarkJournal& bookmarks)
{
    // The old session goes, with its files and its engine
    _stoppedEndValid = false;
    ResetShadow();
    _line->timeline.clear();
    _blobBytes = 0;
    _engine = std::move(loaded);
    _shadowEngine = _engine.get();
    _replayEngine = _engine.get();
    ApplyHistoryPolicy();
    RegisterScreenshotStream(*_engine);

    // Each engine line's controller line (the trunk alone without branches): the side table, the journals read
    // from the engine's events, the write journal's segments, the coverage index
    auto buildLine = [&](const std::vector<uint8_t>& lineCoverage) {
        // The side table: each engine checkpoint's time, CPU and chipset
        for (size_t i = _engine->FirstCheckpoint(); i < _engine->CheckpointCount(); ++i)
        {
            const TTDEngineCheckpoint* e = _engine->Checkpoint(i);
            TTDCheckpoint cp;
            cp.time = TTDTimePoint{e->position.frame, 0};
            cp.globalT = e->position.frame;
            cp.cpu = e->cpu;
            cp.chipset = e->chipset;
            cp.frameKind = e->baseline ? TTDFrameKind::KeyFrame : TTDFrameKind::DeltaFrame;
            cp.keyFrameAnchor = e->position.frame;
            _line->timeline.push_back(std::move(cp));
        }

        // The controller's input journal and marker list from the engine's events: status counts, the marker list
        // and the network devices read them; the engine already has them all
        std::vector<TTDInputEvent> inputs;
        std::vector<TTDNetInput> net;
        std::vector<uint8_t> payload;
        _line->externalEvents.Clear();
        _line->toolEditPayloads.clear();
        for (const TTDEvent& ev : _engine->Events().Events())
        {
            const uint16_t kind = static_cast<uint16_t>(ev.kind);
            const TTDTimePoint at = TimePointAt(ev.machineTime);
            if (kind < 0x0100)
            {
                TTDInputEvent in;
                TTDEventLog::ToInput(ev, in);
                in.time = at;
                if (HasNetRecord(in.kind))
                {
                    TTDNetInput n;
                    TTDEventLog::UnpackNet(ev, n);
                    const std::vector<uint8_t>& bytes = _engine->Payloads().Bytes(ev.payload);
                    n.payloadOffset = static_cast<uint32_t>(payload.size());
                    n.payloadLength = static_cast<uint32_t>(bytes.size());
                    payload.insert(payload.end(), bytes.begin(), bytes.end());
                    net.push_back(n);
                    in.netIndex = static_cast<uint32_t>(net.size());
                }
                inputs.push_back(in);
            }
            else if (kind < 0x0200)
            {
                TTDExternalEvent marker;
                marker.time = at;
                marker.kind = static_cast<TTDExternalEventKind>(kind - 0x0100);
                const bool editBytes = ev.kind == TTDEventKind::DebuggerEdit && ev.args[0] == kEditCarriesData;
                if (ev.payload && !editBytes)
                {
                    const std::vector<uint8_t>& reason = _engine->Payloads().Bytes(ev.payload);
                    std::memcpy(marker.reason, reason.data(), std::min(reason.size(), sizeof(marker.reason) - 1));
                }
                _line->externalEvents.Record(marker);
            }
        }
        _line->inputJournal.Assign(std::move(inputs), std::move(net), std::move(payload));

        // The writes are the engine's index; the live ring starts empty
        if (_line->writeJournal)
            _line->writeJournal->Clear();
        _line->journalSegments = _engine->Writes().Segments();

        // The controller's indexes over the line
        _line->coverageIndex.Clear();
        if (!lineCoverage.empty())
        {
            std::istringstream in(std::string(lineCoverage.begin(), lineCoverage.end()));
            if (!_line->coverageIndex.Deserialize(in))
            {
                _line->coverageIndex.Clear();
                MLOGWARNING("TimeTravelController::DeserializeSession — the coverage index could not be read; "
                            "reverse queries replay instead");
            }
        }
    };
    buildLine(coverage);
    {
        // The trunk's end in the table (a file without branches has no record of it)
        const TTDTimePoint trunkEnd = TimelineEnd();
        _branches.SetEnd(kTTDTrunk, {kTTDTrunk, trunkEnd.frame, trunkEnd.tInFrame});
    }
    if (_engine->LineIds() > 1)
        CommitLoadedBranches(buildLine);

    DisarmInputPlayback();
    _shadowEvents = TTDV1EventCursor{_line->inputJournal.Size(), _line->externalEvents.Size(), 0};
    _shadowFacts.clear();
    _inputHistoryComplete = facts.inputHistoryComplete;

    // The bus journals are the engine's (a resume records on at their ends)
    _portJournalValid = facts.portJournalValid;
    _portJournalRecorded = facts.portJournalValid;
    _portJournalOffReason = facts.portJournalOffReason;
    SyncPortJournalHook();
    _journalLostUpTo = 0;

    if (_context)
        _context->ttdCoverageActive = false;
    _bookmarks.Clear();
    for (TTDBookmark bookmark : bookmarks.Snapshot())
    {
        bookmark.branch = kTTDTrunk;
        _bookmarks.Add(bookmark);
    }
    // A branch's bookmarks, when the branch made it into the tree (a label taken twice keeps the first)
    for (const auto& [line, marks] : _loadedBranches.bookmarks)
        if (_branches.Get(line))
            for (TTDBookmark bookmark : marks)
            {
                bookmark.branch = line;
                _bookmarks.Add(bookmark);
            }
    _loadedBranches = {};

    ClearFrameCache();
    _dirtyScratch.clear();
    if (_dirtyTracker)
        _dirtyTracker->ResetSession();
    _evictedCheckpoints = 0;
    _lastStopReason.clear();
    _recordingPaused = false;

    _loadedFromFile = true;
    _capturedAtUnixMs = facts.capturedAtUnixMs;
    _sessionModelId = facts.modelId;
    _loadedRomSignature = facts.romSignature;
    _loadedNotRecorded = facts.notRecorded;
    _loadedRecordedBy = facts.recordedBy;
    SetState(TTDSessionState::Idle);

    MLOGINFO("TimeTravelController::DeserializeSession — %zu checkpoints (frames %llu..%llu), coverage %s",
             _line->timeline.size(), static_cast<unsigned long long>(_line->timeline.front().time.frame),
             static_cast<unsigned long long>(_line->timeline.back().time.frame), coverage.empty() ? "absent" : "loaded");
}

void TimeTravelController::CommitLoadedBranches(const std::function<void(const std::vector<uint8_t>&)>& buildLine)
{
    // The branch state: the active branch, the trunk's record
    uint32_t tip = kTTDTrunk;
    {
        TTDByteReader r(_loadedBranches.state);
        uint8_t version = 0;
        uint32_t active = 0;
        TTDGuid guid{};
        TTDBranchMeta meta;
        TTDPosition end;
        if (r.U8(version) && version == 1 && r.VarintAs(active) && ReadBranchRecord(r, guid, meta, end))
        {
            tip = active;
            _branches.Reset(guid);
            std::string error;
            if (meta.name != "trunk")
                _branches.Rename(kTTDTrunk, meta.name, error);
            _branches.SetNote(kTTDTrunk, meta.note);
            _branches.SetPinned(kTTDTrunk, meta.pinned);
            _branches.UpdateTags(kTTDTrunk, &meta.tags, {}, {});
        }
        else if (!_loadedBranches.state.empty())
            MLOGWARNING("TimeTravelController::DeserializeSession — the branch state could not be read: the trunk is "
                        "active");
    }
    const TTDTimePoint trunkEnd = TimelineEnd();
    _branches.SetEnd(kTTDTrunk, {kTTDTrunk, trunkEnd.frame, trunkEnd.tInFrame});

    // The branches in the file's order (a parent before its children): the table's entry first, the line read on
    // the branch's own lineage
    for (uint32_t line = 1; line < _engine->LineIds(); ++line)
    {
        if (!_engine->HasLine(line))
        {
            _branches.SkipId();   // the file's branch was left out: the ids after it stay the engine's line ids
            continue;
        }
        const uint32_t parent = _engine->LineParent(line);
        const TTDPosition fork = _engine->LineFork(line);
        TTDGuid guid = TTDNewGuid();
        TTDBranchMeta meta;
        TTDPosition end;
        bool hasEnd = false;
        if (const auto it = _loadedBranches.records.find(line); it != _loadedBranches.records.end())
        {
            TTDByteReader r(it->second);
            hasEnd = ReadBranchRecord(r, guid, meta, end);
            if (!hasEnd)
            {
                meta = {};
                guid = TTDNewGuid();
            }
        }
        if (meta.name.empty())
            meta.name = "branch " + std::to_string(line);
        std::string error;
        std::optional<uint32_t> id = _branches.Create(parent, {parent, fork.frame, fork.tInFrame}, guid, meta, error);
        if (!id && _branches.FindGuid(guid))
        {
            // A GUID taken twice (a damaged or hand-made file): the branch stays, under a new identity
            MLOGWARNING("TimeTravelController::DeserializeSession — branch %u: %s; it gets a new GUID", line,
                        error.c_str());
            id = _branches.Create(parent, {parent, fork.frame, fork.tInFrame}, TTDNewGuid(), meta, error);
        }
        if (!id || *id != line)
        {
            MLOGWARNING("TimeTravelController::DeserializeSession — branch %u is left out of the tree: %s", line,
                        error.c_str());
            if (!id)
                _branches.SkipId();
            // Its line goes with its descendants' (deepest first: a line with children stays); their ids are
            // skipped as the loop reaches them
            std::string released;
            for (uint32_t other = _engine->LineIds(); other-- > line;)
                if (_engine->HasLine(other) && _engine->IsAncestorLine(line, other))
                    _engine->ReleaseLine(other, released);
            continue;
        }
        _lines.resize(std::max<size_t>(_lines.size(), line + 1));
        _lines[line] = std::make_unique<ControllerLine>();
        _engine->ViewLine(line);
        _line = _lines[line].get();
        _viewId = line;
        _tip = line;
        const auto coverage = _loadedBranches.coverage.find(line);
        buildLine(coverage == _loadedBranches.coverage.end() ? std::vector<uint8_t>{} : coverage->second);
        // The copied part: what the fork frame held up to the fork (§4.6)
        const TTDTimePoint forkAt{fork.frame, static_cast<uint32_t>(fork.tInFrame)};
        size_t inputs = 0;
        for (const TTDInputEvent& in : _line->inputJournal.Events())
            inputs += !(forkAt < in.time);
        size_t markers = 0;
        for (const TTDExternalEvent& m : _line->externalEvents.SnapshotEvents())
            markers += !(forkAt < m.time);
        _line->forkInputs = inputs;
        _line->forkMarkers = markers;
        // The record's end, or the line's when it holds more (a recording folder's record is the fork's)
        const TTDTimePoint lineEnd = TimelineEnd();
        const bool recordEnd = hasEnd && !(TTDTimePoint{end.frame, static_cast<uint32_t>(end.tInFrame)} < lineEnd);
        _branches.SetEnd(line, recordEnd ? TTDPosition{line, end.frame, end.tInFrame}
                                         : TTDPosition{line, lineEnd.frame, lineEnd.tInFrame});
    }

    // The active branch as saved
    if (tip >= _lines.size() || !_lines[tip] || !_branches.Get(tip))
        tip = kTTDTrunk;
    _tip = tip;
    _lastVisited.assign(_lines.size(), std::nullopt);
    _engine->ViewLine(tip);
    _line = _lines[tip].get();
    _viewId = tip;
    PublishLineage();
}

bool TimeTravelController::DeserializeSession(std::istream& in, std::string& err)
{
    std::vector<uint8_t> bytes;
    if (!ReadAll(in, bytes, err))
        return false;
    return DeserializeSessionFrom(TTDMemorySource(std::move(bytes)), err);
}

bool TimeTravelController::DeserializeSessionFile(const std::string& path, std::string& err)
{
    // Read where the loader asks (the container reader seeks), not the whole file first
    const TTDFileSource source(path);
    if (!source.Valid())
    {
        err = "Cannot open file: " + path;
        return false;
    }
    return DeserializeSessionFrom(source, err);
}

bool TimeTravelController::DeserializeSessionFrom(const ITTDByteSource& source, std::string& err)
{
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    // A machine where time travel is not available at all (a ZX-Poly member)
    // loads no session
    if (!_unavailableReason.empty())
    {
        err = _unavailableReason;
        return false;
    }
    if (_state == TTDSessionState::Recording)
    {
        err = "stop the recording first: loading a session replaces it";
        return false;
    }
    std::vector<uint8_t> head(std::min<uint64_t>(source.Size(), 6));
    if (!head.empty() && source.ReadAt(0, head.data(), head.size()) && IsV1File(head))
    {
        err = kV1Refused;
        return false;
    }
    TTDSessionFacts facts;
    std::vector<uint8_t> coverage;
    TTDBookmarkJournal bookmarks;
    std::unique_ptr<TimeTravelEngine> loaded = LoadEngineSession(source, facts, coverage, bookmarks, err);
    if (!loaded)
        return false;
    CommitLoadedSession(std::move(loaded), facts, coverage, bookmarks);
    return true;
}

TTDPortSearchResult TimeTravelController::SearchPortEventsInFile(const std::string& path, const TTDPortQuery& q)
{
    TTDPortSearchResult result;
    const TTDFileSource source(path);
    if (!source.Valid())
    {
        result.error = "cannot open " + path;
        return result;
    }
    std::vector<uint8_t> magic(6);
    if (source.Size() >= 6 && source.ReadAt(0, magic.data(), 6) && IsV1File(magic))
    {
        result.error = path + ": " + kV1Refused;
        return result;
    }
    // The file's own bus journals: nothing is checked against this machine
    TimeTravelEngine file;
    std::string err;
    if (!TTDSessionFile::Load(file, source, err))
    {
        result.error = path + ": " + err;
        return result;
    }
    TTDSessionFacts facts;
    std::vector<uint8_t> factBytes;
    if (ReadHolderStream(source, holderstream::kFacts, factBytes, err) && DecodeSessionFacts(factBytes, facts) &&
        !facts.portJournalValid)
    {
        result.error = path + ": the file has no port journals" +
                       (facts.portJournalOffReason.empty() ? std::string() : " (" + facts.portJournalOffReason + ")");
        return result;
    }
    return ttd::SearchPortEvents(file.BusReads(), file.BusWrites(), q);
}

std::vector<uint8_t> TimeTravelController::BranchRecordBytes(uint32_t id) const
{
    TTDByteWriter w;
    if (const TTDBranchInfo* info = _branches.Get(id))
        WriteBranchRecord(w, *info);
    return std::move(w.bytes);
}

}  // namespace ttd
