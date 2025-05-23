#include "cli-processor.h"

#include <3rdparty/message-center/eventqueue.h>
#include <3rdparty/message-center/messagecenter.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/debugmanager.h>
#include <debugger/disassembler/z80disasm.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "platform_sockets.h"

// ClientSession implementation
void ClientSession::SendResponse(const std::string& message) const
{
    if (_clientSocket != INVALID_SOCKET)
    {
        send(_clientSocket, message.c_str(), message.length(), 0);
    }
}

// CLIProcessor implementation
CLIProcessor::CLIProcessor() : _emulator(nullptr), _isFirstCommand(true)
{
    // Initialize command handlers map
    _commandHandlers = {{"help", &CLIProcessor::HandleHelp},
                        {"?", &CLIProcessor::HandleHelp},
                        {"status", &CLIProcessor::HandleStatus},
                        {"list", &CLIProcessor::HandleList},
                        {"select", &CLIProcessor::HandleSelect},
                        {"reset", &CLIProcessor::HandleReset},
                        {"pause", &CLIProcessor::HandlePause},
                        {"resume", &CLIProcessor::HandleResume},
                        {"step", &CLIProcessor::HandleStep},
                        {"memory", &CLIProcessor::HandleMemory},
                        {"registers", &CLIProcessor::HandleRegisters},

                        // Breakpoint commands
                        {"bp", &CLIProcessor::HandleBreakpoint},          // Set execution breakpoint
                        {"break", &CLIProcessor::HandleBreakpoint},       // Alias for bp
                        {"breakpoint", &CLIProcessor::HandleBreakpoint},  // Alias for bp
                        {"bplist", &CLIProcessor::HandleBPList},          // List all breakpoints
                        {"wp", &CLIProcessor::HandleWatchpoint},          // Set memory read/write watchpoint
                        {"bport", &CLIProcessor::HandlePortBreakpoint},   // Set port breakpoint
                        {"bpclear", &CLIProcessor::HandleBPClear},        // Clear breakpoints
                        {"bpgroup", &CLIProcessor::HandleBPGroup},        // Manage breakpoint groups
                        {"bpon", &CLIProcessor::HandleBPActivate},        // Activate breakpoints
                        {"bpoff", &CLIProcessor::HandleBPDeactivate},     // Deactivate breakpoints

                        {"open", &CLIProcessor::HandleOpen},
                        {"exit", &CLIProcessor::HandleExit},
                        {"quit", &CLIProcessor::HandleExit},
                        {"dummy", &CLIProcessor::HandleDummy}};
}

void CLIProcessor::ProcessCommand(ClientSession& session, const std::string& command)
{
    // Special handling for the first command
    if (_isFirstCommand)
    {
        // Force a direct response to ensure the client connection is working
        session.SendResponse(std::string("Processing first command...") + NEWLINE);

        // Force a refresh of the emulator manager
        auto* emulatorManager = EmulatorManager::GetInstance();
        if (emulatorManager)
        {
            auto mostRecent = emulatorManager->GetMostRecentEmulator();
            auto emulatorIds = emulatorManager->GetEmulatorIds();
        }

        // Mark that we've handled the first command
        _isFirstCommand = false;
    }

    // Check for auto-selection of emulators if none is currently selected
    // This handles the case where emulators appear asynchronously after connection
    if (session.GetSelectedEmulatorId().empty() && !_emulator)
    {
        auto* emulatorManager = EmulatorManager::GetInstance();
        if (emulatorManager)
        {
            // Force a refresh to get the latest emulator instances
            auto mostRecent = emulatorManager->GetMostRecentEmulator();
            auto emulatorIds = emulatorManager->GetEmulatorIds();

            // Auto-select if any emulators are available
            if (!emulatorIds.empty())
            {
                // Use the most recent emulator if available, otherwise use the first one
                std::string selectedId;
                if (mostRecent)
                {
                    selectedId = mostRecent->GetId();
                }
                else
                {
                    selectedId = emulatorIds[0];
                }

                // Update our local reference to the emulator
                _emulator = emulatorManager->GetEmulator(selectedId);
            }
        }
    }

    if (command.empty())
    {
        return;
    }

    // Split the command and arguments
    std::string cmd;
    std::vector<std::string> args;

    // Find the first space to separate command from arguments
    size_t spacePos = command.find_first_of(' ');
    if (spacePos != std::string::npos)
    {
        // We have a command with arguments
        cmd = command.substr(0, spacePos);

        // Parse the rest as arguments
        std::string argsStr = command.substr(spacePos + 1);
        std::istringstream iss(argsStr);
        std::string arg;

        // Handle quoted arguments properly
        while (iss >> std::quoted(arg))
        {
            args.push_back(arg);
        }
    }
    else
    {
        // Just a command with no arguments
        cmd = command;
    }

    if (cmd.empty())
    {
        return;
    }

    // Find and execute the command handler
    auto it = _commandHandlers.find(cmd);
    if (it != _commandHandlers.end())
    {
        try
        {
            // Call the member function using 'this' pointer to provide context to the handler
            (this->*(it->second))(session, args);
        }
        catch (const std::exception& e)
        {
            session.SendResponse("Error processing command: " + std::string(e.what()));
        }
    }
    else
    {
        std::string error = "Unknown command: " + cmd + NEWLINE + "Type 'help' for available commands.";
        session.SendResponse(error);
    }
}

std::shared_ptr<Emulator> CLIProcessor::GetSelectedEmulator(const ClientSession& session)
{
    // First check if we have a cached emulator reference
    if (_emulator)
    {
        return _emulator;
    }

    // Get the selected emulator ID from the session
    std::string selectedId = session.GetSelectedEmulatorId();
    if (selectedId.empty())
    {
        // Try to auto-select an emulator if none is selected
        auto* emulatorManager = EmulatorManager::GetInstance();
        if (emulatorManager)
        {
            auto mostRecent = emulatorManager->GetMostRecentEmulator();
            if (mostRecent)
            {
                _emulator = mostRecent;
                return _emulator;
            }
        }
        return nullptr;
    }

    // Get the emulator from the manager
    auto* emulatorManager = EmulatorManager::GetInstance();
    if (emulatorManager)
    {
        _emulator = emulatorManager->GetEmulator(selectedId);
    }

    return _emulator;
}

bool CLIProcessor::ParseAddress(const std::string& addressStr, uint16_t& result, uint16_t maxValue) const
{
    if (addressStr.empty())
        return false;

    try
    {
        // Default base is 10 (decimal)
        int base = 10;
        std::string processedStr = addressStr;

        // Check for hex prefixes
        if (addressStr.size() >= 2)
        {
            if (addressStr.substr(0, 2) == "0x" || addressStr.substr(0, 2) == "0X")
            {
                base = 16;
                processedStr = addressStr.substr(2);
            }
            else if (addressStr[0] == '$')
            {
                base = 16;
                processedStr = addressStr.substr(1);
            }
            else if (addressStr[0] == '#')
            {
                base = 16;
                processedStr = addressStr.substr(1);
            }
        }

        // Parse the number using the determined base
        unsigned long value = std::stoul(processedStr, nullptr, base);

        // Check if the value is within the valid range
        if (value > maxValue)
        {
            return false;
        }

        result = static_cast<uint16_t>(value);
        return true;
    }
    catch (const std::exception&)
    {
        // Any parsing error (invalid format, etc.)
        return false;
    }
}

void CLIProcessor::onBreakpointsChanged()
{
    // Notify UI components that breakpoints have changed
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_BREAKPOINT_CHANGED, nullptr, true);
}

// Command handler implementations
void CLIProcessor::HandleHelp(const ClientSession& session, const std::vector<std::string>& args)
{
    std::ostringstream oss;
    oss << "Available commands:" << NEWLINE;
    oss << "  help, ?       - Show this help message" << NEWLINE;
    oss << "  status        - Show emulator status" << NEWLINE;
    oss << "  list          - List available emulators" << NEWLINE;
    oss << "  select <id>   - Select an emulator" << NEWLINE;
    oss << "  reset         - Reset the emulator" << NEWLINE;
    oss << "  pause         - Pause emulation" << NEWLINE;
    oss << "  resume        - Resume emulation" << NEWLINE;
    oss << "  step [count]  - Execute one or more CPU instructions" << NEWLINE;
    oss << "  memory <addr> - View memory at address" << NEWLINE;
    oss << "  registers     - Show CPU registers" << NEWLINE;
    oss << NEWLINE << "Breakpoint commands:" << NEWLINE;
    oss << "  bp <addr>     - Set execution breakpoint at address" << NEWLINE;
    oss << "  wp <addr> <type> - Set memory watchpoint (r/w/rw)" << NEWLINE;
    oss << "  bport <port> <type> - Set port breakpoint (i/o/io)" << NEWLINE;
    oss << "  bplist        - List all breakpoints" << NEWLINE;
    oss << "  bpclear       - Clear breakpoints" << NEWLINE;
    oss << "  bpgroup       - Manage breakpoint groups" << NEWLINE;
    oss << "  bpon          - Activate breakpoints" << NEWLINE;
    oss << "  bpoff         - Deactivate breakpoints" << NEWLINE;
    oss << NEWLINE << "Other commands:" << NEWLINE;
    oss << "  open [file]   - Open a file or show file dialog" << NEWLINE;
    oss << "  exit, quit    - Exit the CLI" << NEWLINE;
    oss << NEWLINE << "Type any command followed by -h or --help for more information.";

    session.SendResponse(oss.str());
}

void CLIProcessor::HandleStatus(const ClientSession& session, const std::vector<std::string>& args)
{
    std::string status;

    // Get all emulator instances from EmulatorManager
    auto* emulatorManager = EmulatorManager::GetInstance();
    auto emulatorIds = emulatorManager->GetEmulatorIds();

    if (emulatorIds.empty())
    {
        status = std::string("No emulator instances found") + NEWLINE;
    }
    else
    {
        status = std::string("Emulator Instances:") + NEWLINE;
        status += std::string("==================") + NEWLINE;

        for (const auto& id : emulatorIds)
        {
            auto emulator = emulatorManager->GetEmulator(id);
            if (emulator)
            {
                status += "ID: " + id + NEWLINE;
                status += "Status: " + std::string(emulator->IsRunning() ? "Running" : "Stopped") + NEWLINE;
                status += "Debug: " + std::string(emulator->IsDebug() ? "On" : "Off") + NEWLINE;

                // Indicate if this is the currently selected emulator
                // Check both the session's selected ID and our local _emulator reference
                bool isSelected = (session.GetSelectedEmulatorId() == id) ||
                                  (_emulator && _emulator->GetId() == id && session.GetSelectedEmulatorId().empty());
                if (isSelected)
                {
                    status += std::string("SELECTED") + NEWLINE;
                }

                status += std::string("------------------");
            }
        }

        // Add current active emulator status if available
        if (_emulator)
        {
            status += std::string(NEWLINE) + "Current CLI Emulator: " + _emulator->GetId() + NEWLINE;
            status += "Status: " + std::string(_emulator->IsRunning() ? "Running" : "Stopped");
        }
    }

    session.SendResponse(status);
}

void CLIProcessor::HandleList(const ClientSession& session, const std::vector<std::string>& args)
{
    auto* emulatorManager = EmulatorManager::GetInstance();
    if (!emulatorManager)
    {
        session.SendResponse(std::string("Error: Unable to access emulator manager."));
        return;
    }

    // Force a refresh of emulator instances by calling a method that updates the internal state
    auto mostRecent = emulatorManager->GetMostRecentEmulator();

    // Now get the updated list of emulator IDs
    auto emulatorIds = emulatorManager->GetEmulatorIds();

    if (emulatorIds.empty())
    {
        session.SendResponse(std::string("No emulator instances found."));
        return;
    }

    std::string response = std::string("Available emulator instances:") + NEWLINE;
    response += std::string("============================") + NEWLINE;

    // Display emulators with index, ID, and status
    for (size_t i = 0; i < emulatorIds.size(); ++i)
    {
        const auto& id = emulatorIds[i];
        auto emulator = emulatorManager->GetEmulator(id);

        if (emulator)
        {
            // Mark the currently selected emulator
            // Check both the session's selected ID and our local _emulator reference
            bool isSelected = (session.GetSelectedEmulatorId() == id) ||
                              (_emulator && _emulator->GetId() == id && session.GetSelectedEmulatorId().empty());
            std::string selectedMarker = isSelected ? "* " : "  ";

            response += selectedMarker + "[" + std::to_string(i + 1) + "] ";
            response += "ID: " + id;

            // Add symbolic name if available
            // Note: We'd need to add this to the Emulator class if it's not already there
            // response += " (" + emulator->GetSymbolicName() + ")";

            response +=
                std::string(NEWLINE) + "     Status: " + std::string(emulator->IsRunning() ? "Running" : "Stopped");
            response += std::string(NEWLINE) + "     Debug: " + std::string(emulator->IsDebug() ? "On" : "Off");
            response += NEWLINE;
        }
    }

    response += std::string(NEWLINE) + "Use 'select <index>' or 'select <id>' to choose an emulator.";

    session.SendResponse(response);
}

void CLIProcessor::HandleSelect(const ClientSession& session, const std::vector<std::string>& args)
{
    if (args.empty())
    {
        session.SendResponse(std::string("Usage: select <index|id|name>") + NEWLINE);
        session.SendResponse("Use 'list' to see available emulators.");
        return;
    }

    const std::string& selector = args[0];
    auto* emulatorManager = EmulatorManager::GetInstance();

    // Force a refresh of emulator instances by calling a method that updates the internal state
    emulatorManager->GetMostRecentEmulator();

    // Now get the updated list of emulator IDs
    auto emulatorIds = emulatorManager->GetEmulatorIds();

    if (emulatorIds.empty())
    {
        session.SendResponse(std::string("No emulator instances available."));
        return;
    }

    std::string selectedId;

    // Try to interpret as an index first
    try
    {
        int index = std::stoi(selector);
        if (index > 0 && index <= static_cast<int>(emulatorIds.size()))
        {
            // Convert to 0-based index
            size_t arrayIndex = static_cast<size_t>(index - 1);
            if (arrayIndex < emulatorIds.size())
            {
                selectedId = emulatorIds[arrayIndex];
            }
            else
            {
                session.SendResponse(std::string("Error: Index out of bounds"));
                return;
            }
        }
        else
        {
            session.SendResponse(std::string("Invalid emulator index. Use 'list' to see available emulators."));
            return;
        }
    }
    catch (const std::exception&)
    {
        // Not a valid index, try as UUID or name

        // First check if it's a direct UUID match
        if (emulatorManager->HasEmulator(selector))
        {
            selectedId = selector;
        }
        else
        {
            // Try to find by partial ID or name match
            bool found = false;
            for (const auto& id : emulatorIds)
            {
                // Check if the selector is a substring of the ID
                if (id.find(selector) != std::string::npos)
                {
                    selectedId = id;
                    found = true;
                    break;
                }

                // TODO: If emulators have symbolic names, we could check those too
                // auto emulator = emulatorManager->GetEmulator(id);
                // if (emulator && emulator->GetSymbolicName().find(selector) != std::string::npos)
                // {
                //     selectedId = id;
                //     found = true;
                //     break;
                // }
            }

            if (!found)
            {
                session.SendResponse("No emulator found matching: " + selector + NEWLINE);
                session.SendResponse("Use 'list' to see available emulators.");
                return;
            }
        }
    }

    // We have a valid ID at this point
    const_cast<ClientSession&>(session).SetSelectedEmulatorId(selectedId);

    // Also update our local reference to the emulator
    _emulator = emulatorManager->GetEmulator(selectedId);

    std::stringstream ss;
    ss << "Selected emulator: " << selectedId;
    if (_emulator)
    {
        ss << " (" + std::string(_emulator->IsRunning() ? "Running" : "Stopped") + ")";
    }

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleExit(const ClientSession& session, const std::vector<std::string>& args)
{
    std::stringstream ss;
    ss << "Goodbye!";
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleDummy(const ClientSession& session, const std::vector<std::string>& args)
{
    // This is a silent command used for initialization
    // It doesn't send any response to the client
}

void CLIProcessor::InitializeProcessor()
{
    // Force initialization of the EmulatorManager
    auto* emulatorManager = EmulatorManager::GetInstance();
    if (emulatorManager)
    {
        // Force a refresh of emulator instances
        auto mostRecent = emulatorManager->GetMostRecentEmulator();
        auto emulatorIds = emulatorManager->GetEmulatorIds();

        // Auto-select the first emulator if any exist
        if (!emulatorIds.empty())
        {
            // Use the most recent emulator if available, otherwise use the first one
            std::string selectedId;
            if (mostRecent)
            {
                selectedId = mostRecent->GetId();
            }
            else
            {
                selectedId = emulatorIds[0];
            }

            // Update our local reference to the emulator
            _emulator = emulatorManager->GetEmulator(selectedId);
        }

        // Reset the first command flag so that the first real command works properly
        _isFirstCommand = false;
    }
    else
    {
        std::cerr << "Failed to initialize EmulatorManager" << std::endl;
    }
}

void CLIProcessor::HandleReset(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    emulator->Reset();
    session.SendResponse("Emulator reset\n");
}

void CLIProcessor::HandlePause(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    // Check if the emulator is already paused
    if (emulator->IsPaused())
    {
        session.SendResponse("Emulator is already paused.");
        return;
    }

    // Pause the emulator - this will trigger MessageCenter notifications
    // that the GUI will respond to (enabling/disabling buttons)
    emulator->Pause();

    // Confirm to the user
    session.SendResponse("Emulation paused. Use 'resume' to continue execution.");
}

void CLIProcessor::HandleResume(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    // Check if the emulator is already running
    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator is already running.");
        return;
    }

    // Resume the emulator - this will trigger MessageCenter notifications
    // that the GUI will respond to (enabling/disabling buttons)
    emulator->Resume();

    // Confirm to the user
    session.SendResponse("Emulation resumed. Use 'pause' to suspend execution.");
}

void CLIProcessor::HandleStep(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    // Check if emulator is paused
    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.");
        return;
    }

    // Parse step count argument
    int stepCount = 1;
    if (!args.empty())
    {
        try
        {
            stepCount = std::stoi(args[0]);
            if (stepCount < 1)
                stepCount = 1;
        }
        catch (...)
        {
            // Use default step count of 1
        }
    }

    // Get memory and disassembler for instruction info
    Memory* memory = emulator->GetMemory();
    std::unique_ptr<Z80Disassembler>& disassembler = emulator->GetDebugManager()->GetDisassembler();

    if (!memory || !disassembler)
    {
        session.SendResponse("Error: Unable to access memory or disassembler.");
        return;
    }

    // Get the current PC and disassemble the instruction that's about to be executed
    Z80State* z80State = emulator->GetZ80State();
    if (!z80State)
    {
        session.SendResponse("Error: Unable to access Z80 state.\n");
        return;
    }

    // Store the current PC to show what's about to be executed
    uint16_t initialPC = z80State->pc;

    // Get physical address for the PC
    uint8_t* pcPhysicalAddress = memory->MapZ80AddressToPhysicalAddress(initialPC);
    if (!pcPhysicalAddress)
    {
        session.SendResponse("Error: Unable to get physical address for PC.");
        return;
    }

    // Disassemble the instruction that's about to be executed
    uint8_t commandLen = 0;
    DecodedInstruction decodedBefore;
    std::string instructionBefore = disassembler->disassembleSingleCommandWithRuntime(pcPhysicalAddress, 6, &commandLen,
                                                                                      z80State, memory, &decodedBefore);

    // Execute the requested number of CPU cycles
    for (int i = 0; i < stepCount; ++i)
    {
        emulator->RunSingleCPUCycle(false);  // false = don't skip breakpoints
    }

    // Get the Z80 state after execution
    z80State = emulator->GetZ80State();  // Refresh state after execution
    if (!z80State)
    {
        session.SendResponse("Error: Unable to access Z80 state after execution.");
        return;
    }

    // Get the new PC and disassemble the next instruction to be executed
    uint16_t newPC = z80State->pc;
    pcPhysicalAddress = memory->MapZ80AddressToPhysicalAddress(newPC);
    if (!pcPhysicalAddress)
    {
        session.SendResponse("Error: Unable to get physical address for new PC.");
        return;
    }

    // Disassemble the next instruction to be executed
    commandLen = 0;
    DecodedInstruction decodedAfter;
    std::string instructionAfter = disassembler->disassembleSingleCommandWithRuntime(pcPhysicalAddress, 6, &commandLen,
                                                                                     z80State, memory, &decodedAfter);

    // Format response with CPU state information
    std::stringstream ss;
    ss << "Executed " << stepCount << " instruction" << (stepCount != 1 ? "s" : "") << NEWLINE;

    // Show executed instruction
    ss << std::hex << std::uppercase << std::setfill('0');
    ss << "Executed: [$" << std::setw(4) << initialPC << "] ";

    // Add hex dump of the executed instruction
    if (decodedBefore.instructionBytes.size() > 0)
    {
        for (uint8_t byte : decodedBefore.instructionBytes)
        {
            ss << std::setw(2) << static_cast<int>(byte) << " ";
        }
        // Add padding for alignment if needed
        for (size_t i = decodedBefore.instructionBytes.size(); i < 4; i++)
        {
            ss << "   ";
        }
    }

    ss << instructionBefore << NEWLINE;

    // Show next instruction
    ss << "Next:     [$" << std::setw(4) << newPC << "] ";

    // Add hex dump of the next instruction
    if (decodedAfter.instructionBytes.size() > 0)
    {
        for (uint8_t byte : decodedAfter.instructionBytes)
        {
            ss << std::setw(2) << static_cast<int>(byte) << " ";
        }
        // Add padding for alignment if needed
        for (size_t i = decodedAfter.instructionBytes.size(); i < 4; i++)
        {
            ss << "   ";
        }
    }

    ss << instructionAfter << "\n\n";

    // Format current PC and registers
    ss << "PC: $" << std::setw(4) << z80State->pc << "  ";

    // Show main registers (compact format)
    ss << "AF: $" << std::setw(4) << z80State->af << "  ";
    ss << "BC: $" << std::setw(4) << z80State->bc << "  ";
    ss << "DE: $" << std::setw(4) << z80State->de << "  ";
    ss << "HL: $" << std::setw(4) << z80State->hl << NEWLINE;

    // Show flags
    ss << "Flags: ";
    ss << (z80State->f & 0x80 ? "S" : "-");
    ss << (z80State->f & 0x40 ? "Z" : "-");
    ss << (z80State->f & 0x20 ? "5" : "-");
    ss << (z80State->f & 0x10 ? "H" : "-");
    ss << (z80State->f & 0x08 ? "3" : "-");
    ss << (z80State->f & 0x04 ? "P" : "-");
    ss << (z80State->f & 0x02 ? "N" : "-");
    ss << (z80State->f & 0x01 ? "C" : "-");
    ss << NEWLINE;

    // Add note about viewing full register state
    ss << "\nUse 'registers' command to view full CPU state\n";

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleMemory(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    if (args.empty())
    {
        session.SendResponse(std::string("Usage: memory <address>") + NEWLINE);
        session.SendResponse(std::string("Displays memory contents at the specified address.") + NEWLINE);
        session.SendResponse(std::string("Examples:") + NEWLINE);
        session.SendResponse(std::string("  memory 0x1000    - View memory at address 0x1000 (hex)") + NEWLINE);
        session.SendResponse(std::string("  memory $8000     - View memory at address $8000 (hex)") + NEWLINE);
        session.SendResponse(std::string("  memory #C000     - View memory at address #C000 (hex)") + NEWLINE);
        session.SendResponse(std::string("  memory 32768     - View memory at address 32768 (decimal)") + NEWLINE);
        return;
    }

    uint16_t address;
    if (!ParseAddress(args[0], address))
    {
        session.SendResponse("Invalid address format or out of range (must be 0-65535)");
        return;
    }

    Memory* memory = emulator->GetMemory();
    if (!memory)
    {
        session.SendResponse("Memory not available\n");
        return;
    }

    std::ostringstream oss;
    oss << "Memory at 0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << address << ":" << NEWLINE;

    // Display 8 rows of 16 bytes each
    for (int row = 0; row < 8; ++row)
    {
        uint16_t rowAddr = address + (row * 16);
        oss << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << rowAddr << ": ";

        // Hex values
        for (int col = 0; col < 16; ++col)
        {
            uint16_t byteAddr = rowAddr + col;
            uint8_t value = memory->DirectReadFromZ80Memory(byteAddr);
            oss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(value) << " ";
        }

        oss << " | ";

        // ASCII representation
        for (int col = 0; col < 16; ++col)
        {
            uint16_t byteAddr = rowAddr + col;
            uint8_t value = memory->DirectReadFromZ80Memory(byteAddr);
            oss << (value >= 32 && value <= 126 ? static_cast<char>(value) : '.');
        }
    }

    session.SendResponse(oss.str());
}

void CLIProcessor::HandleRegisters(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    // Get the Z80 state from the emulator
    Z80State* z80State = emulator->GetZ80State();
    if (!z80State)
    {
        session.SendResponse("Error: Unable to access Z80 state.");
        return;
    }

    // Format the register values
    std::stringstream ss;
    ss << "Z80 Registers:" << NEWLINE;
    ss << "=============" << NEWLINE << NEWLINE;

    // Main register pairs and alternate registers side by side
    ss << "Main registers:                     Alternate registers:" << NEWLINE;
    ss << std::hex << std::uppercase << std::setfill('0');

    // Access alternate registers from the shadow registers in Z80State
    ss << "  AF: " << std::setw(4) << z80State->af << "  (A: " << std::setw(2) << static_cast<int>(z80State->a)
       << ", F: " << std::setw(2) << static_cast<int>(z80State->f) << ")";
    ss << "           AF': " << std::setw(4) << z80State->alt.af << "  (A': " << std::setw(2)
       << static_cast<int>(z80State->alt.a) << ", F': " << std::setw(2) << static_cast<int>(z80State->alt.f) << ")" << NEWLINE;

    ss << "  BC: " << std::setw(4) << z80State->bc << "  (B: " << std::setw(2) << static_cast<int>(z80State->b)
       << ", C: " << std::setw(2) << static_cast<int>(z80State->c) << ")";
    ss << "           BC': " << std::setw(4) << z80State->alt.bc << "  (B': " << std::setw(2)
       << static_cast<int>(z80State->alt.b) << ", C': " << std::setw(2) << static_cast<int>(z80State->alt.c) << ")" << NEWLINE;

    ss << "  DE: " << std::setw(4) << z80State->de << "  (D: " << std::setw(2) << static_cast<int>(z80State->d)
       << ", E: " << std::setw(2) << static_cast<int>(z80State->e) << ")";
    ss << "           DE': " << std::setw(4) << z80State->alt.de << "  (D': " << std::setw(2)
       << static_cast<int>(z80State->alt.d) << ", E': " << std::setw(2) << static_cast<int>(z80State->alt.e) << ")" << NEWLINE;

    ss << "  HL: " << std::setw(4) << z80State->hl << "  (H: " << std::setw(2) << static_cast<int>(z80State->h)
       << ", L: " << std::setw(2) << static_cast<int>(z80State->l) << ")";
    ss << "           HL': " << std::setw(4) << z80State->alt.hl << "  (H': " << std::setw(2)
       << static_cast<int>(z80State->alt.h) << ", L': " << std::setw(2) << static_cast<int>(z80State->alt.l) << ")" << NEWLINE;

    ss << NEWLINE;

    // Index and special registers in two columns
    ss << "Index registers:                    Special registers:" << NEWLINE;
    ss << "  IX: " << std::setw(4) << z80State->ix << "  (IXH: " << std::setw(2) << static_cast<int>(z80State->xh)
       << ", IXL: " << std::setw(2) << static_cast<int>(z80State->xl) << ")";
    ss << "       PC: " << std::setw(4) << z80State->pc << NEWLINE;

    ss << "  IY: " << std::setw(4) << z80State->iy << "  (IYH: " << std::setw(2) << static_cast<int>(z80State->yh)
       << ", IYL: " << std::setw(2) << static_cast<int>(z80State->yl) << ")";
    ss << "       SP: " << std::setw(4) << z80State->sp << NEWLINE;

    // Empty line for IR and first line of flags
    ss << "                                     IR: " << std::setw(4) << z80State->ir_ << "  (I: " << std::setw(2)
       << static_cast<int>(z80State->i) << ", R: " << std::setw(2) << static_cast<int>(z80State->r_low) << ")" << NEWLINE;
    ss << NEWLINE;

    // Flags and interrupt state in two columns
    ss << "Flags (" << std::setw(2) << static_cast<int>(z80State->f) << "):                         Interrupt state:\n";
    ss << "  S: " << ((z80State->f & 0x80) ? "1" : "0") << " (Sign)";
    ss << "                        IFF1: " << (z80State->iff1 ? "Enabled" : "Disabled") << NEWLINE;

    ss << "  Z: " << ((z80State->f & 0x40) ? "1" : "0") << " (Zero)";
    ss << "                        IFF2: " << (z80State->iff2 ? "Enabled" : "Disabled") << NEWLINE;

    ss << "  5: " << ((z80State->f & 0x20) ? "1" : "0") << " (Unused bit 5)";
    ss << "                HALT: " << (z80State->halted ? "Yes" : "No") << NEWLINE;

    ss << "  H: " << ((z80State->f & 0x10) ? "1" : "0") << " (Half-carry)" << NEWLINE;
    ss << "  3: " << ((z80State->f & 0x08) ? "1" : "0") << " (Unused bit 3)" << NEWLINE;
    ss << "  P/V: " << ((z80State->f & 0x04) ? "1" : "0") << " (Parity/Overflow)" << NEWLINE;
    ss << "  N: " << ((z80State->f & 0x02) ? "1" : "0") << " (Add/Subtract)" << NEWLINE;
    ss << "  C: " << ((z80State->f & 0x01) ? "1" : "0") << " (Carry)";

    // Send the formatted register dump
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleBreakpoint(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    if (args.empty())
    {
        stringstream ss;
        ss << "Usage: bp <address> [note]" << NEWLINE
           << "Sets an execution breakpoint at the specified address." << NEWLINE
           << "Examples:" << NEWLINE
           << "  bp 0x1234       - Set breakpoint at address 0x1234" << NEWLINE
           << "  bp $1234        - Set breakpoint at address $1234 (hex)" << NEWLINE
           << "  bp #1234        - Set breakpoint at address #1234 (hex)" << NEWLINE
           << "  bp 1234         - Set breakpoint at address 1234 (decimal)" << NEWLINE
           << "  bp 1234 Main loop - Set breakpoint with a note" << NEWLINE
           << "Use 'bplist' to view all breakpoints";
        session.SendResponse(ss.str());

        return;
    }

    uint16_t address;
    if (!ParseAddress(args[0], address))
    {
        session.SendResponse("Invalid address format or out of range (must be 0-65535)");
        return;
    }

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }

    uint16_t bpId = bpManager->AddExecutionBreakpoint(address);

    // Add note if provided
    if (bpId != BRK_INVALID && args.size() > 1)
    {
        // Collect all remaining arguments as the note
        std::string note;
        for (size_t i = 1; i < args.size(); ++i)
        {
            if (i > 1)
                note += " ";
            note += args[i];
        }

        // Set the note for this breakpoint
        auto& breakpoints = bpManager->GetAllBreakpoints();
        if (breakpoints.find(bpId) != breakpoints.end())
        {
            breakpoints.at(bpId)->note = note;
        }
    }

    std::ostringstream oss;
    if (bpId != BRK_INVALID)
    {
        oss << "Breakpoint #" << bpId << " set at 0x" << std::hex << std::setw(4) << std::setfill('0') << address;

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else
    {
        oss << "Failed to set breakpoint at 0x" << std::hex << std::setw(4) << std::setfill('0') << address;
    }

    session.SendResponse(oss.str());
}

void CLIProcessor::HandleBPList(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }

    // Check if a specific group was requested
    if (!args.empty())
    {
        std::string groupName = args[0];
        std::string list = bpManager->GetBreakpointListAsStringByGroup(groupName);
        session.SendResponse(list);
        return;
    }

    // No group specified, list all breakpoints
    std::string list = bpManager->GetBreakpointListAsString();
    session.SendResponse(list);
}

void CLIProcessor::HandleWatchpoint(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    if (args.empty() || args.size() < 2)
    {
        std::stringstream ss;
        ss << "Usage: wp <address> <type> [note]" << NEWLINE
           << "Sets a memory watchpoint at the specified address." << NEWLINE
           << "Types:" << NEWLINE
           << "  r    - Watch for memory reads" << NEWLINE
           << "  w    - Watch for memory writes" << NEWLINE
           << "  rw   - Watch for both reads and writes" << NEWLINE
           << "Examples:" << NEWLINE
           << "  wp 0x1234 r     - Watch for reads at address 0x1234" << NEWLINE
           << "  wp $4000 w      - Watch for writes at address $4000 (hex)" << NEWLINE
           << "  wp #8000 rw     - Watch for reads/writes at address #8000 (hex)" << NEWLINE
           << "  wp 49152 rw Stack pointer - Watch for reads/writes with a note";
        session.SendResponse(ss.str());

        return;
    }

    uint16_t address;
    if (!ParseAddress(args[0], address))
    {
        session.SendResponse("Invalid address format or out of range (must be 0-65535)\n");
        return;
    }

    std::string typeStr = args[1];
    uint8_t memoryType = BRK_MEM_NONE;

    // Parse the type string
    if (typeStr.find('r') != std::string::npos)
        memoryType |= BRK_MEM_READ;
    if (typeStr.find('w') != std::string::npos)
        memoryType |= BRK_MEM_WRITE;

    if (memoryType == BRK_MEM_NONE)
    {
        session.SendResponse("Invalid watchpoint type. Use 'r', 'w', or 'rw'.");
        return;
    }

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }

    uint16_t bpId = bpManager->AddCombinedMemoryBreakpoint(address, memoryType);

    // Add note if provided
    if (bpId != BRK_INVALID && args.size() > 2)
    {
        // Collect all remaining arguments as the note
        std::string note;
        for (size_t i = 2; i < args.size(); ++i)
        {
            if (i > 2)
                note += " ";
            note += args[i];
        }

        // Set the note for this breakpoint
        auto& breakpoints = bpManager->GetAllBreakpoints();
        if (breakpoints.find(bpId) != breakpoints.end())
        {
            breakpoints.at(bpId)->note = note;
        }
    }

    std::ostringstream oss;
    if (bpId != BRK_INVALID)
    {
        oss << "Watchpoint #" << bpId << " set at 0x" << std::hex << std::setw(4) << std::setfill('0') << address;
        oss << " (";
        if (memoryType & BRK_MEM_READ)
            oss << "read";
        if ((memoryType & BRK_MEM_READ) && (memoryType & BRK_MEM_WRITE))
            oss << "/";
        if (memoryType & BRK_MEM_WRITE)
            oss << "write";
    }
    else
    {
        oss << "Failed to set watchpoint at 0x" << std::hex << std::setw(4) << std::setfill('0') << address;
    }

    session.SendResponse(oss.str());
}

void CLIProcessor::HandlePortBreakpoint(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    if (args.empty() || args.size() < 2)
    {
        std::stringstream ss;
        ss << "Usage: bport <port> <type> [note]" << NEWLINE
           << "Sets a port breakpoint at the specified port address." << NEWLINE
           << "Types:" << NEWLINE
           << "  i    - Watch for port IN operations" << NEWLINE
           << "  o    - Watch for port OUT operations" << NEWLINE
           << "  io   - Watch for both IN and OUT operations" << NEWLINE
           << "Examples:" << NEWLINE
           << "  bport 0x1234 i     - Watch for IN operations at port 0x1234" << NEWLINE
           << "  bport $FE o        - Watch for OUT operations at port $FE (hex)" << NEWLINE
           << "  bport #A0 io       - Watch for IN/OUT at port #A0 (hex)" << NEWLINE
           << "  bport 254 io Keyboard port - Watch for IN/OUT with a note";
        session.SendResponse(ss.str());

        return;
    }

    uint16_t port;
    if (!ParseAddress(args[0], port, 0xFFFF))
    {
        session.SendResponse("Invalid port format or out of range (must be 0-65535)\n");
        return;
    }

    std::string typeStr = args[1];
    uint8_t ioType = BRK_IO_NONE;

    // Parse the type string
    if (typeStr.find('i') != std::string::npos)
        ioType |= BRK_IO_IN;
    if (typeStr.find('o') != std::string::npos)
        ioType |= BRK_IO_OUT;

    if (ioType == BRK_IO_NONE)
    {
        session.SendResponse("Invalid port breakpoint type. Use 'i', 'o', or 'io'.");
        return;
    }

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }

    uint16_t bpId = bpManager->AddCombinedPortBreakpoint(port, ioType);

    // Add note if provided
    if (bpId != BRK_INVALID && args.size() > 2)
    {
        // Collect all remaining arguments as the note
        std::string note;
        for (size_t i = 2; i < args.size(); ++i)
        {
            if (i > 2)
                note += " ";
            note += args[i];
        }

        // Set the note for this breakpoint
        auto& breakpoints = bpManager->GetAllBreakpoints();
        if (breakpoints.find(bpId) != breakpoints.end())
        {
            breakpoints.at(bpId)->note = note;
        }
    }

    std::ostringstream oss;
    if (bpId != BRK_INVALID)
    {
        oss << "Port breakpoint #" << bpId << " set at port 0x" << std::hex << std::setw(4) << std::setfill('0')
            << port;
        oss << " (";
        if (ioType & BRK_IO_IN)
            oss << "in";
        if ((ioType & BRK_IO_IN) && (ioType & BRK_IO_OUT))
            oss << "/";
        if (ioType & BRK_IO_OUT)
            oss << "out";
        oss << ")";
    }
    else
    {
        oss << "Failed to set port breakpoint at 0x" << std::hex << std::setw(4) << std::setfill('0') << port;
    }

    session.SendResponse(oss.str());
}

void CLIProcessor::HandleBPClear(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }

    if (args.empty())
    {
        std::stringstream ss;
        ss << "Usage: bpclear <option>" << NEWLINE
           << "Options:" << NEWLINE
           << "  all       - Clear all breakpoints" << NEWLINE
           << "  <id>      - Clear breakpoint with specific ID" << NEWLINE
           << "  addr <addr> - Clear breakpoint at specific address" << NEWLINE
           << "  port <port> - Clear breakpoint at specific port" << NEWLINE
           << "  mem       - Clear all memory breakpoints" << NEWLINE
           << "  port      - Clear all port breakpoints" << NEWLINE
           << "  read      - Clear all memory read breakpoints" << NEWLINE
           << "  write     - Clear all memory write breakpoints" << NEWLINE
           << "  exec      - Clear all execution breakpoints" << NEWLINE
           << "  in        - Clear all port IN breakpoints" << NEWLINE
           << "  out       - Clear all port OUT breakpoints" << NEWLINE
           << "  group <name> - Clear all breakpoints in a group";
        session.SendResponse(ss.str());

        return;
    }

    std::string option = args[0];

    if (option == "all")
    {
        bpManager->ClearBreakpoints();
        session.SendResponse("All breakpoints cleared\n");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "addr" && args.size() > 1)
    {
        uint16_t address;
        if (!ParseAddress(args[1], address))
        {
            session.SendResponse("Invalid address format or out of range (must be 0-65535)");
            return;
        }

        bool result = bpManager->RemoveBreakpointByAddress(address);
        if (result)
        {
            session.SendResponse("Breakpoint at address 0x" + std::to_string(address) + " cleared");

            // Notify UI components that breakpoints have changed
            onBreakpointsChanged();
        }
        else
            session.SendResponse("No breakpoint found at address 0x" + std::to_string(address));
    }
    else if (option == "port" && args.size() == 1)
    {
        bpManager->RemoveBreakpointsByType(BRK_IO);
        session.SendResponse("All port breakpoints cleared");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "port" && args.size() > 1)
    {
        uint16_t port;
        if (!ParseAddress(args[1], port))
        {
            session.SendResponse("Invalid port format or out of range (must be 0-65535)");
            return;
        }

        bool result = bpManager->RemoveBreakpointByPort(port);
        if (result)
        {
            session.SendResponse("Breakpoint at port 0x" + std::to_string(port) + " cleared");

            // Notify UI components that breakpoints have changed
            onBreakpointsChanged();
        }
        else
            session.SendResponse("No breakpoint found at port 0x" + std::to_string(port));
    }
    else if (option == "mem")
    {
        bpManager->RemoveBreakpointsByType(BRK_MEMORY);
        session.SendResponse("All memory breakpoints cleared");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "read")
    {
        bpManager->RemoveMemoryBreakpointsByType(BRK_MEM_READ);
        session.SendResponse("All memory read breakpoints cleared");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "write")
    {
        bpManager->RemoveMemoryBreakpointsByType(BRK_MEM_WRITE);
        session.SendResponse("All memory write breakpoints cleared");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "exec")
    {
        bpManager->RemoveMemoryBreakpointsByType(BRK_MEM_EXECUTE);
        session.SendResponse("All execution breakpoints cleared");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "in")
    {
        bpManager->RemovePortBreakpointsByType(BRK_IO_IN);
        session.SendResponse("All port IN breakpoints cleared");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "out")
    {
        bpManager->RemovePortBreakpointsByType(BRK_IO_OUT);
        session.SendResponse("All port OUT breakpoints cleared");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "group" && args.size() > 1)
    {
        std::string groupName = args[1];
        bpManager->RemoveBreakpointGroup(groupName);
        session.SendResponse("All breakpoints in group '" + groupName + "' cleared");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else
    {
        // Try to interpret as a breakpoint ID
        uint16_t id;
        if (ParseAddress(option, id))
        {
            bool result = bpManager->RemoveBreakpointByID(id);
            if (result)
            {
                session.SendResponse("Breakpoint #" + std::to_string(id) + " cleared");

                // Notify UI components that breakpoints have changed
                onBreakpointsChanged();
            }
            else
                session.SendResponse("No breakpoint found with ID " + std::to_string(id));
        }
        else
        {
            session.SendResponse("Invalid option or breakpoint ID. Use 'bpclear' for help.");
        }
    }
}

void CLIProcessor::HandleBPGroup(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }

    if (args.empty())
    {
        std::stringstream ss;
        ss << "Usage: bpgroup <command> [parameters]" << NEWLINE
           << "Commands:" << NEWLINE
           << "  list             - List all breakpoint groups" << NEWLINE
           << "  show <name>      - Show breakpoints in a specific group" << NEWLINE
           << "  set <id> <name>  - Assign a breakpoint to a group" << NEWLINE
           << "  remove <id>      - Remove a breakpoint from its group (sets to 'default')";
        session.SendResponse(ss.str());
        return;
    }

    std::string command = args[0];

    if (command == "list")
    {
        std::vector<std::string> groups = bpManager->GetBreakpointGroups();

        if (groups.empty())
        {
            session.SendResponse("No breakpoint groups defined");
            return;
        }

        std::ostringstream oss;
        oss << "Breakpoint groups:" << NEWLINE;
        for (const auto& group : groups)
        {
            auto breakpoints = bpManager->GetBreakpointsByGroup(group);
            oss << "  " << group << " (" << breakpoints.size() << " breakpoints)" << NEWLINE;
        }

        session.SendResponse(oss.str());
    }
    else if (command == "show" && args.size() > 1)
    {
        std::string groupName = args[1];
        std::string list = bpManager->GetBreakpointListAsStringByGroup(groupName);
        session.SendResponse(list);
    }
    else if (command == "set" && args.size() > 2)
    {
        uint16_t id;
        if (!ParseAddress(args[1], id))
        {
            session.SendResponse("Invalid breakpoint ID format or out of range");
            return;
        }

        std::string groupName = args[2];
        bool result = bpManager->SetBreakpointGroup(id, groupName);
        if (result)
        {
            session.SendResponse("Breakpoint #" + std::to_string(id) + " assigned to group '" + groupName + "'");

            // Notify UI components that breakpoints have changed
            onBreakpointsChanged();
        }
        else
            session.SendResponse("Failed to assign breakpoint to group. Check if the breakpoint ID is valid.");
    }
    else if (command == "remove" && args.size() > 1)
    {
        uint16_t id;
        if (!ParseAddress(args[1], id))
        {
            session.SendResponse("Invalid breakpoint ID format or out of range");
            return;
        }

        bool result = bpManager->RemoveBreakpointFromGroup(id);
        if (result)
        {
            session.SendResponse("Breakpoint #" + std::to_string(id) + " removed from its group (set to 'default')");

            // Notify UI components that breakpoints have changed
            onBreakpointsChanged();
        }
        else
            session.SendResponse("Failed to remove breakpoint from group. Check if the breakpoint ID is valid.");
    }
    else
    {
        session.SendResponse("Invalid command. Use 'bpgroup' for help.");
    }
}

void CLIProcessor::HandleBPActivate(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }

    if (args.empty())
    {
        std::stringstream ss;
        ss << "Usage: bpon <option>" << NEWLINE
           << "Options:" << NEWLINE
           << "  all       - Activate all breakpoints" << NEWLINE
           << "  <id>      - Activate breakpoint with specific ID" << NEWLINE
           << "  mem       - Activate all memory breakpoints" << NEWLINE
           << "  port      - Activate all port breakpoints" << NEWLINE
           << "  read      - Activate all memory read breakpoints" << NEWLINE
           << "  write     - Activate all memory write breakpoints" << NEWLINE
           << "  exec      - Activate all execution breakpoints" << NEWLINE
           << "  in        - Activate all port IN breakpoints" << NEWLINE
           << "  out       - Activate all port OUT breakpoints" << NEWLINE
           << "  group <name> - Activate all breakpoints in a group";
        session.SendResponse(ss.str());
        return;
    }

    std::string option = args[0];

    if (option == "all")
    {
        bpManager->ActivateAllBreakpoints();
        session.SendResponse("All breakpoints activated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "mem")
    {
        bpManager->ActivateBreakpointsByType(BRK_MEMORY);
        session.SendResponse("All memory breakpoints activated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "port")
    {
        bpManager->ActivateBreakpointsByType(BRK_IO);
        session.SendResponse("All port breakpoints activated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "read")
    {
        bpManager->ActivateMemoryBreakpointsByType(BRK_MEM_READ);
        session.SendResponse("All memory read breakpoints activated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "write")
    {
        bpManager->ActivateMemoryBreakpointsByType(BRK_MEM_WRITE);
        session.SendResponse("All memory write breakpoints activated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "exec")
    {
        bpManager->ActivateMemoryBreakpointsByType(BRK_MEM_EXECUTE);
        session.SendResponse("All execution breakpoints activated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "in")
    {
        bpManager->ActivatePortBreakpointsByType(BRK_IO_IN);
        session.SendResponse("All port IN breakpoints activated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "out")
    {
        bpManager->ActivatePortBreakpointsByType(BRK_IO_OUT);
        session.SendResponse("All port OUT breakpoints activated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "group" && args.size() > 1)
    {
        std::string groupName = args[1];
        bpManager->ActivateBreakpointGroup(groupName);
        session.SendResponse("All breakpoints in group '" + groupName + "' activated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else
    {
        // Try to interpret as a breakpoint ID
        uint16_t id;
        if (ParseAddress(option, id))
        {
            bool result = bpManager->ActivateBreakpoint(id);
            if (result)
            {
                session.SendResponse("Breakpoint #" + std::to_string(id) + " activated");

                // Notify UI components that breakpoints have changed
                onBreakpointsChanged();
            }
            else
                session.SendResponse("No breakpoint found with ID " + std::to_string(id));
        }
        else
        {
            session.SendResponse("Invalid option or breakpoint ID. Use 'bpon' for help.");
        }
    }
}

void CLIProcessor::HandleBPDeactivate(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }

    if (args.empty())
    {
        std::stringstream ss;
        ss << "Usage: bpoff <option>" << NEWLINE
           << "Options:" << NEWLINE
           << "  all       - Deactivate all breakpoints" << NEWLINE
           << "  <id>      - Deactivate breakpoint with specific ID" << NEWLINE
           << "  mem       - Deactivate all memory breakpoints" << NEWLINE
           << "  port      - Deactivate all port breakpoints" << NEWLINE
           << "  read      - Deactivate all memory read breakpoints" << NEWLINE
           << "  write     - Deactivate all memory write breakpoints" << NEWLINE
           << "  exec      - Deactivate all execution breakpoints" << NEWLINE
           << "  in        - Deactivate all port IN breakpoints" << NEWLINE
           << "  out       - Deactivate all port OUT breakpoints" << NEWLINE
           << "  group <name> - Deactivate all breakpoints in a group";
        session.SendResponse(ss.str());
        return;
    }

    std::string option = args[0];

    if (option == "all")
    {
        bpManager->DeactivateAllBreakpoints();
        session.SendResponse("All breakpoints deactivated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "mem")
    {
        bpManager->DeactivateBreakpointsByType(BRK_MEMORY);
        session.SendResponse("All memory breakpoints deactivated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "port")
    {
        bpManager->DeactivateBreakpointsByType(BRK_IO);
        session.SendResponse("All port breakpoints deactivated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "read")
    {
        bpManager->DeactivateMemoryBreakpointsByType(BRK_MEM_READ);
        session.SendResponse("All memory read breakpoints deactivated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "write")
    {
        bpManager->DeactivateMemoryBreakpointsByType(BRK_MEM_WRITE);
        session.SendResponse("All memory write breakpoints deactivated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "exec")
    {
        bpManager->DeactivateMemoryBreakpointsByType(BRK_MEM_EXECUTE);
        session.SendResponse("All execution breakpoints deactivated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "in")
    {
        bpManager->DeactivatePortBreakpointsByType(BRK_IO_IN);
        session.SendResponse("All port IN breakpoints deactivated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "out")
    {
        bpManager->DeactivatePortBreakpointsByType(BRK_IO_OUT);
        session.SendResponse("All port OUT breakpoints deactivated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else if (option == "group" && args.size() > 1)
    {
        std::string groupName = args[1];
        bpManager->DeactivateBreakpointGroup(groupName);
        session.SendResponse("All breakpoints in group '" + groupName + "' deactivated");

        // Notify UI components that breakpoints have changed
        onBreakpointsChanged();
    }
    else
    {
        // Try to interpret as a breakpoint ID
        uint16_t id;
        if (ParseAddress(option, id))
        {
            bool result = bpManager->DeactivateBreakpoint(id);
            if (result)
            {
                session.SendResponse("Breakpoint #" + std::to_string(id) + " deactivated");

                // Notify UI components that breakpoints have changed
                onBreakpointsChanged();
            }
            else
                session.SendResponse("No breakpoint found with ID " + std::to_string(id));
        }
        else
        {
            session.SendResponse("Invalid option or breakpoint ID. Use 'bpoff' for help.");
        }
    }
}

void CLIProcessor::HandleOpen(const ClientSession& session, const std::vector<std::string>& args)
{
    // Get the MessageCenter instance
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();

    if (args.empty())
    {
        // No filepath provided, send a message to open the file dialog
        session.SendResponse("Requesting file open dialog...\n");
        messageCenter.Post(NC_FILE_OPEN_REQUEST, nullptr, true);
    }
    else
    {
        // Filepath provided, check if it exists
        std::string filepath = args[0];

        // Send the filepath in the message payload using the existing SimpleTextPayload
        session.SendResponse("Requesting to open file: " + filepath);
        messageCenter.Post(NC_FILE_OPEN_REQUEST, new SimpleTextPayload(filepath), true);
    }
}
