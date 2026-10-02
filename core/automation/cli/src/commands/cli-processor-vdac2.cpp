/**
 * @file cli-processor-vdac2.cpp
 * @brief TS-Conf VDAC2 card (FT812) command handlers for the CLI processor
 *
 * Handles the `vdac2` command family:
 *   vdac2 capture start <path>   write the FT812's bus traffic to an .evr replay stream
 *   vdac2 capture stop
 *   vdac2 capture status
 *
 * Design: docs/inprogress/2026-10-01-tsconf-vdac2/ (vdac2-test-corpus.md §4: the format).
 * Every surface goes through Vdac2Control, so all of them answer the same.
 */

#include "cli-processor.h"

#include <algorithm>
#include <sstream>

#include "emulator/emulatorcontext.h"
#include "emulator/platforms/tsconf/vdac2control.h"

void CLIProcessor::ShowVdac2Help(const ClientSession& session)
{
    std::ostringstream out;
    out << "vdac2 - TS-Conf VDAC2 card (FT812)" << NEWLINE
        << "  vdac2 capture start <path>  write the FT812 bus (chip selects, bytes and answers, FT812 clocks," << NEWLINE
        << "                              per-frame picture hashes) to an .evr replay stream; started on a" << NEWLINE
        << "                              running chip, the stream begins with the chip's whole state" << NEWLINE
        << "  vdac2 capture stop          finish the stream" << NEWLINE
        << "  vdac2 capture status        the running or the last capture" << NEWLINE
        << "Needs TS-Conf in the VDAC2 build ([MISC] TS_VDAC2=1)." << NEWLINE;
    session.SendResponse(out.str());
}

void CLIProcessor::HandleVdac2(const ClientSession& session, const std::vector<std::string>& args)
{
    if (args.size() < 2 || args[0] != "capture")
    {
        ShowVdac2Help(session);
        return;
    }

    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select' or 'list' to manage emulators." + std::string(NEWLINE));
        return;
    }
    EmulatorContext* context = emulator->GetContext();

    std::string action = args[1];
    std::transform(action.begin(), action.end(), action.begin(), ::tolower);
    std::string error;
    if (action == "start")
    {
        if (args.size() < 3)
        {
            session.SendResponse("Usage: vdac2 capture start <path>" + std::string(NEWLINE));
            return;
        }
        if (!Vdac2Control::StartCapture(context, args[2], &error))
        {
            session.SendResponse("VDAC2 capture not started: " + error + NEWLINE);
            return;
        }
    }
    else if (action == "stop")
    {
        if (!Vdac2Control::StopCapture(context, &error))
        {
            session.SendResponse("VDAC2 capture not stopped: " + error + NEWLINE);
            return;
        }
    }
    else if (action != "status")
    {
        ShowVdac2Help(session);
        return;
    }

    Vdac2Control::CaptureStatus status;
    if (!Vdac2Control::GetCaptureStatus(context, status, &error))
    {
        session.SendResponse("VDAC2: " + error + NEWLINE);
        return;
    }
    std::ostringstream out;
    out << "VDAC2 capture: " << (status.capturing ? "running" : "stopped");
    if (!status.path.empty())
        out << ", file " << status.path << ", " << status.bytesWritten << " bytes, " << status.exchanges << " bytes on the bus, "
            << status.selects << " chip selects, " << status.frames << " frames, FT812 clocks " << status.startClock << ".."
            << status.lastClock;
    out << NEWLINE;
    session.SendResponse(out.str());
}
