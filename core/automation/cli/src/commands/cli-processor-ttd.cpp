/// @file cli-processor-ttd.cpp
/// @brief CLI Time-Travel Debug (TTD) command handlers.
///
/// Exposes the full TTD engine surface (Phase 2 complete) to CLI automation:
///   ttd status                 — session info (state, frame range, checkpoint count)
///   ttd start [--no-journal]   — begin recording (--no-journal = gaming mode)
///   ttd stop                   — stop recording (history retained, browsable)
///   ttd invalidate [reason]    — drop all history, return to Idle
///   ttd seek <frame> [tinframe] — seek to a point in the timeline
///   ttd step-back              — step back one frame (preserve intra-frame pos)
///   ttd step-forward           — step forward one frame (preserve intra-frame pos)
///   ttd resume [frame] [tin]   — resume recording from current or specified point
///   ttd position               — show current TTDTimePoint (frame + tInFrame)
///   ttd markers                — list external-event markers (replay barriers)
///
/// All commands operate on the currently selected emulator instance. The TTD
/// engine requires the emulator to be paused for seek/step/resume operations
/// (existing pause discipline — same as RestoreCheckpointForTesting).

#include "cli-processor.h"

#include <debugger/ttd/timetravelmanager.h>
#include <debugger/ttd/ttd_external_events.h>
#include <debugger/ttd/ttd_probe.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>

#include <fstream>
#include <iomanip>
#include <sstream>

/// region <TTD Commands>

void CLIProcessor::HandleTTD(const ClientSession& session, const std::vector<std::string>& args)
{
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

    if (!context->pTimeTravelManager)
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
    else if (subcommand == "stop")
    {
        HandleTTDStop(session, context);
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
    else if (subcommand == "dump" || subcommand == "save")
    {
        HandleTTDDump(session, context, args);
    }
    else if (subcommand == "find-last" || subcommand == "fl")
    {
        HandleTTDFindLast(session, context, args);
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
    ss << "  ttd status                       Show session info (state, frames, checkpoints)" << NEWLINE;
    ss << "  ttd start [--no-journal]         Begin recording (captures baseline checkpoint)" << NEWLINE;
    ss << "                                     --no-journal: gaming mode, smaller memory footprint" << NEWLINE;
    ss << "  ttd stop                         Stop recording (history retained, browsable)" << NEWLINE;
    ss << "  ttd invalidate [reason]          Drop all history, return to Idle" << NEWLINE;
    ss << "  ttd seek <frame> [tinframe]      Seek to a point in the timeline" << NEWLINE;
    ss << "  ttd step-back                    Step back one frame (preserve intra-frame pos)" << NEWLINE;
    ss << "  ttd step-forward                 Step forward one frame (preserve intra-frame pos)" << NEWLINE;
    ss << "  ttd resume [frame] [tinframe]    Resume recording from current or specified point" << NEWLINE;
    ss << "  ttd position                     Show current TTDTimePoint (frame + tInFrame)" << NEWLINE;
    ss << "  ttd markers                      List external-event markers (replay barriers)" << NEWLINE;
    ss << NEWLINE;
    ss << "Phase 4 — Reverse Search + Automation:" << NEWLINE;
    ss << "  ttd dump <path>                  Serialize session to .ttd file" << NEWLINE;
    ss << "  ttd find-last --addr <A>         Reverse search: find last access at address" << NEWLINE;
    ss << "    [--access write|read|execute|io]  (default: write)" << NEWLINE;
    ss << "    [--value V] [--pc-from X] [--pc-to Y]" << NEWLINE;
    ss << "    [--before-frame F] [--before-tin T]" << NEWLINE;
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
    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;
    ttd::TTDSessionInfo info = mgr->GetSessionInfo();

    std::stringstream ss;
    ss << "TTD Session Status" << NEWLINE;
    ss << "==================" << NEWLINE;
    ss << NEWLINE;
    ss << "  State:                  " << ttd::TTDSessionStateToString(info.state) << NEWLINE;
    ss << "  Session start frame:    " << info.sessionStartFrame << NEWLINE;
    ss << "  Current end frame:      " << info.currentEndFrame << NEWLINE;
    ss << "  Checkpoint count:       " << info.checkpointCount << NEWLINE;
    ss << "  Page store capacity:    " << info.pageStoreBytes << " bytes" << NEWLINE;
    ss << "  Page store used:        " << info.pageStoreUsedBytes << " bytes" << NEWLINE;
    ss << "  Baseline frames cap'd:  " << info.baselineFramesCaptured << NEWLINE;
    ss << "  Session heap total:     " << info.sessionHeapBytes << " bytes" << NEWLINE;

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTTDStart(const ClientSession& session, EmulatorContext* context,
                                   const std::vector<std::string>& args)
{
    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;

    if (mgr->IsRecording())
    {
        session.SendResponse(std::string("TTD: Already recording (no-op)") + NEWLINE);
        return;
    }

    // Parse optional --no-journal or --journal flag.
    bool enableJournal = true;  // Default: development mode with write journal.
    for (size_t i = 1; i < args.size(); ++i)
    {
        if (args[i] == "--no-journal" || args[i] == "-n")
            enableJournal = false;
        else if (args[i] == "--journal" || args[i] == "-j")
            enableJournal = true;
    }

    mgr->SetEnableWriteJournal(enableJournal);
    bool ok = mgr->StartRecording();
    if (ok)
    {
        std::string msg = enableJournal
            ? "TTD: Recording started (with write journal)"
            : "TTD: Recording started (gaming mode, no journal)";
        session.SendResponse(msg + NEWLINE);
    }
    else
    {
        session.SendResponse(std::string("TTD: Failed to start recording") + NEWLINE);
    }
}

void CLIProcessor::HandleTTDStop(const ClientSession& session, EmulatorContext* context)
{
    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;

    if (!mgr->IsRecording())
    {
        session.SendResponse(std::string("TTD: Not recording (no-op)") + NEWLINE);
        return;
    }

    mgr->StopRecording();
    session.SendResponse(std::string("TTD: Recording stopped (history retained)") + NEWLINE);
}

void CLIProcessor::HandleTTDInvalidate(const ClientSession& session, EmulatorContext* context,
                                        const std::vector<std::string>& args)
{
    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;

    std::string reason = "CLI invalidate";
    if (args.size() > 1)
    {
        reason = args[1];
    }

    mgr->InvalidateSession(reason.c_str());
    session.SendResponse(std::string("TTD: Session invalidated (") + reason + ")" + NEWLINE);
}

void CLIProcessor::HandleTTDSeek(const ClientSession& session, EmulatorContext* context,
                                  const std::vector<std::string>& args)
{
    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;

    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing frame argument") + NEWLINE +
                             "Usage: ttd seek <frame> [tinframe]" + NEWLINE);
        return;
    }

    try
    {
        uint64_t frame = std::stoull(args[1]);
        uint32_t tInFrame = 0;
        if (args.size() > 2)
        {
            tInFrame = static_cast<uint32_t>(std::stoul(args[2]));
        }

        ttd::TTDTimePoint target{frame, tInFrame};

        ttd::TimeTravelManager::TTDSeekResult result;
        bool reached = mgr->SeekTo(target, &result);

        std::stringstream ss;
        if (reached)
        {
            ss << "TTD: Seek reached target (frame=" << result.arrivedAt.frame
               << ", tInFrame=" << result.arrivedAt.tInFrame << ")" << NEWLINE;
        }
        else
        {
            ss << "TTD: Seek halted at (frame=" << result.arrivedAt.frame
               << ", tInFrame=" << result.arrivedAt.tInFrame << ")" << NEWLINE;

            switch (result.haltReason)
            {
                case ttd::TimeTravelManager::TTDSeekHaltReason::ExternalEvent:
                    ss << "  Reason: External-event marker barrier" << NEWLINE;
                    ss << "  Marker kind: " << ttd::TTDExternalEventKindToString(result.blockingMarker.kind) << NEWLINE;
                    ss << "  Marker reason: " << result.blockingMarker.reason << NEWLINE;
                    break;
                case ttd::TimeTravelManager::TTDSeekHaltReason::OutOfRange:
                    ss << "  Reason: Target out of range" << NEWLINE;
                    break;
                default:
                    ss << "  Reason: Unknown" << NEWLINE;
                    break;
            }
        }

        session.SendResponse(ss.str());
    }
    catch (const std::exception& e)
    {
        session.SendResponse(std::string("Error: Invalid argument: ") + e.what() + NEWLINE);
    }
}

void CLIProcessor::HandleTTDStepBack(const ClientSession& session, EmulatorContext* context)
{
    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;

    bool ok = mgr->StepBackFrame();
    if (ok)
    {
        ttd::TTDTimePoint pos = mgr->CurrentPosition();
        std::stringstream ss;
        ss << "TTD: Stepped back to (frame=" << pos.frame << ", tInFrame=" << pos.tInFrame << ")" << NEWLINE;
        session.SendResponse(ss.str());
    }
    else
    {
        session.SendResponse(std::string("TTD: Cannot step back (at or before first captured frame)") + NEWLINE);
    }
}

void CLIProcessor::HandleTTDStepForward(const ClientSession& session, EmulatorContext* context)
{
    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;

    bool ok = mgr->StepForwardFrame();
    if (ok)
    {
        ttd::TTDTimePoint pos = mgr->CurrentPosition();
        std::stringstream ss;
        ss << "TTD: Stepped forward to (frame=" << pos.frame << ", tInFrame=" << pos.tInFrame << ")" << NEWLINE;
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
    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;

    ttd::TTDTimePoint from = mgr->CurrentPosition();

    if (args.size() >= 2)
    {
        try
        {
            from.frame = std::stoull(args[1]);
            if (args.size() > 2)
            {
                from.tInFrame = static_cast<uint32_t>(std::stoul(args[2]));
            }
            else
            {
                from.tInFrame = 0;
            }
        }
        catch (const std::exception& e)
        {
            session.SendResponse(std::string("Error: Invalid argument: ") + e.what() + NEWLINE);
            return;
        }
    }

    bool ok = mgr->ResumeRecordingFrom(from);
    if (ok)
    {
        std::stringstream ss;
        ss << "TTD: Resumed recording from (frame=" << from.frame << ", tInFrame=" << from.tInFrame << ")" << NEWLINE;
        session.SendResponse(ss.str());
    }
    else
    {
        session.SendResponse(std::string("TTD: Cannot resume (invalid state or out of bounds)") + NEWLINE);
    }
}

void CLIProcessor::HandleTTDPosition(const ClientSession& session, EmulatorContext* context)
{
    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;

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
    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;
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
        const auto& events = journal.Events();
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
    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;

    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing path argument") + NEWLINE +
                             "Usage: ttd dump <path>" + NEWLINE);
        return;
    }

    const std::string& path = args[1];
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open())
    {
        session.SendResponse(std::string("Error: Cannot open file: ") + path + NEWLINE);
        return;
    }

    std::string err;
    bool ok = mgr->SerializeSession(out, err);
    if (ok)
    {
        auto bytes = out.tellp();
        std::stringstream ss;
        ss << "TTD: Session dumped to '" << path << "' ("
           << static_cast<long long>(bytes) << " bytes)" << NEWLINE;
        session.SendResponse(ss.str());
    }
    else
    {
        session.SendResponse(std::string("TTD: Dump failed: ") + err + NEWLINE);
    }
}

void CLIProcessor::HandleTTDFindLast(const ClientSession& session, EmulatorContext* context,
                                      const std::vector<std::string>& args)
{
    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;

    // Parse --key value pairs from args[1..]
    ttd::TTDSearchQuery q;
    bool hasAddr = false;

    for (size_t i = 1; i < args.size(); ++i)
    {
        const std::string& tok = args[i];
        if (tok == "--addr" && i + 1 < args.size())
        {
            q.addrFrom = q.addrTo = static_cast<uint16_t>(std::stoul(args[++i], nullptr, 0));
            hasAddr = true;
        }
        else if (tok == "--access" && i + 1 < args.size())
        {
            q.access = ttd::TTDAccessTypeFromString(args[++i].c_str());
        }
        else if (tok == "--value" && i + 1 < args.size())
        {
            q.value = static_cast<uint8_t>(std::stoul(args[++i], nullptr, 0));
            q.hasValueFilter = true;
        }
        else if (tok == "--pc-from" && i + 1 < args.size())
        {
            q.pcFrom = static_cast<uint16_t>(std::stoul(args[++i], nullptr, 0));
            q.hasPcFilter = true;
        }
        else if (tok == "--pc-to" && i + 1 < args.size())
        {
            q.pcTo = static_cast<uint16_t>(std::stoul(args[++i], nullptr, 0));
            if (!q.hasPcFilter) q.hasPcFilter = true;
        }
        else if (tok == "--before-frame" && i + 1 < args.size())
        {
            uint64_t f = std::stoull(args[++i]);
            // Will be combined with before-tin below; store frame in upper bits
            const uint32_t frameT = context->config.frame;
            uint32_t tin = 0;
            // If before-tin was already set, preserve it
            if (q.beforeGlobalT != UINT64_MAX)
                tin = static_cast<uint32_t>(q.beforeGlobalT % frameT);
            q.beforeGlobalT = f * frameT + tin;
        }
        else if (tok == "--before-tin" && i + 1 < args.size())
        {
            uint32_t tin = static_cast<uint32_t>(std::stoul(args[++i]));
            const uint32_t frameT = context->config.frame;
            uint64_t frame = (q.beforeGlobalT != UINT64_MAX) ? (q.beforeGlobalT / frameT) : 0;
            q.beforeGlobalT = frame * frameT + tin;
        }
    }

    if (!hasAddr)
    {
        session.SendResponse(std::string("Error: --addr is required") + NEWLINE +
                             "Usage: ttd find-last --addr <A> [--access write|read|execute|io] "
                             "[--value V] [--pc-from X] [--pc-to Y] "
                             "[--before-frame F] [--before-tin T]" + NEWLINE);
        return;
    }

    ttd::TTDExternalEvent blockingMarker;
    auto result = mgr->FindLastAccess(q, &blockingMarker);

    if (result)
    {
        std::stringstream ss;
        ss << "TTD: Match found" << NEWLINE;
        ss << "  Frame:    " << result->time.frame << NEWLINE;
        ss << "  tInFrame: " << result->time.tInFrame << NEWLINE;
        ss << "  PC:       0x" << std::hex << std::uppercase << std::setfill('0')
           << std::setw(4) << result->pc << NEWLINE;
        ss << "  Value:    0x" << std::setw(2) << static_cast<int>(result->value) << NEWLINE;
        ss << "  PhysPage: " << std::dec << static_cast<int>(result->physPage) << NEWLINE;
        ss << "  Access:   " << ttd::TTDAccessTypeToString(result->access) << NEWLINE;
        session.SendResponse(ss.str());
    }
    else if (blockingMarker.reason[0] != '\0')
    {
        std::stringstream ss;
        ss << "TTD: Search blocked by external-event marker" << NEWLINE;
        ss << "  Marker at frame=" << blockingMarker.time.frame
           << " tInFrame=" << blockingMarker.time.tInFrame << NEWLINE;
        ss << "  Kind: " << ttd::TTDExternalEventKindToString(blockingMarker.kind) << NEWLINE;
        ss << "  Reason: " << blockingMarker.reason << NEWLINE;
        session.SendResponse(ss.str());
    }
    else
    {
        session.SendResponse(std::string("TTD: No match found") + NEWLINE);
    }
}

void CLIProcessor::HandleTTDStepInstruction(const ClientSession& session, EmulatorContext* context,
                                             const std::vector<std::string>& args)
{
    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;

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

    bool ok = forward ? mgr->StepForwardInstruction() : mgr->StepBackInstruction();
    if (ok)
    {
        ttd::TTDTimePoint pos = mgr->CurrentPosition();
        std::stringstream ss;
        ss << "TTD: Stepped " << (forward ? "forward" : "back")
           << " to (frame=" << pos.frame << ", tInFrame=" << pos.tInFrame << ")" << NEWLINE;
        session.SendResponse(ss.str());
    }
    else
    {
        session.SendResponse(std::string("TTD: Cannot step ") +
                             (forward ? "forward (at session end)" : "back (at session start)") +
                             NEWLINE);
    }
}

void CLIProcessor::HandleTTDReverseStep(const ClientSession& session, EmulatorContext* context,
                                          const std::vector<std::string>& args)
{
    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;

    // Parse --count N and --tstates T from args[1..]
    uint32_t count   = 1;
    bool     hasCount = false;
    uint64_t tstates  = 0;
    bool     hasTstates = false;

    for (size_t i = 1; i < args.size(); ++i)
    {
        const std::string& tok = args[i];
        if (tok == "--count" && i + 1 < args.size())
        {
            count = static_cast<uint32_t>(std::stoul(args[++i]));
            hasCount = true;
        }
        else if (tok == "--tstates" && i + 1 < args.size())
        {
            tstates = std::stoull(args[++i]);
            hasTstates = true;
        }
    }

    bool ok;
    ttd::TTDTimePoint pos{};
    if (hasTstates)
    {
        ok = mgr->ReverseStepTStates(tstates);
    }
    else
    {
        ok = mgr->ReverseStepInstructions(count);
    }
    (void)hasCount;

    if (ok)
    {
        pos = mgr->CurrentPosition();
        std::stringstream ss;
        if (hasTstates)
        {
            ss << "TTD: Stepped back " << tstates << " t-states to ";
        }
        else
        {
            ss << "TTD: Stepped back " << count << " instruction"
               << (count == 1 ? "" : "s") << " to ";
        }
        ss << "(frame=" << pos.frame << ", tInFrame=" << pos.tInFrame << ")" << NEWLINE;
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
    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;

    // Parse one or more --pc <V> arguments.
    std::vector<uint16_t> bps;
    for (size_t i = 1; i < args.size(); ++i)
    {
        const std::string& tok = args[i];
        if (tok == "--pc" && i + 1 < args.size())
        {
            bps.push_back(static_cast<uint16_t>(std::stoul(args[++i], nullptr, 0)));
        }
    }

    if (bps.empty())
    {
        session.SendResponse(std::string("Error: --pc is required (at least one)") + NEWLINE +
                             "Usage: ttd reverse-continue --pc <A> [--pc <B> ...]" + NEWLINE);
        return;
    }

    auto result = mgr->ReverseContinue(bps);

    std::stringstream ss;
    if (result.matched)
    {
        ss << "TTD: Reverse-continue hit PC=0x" << std::hex << std::uppercase
           << std::setfill('0') << std::setw(4) << result.pc << std::dec
           << " at (frame=" << result.arrivedAt.frame
           << ", tInFrame=" << result.arrivedAt.tInFrame << ")" << NEWLINE;
    }
    else if (result.blockingMarker.reason[0] != '\0')
    {
        ss << "TTD: Reverse-continue blocked by marker at "
           << "(frame=" << result.blockingMarker.time.frame
           << ", tInFrame=" << result.blockingMarker.time.tInFrame << ")" << NEWLINE;
        ss << "  Kind: " << ttd::TTDExternalEventKindToString(result.blockingMarker.kind) << NEWLINE;
        ss << "  Reason: " << result.blockingMarker.reason << NEWLINE;
    }
    else
    {
        ss << "TTD: Reverse-continue found no match (reached session start)" << NEWLINE;
    }
    session.SendResponse(ss.str());
}

/// endregion </TTD Commands>
