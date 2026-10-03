/**
 * @file cli-processor-vdac2.cpp
 * @brief TS-Conf VDAC2 card (FT812) command handlers for the CLI processor
 *
 * Handles the `vdac2` command family:
 *   vdac2 capture start <path>   write the FT812's bus traffic to an .evr replay stream
 *   vdac2 capture stop
 *   vdac2 capture status
 *   vdac2 metrics [lines] [inflight]   FT812 line budget of the last finished frame
 *   vdac2 metrics margin <percent>     soft budget, percent below the line period (0..50)
 *   vdac2 metrics always on|off        draw and measure every frame
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
        << "  vdac2 metrics [lines] [inflight]" << NEWLINE
        << "                              FT812 line budget of the last finished frame: worst line, lines over" << NEWLINE
        << "                              the soft / hard budget; 'lines' lists every line's cost, 'inflight'" << NEWLINE
        << "                              adds the frame in flight (paused machine)" << NEWLINE
        << "  vdac2 metrics margin <0..50> soft budget, percent below the line period" << NEWLINE
        << "  vdac2 metrics always on|off draw and measure every frame, also while not shown" << NEWLINE
        << "Needs TS-Conf in the VDAC2 build ([MISC] TS_VDAC2=1)." << NEWLINE;
    session.SendResponse(out.str());
}

void CLIProcessor::HandleVdac2Metrics(const ClientSession& session, EmulatorContext* context,
                                      const std::vector<std::string>& args)
{
    std::string error;
    bool withLines = false;
    bool inFlight = false;
    for (size_t i = 1; i < args.size(); ++i)
    {
        if (args[i] == "margin" && i + 1 < args.size())
        {
            const std::string& value = args[++i];
            if (value.empty() || value.size() > 2 || value.find_first_not_of("0123456789") != std::string::npos ||
                !Vdac2Control::SetLineBudgetMargin(context, static_cast<uint32_t>(std::stoul(value)), &error))
            {
                session.SendResponse("VDAC2 metrics: " + (error.empty() ? std::string("margin is a percent from 0 to 50") : error) +
                                     NEWLINE);
                return;
            }
        }
        else if (args[i] == "always" && i + 1 < args.size() && (args[i + 1] == "on" || args[i + 1] == "off"))
        {
            if (!Vdac2Control::SetMeasureAlways(context, args[++i] == "on", &error))
            {
                session.SendResponse("VDAC2 metrics: " + error + NEWLINE);
                return;
            }
        }
        else if (args[i] == "lines")
            withLines = true;
        else if (args[i] == "inflight" || args[i] == "in-flight")
            inFlight = true;
        else
        {
            session.SendResponse("Usage: vdac2 metrics [lines] [inflight] | vdac2 metrics margin <0..50> | "
                                 "vdac2 metrics always on|off" + std::string(NEWLINE));
            return;
        }
    }

    Vdac2Control::FrameMetrics m;
    if (!Vdac2Control::GetFrameMetrics(context, m, withLines, inFlight, &error))
    {
        session.SendResponse("VDAC2: " + error + NEWLINE);
        return;
    }
    std::ostringstream out;
    out << "VDAC2 line budget, FT812 frame " << m.frame << (m.valid ? "" : " (not measured: the frame was not drawn)") << NEWLINE
        << "  Budget:        " << m.hardBudget << " clocks per line, soft " << m.softBudget << " (margin " << m.margin
        << " %)" << NEWLINE << "  Worst line:    " << m.worstLine << " = " << m.worstClocks << " clocks" << NEWLINE
        << "  Over soft:     " << m.linesOverSoft << " line(s)" << NEWLINE << "  Over hard:     " << m.linesOverHard
        << " line(s) (broken on a real card)" << NEWLINE << "  Lines:         " << m.lines << ", total " << m.totalClocks
        << " clocks" << NEWLINE << "  Measure always: " << (m.measureAlways ? "on" : "off") << NEWLINE;
    if (withLines)
    {
        out << "  Line costs:";
        for (size_t i = 0; i < m.lineClocks.size(); ++i)
            out << (i % 16 == 0 ? std::string(NEWLINE) + "    " + std::to_string(i) + ":" : std::string()) << " "
                << m.lineClocks[i];
        out << NEWLINE;
    }
    if (inFlight)
    {
        if (!m.inFlightKnown)
            out << "  In flight:     unknown (the machine is running: pause it)" << NEWLINE;
        else
        {
            out << "  In flight:     " << m.inFlightLinesPassed << " line(s) passed" << NEWLINE;
            if (withLines && !m.inFlightLineClocks.empty())
            {
                out << "  In-flight costs (-1 = passed without drawing):";
                for (size_t i = 0; i < m.inFlightLineClocks.size(); ++i)
                    out << (i % 16 == 0 ? std::string(NEWLINE) + "    " + std::to_string(i) + ":" : std::string()) << " "
                        << m.inFlightLineClocks[i];
                out << NEWLINE;
            }
        }
    }
    session.SendResponse(out.str());
}

void CLIProcessor::HandleVdac2(const ClientSession& session, const std::vector<std::string>& args)
{
    if (args.empty() || (args[0] != "capture" && args[0] != "metrics") || (args[0] == "capture" && args.size() < 2))
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
    if (args[0] == "metrics")
    {
        HandleVdac2Metrics(session, context, args);
        return;
    }

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
