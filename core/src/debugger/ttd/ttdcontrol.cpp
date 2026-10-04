#include "ttdcontrol.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdbookmarks.h"
#include "debugger/ttd/ttdexternalevents.h"
#include "debugger/ttd/ttdfileinfo.h"
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

/// A non-negative integer, decimal or 0x hex; nothing else (no sign, no fraction)
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

StateNode RecordedMachineNode(const TTDRecordedMachine& m)
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

/// A position in the reply body: frame and tinframe as top-level fields
void AddPosition(StateNode& body, const TTDTimePoint& t)
{
    body["frame"] = t.frame;
    body["tinframe"] = static_cast<unsigned>(t.tInFrame);
}
}  // namespace

/// region <Verbs>

TTDControl::TTDControl(EmulatorContext* context)
    : _context(context), _manager(context ? context->pTimeTravelManager : nullptr)
{
}

const std::vector<std::string>& TTDControl::Verbs()
{
    static const std::vector<std::string> verbs = {
        "status", "start", "stop", "invalidate", "history-limit", "journal", "journal-build", "journal-build-cancel",
        "position", "seek", "step-back", "step-forward", "resume", "step-instruction", "reverse-step",
        "markers", "bookmarks", "bookmark-add", "bookmark-delete"};
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
    for (const auto& [name, value] : request.options)
    {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
        {
            std::string list;
            for (const std::string& o : allowed)
                list += (list.empty() ? "" : ", ") + o;
            return Fail(TTDControlError::BadRequest, "'" + verb + "' has no option '" + name + "'" +
                                                         (list.empty() ? std::string(" (it takes none)")
                                                                       : " (options: " + list + ")"));
        }
    }

    // Status answers without time travel too: it is the capability probe
    if (verb == "status")
        return Status();
    if (!_manager)
        return Fail(TTDControlError::NotAvailable, "TTD engine not available in this build");
    return Run(verb, request);
}

TTDReply TTDControl::Run(const std::string& verb, const TTDRequest& request)
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
    return Fail(TTDControlError::Internal, "verb '" + verb + "' has no implementation");
}

StateNode TTDControl::StatusBody(const TimeTravelManager* manager)
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
    ret["loaded_from_file"] = info.loadedFromFile;
    ret["source_path"] = info.sourcePath;
    ret["captured_at_unix_ms"] = info.capturedAtUnixMs;
    ret["model_id"] = static_cast<unsigned>(info.modelId);
    ret["model_ram_pages"] = static_cast<unsigned>(info.modelRamPages);
    // The recorded machine (null while there is no session) and, for a loaded
    // file, the instance that recorded it
    ret["machine"] = info.checkpointCount != 0 ? RecordedMachineNode(info.machine) : StateNode();
    ret["recorded_by"] = StringOrNull(info.recordedBy);
    ret["write_journal_records"] = static_cast<uint64_t>(info.writeJournalRecords);
    ret["write_journal_bytes"] = static_cast<uint64_t>(info.writeJournalBytes);
    ret["coverage_index_frames"] = static_cast<uint64_t>(info.coverageIndexFrames);
    ret["coverage_index_bytes"] = static_cast<uint64_t>(info.coverageIndexBytes);
    // Why the last session with history was dropped (null when none was):
    // tells an agent why its recording is gone, e.g. a device TTD cannot follow
    ret["last_drop_reason"] = StringOrNull(info.lastDropReason);
    // Why time travel is not available for this machine at all (null when it is)
    ret["unavailable_reason"] = StringOrNull(info.unavailableReason);
    AddWriteJournal(ret, *manager);
    return ret;
}

void TTDControl::AddWriteJournal(StateNode& body, const TimeTravelManager& manager)
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
    const TimeTravelManager::JournalBuildState build = manager.GetJournalBuildState();
    StateNode b = StateNode::Object();
    b["active"] = build.active;
    b["done"] = build.done;
    b["total"] = build.total;
    body["write_journal_build"] = b;
}

TTDReply TTDControl::Status()
{
    TTDReply reply;
    reply.body = StatusBody(_manager);
    return reply;
}

TTDReply TTDControl::Start(const TTDRequest& request)
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

TTDReply TTDControl::Stop()
{
    const bool wasRecording = _manager->IsRecording();
    _manager->StopRecording();
    TTDReply reply;
    reply.body["stopped"] = wasRecording;
    reply.body["state"] = TTDSessionStateToString(_manager->GetState());
    return reply;
}

TTDReply TTDControl::Invalidate(const TTDRequest& request)
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

TTDReply TTDControl::HistoryLimit(const TTDRequest& request)
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

TTDReply TTDControl::Journal(const TTDRequest& request)
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
    AddWriteJournal(reply.body, *_manager);
    // The choice for recordings, also without a session (write_journal_enabled: recorded now)
    reply.body["write_journal_setting"] = _manager->GetEnableWriteJournal();
    reply.body["write_journal_records"] = static_cast<uint64_t>(_manager->GetSessionInfo().writeJournalRecords);
    return reply;
}

TTDReply TTDControl::JournalBuild(const TTDRequest& request)
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
    AddWriteJournal(body, *_manager);
    if (!r.ok)
        return Fail(TTDControlError::Conflict, r.error, body);
    TTDReply reply;
    reply.body = body;
    return reply;
}

TTDReply TTDControl::JournalBuildCancel()
{
    const bool active = _manager->GetJournalBuildState().active;
    _manager->CancelJournalBuild();
    TTDReply reply;
    reply.body["cancelled"] = active;
    return reply;
}

bool TTDControl::RefuseWhileRecording(TTDReply& reply) const
{
    if (!_manager->IsRecording())
        return false;
    StateNode body = StateNode::Object();
    body["state"] = TTDSessionStateToString(_manager->GetState());
    reply = Fail(TTDControlError::Conflict,
                 "Cannot scrub while recording is active - stop the recording first. Scrubbing during "
                 "recording would overwrite live emulator state with restored checkpoint data and corrupt "
                 "the timeline.",
                 body);
    return true;
}

TTDReply TTDControl::Position()
{
    TTDReply reply;
    reply.body["current"] = TimePointNode(_manager->CurrentPosition());
    reply.body["session_end"] = TimePointNode(_manager->SessionEndPosition());
    reply.body["state"] = TTDSessionStateToString(_manager->GetState());
    return reply;
}

TTDReply TTDControl::Seek(const TTDRequest& request)
{
    if (TTDReply refusal; RefuseWhileRecording(refusal))
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

    // Park the machine so its thread cannot advance past the restored checkpoint; it
    // stays paused at the target (Detached) until resume
    PauseAndConfirm();
    TimeTravelManager::TTDSeekResult result;
    const bool reached = _manager->SeekTo(target, &result);
    NotifyFrameRefresh();

    TTDReply reply;
    reply.body["reached"] = reached;
    reply.body["arrived_at"] = TimePointNode(result.arrivedAt);
    const char* reason = "target";
    if (result.haltReason == TimeTravelManager::TTDSeekHaltReason::ExternalEvent)
        reason = "external_event";
    else if (result.haltReason == TimeTravelManager::TTDSeekHaltReason::OutOfRange)
        reason = "out_of_range";
    reply.body["halt_reason"] = reason;
    if (result.haltReason == TimeTravelManager::TTDSeekHaltReason::ExternalEvent)
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

TTDReply TTDControl::StepFrame(bool forward)
{
    if (TTDReply refusal; RefuseWhileRecording(refusal))
        return refusal;
    PauseAndConfirm();
    const bool ok = forward ? _manager->StepForwardFrame() : _manager->StepBackFrame();
    NotifyFrameRefresh();
    TTDReply reply;
    reply.body["stepped"] = ok;
    AddPosition(reply.body, _manager->CurrentPosition());
    return reply;
}

TTDReply TTDControl::Resume(const TTDRequest& request)
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
    if (ok && emulator && !OnMachineThread())
        emulator->Resume();

    TTDReply reply;
    reply.body["resumed"] = ok;
    AddPosition(reply.body, from);
    reply.body["state"] = TTDSessionStateToString(_manager->GetState());
    return reply;
}

TTDReply TTDControl::StepInstruction(const TTDRequest& request)
{
    bool forward = false;
    if (const std::string* dir = Option(request, "dir"))
    {
        if (*dir == "forward" || *dir == "fwd")
            forward = true;
        else if (*dir != "back")
            return Fail(TTDControlError::BadRequest, "dir must be back or forward");
    }
    if (TTDReply refusal; RefuseWhileRecording(refusal))
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

TTDReply TTDControl::ReverseStep(const TTDRequest& request)
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
    if (TTDReply refusal; RefuseWhileRecording(refusal))
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

TTDReply TTDControl::Markers()
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
TTDReply TTDControl::Bookmarks()
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

TTDReply TTDControl::BookmarkAdd(const TTDRequest& request)
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

TTDReply TTDControl::BookmarkDelete(const TTDRequest& request)
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

/// endregion </Verbs>

/// region <Machine thread discipline>

bool TTDControl::OnMachineThread() const
{
    return _context && _context->pMainLoop && _context->pMainLoop->IsRunThread();
}

void TTDControl::PauseAndConfirm()
{
    Emulator* emulator = _context ? _context->pEmulator : nullptr;
    if (!emulator || OnMachineThread())
        return;
    emulator->Pause();
    emulator->WaitForPauseConfirmation(1000);
}

void TTDControl::NotifyFrameRefresh()
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
