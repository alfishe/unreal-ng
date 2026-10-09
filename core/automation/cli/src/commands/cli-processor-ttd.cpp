/// @file cli-processor-ttd.cpp
/// @brief CLI Time-Travel Debug (TTD) command handlers.
///
/// Exposes the full TTD engine surface (Phase 2 complete) to CLI automation:
///   ttd status                 — session info (state, frame range, checkpoint count)
///   ttd start [--journal]      — begin recording (--journal: record the write journal too)
///   ttd journal [on|off|build [from] [to]] — the write journal: switch it at any moment, or build it by replay
///   ttd stop                   — stop recording (history retained, browsable)
///   ttd invalidate [reason]    — drop all history, return to Idle
///   ttd seek <frame> [tinframe] — seek to a point in the timeline
///   ttd step-back              — step back one frame (preserve intra-frame pos)
///   ttd step-forward           — step forward one frame (preserve intra-frame pos)
///   ttd resume [frame] [tin]   — resume recording from current or specified point
///   ttd position               — show current TTDTimePoint (frame + tInFrame)
///   ttd markers                — list external-event markers (replay barriers)
///   ttd bookmark [add|del|list] — agent bookmarks (advisory, never barriers)
///   ttd seek --bookmark <label> — seek to a bookmarked position
///
/// All commands operate on the currently selected emulator instance. The TTD
/// engine requires the emulator to be paused for seek/step/resume operations
/// (existing pause discipline — same as RestoreCheckpointForTesting).

#include "cli-processor.h"

#include <algorithm>
#include <cctype>

#include <debugger/ttd/timetravelmanager.h>
#include <debugger/ttd/ttdcontrol.h>
#include <debugger/ttd/ttdsession.h>
#include <debugger/ttd/ttdbookmarks.h>
#include <debugger/ttd/ttdexternalevents.h>
#include <debugger/ttd/ttdfileinfo.h>
#include <debugger/ttd/machinestatehash.h>
#include <debugger/ttd/ttdprobe.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>

#include <fstream>
#include <iomanip>
#include <sstream>

namespace
{
/// "frames 10:0 .. 24:71680, 40:1200 .. 60:0" or "none"
std::string DescribeJournalSpans(const ttd::TTDSessionInfo& info)
{
    if (info.writeJournalSpans.empty())
        return "nothing (write searches replay)";
    std::stringstream ss;
    if (info.writeJournalComplete)
        ss << "the whole session (";
    else
        ss << info.writeJournalSpans.size() << " span(s), searches outside them replay (";
    for (size_t i = 0; i < info.writeJournalSpans.size(); ++i)
    {
        const auto& [from, to] = info.writeJournalSpans[i];
        if (i == 4)
        {
            ss << ", ...";
            break;
        }
        ss << (i ? ", " : "") << "frame " << from.frame << ":" << from.tInFrame << " .. " << to.frame << ":" << to.tInFrame;
    }
    ss << ")";
    return ss.str();
}
}  // namespace


/// region <TTD Commands>


/// TD-8: the part of history a backward search examined, one line (a TTDControl
/// reply's covered_* fields)
static std::string FormatSearchWindow(const StateNode& body)
{
    const StateNode* from = body.find("covered_from");
    if (!from)
        return {};
    std::stringstream ss;
    ss << "  Searched: frame " << from->i << " t=" << body.find("covered_from_tinframe")->i << " .. frame "
       << body.find("covered_to")->i << " t=" << body.find("covered_to_tinframe")->i << CLIProcessor::NEWLINE;
    return ss.str();
}

namespace
{

/// The recorded machine as indented lines (ttd info <path>, ttd status).
std::string FormatRecordedMachine(const ttd::TTDRecordedMachine& m)
{
    std::stringstream ss;
    ss << "  Model:                  " << (m.model.empty() ? "unknown" : m.model) << " (id "
       << static_cast<unsigned>(m.modelId) << "), RAM page bound " << m.ramPageBound << CLIProcessor::NEWLINE;
    ss << "  ROM signature:          ";
    if (m.romSignature == 0)
        ss << "unknown (not checked)";
    else
        ss << "0x" << ttd::HashToString(m.romSignature);
    ss << CLIProcessor::NEWLINE;
    ss << "  General Sound:          " << ttd::GeneralSoundName(m.generalSound) << CLIProcessor::NEWLINE;
    ss << "  TurboSound slot:        " << m.turboSound << CLIProcessor::NEWLINE;
    ss << "  Devices:                ";
    if (m.peripherals.empty())
        ss << "none";
    for (size_t i = 0; i < m.peripherals.size(); ++i)
        ss << (i ? ", " : "") << m.peripherals[i];
    ss << CLIProcessor::NEWLINE;
    if (!m.notRecorded.empty())
    {
        ss << "  Not recorded:           ";
        for (size_t i = 0; i < m.notRecorded.size(); ++i)
            ss << (i ? ", " : "") << m.notRecorded[i];
        ss << " (fitted, run live through seeks)" << CLIProcessor::NEWLINE;
    }
    return ss.str();
}

}  // namespace

/// ttd info <path>: a .ttd file's header, sections and recorded machine, read
/// without loading it (no emulator needed).
void CLIProcessor::HandleTTDFileInfo(const ClientSession& session, const std::string& path)
{
    ttd::TTDFileInfo info;
    std::string err;
    if (!ttd::ReadTTDFileInfo(path, info, err))
    {
        session.SendResponse("Error: " + err + NEWLINE);
        return;
    }
    std::stringstream ss;
    ss << "TTD File" << NEWLINE;
    ss << "========" << NEWLINE;
    ss << NEWLINE;
    ss << "  Path:                   " << info.path << NEWLINE;
    ss << "  Size:                   " << info.fileBytes << " bytes" << NEWLINE;
    ss << "  Schema:                 v" << info.schemaVersion << ", flags 0x" << std::hex << std::setw(4)
       << std::setfill('0') << info.flags << std::dec << std::setfill(' ') << NEWLINE;
    if (info.capturedAtUnixMs != 0)
        ss << "  Captured at:            " << info.capturedAtUnixMs << " (unix ms)" << NEWLINE;
    if (!info.emulatorId.empty())
        ss << "  Recorded by:            " << info.emulatorId << NEWLINE;
    ss << "  Frames:                 " << info.startFrame << " .. " << info.endFrame << NEWLINE;
    ss << "  Checkpoints:            " << info.checkpointCount << " (" << info.pageStoreCount << " page slots)"
       << NEWLINE;
    ss << NEWLINE;
    ss << FormatRecordedMachine(info.machine);
    ss << "  Device set from:        "
       << (info.peripheralsFromHeader ? "header" : "first checkpoint (file written before the header mask)")
       << NEWLINE;
    ss << NEWLINE;
    ss << "  Sections:               ";
    const std::pair<bool, const char*> sections[] = {
        {info.hasWriteJournal, "write-journal"}, {info.writeJournalComplete, "journal-complete"},
        {info.hasCoverageIndex, "coverage-index"}, {info.hasBookmarks, "bookmarks"},
        {info.hasInputJournal, "input-journal"},   {info.hasExternalEvents, "external-events"},
        {info.hasPortJournals, "port-journals"},   {info.topClockTime, "top-clock-time"}};
    bool any = false;
    for (const auto& [present, name] : sections)
        if (present)
        {
            ss << (any ? ", " : "") << name;
            any = true;
        }
    ss << (any ? "" : "none") << NEWLINE;
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTTD(const ClientSession& session, const std::vector<std::string>& args)
{
    // ttd info <path> reads a file: no emulator needed
    if (args.size() >= 2 && (args[0] == "info" || args[0] == "file-info"))
    {
        HandleTTDFileInfo(session, args[1]);
        return;
    }

    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        return;
    }

    auto context = emulator->GetContext();
    if (!context)
    {
        session.SendResponse(std::string("Error: Emulator context not available") + NEWLINE);
        return;
    }

    if (!ttd::HasTimeTravelSession(context))
    {
        session.SendResponse(std::string("Error: TTD engine not available in this build") + NEWLINE);
        return;
    }

    if (args.empty())
    {
        ShowTTDHelp(session);
        return;
    }

    std::string subcommand = args[0];

    if (subcommand == "status" || subcommand == "info")
    {
        HandleTTDStatus(session, context);
    }
    else if (subcommand == "start" || subcommand == "record")
    {
        HandleTTDStart(session, context, args);
    }
    else if (subcommand == "journal")
    {
        HandleTTDJournal(session, context, args);
    }
    else if (subcommand == "stop")
    {
        HandleTTDStop(session, context);
    }
    else if (subcommand == "limit" || subcommand == "history-limit")
    {
        HandleTTDHistoryLimit(session, context, args);
    }
    else if (subcommand == "invalidate" || subcommand == "clear" || subcommand == "reset")
    {
        HandleTTDInvalidate(session, context, args);
    }
    else if (subcommand == "seek" || subcommand == "goto")
    {
        HandleTTDSeek(session, context, args);
    }
    else if (subcommand == "step-back" || subcommand == "back" || subcommand == "sb")
    {
        HandleTTDStepBack(session, context);
    }
    else if (subcommand == "step-forward" || subcommand == "forward" || subcommand == "sf")
    {
        HandleTTDStepForward(session, context);
    }
    else if (subcommand == "resume")
    {
        HandleTTDResume(session, context, args);
    }
    else if (subcommand == "position" || subcommand == "pos")
    {
        HandleTTDPosition(session, context);
    }
    else if (subcommand == "markers" || subcommand == "barriers")
    {
        HandleTTDMarkers(session, context);
    }
    else if (subcommand == "bookmark" || subcommand == "bookmarks" || subcommand == "bm")
    {
        HandleTTDBookmark(session, context, args);
    }
    else if (subcommand == "dump" || subcommand == "save")
    {
        HandleTTDDump(session, context, args);
    }
    else if (subcommand == "load" || subcommand == "open")
    {
        HandleTTDLoad(session, context, args);
    }
    else if (subcommand == "export-clip" || subcommand == "clip")
    {
        HandleTTDExportClip(session, context, args);
    }
    else if (subcommand == "find-last" || subcommand == "fl")
    {
        HandleTTDFindLast(session, context, args);
    }
    else if (subcommand == "port-events" || subcommand == "pe")
    {
        HandleTTDPortEvents(session, context, args);
    }
    else if (subcommand == "step-instruction" ||
             subcommand == "si-back"    ||
             subcommand == "si-forward")
    {
        HandleTTDStepInstruction(session, context, args);
    }
    else if (subcommand == "reverse-step"   ||
             subcommand == "reverse-continue" ||
             subcommand == "rs"             ||
             subcommand == "rc")
    {
        if (subcommand == "reverse-continue" || subcommand == "rc")
            HandleTTDReverseContinue(session, context, args);
        else
            HandleTTDReverseStep(session, context, args);
    }
    else if (subcommand == "coverage" || subcommand == "cov")
    {
        HandleTTDCoverage(session, context, args);
    }
    else if (subcommand == "memory-at" || subcommand == "mat" || subcommand == "memory-diff" || subcommand == "mdiff")
    {
        HandleTTDMemoryAt(session, context, args);
    }
    else if (subcommand == "help" || subcommand == "?")
    {
        ShowTTDHelp(session);
    }
    else
    {
        session.SendResponse(std::string("Error: Unknown TTD subcommand '") + args[0] + "'" + NEWLINE +
                             "Use 'ttd' without arguments to see available subcommands." + NEWLINE);
    }
}

void CLIProcessor::ShowTTDHelp(const ClientSession& session)
{
    std::stringstream ss;
    ss << "Time-Travel Debug (TTD) Commands" << NEWLINE;
    ss << "=================================" << NEWLINE;
    ss << NEWLINE;
    ss << "  ttd status                       Show session info (state, frames, checkpoints; alias: info)" << NEWLINE;
    ss << "  ttd info <path>                  Describe a .ttd file without loading it: frames, sections and" << NEWLINE;
    ss << "                                     the recorded machine (model, ROM, General Sound card, devices)" << NEWLINE;
    ss << "  ttd start [--journal]            Begin recording (captures baseline checkpoint)" << NEWLINE;
    ss << "  ttd start --black-box [--minutes N]  Record as the black box: the last N minutes (default 5)" << NEWLINE;
    ss << "                                     --journal: also record the write journal (off by default)" << NEWLINE;
    ss << "  ttd journal [on|off]             The write journal: who wrote an address last, at once." << NEWLINE;
    ss << "                                     Switch it at any moment, also while recording; each on-off" << NEWLINE;
    ss << "                                     span is a segment. No argument: its state and segments" << NEWLINE;
    ss << "  ttd journal build [from] [to]    Build it by replaying frames from..to (default: the whole" << NEWLINE;
    ss << "                                     session); about 2-4 ms per frame. Not while recording" << NEWLINE;
    ss << "  ttd stop                         Stop recording (history retained, browsable)" << NEWLINE;
    ss << "  ttd limit [frames <n>] [bytes <n>[K|M|G]]" << NEWLINE;
    ss << "                                   Bound the history: while recording, the oldest frames are" << NEWLINE;
    ss << "                                     released beyond it (0 = no limit; 'ttd limit off' clears both;" << NEWLINE;
    ss << "                                     no arguments: show the limit and what it holds)" << NEWLINE;
    ss << "  ttd invalidate [reason]          Drop all history, return to Idle" << NEWLINE;
    ss << "  ttd seek <frame> [tinframe]      Seek to a point in the timeline" << NEWLINE;
    ss << "  ttd step-back                    Step back one frame (preserve intra-frame pos)" << NEWLINE;
    ss << "  ttd step-forward                 Step forward one frame (preserve intra-frame pos)" << NEWLINE;
    ss << "  ttd resume [frame] [tinframe]    Resume recording from current or specified point" << NEWLINE;
    ss << "  ttd position                     Show current TTDTimePoint (frame + tInFrame)" << NEWLINE;
    ss << "  ttd markers                      List external-event markers (replay barriers)" << NEWLINE;
    ss << "  ttd bookmark [list]              List agent bookmarks (advisory, never barriers)" << NEWLINE;
    ss << "  ttd bookmark add <l> [f] [t]     Add bookmark <label> at frame/tinframe (default: here)" << NEWLINE;
    ss << "  ttd bookmark del <label>         Remove a bookmark by label" << NEWLINE;
    ss << "  ttd seek --bookmark <label>      Seek to a bookmarked position" << NEWLINE;
    ss << NEWLINE;
    ss << "Phase 4 — Reverse Search + Automation:" << NEWLINE;
    ss << "  ttd dump <path>                  Serialize session to .ttd file" << NEWLINE;
    ss << "  ttd load <path>                  Load a .ttd session for playback (alias: open)" << NEWLINE;
    ss << "  ttd export-clip <from> <to> <dir> [--chunk N]" << NEWLINE;
    ss << "                                   Write frames from..to as a lossless clip into dir (alias: clip)" << NEWLINE;
    ss << "  ttd find-last --addr <A>         Reverse search: find last access at address" << NEWLINE;
    ss << "    [--access write|read|execute|io]  (default: write)" << NEWLINE;
    ss << "    [--value V] [--pc-from X] [--pc-to Y]" << NEWLINE;
    ss << "    [--before-frame F] [--before-tin T] [--space ram|vram|cache]" << NEWLINE;
    ss << "  ttd memory-at --space S --frame F [--offset O] [--length N]   (alias: mat)" << NEWLINE;
    ss << "                                   A memory at a past checkpoint, without seeking (S: ram, ramN, or a" << NEWLINE;
    ss << "                                   region: vram, cache, neogs.ram, ... - memory regions lists them)" << NEWLINE;
    ss << "  ttd memory-diff --space S --from F1 --to F2 [--limit N]       (alias: mdiff)" << NEWLINE;
    ss << "                                   The bytes of a memory that differ between two checkpoints" << NEWLINE;
    ss << "  ttd port-events <event> [arg] [option=value ...]   (alias: pe)" << NEWLINE;
    ss << "                                   When did the program ... - from the port journals, no replay:" << NEWLINE;
    ss << "    key [KEY]       saw a key down (KEY: a, enter, space, caps, symbol...; none: any key)" << NEWLINE;
    ss << "    ear             saw the tape (EAR) signal change" << NEWLINE;
    ss << "    ay-read [R]  ay-write [R]  ay-select [R]   AY accesses (R: register 0..15)" << NEWLINE;
    ss << "    border  beeper  OUT #FE changed the border color / beeper bit" << NEWLINE;
    ss << "    in  out         every IN / OUT, narrowed by port=, port_mask=, value=, value_mask=" << NEWLINE;
    ss << "    options: limit=N newest=true from=F[:T] to=F[:T] match=any|equals|any-clear|any-set" << NEWLINE;
    ss << "             trigger=every|rising|change stream_mask=M ay_register=R" << NEWLINE;
    ss << "             file=<path.ttd>  search a saved session without loading it" << NEWLINE;
    ss << "  ttd step-instruction <back|fwd>  Step one instruction (aliases: si-back, si-forward)" << NEWLINE;
    ss << NEWLINE;
    ss << "Phase 4 — Reverse Execution:" << NEWLINE;
    ss << "  ttd reverse-step [--count N]     Step back N instructions (default: 1)" << NEWLINE;
    ss << "    [--tstates T]                     Step back T t-states (aligns to M1)" << NEWLINE;
    ss << "  ttd reverse-continue --pc <A>    Run backward until any PC matches" << NEWLINE;
    ss << "    [--pc <B> ...]                    (repeat --pc for multiple breakpoints)" << NEWLINE;
    ss << "  Aliases: rs=reverse-step, rc=reverse-continue" << NEWLINE;
    ss << NEWLINE;
    ss << "Notes:" << NEWLINE;
    ss << "  - Seek/step/resume require the emulator to be paused." << NEWLINE;
    ss << "  - Frame indices are absolute (from session start)." << NEWLINE;
    ss << "  - 'tinframe' is the intra-frame t-state offset (default: 0)." << NEWLINE;

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTTDStatus(const ClientSession& session, EmulatorContext* context)
{
    const ttd::TTDSessionView view(context);
    const ttd::TTDSessionView* mgr = &view;
    ttd::TTDSessionInfo info = mgr->ReadSessionInfo();

    std::stringstream ss;
    ss << "TTD Session Status" << NEWLINE;
    ss << "==================" << NEWLINE;
    ss << NEWLINE;
    ss << "  Origin:                 "
       << (info.loadedFromFile ? "loaded from file" : "recorded in this process") << NEWLINE;
    if (info.loadedFromFile && !info.sourcePath.empty())
        ss << "  Source:                 " << info.sourcePath << NEWLINE;
    if (info.capturedAtUnixMs != 0)
        ss << "  Captured at:            " << info.capturedAtUnixMs << " (unix ms)" << NEWLINE;
    if (info.checkpointCount != 0)
        ss << FormatRecordedMachine(info.machine);
    else
        ss << "  Model:                  id=" << static_cast<unsigned>(info.modelId)
           << ", RAM page bound " << info.modelRamPages << NEWLINE;
    if (!info.recordedBy.empty())
        ss << "  Recorded by:            " << info.recordedBy << NEWLINE;
    ss << "  State:                  " << ttd::TTDSessionStateToString(info.state) << NEWLINE;
    ss << "  Session start frame:    " << info.sessionStartFrame << NEWLINE;
    ss << "  Current end frame:      " << info.currentEndFrame << NEWLINE;
    ss << "  Checkpoint count:       " << info.checkpointCount << NEWLINE;
    ss << "  Page store capacity:    " << info.pageStoreBytes << " bytes" << NEWLINE;
    ss << "  Page store used:        " << info.pageStoreUsedBytes << " bytes" << NEWLINE;
    ss << "  Baseline frames cap'd:  " << info.baselineFramesCaptured << NEWLINE;
    ss << "  Session heap total:     " << info.sessionHeapBytes << " bytes" << NEWLINE;
    ss << "  History limit:          ";
    if (info.historyLimitFrames == 0 && info.historyLimitBytes == 0)
        ss << "none";
    if (info.historyLimitFrames != 0)
        ss << info.historyLimitFrames << " frames ";
    if (info.historyLimitBytes != 0)
        ss << info.historyLimitBytes << " bytes";
    ss << " (" << info.historyBytes << " bytes held, " << info.evictedCheckpoints << " oldest released)" << NEWLINE;
    ss << NEWLINE;
    ss << "  Write journal:          " << (info.writeJournalEnabled ? "on" : "off") << ", "
       << info.writeJournalRecords << " records (" << info.writeJournalBytes << " bytes in memory)" << NEWLINE;
    ss << "  Journal covers:         " << DescribeJournalSpans(info) << NEWLINE;
    if (info.coverageIndexFrames != 0)
    {
        ss << "  Coverage index:         " << info.coverageIndexFrames << " frames ("
           << info.coverageIndexBytes << " bytes)" << NEWLINE;
    }
    else
    {
        ss << "  Coverage index:         absent (reverse queries fall back to replay)"
           << NEWLINE;
    }
    ss << "  Bookmarks:              " << info.bookmarkCount << " (advisory, never barriers)" << NEWLINE;
    ss << "  Input events:           " << info.inputEventCount << NEWLINE;
    ss << "  Replay barriers:        " << info.externalEventCount << NEWLINE;
    if (!info.inputHistoryComplete)
        ss << "  Input history:          incomplete (the file predates saved input: in-frame replay may "
              "differ from the recording)"
           << NEWLINE;
    if (info.portJournalActive)
        ss << "  Port journals:          " << info.portReadCount << " IN, " << info.portWriteCount << " OUT ("
           << info.portJournalBytes << " bytes), replay isolated from media and host devices" << NEWLINE;
    else if (!info.portJournalOffReason.empty())
        ss << "  Port journals:          off - " << info.portJournalOffReason << NEWLINE;
    if (info.portReplayValueMismatches != 0 || info.portReplayDivergences != 0)
        ss << "  Replay mismatches:      " << info.portReplayValueMismatches
           << " device answer(s) (the CPU got the recorded values), " << info.portReplayDivergences
           << " divergence(s)" << NEWLINE;
    if (!info.lastDropReason.empty())
        ss << "  Last session dropped:   " << info.lastDropReason << NEWLINE;
    if (!info.lastStopReason.empty())
        ss << "  Recording stopped by:   " << info.lastStopReason << NEWLINE;
    if (info.recordingPaused)
        ss << "  Recording paused:       yes (browsing; resume at its end goes on, stop ends it)" << NEWLINE;
    if (!info.unavailableReason.empty())
        ss << "  Not available:          " << info.unavailableReason << NEWLINE;

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTTDStart(const ClientSession& session, EmulatorContext* context,
                                   const std::vector<std::string>& args)
{
    const ttd::TTDSessionView view(context);
    const ttd::TTDSessionView* mgr = &view;
    if (mgr->IsRecording())
    {
        session.SendResponse(std::string("TTD: Already recording (no-op)") + NEWLINE);
        return;
    }

    // The write journal is off unless asked for (D40)
    std::map<std::string, std::string> options{{"journal", "false"}};
    for (size_t i = 1; i < args.size(); ++i)
    {
        if (args[i] == "--journal" || args[i] == "-j")
            options["journal"] = "true";
        else if (args[i] == "--black-box")
            options["black_box"] = "true";
        else if (args[i] == "--minutes" && i + 1 < args.size())
            options["minutes"] = args[++i];
        else
        {
            session.SendResponse("TTD: unknown option '" + args[i] +
                                 "' (ttd start [--journal] [--black-box [--minutes N]])" + NEWLINE);
            return;
        }
    }

    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"start", options});
    if (reply.Ok() && reply.body.find("started")->b)
    {
        std::string text = options["journal"] == "true" ? "TTD: Recording started (with the write journal)"
                                                        : "TTD: Recording started";
        if (options.count("black_box"))
            text += std::string(" as the black box: the last ") +
                    (options.count("minutes") ? options["minutes"] : std::string("5")) +
                    " minutes, turbo and host speed stay free";
        session.SendResponse(text + NEWLINE);
    }
    else
        session.SendResponse(std::string("TTD: Failed to start recording") +
                             (reply.message.empty() ? "" : ": " + reply.message) + NEWLINE);
}


void CLIProcessor::HandleTTDJournal(const ClientSession& session, EmulatorContext* context,
                                     const std::vector<std::string>& args)
{
    const ttd::TTDSessionView view(context);
    const ttd::TTDSessionView* mgr = &view;
    const std::string action = args.size() > 1 ? args[1] : "status";
    if (action == "on" || action == "off")
    {
        const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"journal", {{"enabled", action}}});
        if (!reply.Ok())
        {
            session.SendResponse("Error: " + reply.message + NEWLINE);
            return;
        }
        session.SendResponse(std::string("TTD: write journal ") + action +
                             (mgr->IsRecording() ? " (a segment " + std::string(action == "on" ? "starts" : "ends") +
                                                       " here)"
                                                 : "") +
                             NEWLINE);
        return;
    }
    if (action == "build")
    {
        std::map<std::string, std::string> options;
        if (args.size() > 2)
            options["from_frame"] = args[2];
        if (args.size() > 3)
            options["to_frame"] = args[3];
        const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"journal-build", options});
        if (reply.error == ttd::TTDControlError::BadRequest)
        {
            session.SendResponse(std::string("TTD: usage: ttd journal build [from-frame] [to-frame]") + NEWLINE);
            return;
        }
        if (!reply.Ok())
        {
            session.SendResponse("TTD: cannot build the write journal: " + reply.message + NEWLINE);
            return;
        }
        const StateNode& b = reply.body;
        std::stringstream ss;
        ss << "TTD: write journal built for " << b.find("frames_built")->i << " frame(s), " << b.find("records")->i
           << " writes";
        if (b.find("frames_covered")->i)
            ss << "; " << b.find("frames_covered")->i << " already covered";
        if (b.find("frames_refused")->i)
            ss << "; " << b.find("frames_refused")->i << " not replayable (a marker without its data)";
        if (b.find("cancelled")->b)
            ss << "; cancelled";
        ss << NEWLINE << "  Journal covers: " << DescribeJournalSpans(mgr->GetSessionInfo()) << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }
    if (action != "status")
    {
        session.SendResponse("TTD: unknown journal action '" + action + "' (on, off, build, status)" + NEWLINE);
        return;
    }
    const ttd::TTDSessionInfo info = mgr->GetSessionInfo();
    session.SendResponse(std::string("Write journal: ") + (info.writeJournalEnabled ? "on" : "off") + ", " +
                         std::to_string(info.writeJournalRecords) + " records" + NEWLINE +
                         "  Covers: " + DescribeJournalSpans(info) + NEWLINE);
}

namespace
{
/// "1234", "512K", "64M", "4G" -> bytes; false when it is not a number
bool ParseTTDByteCount(const std::string& text, uint64_t& out)
{
    if (text.empty())
        return false;
    uint64_t multiplier = 1;
    std::string digits = text;
    const char suffix = static_cast<char>(std::toupper(static_cast<unsigned char>(text.back())));
    if (suffix == 'K' || suffix == 'M' || suffix == 'G')
    {
        multiplier = suffix == 'K' ? 1024ull : suffix == 'M' ? 1024ull * 1024 : 1024ull * 1024 * 1024;
        digits.pop_back();
    }
    if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos || digits.size() > 15)
        return false;
    out = std::stoull(digits) * multiplier;
    return true;
}
}  // namespace

void CLIProcessor::HandleTTDHistoryLimit(const ClientSession& session, EmulatorContext* context,
                                         const std::vector<std::string>& args)
{
    const ttd::TTDSessionView view(context);
    const ttd::TTDSessionView* mgr = &view;
    ttd::TTDSessionInfo info = mgr->ReadSessionInfo();
    uint64_t frames = info.historyLimitFrames;
    uint64_t bytes = info.historyLimitBytes;
    bool change = false;
    if (args.size() == 2 && args[1] == "off")
    {
        frames = bytes = 0;
        change = true;
    }
    else
    {
        for (size_t i = 1; i < args.size(); i += 2)
        {
            uint64_t value = 0;
            if (i + 1 >= args.size() || !ParseTTDByteCount(args[i + 1], value) ||
                (args[i] != "frames" && args[i] != "bytes") ||
                (args[i] == "frames" && args[i + 1].find_first_not_of("0123456789") != std::string::npos))
            {
                session.SendResponse(std::string("Usage: ttd limit [frames <n>] [bytes <n>[K|M|G]] | ttd limit off") +
                                     NEWLINE);
                return;
            }
            (args[i] == "frames" ? frames : bytes) = value;
            change = true;
        }
    }
    if (change)
    {
        const ttd::TTDReply reply = ttd::TTDControl(context).Execute(
            {"history-limit", {{"frames", std::to_string(frames)}, {"bytes", std::to_string(bytes)}}});
        if (!reply.Ok())
        {
            session.SendResponse("Error: " + reply.message + NEWLINE);
            return;
        }
        info = mgr->ReadSessionInfo();
    }

    std::stringstream ss;
    ss << "TTD history limit: ";
    if (info.historyLimitFrames == 0 && info.historyLimitBytes == 0)
        ss << "none";
    if (info.historyLimitFrames != 0)
        ss << info.historyLimitFrames << " frames ";
    if (info.historyLimitBytes != 0)
        ss << info.historyLimitBytes << " bytes (" << info.historyLimitBytes / (1024 * 1024) << " MB)";
    ss << NEWLINE;
    ss << "  History: frames " << info.sessionStartFrame << " .. " << info.currentEndFrame << ", "
       << info.checkpointCount << " checkpoints, " << info.historyBytes << " bytes held, " << info.evictedCheckpoints
       << " oldest released" << NEWLINE;
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTTDStop(const ClientSession& session, EmulatorContext* context)
{
    const ttd::TTDSessionView view(context);
    const ttd::TTDSessionView* mgr = &view;
    if (!mgr->IsRecording())
    {
        session.SendResponse(std::string("TTD: Not recording (no-op)") + NEWLINE);
        return;
    }
    (void)ttd::TTDControl(context).Execute({"stop", {}});
    session.SendResponse(std::string("TTD: Recording stopped (history retained)") + NEWLINE);
}

void CLIProcessor::HandleTTDInvalidate(const ClientSession& session, EmulatorContext* context,
                                        const std::vector<std::string>& args)
{
    const std::string reason = args.size() > 1 ? args[1] : "CLI invalidate";
    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"invalidate", {{"reason", reason}}});
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE);
        return;
    }
    session.SendResponse(std::string("TTD: Session invalidated (") + reason + ")" + NEWLINE);
}

void CLIProcessor::HandleTTDSeek(const ClientSession& session, EmulatorContext* context,
                                  const std::vector<std::string>& args)
{
    // TD-4: `ttd seek --bookmark <label>` (alias -b) resolves the label to
    // its stored position; everything else is a coordinate seek.
    std::string bookmarkLabel;
    std::vector<std::string> positional;
    for (size_t i = 1; i < args.size(); ++i)
    {
        if ((args[i] == "--bookmark" || args[i] == "-b") && i + 1 < args.size())
        {
            bookmarkLabel = args[++i];
        }
        else if (args[i] == "--bookmark" || args[i] == "-b")
        {
            session.SendResponse(std::string("Error: --bookmark requires a label") + NEWLINE +
                                 "Usage: ttd seek --bookmark <label>" + NEWLINE);
            return;
        }
        else
        {
            positional.push_back(args[i]);
        }
    }

    if (bookmarkLabel.empty() && positional.empty())
    {
        session.SendResponse(std::string("Error: Missing frame argument") + NEWLINE +
                             "Usage: ttd seek <frame> [tinframe]" + NEWLINE +
                             "       ttd seek --bookmark <label>" + NEWLINE);
        return;
    }

    std::map<std::string, std::string> options;
    if (!bookmarkLabel.empty())
        options["bookmark"] = bookmarkLabel;
    else
    {
        options["frame"] = positional[0];
        if (positional.size() > 1)
            options["tinframe"] = positional[1];
    }
    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"seek", options});
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE);
        return;
    }

    const StateNode& b = reply.body;
    const StateNode& at = *b.find("arrived_at");
    std::stringstream ss;
    ss << (b.find("reached")->b ? "TTD: Seek reached target" : "TTD: Seek halted at") << " (frame="
       << at.find("frame")->i << ", tInFrame=" << at.find("tinframe")->i << ")" << NEWLINE;
    if (!b.find("reached")->b)
    {
        const std::string reason = b.find("halt_reason")->s;
        if (reason == "external_event")
        {
            const StateNode& marker = *b.find("blocking_marker");
            ss << "  Reason: External-event marker barrier" << NEWLINE;
            ss << "  Marker kind: " << marker.find("kind")->s << NEWLINE;
            ss << "  Marker reason: " << marker.find("reason")->s << NEWLINE;
        }
        else if (reason == "out_of_range")
            ss << "  Reason: Target out of range" << NEWLINE;
        else
            ss << "  Reason: Unknown" << NEWLINE;
    }
    if (!bookmarkLabel.empty())
        ss << "  (bookmark '" << bookmarkLabel << "')" << NEWLINE;
    session.SendResponse(ss.str());
}

// -------------------------------------------------------------------------
// TD-4 — agent bookmarks (advisory annotations, never replay barriers).
// Labels are keys: non-empty, at most 63 characters, unique per session.
// -------------------------------------------------------------------------

void CLIProcessor::HandleTTDBookmark(const ClientSession& session, EmulatorContext* context,
                                      const std::vector<std::string>& args)
{
    const ttd::TTDSessionView view(context);
    const ttd::TTDSessionView* mgr = &view;

    const std::string action = args.size() > 1 ? args[1] : "list";

    if (action == "list" || action == "ls")
    {
        const auto bookmarks = mgr->GetBookmarks();

        std::stringstream ss;
        ss << "TTD Bookmarks (" << bookmarks.size() << " total, advisory — never replay barriers)" << NEWLINE;
        ss << "==========================================================" << NEWLINE;

        if (bookmarks.empty())
        {
            ss << "  (none)" << NEWLINE;
        }
        else
        {
            for (size_t i = 0; i < bookmarks.size(); ++i)
            {
                ss << "  [" << i << "] frame=" << bookmarks[i].time.frame
                   << " tInFrame=" << bookmarks[i].time.tInFrame
                   << " label=\"" << bookmarks[i].label << "\"" << NEWLINE;
            }
        }

        session.SendResponse(ss.str());
        return;
    }

    if (action == "add" || action == "mark")
    {
        if (args.size() < 3)
        {
            session.SendResponse(std::string("Error: Missing label argument") + NEWLINE +
                                 "Usage: ttd bookmark add <label> [frame] [tinframe]" + NEWLINE +
                                 "       (frame omitted → bookmark the current position)" + NEWLINE);
            return;
        }
        const std::string label = args[2];
        // Optional position; omitted: the current position (mark here)
        std::map<std::string, std::string> options{{"label", label}};
        if (args.size() > 3)
        {
            options["frame"] = args[3];
            options["tinframe"] = args.size() > 4 ? args[4] : "0";
        }
        const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"bookmark-add", options});
        if (!reply.Ok())
        {
            session.SendResponse(std::string("Error: ") + reply.message + NEWLINE);
            return;
        }
        std::stringstream ss;
        ss << "TTD: Bookmark '" << label << "' added at (frame=" << reply.body.find("frame")->i
           << ", tInFrame=" << reply.body.find("tinframe")->i << ")" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    if (action == "del" || action == "delete" || action == "remove" || action == "rm")
    {
        if (args.size() < 3)
        {
            session.SendResponse(std::string("Error: Missing label argument") + NEWLINE +
                                 "Usage: ttd bookmark del <label>" + NEWLINE);
            return;
        }

        const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"bookmark-delete", {{"label", args[2]}}});
        if (!reply.Ok())
        {
            session.SendResponse("Error: " + reply.message + NEWLINE);
            return;
        }

        session.SendResponse(std::string("TTD: Bookmark '") + args[2] + "' removed" + NEWLINE);
        return;
    }

    session.SendResponse(std::string("Error: Unknown bookmark action '") + action + "'" + NEWLINE +
                         "Usage: ttd bookmark [list|add|del]" + NEWLINE);
}

void CLIProcessor::HandleTTDStepBack(const ClientSession& session, EmulatorContext* context)
{
    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"step-back", {}});
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE);
        return;
    }
    if (reply.body.find("stepped")->b)
    {
        std::stringstream ss;
        ss << "TTD: Stepped back to (frame=" << reply.body.find("frame")->i << ", tInFrame=" << reply.body.find("tinframe")->i
           << ")" << NEWLINE;
        session.SendResponse(ss.str());
    }
    else
    {
        session.SendResponse(std::string("TTD: Cannot step back (at or before first captured frame)") + NEWLINE);
    }
}

void CLIProcessor::HandleTTDStepForward(const ClientSession& session, EmulatorContext* context)
{
    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"step-forward", {}});
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE);
        return;
    }
    if (reply.body.find("stepped")->b)
    {
        std::stringstream ss;
        ss << "TTD: Stepped forward to (frame=" << reply.body.find("frame")->i << ", tInFrame=" << reply.body.find("tinframe")->i
           << ")" << NEWLINE;
        session.SendResponse(ss.str());
    }
    else
    {
        session.SendResponse(std::string("TTD: Cannot step forward (at or beyond last captured frame)") + NEWLINE);
    }
}

void CLIProcessor::HandleTTDResume(const ClientSession& session, EmulatorContext* context,
                                    const std::vector<std::string>& args)
{
    std::map<std::string, std::string> options;
    if (args.size() >= 2)
    {
        options["frame"] = args[1];
        options["tinframe"] = args.size() > 2 ? args[2] : "0";
    }
    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"resume", options});
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE);
        return;
    }
    if (reply.body.find("resumed")->b)
    {
        std::stringstream ss;
        ss << "TTD: Resumed recording from (frame=" << reply.body.find("frame")->i
           << ", tInFrame=" << reply.body.find("tinframe")->i << ")" << NEWLINE;
        session.SendResponse(ss.str());
    }
    else
    {
        session.SendResponse(std::string("TTD: Cannot resume (invalid state or out of bounds)") + NEWLINE);
    }
}

void CLIProcessor::HandleTTDPosition(const ClientSession& session, EmulatorContext* context)
{
    const ttd::TTDSessionView view(context);
    const ttd::TTDSessionView* mgr = &view;

    ttd::TTDTimePoint pos = mgr->CurrentPosition();
    ttd::TTDTimePoint end = mgr->SessionEndPosition();

    std::stringstream ss;
    ss << "TTD Position" << NEWLINE;
    ss << "============" << NEWLINE;
    ss << "  Current: (frame=" << pos.frame << ", tInFrame=" << pos.tInFrame << ")" << NEWLINE;
    ss << "  End:     (frame=" << end.frame << ", tInFrame=" << end.tInFrame << ")" << NEWLINE;

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTTDMarkers(const ClientSession& session, EmulatorContext* context)
{
    const ttd::TTDSessionView view(context);
    const ttd::TTDSessionView* mgr = &view;
    const ttd::TTDExternalEventJournal& journal = mgr->GetExternalEvents();

    std::stringstream ss;
    ss << "TTD External-Event Markers (" << journal.Size() << " total)" << NEWLINE;
    ss << "=============================================" << NEWLINE;

    if (journal.Size() == 0)
    {
        ss << "  (none)" << NEWLINE;
    }
    else
    {
        const auto events = journal.SnapshotEvents();
        for (size_t i = 0; i < events.size(); ++i)
        {
            const auto& e = events[i];
            ss << "  [" << i << "] frame=" << e.time.frame
               << " tInFrame=" << e.time.tInFrame
               << " kind=" << ttd::TTDExternalEventKindToString(e.kind)
               << " reason=\"" << e.reason << "\"" << NEWLINE;
        }
    }

    session.SendResponse(ss.str());
}

// -------------------------------------------------------------------------
// Phase 4 — Reverse-search + dump + instruction-step handlers
// -------------------------------------------------------------------------

void CLIProcessor::HandleTTDDump(const ClientSession& session, EmulatorContext* context,
                                  const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing path argument") + NEWLINE + "Usage: ttd dump <path>" + NEWLINE);
        return;
    }
    const std::string& path = args[1];
    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"dump", {{"path", path}}});
    if (!reply.Ok())
    {
        session.SendResponse(std::string(reply.message.rfind("Cannot open", 0) == 0 ? "Error: " : "TTD: Dump failed: ") +
                             reply.message + NEWLINE);
        return;
    }
    std::stringstream ss;
    ss << "TTD: Session dumped to '" << path << "' (" << reply.body.find("bytes")->i << " bytes)" << NEWLINE;
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTTDLoad(const ClientSession& session, EmulatorContext* context,
                                 const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing path argument") + NEWLINE + "Usage: ttd load <path>" + NEWLINE);
        return;
    }
    const std::string& path = args[1];
    // The most common failure is a model mismatch: a session only restores into an
    // instance of the model it was recorded on
    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"load", {{"path", path}}});
    if (!reply.Ok())
    {
        session.SendResponse(std::string(reply.message.rfind("Cannot open", 0) == 0 ? "Error: " : "TTD: Load failed: ") +
                             reply.message + NEWLINE);
        return;
    }
    const StateNode& b = reply.body;
    std::stringstream ss;
    ss << "TTD: Session loaded from '" << path << "' (" << b.find("checkpoint_count")->i << " checkpoints, frames "
       << b.find("session_start_frame")->i << ".." << b.find("current_end_frame")->i << ")" << NEWLINE
       << "     Session is idle - use 'ttd seek' to position the emulator." << NEWLINE;
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTTDExportClip(const ClientSession& session, EmulatorContext* context,
                                       const std::vector<std::string>& args)
{
    // ttd export-clip <from> <to> <dir> [--chunk N]
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;
    for (size_t i = 1; i < args.size(); ++i)
    {
        if (args[i] == "--chunk" && i + 1 < args.size())
            options["chunk"] = args[++i];
        else
            positional.push_back(args[i]);
    }
    if (positional.size() != 3)
    {
        session.SendResponse(std::string("Error: Missing arguments") + NEWLINE +
                             "Usage: ttd export-clip <from-frame> <to-frame> <dir> [--chunk N]" + NEWLINE);
        return;
    }
    options["from"] = positional[0];
    options["to"] = positional[1];
    options["path"] = positional[2];
    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"export-clip", options});
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE);
        return;
    }
    const StateNode& b = reply.body;
    std::stringstream ss;
    ss << "TTD: Clip written to '" << b.find("path")->s << "': " << b.find("frames")->i << " frame(s), "
       << b.find("width")->i << "x" << b.find("height")->i << ", " << b.find("bytes")->i << " bytes"
       << (b.find("planeb")->b ? ", plane B included" : "") << " in " << b.find("seconds")->d << " s" << NEWLINE;
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTTDPortEvents(const ClientSession& session, EmulatorContext* context,
                                        const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Usage: ttd port-events <event> [arg] [option=value ...]  (ttd help)") +
                             NEWLINE);
        return;
    }

    // args[1] is the event; an argument without '=' after it is the event's
    // argument (a key name, an AY register); the rest are option=value
    std::map<std::string, std::string> options{{"event", args[1]}};
    size_t next = 2;
    if (args.size() > 2 && args[2].find('=') == std::string::npos)
        options["arg"] = args[next++];
    for (; next < args.size(); ++next)
    {
        const size_t eq = args[next].find('=');
        if (eq == std::string::npos)
        {
            session.SendResponse("Error: expected option=value, got '" + args[next] + "'" + NEWLINE);
            return;
        }
        options[args[next].substr(0, eq)] = args[next].substr(eq + 1);
    }

    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"port-events", options});
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE);
        return;
    }
    const StateNode& b = reply.body;
    std::stringstream ss;
    ss << b.find("count")->i << " hit(s)" << (b.find("truncated")->b ? " (more than the limit)" : "") << ", "
       << b.find("scanned")->i << (b.find("direction")->s == "in" ? " IN" : " OUT") << " record(s) scanned" << NEWLINE;
    for (const StateNode& h : b.find("hits")->items)
    {
        ss << "  frame " << h.find("frame")->i << " t " << h.find("tinframe")->i << "  PC #" << std::hex
           << std::uppercase << std::setw(4) << std::setfill('0') << h.find("pc")->i << "  port #" << std::setw(4)
           << h.find("port")->i << "  value #" << std::setw(2) << h.find("value")->i << std::dec << std::setfill(' ');
        if (const StateNode* r = h.find("ay_register"))
            ss << "  R" << r->i;
        ss << NEWLINE;
    }
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTTDFindLast(const ClientSession& session, EmulatorContext* context,
                                      const std::vector<std::string>& args)
{
    // --key value pairs from args[1..]; the verb checks every value
    static const std::map<std::string, std::string> flags = {
        {"--addr", "addr"},       {"--addr-from", "addr_from"},   {"--addr-to", "addr_to"},
        {"--access", "access"},   {"--value", "value"},           {"--pc-from", "pc_from"},
        {"--pc-to", "pc_to"},     {"--phys-page", "phys_page"},   {"--before-frame", "before_frame"},
        {"--before-tin", "before_tin"}, {"--space", "space"}};
    std::map<std::string, std::string> options;
    for (size_t i = 1; i < args.size(); ++i)
    {
        const auto flag = flags.find(args[i]);
        if (flag != flags.end() && i + 1 < args.size())
            options[flag->second] = args[++i];
    }
    // --before-tin alone: T-state of frame 0, as before
    if (options.count("before_tin") && !options.count("before_frame"))
        options["before_frame"] = "0";

    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"find-last", options});
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE +
                             (reply.error == ttd::TTDControlError::BadRequest
                                  ? std::string("Usage: ttd find-last [--addr <A> | --addr-from <F> --addr-to <T>] "
                                                "[--access write|read|execute|io] [--value V] [--pc-from X] [--pc-to Y] "
                                                "[--phys-page P] [--space ram|vram|cache] [--before-frame F] [--before-tin T]") +
                                        NEWLINE
                                  : std::string()));
        return;
    }

    const StateNode& b = reply.body;
    std::stringstream ss;
    if (b.find("found")->b)
    {
        ss << "TTD: Match found" << NEWLINE;
        ss << "  Frame:    " << b.find("frame")->i << NEWLINE;
        ss << "  tInFrame: " << b.find("tinframe")->i << NEWLINE;
        ss << "  PC:       0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << b.find("pc")->i
           << NEWLINE;
        ss << "  Value:    0x" << std::setw(2) << b.find("value")->i << NEWLINE;
        ss << "  PhysPage: " << std::dec;
        const StateNode* offset = b.find("offset");   // --space vram / cache: the offset in that memory
        if (offset)
            ss << "none (" << b.find("space")->s << ")" << NEWLINE;
        else if (b.find("phys_page")->kind == StateNode::Kind::Null)
            ss << "none (ROM / no RAM page)" << NEWLINE;
        else
            ss << b.find("phys_page")->i << NEWLINE;
        if (offset)
            ss << "  Offset:   " << b.find("space")->s << " 0x" << std::hex << std::uppercase << std::setw(5) << offset->i
               << std::dec << NEWLINE;
        ss << "  Access:   " << b.find("access")->s << NEWLINE;
    }
    else if (b.find("blocked"))
    {
        ss << "TTD: Search blocked by external-event marker" << NEWLINE;
        ss << "  Marker at frame=" << b.find("marker_frame")->i << " tInFrame=" << b.find("marker_tinframe")->i
           << NEWLINE;
        ss << "  Kind: " << b.find("marker_kind")->s << NEWLINE;
        ss << "  Reason: " << b.find("marker_reason")->s << NEWLINE;
    }
    else
    {
        ss << "TTD: No match found" << NEWLINE;
    }
    ss << FormatSearchWindow(b);
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTTDStepInstruction(const ClientSession& session, EmulatorContext* context,
                                             const std::vector<std::string>& args)
{
    // Determine direction from subcommand or explicit arg.
    bool forward = false;
    if (!args.empty())
    {
        const std::string& sub = args[0];
        if (sub == "si-forward")
            forward = true;
        else if (sub == "si-back")
            forward = false;
        else if (args.size() > 1 && (args[1] == "forward" || args[1] == "fwd"))
            forward = true;
    }

    const ttd::TTDReply reply =
        ttd::TTDControl(context).Execute({"step-instruction", {{"dir", forward ? "forward" : "back"}}});
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE);
        return;
    }
    if (reply.body.find("stepped")->b)
    {
        std::stringstream ss;
        ss << "TTD: Stepped " << (forward ? "forward" : "back") << " to (frame=" << reply.body.find("frame")->i
           << ", tInFrame=" << reply.body.find("tinframe")->i << ")" << NEWLINE;
        session.SendResponse(ss.str());
    }
    else
    {
        session.SendResponse(std::string("TTD: Cannot step ") +
                             (forward ? "forward (at session end)" : "back (at session start)") + NEWLINE);
    }
}

void CLIProcessor::HandleTTDReverseStep(const ClientSession& session, EmulatorContext* context,
                                          const std::vector<std::string>& args)
{
    // --count N or --tstates T; neither: one instruction
    std::map<std::string, std::string> options;
    for (size_t i = 1; i < args.size(); ++i)
    {
        const std::string& tok = args[i];
        if (tok == "--count" && i + 1 < args.size())
            options["count"] = args[++i];
        else if (tok == "--tstates" && i + 1 < args.size())
            options["tstates"] = args[++i];
    }
    if (options.empty())
        options["count"] = "1";

    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"reverse-step", options});
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE);
        return;
    }
    if (reply.body.find("reached")->b)
    {
        std::stringstream ss;
        if (options.count("tstates"))
            ss << "TTD: Stepped back " << options["tstates"] << " t-states to ";
        else
            ss << "TTD: Stepped back " << options["count"] << " instruction" << (options["count"] == "1" ? "" : "s")
               << " to ";
        ss << "(frame=" << reply.body.find("frame")->i << ", tInFrame=" << reply.body.find("tinframe")->i << ")"
           << NEWLINE;
        session.SendResponse(ss.str());
    }
    else
    {
        session.SendResponse(std::string("TTD: Reverse-step failed (at session start or out of history)") + NEWLINE);
    }
}

void CLIProcessor::HandleTTDReverseContinue(const ClientSession& session, EmulatorContext* context,
                                             const std::vector<std::string>& args)
{
    // One or more --pc <V> arguments
    std::string pcs;
    for (size_t i = 1; i < args.size(); ++i)
        if (args[i] == "--pc" && i + 1 < args.size())
            pcs += (pcs.empty() ? "" : ",") + args[++i];
    if (pcs.empty())
    {
        session.SendResponse(std::string("Error: --pc is required (at least one)") + NEWLINE +
                             "Usage: ttd reverse-continue --pc <A> [--pc <B> ...]" + NEWLINE);
        return;
    }

    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"reverse-continue", {{"pcs", pcs}}});
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE);
        return;
    }
    const StateNode& b = reply.body;
    std::stringstream ss;
    if (b.find("matched")->b)
    {
        ss << "TTD: Reverse-continue hit PC=0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(4)
           << b.find("pc")->i << std::dec << " at (frame=" << b.find("frame")->i
           << ", tInFrame=" << b.find("tinframe")->i << ")" << NEWLINE;
    }
    else if (const StateNode* m = b.find("blocked_by_marker"))
    {
        ss << "TTD: Reverse-continue blocked by marker at (frame=" << m->find("frame")->i
           << ", tInFrame=" << m->find("tinframe")->i << ")" << NEWLINE;
        ss << "  Kind: " << m->find("kind")->s << NEWLINE;
        ss << "  Reason: " << m->find("reason")->s << NEWLINE;
    }
    else
    {
        ss << "TTD: Reverse-continue found no match (reached session start)" << NEWLINE;
    }
    ss << FormatSearchWindow(b);
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTTDCoverage(const ClientSession& session, EmulatorContext* context, const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Usage: ttd coverage [probe|scan|summary] [options]") + NEWLINE);
        return;
    }
    const std::string sub = args[1];
    if (sub != "probe" && sub != "scan" && sub != "summary")
    {
        session.SendResponse("Unknown coverage subcommand '" + sub + "'. Expected: probe, scan, summary" + NEWLINE);
        return;
    }

    // Flags to the verb's options; the verb checks every value
    static const std::map<std::string, std::string> flags = {
        {"--frame", "frame"},         {"-f", "frame"},           {"--from-frame", "from_frame"},
        {"--to-frame", "to_frame"},   {"--kind", "kind"},        {"-k", "kind"},
        {"--from", "addr_from"},      {"--addr-from", "addr_from"}, {"-a", "addr_from"},
        {"--to", "addr_to"},          {"--addr-to", "addr_to"},  {"-b", "addr_to"},
        {"--page", "phys_page"},      {"--phys-page", "phys_page"}, {"-p", "phys_page"},
        {"--space", "space"},
        {"--limit", "limit"},         {"-l", "limit"},           {"--bucket", "bucket_size"},
        {"--bucket-size", "bucket_size"}};
    std::map<std::string, std::string> options;
    for (size_t i = 2; i < args.size(); ++i)
    {
        const auto flag = flags.find(args[i]);
        if (flag != flags.end() && i + 1 < args.size())
            options[flag->second] = args[++i];
    }
    const std::string verb = "coverage-" + sub;
    // Options another subcommand takes are ignored, as before
    const auto& allowed = ttd::TTDControl::OptionsFor(verb);
    for (auto it = options.begin(); it != options.end();)
        it = std::find(allowed.begin(), allowed.end(), it->first) == allowed.end() ? options.erase(it) : std::next(it);

    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({verb, options});
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE);
        return;
    }
    const StateNode& b = reply.body;
    std::ostringstream ss;
    if (!b.find("index_available")->b)
    {
        if (sub == "probe")
            ss << "Coverage index not available for frame " << b.find("frame")->i << NEWLINE;
        else
            ss << "Coverage index not available for this session" << NEWLINE;
    }
    else if (sub == "probe")
    {
        // A range in another memory (--space vram / cache) by its offsets
        const bool space = b.find("space") != nullptr;
        ss << "Frame " << b.find("frame")->i << " kind=" << b.find("kind")->s
           << (space ? " space=" + b.find("space")->s : std::string()) << " range=["
           << (space ? b.find("offset_from")->s : b.find("addr_from")->s) << ".."
           << (space ? b.find("offset_to")->s : b.find("addr_to")->s) << "]: " << (b.find("touched")->b ? "TOUCHED" : "NOT touched")
           << NEWLINE;
    }
    else if (sub == "scan")
    {
        ss << "Scanned " << b.find("scanned_frames")->i << " frames, matched " << b.find("matching_frames")->i
           << " frames (first=" << b.find("first_match")->i << ", last=" << b.find("last_match")->i << ")"
           << " [index covers " << b.find("covered_from")->i << ".." << b.find("covered_to")->i << "]";
        if (b.find("truncated")->b)
            ss << " [TRUNCATED]";
        ss << NEWLINE;
        const auto& frames = b.find("frames")->items;
        if (!frames.empty())
        {
            ss << "Matching frames: ";
            for (size_t n = 0; n < frames.size(); ++n)
                ss << (n ? ", " : "") << frames[n].i;
            ss << NEWLINE;
        }
    }
    else
    {
        ss << "Coverage summary (" << b.find("from_frame")->i << ".." << b.find("to_frame")->i << ", index covers "
           << b.find("covered_from")->i << ".." << b.find("covered_to")->i << ", bucket_size=" << b.find("bucket_size")->i
           << ", buckets=" << b.find("bucket_count")->i << "):" << NEWLINE;
        for (const StateNode& bucket : b.find("buckets")->items)
            ss << "  [" << bucket.find("frame_start")->i << ".." << bucket.find("frame_end")->i << "]"
               << " exec=" << bucket.find("executed_distinct")->i << " write=" << bucket.find("written_distinct")->i
               << " read=" << bucket.find("read_distinct")->i << (bucket.find("has_keyframe")->b ? " [I-frame]" : "")
               << NEWLINE;
    }
    session.SendResponse(ss.str());
}

/// `ttd memory-at --space S --frame F [--offset O] [--length N]` and `ttd memory-diff --space S --from F1 --to F2
/// [--limit N]`: a memory at a past checkpoint, and what changed between two (the engine's store, no seek)
void CLIProcessor::HandleTTDMemoryAt(const ClientSession& session, EmulatorContext* context,
                                     const std::vector<std::string>& args)
{
    const bool diff = args[0] == "memory-diff" || args[0] == "mdiff";
    static const std::map<std::string, std::string> flags = {
        {"--space", "space"}, {"--frame", "frame"},       {"--offset", "offset"}, {"--length", "length"},
        {"--from", "from_frame"}, {"--to", "to_frame"},   {"--limit", "limit"}};
    std::map<std::string, std::string> options;
    for (size_t i = 1; i < args.size(); ++i)
    {
        const auto flag = flags.find(args[i]);
        if (flag != flags.end() && i + 1 < args.size())
            options[flag->second] = args[++i];
    }
    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({diff ? "memory-diff" : "memory-at", options});
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE +
                             (reply.error == ttd::TTDControlError::BadRequest
                                  ? std::string(diff ? "Usage: ttd memory-diff --space S --from F1 --to F2 [--limit N]"
                                                     : "Usage: ttd memory-at --space S --frame F [--offset O] [--length N]") +
                                        NEWLINE
                                  : std::string()));
        return;
    }
    const StateNode& b = reply.body;
    std::ostringstream ss;
    if (diff)
    {
        ss << b.find("space")->s << ": " << b.find("changed_bytes")->i << " bytes differ between frame "
           << b.find("at_from")->i << " and frame " << b.find("at_to")->i << NEWLINE;
        for (const StateNode& r : b.find("ranges")->items)
            ss << "  0x" << std::hex << std::uppercase << std::setw(5) << std::setfill('0') << r.find("offset")->i << std::dec
               << " +" << r.find("length")->i << NEWLINE;
        if (b.find("truncated")->b)
            ss << "  ... (more: --limit)" << NEWLINE;
    }
    else
    {
        const std::string& hex = b.find("hex")->s;
        ss << b.find("space")->s << " at frame " << b.find("at_frame")->i
           << (b.find("exact")->b ? "" : " (the checkpoint at or before frame " + std::to_string(b.find("frame")->i) + ")")
           << ":" << NEWLINE;
        const int64_t offset = b.find("offset")->i;
        for (size_t i = 0; i < hex.size(); i += 32)
        {
            ss << "  0x" << std::hex << std::uppercase << std::setw(5) << std::setfill('0') << offset + static_cast<int64_t>(i / 2)
               << std::dec << ":";
            for (size_t j = i; j < std::min(i + 32, hex.size()); j += 2)
                ss << " " << hex.substr(j, 2);
            ss << NEWLINE;
        }
    }
    session.SendResponse(ss.str());
}

/// endregion </TTD Commands>
