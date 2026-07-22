/// @file cli-processor-ttd.cpp
/// @brief CLI Time-Travel Debug (TTD) command handlers.
///
/// Exposes the full TTD engine surface (Phase 2 complete) to CLI automation:
///   ttd status                 — session info (state, frame range, checkpoint count)
///   ttd start                  — begin recording (captures baseline checkpoint)
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
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>

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
        HandleTTDStart(session, context);
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
    ss << "  ttd start                        Begin recording (captures baseline checkpoint)" << NEWLINE;
    ss << "  ttd stop                         Stop recording (history retained, browsable)" << NEWLINE;
    ss << "  ttd invalidate [reason]          Drop all history, return to Idle" << NEWLINE;
    ss << "  ttd seek <frame> [tinframe]      Seek to a point in the timeline" << NEWLINE;
    ss << "  ttd step-back                    Step back one frame (preserve intra-frame pos)" << NEWLINE;
    ss << "  ttd step-forward                 Step forward one frame (preserve intra-frame pos)" << NEWLINE;
    ss << "  ttd resume [frame] [tinframe]    Resume recording from current or specified point" << NEWLINE;
    ss << "  ttd position                     Show current TTDTimePoint (frame + tInFrame)" << NEWLINE;
    ss << "  ttd markers                      List external-event markers (replay barriers)" << NEWLINE;
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

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleTTDStart(const ClientSession& session, EmulatorContext* context)
{
    ttd::TimeTravelManager* mgr = context->pTimeTravelManager;

    if (mgr->IsRecording())
    {
        session.SendResponse(std::string("TTD: Already recording (no-op)") + NEWLINE);
        return;
    }

    bool ok = mgr->StartRecording();
    if (ok)
    {
        session.SendResponse(std::string("TTD: Recording started") + NEWLINE);
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

/// endregion </TTD Commands>
