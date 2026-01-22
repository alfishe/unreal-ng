// CLI Analyzer Commands
// Extracted from cli-processor.cpp - 2026-01-08

#include "cli-processor.h"

#include <common/modulelogger.h>
#include <common/stringhelper.h>
#include <common/image/imagehelper.h>
#include <debugger/debugmanager.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/labels/labelmanager.h>
#include <debugger/analyzers/basic-lang/basicextractor.h>
#include <debugger/analyzers/basic-lang/basicencoder.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/emulatorcontext.h>
#include <emulator/spectrumconstants.h>

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
        ss << "  basic inject <command>    - Inject command into edit buffer (no execute)" << NEWLINE;
        ss << "  basic run [<command>]     - Inject + execute command (default: RUN)" << NEWLINE;
        ss << "  basic program <code>      - Load multi-line BASIC program (with \\n)" << NEWLINE;
        ss << "  basic list                - Show current program" << NEWLINE;
        ss << "  basic clear               - Clear program (NEW)" << NEWLINE;
        ss << "  basic state               - Show BASIC/TR-DOS state" << NEWLINE;
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
        else
        {
            session.SendResponse(std::string("Error: 'basic extract' with arguments not yet implemented.") + NEWLINE);
        }
    }
    else if (subcommand == "inject")
    {
        // basic inject <command> - Inject command into edit buffer (NO execution)
        if (args.size() < 2)
        {
            session.SendResponse(std::string("Error: basic inject requires a command argument.") + NEWLINE);
            return;
        }

        Memory* memory = emulator->GetMemory();
        if (!memory)
        {
            session.SendResponse(std::string("Error: Unable to access emulator memory.") + NEWLINE);
            return;
        }

        // Build command string from args
        std::string command;
        for (size_t i = 1; i < args.size(); i++)
        {
            if (i > 1) command += " ";
            command += args[i];
        }

        // Use injectCommand (writes to edit buffer, no execution)
        auto result = BasicEncoder::injectCommand(memory, command);
        
        std::stringstream ss;
        ss << result.message << NEWLINE;
        if (result.success)
        {
            ss << "Press ENTER to execute." << NEWLINE;
        }
        session.SendResponse(ss.str());
    }
    else if (subcommand == "program")
    {
        // basic program <code> - Load multi-line BASIC program into PROG area
        if (args.size() < 2)
        {
            session.SendResponse(std::string("Error: basic program requires BASIC code with line numbers.") + NEWLINE);
            return;
        }

        Memory* memory = emulator->GetMemory();
        if (!memory)
        {
            session.SendResponse(std::string("Error: Unable to access emulator memory.") + NEWLINE);
            return;
        }

        // Check state before injection
        auto state = BasicEncoder::detectState(memory);
        if (state == BasicEncoder::BasicState::TRDOS_Active || 
            state == BasicEncoder::BasicState::TRDOS_SOS_Call)
        {
            session.SendResponse(std::string("Error: TR-DOS is active. Please exit to BASIC first.") + NEWLINE);
            return;
        }
        if (state == BasicEncoder::BasicState::Menu128K)
        {
            session.SendResponse(std::string("Error: On 128K menu. Please enter BASIC first.") + NEWLINE);
            return;
        }

        // Join remaining args as program
        std::string program;
        for (size_t i = 1; i < args.size(); i++)
        {
            if (i > 1) program += " ";
            program += args[i];
        }

        // Replace \\n with actual newlines
        size_t pos = 0;
        while ((pos = program.find("\\n", pos)) != std::string::npos)
        {
            program.replace(pos, 2, "\n");
            pos++;
        }

        BasicEncoder encoder;
        bool success = encoder.loadProgram(memory, program);
        if (success)
        {
            session.SendResponse(std::string("BASIC program loaded successfully.") + NEWLINE);
        }
        else
        {
            session.SendResponse(std::string("Error: Failed to load BASIC program.") + NEWLINE);
        }
    }
    else if (subcommand == "appendline")
    {
        if (args.size() < 2)
        {
            session.SendResponse(std::string("Error: basic appendline requires a line argument.") + NEWLINE);
            return;
        }

        // Join remaining args as the line
        std::string line = args[1];

        // TODO: Implement append logic (need to extend BasicEncoder)
        session.SendResponse(std::string("Error: basic appendline not yet implemented.") + NEWLINE);
    }
    else if (subcommand == "run")
    {
        EmulatorContext* context = emulator->GetContext();
        if (!context || !context->pKeyboard)
        {
            session.SendResponse(std::string("Error: Keyboard not available.") + NEWLINE);
            return;
        }

        Keyboard* keyboard = context->pKeyboard;
        Memory* memory = emulator->GetMemory();
        std::stringstream ss;

        // Check if we're on 128K menu - if so, navigate to 128K BASIC first
        auto state = BasicEncoder::detectState(memory);
        if (state == BasicEncoder::BasicState::Menu128K)
        {
            ss << "Detected 128K menu, navigating to BASIC..." << NEWLINE;
            BasicEncoder::navigateToBasic128K(memory);
            BasicEncoder::injectEnter(memory);
            ss << "Please wait for menu transition, then retry command." << NEWLINE;
            session.SendResponse(ss.str());
            return;
        }

        // Build command string from args
        std::string command = "RUN";  // Default
        if (args.size() > 1)
        {
            command.clear();
            for (size_t i = 1; i < args.size(); i++)
            {
                if (i > 1) command += " ";
                command += args[i];
            }
            // Replace \\n with actual newlines
            size_t pos = 0;
            while ((pos = command.find("\\n", pos)) != std::string::npos)
            {
                command.replace(pos, 2, "\n");
                pos++;
            }
        }

        // Use new runCommand API (injects + executes via ENTER)
        auto result = BasicEncoder::runCommand(memory, command);
        ss << result.message << NEWLINE;

        session.SendResponse(ss.str());
    }
    else if (subcommand == "list")
    {
        // Same as extract - show current program
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
            session.SendResponse(std::string("No BASIC program in memory.") + NEWLINE);
            return;
        }

        std::stringstream ss;
        ss << basicListing;
        session.SendResponse(ss.str());
    }
    else if (subcommand == "clear")
    {
        // Clear program by injecting empty program (just end marker)
        BasicEncoder encoder;
        Memory* memory = emulator->GetMemory();

        if (!memory)
        {
            session.SendResponse(std::string("Error: Unable to access emulator memory.") + NEWLINE);
            return;
        }

        // Inject empty program (tokenize returns just 0x00 0x00 for empty input)
        bool success = encoder.loadProgram(memory, "");
        if (success)
        {
            session.SendResponse(std::string("BASIC program cleared.") + NEWLINE);
        }
        else
        {
            session.SendResponse(std::string("Error: Failed to clear program.") + NEWLINE);
        }
    }
    else if (subcommand == "load")
    {
        if (args.size() < 2)
        {
            session.SendResponse(std::string("Error: basic load requires a file path.") + NEWLINE);
            return;
        }
        
        // TODO: Implement file loading
        session.SendResponse(std::string("Error: 'basic load' not yet implemented.") + NEWLINE);
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

