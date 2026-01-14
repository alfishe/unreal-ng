/// @file cli-processor-batch.cpp
/// @author antigravity
/// @date 2026-01-13
/// @brief CLI Batch command execution handler
///
/// Implements batch mode for collecting and executing multiple commands in parallel.
/// Commands:
///   batch start   - Enter batch mode
///   batch add     - Add command to batch (in batch mode: just type command)
///   batch execute - Execute all queued commands
///   batch cancel  - Cancel batch and exit batch mode
///   batch list    - List queued commands
///   batch status  - Show batch execution status

#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>

#include <sstream>

#include "batch-command-processor.h"
#include "cli-processor.h"

/// region <Batch Command Execution>

void CLIProcessor::HandleBatch(const ClientSession& session, const std::vector<std::string>& args)
{
    if (args.empty())
    {
        ShowBatchHelp(session);
        return;
    }

    std::string subcommand = args[0];

    if (subcommand == "start")
    {
        HandleBatchStart(session, args);
    }
    else if (subcommand == "execute" || subcommand == "exec")
    {
        HandleBatchExecute(session, args);
    }
    else if (subcommand == "cancel" || subcommand == "abort")
    {
        HandleBatchCancel(session, args);
    }
    else if (subcommand == "list" || subcommand == "ls")
    {
        HandleBatchList(session, args);
    }
    else if (subcommand == "status")
    {
        HandleBatchStatus(session, args);
    }
    else if (subcommand == "commands")
    {
        HandleBatchCommands(session, args);
    }
    else
    {
        session.SendResponse(std::string("Error: Unknown batch subcommand '") + subcommand + "'" + NEWLINE +
                             "Use 'batch' without arguments to see available subcommands." + NEWLINE);
    }
}

void CLIProcessor::HandleBatchStart(const ClientSession& session, const std::vector<std::string>& args)
{
    // Check if already in batch mode
    if (session.batchModeActive)
    {
        session.SendResponse(
            std::string("Already in batch mode. Use 'batch execute' to run or 'batch cancel' to exit.") + NEWLINE);
        return;
    }

    // Enter batch mode
    session.batchModeActive = true;
    session.batchCommands.clear();
    session.batchPrompt = "[batch]> ";

    std::stringstream ss;
    ss << "Batch mode started. Type commands to queue, then:" << NEWLINE;
    ss << "  batch execute   Execute all queued commands in parallel" << NEWLINE;
    ss << "  batch list      Show queued commands" << NEWLINE;
    ss << "  batch cancel    Cancel batch and exit" << NEWLINE;
    ss << NEWLINE;
    ss << "Batchable commands: ";

    auto commands = BatchCommandProcessor::GetBatchableCommands();
    for (size_t i = 0; i < commands.size(); i++)
    {
        if (i > 0)
            ss << ", ";
        ss << commands[i];
    }
    ss << NEWLINE;

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleBatchExecute(const ClientSession& session, const std::vector<std::string>& args)
{
    if (!session.batchModeActive)
    {
        session.SendResponse(std::string("Not in batch mode. Use 'batch start' first.") + NEWLINE);
        return;
    }

    if (session.batchCommands.empty())
    {
        session.SendResponse(std::string("No commands queued. Add commands first.") + NEWLINE);
        return;
    }

    // Execute batch
    EmulatorManager* manager = EmulatorManager::GetInstance();
    BatchCommandProcessor processor(manager);
    BatchResult result = processor.Execute(session.batchCommands);

    // Format output
    std::stringstream ss;
    ss << "Batch Execution Complete" << NEWLINE;
    ss << "========================" << NEWLINE;
    ss << NEWLINE;
    ss << "Total:     " << result.total << NEWLINE;
    ss << "Succeeded: " << result.succeeded << NEWLINE;
    ss << "Failed:    " << result.failed << NEWLINE;
    ss << "Duration:  " << std::fixed << std::setprecision(2) << result.durationMs << " ms" << NEWLINE;
    ss << NEWLINE;

    if (result.failed > 0)
    {
        ss << "Failures:" << NEWLINE;
        for (const auto& r : result.results)
        {
            if (!r.success)
            {
                ss << "  [" << r.emulatorId << "] " << r.command << ": " << r.error << NEWLINE;
            }
        }
        ss << NEWLINE;
    }

    session.SendResponse(ss.str());

    // Exit batch mode
    session.batchModeActive = false;
    session.batchCommands.clear();
    session.batchPrompt = "";
}

void CLIProcessor::HandleBatchCancel(const ClientSession& session, const std::vector<std::string>& args)
{
    if (!session.batchModeActive)
    {
        session.SendResponse(std::string("Not in batch mode.") + NEWLINE);
        return;
    }

    int count = static_cast<int>(session.batchCommands.size());
    session.batchModeActive = false;
    session.batchCommands.clear();
    session.batchPrompt = "";

    session.SendResponse(std::string("Batch cancelled. ") + std::to_string(count) + " commands discarded." + NEWLINE);
}

void CLIProcessor::HandleBatchList(const ClientSession& session, const std::vector<std::string>& args)
{
    if (!session.batchModeActive)
    {
        session.SendResponse(std::string("Not in batch mode. Use 'batch start' first.") + NEWLINE);
        return;
    }

    std::stringstream ss;
    ss << "Queued Commands (" << session.batchCommands.size() << ")" << NEWLINE;
    ss << "==============================" << NEWLINE;

    if (session.batchCommands.empty())
    {
        ss << "(empty)" << NEWLINE;
    }
    else
    {
        int idx = 0;
        for (const auto& cmd : session.batchCommands)
        {
            ss << std::setw(3) << idx++ << ". [" << cmd.emulatorId << "] " << cmd.command;
            if (!cmd.arg1.empty())
                ss << " " << cmd.arg1;
            if (!cmd.arg2.empty())
                ss << " " << cmd.arg2;
            ss << NEWLINE;
        }
    }
    ss << NEWLINE;

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleBatchStatus(const ClientSession& session, const std::vector<std::string>& args)
{
    std::stringstream ss;
    ss << "Batch Status" << NEWLINE;
    ss << "============" << NEWLINE;
    ss << "Mode:     " << (session.batchModeActive ? "ACTIVE" : "inactive") << NEWLINE;
    ss << "Queued:   " << session.batchCommands.size() << " commands" << NEWLINE;
    ss << NEWLINE;

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleBatchCommands(const ClientSession& session, const std::vector<std::string>& args)
{
    std::stringstream ss;
    ss << "Batchable Commands" << NEWLINE;
    ss << "==================" << NEWLINE;
    ss << NEWLINE;

    auto commands = BatchCommandProcessor::GetBatchableCommands();
    for (const auto& cmd : commands)
    {
        ss << "  " << cmd << NEWLINE;
    }
    ss << NEWLINE;
    ss << "Total: " << commands.size() << " commands" << NEWLINE;

    session.SendResponse(ss.str());
}

/// @brief Add a command to the batch queue (called during batch mode)
void CLIProcessor::AddToBatch(const ClientSession& session, const std::string& emulatorId, const std::string& command,
                              const std::string& arg1, const std::string& arg2)
{
    BatchCommand cmd;
    cmd.emulatorId = emulatorId;
    cmd.command = command;
    cmd.arg1 = arg1;
    cmd.arg2 = arg2;

    session.batchCommands.push_back(cmd);

    session.SendResponse(std::string("Queued: [") + emulatorId + "] " + command + (arg1.empty() ? "" : " " + arg1) +
                         (arg2.empty() ? "" : " " + arg2) + " (" + std::to_string(session.batchCommands.size()) +
                         " total)" + NEWLINE);
}

void CLIProcessor::ShowBatchHelp(const ClientSession& session)
{
    std::stringstream ss;
    ss << "Batch Command Execution" << NEWLINE;
    ss << "=======================" << NEWLINE;
    ss << NEWLINE;
    ss << "Commands:" << NEWLINE;
    ss << "  batch start              Enter batch mode" << NEWLINE;
    ss << "  batch execute            Execute all queued commands in parallel" << NEWLINE;
    ss << "  batch cancel             Cancel batch and exit batch mode" << NEWLINE;
    ss << "  batch list               List queued commands" << NEWLINE;
    ss << "  batch status             Show batch mode status" << NEWLINE;
    ss << "  batch commands           List batchable command names" << NEWLINE;
    ss << NEWLINE;
    ss << "Workflow:" << NEWLINE;
    ss << "  1. batch start" << NEWLINE;
    ss << "  2. Type commands (one per line) - they are queued, not executed" << NEWLINE;
    ss << "  3. batch execute" << NEWLINE;
    ss << NEWLINE;
    ss << "Example:" << NEWLINE;
    ss << "  > batch start" << NEWLINE;
    ss << "  [batch]> 0 load-snapshot /path/to/game.sna" << NEWLINE;
    ss << "  [batch]> 1 load-snapshot /path/to/game.sna" << NEWLINE;
    ss << "  [batch]> 2 feature sound off" << NEWLINE;
    ss << "  [batch]> batch execute" << NEWLINE;
    ss << NEWLINE;

    session.SendResponse(ss.str());
}

/// endregion </Batch Command Execution>
