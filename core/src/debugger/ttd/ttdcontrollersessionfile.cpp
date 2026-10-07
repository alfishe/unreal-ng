/// @file ttdcontrollersessionfile.cpp
/// @brief TimeTravelController's session files (Phase 5, C4b): the engine's
/// session file format (engine/ttdsessionfile.h) with the controller's holder
/// streams (ttdsessionfacts.h). v1 .ttd files are not read: v1 files are
/// converted only by the verification tools (D32), and before the release no
/// file format is kept compatible.

#include <chrono>
#include <cstring>
#include <iterator>
#include <random>
#include <sstream>
#include <unordered_map>

#include "common/filehelper.h"
#include "common/modulelogger.h"
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
#include "emulator/ports/portdecoder.h"
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

/// Both formats start with "TTDD"; the schema after it tells them apart (v1: 1, the container: 2)
bool IsV1File(const std::vector<uint8_t>& bytes)
{
    return bytes.size() >= 6 && std::memcmp(bytes.data(), dump::kMagic, 4) == 0 &&
           (bytes[4] | (bytes[5] << 8)) == dump::kSchemaVersion;
}

constexpr const char* kV1Refused =
    "a v1 .ttd file: this build reads session files in the engine's format only (record the session again; "
    "the verification tools convert v1 files)";
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
    for (const TTDDeviceEntry& device : _engine->Devices().Entries())
        if (const uint8_t id = static_cast<uint8_t>(device.descriptor.legacyId); id < 64)
            facts.peripheralMask |= uint64_t(1) << id;
    facts.notRecordedMask = NotRecordedMask();
    facts.inputHistoryComplete = _inputHistoryComplete;
    facts.portJournalValid = _portJournalValid;
    facts.portJournalOffReason = _portJournalOffReason;

    std::vector<TTDHolderStream> streams;
    streams.push_back({holderstream::kFacts, "controller-facts", EncodeSessionFacts(facts)});
    std::vector<uint8_t> coverageBytes;
    if (_coverageIndex.SealedFrameCount(TTDCoverageKind::Executed) > 0 ||
        _coverageIndex.SealedFrameCount(TTDCoverageKind::Written) > 0 ||
        _coverageIndex.SealedFrameCount(TTDCoverageKind::Read) > 0)
    {
        std::ostringstream coverage;
        if (_coverageIndex.Serialize(coverage))
        {
            const std::string bytes = coverage.str();
            coverageBytes.assign(bytes.begin(), bytes.end());
        }
    }
    if (!coverageBytes.empty() || declareAll)
        streams.push_back({holderstream::kCoverage, "coverage", std::move(coverageBytes)});
    streams.push_back({holderstream::kBookmarks, "bookmarks", EncodeBookmarks(_bookmarks)});
    return streams;
}

bool TimeTravelController::SerializeSession(std::ostream& out, std::string& err)
{
    const SessionOperation op{*this, SessionOperation::Kind::Read};
    if (_timeline.empty() || !_engine->IsSessionOpen())
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

    OStreamSink sink(out);
    return TTDSessionFile::Save(*_engine, sink, err, params);
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

    // The slot-set guard (ZX-bus slots SL-5), as v1's: a session recorded with
    // other cards in the slots is refused with every difference listed by slot
    // ("ay-socket: recorded tsfm, this machine ay / ts"), before the binding
    // below would name only the devices it cannot place. The session's first
    // checkpoint names its cards: the device set is fixed for a session (D38)
    if (_context && loaded->CheckpointCount() > loaded->FirstCheckpoint())
    {
        std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
        const size_t first = loaded->FirstCheckpoint();
        for (const TTDDeviceEntry& device : loaded->Devices().Entries())
        {
            const uint8_t id = static_cast<uint8_t>(device.descriptor.legacyId);
            std::vector<uint8_t> state;
            if (id < 64 && loaded->DeviceState(first, id, state) && !state.empty())
                blobs[id] = TTDPeripheralRegistry::EncodeBlob(id, state.data(), state.size());
        }
        std::string why;
        const bool matches =
            _context->pSlotManager
                ? _context->pSlotManager->TtdSessionMatches(blobs, facts.notRecordedMask, _peripherals, why)
                : SlotManager::TtdSlotSetMatches({}, SlotManager::TtdDeviceSet::Of(blobs, facts.notRecordedMask),
                                                 SlotManager::TtdDeviceSet::Of(_peripherals), why) &&
                      (!_context->pPortDecoder || _context->pPortDecoder->TtdSessionMatches(blobs, why));
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
    _timeline.clear();
    _blobBytes = 0;
    _engine = std::move(loaded);
    _shadowEngine = _engine.get();
    _replayEngine = _engine.get();
    ApplyHistoryPolicy();
    RegisterScreenshotStream(*_engine);

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
        _timeline.push_back(std::move(cp));
    }

    // v1's journals from the engine's events: status counts, the marker list
    // and the network devices read them; the engine already has them all
    std::vector<TTDInputEvent> inputs;
    std::vector<TTDNetInput> net;
    std::vector<uint8_t> payload;
    _externalEvents.Clear();
    _toolEditPayloads.clear();
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
            _externalEvents.Record(marker);
        }
    }
    _inputJournal.Assign(std::move(inputs), std::move(net), std::move(payload));
    DisarmInputPlayback();
    _shadowEvents = TTDV1EventCursor{_inputJournal.Size(), _externalEvents.Size(), 0};
    _shadowFacts.clear();
    _inputHistoryComplete = facts.inputHistoryComplete;

    // The bus journals are the engine's; v1's recorders start empty on a resume
    _portReads.Clear();
    _portWrites.Clear();
    _shadowBusReads = 0;
    _shadowBusWrites = 0;
    _portJournalValid = facts.portJournalValid;
    _portJournalRecorded = facts.portJournalValid;
    _portJournalOffReason = facts.portJournalOffReason;
    SyncPortJournalHook();

    // The writes are the engine's index; the live ring starts empty
    const TTDWriteIndex& writes = _engine->Writes();
    if (_writeJournal)
        _writeJournal->Clear();
    _journalLostUpTo = 0;
    _journalSegments = writes.Segments();

    // The controller's indexes over the session
    _coverageIndex.Clear();
    if (!coverage.empty())
    {
        std::istringstream in(std::string(coverage.begin(), coverage.end()));
        if (!_coverageIndex.Deserialize(in))
        {
            _coverageIndex.Clear();
            MLOGWARNING("TimeTravelController::DeserializeSession — the coverage index could not be read; "
                        "reverse queries replay instead");
        }
    }
    if (_context)
        _context->ttdCoverageActive = false;
    _bookmarks.Clear();
    for (const TTDBookmark& bookmark : bookmarks.Snapshot())
        _bookmarks.Add(bookmark);

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
    _loadedNotRecordedMask = facts.notRecordedMask;
    _loadedRecordedBy = facts.recordedBy;
    SetState(TTDSessionState::Idle);

    MLOGINFO("TimeTravelController::DeserializeSession — %zu checkpoints (frames %llu..%llu), coverage %s",
             _timeline.size(), static_cast<unsigned long long>(_timeline.front().time.frame),
             static_cast<unsigned long long>(_timeline.back().time.frame), coverage.empty() ? "absent" : "loaded");
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

}  // namespace ttd
