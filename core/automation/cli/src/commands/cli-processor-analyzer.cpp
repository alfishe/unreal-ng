// CLI Analyzer Commands
// Extracted from cli-processor.cpp - 2026-01-08

#include "cli-processor.h"

#include <debugger/analyzers/basicextractor.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>

#include <iostream>
#include <sstream>

// HandleBasic - lines 3050-3135
void CLIProcessor::HandleBasic(const ClientSession& session, const std::vector<std::string>& args)
{
    // Get the selected emulator
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }

    // Check for subcommand
    if (args.empty())
    {
        std::stringstream ss;
        ss << "BASIC commands:" << NEWLINE;
        ss << "  basic extract             - Extract BASIC program from memory" << NEWLINE;
        ss << "  basic extract <addr> <len> - Extract BASIC from specific memory region (not implemented)" << NEWLINE;
        ss << "  basic extract file <file>  - Extract BASIC from file (not implemented)" << NEWLINE;
        ss << "  basic save <file>          - Save extracted BASIC to text file (not implemented)" << NEWLINE;
        ss << "  basic load <file>          - Load ASCII BASIC from text file (not implemented)" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    std::string subcommand = args[0];

    if (subcommand == "extract")
    {
        if (args.size() == 1)
        {
            // Extract from memory using system variables
            BasicExtractor extractor;
            Memory* memory = emulator->GetMemory();

            if (!memory)
            {
                session.SendResponse(std::string("Error: Unable to access emulator memory.") + NEWLINE);
                return;
            }

            std::string basicListing = extractor.extractFromMemory(memory);

            if (basicListing.empty())
            {
                session.SendResponse(std::string("No BASIC program found in memory or invalid program structure.") +
                                     NEWLINE);
                return;
            }

            std::stringstream ss;
            ss << "BASIC Program:" << NEWLINE;
            ss << "----------------------------------------" << NEWLINE;
            ss << FormatForTerminal(basicListing);
            ss << "----------------------------------------" << NEWLINE;
            session.SendResponse(ss.str());
        }
        else if (args.size() == 3)
        {
            // basic extract <addr> <len>
            session.SendResponse(std::string("Error: 'basic extract <addr> <len>' is not yet implemented.") + NEWLINE);
        }
        else if (args.size() == 3 && args[1] == "file")
        {
            // basic extract file <file>
            session.SendResponse(std::string("Error: 'basic extract file' is not yet implemented.") + NEWLINE);
        }
        else
        {
            session.SendResponse(std::string("Error: Invalid syntax. Use 'basic' to see available commands.") +
                                 NEWLINE);
        }
    }
    else if (subcommand == "save")
    {
        session.SendResponse(std::string("Error: 'basic save' is not yet implemented.") + NEWLINE);
    }
    else if (subcommand == "load")
    {
        session.SendResponse(std::string("Error: 'basic load' is not yet implemented.") + NEWLINE);
    }
    else
    {
        session.SendResponse(std::string("Error: Unknown BASIC subcommand: ") + subcommand + NEWLINE +
                             "Use 'basic' to see available commands." + NEWLINE);
    }
}

