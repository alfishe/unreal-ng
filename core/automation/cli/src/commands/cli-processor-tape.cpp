#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/io/tape/tape.h>
#include <emulator/platform.h>

#include <algorithm>
#include <sstream>

#include "cli-processor.h"
#include "common/timehelper.h"

/// region <Tape Control Commands>

void CLIProcessor::HandleTape(const ClientSession& session, const std::vector<std::string>& args)
{
    // Get the selected emulator
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }

    // Get emulator context
    EmulatorContext* context = emulator->GetContext();
    if (!context)
    {
        session.SendResponse(std::string("Error: Unable to access emulator context.") + NEWLINE);
        return;
    }

    // If no arguments, show usage
    if (args.empty())
    {
        std::stringstream ss;
        ss << "Usage: tape <subcommand> [args]" << NEWLINE;
        ss << NEWLINE;
        ss << "Available subcommands:" << NEWLINE;
        ss << "  load <file>   - Load tape image (.tap, .tzx)" << NEWLINE;
        ss << "  eject         - Eject current tape" << NEWLINE;
        ss << "  play          - Start tape playback" << NEWLINE;
        ss << "  stop          - Stop tape playback" << NEWLINE;
        ss << "  rewind        - Rewind tape to beginning" << NEWLINE;
        ss << "  info          - Show tape status" << NEWLINE;
        ss << NEWLINE;
        ss << "Examples:" << NEWLINE;
        ss << "  tape load /path/to/game.tap" << NEWLINE;
        ss << "  tape play" << NEWLINE;
        ss << "  tape info" << NEWLINE;

        session.SendResponse(ss.str());
        return;
    }

    std::string subcommand = args[0];
    std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

    // Dispatch to subcommand handlers
    if (subcommand == "load")
    {
        HandleTapeLoad(session, emulator, context, args);
    }
    else if (subcommand == "eject")
    {
        HandleTapeEject(session, emulator, context);
    }
    else if (subcommand == "play")
    {
        HandleTapePlay(session, emulator, context);
    }
    else if (subcommand == "stop")
    {
        HandleTapeStop(session, emulator, context);
    }
    else if (subcommand == "rewind")
    {
        HandleTapeRewind(session, emulator, context);
    }
    else if (subcommand == "info")
    {
        HandleTapeInfo(session, context);
    }
    else
    {
        session.SendResponse(std::string("Error: Unknown subcommand '") + args[0] + "'" + NEWLINE +
                             "Use 'tape' without arguments to see available subcommands." + NEWLINE);
    }
}

void CLIProcessor::HandleTapeLoad(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                  EmulatorContext* context, const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing file path") + NEWLINE + "Usage: tape load <file>" + NEWLINE);
        return;
    }

    std::string filepath = args[1];

    // Use existing LoadTape method (already handles file loading)
    bool success = emulator->LoadTape(filepath);

    if (success)
    {
        session.SendResponse(std::string("Tape loaded: ") + filepath + NEWLINE);
    }
    else
    {
        session.SendResponse(std::string("Error: Failed to load tape: ") + filepath + NEWLINE);
    }
}

void CLIProcessor::HandleTapeEject(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                   EmulatorContext* context)
{
    if (!context->pTape)
    {
        session.SendResponse(std::string("Error: Tape subsystem not available") + NEWLINE);
        return;
    }

    // Thread-safe: Pause emulator if running
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning)
    {
        emulator->Pause();
        sleep_ms(10);  // Give emulator time to pause
    }

    // Stop tape and clear filepath
    context->pTape->stopTape();
    context->coreState.tapeFilePath.clear();

    if (wasRunning)
    {
        emulator->Resume();
    }

    session.SendResponse(std::string("Tape ejected") + NEWLINE);
}

void CLIProcessor::HandleTapePlay(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                  EmulatorContext* context)
{
    if (!context->pTape)
    {
        session.SendResponse(std::string("Error: Tape subsystem not available") + NEWLINE);
        return;
    }

    if (context->coreState.tapeFilePath.empty())
    {
        session.SendResponse(std::string("Error: No tape loaded") + NEWLINE + "Use 'tape load <file>' first" + NEWLINE);
        return;
    }

    // Thread-safe: Pause emulator if running
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning)
    {
        emulator->Pause();
        sleep_ms(10);  // Give emulator time to pause
    }

    context->pTape->startTape();

    if (wasRunning)
    {
        emulator->Resume();
    }

    session.SendResponse(std::string("Tape playback started") + NEWLINE);
}

void CLIProcessor::HandleTapeStop(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                  EmulatorContext* context)
{
    if (!context->pTape)
    {
        session.SendResponse(std::string("Error: Tape subsystem not available") + NEWLINE);
        return;
    }

    // Thread-safe: Pause emulator if running
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning)
    {
        emulator->Pause();
        sleep_ms(10);  // Give emulator time to pause
    }

    context->pTape->stopTape();

    if (wasRunning)
    {
        emulator->Resume();
    }

    session.SendResponse(std::string("Tape playback stopped") + NEWLINE);
}

void CLIProcessor::HandleTapeRewind(const ClientSession& session, std::shared_ptr<Emulator> emulator,
                                    EmulatorContext* context)
{
    if (!context->pTape)
    {
        session.SendResponse(std::string("Error: Tape subsystem not available") + NEWLINE);
        return;
    }

    // Thread-safe: Pause emulator if running
    bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
    if (wasRunning)
    {
        emulator->Pause();
        sleep_ms(10);  // Give emulator time to pause
    }

    context->pTape->reset();

    if (wasRunning)
    {
        emulator->Resume();
    }

    session.SendResponse(std::string("Tape rewound to beginning") + NEWLINE);
}

void CLIProcessor::HandleTapeInfo(const ClientSession& session, EmulatorContext* context)
{
    if (!context->pTape)
    {
        session.SendResponse(std::string("Error: Tape subsystem not available") + NEWLINE);
        return;
    }

    std::stringstream ss;
    ss << "Tape Status" << NEWLINE;
    ss << "===========" << NEWLINE;
    ss << NEWLINE;

    if (context->coreState.tapeFilePath.empty())
    {
        ss << "No tape loaded" << NEWLINE;
    }
    else
    {
        ss << "File: " << context->coreState.tapeFilePath << NEWLINE;
        // Note: _tapeStarted is protected, cannot access directly
        // This is a read-only query, safe without pausing
        ss << "Status: " << (context->pTape ? "Ready" : "Not available") << NEWLINE;
    }

    session.SendResponse(ss.str());
}

/// endregion </Tape Control Commands>
