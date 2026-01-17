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
        ss << "  basic inject <program>    - Inject BASIC program (multiline with \\n)" << NEWLINE;
        ss << "  basic load <path>         - Load BASIC from file" << NEWLINE;
        ss << "  basic appendline <line>   - Append single line to program" << NEWLINE;
        ss << "  basic run [<command>]     - Execute RUN or inject command to E_LINE" << NEWLINE;
        ss << "  basic list                - Show current program" << NEWLINE;
        ss << "  basic clear               - Clear program (NEW)" << NEWLINE;
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
        // basic inject <program> OR basic inject --file <path>
        if (args.size() < 2)
        {
            session.SendResponse(std::string("Error: basic inject requires a program or --file argument.") + NEWLINE);
            return;
        }

        // Join remaining args as program
        std::string program;
        for (size_t i = 1; i < args.size(); i++)
        {
            if (i > 1) program += " ";
            program += args[i];
        }

        // Debug: show what we received
        std::cout << "[DEBUG] Program before: [" << program << "]" << std::endl;
        
        // Replace \n with actual newlines
        size_t pos = 0;
        while ((pos = program.find("\\n", pos)) != std::string::npos)
        {
            std::cout << "[DEBUG] Found backslash-n at pos " << pos << std::endl;
            program.replace(pos, 2, "\n");
            pos++;
        }
        
        std::cout << "[DEBUG] Program after: [" << program << "]" << std::endl;

        BasicEncoder encoder;
        Memory* memory = emulator->GetMemory();

        if (!memory)
        {
            session.SendResponse(std::string("Error: Unable to access emulator memory.") + NEWLINE);
            return;
        }

        bool success = encoder.loadProgram(memory, program);
        if (success)
        {
            session.SendResponse(std::string("BASIC program injected successfully.") + NEWLINE);
        }
        else
        {
            session.SendResponse(std::string("Error: Failed to inject BASIC program.") + NEWLINE);
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

        // Inject using BasicEncoder (handles 128K SLEB vs 48K E_LINE internally)
        std::string result = BasicEncoder::injectCommand(memory, command);
        ss << result << NEWLINE;

        // Trigger execution by simulating ENTER key
        BasicEncoder::injectEnter(memory);

        ss << "Executed." << NEWLINE;
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

