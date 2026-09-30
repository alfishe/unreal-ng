/// @brief CLI RZX playback commands: rzx play / stop / status
/// (docs/inprogress/2026-09-29-rzx-replay, design §13).
///
/// Example: `rzx play games/eric.rzx --tolerant` plays on the selected machine
/// (switching the model first when the recording needs another one); `rzx
/// status` prints the frame, progress and desyncs.

#include "cli-processor.h"

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/rzx/rzxlauncher.h>

#include <cstdlib>
#include <sstream>

/// region <RZX Playback Commands>

void CLIProcessor::HandleRzx(const ClientSession& session, const std::vector<std::string>& args)
{
    if (args.empty() || args[0] == "help")
    {
        ShowRzxHelp(session);
        return;
    }

    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
        return;

    const std::string& subcommand = args[0];
    if (subcommand == "stop")
    {
        const bool stopped = emulator->StopRzx();
        session.SendResponse(std::string(stopped ? "RZX playback stopped; the machine runs live"
                                                 : "No RZX playback was running") +
                             NEWLINE);
        return;
    }
    if (subcommand == "seek")
    {
        if (args.size() < 2)
        {
            session.SendResponse(std::string("Error: Missing frame") + NEWLINE + "Usage: rzx seek <frame>" + NEWLINE);
            return;
        }
        char* end = nullptr;
        const unsigned long long frame = std::strtoull(args[1].c_str(), &end, 10);
        if (end == args[1].c_str() || *end != 0)
        {
            session.SendResponse("Error: '" + args[1] + "' is not a frame number" + NEWLINE);
            return;
        }
        std::string error;
        if (!emulator->SeekRzx(frame, &error))
        {
            session.SendResponse("Error: " + error + NEWLINE);
            return;
        }
        session.SendResponse("Sought: " + rzx::RzxLauncher::StatusLine(emulator->GetRzxStatus()) + NEWLINE);
        return;
    }
    if (subcommand == "status")
    {
        std::string text = rzx::RzxLauncher::StatusText(emulator->GetRzxStatus());
        std::string out;
        for (char c : text)
        {
            if (c == '\n')
                out += NEWLINE;
            else
                out += c;
        }
        session.SendResponse(out);
        return;
    }
    if (subcommand != "play")
    {
        session.SendResponse("Error: Unknown subcommand '" + subcommand + "'" + NEWLINE +
                             "Use 'rzx' without arguments to see available subcommands." + NEWLINE);
        return;
    }

    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing file path") + NEWLINE + "Usage: rzx play <file> [options]" +
                             NEWLINE);
        return;
    }

    rzx::LaunchRequest request;
    request.emulatorId = emulator->GetId();
    request.path = args[1];
    for (size_t i = 2; i < args.size(); i++)
    {
        const std::string& option = args[i];
        if (option == "--tolerant")
            request.options.desyncMode = rzx::DesyncMode::Tolerant;
        else if (option == "--strict")
            request.options.desyncMode = rzx::DesyncMode::Strict;
        else if (option == "--ei-short-frame")
            request.options.eiShortFrameBlocksInt = true;
        else if (option == "--ld-air-quirk")
            request.options.ldAirParityQuirk = true;
        else if (option == "--ignore-later-snapshots")
            request.options.ignoreLaterSnapshots = true;
        else if (option == "--no-switch")
            request.switchModel = false;
        else
        {
            session.SendResponse("Error: Unknown option '" + option + "'" + NEWLINE);
            return;
        }
    }
    emulator.reset();

    const rzx::LaunchResult result = rzx::RzxLauncher::Play(request);
    std::stringstream ss;
    if (!result.play.Ok())
    {
        ss << "Error: " << result.play.message << NEWLINE;
        if (result.play.error == rzx::PlayError::ModelMismatch)
            ss << "Switch with 'model " << result.play.requiredModel << "', or play without --no-switch" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    if (result.modelSwitched)
    {
        ss << "Switched to " << result.switchedToModel << " for the recording" << NEWLINE
           << "New emulator instance: " << result.emulator->GetId() << NEWLINE;
    }
    ss << "Playing " << request.path << ": " << rzx::RzxLauncher::StatusLine(result.emulator->GetRzxStatus())
       << NEWLINE;
    session.SendResponse(ss.str());
}

void CLIProcessor::ShowRzxHelp(const ClientSession& session)
{
    std::stringstream ss;
    ss << "RZX Playback Commands" << NEWLINE;
    ss << "=====================" << NEWLINE;
    ss << NEWLINE;
    ss << "  rzx play <file> [options]  Play an RZX input recording from its start snapshot" << NEWLINE;
    ss << "      --tolerant               count desyncs and go on (default: stop at the first)" << NEWLINE;
    ss << "      --ei-short-frame         a 1-2 fetch frame after EI blocks the interrupt" << NEWLINE;
    ss << "      --ld-air-quirk           NMOS LD A,I / LD A,R parity quirk on the frame interrupt" << NEWLINE;
    ss << "      --ignore-later-snapshots skip snapshot blocks after the first" << NEWLINE;
    ss << "      --no-switch              refuse instead of switching to the recording's model" << NEWLINE;
    ss << "  rzx seek <frame>           Move to the boundary after <frame> frames (0 = start; back via keyframes)" << NEWLINE;
    ss << "  rzx stop                   Stop playing; the machine runs live" << NEWLINE;
    ss << "  rzx status                 Frame, progress, desyncs, drift, options" << NEWLINE;
    ss << NEWLINE;
    ss << "'snapshot load <file.rzx>' plays on the selected machine without switching the model." << NEWLINE;
    session.SendResponse(ss.str());
}

/// endregion </RZX Playback Commands>
