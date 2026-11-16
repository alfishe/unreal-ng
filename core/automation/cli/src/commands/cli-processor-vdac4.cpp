/**
 * @file cli-processor-vdac4.cpp
 * @brief TS-Conf VDAC4 board (Tang FT812 core + ESP32-S3) command handlers for the CLI processor
 *
 * Handles the `vdac4` command family:
 *   vdac4 state [log N]   the Tang's personality, the ESP's phase, the running app, the link, the audio and
 *                         the last N ESP log lines (default 10)
 *   vdac4 status          the same as `vdac4 state`
 *   vdac4 log [N]         only the ESP log (default 50, at most 256)
 *
 * Design: docs/inprogress/2026-11-15-tsconf-vdac4/design.md §10.2.
 * Every surface goes through Vdac4Control, so all of them answer the same.
 */

#include "cli-processor.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "emulator/emulatorcontext.h"
#include "emulator/platforms/tsconf/vdac4/vdac4control.h"

namespace
{

/// A decimal line count, capped at 256; false when `text` is not a number
bool ParseLogLines(const std::string& text, size_t& lines)
{
    if (text.empty() || text.size() > 6 || text.find_first_not_of("0123456789") != std::string::npos)
        return false;
    lines = std::min<size_t>(std::strtoul(text.c_str(), nullptr, 10), 256);
    return true;
}

std::string Quoted(const std::string& text)
{
    return text.empty() ? std::string("-") : "\"" + text + "\"";
}

} // namespace

void CLIProcessor::ShowVdac4Help(const ClientSession& session)
{
    std::ostringstream out;
    out << "vdac4 - TS-Conf VDAC4 board (Tang FT812 core + ESP32-S3)" << NEWLINE
        << "  vdac4 state [log N]  the Tang's personality and FT_CFG, the ESP's phase, boots and errors, the" << NEWLINE
        << "                       running app, the link counters, the audio player and the last N ESP log" << NEWLINE
        << "                       lines (default 10, at most 256)" << NEWLINE
        << "  vdac4 status         the same as 'vdac4 state'" << NEWLINE
        << "  vdac4 log [N]        only the ESP log (default 50, at most 256)" << NEWLINE
        << "Needs TS-Conf with the board ([MISC] TS_VDAC4=1, the TSL-VDAC4 model)." << NEWLINE;
    session.SendResponse(out.str());
}

void CLIProcessor::HandleVdac4(const ClientSession& session, const std::vector<std::string>& args)
{
    std::string action = args.empty() ? std::string() : args[0];
    std::transform(action.begin(), action.end(), action.begin(), ::tolower);
    if (action != "state" && action != "status" && action != "log")
    {
        ShowVdac4Help(session);
        return;
    }

    const bool logOnly = action == "log";
    size_t logLines = logOnly ? 50 : 10;
    if (logOnly && args.size() == 2)
    {
        if (!ParseLogLines(args[1], logLines))
        {
            session.SendResponse("Usage: vdac4 log [N]" + std::string(NEWLINE));
            return;
        }
    }
    else if (!logOnly && args.size() == 3 && args[1] == "log")
    {
        if (!ParseLogLines(args[2], logLines))
        {
            session.SendResponse("Usage: vdac4 state [log N]" + std::string(NEWLINE));
            return;
        }
    }
    else if (args.size() != 1)
    {
        ShowVdac4Help(session);
        return;
    }

    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select' or 'list' to manage emulators." + std::string(NEWLINE));
        return;
    }

    Vdac4Control::BoardState s;
    std::string error;
    if (!Vdac4Control::GetState(emulator->GetContext(), s, logLines, &error))
    {
        session.SendResponse("VDAC4: " + error + NEWLINE);
        return;
    }

    std::ostringstream out;
    if (!logOnly)
    {
        out << "VDAC4 board" << NEWLINE
            << "  Tang:    personality " << s.personality << (s.armed ? ", armed" : "") << (s.espHost ? ", ESP host" : "")
            << ", RAM_G " << (s.ramG3M ? "3 MB" : "1 MB") << ", FT_CFG 0x" << std::hex << s.ftCfg << std::dec << NEWLINE
            << "  ESP:     " << s.espPhase << ", " << s.espMs << " ms, boots " << s.boots << " (nonce "
            << static_cast<unsigned>(s.bootNonce) << "), last step " << static_cast<unsigned>(s.lastStep) << ", resources "
            << s.resources << ", clock restarts " << s.clockRestarts << ", ahead " << s.clockOffsetUs << " us" << NEWLINE;
        if (!s.lastError.empty())
            out << "  ESP error: " << s.lastError << NEWLINE;
        out << "  App:     ";
        if (s.appRunning)
        {
            out << "running " << Quoted(s.appName) << " " << s.appVersion << " (" << s.appModule << ")";
            if (s.commandFirst <= s.commandLast)
                out << ", commands " << static_cast<unsigned>(s.commandFirst) << ".." << static_cast<unsigned>(s.commandLast);
        }
        else
            out << "none" << (s.appModule.empty() ? "" : " (last " + s.appModule + ")");
        out << NEWLINE;
        if (!s.appError.empty())
            out << "  App error: " << s.appError << NEWLINE;
        out << "  Link:    " << s.packets << " packets, pending from Z80 " << s.pendingFromZ80 << " / to Z80 " << s.pendingToZ80
            << (s.sendBlocked ? ", send blocked" : "") << ", bad checksums " << s.badChecksums << ", resyncs " << s.resyncs
            << ", dropped frames " << s.droppedFrames << NEWLINE;
        if (!s.recentPackets.empty())
        {
            const Vdac4Control::BoardState::Packet& last = s.recentPackets.back();
            out << "  Last packet: command 0x" << std::hex << static_cast<unsigned>(last.command) << std::dec << ", "
                << last.length << " bytes at " << last.ms << " ms" << NEWLINE;
        }
        out << "  Audio:   ";
        if (s.audioInputRate == 0)
            out << "never opened";
        else
            out << s.audioInputRate << " Hz -> " << s.audioOutputRate << " Hz, queued " << s.audioQueued << ", underruns "
                << s.audioUnderruns << ", dropped " << s.audioDropped << (s.audioSound ? ", sounding" : ", silent");
        out << NEWLINE;
    }
    out << (logOnly ? "VDAC4 ESP log" : "  Log") << " (" << s.log.size() << " line(s))" << (s.log.empty() ? "" : ":") << NEWLINE;
    for (const std::string& line : s.log)
        out << "    " << line << NEWLINE;
    session.SendResponse(out.str());
}
