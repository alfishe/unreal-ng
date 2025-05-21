#include "cli-processor.h"

#include <debugger/debugmanager.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/disassembler/z80disasm.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <3rdparty/message-center/messagecenter.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iomanip>
#include <iostream>
#include <sstream>

// ClientSession implementation
void ClientSession::SendResponse(const std::string& message) const
{
    if (_clientSocket != -1)
    {
        send(_clientSocket, message.c_str(), message.length(), 0);
    }
}

// CLIProcessor implementation
CLIProcessor::CLIProcessor() : _emulator(nullptr), _isFirstCommand(true)
{
    // Initialize command handlers map
    _commandHandlers =
    {
        {"help", &CLIProcessor::HandleHelp},
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
        {"bp", &CLIProcessor::HandleBreakpoint},      // Set execution breakpoint
        {"break", &CLIProcessor::HandleBreakpoint},   // Alias for bp
        {"breakpoint", &CLIProcessor::HandleBreakpoint}, // Alias for bp
        {"bplist", &CLIProcessor::HandleBPList},     // List all breakpoints
        {"wp", &CLIProcessor::HandleWatchpoint},     // Set memory read/write watchpoint
        {"bport", &CLIProcessor::HandlePortBreakpoint}, // Set port breakpoint
        {"bpclear", &CLIProcessor::HandleBPClear},   // Clear breakpoints
        {"bpgroup", &CLIProcessor::HandleBPGroup},   // Manage breakpoint groups
        {"bpon", &CLIProcessor::HandleBPActivate},   // Activate breakpoints
        {"bpoff", &CLIProcessor::HandleBPDeactivate}, // Deactivate breakpoints
        
        {"open", &CLIProcessor::HandleOpen},
        {"exit", &CLIProcessor::HandleExit},
        {"quit", &CLIProcessor::HandleExit},
        {"dummy", &CLIProcessor::HandleDummy}
    };
}

void CLIProcessor::ProcessCommand(ClientSession& session, const std::string& command)
{
    // Special handling for the first command
    if (_isFirstCommand)
    {
        // Force a direct response to ensure the client connection is working
        session.SendResponse("Processing first command...\n");

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
            // Call the member function using the this pointer
            (this->*(it->second))(session, args);
        }
        catch (const std::exception& e)
        {
            session.SendResponse("Error processing command: " + std::string(e.what()) + "\n");
        }
    }
    else
    {
        std::string error = "Unknown command: " + cmd + "\nType 'help' for available commands.\n";
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

// Command handler implementations
void CLIProcessor::HandleHelp(const ClientSession& session, const std::vector<std::string>& args)
{
    std::ostringstream oss;
    oss << "Available commands:\n";
    oss << "  help, ?       - Show this help message\n";
    oss << "  status        - Show emulator status\n";
    oss << "  list          - List available emulators\n";
    oss << "  select <id>   - Select an emulator\n";
    oss << "  reset         - Reset the emulator\n";
    oss << "  pause         - Pause emulation\n";
    oss << "  resume        - Resume emulation\n";
    oss << "  step [count]  - Execute one or more CPU instructions\n";
    oss << "  memory <addr> - View memory at address\n";
    oss << "  registers     - Show CPU registers\n";
    oss << "\nBreakpoint commands:\n";
    oss << "  bp <addr>     - Set execution breakpoint at address\n";
    oss << "  wp <addr> <type> - Set memory watchpoint (r/w/rw)\n";
    oss << "  bport <port> <type> - Set port breakpoint (i/o/io)\n";
    oss << "  bplist        - List all breakpoints\n";
    oss << "  bpclear       - Clear breakpoints\n";
    oss << "  bpgroup       - Manage breakpoint groups\n";
    oss << "  bpon          - Activate breakpoints\n";
    oss << "  bpoff         - Deactivate breakpoints\n";
    oss << "\nOther commands:\n";
    oss << "  open [file]   - Open a file or show file dialog\n";
    oss << "  exit, quit    - Exit the CLI\n";
    oss << "\nType any command followed by -h or --help for more information.\n";

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
        status = "No emulator instances found\n";
    }
    else
    {
        status = "Emulator Instances:\n";
        status += "==================\n";

        for (const auto& id : emulatorIds)
        {
            auto emulator = emulatorManager->GetEmulator(id);
            if (emulator)
            {
                status += "ID: " + id + "\n";
                status += "Status: " + std::string(emulator->IsRunning() ? "Running" : "Stopped") + "\n";
                status += "Debug: " + std::string(emulator->IsDebug() ? "On" : "Off") + "\n";

                // Indicate if this is the currently selected emulator
                // Check both the session's selected ID and our local _emulator reference
                bool isSelected = (session.GetSelectedEmulatorId() == id) || 
                                 (_emulator && _emulator->GetId() == id && session.GetSelectedEmulatorId().empty());
                if (isSelected)
                {
                    status += "SELECTED\n";
                }

                status += "------------------\n";
            }
        }

        // Add current active emulator status if available
        if (_emulator)
        {
            status += "\nCurrent CLI Emulator: " + _emulator->GetId() + "\n";
            status += "Status: " + std::string(_emulator->IsRunning() ? "Running" : "Stopped") + "\n";
        }
    }

    session.SendResponse(status);
}

void CLIProcessor::HandleList(const ClientSession& session, const std::vector<std::string>& args)
{
    auto* emulatorManager = EmulatorManager::GetInstance();
    if (!emulatorManager)
    {
        session.SendResponse("Error: Unable to access emulator manager.\n");
        return;
    }

    // Force a refresh of emulator instances by calling a method that updates the internal state
    auto mostRecent = emulatorManager->GetMostRecentEmulator();

    // Now get the updated list of emulator IDs
    auto emulatorIds = emulatorManager->GetEmulatorIds();

    if (emulatorIds.empty())
    {
        session.SendResponse("No emulator instances found.\n");
        return;
    }

    std::string response = "Available emulator instances:\n";
    response += "============================\n";

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

            response += "\n     Status: " + std::string(emulator->IsRunning() ? "Running" : "Stopped");
            response += "\n     Debug: " + std::string(emulator->IsDebug() ? "On" : "Off");
            response += "\n";
        }
    }

    response += "\nUse 'select <index>' or 'select <id>' to choose an emulator.\n";

    session.SendResponse(response);
}

void CLIProcessor::HandleSelect(const ClientSession& session, const std::vector<std::string>& args)
{
    if (args.empty())
    {
        session.SendResponse("Usage: select <index|id|name>\n");
        session.SendResponse("Use 'list' to see available emulators.\n");
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
        session.SendResponse("No emulator instances available.\n");
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
                session.SendResponse("Error: Index out of bounds\n");
                return;
            }
        }
        else
        {
            session.SendResponse("Invalid emulator index. Use 'list' to see available emulators.\n");
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
                session.SendResponse("No emulator found matching: " + selector + "\n");
                session.SendResponse("Use 'list' to see available emulators.\n");
                return;
            }
        }
    }

    // We have a valid ID at this point
    const_cast<ClientSession&>(session).SetSelectedEmulatorId(selectedId);

    // Also update our local reference to the emulator
    _emulator = emulatorManager->GetEmulator(selectedId);

    std::string response = "Selected emulator: " + selectedId;
    if (_emulator)
    {
        response += " (" + std::string(_emulator->IsRunning() ? "Running" : "Stopped") + ")";
    }
    response += "\n";

    session.SendResponse(response);
}

void CLIProcessor::HandleExit(const ClientSession& session, const std::vector<std::string>& args)
{
    session.SendResponse("Goodbye!\n");
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
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.\n");
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
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.\n");
        return;
    }

    // Check if the emulator is already paused
    if (emulator->IsPaused())
    {
        session.SendResponse("Emulator is already paused.\n");
        return;
    }

    // Pause the emulator - this will trigger MessageCenter notifications
    // that the GUI will respond to (enabling/disabling buttons)
    emulator->Pause();
    
    // Confirm to the user
    session.SendResponse("Emulation paused. Use 'resume' to continue execution.\n");
}

void CLIProcessor::HandleResume(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.\n");
        return;
    }

    // Check if the emulator is already running
    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator is already running.\n");
        return;
    }

    // Resume the emulator - this will trigger MessageCenter notifications
    // that the GUI will respond to (enabling/disabling buttons)
    emulator->Resume();
    
    // Confirm to the user
    session.SendResponse("Emulation resumed. Use 'pause' to suspend execution.\n");
}

void CLIProcessor::HandleStep(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.\n");
        return;
    }

    // Check if emulator is paused
    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.\n");
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
        session.SendResponse("Error: Unable to access memory or disassembler.\n");
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
        session.SendResponse("Error: Unable to get physical address for PC.\n");
        return;
    }
    
    // Disassemble the instruction that's about to be executed
    uint8_t commandLen = 0;
    DecodedInstruction decodedBefore;
    std::string instructionBefore = disassembler->disassembleSingleCommandWithRuntime(
        pcPhysicalAddress, 6, &commandLen, z80State, memory, &decodedBefore);
    
    // Execute the requested number of CPU cycles
    for (int i = 0; i < stepCount; ++i)
    {
        emulator->RunSingleCPUCycle(false); // false = don't skip breakpoints
    }

    // Get the Z80 state after execution
    z80State = emulator->GetZ80State(); // Refresh state after execution
    if (!z80State)
    {
        session.SendResponse("Error: Unable to access Z80 state after execution.\n");
        return;
    }
    
    // Get the new PC and disassemble the next instruction to be executed
    uint16_t newPC = z80State->pc;
    pcPhysicalAddress = memory->MapZ80AddressToPhysicalAddress(newPC);
    if (!pcPhysicalAddress)
    {
        session.SendResponse("Error: Unable to get physical address for new PC.\n");
        return;
    }
    
    // Disassemble the next instruction to be executed
    commandLen = 0;
    DecodedInstruction decodedAfter;
    std::string instructionAfter = disassembler->disassembleSingleCommandWithRuntime(
        pcPhysicalAddress, 6, &commandLen, z80State, memory, &decodedAfter);

    // Format response with CPU state information
    std::stringstream ss;
    ss << "Executed " << stepCount << " instruction" << (stepCount != 1 ? "s" : "") << "\n";
    
    // Show executed instruction
    ss << std::hex << std::uppercase << std::setfill('0');
    ss << "Executed: [$" << std::setw(4) << initialPC << "] ";
    
    // Add hex dump of the executed instruction
    if (decodedBefore.instructionBytes.size() > 0) {
        for (uint8_t byte : decodedBefore.instructionBytes) {
            ss << std::setw(2) << static_cast<int>(byte) << " ";
        }
        // Add padding for alignment if needed
        for (size_t i = decodedBefore.instructionBytes.size(); i < 4; i++) {
            ss << "   ";
        }
    }
    
    ss << instructionBefore << "\n";
    
    // Show next instruction
    ss << "Next:     [$" << std::setw(4) << newPC << "] ";
    
    // Add hex dump of the next instruction
    if (decodedAfter.instructionBytes.size() > 0) {
        for (uint8_t byte : decodedAfter.instructionBytes) {
            ss << std::setw(2) << static_cast<int>(byte) << " ";
        }
        // Add padding for alignment if needed
        for (size_t i = decodedAfter.instructionBytes.size(); i < 4; i++) {
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
    ss << "HL: $" << std::setw(4) << z80State->hl << "\n";
    
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
    ss << "\n";
    
    // Add note about viewing full register state
    ss << "\nUse 'registers' command to view full CPU state\n";
    
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleMemory(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.\n");
        return;
    }

    if (args.empty())
    {
        session.SendResponse("Usage: memory <address>\n");
        session.SendResponse("Displays memory contents at the specified address.\n");
        session.SendResponse("Examples:\n");
        session.SendResponse("  memory 0x1000    - View memory at address 0x1000 (hex)\n");
        session.SendResponse("  memory $8000     - View memory at address $8000 (hex)\n");
        session.SendResponse("  memory #C000     - View memory at address #C000 (hex)\n");
        session.SendResponse("  memory 32768     - View memory at address 32768 (decimal)\n");
        return;
    }

    uint16_t address;
    if (!ParseAddress(args[0], address))
    {
        session.SendResponse("Invalid address format or out of range (must be 0-65535)\n");
        return;
    }

    Memory* memory = emulator->GetMemory();
    if (!memory)
    {
        session.SendResponse("Memory not available\n");
        return;
    }

    std::ostringstream oss;
    oss << "Memory at 0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << address << ":\n";

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

        oss << "\n";
    }

    session.SendResponse(oss.str());
}

void CLIProcessor::HandleRegisters(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.\n");
        return;
    }

    // Get the Z80 state from the emulator
    Z80State* z80State = emulator->GetZ80State();
    if (!z80State)
    {
        session.SendResponse("Error: Unable to access Z80 state.\n");
        return;
    }

    // Format the register values
    std::stringstream ss;
    ss << "Z80 Registers:\n";
    ss << "=============\n\n";
    
    // Main register pairs and alternate registers side by side
    ss << "Main registers:                     Alternate registers:\n";
    ss << std::hex << std::uppercase << std::setfill('0');
    
    // Access alternate registers from the shadow registers in Z80State
    ss << "  AF: " << std::setw(4) << z80State->af << "  (A: " << std::setw(2) << static_cast<int>(z80State->a) 
       << ", F: " << std::setw(2) << static_cast<int>(z80State->f) << ")";
    ss << "           AF': " << std::setw(4) << z80State->alt.af << "  (A': " << std::setw(2) << static_cast<int>(z80State->alt.a) 
       << ", F': " << std::setw(2) << static_cast<int>(z80State->alt.f) << ")\n";
       
    ss << "  BC: " << std::setw(4) << z80State->bc << "  (B: " << std::setw(2) << static_cast<int>(z80State->b) 
       << ", C: " << std::setw(2) << static_cast<int>(z80State->c) << ")";
    ss << "           BC': " << std::setw(4) << z80State->alt.bc << "  (B': " << std::setw(2) << static_cast<int>(z80State->alt.b) 
       << ", C': " << std::setw(2) << static_cast<int>(z80State->alt.c) << ")\n";
       
    ss << "  DE: " << std::setw(4) << z80State->de << "  (D: " << std::setw(2) << static_cast<int>(z80State->d) 
       << ", E: " << std::setw(2) << static_cast<int>(z80State->e) << ")";
    ss << "           DE': " << std::setw(4) << z80State->alt.de << "  (D': " << std::setw(2) << static_cast<int>(z80State->alt.d) 
       << ", E': " << std::setw(2) << static_cast<int>(z80State->alt.e) << ")\n";
       
    ss << "  HL: " << std::setw(4) << z80State->hl << "  (H: " << std::setw(2) << static_cast<int>(z80State->h) 
       << ", L: " << std::setw(2) << static_cast<int>(z80State->l) << ")";
    ss << "           HL': " << std::setw(4) << z80State->alt.hl << "  (H': " << std::setw(2) << static_cast<int>(z80State->alt.h) 
       << ", L': " << std::setw(2) << static_cast<int>(z80State->alt.l) << ")\n";
       
    ss << "\n";
    
    // Index and special registers in two columns
    ss << "Index registers:                    Special registers:\n";
    ss << "  IX: " << std::setw(4) << z80State->ix << "  (IXH: " << std::setw(2) << static_cast<int>(z80State->xh) 
       << ", IXL: " << std::setw(2) << static_cast<int>(z80State->xl) << ")";
    ss << "       PC: " << std::setw(4) << z80State->pc << "\n";
       
    ss << "  IY: " << std::setw(4) << z80State->iy << "  (IYH: " << std::setw(2) << static_cast<int>(z80State->yh) 
       << ", IYL: " << std::setw(2) << static_cast<int>(z80State->yl) << ")";
    ss << "       SP: " << std::setw(4) << z80State->sp << "\n";
    
    // Empty line for IR and first line of flags
    ss << "                                     IR: " << std::setw(4) << z80State->ir_ << "  (I: " << std::setw(2) << static_cast<int>(z80State->i) 
       << ", R: " << std::setw(2) << static_cast<int>(z80State->r_low) << ")\n\n";
    
    // Flags and interrupt state in two columns
    ss << "Flags (" << std::setw(2) << static_cast<int>(z80State->f) << "):                         Interrupt state:\n";
    ss << "  S: " << ((z80State->f & 0x80) ? "1" : "0") << " (Sign)";
    ss << "                        IFF1: " << (z80State->iff1 ? "Enabled" : "Disabled") << "\n";
    
    ss << "  Z: " << ((z80State->f & 0x40) ? "1" : "0") << " (Zero)";
    ss << "                        IFF2: " << (z80State->iff2 ? "Enabled" : "Disabled") << "\n";
    
    ss << "  5: " << ((z80State->f & 0x20) ? "1" : "0") << " (Unused bit 5)";
    ss << "                HALT: " << (z80State->halted ? "Yes" : "No") << "\n";
    
    ss << "  H: " << ((z80State->f & 0x10) ? "1" : "0") << " (Half-carry)\n";
    ss << "  3: " << ((z80State->f & 0x08) ? "1" : "0") << " (Unused bit 3)\n";
    ss << "  P/V: " << ((z80State->f & 0x04) ? "1" : "0") << " (Parity/Overflow)\n";
    ss << "  N: " << ((z80State->f & 0x02) ? "1" : "0") << " (Add/Subtract)\n";
    ss << "  C: " << ((z80State->f & 0x01) ? "1" : "0") << " (Carry)";
    ss << std::endl;
    
    // Send the formatted register dump
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleBreakpoint(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.\n");
        return;
    }

    if (args.empty())
    {
        session.SendResponse("Usage: bp <address> [note]\n");
        session.SendResponse("Sets an execution breakpoint at the specified address.\n");
        session.SendResponse("Examples:\n");
        session.SendResponse("  bp 0x1234       - Set breakpoint at address 0x1234\n");
        session.SendResponse("  bp $1234        - Set breakpoint at address $1234 (hex)\n");
        session.SendResponse("  bp #1234        - Set breakpoint at address #1234 (hex)\n");
        session.SendResponse("  bp 1234         - Set breakpoint at address 1234 (decimal)\n");
        session.SendResponse("  bp 1234 Main loop - Set breakpoint with a note\n");
        session.SendResponse("Use 'bplist' to view all breakpoints\n");
        return;
    }

    uint16_t address;
    if (!ParseAddress(args[0], address))
    {
        session.SendResponse("Invalid address format or out of range (must be 0-65535)\n");
        return;
    }

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available\n");
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
            if (i > 1) note += " ";
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
        oss << "Breakpoint #" << bpId << " set at 0x" << std::hex << std::setw(4) << std::setfill('0') << address << "\n";
    }
    else
    {
        oss << "Failed to set breakpoint at 0x" << std::hex << std::setw(4) << std::setfill('0') << address << "\n";
    }

    session.SendResponse(oss.str());
}

void CLIProcessor::HandleBPList(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.\n");
        return;
    }

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available\n");
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
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.\n");
        return;
    }

    if (args.empty() || args.size() < 2)
    {
        session.SendResponse("Usage: wp <address> <type> [note]\n");
        session.SendResponse("Sets a memory watchpoint at the specified address.\n");
        session.SendResponse("Types:\n");
        session.SendResponse("  r    - Watch for memory reads\n");
        session.SendResponse("  w    - Watch for memory writes\n");
        session.SendResponse("  rw   - Watch for both reads and writes\n");
        session.SendResponse("Examples:\n");
        session.SendResponse("  wp 0x1234 r     - Watch for reads at address 0x1234\n");
        session.SendResponse("  wp $4000 w      - Watch for writes at address $4000 (hex)\n");
        session.SendResponse("  wp #8000 rw     - Watch for reads/writes at address #8000 (hex)\n");
        session.SendResponse("  wp 49152 rw Stack pointer - Watch for reads/writes with a note\n");
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
        session.SendResponse("Invalid watchpoint type. Use 'r', 'w', or 'rw'.\n");
        return;
    }
    
    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available\n");
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
            if (i > 2) note += " ";
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
        if (memoryType & BRK_MEM_READ) oss << "read";
        if ((memoryType & BRK_MEM_READ) && (memoryType & BRK_MEM_WRITE)) oss << "/";
        if (memoryType & BRK_MEM_WRITE) oss << "write";
        oss << ")\n";
    }
    else
    {
        oss << "Failed to set watchpoint at 0x" << std::hex << std::setw(4) << std::setfill('0') << address << "\n";
    }

    session.SendResponse(oss.str());
}

void CLIProcessor::HandlePortBreakpoint(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.\n");
        return;
    }

    if (args.empty() || args.size() < 2)
    {
        session.SendResponse("Usage: bport <port> <type> [note]\n");
        session.SendResponse("Sets a port breakpoint at the specified port address.\n");
        session.SendResponse("Types:\n");
        session.SendResponse("  i    - Watch for port IN operations\n");
        session.SendResponse("  o    - Watch for port OUT operations\n");
        session.SendResponse("  io   - Watch for both IN and OUT operations\n");
        session.SendResponse("Examples:\n");
        session.SendResponse("  bport 0x1234 i     - Watch for IN operations at port 0x1234\n");
        session.SendResponse("  bport $FE o        - Watch for OUT operations at port $FE (hex)\n");
        session.SendResponse("  bport #A0 io       - Watch for IN/OUT at port #A0 (hex)\n");
        session.SendResponse("  bport 254 io Keyboard port - Watch for IN/OUT with a note\n");
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
        session.SendResponse("Invalid port breakpoint type. Use 'i', 'o', or 'io'.\n");
        return;
    }
    
    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available\n");
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
            if (i > 2) note += " ";
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
        oss << "Port breakpoint #" << bpId << " set at port 0x" << std::hex << std::setw(4) << std::setfill('0') << port;
        oss << " (";
        if (ioType & BRK_IO_IN) oss << "in";
        if ((ioType & BRK_IO_IN) && (ioType & BRK_IO_OUT)) oss << "/";
        if (ioType & BRK_IO_OUT) oss << "out";
        oss << ")\n";
    }
    else
    {
        oss << "Failed to set port breakpoint at 0x" << std::hex << std::setw(4) << std::setfill('0') << port << "\n";
    }

    session.SendResponse(oss.str());
}

void CLIProcessor::HandleBPClear(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.\n");
        return;
    }

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available\n");
        return;
    }

    if (args.empty())
    {
        session.SendResponse("Usage: bpclear <option>\n");
        session.SendResponse("Options:\n");
        session.SendResponse("  all       - Clear all breakpoints\n");
        session.SendResponse("  <id>      - Clear breakpoint with specific ID\n");
        session.SendResponse("  addr <addr> - Clear breakpoint at specific address\n");
        session.SendResponse("  port <port> - Clear breakpoint at specific port\n");
        session.SendResponse("  mem       - Clear all memory breakpoints\n");
        session.SendResponse("  port      - Clear all port breakpoints\n");
        session.SendResponse("  read      - Clear all memory read breakpoints\n");
        session.SendResponse("  write     - Clear all memory write breakpoints\n");
        session.SendResponse("  exec      - Clear all execution breakpoints\n");
        session.SendResponse("  in        - Clear all port IN breakpoints\n");
        session.SendResponse("  out       - Clear all port OUT breakpoints\n");
        session.SendResponse("  group <name> - Clear all breakpoints in a group\n");
        return;
    }

    std::string option = args[0];
    
    if (option == "all")
    {
        bpManager->ClearBreakpoints();
        session.SendResponse("All breakpoints cleared\n");
    }
    else if (option == "addr" && args.size() > 1)
    {
        uint16_t address;
        if (!ParseAddress(args[1], address))
        {
            session.SendResponse("Invalid address format or out of range (must be 0-65535)\n");
            return;
        }
        
        bool result = bpManager->RemoveBreakpointByAddress(address);
        if (result)
            session.SendResponse("Breakpoint at address 0x" + std::to_string(address) + " cleared\n");
        else
            session.SendResponse("No breakpoint found at address 0x" + std::to_string(address) + "\n");
    }
    else if (option == "port" && args.size() == 1)
    {
        bpManager->RemoveBreakpointsByType(BRK_IO);
        session.SendResponse("All port breakpoints cleared\n");
    }
    else if (option == "port" && args.size() > 1)
    {
        uint16_t port;
        if (!ParseAddress(args[1], port))
        {
            session.SendResponse("Invalid port format or out of range (must be 0-65535)\n");
            return;
        }
        
        bool result = bpManager->RemoveBreakpointByPort(port);
        if (result)
            session.SendResponse("Breakpoint at port 0x" + std::to_string(port) + " cleared\n");
        else
            session.SendResponse("No breakpoint found at port 0x" + std::to_string(port) + "\n");
    }
    else if (option == "mem")
    {
        bpManager->RemoveBreakpointsByType(BRK_MEMORY);
        session.SendResponse("All memory breakpoints cleared\n");
    }
    else if (option == "read")
    {
        bpManager->RemoveMemoryBreakpointsByType(BRK_MEM_READ);
        session.SendResponse("All memory read breakpoints cleared\n");
    }
    else if (option == "write")
    {
        bpManager->RemoveMemoryBreakpointsByType(BRK_MEM_WRITE);
        session.SendResponse("All memory write breakpoints cleared\n");
    }
    else if (option == "exec")
    {
        bpManager->RemoveMemoryBreakpointsByType(BRK_MEM_EXECUTE);
        session.SendResponse("All execution breakpoints cleared\n");
    }
    else if (option == "in")
    {
        bpManager->RemovePortBreakpointsByType(BRK_IO_IN);
        session.SendResponse("All port IN breakpoints cleared\n");
    }
    else if (option == "out")
    {
        bpManager->RemovePortBreakpointsByType(BRK_IO_OUT);
        session.SendResponse("All port OUT breakpoints cleared\n");
    }
    else if (option == "group" && args.size() > 1)
    {
        std::string groupName = args[1];
        bpManager->RemoveBreakpointGroup(groupName);
        session.SendResponse("All breakpoints in group '" + groupName + "' cleared\n");
    }
    else
    {
        // Try to interpret as a breakpoint ID
        uint16_t id;
        if (ParseAddress(option, id))
        {
            bool result = bpManager->RemoveBreakpointByID(id);
            if (result)
                session.SendResponse("Breakpoint #" + std::to_string(id) + " cleared\n");
            else
                session.SendResponse("No breakpoint found with ID " + std::to_string(id) + "\n");
        }
        else
        {
            session.SendResponse("Invalid option or breakpoint ID. Use 'bpclear' for help.\n");
        }
    }
}

void CLIProcessor::HandleBPGroup(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.\n");
        return;
    }

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available\n");
        return;
    }

    if (args.empty())
    {
        session.SendResponse("Usage: bpgroup <command> [parameters]\n");
        session.SendResponse("Commands:\n");
        session.SendResponse("  list             - List all breakpoint groups\n");
        session.SendResponse("  show <name>      - Show breakpoints in a specific group\n");
        session.SendResponse("  set <id> <name>  - Assign a breakpoint to a group\n");
        session.SendResponse("  remove <id>      - Remove a breakpoint from its group (sets to 'default')\n");
        return;
    }

    std::string command = args[0];
    
    if (command == "list")
    {
        std::vector<std::string> groups = bpManager->GetBreakpointGroups();
        
        if (groups.empty())
        {
            session.SendResponse("No breakpoint groups defined\n");
            return;
        }
        
        std::ostringstream oss;
        oss << "Breakpoint groups:\n";
        for (const auto& group : groups)
        {
            auto breakpoints = bpManager->GetBreakpointsByGroup(group);
            oss << "  " << group << " (" << breakpoints.size() << " breakpoints)\n";
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
            session.SendResponse("Invalid breakpoint ID format or out of range\n");
            return;
        }
        
        std::string groupName = args[2];
        bool result = bpManager->SetBreakpointGroup(id, groupName);
        if (result)
            session.SendResponse("Breakpoint #" + std::to_string(id) + " assigned to group '" + groupName + "'\n");
        else
            session.SendResponse("Failed to assign breakpoint to group. Check if the breakpoint ID is valid.\n");
    }
    else if (command == "remove" && args.size() > 1)
    {
        uint16_t id;
        if (!ParseAddress(args[1], id))
        {
            session.SendResponse("Invalid breakpoint ID format or out of range\n");
            return;
        }
        
        bool result = bpManager->RemoveBreakpointFromGroup(id);
        if (result)
            session.SendResponse("Breakpoint #" + std::to_string(id) + " removed from its group (set to 'default')\n");
        else
            session.SendResponse("Failed to remove breakpoint from group. Check if the breakpoint ID is valid.\n");
    }
    else
    {
        session.SendResponse("Invalid command. Use 'bpgroup' for help.\n");
    }
}

void CLIProcessor::HandleBPActivate(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.\n");
        return;
    }

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available\n");
        return;
    }

    if (args.empty())
    {
        session.SendResponse("Usage: bpon <option>\n");
        session.SendResponse("Options:\n");
        session.SendResponse("  all       - Activate all breakpoints\n");
        session.SendResponse("  <id>      - Activate breakpoint with specific ID\n");
        session.SendResponse("  mem       - Activate all memory breakpoints\n");
        session.SendResponse("  port      - Activate all port breakpoints\n");
        session.SendResponse("  read      - Activate all memory read breakpoints\n");
        session.SendResponse("  write     - Activate all memory write breakpoints\n");
        session.SendResponse("  exec      - Activate all execution breakpoints\n");
        session.SendResponse("  in        - Activate all port IN breakpoints\n");
        session.SendResponse("  out       - Activate all port OUT breakpoints\n");
        session.SendResponse("  group <name> - Activate all breakpoints in a group\n");
        return;
    }

    std::string option = args[0];
    
    if (option == "all")
    {
        bpManager->ActivateAllBreakpoints();
        session.SendResponse("All breakpoints activated\n");
    }
    else if (option == "mem")
    {
        bpManager->ActivateBreakpointsByType(BRK_MEMORY);
        session.SendResponse("All memory breakpoints activated\n");
    }
    else if (option == "port")
    {
        bpManager->ActivateBreakpointsByType(BRK_IO);
        session.SendResponse("All port breakpoints activated\n");
    }
    else if (option == "read")
    {
        bpManager->ActivateMemoryBreakpointsByType(BRK_MEM_READ);
        session.SendResponse("All memory read breakpoints activated\n");
    }
    else if (option == "write")
    {
        bpManager->ActivateMemoryBreakpointsByType(BRK_MEM_WRITE);
        session.SendResponse("All memory write breakpoints activated\n");
    }
    else if (option == "exec")
    {
        bpManager->ActivateMemoryBreakpointsByType(BRK_MEM_EXECUTE);
        session.SendResponse("All execution breakpoints activated\n");
    }
    else if (option == "in")
    {
        bpManager->ActivatePortBreakpointsByType(BRK_IO_IN);
        session.SendResponse("All port IN breakpoints activated\n");
    }
    else if (option == "out")
    {
        bpManager->ActivatePortBreakpointsByType(BRK_IO_OUT);
        session.SendResponse("All port OUT breakpoints activated\n");
    }
    else if (option == "group" && args.size() > 1)
    {
        std::string groupName = args[1];
        bpManager->ActivateBreakpointGroup(groupName);
        session.SendResponse("All breakpoints in group '" + groupName + "' activated\n");
    }
    else
    {
        // Try to interpret as a breakpoint ID
        uint16_t id;
        if (ParseAddress(option, id))
        {
            bool result = bpManager->ActivateBreakpoint(id);
            if (result)
                session.SendResponse("Breakpoint #" + std::to_string(id) + " activated\n");
            else
                session.SendResponse("No breakpoint found with ID " + std::to_string(id) + "\n");
        }
        else
        {
            session.SendResponse("Invalid option or breakpoint ID. Use 'bpon' for help.\n");
        }
    }
}

void CLIProcessor::HandleBPDeactivate(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.\n");
        return;
    }

    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available\n");
        return;
    }

    if (args.empty())
    {
        session.SendResponse("Usage: bpoff <option>\n");
        session.SendResponse("Options:\n");
        session.SendResponse("  all       - Deactivate all breakpoints\n");
        session.SendResponse("  <id>      - Deactivate breakpoint with specific ID\n");
        session.SendResponse("  mem       - Deactivate all memory breakpoints\n");
        session.SendResponse("  port      - Deactivate all port breakpoints\n");
        session.SendResponse("  read      - Deactivate all memory read breakpoints\n");
        session.SendResponse("  write     - Deactivate all memory write breakpoints\n");
        session.SendResponse("  exec      - Deactivate all execution breakpoints\n");
        session.SendResponse("  in        - Deactivate all port IN breakpoints\n");
        session.SendResponse("  out       - Deactivate all port OUT breakpoints\n");
        session.SendResponse("  group <name> - Deactivate all breakpoints in a group\n");
        return;
    }

    std::string option = args[0];
    
    if (option == "all")
    {
        bpManager->DeactivateAllBreakpoints();
        session.SendResponse("All breakpoints deactivated\n");
    }
    else if (option == "mem")
    {
        bpManager->DeactivateBreakpointsByType(BRK_MEMORY);
        session.SendResponse("All memory breakpoints deactivated\n");
    }
    else if (option == "port")
    {
        bpManager->DeactivateBreakpointsByType(BRK_IO);
        session.SendResponse("All port breakpoints deactivated\n");
    }
    else if (option == "read")
    {
        bpManager->DeactivateMemoryBreakpointsByType(BRK_MEM_READ);
        session.SendResponse("All memory read breakpoints deactivated\n");
    }
    else if (option == "write")
    {
        bpManager->DeactivateMemoryBreakpointsByType(BRK_MEM_WRITE);
        session.SendResponse("All memory write breakpoints deactivated\n");
    }
    else if (option == "exec")
    {
        bpManager->DeactivateMemoryBreakpointsByType(BRK_MEM_EXECUTE);
        session.SendResponse("All execution breakpoints deactivated\n");
    }
    else if (option == "in")
    {
        bpManager->DeactivatePortBreakpointsByType(BRK_IO_IN);
        session.SendResponse("All port IN breakpoints deactivated\n");
    }
    else if (option == "out")
    {
        bpManager->DeactivatePortBreakpointsByType(BRK_IO_OUT);
        session.SendResponse("All port OUT breakpoints deactivated\n");
    }
    else if (option == "group" && args.size() > 1)
    {
        std::string groupName = args[1];
        bpManager->DeactivateBreakpointGroup(groupName);
        session.SendResponse("All breakpoints in group '" + groupName + "' deactivated\n");
    }
    else
    {
        // Try to interpret as a breakpoint ID
        uint16_t id;
        if (ParseAddress(option, id))
        {
            bool result = bpManager->DeactivateBreakpoint(id);
            if (result)
                session.SendResponse("Breakpoint #" + std::to_string(id) + " deactivated\n");
            else
                session.SendResponse("No breakpoint found with ID " + std::to_string(id) + "\n");
        }
        else
        {
            session.SendResponse("Invalid option or breakpoint ID. Use 'bpoff' for help.\n");
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
        
        // Create a StringPayload with the filepath
        class StringPayload : public MessagePayload
        {
        public:
            StringPayload(const std::string& str) : _str(str) {}
            virtual ~StringPayload() {}
            
            const std::string& GetString() const { return _str; }
            
        private:
            std::string _str;
        };
        
        // Send the filepath in the message payload
        session.SendResponse("Requesting to open file: " + filepath + "\n");
        messageCenter.Post(NC_FILE_OPEN_REQUEST, new StringPayload(filepath), true);
    }
}
