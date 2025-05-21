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
        {"break", &CLIProcessor::HandleBreakpoint},
        {"breakpoint", &CLIProcessor::HandleBreakpoint},
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
    // Get the selected emulator or use the auto-selected one
    std::shared_ptr<Emulator> emulator = nullptr;

    if (!session.GetSelectedEmulatorId().empty())
    {
        // If the client session has a selected emulator ID, use that
        auto* emulatorManager = EmulatorManager::GetInstance();
        emulator = emulatorManager->GetEmulator(session.GetSelectedEmulatorId());
    }
    else if (_emulator)
    {
        // If no explicit selection but we have an auto-selected emulator, use that
        emulator = _emulator;
        
        // Update the session with the auto-selected emulator ID for consistency
        if (emulator)
        {
            const_cast<ClientSession&>(session).SetSelectedEmulatorId(emulator->GetId());
        }
    }

    return emulator;
}

// Command handler implementations
void CLIProcessor::HandleHelp(const ClientSession& session, const std::vector<std::string>& args)
{
    std::string response =
        "Available commands:\n"
        "  help           - Show this help message\n"
        "  ?              - Show this help message\n"
        "  status         - Show emulator status\n"
        "  list           - List all emulator instances\n"
        "  select <id>    - Select an emulator by ID, index, or name\n"
        "  reset          - Reset the selected emulator\n"
        "  pause          - Pause emulation\n"
        "  resume         - Resume emulation\n"
        "  step [count]   - Single step execution\n"
        "  memory <addr>  - Examine memory\n"
        "  registers      - Show CPU registers\n"
        "  break <addr>   - Manage breakpoints\n"
        "  exit           - Exit the CLI\n"
        "  quit           - Exit the CLI\n";
    session.SendResponse(response);
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
        session.SendResponse("Usage: memory <address> [count]\n");
        return;
    }

    try
    {
        uint32_t address = std::stoul(args[0], nullptr, 0);
        size_t count = 16;
        if (args.size() > 1)
        {
            count = std::stoul(args[1]);
        }

        std::ostringstream oss;
        oss << "Memory dump at 0x" << std::hex << address << " (" << std::dec << count << " bytes):\n";

        // TODO: Implement actual memory dump
        oss << "Memory dump not yet implemented\n";

        session.SendResponse(oss.str());
    }
    catch (const std::exception& e)
    {
        session.SendResponse("Invalid address or count: " + std::string(e.what()) + "\n");
    }
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
        session.SendResponse("Usage: break <address>\n");
        return;
    }

    try
    {
        uint32_t address = std::stoul(args[0], nullptr, 0);
        BreakpointManager* bpManager = emulator->GetBreakpointManager();

        if (bpManager)
        {
            uint16_t bpId = bpManager->AddExecutionBreakpoint(static_cast<uint16_t>(address));

            std::ostringstream oss;
            if (bpId != 0xFFFF)
            {
                oss << "Breakpoint set at 0x" << std::hex << address << "\n";
            }
            else
            {
                oss << "Failed to set breakpoint at 0x" << std::hex << address << "\n";
            }

            session.SendResponse(oss.str());
        }
        else
        {
            session.SendResponse("Breakpoint manager not available\n");
        }
    }
    catch (const std::exception& e)
    {
        session.SendResponse("Invalid address: " + std::string(e.what()) + "\n");
    }
}
