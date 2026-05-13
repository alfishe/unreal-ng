/// @author rfishe
/// @date 08.01.2026
/// @brief CLI Snapshot control commands handler

#include "cli-processor.h"

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <common/filehelper.h>

#include <sstream>

/// region <Snapshot Control Commands>

void CLIProcessor::HandleSnapshot(const ClientSession& session, const std::vector<std::string>& args)
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

    if (args.empty())
    {
        ShowSnapshotHelp(session);
        return;
    }

    std::string subcommand = args[0];

    if (subcommand == "load")
    {
        HandleSnapshotLoad(session, emulator, context, args);
    }
    else if (subcommand == "save")
    {
        HandleSnapshotSave(session, emulator, args);
    }
    else if (subcommand == "info")
    {
        HandleSnapshotInfo(session, context);
    }
    else
    {
        session.SendResponse(std::string("Error: Unknown subcommand '") + args[0] + "'" + NEWLINE +
                             "Use 'snapshot' without arguments to see available subcommands." + NEWLINE);
    }
}

void CLIProcessor::HandleSnapshotLoad(const ClientSession& session,
                                      std::shared_ptr<Emulator> emulator,
                                      EmulatorContext* context,
                                      const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing file path") + NEWLINE +
                            "Usage: snapshot load <file>" + NEWLINE);
        return;
    }

    std::string filepath = args[1];

    // Use existing LoadSnapshot method (includes path validation)
    bool success = emulator->LoadSnapshot(filepath);

    if (success)
    {
        session.SendResponse(std::string("Snapshot loaded: ") + filepath + NEWLINE);
    }
    else
    {
        session.SendResponse(std::string("Error: Failed to load snapshot: ") + filepath + NEWLINE);
    }
}

void CLIProcessor::HandleSnapshotInfo(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    ss << "Snapshot Status" << NEWLINE;
    ss << "===============" << NEWLINE;
    ss << NEWLINE;

    if (context->coreState.snapshotFilePath.empty())
    {
        ss << "No snapshot loaded" << NEWLINE;
    }
    else
    {
        ss << "File: " << context->coreState.snapshotFilePath << NEWLINE;
    }

    session.SendResponse(ss.str());
}

void CLIProcessor::ShowSnapshotHelp(const ClientSession& session)
{
    std::stringstream ss;
    ss << "Snapshot Commands" << NEWLINE;
    ss << "=================" << NEWLINE;
    ss << NEWLINE;
    ss << "  snapshot load <file>           Load snapshot from file (.z80, .sna)" << NEWLINE;
    ss << "  snapshot save <file> [--force] Save snapshot to file (.sna)" << NEWLINE;
    ss << "  snapshot info                  Get current snapshot status" << NEWLINE;
    ss << NEWLINE;

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleSnapshotSave(const ClientSession& session,
                                      std::shared_ptr<Emulator> emulator,
                                      const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing file path") + NEWLINE +
                            "Usage: snapshot save <file> [--force]" + NEWLINE);
        return;
    }

    std::string filepath = args[1];
    
    // Check for --force flag
    bool force = false;
    for (size_t i = 2; i < args.size(); i++)
    {
        if (args[i] == "--force" || args[i] == "-f")
        {
            force = true;
        }
    }
    
    // Check if file exists and force wasn't specified
    if (!force && FileHelper::FileExists(filepath))
    {
        session.SendResponse(std::string("Error: File already exists: ") + filepath + NEWLINE +
                            "Use --force to overwrite." + NEWLINE);
        return;
    }

    // Use SaveSnapshot method
    bool success = emulator->SaveSnapshot(filepath);

    if (success)
    {
        session.SendResponse(std::string("Snapshot saved: ") + filepath + NEWLINE);
    }
    else
    {
        session.SendResponse(std::string("Error: Failed to save snapshot: ") + filepath + NEWLINE);
    }
}

/// endregion </Snapshot Control Commands>
