#include "ttdcontrol.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <type_traits>

#include "3rdparty/message-center/messagecenter.h"
#include "common/filehelper.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdbookmarks.h"
#include "debugger/ttd/ttdexternalevents.h"
#include "debugger/ttd/ttdfileinfo.h"
#include "debugger/ttd/ttdportsearch.h"
#include "debugger/ttd/ttdprobe.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/mainloop.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

namespace ttd
{
/// region <Errors and the reply>

int TTDControlErrorHttpStatus(TTDControlError error)
{
    switch (error)
    {
        case TTDControlError::None: return 200;
        case TTDControlError::NotAvailable: return 501;
        case TTDControlError::BadRequest: return 400;
        case TTDControlError::NotFound: return 404;
        case TTDControlError::Conflict: return 409;
        case TTDControlError::Internal: return 500;
    }
    return 500;
}

const char* TTDControlErrorPhrase(TTDControlError error)
{
    switch (error)
    {
        case TTDControlError::None: return "";
        case TTDControlError::NotAvailable: return "Not Available";
        case TTDControlError::BadRequest: return "Bad Request";
        case TTDControlError::NotFound: return "Not Found";
        case TTDControlError::Conflict: return "Conflict";
        case TTDControlError::Internal: return "Internal Error";
    }
    return "Internal Error";
}

StateNode TTDReply::ToValue() const
{
    if (Ok())
        return body;
    StateNode value = StateNode::Object();
    value["error"] = body.find("error") ? *body.find("error") : StateNode(TTDControlErrorPhrase(error));
    value["message"] = message;
    for (const auto& [key, field] : body.members)
        if (key != "error")
            value[key] = field;
    return value;
}

/// endregion </Errors and the reply>

namespace
{
TTDReply Fail(TTDControlError error, std::string message, StateNode body = StateNode::Object())
{
    TTDReply reply;
    reply.error = error;
    reply.message = std::move(message);
    reply.body = std::move(body);
    return reply;
}

StateNode StringOrNull(const std::string& s)
{
    return s.empty() ? StateNode() : StateNode(s);
}

/// A non-negative integer: decimal, or hex as 0x.., #.. or $..; nothing else (no sign, no fraction)
bool ParseU64(const std::string& text, uint64_t& out)
{
    if (text.empty())
        return false;
    size_t i = 0;
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        base = 16;
        i = 2;
    }
    else if (text.size() > 1 && (text[0] == '#' || text[0] == '$'))
    {
        base = 16;
        i = 1;
    }
    uint64_t value = 0;
    for (; i < text.size(); ++i)
    {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        int digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else
            return false;
        if (value > (UINT64_MAX - static_cast<uint64_t>(digit)) / static_cast<uint64_t>(base))
            return false;
        value = value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
    }
    out = value;
    return true;
}

/// true / false / 1 / 0 / on / off / yes / no; a flag without a value ("") is true
bool ParseBool(const std::string& text, bool& out)
{
    std::string t = text;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (t.empty() || t == "true" || t == "1" || t == "on" || t == "yes")
        out = true;
    else if (t == "false" || t == "0" || t == "off" || t == "no")
        out = false;
    else
        return false;
    return true;
}

const std::string* Option(const TTDRequest& request, const char* name)
{
    auto it = request.options.find(name);
    return it == request.options.end() ? nullptr : &it->second;
}

StateNode RecordedMachineNodeImpl(const TTDRecordedMachine& m)
{
    StateNode v = StateNode::Object();
    v["model_id"] = static_cast<unsigned>(m.modelId);
    v["model"] = StringOrNull(m.model);
    v["ram_page_bound"] = static_cast<unsigned>(m.ramPageBound);
    // A string: a 64-bit hash does not survive a JSON number (doubles)
    v["rom_signature"] = m.romSignature == 0 ? StateNode() : StateNode("0x" + HashToString(m.romSignature));
    v["peripheral_mask"] = static_cast<uint64_t>(m.peripheralMask);
    StateNode list = StateNode::Array();
    for (const std::string& name : m.peripherals)
        list.push(name);
    v["peripherals"] = list;
    StateNode notRecorded = StateNode::Array();
    for (const std::string& name : m.notRecorded)
        notRecorded.push(name);
    v["not_recorded"] = notRecorded;
    v["general_sound"] = GeneralSoundName(m.generalSound);
    v["turbo_sound"] = m.turboSound;
    return v;
}

StateNode TimePointNode(const TTDTimePoint& t)
{
    StateNode node = StateNode::Object();
    node["frame"] = t.frame;
    node["tinframe"] = static_cast<unsigned>(t.tInFrame);
    return node;
}

/// TD-8: the part of history a backward search examined
void AddSearchWindow(StateNode& body, const TTDSearchWindow& window)
{
    if (!window.searched)
        return;
    body["covered_from"] = window.from.frame;
    body["covered_from_tinframe"] = static_cast<unsigned>(window.from.tInFrame);
    body["covered_to"] = window.to.frame;
    body["covered_to_tinframe"] = static_cast<unsigned>(window.to.tInFrame);
}

/// An address-sized number (decimal, 0x.., #.., $..) no larger than @p max
bool ParseUpTo(const std::string& text, uint64_t max, uint64_t& out)
{
    return ParseU64(text, out) && out <= max;
}

/// A position in the reply body: frame and tinframe as top-level fields
void AddPosition(StateNode& body, const TTDTimePoint& t)
{
    body["frame"] = t.frame;
    body["tinframe"] = static_cast<unsigned>(t.tInFrame);
}
}  // namespace

/// region <Backends>

/// The verbs over one session class: v1's TimeTravelManager or the engine's
/// TimeTravelController (Phase 5, C5). Both offer the same methods, so one
/// implementation serves both; TTDControl picks the one the instance runs
class TTDControl::Backend
{
public:
    virtual ~Backend() = default;
    /// @p verb is known and its options checked
    virtual TTDReply Execute(const std::string& verb, const TTDRequest& request) = 0;
};

template <class S>
StateNode StatusBodyOf(const S* manager);
template <class S>
void AddWriteJournalOf(StateNode& body, const S& manager);

template <class S>
class TTDControlBackend final : public TTDControl::Backend
{
public:
    TTDControlBackend(EmulatorContext* context, S* manager) : _context(context), _manager(manager) {}
    TTDReply Execute(const std::string& verb, const TTDRequest& request) override;

private:
    TTDReply Run(const std::string& verb, const TTDRequest& request);
    TTDReply Status();
    TTDReply Start(const TTDRequest& request);
    TTDReply Stop();
    TTDReply Invalidate(const TTDRequest& request);
    TTDReply HistoryLimit(const TTDRequest& request);
    TTDReply Journal(const TTDRequest& request);
    TTDReply JournalBuild(const TTDRequest& request);
    TTDReply JournalBuildCancel();
    TTDReply Position();
    TTDReply Seek(const TTDRequest& request);
    TTDReply StepFrame(bool forward);
    TTDReply Resume(const TTDRequest& request);
    TTDReply StepInstruction(const TTDRequest& request);
    TTDReply ReverseStep(const TTDRequest& request);
    TTDReply Markers();
    TTDReply Bookmarks();
    TTDReply BookmarkAdd(const TTDRequest& request);
    TTDReply BookmarkDelete(const TTDRequest& request);
    TTDReply PortEvents(const TTDRequest& request);
    TTDReply FindLast(const TTDRequest& request);
    TTDReply ReverseContinue(const TTDRequest& request);
    TTDReply CoverageProbe(const TTDRequest& request);
    TTDReply CoverageScan(const TTDRequest& request);
    TTDReply CoverageSummary(const TTDRequest& request);
    TTDReply FileInfo(const TTDRequest& request);
    TTDReply Dump(const TTDRequest& request);
    TTDReply Load(const TTDRequest& request);
    TTDReply ExportClip(const TTDRequest& request);

    /// A verb that cannot run while a recording captures: refused (409). @p browses:
    /// the engine's controller pauses the recording for it instead (D8)
    bool RefuseWhileRecording(TTDReply& reply, bool browses = false) const;
    bool OnMachineThread() const;
    void PauseAndConfirm();
    void NotifyFrameRefresh();

    EmulatorContext* _context = nullptr;
    S* _manager = nullptr;
};

TTDControl::TTDControl(EmulatorContext* context)
{
    if (context && context->pTimeTravelController)
        _backend = std::make_unique<TTDControlBackend<TimeTravelController>>(context, context->pTimeTravelController);
    else
        _backend = std::make_unique<TTDControlBackend<TimeTravelManager>>(
            context, context ? context->pTimeTravelManager : nullptr);
}

TTDControl::TTDControl(EmulatorContext* context, TimeTravelController* controller)
    : _backend(std::make_unique<TTDControlBackend<TimeTravelController>>(context, controller))
{
}

TTDControl::~TTDControl() = default;

StateNode TTDControl::StatusBody(const TimeTravelManager* manager)
{
    return StatusBodyOf(manager);
}

StateNode TTDControl::StatusBody(const TimeTravelController* controller)
{
    return StatusBodyOf(controller);
}

StateNode TTDControl::StatusBody(std::nullptr_t)
{
    return StatusBodyOf(static_cast<const TimeTravelManager*>(nullptr));
}

void TTDControl::AddWriteJournal(StateNode& body, const TimeTravelManager& manager)
{
    AddWriteJournalOf(body, manager);
}

void TTDControl::AddWriteJournal(StateNode& body, const TimeTravelController& controller)
{
    AddWriteJournalOf(body, controller);
}

/// endregion </Backends>

/// region <Verbs>


const std::vector<std::string>& TTDControl::Verbs()
{
    static const std::vector<std::string> verbs = {
        "status", "start", "stop", "invalidate", "history-limit", "journal", "journal-build", "journal-build-cancel",
        "position", "seek", "step-back", "step-forward", "resume", "step-instruction", "reverse-step",
        "markers", "bookmarks", "bookmark-add", "bookmark-delete",
        "port-events", "find-last", "reverse-continue", "coverage-probe", "coverage-scan", "coverage-summary",
        "file-info", "dump", "load", "export-clip"};
    return verbs;
}

const std::vector<std::string>& TTDControl::OptionsFor(const std::string& verb)
{
    static const std::map<std::string, std::vector<std::string>> options = {
        {"start", {"journal", "history_limit_frames", "history_limit_bytes"}},
        {"invalidate", {"reason"}},
        {"history-limit", {"frames", "bytes"}},
        {"journal", {"enabled"}},
        {"journal-build", {"from_frame", "to_frame"}},
        {"seek", {"frame", "tinframe", "bookmark"}},
        {"resume", {"frame", "tinframe"}},
        {"step-instruction", {"dir"}},
        {"reverse-step", {"count", "tstates"}},
        {"bookmark-add", {"label", "frame", "tinframe"}},
        {"bookmark-delete", {"label"}},
        {"port-events", {"*"}},
        {"find-last", {"addr", "addr_from", "addr_to", "access", "value", "pc_from", "pc_to", "phys_page",
                       "before_frame", "before_tin", "before"}},
        {"reverse-continue", {"pcs"}},
        {"coverage-probe", {"frame", "kind", "addr_from", "addr_to", "phys_page"}},
        {"coverage-scan", {"from_frame", "to_frame", "kind", "addr_from", "addr_to", "phys_page", "limit"}},
        {"coverage-summary", {"from_frame", "to_frame", "kind", "bucket_size", "limit"}},
        {"file-info", {"path"}},
        {"dump", {"path"}},
        {"load", {"path"}},
        {"export-clip", {"from", "to", "path", "chunk"}},
    };
    static const std::vector<std::string> none;
    auto it = options.find(verb);
    return it == options.end() ? none : it->second;
}

TTDReply TTDControl::Execute(const TTDRequest& request)
{
    std::string verb = request.verb;
    std::transform(verb.begin(), verb.end(), verb.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto& verbs = Verbs();
    if (std::find(verbs.begin(), verbs.end(), verb) == verbs.end())
    {
        std::string known;
        for (const std::string& v : verbs)
            known += (known.empty() ? "" : ", ") + v;
        return Fail(TTDControlError::BadRequest, "unknown time-travel verb '" + request.verb + "' (verbs: " + known + ")");
    }

    // Every option name is checked, so a typo fails the same way on every surface
    const auto& allowed = OptionsFor(verb);
    const bool anyName = allowed.size() == 1 && allowed.front() == "*";
    for (const auto& [name, value] : request.options)
    {
        (void)value;
        if (!anyName && std::find(allowed.begin(), allowed.end(), name) == allowed.end())
        {
            std::string list;
            for (const std::string& o : allowed)
                list += (list.empty() ? "" : ", ") + o;
            return Fail(TTDControlError::BadRequest, "'" + verb + "' has no option '" + name + "'" +
                                                         (list.empty() ? std::string(" (it takes none)")
                                                                       : " (options: " + list + ")"));
        }
    }

    return _backend->Execute(verb, request);
}

template <class S>
TTDReply TTDControlBackend<S>::Execute(const std::string& verb, const TTDRequest& request)
{
    // Status answers without time travel too: it is the capability probe. So do the
    // coverage queries: "no index" (index_available false) is their answer then
    if (verb == "status")
        return Status();
    if (verb == "coverage-probe")
        return CoverageProbe(request);
    if (verb == "coverage-scan")
        return CoverageScan(request);
    if (verb == "coverage-summary")
        return CoverageSummary(request);
    // A file on disk is described without any instance
    if (verb == "file-info")
        return FileInfo(request);
    if (!_manager)
        return Fail(TTDControlError::NotAvailable, "TTD engine not available in this build");
    return Run(verb, request);
}

template <class S>
TTDReply TTDControlBackend<S>::Run(const std::string& verb, const TTDRequest& request)
{
    if (verb == "start")
        return Start(request);
    if (verb == "stop")
        return Stop();
    if (verb == "invalidate")
        return Invalidate(request);
    if (verb == "history-limit")
        return HistoryLimit(request);
    if (verb == "journal")
        return Journal(request);
    if (verb == "journal-build")
        return JournalBuild(request);
    if (verb == "journal-build-cancel")
        return JournalBuildCancel();
    if (verb == "position")
        return Position();
    if (verb == "seek")
        return Seek(request);
    if (verb == "step-back" || verb == "step-forward")
        return StepFrame(verb == "step-forward");
    if (verb == "resume")
        return Resume(request);
    if (verb == "step-instruction")
        return StepInstruction(request);
    if (verb == "reverse-step")
        return ReverseStep(request);
    if (verb == "markers")
        return Markers();
    if (verb == "bookmarks")
        return Bookmarks();
    if (verb == "bookmark-add")
        return BookmarkAdd(request);
    if (verb == "bookmark-delete")
        return BookmarkDelete(request);
    if (verb == "port-events")
        return PortEvents(request);
    if (verb == "find-last")
        return FindLast(request);
    if (verb == "reverse-continue")
        return ReverseContinue(request);
    if (verb == "dump")
        return Dump(request);
    if (verb == "load")
        return Load(request);
    if (verb == "export-clip")
        return ExportClip(request);
    return Fail(TTDControlError::Internal, "verb '" + verb + "' has no implementation");
}

template <class S>
StateNode StatusBodyOf(const S* manager)
{
    StateNode ret = StateNode::Object();
    if (!manager)
    {
        // The idle answer: automation detects "TTD not available" by ttd_available
        ret["state"] = TTDSessionStateToString(TTDSessionState::Idle);
        for (const char* key : {"session_start_frame", "current_end_frame", "checkpoint_count", "page_store_bytes",
                                "page_store_used_bytes", "baseline_frames_captured", "session_heap_bytes",
                                "bookmark_count", "input_event_count", "external_event_count"})
            ret[key] = 0;
        ret["input_history_complete"] = true;
        ret["port_journal_active"] = false;
        for (const char* key : {"history_limit_frames", "history_limit_bytes", "history_bytes", "evicted_checkpoints"})
            ret[key] = 0;
        ret["port_journal_off_reason"] = StateNode();
        for (const char* key : {"port_read_count", "port_write_count", "port_journal_bytes",
                                "port_replay_value_mismatches", "port_replay_divergences"})
            ret[key] = 0;
        ret["ttd_available"] = false;
        ret["backend"] = StateNode();
        ret["recording_paused"] = false;
        ret["earliest"] = StateNode();
        return ret;
    }

    const TTDSessionInfo info = manager->ReadSessionInfo();
    ret["state"] = TTDSessionStateToString(info.state);
    ret["session_start_frame"] = info.sessionStartFrame;
    ret["current_end_frame"] = info.currentEndFrame;
    ret["checkpoint_count"] = static_cast<uint64_t>(info.checkpointCount);
    ret["page_store_bytes"] = static_cast<uint64_t>(info.pageStoreBytes);
    ret["page_store_used_bytes"] = static_cast<uint64_t>(info.pageStoreUsedBytes);
    ret["baseline_frames_captured"] = static_cast<uint64_t>(info.baselineFramesCaptured);
    ret["session_heap_bytes"] = static_cast<uint64_t>(info.sessionHeapBytes);
    ret["bookmark_count"] = static_cast<uint64_t>(info.bookmarkCount);
    ret["input_event_count"] = static_cast<uint64_t>(info.inputEventCount);
    ret["external_event_count"] = static_cast<uint64_t>(info.externalEventCount);
    ret["input_history_complete"] = info.inputHistoryComplete;
    // Port-read journal: whether replay is isolated from media and host devices
    ret["port_journal_active"] = info.portJournalActive;
    // History limit: the oldest checkpoints are released beyond it while recording
    ret["history_limit_frames"] = info.historyLimitFrames;
    ret["history_limit_bytes"] = info.historyLimitBytes;
    ret["history_bytes"] = static_cast<uint64_t>(info.historyBytes);
    ret["evicted_checkpoints"] = static_cast<uint64_t>(info.evictedCheckpoints);
    ret["port_journal_off_reason"] = StringOrNull(info.portJournalOffReason);
    ret["port_read_count"] = static_cast<uint64_t>(info.portReadCount);
    ret["port_write_count"] = static_cast<uint64_t>(info.portWriteCount);
    ret["port_journal_bytes"] = static_cast<uint64_t>(info.portJournalBytes);
    ret["port_replay_value_mismatches"] = static_cast<uint64_t>(info.portReplayValueMismatches);
    ret["port_replay_divergences"] = static_cast<uint64_t>(info.portReplayDivergences);
    ret["ttd_available"] = true;
    // Which implementation records on this instance (Phase 5): "engine" or "v1"
    ret["backend"] = std::is_same<S, TimeTravelController>::value ? "engine" : "v1";
    ret["loaded_from_file"] = info.loadedFromFile;
    ret["source_path"] = info.sourcePath;
    ret["captured_at_unix_ms"] = info.capturedAtUnixMs;
    ret["model_id"] = static_cast<unsigned>(info.modelId);
    ret["model_ram_pages"] = static_cast<unsigned>(info.modelRamPages);
    // The recorded machine (null while there is no session) and, for a loaded
    // file, the instance that recorded it
    ret["machine"] = info.checkpointCount != 0 ? TTDControl::RecordedMachineBody(info.machine) : StateNode();
    ret["recorded_by"] = StringOrNull(info.recordedBy);
    ret["write_journal_records"] = static_cast<uint64_t>(info.writeJournalRecords);
    ret["write_journal_bytes"] = static_cast<uint64_t>(info.writeJournalBytes);
    ret["coverage_index_frames"] = static_cast<uint64_t>(info.coverageIndexFrames);
    ret["coverage_index_bytes"] = static_cast<uint64_t>(info.coverageIndexBytes);
    // Why the last session with history was dropped (null when none was):
    // tells an agent why its recording is gone, e.g. a device TTD cannot follow
    ret["last_drop_reason"] = StringOrNull(info.lastDropReason);
    // Why the last recording stopped when no stop request ended it (FR-17: a feature
    // switched off); null otherwise
    ret["last_stop_reason"] = StringOrNull(info.lastStopReason);
    ret["recording_paused"] = info.recordingPaused;
    // D12: the earliest position kept - where "jump to start" goes (branch 0: the trunk)
    if (info.checkpointCount)
    {
        StateNode earliest = StateNode::Object();
        earliest["branch"] = 0;
        earliest["frame"] = info.sessionStartFrame;
        earliest["tinframe"] = 0;
        ret["earliest"] = earliest;
    }
    else
        ret["earliest"] = StateNode();
    // Why time travel is not available for this machine at all (null when it is)
    ret["unavailable_reason"] = StringOrNull(info.unavailableReason);
    AddWriteJournalOf(ret, *manager);
    return ret;
}

template <class S>
void AddWriteJournalOf(StateNode& body, const S& manager)
{
    const TTDSessionInfo info = manager.ReadSessionInfo();
    body["write_journal_enabled"] = info.writeJournalEnabled;
    body["write_journal_complete"] = info.writeJournalComplete;
    StateNode spans = StateNode::Array();
    for (const auto& [from, to] : info.writeJournalSpans)
    {
        StateNode span = StateNode::Object();
        span["from_frame"] = from.frame;
        span["from_tinframe"] = static_cast<unsigned>(from.tInFrame);
        span["to_frame"] = to.frame;
        span["to_tinframe"] = static_cast<unsigned>(to.tInFrame);
        spans.push(span);
    }
    body["write_journal_segments"] = spans;
    const TTDJournalBuildState build = manager.GetJournalBuildState();
    StateNode b = StateNode::Object();
    b["active"] = build.active;
    b["done"] = build.done;
    b["total"] = build.total;
    body["write_journal_build"] = b;
}

template <class S>
TTDReply TTDControlBackend<S>::Status()
{
    TTDReply reply;
    reply.body = StatusBodyOf(_manager);
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::Start(const TTDRequest& request)
{
    // Time travel not available for this machine at all (a ZX-Poly member)
    if (!_manager->GetUnavailableReason().empty())
    {
        StateNode body = StateNode::Object();
        body["state"] = TTDSessionStateToString(_manager->GetState());
        return Fail(TTDControlError::Conflict, _manager->GetUnavailableReason(), body);
    }

    // journal: record the write journal (D40). Absent: the current choice stands (Lua and
    // Python start() keep ttd_set_journal_enabled; the WebAPI and the CLI pass false)
    bool journal = false;
    const std::string* journalText = Option(request, "journal");
    if (journalText && !ParseBool(*journalText, journal))
        return Fail(TTDControlError::BadRequest, "journal must be true or false");

    // Optional history limit, the same as the history-limit verb
    const TTDSessionInfo current = _manager->ReadSessionInfo();
    uint64_t limitFrames = current.historyLimitFrames;
    uint64_t limitBytes = current.historyLimitBytes;
    const std::string* framesText = Option(request, "history_limit_frames");
    const std::string* bytesText = Option(request, "history_limit_bytes");
    if ((framesText && !ParseU64(*framesText, limitFrames)) || (bytesText && !ParseU64(*bytesText, limitBytes)))
        return Fail(TTDControlError::BadRequest,
                    "history_limit_frames and history_limit_bytes must be non-negative integers (0 = no limit)");

    const bool alreadyRecording = _manager->IsRecording();
    if (!alreadyRecording && journalText)
        _manager->SetEnableWriteJournal(journal);
    if (framesText || bytesText)
        _manager->SetHistoryLimit(limitFrames, limitBytes);
    const bool ok = _manager->StartRecording();

    TTDReply reply;
    reply.body["started"] = ok && !alreadyRecording;
    reply.body["already_active"] = alreadyRecording;
    reply.body["state"] = TTDSessionStateToString(_manager->GetState());
    reply.body["write_journal_enabled"] = _manager->GetEnableWriteJournal();
    const TTDSessionInfo info = _manager->ReadSessionInfo();
    reply.body["history_limit_frames"] = info.historyLimitFrames;
    reply.body["history_limit_bytes"] = info.historyLimitBytes;
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::Stop()
{
    const bool wasRecording = _manager->IsRecording();
    _manager->StopRecording();
    TTDReply reply;
    reply.body["stopped"] = wasRecording;
    reply.body["state"] = TTDSessionStateToString(_manager->GetState());
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::Invalidate(const TTDRequest& request)
{
    const std::string* reasonText = Option(request, "reason");
    const std::string reason = reasonText && !reasonText->empty() ? *reasonText : "invalidate";
    if (const std::string refusal = _manager->RecordingGuard(TTDGuardedAction::Invalidate); !refusal.empty())
        return Fail(TTDControlError::Conflict, refusal);
    _manager->InvalidateSession(reason.c_str());
    TTDReply reply;
    reply.body["invalidated"] = true;
    reply.body["reason"] = reason;
    reply.body["state"] = TTDSessionStateToString(_manager->GetState());
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::HistoryLimit(const TTDRequest& request)
{
    TTDSessionInfo info = _manager->ReadSessionInfo();
    const std::string* framesText = Option(request, "frames");
    const std::string* bytesText = Option(request, "bytes");
    if (framesText || bytesText)
    {
        uint64_t frames = info.historyLimitFrames;
        uint64_t bytes = info.historyLimitBytes;
        if ((framesText && !ParseU64(*framesText, frames)) || (bytesText && !ParseU64(*bytesText, bytes)))
            return Fail(TTDControlError::BadRequest, "frames and bytes must be non-negative integers (0 = no limit)");
        _manager->SetHistoryLimit(frames, bytes);
        info = _manager->ReadSessionInfo();
    }
    TTDReply reply;
    reply.body["history_limit_frames"] = info.historyLimitFrames;
    reply.body["history_limit_bytes"] = info.historyLimitBytes;
    reply.body["history_bytes"] = static_cast<uint64_t>(info.historyBytes);
    reply.body["evicted_checkpoints"] = static_cast<uint64_t>(info.evictedCheckpoints);
    reply.body["checkpoint_count"] = static_cast<uint64_t>(info.checkpointCount);
    reply.body["session_start_frame"] = info.sessionStartFrame;
    reply.body["current_end_frame"] = info.currentEndFrame;
    reply.body["state"] = TTDSessionStateToString(info.state);
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::Journal(const TTDRequest& request)
{
    if (const std::string* value = Option(request, "enabled"))
    {
        bool enabled = false;
        if (!ParseBool(*value, enabled))
            return Fail(TTDControlError::BadRequest, "enabled must be true or false");
        if (!_manager->SwitchWriteJournal(enabled))
            return Fail(TTDControlError::Conflict, "write journal not available");
    }
    TTDReply reply;
    AddWriteJournalOf(reply.body, *_manager);
    // The choice for recordings, also without a session (write_journal_enabled: recorded now)
    reply.body["write_journal_setting"] = _manager->GetEnableWriteJournal();
    reply.body["write_journal_records"] = static_cast<uint64_t>(_manager->GetSessionInfo().writeJournalRecords);
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::JournalBuild(const TTDRequest& request)
{
    if (TTDReply refusal; RefuseWhileRecording(refusal))
        return refusal;
    uint64_t from = 0;
    uint64_t to = UINT64_MAX;
    const std::string* fromText = Option(request, "from_frame");
    const std::string* toText = Option(request, "to_frame");
    if ((fromText && !ParseU64(*fromText, from)) || (toText && !ParseU64(*toText, to)))
        return Fail(TTDControlError::BadRequest, "from_frame and to_frame must be non-negative integers");

    PauseAndConfirm();
    const TTDJournalBuildResult r = _manager->BuildWriteJournalFrames(from, to);
    NotifyFrameRefresh();

    StateNode body = StateNode::Object();
    body["ok"] = r.ok;
    if (!r.ok)
        body["error"] = r.error;
    body["cancelled"] = r.cancelled;
    body["frames_built"] = r.framesBuilt;
    body["frames_covered"] = r.framesCovered;
    body["frames_refused"] = r.framesRefused;
    body["records"] = r.records;
    AddWriteJournalOf(body, *_manager);
    if (!r.ok)
        return Fail(TTDControlError::Conflict, r.error, body);
    TTDReply reply;
    reply.body = body;
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::JournalBuildCancel()
{
    const bool active = _manager->GetJournalBuildState().active;
    _manager->CancelJournalBuild();
    TTDReply reply;
    reply.body["cancelled"] = active;
    return reply;
}

template <class S>
bool TTDControlBackend<S>::RefuseWhileRecording(TTDReply& reply, bool browses) const
{
    if (!_manager->IsRecording())
        return false;
    if (std::is_same<S, TimeTravelController>::value && browses)
        return false;   // the controller pauses the recording (D8)
    StateNode body = StateNode::Object();
    body["state"] = TTDSessionStateToString(_manager->GetState());
    reply = Fail(TTDControlError::Conflict,
                 "Cannot scrub while recording is active - stop the recording first. Scrubbing during "
                 "recording would overwrite live emulator state with restored checkpoint data and corrupt "
                 "the timeline.",
                 body);
    return true;
}

template <class S>
TTDReply TTDControlBackend<S>::Position()
{
    TTDReply reply;
    reply.body["current"] = TimePointNode(_manager->CurrentPosition());
    reply.body["session_end"] = TimePointNode(_manager->SessionEndPosition());
    reply.body["state"] = TTDSessionStateToString(_manager->GetState());
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::Seek(const TTDRequest& request)
{
    if (TTDReply refusal; RefuseWhileRecording(refusal, true))
        return refusal;

    // The target: coordinates (frame [+ tinframe]) or a bookmark label (TD-4). A bookmark
    // is advisory and never a barrier: after the lookup it is a plain seek
    const std::string* bookmark = Option(request, "bookmark");
    const std::string* frameText = Option(request, "frame");
    const std::string* tText = Option(request, "tinframe");
    TTDTimePoint target{};
    if (bookmark)
    {
        TTDBookmark found;
        if (bookmark->empty())
            return Fail(TTDControlError::BadRequest, "Field bookmark must be a non-empty bookmark label");
        if (!_manager->FindBookmark(*bookmark, found))
            return Fail(TTDControlError::NotFound, "Unknown bookmark label: " + *bookmark);
        target = found.time;
    }
    else
    {
        if (!frameText)
            return Fail(TTDControlError::BadRequest, "Missing required field: frame (or bookmark)");
        uint64_t frame = 0;
        uint64_t t = 0;
        if (!ParseU64(*frameText, frame) || (tText && (!ParseU64(*tText, t) || t > UINT32_MAX)))
            return Fail(TTDControlError::BadRequest, "frame and tinframe must be non-negative integers");
        target = {frame, static_cast<uint32_t>(t)};
    }
    // D13 (the engine): a frame without a T-state is the frame's end - machine
    // state and picture are then the frame's final ones
    const bool frameEnd = std::is_same<S, TimeTravelController>::value && !bookmark && !tText;

    // Park the machine so its thread cannot advance past the restored checkpoint; it
    // stays paused at the target (Detached) until resume
    PauseAndConfirm();
    [[maybe_unused]] const uint64_t requestedFrame = target.frame;
    if constexpr (std::is_same<S, TimeTravelController>::value)
        if (frameEnd)
            target = _manager->FrameEndPosition(requestedFrame);
    TTDSeekResult result;
    const bool reached = _manager->SeekTo(target, &result);
    NotifyFrameRefresh();

    TTDReply reply;
    reply.body["reached"] = reached;
    TTDTimePoint arrived = result.arrivedAt;
    if constexpr (std::is_same<S, TimeTravelController>::value)
    {
        // The end of frame N is named in frame N: {N, its length}, the same machine time as {N+1, 0}
        if (frameEnd && reached && arrived == TTDTimePoint{requestedFrame + 1, 0} &&
            requestedFrame >= _manager->GetSessionInfo().sessionStartFrame)
            arrived = {requestedFrame, _manager->FrameLength(requestedFrame)};
    }
    reply.body["arrived_at"] = TimePointNode(arrived);
    const char* reason = "target";
    if (result.haltReason == TTDSeekHaltReason::ExternalEvent)
        reason = "external_event";
    else if (result.haltReason == TTDSeekHaltReason::OutOfRange)
        reason = "out_of_range";
    reply.body["halt_reason"] = reason;
    if (result.beforeEarliest)
    {
        // D12: before the history, the answer names where it starts
        reply.body["earliest"] = TimePointNode(result.earliest);
        reply.body["message"] = "frame " + std::to_string(result.earliest.frame) +
                                " is the earliest position kept (the older frames were released or never recorded)";
    }
    if (result.haltReason == TTDSeekHaltReason::ExternalEvent)
    {
        StateNode marker = TimePointNode(result.blockingMarker.time);
        marker["kind"] = TTDExternalEventKindToString(result.blockingMarker.kind);
        marker["reason"] = std::string(result.blockingMarker.reason);
        reply.body["blocking_marker"] = marker;
    }
    reply.body["state"] = TTDSessionStateToString(_manager->GetState());
    if (bookmark)
        reply.body["bookmark"] = *bookmark;
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::StepFrame(bool forward)
{
    if (TTDReply refusal; RefuseWhileRecording(refusal, true))
        return refusal;
    PauseAndConfirm();
    const bool ok = forward ? _manager->StepForwardFrame() : _manager->StepBackFrame();
    NotifyFrameRefresh();
    TTDReply reply;
    reply.body["stepped"] = ok;
    AddPosition(reply.body, _manager->CurrentPosition());
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::Resume(const TTDRequest& request)
{
    // No frame: resume exactly where the machine stands
    TTDTimePoint from = _manager->CurrentPosition();
    if (const std::string* frameText = Option(request, "frame"))
    {
        uint64_t frame = 0;
        uint64_t t = 0;
        const std::string* tText = Option(request, "tinframe");
        if (!ParseU64(*frameText, frame) || (tText && (!ParseU64(*tText, t) || t > UINT32_MAX)))
            return Fail(TTDControlError::BadRequest, "frame and tinframe must be non-negative integers");
        from = {frame, static_cast<uint32_t>(t)};
    }
    else if (Option(request, "tinframe"))
        return Fail(TTDControlError::BadRequest, "tinframe needs a frame");

    const bool ok = _manager->ResumeRecordingFrom(from);
    // Recording again: the machine runs so the capture continues (a seek left it paused)
    Emulator* emulator = _context ? _context->pEmulator : nullptr;
    if (ok && emulator && !OnMachineThread() && emulator->IsRunning() && emulator->IsPaused())
        emulator->Resume();

    TTDReply reply;
    reply.body["resumed"] = ok;
    AddPosition(reply.body, from);
    reply.body["state"] = TTDSessionStateToString(_manager->GetState());
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::StepInstruction(const TTDRequest& request)
{
    bool forward = false;
    if (const std::string* dir = Option(request, "dir"))
    {
        if (*dir == "forward" || *dir == "fwd")
            forward = true;
        else if (*dir != "back")
            return Fail(TTDControlError::BadRequest, "dir must be back or forward");
    }
    if (TTDReply refusal; RefuseWhileRecording(refusal, true))
        return refusal;
    PauseAndConfirm();
    const bool ok = forward ? _manager->StepForwardInstruction() : _manager->StepBackInstruction();
    NotifyFrameRefresh();
    TTDReply reply;
    reply.body["stepped"] = ok;
    reply.body["dir"] = forward ? "forward" : "back";
    AddPosition(reply.body, _manager->CurrentPosition());
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::ReverseStep(const TTDRequest& request)
{
    const std::string* countText = Option(request, "count");
    const std::string* tstatesText = Option(request, "tstates");
    if (countText && tstatesText)
        return Fail(TTDControlError::BadRequest, "Specify exactly one of 'count' or 'tstates' (not both)");
    if (!countText && !tstatesText)
        return Fail(TTDControlError::BadRequest, "Missing required field: 'count' or 'tstates'");
    uint64_t n = 0;
    if (!ParseU64(countText ? *countText : *tstatesText, n) || (countText && n > UINT32_MAX))
        return Fail(TTDControlError::BadRequest, "count and tstates must be non-negative integers");
    if (TTDReply refusal; RefuseWhileRecording(refusal, true))
        return refusal;

    PauseAndConfirm();
    const bool ok = tstatesText ? _manager->ReverseStepTStates(n)
                                : _manager->ReverseStepInstructions(static_cast<uint32_t>(n));
    NotifyFrameRefresh();
    TTDReply reply;
    reply.body["reached"] = ok;
    reply.body["mode"] = tstatesText ? "tstates" : "count";
    AddPosition(reply.body, _manager->CurrentPosition());
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::Markers()
{
    const auto events = _manager->GetExternalEvents().SnapshotEvents();
    TTDReply reply;
    reply.body["count"] = static_cast<uint64_t>(events.size());
    StateNode list = StateNode::Array();
    for (const auto& e : events)
    {
        StateNode marker = TimePointNode(e.time);
        marker["kind"] = TTDExternalEventKindToString(e.kind);
        marker["reason"] = std::string(e.reason);
        list.push(marker);
    }
    reply.body["markers"] = list;
    return reply;
}

// TD-4: agent bookmarks are advisory annotations, never replay barriers. Labels are
// keys: non-empty, at most 63 characters, unique per session
template <class S>
TTDReply TTDControlBackend<S>::Bookmarks()
{
    const auto bookmarks = _manager->GetBookmarks();  // time-sorted snapshot copy
    TTDReply reply;
    reply.body["count"] = static_cast<uint64_t>(bookmarks.size());
    StateNode list = StateNode::Array();
    for (const auto& bm : bookmarks)
    {
        StateNode entry = TimePointNode(bm.time);
        entry["label"] = bm.label;
        list.push(entry);
    }
    reply.body["bookmarks"] = list;
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::BookmarkAdd(const TTDRequest& request)
{
    const std::string* label = Option(request, "label");
    if (!label || label->empty())
        return Fail(TTDControlError::BadRequest,
                    "Missing or empty required field: label (non-empty string, at most 63 characters)");

    // No frame: the current position ("mark here")
    TTDTimePoint time = _manager->CurrentPosition();
    if (const std::string* frameText = Option(request, "frame"))
    {
        uint64_t frame = 0;
        uint64_t t = 0;
        const std::string* tText = Option(request, "tinframe");
        if (!ParseU64(*frameText, frame) || (tText && (!ParseU64(*tText, t) || t > UINT32_MAX)))
            return Fail(TTDControlError::BadRequest, "frame and tinframe must be non-negative integers");
        time = {frame, static_cast<uint32_t>(t)};
    }
    else if (Option(request, "tinframe"))
        return Fail(TTDControlError::BadRequest, "tinframe needs a frame");

    // The label contract is the client's (400); a duplicate label or a position outside
    // the timeline conflicts with the session (409)
    if (label->size() > kMaxBookmarkLabelLength)
        return Fail(TTDControlError::BadRequest, "bookmark label is longer than " +
                                                     std::to_string(kMaxBookmarkLabelLength) +
                                                     " characters");
    std::string err;
    if (!_manager->AddBookmark(time, *label, &err))
        return Fail(TTDControlError::Conflict, err);
    TTDReply reply;
    reply.created = true;
    reply.body["added"] = true;
    reply.body["label"] = *label;
    AddPosition(reply.body, time);
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::BookmarkDelete(const TTDRequest& request)
{
    const std::string* label = Option(request, "label");
    if (!label || label->empty())
        return Fail(TTDControlError::BadRequest, "Missing required field: label");
    if (!_manager->RemoveBookmark(*label))
        return Fail(TTDControlError::NotFound, "Unknown bookmark label: " + *label);
    TTDReply reply;
    reply.body["removed"] = true;
    reply.body["label"] = *label;
    return reply;
}

// "When did the program ..." over the session's port journals (ttdportsearch.h): no
// replay, works on a loaded file. event + arg build the query; every other option
// is a port query option (ApplyPortQueryOption checks the name); file: a .ttd on
// disk, searched without loading it (the session is untouched)
template <class S>
TTDReply TTDControlBackend<S>::PortEvents(const TTDRequest& request)
{
    const std::string* event = Option(request, "event");
    if (!event || event->empty())
    {
        std::string names;
        for (const std::string& n : PortEventNames())
            names += (names.empty() ? "" : ", ") + n;
        return Fail(TTDControlError::BadRequest, "'event' is required: one of " + names);
    }
    const std::string* arg = Option(request, "arg");
    TTDPortQuery q;
    std::string err;
    if (!BuildPortEventQuery(*event, arg ? *arg : std::string(), q, err))
        return Fail(TTDControlError::BadRequest, err);
    for (const auto& [name, value] : request.options)
    {
        if (name == "event" || name == "arg" || name == "file")
            continue;
        if (!ApplyPortQueryOption(q, name, value, err))
            return Fail(TTDControlError::BadRequest, err);
    }

    const std::string* file = Option(request, "file");
    const TTDPortSearchResult result = file ? _manager->SearchPortEventsInFile(*file, q) : _manager->SearchPortEvents(q);
    if (!result.ok)
        return Fail(file ? TTDControlError::BadRequest : TTDControlError::Conflict, result.error);

    TTDReply reply;
    reply.body["event"] = *event;
    reply.body["direction"] = PortDirectionName(q.direction);
    reply.body["count"] = static_cast<uint64_t>(result.hits.size());
    reply.body["truncated"] = result.truncated;
    reply.body["scanned"] = static_cast<uint64_t>(result.scanned);
    StateNode hits = StateNode::Array();
    for (const TTDPortHit& h : result.hits)
    {
        StateNode hit = StateNode::Object();
        hit["index"] = static_cast<uint64_t>(h.index);
        hit["frame"] = h.record.frame;
        hit["tinframe"] = static_cast<unsigned>(h.record.tInFrame);
        hit["port"] = static_cast<unsigned>(h.record.port);
        hit["value"] = static_cast<unsigned>(h.record.value);
        hit["pc"] = static_cast<unsigned>(h.record.pc);
        if (h.ayRegister >= 0)
            hit["ay_register"] = h.ayRegister;
        hits.push(hit);
    }
    reply.body["hits"] = hits;
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::FindLast(const TTDRequest& request)
{
    TTDSearchQuery q;
    const std::string* addr = Option(request, "addr");
    const std::string* addrFrom = Option(request, "addr_from");
    const std::string* addrTo = Option(request, "addr_to");
    const std::string* value = Option(request, "value");
    const std::string* pcFrom = Option(request, "pc_from");
    const std::string* pcTo = Option(request, "pc_to");
    if (!addr && !addrFrom && !addrTo && !pcFrom && !pcTo && !value)
        return Fail(TTDControlError::BadRequest, "Missing search criteria (must supply 'addr', 'addr_from', 'addr_to', "
                                                 "'pc_from', 'pc_to', or 'value')");

    // Numbers: decimal, or hex as 0x.., #.. or $..
    auto number = [&](const std::string* text, const char* name, uint64_t max, uint64_t& out) -> std::string {
        if (!text || ParseUpTo(*text, max, out))
            return "";
        return std::string("'") + name + "' must be 0.." + std::to_string(max) +
               " (decimal, or hex as \"0x..\", \"#..\" or \"$..\")";
    };
    uint64_t n = 0;
    std::string err;
    if (addr)
    {
        if (!(err = number(addr, "addr", 0xFFFF, n)).empty())
            return Fail(TTDControlError::BadRequest, err);
        q.addrFrom = q.addrTo = static_cast<uint16_t>(n);
    }
    else
    {
        if (addrFrom)
        {
            if (!(err = number(addrFrom, "addr_from", 0xFFFF, n)).empty())
                return Fail(TTDControlError::BadRequest, err);
            q.addrFrom = static_cast<uint16_t>(n);
        }
        if (addrTo)
        {
            if (!(err = number(addrTo, "addr_to", 0xFFFF, n)).empty())
                return Fail(TTDControlError::BadRequest, err);
            q.addrTo = static_cast<uint16_t>(n);
        }
    }
    if (const std::string* access = Option(request, "access"))
    {
        if (*access != "write" && *access != "w" && *access != "read" && *access != "execute" && *access != "io")
            return Fail(TTDControlError::BadRequest, "access must be write, read, execute or io");
        q.access = TTDAccessTypeFromString(access->c_str());
    }
    if (value)
    {
        if (!(err = number(value, "value", 0xFF, n)).empty())
            return Fail(TTDControlError::BadRequest, err);
        q.value = static_cast<uint8_t>(n);
        q.hasValueFilter = true;
    }
    if (pcFrom)
    {
        if (!(err = number(pcFrom, "pc_from", 0xFFFF, n)).empty())
            return Fail(TTDControlError::BadRequest, err);
        q.pcFrom = static_cast<uint16_t>(n);
        q.hasPcFilter = true;
    }
    if (pcTo)
    {
        if (!(err = number(pcTo, "pc_to", 0xFFFF, n)).empty())
            return Fail(TTDControlError::BadRequest, err);
        q.pcTo = static_cast<uint16_t>(n);
        q.hasPcFilter = true;
    }
    // Bank-aware search: an address on a banked machine answers for one physical RAM page
    if (const std::string* page = Option(request, "phys_page"))
    {
        if (!ParseUpTo(*page, kPhysPageMax, n))
            return Fail(TTDControlError::BadRequest, "Invalid phys_page (expected 0..255)");
        q.physPage = static_cast<PhysPage>(n);
        q.hasPhysPageFilter = true;
    }
    // Only accesses at or before a point: a frame [+ T-state], or a raw machine time
    if (const std::string* beforeFrame = Option(request, "before_frame"))
    {
        uint64_t f = 0;
        uint64_t t = 0;
        const std::string* beforeTin = Option(request, "before_tin");
        if (!ParseU64(*beforeFrame, f) || (beforeTin && (!ParseU64(*beforeTin, t) || t > UINT32_MAX)))
            return Fail(TTDControlError::BadRequest, "before_frame and before_tin must be non-negative integers");
        q.beforeGlobalT = _manager->GlobalT({f, static_cast<uint32_t>(t)});
    }
    else if (const std::string* before = Option(request, "before"))
    {
        if (!ParseU64(*before, q.beforeGlobalT))
            return Fail(TTDControlError::BadRequest, "before must be a non-negative integer (machine T-states)");
    }

    if (TTDReply refusal; RefuseWhileRecording(refusal, true))
        return refusal;
    PauseAndConfirm();
    TTDExternalEvent marker;
    TTDSearchWindow window;
    const auto found = _manager->FindLastAccess(q, &marker, &window);
    NotifyFrameRefresh();

    TTDReply reply;
    if (found)
    {
        reply.body["found"] = true;
        reply.body["frame"] = found->time.frame;
        reply.body["tinframe"] = static_cast<unsigned>(found->time.tInFrame);
        reply.body["pc"] = static_cast<unsigned>(found->pc);
        reply.body["value"] = static_cast<unsigned>(found->value);
        // null = the access had no RAM page (ROM, cache, I/O)
        reply.body["phys_page"] = found->physPage == kPhysPageNone ? StateNode() : StateNode(static_cast<unsigned>(found->physPage));
        reply.body["access"] = TTDAccessTypeToString(found->access);
    }
    else
    {
        reply.body["found"] = false;
        if (marker.reason[0] != '\0')
        {
            // A replay barrier stopped the search before any match
            reply.body["blocked"] = true;
            reply.body["marker_frame"] = marker.time.frame;
            reply.body["marker_tinframe"] = static_cast<unsigned>(marker.time.tInFrame);
            reply.body["marker_kind"] = TTDExternalEventKindToString(marker.kind);
            reply.body["marker_reason"] = std::string(marker.reason);
        }
    }
    AddSearchWindow(reply.body, window);
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::ReverseContinue(const TTDRequest& request)
{
    const std::string* pcsText = Option(request, "pcs");
    if (!pcsText || pcsText->empty())
        return Fail(TTDControlError::BadRequest, "'pcs' must be a non-empty list of addresses");
    std::vector<uint16_t> pcs;
    size_t start = 0;
    while (start <= pcsText->size())
    {
        size_t end = pcsText->find(',', start);
        if (end == std::string::npos)
            end = pcsText->size();
        std::string item = pcsText->substr(start, end - start);
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);
        uint64_t pc = 0;
        if (!ParseUpTo(item, 0xFFFF, pc))
            return Fail(TTDControlError::BadRequest,
                        "'pcs' entries must be 0..65535 (decimal, or hex as \"0x..\", \"#..\" or \"$..\")");
        pcs.push_back(static_cast<uint16_t>(pc));
        start = end + 1;
    }

    if (TTDReply refusal; RefuseWhileRecording(refusal, true))
        return refusal;
    PauseAndConfirm();
    const auto r = _manager->ReverseContinue(pcs);
    NotifyFrameRefresh();

    TTDReply reply;
    reply.body["matched"] = r.matched;
    reply.body["pc"] = static_cast<unsigned>(r.pc);
    AddPosition(reply.body, r.arrivedAt);
    if (r.blockingMarker.reason[0] != '\0')
    {
        StateNode m = StateNode::Object();
        m["kind"] = TTDExternalEventKindToString(r.blockingMarker.kind);
        m["reason"] = std::string(r.blockingMarker.reason);
        m["frame"] = r.blockingMarker.time.frame;
        m["tinframe"] = static_cast<unsigned>(r.blockingMarker.time.tInFrame);
        reply.body["blocked_by_marker"] = m;
    }
    AddSearchWindow(reply.body, r.window);
    return reply;
}

namespace
{
std::string Hex4(uint16_t v)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "0x%04X", v);
    return buf;
}

/// The coverage queries' shared options: kind, an address range, a physical page.
/// Empty string: all good; otherwise the 400 message
std::string CoverageRange(const TTDRequest& request, TTDCoverageKind& kind, uint16_t& addrFrom, uint16_t& addrTo,
                          std::optional<PhysPage>& page)
{
    uint64_t n = 0;
    if (const std::string* k = Option(request, "kind"); k && !TTDCoverageKindFromString(*k, kind))
        return "Invalid kind: '" + *k + "' (expected executed, written or read)";
    if (const std::string* t = Option(request, "addr_from"))
    {
        if (!ParseUpTo(*t, 0xFFFF, n))
            return "Invalid addr_from: '" + *t + "' (expected 16-bit address)";
        addrFrom = static_cast<uint16_t>(n);
    }
    if (const std::string* t = Option(request, "addr_to"))
    {
        if (!ParseUpTo(*t, 0xFFFF, n))
            return "Invalid addr_to: '" + *t + "' (expected 16-bit address)";
        addrTo = static_cast<uint16_t>(n);
    }
    if (addrFrom > addrTo)
        return "addr_from (" + Hex4(addrFrom) + ") must not exceed addr_to (" + Hex4(addrTo) + ")";
    if (const std::string* t = Option(request, "phys_page"))
    {
        if (!ParseUpTo(*t, kPhysPageMax, n))
            return "Invalid phys_page: '" + *t + "' (expected 0..255)";
        page = static_cast<PhysPage>(n);
    }
    return "";
}

/// from_frame / to_frame / limit of scan and summary; to_frame defaults to the session's end
std::string CoverageWindow(const TTDRequest& request, uint64_t& fromFrame, uint64_t& toFrame, size_t& limit)
{
    uint64_t n = 0;
    if (const std::string* t = Option(request, "from_frame"); t && !ParseU64(*t, fromFrame))
        return "Invalid from_frame: '" + *t + "' (expected unsigned integer)";
    if (const std::string* t = Option(request, "to_frame"); t && !ParseU64(*t, toFrame))
        return "Invalid to_frame: '" + *t + "' (expected unsigned integer)";
    if (const std::string* t = Option(request, "limit"))
    {
        if (!ParseU64(*t, n) || n == 0)
            return "Invalid limit: '" + *t + "' (expected integer >= 1)";
        limit = static_cast<size_t>(n);
    }
    return "";
}
}  // namespace

template <class S>
TTDReply TTDControlBackend<S>::CoverageProbe(const TTDRequest& request)
{
    const std::string* frameText = Option(request, "frame");
    uint64_t frame = 0;
    if (!frameText)
        return Fail(TTDControlError::BadRequest, "Missing required parameter: frame");
    if (!ParseU64(*frameText, frame))
        return Fail(TTDControlError::BadRequest, "Invalid frame: '" + *frameText + "' (expected unsigned integer)");
    TTDCoverageKind kind = TTDCoverageKind::Executed;
    uint16_t addrFrom = 0;
    uint16_t addrTo = 0xFFFF;
    std::optional<PhysPage> page;
    if (const std::string err = CoverageRange(request, kind, addrFrom, addrTo, page); !err.empty())
        return Fail(TTDControlError::BadRequest, err);

    TTDReply reply;
    reply.body["frame"] = frame;
    reply.body["kind"] = TTDCoverageKindToString(kind);
    reply.body["addr_from"] = Hex4(addrFrom);
    reply.body["addr_to"] = Hex4(addrTo);
    if (page)
        reply.body["phys_page"] = static_cast<unsigned>(*page);
    const TTDCoverageProbeResult res = _manager ? _manager->QueryCoverageProbe(frame, kind, addrFrom, addrTo, page)
                                                : TTDCoverageProbeResult{};
    reply.body["touched"] = res.touched;
    reply.body["index_available"] = res.indexAvailable;
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::CoverageScan(const TTDRequest& request)
{
    TTDCoverageKind kind = TTDCoverageKind::Executed;
    uint16_t addrFrom = 0;
    uint16_t addrTo = 0xFFFF;
    std::optional<PhysPage> page;
    uint64_t fromFrame = 0;
    uint64_t toFrame = _manager ? _manager->ReadSessionInfo().currentEndFrame : 0;
    size_t limit = 200;
    std::string err = CoverageWindow(request, fromFrame, toFrame, limit);
    if (err.empty())
        err = CoverageRange(request, kind, addrFrom, addrTo, page);
    if (!err.empty())
        return Fail(TTDControlError::BadRequest, err);

    TTDReply reply;
    reply.body["kind"] = TTDCoverageKindToString(kind);
    reply.body["addr_from"] = Hex4(addrFrom);
    reply.body["addr_to"] = Hex4(addrTo);
    if (page)
        reply.body["phys_page"] = static_cast<unsigned>(*page);
    const TTDCoverageScanResult res =
        _manager ? _manager->QueryCoverageScan(fromFrame, toFrame, kind, addrFrom, addrTo, page, limit)
                 : TTDCoverageScanResult{};
    reply.body["scanned_frames"] = res.scannedFrames;
    reply.body["matching_frames"] = res.matchingFrames;
    reply.body["first_match"] = res.firstMatch;
    reply.body["last_match"] = res.lastMatch;
    reply.body["truncated"] = res.truncated;
    reply.body["index_available"] = res.indexAvailable;
    // The covered window shows a request range that was clamped to the index
    if (res.indexAvailable)
    {
        reply.body["covered_from"] = res.coveredFrom;
        reply.body["covered_to"] = res.coveredTo;
    }
    StateNode frames = StateNode::Array();
    for (uint64_t f : res.frames)
        frames.push(f);
    reply.body["frames"] = frames;
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::CoverageSummary(const TTDRequest& request)
{
    uint64_t fromFrame = 0;
    uint64_t toFrame = _manager ? _manager->ReadSessionInfo().currentEndFrame : 0;
    size_t limit = 100;
    if (const std::string err = CoverageWindow(request, fromFrame, toFrame, limit); !err.empty())
        return Fail(TTDControlError::BadRequest, err);
    std::optional<TTDCoverageKind> kind;
    if (const std::string* k = Option(request, "kind"))
    {
        TTDCoverageKind parsed;
        if (!TTDCoverageKindFromString(*k, parsed))
            return Fail(TTDControlError::BadRequest, "Invalid kind: '" + *k + "' (expected executed, written or read)");
        kind = parsed;
    }
    uint64_t bucketSize = 0;
    if (const std::string* t = Option(request, "bucket_size"); t && !ParseU64(*t, bucketSize))
        return Fail(TTDControlError::BadRequest, "Invalid bucket_size: '" + *t + "' (expected unsigned integer)");

    TTDReply reply;
    reply.body["from_frame"] = fromFrame;
    reply.body["to_frame"] = toFrame;
    TTDCoverageSummaryResult res;
    if (_manager)
        res = _manager->QueryCoverageSummary(fromFrame, toFrame, kind, bucketSize, limit);
    else
        res.bucketSize = 0;
    reply.body["bucket_size"] = res.bucketSize;
    reply.body["bucket_count"] = static_cast<uint64_t>(res.bucketCount);
    reply.body["index_available"] = res.indexAvailable;
    if (res.indexAvailable)
    {
        reply.body["covered_from"] = res.coveredFrom;
        reply.body["covered_to"] = res.coveredTo;
    }
    StateNode buckets = StateNode::Array();
    for (const TTDCoverageSummaryBucket& b : res.buckets)
    {
        StateNode node = StateNode::Object();
        node["frame_start"] = b.frameStart;
        node["frame_end"] = b.frameEnd;
        node["executed_distinct"] = static_cast<unsigned>(b.executedDistinct);
        node["written_distinct"] = static_cast<unsigned>(b.writtenDistinct);
        node["read_distinct"] = static_cast<unsigned>(b.readDistinct);
        node["has_keyframe"] = b.hasKeyframe;
        buckets.push(node);
    }
    reply.body["buckets"] = buckets;
    return reply;
}

StateNode TTDControl::RecordedMachineBody(const TTDRecordedMachine& machine)
{
    return RecordedMachineNodeImpl(machine);
}

StateNode TTDControl::FileInfoBody(const TTDFileInfo& info)
{
    StateNode v = StateNode::Object();
    v["ok"] = true;
    v["path"] = info.path;
    v["file_bytes"] = static_cast<uint64_t>(info.fileBytes);
    v["schema_version"] = static_cast<unsigned>(info.schemaVersion);
    v["flags"] = static_cast<unsigned>(info.flags);
    v["captured_at_unix_ms"] = static_cast<uint64_t>(info.capturedAtUnixMs);
    v["recorded_by"] = StringOrNull(info.emulatorId);
    v["session_state"] = TTDSessionStateToString(static_cast<TTDSessionState>(info.sessionState));
    v["session_start_frame"] = static_cast<uint64_t>(info.startFrame);
    v["session_end_frame"] = static_cast<uint64_t>(info.endFrame);
    v["checkpoint_count"] = static_cast<uint64_t>(info.checkpointCount);
    v["page_slot_count"] = static_cast<uint64_t>(info.pageStoreCount);
    StateNode sections = StateNode::Object();
    sections["write_journal"] = info.hasWriteJournal;
    sections["write_journal_complete"] = info.writeJournalComplete;
    sections["coverage_index"] = info.hasCoverageIndex;
    sections["bookmarks"] = info.hasBookmarks;
    sections["input_journal"] = info.hasInputJournal;
    sections["external_events"] = info.hasExternalEvents;
    sections["port_journals"] = info.hasPortJournals;
    sections["top_clock_time"] = info.topClockTime;
    v["sections"] = sections;
    v["machine"] = RecordedMachineBody(info.machine);
    v["peripherals_from_header"] = info.peripheralsFromHeader;
    return v;
}

template <class S>
TTDReply TTDControlBackend<S>::FileInfo(const TTDRequest& request)
{
    const std::string* path = Option(request, "path");
    if (!path || path->empty())
    {
        StateNode body = StateNode::Object();
        body["ok"] = false;
        return Fail(TTDControlError::BadRequest, "'path' is required", body);
    }
    TTDFileInfo info;
    std::string err;
    if (!ReadTTDFileInfo(*path, info, err))
    {
        StateNode body = StateNode::Object();
        body["ok"] = false;
        body["path"] = *path;
        body["error"] = err;
        return Fail(err.rfind("cannot open", 0) == 0 ? TTDControlError::NotFound : TTDControlError::BadRequest, err,
                    body);
    }
    TTDReply reply;
    reply.body = TTDControl::FileInfoBody(info);
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::Dump(const TTDRequest& request)
{
    const std::string* path = Option(request, "path");
    if (!path || path->empty())
        return Fail(TTDControlError::BadRequest, "Missing 'path'");
    // UTF-8 path: FileHelper turns it into the host's form (non-ASCII names on Windows)
    std::ofstream out(FileHelper::ToFsPath(*path), std::ios::binary);
    if (!out.is_open())
    {
        StateNode body = StateNode::Object();
        body["ok"] = false;
        return Fail(TTDControlError::Conflict, "Cannot open file: " + *path, body);
    }
    std::string err;
    const bool ok = _manager->SerializeSession(out, err);
    const auto bytes = out.tellp();
    StateNode body = StateNode::Object();
    body["ok"] = ok;
    if (!ok)
    {
        body["error"] = err;
        return Fail(TTDControlError::Conflict, err, body);
    }
    body["path"] = *path;
    body["bytes"] = static_cast<int64_t>(bytes);
    TTDReply reply;
    reply.body = body;
    return reply;
}

template <class S>
TTDReply TTDControlBackend<S>::Load(const TTDRequest& request)
{
    const std::string* path = Option(request, "path");
    if (!path || path->empty())
        return Fail(TTDControlError::BadRequest, "Missing 'path'");
    std::ifstream in(FileHelper::ToFsPath(*path), std::ios::binary);
    if (!in.is_open())
    {
        StateNode body = StateNode::Object();
        body["ok"] = false;
        return Fail(TTDControlError::NotFound, "Cannot open file: " + *path, body);
    }
    // A session loads only into an instance of the model it was recorded on: the reason says so
    std::string err;
    _manager->SetSessionSourcePath(*path);
    if (!_manager->DeserializeSession(in, err))
    {
        StateNode body = StateNode::Object();
        body["ok"] = false;
        body["error"] = err;
        return Fail(TTDControlError::BadRequest, err, body);
    }
    const TTDSessionInfo info = _manager->ReadSessionInfo();
    TTDReply reply;
    reply.body["ok"] = true;
    reply.body["path"] = *path;
    reply.body["checkpoint_count"] = static_cast<uint64_t>(info.checkpointCount);
    reply.body["session_start_frame"] = info.sessionStartFrame;
    reply.body["current_end_frame"] = info.currentEndFrame;
    reply.body["state"] = TTDSessionStateToString(info.state);
    return reply;
}

// A frame range as a lossless clip (final picture, plane B when zxdlss is on, frame
// meta), written inside the core: one call instead of a seek and a capture per frame
template <class S>
TTDReply TTDControlBackend<S>::ExportClip(const TTDRequest& request)
{
    const std::string* fromText = Option(request, "from");
    const std::string* toText = Option(request, "to");
    const std::string* path = Option(request, "path");
    if (!fromText || !toText || !path || path->empty())
        return Fail(TTDControlError::BadRequest, "Required fields: from, to, path (absolute directory)");
    TTDClipExportOptions options;
    uint64_t chunk = options.chunkFrames;
    const std::string* chunkText = Option(request, "chunk");
    if (!ParseU64(*fromText, options.fromFrame) || !ParseU64(*toText, options.toFrame) ||
        (chunkText && (!ParseU64(*chunkText, chunk) || chunk == 0 || chunk > UINT32_MAX)))
        return Fail(TTDControlError::BadRequest, "from, to and chunk must be non-negative integers (chunk >= 1)");
    options.directory = *path;
    options.chunkFrames = static_cast<uint32_t>(chunk);
    if (TTDReply refusal; RefuseWhileRecording(refusal))
        return refusal;

    PauseAndConfirm();
    const auto result = _manager->ExportClip(options);
    NotifyFrameRefresh();

    StateNode body = StateNode::Object();
    body["ok"] = result.ok;
    body["frames"] = result.frames;
    body["bytes"] = result.bytesWritten;
    body["planeb"] = result.planeB;
    body["width"] = static_cast<unsigned>(result.width);
    body["height"] = static_cast<unsigned>(result.height);
    body["seconds"] = result.seconds;
    body["path"] = options.directory;
    if (!result.ok)
    {
        body["error"] = result.error;
        return Fail(TTDControlError::BadRequest, result.error, body);
    }
    TTDReply reply;
    reply.body = body;
    return reply;
}

/// endregion </Verbs>

/// region <Machine thread discipline>

template <class S>
bool TTDControlBackend<S>::OnMachineThread() const
{
    return _context && _context->pMainLoop && _context->pMainLoop->IsRunThread();
}

template <class S>
void TTDControlBackend<S>::PauseAndConfirm()
{
    // Only a machine whose own loop runs: a stopped one, or a ZX-Poly member stepped by
    // its master, has nothing to park
    Emulator* emulator = _context ? _context->pEmulator : nullptr;
    if (!emulator || OnMachineThread() || !emulator->IsRunning() || emulator->IsPaused())
        return;
    emulator->Pause();
    emulator->WaitForPauseConfirmation(1000);
}

template <class S>
void TTDControlBackend<S>::NotifyFrameRefresh()
{
    Emulator* emulator = _context ? _context->pEmulator : nullptr;
    if (!emulator)
        return;
    // GetId() (the UUID), not the symbolic id: observers match frames by UUID
    MessageCenter::DefaultMessageCenter().Post(
        NC_VIDEO_FRAME_REFRESH, new EmulatorFramePayload(emulator->GetId(), _context->emulatorState.frame_counter));
}

/// endregion </Machine thread discipline>
}  // namespace ttd
