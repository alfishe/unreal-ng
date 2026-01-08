#include "cli-processor.h"

#include <3rdparty/message-center/eventqueue.h>
#include <3rdparty/message-center/messagecenter.h>
#include <debugger/analyzers/basicextractor.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/debugmanager.h>
#include <debugger/disassembler/z80disasm.h>
#include <emulator/memory/memory.h>
#include <emulator/notifications.h>
#include <emulator/platform.h>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "base/featuremanager.h"
#include "common/stringhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/platform.h"
#include "platform-sockets.h"

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
                        {"step", &CLIProcessor::HandleStepIn},        // Always one instruction
                        {"stepin", &CLIProcessor::HandleStepIn},      // Always one instruction
                        {"steps", &CLIProcessor::HandleSteps},        // Execute 1...N instructions
                        {"stepover", &CLIProcessor::HandleStepOver},  // Execute instruction, skip calls
                        {"memory", &CLIProcessor::HandleMemory},
                        {"registers", &CLIProcessor::HandleRegisters},
                        {"debugmode", &CLIProcessor::HandleDebugMode},

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
                        {"dummy", &CLIProcessor::HandleDummy},
                        {"memcounters", &CLIProcessor::HandleMemCounters},
                        {"memstats", &CLIProcessor::HandleMemCounters},
                        {"calltrace", &CLIProcessor::HandleCallTrace},
                        {"feature", &CLIProcessor::HandleFeature},

                        // BASIC commands
                        {"basic", &CLIProcessor::HandleBasic},

                        // Settings commands
                        {"setting", &CLIProcessor::HandleSetting},
                        {"settings", &CLIProcessor::HandleSetting},
                        {"set", &CLIProcessor::HandleSetting},

                        // State inspection commands
                        {"state", &CLIProcessor::HandleState},

                        // Instance management commands
                        {"start", &CLIProcessor::HandleStart},
                        {"stop", &CLIProcessor::HandleStop},

                        // Tape control commands
                        {"tape", &CLIProcessor::HandleTape},

                        // Disk control commands
                        {"disk", &CLIProcessor::HandleDisk},

                        // Snapshot control commands
                        {"snapshot", &CLIProcessor::HandleSnapshot}};
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
    // In async mode, we auto-select only if there's exactly one emulator (stateless behavior)
    if (session.GetSelectedEmulatorId().empty() && !_emulator)
    {
        auto* emulatorManager = EmulatorManager::GetInstance();
        if (emulatorManager)
        {
            auto emulatorIds = emulatorManager->GetEmulatorIds();

            // Auto-select only if there's exactly one emulator (stateless)
            if (emulatorIds.size() == 1)
            {
                _emulator = emulatorManager->GetEmulator(emulatorIds[0]);
                // Note: We don't persist the selection in the session for stateless behavior
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
    auto* emulatorManager = EmulatorManager::GetInstance();
    if (!emulatorManager)
    {
        return nullptr;
    }

    // Get the selected emulator ID from the session
    std::string selectedId = session.GetSelectedEmulatorId();

    // If a specific emulator is selected, try to use it
    if (!selectedId.empty())
    {
        auto emulator = emulatorManager->GetEmulator(selectedId);
        if (emulator)
        {
            _emulator = emulator;
            return _emulator;
        }

        // Selected emulator no longer exists, clear the selection
        const_cast<ClientSession&>(session).SetSelectedEmulatorId("");
    }

    // No selection or selected emulator is gone - auto-select if only one emulator exists
    auto emulatorIds = emulatorManager->GetEmulatorIds();

    if (emulatorIds.size() == 1)
    {
        // Only one emulator - auto-select it (stateless behavior)
        _emulator = emulatorManager->GetEmulator(emulatorIds[0]);
        return _emulator;
    }
    else if (emulatorIds.size() > 1)
    {
        // Multiple emulators - require explicit selection
        _emulator.reset();
        return nullptr;
    }

    // No emulators available
    _emulator.reset();
    return nullptr;
}

std::shared_ptr<Emulator> CLIProcessor::ResolveEmulator(const ClientSession& session,
                                                        const std::vector<std::string>& args, std::string& errorMessage)
{
    auto* emulatorManager = EmulatorManager::GetInstance();
    if (!emulatorManager)
    {
        errorMessage = "EmulatorManager not available.";
        return nullptr;
    }

    // Check if an emulator ID or index is provided as first argument
    if (!args.empty() && !args[0].empty())
    {
        std::string idOrIndex = args[0];

        // Try as index first (numeric)
        bool isIndex = true;
        for (char c : idOrIndex)
        {
            if (!std::isdigit(c))
            {
                isIndex = false;
                break;
            }
        }

        if (isIndex)
        {
            // Parse as index (user provides 1-based, convert to 0-based for internal API)
            int userIndex = std::stoi(idOrIndex);
            if (userIndex < 1)
            {
                errorMessage =
                    "Invalid index " + idOrIndex + ". Index must be at least 1. Use 'list' to see available emulators.";
                return nullptr;
            }

            int internalIndex = userIndex - 1;  // Convert from 1-based to 0-based
            auto emulator = emulatorManager->GetEmulatorByIndex(internalIndex);
            if (emulator)
            {
                _emulator = emulator;
                return _emulator;
            }
            else
            {
                errorMessage = "No emulator found with index " + idOrIndex + ". Use 'list' to see available emulators.";
                return nullptr;
            }
        }
        else
        {
            // Try as UUID
            auto emulator = emulatorManager->GetEmulator(idOrIndex);
            if (emulator)
            {
                _emulator = emulator;
                return _emulator;
            }
            else
            {
                errorMessage = "No emulator found with ID '" + idOrIndex + "'. Use 'list' to see available emulators.";
                return nullptr;
            }
        }
    }

    // No argument provided - use stateless auto-selection logic
    return GetSelectedEmulator(session);
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

std::string CLIProcessor::FormatForTerminal(const std::string& text)
{
    std::string result;
    result.reserve(text.size() + text.size() / 10);  // Reserve extra space for \r characters

    for (size_t i = 0; i < text.size(); ++i)
    {
        char c = text[i];

        if (c == '\n')
        {
            // Check if this \n is already part of \r\n
            if (i > 0 && text[i - 1] == '\r')
            {
                // Already have \r\n, just add \n
                result += c;
            }
            else
            {
                // Convert standalone \n to \r\n
                result += NEWLINE;
            }
        }
        else if (c == '\r')
        {
            // Check if next char is \n
            if (i + 1 < text.size() && text[i + 1] == '\n')
            {
                // Already \r\n, keep \r
                result += c;
            }
            else
            {
                // Standalone \r, convert to \r\n
                result += NEWLINE;
            }
        }
        else
        {
            result += c;
        }
    }

    return result;
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
    oss << "  list          - List managed emulator instances" << NEWLINE;
    oss << "  select <id>   - Select an emulator" << NEWLINE;
    oss << "  start [model] - Start new emulator instance (default 48K or specified model)" << NEWLINE;
    oss << "  stop [id|index|all] - Stop emulator (single if only one running, or by ID/index/all)" << NEWLINE;
    oss << "  reset [id|index]    - Reset the emulator (auto-select if only one, or by ID/index)" << NEWLINE;
    oss << "  pause [id|index]    - Pause emulation (auto-select if only one, or by ID/index)" << NEWLINE;
    oss << "  resume [id|index]   - Resume emulation (auto-select if only one, or by ID/index)" << NEWLINE;
    oss << "  step          - Execute single CPU instruction" << NEWLINE;
    oss << "  stepin        - Execute single CPU instruction (alias for step)" << NEWLINE;
    oss << "  steps <count> - Execute 1 to N CPU instructions" << NEWLINE;
    oss << "  stepover      - Execute instruction, skip calls and subroutines" << NEWLINE;
    oss << "  memory <addr> - View memory at address" << NEWLINE;
    oss << "  registers     - Show CPU registers" << NEWLINE;
    oss << NEWLINE;
    oss << "Breakpoint commands:" << NEWLINE;
    oss << "  bp <addr>     - Set execution breakpoint at address" << NEWLINE;
    oss << "  wp <addr> <type> - Set memory watchpoint (r/w/rw)" << NEWLINE;
    oss << "  bport <port> <type> - Set port breakpoint (i/o/io)" << NEWLINE;
    oss << "  bplist        - List all breakpoints" << NEWLINE;
    oss << "  bpclear       - Clear breakpoints" << NEWLINE;
    oss << "  bpgroup <add|remove|list> <group> [bp_id] - Manage breakpoint groups" << NEWLINE;
    oss << "  bpon <all|group <name>|id <id>>        - Activate breakpoints" << NEWLINE;
    oss << "  bpoff <all|group <name>|id <id>>       - Deactivate breakpoints" << NEWLINE;
    oss << "  memory <hex address> [length]          - Dump memory contents" << NEWLINE;
    oss << "  debugmode <on|off>                     - Toggle debug memory mode (affects performance)" << NEWLINE;
    oss << NEWLINE;
    oss << "Feature toggles:" << NEWLINE;
    oss << "  feature                      - List all features and their states/modes" << NEWLINE;
    oss << "  feature <name> on|off        - Enable or disable a feature" << NEWLINE;
    oss << "  feature <name> mode <mode>   - Set mode for a feature" << NEWLINE;
    oss << "  feature save                 - Save current feature settings to features.ini" << NEWLINE;
    oss << NEWLINE;
    oss << "State Inspection:" << NEWLINE;
    oss << "  state screen                 - Show screen configuration (brief)" << NEWLINE;
    oss << "  state screen verbose         - Show screen configuration (detailed)" << NEWLINE;
    oss << "  state screen mode            - Show video mode details" << NEWLINE;
    oss << "  state screen flash           - Show flash state and counter" << NEWLINE;
    oss << NEWLINE;
    oss << "Emulator Settings:" << NEWLINE;
    oss << "  setting, setting list        - List all emulator settings and their values" << NEWLINE;
    oss << "  setting <name>               - Show current value of a specific setting" << NEWLINE;
    oss << "  setting <name> <value>       - Change a setting value" << NEWLINE;
    oss << "    Available settings:" << NEWLINE;
    oss << "      fast_tape on|off         - Enable/disable fast tape loading" << NEWLINE;
    oss << "      fast_disk on|off         - Enable/disable fast disk I/O" << NEWLINE;
    oss << NEWLINE;
    oss << "Memory Access Tracking:" << NEWLINE;
    oss << "  memcounters [all|reset] - Show memory access counters" << NEWLINE;
    oss << "  memcounters save [opts] - Save memory access data to file" << NEWLINE;
    oss << NEWLINE;
    oss << "Call Trace:" << NEWLINE;
    oss << "  calltrace [latest [N]] - Show latest N call trace events" << NEWLINE;
    oss << "  calltrace stats        - Show call trace buffer statistics" << NEWLINE;
    oss << "  calltrace save [file]  - Save call trace to file" << NEWLINE;
    oss << NEWLINE;
    oss << "BASIC Program Tools:" << NEWLINE;
    oss << "  basic                  - Show BASIC command help" << NEWLINE;
    oss << "  basic extract          - Extract BASIC program from memory" << NEWLINE;
    oss << NEWLINE;
    oss << "  open [file]   - Open a file or show file dialog" << NEWLINE;
    oss << "  exit, quit    - Exit the CLI" << NEWLINE;
    oss << NEWLINE;
    oss << "Type any command followed by -h or --help for more information.";

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

            std::string status;
            if (emulator->IsPaused())
            {
                status = "Paused";
            }
            else if (emulator->IsRunning())
            {
                status = "Running";
            }
            else
            {
                status = "Stopped";
            }

            response += std::string(NEWLINE) + "     Status: " + status;
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

    // Track the previous selection for the notification
    std::string previousId = session.GetSelectedEmulatorId();

    // We have a valid ID at this point
    const_cast<ClientSession&>(session).SetSelectedEmulatorId(selectedId);

    // Also update our local reference to the emulator
    _emulator = emulatorManager->GetEmulator(selectedId);

    // Send notification about selection change
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    EmulatorSelectionPayload* payload = new EmulatorSelectionPayload(previousId, selectedId);
    messageCenter.Post(NC_EMULATOR_SELECTION_CHANGED, payload);

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
    ss << "Goodbye!" << NEWLINE;
    session.SendResponse(ss.str());

    // Mark the session for closure - it will be closed after command processing
    const_cast<ClientSession&>(session).MarkForClosure();
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
    std::string errorMessage;
    auto emulator = ResolveEmulator(session, args, errorMessage);

    if (!emulator)
    {
        if (!errorMessage.empty())
        {
            session.SendResponse(errorMessage);
        }
        else
        {
            session.SendResponse("No emulator selected. Use 'select <id>' or 'list' to see available emulators.");
        }
        return;
    }

    emulator->Reset();
    session.SendResponse("Emulator reset\n");
}

void CLIProcessor::HandlePause(const ClientSession& session, const std::vector<std::string>& args)
{
    std::string errorMessage;
    auto emulator = ResolveEmulator(session, args, errorMessage);

    if (!emulator)
    {
        if (!errorMessage.empty())
        {
            session.SendResponse(errorMessage);
        }
        else
        {
            session.SendResponse("No emulator selected. Use 'select <id>' or 'list' to see available emulators.");
        }
        return;
    }

    // Check if the emulator is running
    if (!emulator->IsRunning())
    {
        session.SendResponse("Emulator is not running. Cannot pause.");
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
    std::string errorMessage;
    auto emulator = ResolveEmulator(session, args, errorMessage);

    if (!emulator)
    {
        if (!errorMessage.empty())
        {
            session.SendResponse(errorMessage);
        }
        else
        {
            session.SendResponse("No emulator selected. Use 'select <id>' or 'list' to see available emulators.");
        }
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

void CLIProcessor::HandleStepIn(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    // Check if the emulator is paused
    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.");
        return;
    }

    // Parse step count argument
    int stepCount = 1;  // stepin always executes one instruction
    // Note: stepin ignores any count parameter - it always steps one instruction
    if (!args.empty())
    {
        // For stepin, we ignore the count parameter and always execute one instruction
        // This makes stepin behavior consistent and predictable
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

    // Disassemble the instruction that's about to be executed
    uint8_t commandLen = 0;
    DecodedInstruction decodedBefore;
    std::vector<uint8_t> buffer(Z80Disassembler::MAX_INSTRUCTION_LENGTH);
    for (int i = 0; i < buffer.size(); i++)
    {
        buffer[i] = memory->DirectReadFromZ80Memory(initialPC + i);
    }
    std::string instructionBefore = disassembler->disassembleSingleCommandWithRuntime(buffer, initialPC, &commandLen,
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

    // Disassemble the next instruction to be executed
    for (int i = 0; i < Z80Disassembler::MAX_INSTRUCTION_LENGTH; i++)
    {
        buffer[i] = memory->DirectReadFromZ80Memory(newPC + i);
    }

    DecodedInstruction decodedAfter;
    commandLen = 0;
    std::string instructionAfter =
        disassembler->disassembleSingleCommandWithRuntime(buffer, newPC, &commandLen, z80State, memory, &decodedAfter);

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

void CLIProcessor::HandleStepOver(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    // Check if the emulator is paused
    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.");
        return;
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
        session.SendResponse("Error: Unable to access Z80 state.");
        return;
    }

    uint16_t initialPC = z80State->pc;

    // Disassemble the instruction that's about to be executed
    uint8_t commandLen = 0;
    DecodedInstruction decodedBefore;
    std::vector<uint8_t> buffer(Z80Disassembler::MAX_INSTRUCTION_LENGTH);
    for (int i = 0; i < buffer.size(); i++)
    {
        buffer[i] = memory->DirectReadFromZ80Memory(initialPC + i);
    }
    std::string instructionBefore = disassembler->disassembleSingleCommandWithRuntime(buffer, initialPC, &commandLen,
                                                                                      z80State, memory, &decodedBefore);

    // Execute the step-over operation
    emulator->StepOver();

    // Get the updated Z80 state
    z80State = emulator->GetZ80State();
    if (!z80State)
    {
        session.SendResponse("Error: Unable to access Z80 state after step-over.");
        return;
    }

    uint16_t newPC = z80State->pc;

    // Determine if this was a simple step or actual step-over
    bool wasStepOver = (newPC != initialPC + decodedBefore.fullCommandLen);
    std::string operationType = wasStepOver ? "Step-over" : "Step-in (instruction didn't require step-over)";

    // Add instruction type information
    std::string instructionType = "";
    if (decodedBefore.hasJump && !decodedBefore.hasRelativeJump)
    {
        if (decodedBefore.isRst)
            instructionType = " (RST instruction)";
        else if (decodedBefore.opcode.mnem && strstr(decodedBefore.opcode.mnem, "call"))
            instructionType = " (CALL instruction)";
        else
            instructionType = " (JUMP instruction)";
    }
    else if (decodedBefore.isDjnz)
    {
        instructionType = " (DJNZ instruction)";
    }
    else if (decodedBefore.isBlockOp)
    {
        instructionType = " (Block instruction)";
    }
    else if (decodedBefore.hasCondition)
    {
        instructionType = " (Conditional instruction)";
    }

    // Disassemble the next instruction to be executed
    for (int i = 0; i < Z80Disassembler::MAX_INSTRUCTION_LENGTH; i++)
    {
        buffer[i] = memory->DirectReadFromZ80Memory(newPC + i);
    }

    DecodedInstruction decodedAfter;
    commandLen = 0;
    std::string instructionAfter =
        disassembler->disassembleSingleCommandWithRuntime(buffer, newPC, &commandLen, z80State, memory, &decodedAfter);

    // Format response with CPU state information
    std::stringstream ss;
    ss << operationType << instructionType << " completed" << NEWLINE;

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

    // Show next instruction to be executed
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

    ss << instructionAfter << NEWLINE;

    // Show register state
    ss << NEWLINE << "Registers:" << NEWLINE;
    ss << "  PC: $" << std::setw(4) << z80State->pc << NEWLINE;
    ss << "  AF: $" << std::setw(4) << z80State->af << NEWLINE;
    ss << "  BC: $" << std::setw(4) << z80State->bc << NEWLINE;
    ss << "  DE: $" << std::setw(4) << z80State->de << NEWLINE;
    ss << "  HL: $" << std::setw(4) << z80State->hl << NEWLINE;
    ss << "  SP: $" << std::setw(4) << z80State->sp << NEWLINE;
    ss << "  IX: $" << std::setw(4) << z80State->ix << NEWLINE;
    ss << "  IY: $" << std::setw(4) << z80State->iy << NEWLINE;
    ss << "  Flags: ";
    ss << (z80State->f & 0x80 ? "S" : "-");
    ss << (z80State->f & 0x40 ? "Z" : "-");
    ss << (z80State->f & 0x20 ? "5" : "-");
    ss << (z80State->f & 0x10 ? "H" : "-");
    ss << (z80State->f & 0x08 ? "3" : "-");
    ss << (z80State->f & 0x04 ? "P" : "-");
    ss << (z80State->f & 0x02 ? "N" : "-");
    ss << (z80State->f & 0x01 ? "C" : "-");
    ss << NEWLINE;

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
    oss << "Memory at 0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << address << ":"
        << NEWLINE;

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

        oss << NEWLINE;
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
       << static_cast<int>(z80State->alt.a) << ", F': " << std::setw(2) << static_cast<int>(z80State->alt.f) << ")"
       << NEWLINE;

    ss << "  BC: " << std::setw(4) << z80State->bc << "  (B: " << std::setw(2) << static_cast<int>(z80State->b)
       << ", C: " << std::setw(2) << static_cast<int>(z80State->c) << ")";
    ss << "           BC': " << std::setw(4) << z80State->alt.bc << "  (B': " << std::setw(2)
       << static_cast<int>(z80State->alt.b) << ", C': " << std::setw(2) << static_cast<int>(z80State->alt.c) << ")"
       << NEWLINE;

    ss << "  DE: " << std::setw(4) << z80State->de << "  (D: " << std::setw(2) << static_cast<int>(z80State->d)
       << ", E: " << std::setw(2) << static_cast<int>(z80State->e) << ")";
    ss << "           DE': " << std::setw(4) << z80State->alt.de << "  (D': " << std::setw(2)
       << static_cast<int>(z80State->alt.d) << ", E': " << std::setw(2) << static_cast<int>(z80State->alt.e) << ")"
       << NEWLINE;

    ss << "  HL: " << std::setw(4) << z80State->hl << "  (H: " << std::setw(2) << static_cast<int>(z80State->h)
       << ", L: " << std::setw(2) << static_cast<int>(z80State->l) << ")";
    ss << "           HL': " << std::setw(4) << z80State->alt.hl << "  (H': " << std::setw(2)
       << static_cast<int>(z80State->alt.h) << ", L': " << std::setw(2) << static_cast<int>(z80State->alt.l) << ")"
       << NEWLINE;

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
       << static_cast<int>(z80State->i) << ", R: " << std::setw(2) << static_cast<int>(z80State->r_low) << ")"
       << NEWLINE;
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
        ss << "Usage: bp <address> [note]" << NEWLINE << "Sets an execution breakpoint at the specified address."
           << NEWLINE << "Examples:" << NEWLINE << "  bp 0x1234       - Set breakpoint at address 0x1234" << NEWLINE
           << "  bp $1234        - Set breakpoint at address $1234 (hex)" << NEWLINE
           << "  bp #1234        - Set breakpoint at address #1234 (hex)" << NEWLINE
           << "  bp 1234         - Set breakpoint at address 1234 (decimal)" << NEWLINE
           << "  bp 1234 Main loop - Set breakpoint with a note" << NEWLINE << "Use 'bplist' to view all breakpoints";
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
    std::string list = bpManager->GetBreakpointListAsString(NEWLINE);
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
        ss << "Usage: wp <address> <type> [note]" << NEWLINE << "Sets a memory watchpoint at the specified address."
           << NEWLINE << "Types:" << NEWLINE << "  r    - Watch for memory reads" << NEWLINE
           << "  w    - Watch for memory writes" << NEWLINE << "  rw   - Watch for both reads and writes" << NEWLINE
           << "Examples:" << NEWLINE << "  wp 0x1234 r     - Watch for reads at address 0x1234" << NEWLINE
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
        ss << "Usage: bport <port> <type> [note]" << NEWLINE << "Sets a port breakpoint at the specified port address."
           << NEWLINE << "Types:" << NEWLINE << "  i    - Watch for port IN operations" << NEWLINE
           << "  o    - Watch for port OUT operations" << NEWLINE << "  io   - Watch for both IN and OUT operations"
           << NEWLINE << "Examples:" << NEWLINE << "  bport 0x1234 i     - Watch for IN operations at port 0x1234"
           << NEWLINE << "  bport $FE o        - Watch for OUT operations at port $FE (hex)" << NEWLINE
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
        ss << "Usage: bpclear <option>" << NEWLINE << "Options:" << NEWLINE << "  all       - Clear all breakpoints"
           << NEWLINE << "  <id>      - Clear breakpoint with specific ID" << NEWLINE
           << "  addr <addr> - Clear breakpoint at specific address" << NEWLINE
           << "  port <port> - Clear breakpoint at specific port" << NEWLINE
           << "  mem       - Clear all memory breakpoints" << NEWLINE << "  port      - Clear all port breakpoints"
           << NEWLINE << "  read      - Clear all memory read breakpoints" << NEWLINE
           << "  write     - Clear all memory write breakpoints" << NEWLINE
           << "  exec      - Clear all execution breakpoints" << NEWLINE
           << "  in        - Clear all port IN breakpoints" << NEWLINE << "  out       - Clear all port OUT breakpoints"
           << NEWLINE << "  group <name> - Clear all breakpoints in a group";
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
        ss << "Usage: bpgroup <command> [parameters]" << NEWLINE << "Commands:" << NEWLINE
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
        ss << "Usage: bpon <option>" << NEWLINE << "Options:" << NEWLINE << "  all       - Activate all breakpoints"
           << NEWLINE << "  <id>      - Activate breakpoint with specific ID" << NEWLINE
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
        ss << "Usage: bpoff <option>" << NEWLINE << "Options:" << NEWLINE << "  all       - Deactivate all breakpoints"
           << NEWLINE << "  <id>      - Deactivate breakpoint with specific ID" << NEWLINE
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

void CLIProcessor::HandleDebugMode(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("Error: No emulator selected" + std::string(NEWLINE));
        return;
    }

    if (args.size() < 1)
    {
        // Show current mode
        bool isDebugMode = emulator->GetContext()->pCore->GetZ80()->isDebugMode;
        std::string mode = isDebugMode ? "on" : "off";
        session.SendResponse("Debug mode is currently " + mode + NEWLINE);
        session.SendResponse("Usage: debugmode <on|off>" + std::string(NEWLINE));
        return;
    }

    const std::string& mode = args[0];
    Core* core = emulator->GetContext()->pCore;
    bool success = true;
    std::string response;

    if (mode == "on")
    {
        core->UseDebugMemoryInterface();
        core->GetZ80()->isDebugMode = true;
        response = "Debug mode enabled (slower, with breakpoint support)" + std::string(NEWLINE);
    }
    else if (mode == "off")
    {
        core->UseFastMemoryInterface();
        core->GetZ80()->isDebugMode = false;
        response = "Debug mode disabled (faster, no breakpoints)" + std::string(NEWLINE);
    }
    else
    {
        success = false;
        response = "Error: Invalid parameter. Use 'on' or 'off'" + std::string(NEWLINE);
    }

    session.SendResponse(response);
    if (success)
    {
        // Also show the current mode after changing it
        bool isDebugMode = emulator->GetContext()->pCore->GetZ80()->isDebugMode;
        std::string currentMode = isDebugMode ? "on" : "off";
        session.SendResponse("Debug mode is now " + currentMode + NEWLINE);
    }
}

void CLIProcessor::HandleMemCounters(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("Error: No emulator selected" + std::string(NEWLINE));
        return;
    }

    // Check for save command first
    if (!args.empty() && args[0] == "save")
    {
        std::string outputPath = "";
        bool singleFile = false;
        std::vector<std::string> filterPages;

        // Parse options
        for (size_t i = 1; i < args.size(); i++)
        {
            if (args[i] == "--single-file" || args[i] == "-s")
            {
                singleFile = true;
            }
            else if (args[i] == "--output" || args[i] == "-o")
            {
                if (i + 1 < args.size())
                {
                    outputPath = args[++i];
                }
                else
                {
                    session.SendResponse("Error: Missing output path" + std::string(NEWLINE));
                    return;
                }
            }
            else if (args[i] == "--page" || args[i] == "-p")
            {
                if (i + 1 < args.size())
                {
                    filterPages.push_back(args[++i]);
                }
                else
                {
                    session.SendResponse("Error: Missing page specification" + std::string(NEWLINE));
                    return;
                }
            }
        }

        // Set the subfolder name
        if (!singleFile)
        {
            outputPath = "memory_logs";
        }

        // Get the memory access tracker
        auto* memory = emulator->GetContext()->pMemory;
        auto& tracker = memory->GetAccessTracker();

        std::string savedPath = tracker.SaveAccessData(outputPath, "yaml", singleFile, filterPages);
        if (!savedPath.empty())
        {
            session.SendResponse("Memory access data saved successfully to " + savedPath + NEWLINE);
        }
        else
        {
            session.SendResponse("Failed to save memory access data" + std::string(NEWLINE));
        }
        return;
    }

    // Parse command line arguments
    bool showAll = false;
    bool resetAfter = false;

    for (const auto& arg : args)
    {
        if (arg == "all")
            showAll = true;
        else if (arg == "reset")
            resetAfter = true;
    }

    // Get the memory access tracker
    auto* memory = emulator->GetContext()->pMemory;
    auto& tracker = memory->GetAccessTracker();

    // Get the current counters by summing up all banks
    uint64_t totalReads = 0;
    uint64_t totalWrites = 0;
    uint64_t totalExecutes = 0;

    // Get per-Z80 bank (4 banks of 16KB each)
    uint64_t bankReads[4] = {0};
    uint64_t bankWrites[4] = {0};
    uint64_t bankExecutes[4] = {0};

    for (int bank = 0; bank < 4; bank++)
    {
        bankReads[bank] = tracker.GetZ80BankReadAccessCount(bank);
        bankWrites[bank] = tracker.GetZ80BankWriteAccessCount(bank);
        bankExecutes[bank] = tracker.GetZ80BankExecuteAccessCount(bank);

        totalReads += bankReads[bank];
        totalWrites += bankWrites[bank];
        totalExecutes += bankExecutes[bank];
    }

    uint64_t totalAccesses = totalReads + totalWrites + totalExecutes;

    // Format the output
    std::stringstream ss;
    ss << "Memory Access Counters" << NEWLINE;
    ss << "=====================" << NEWLINE;
    ss << "Total Reads:    " << StringHelper::Format("%'llu", totalReads) << NEWLINE;
    ss << "Total Writes:   " << StringHelper::Format("%'llu", totalWrites) << NEWLINE;
    ss << "Total Executes: " << StringHelper::Format("%'llu", totalExecutes) << NEWLINE;
    ss << "Total Accesses: " << StringHelper::Format("%'llu", totalAccesses) << NEWLINE << NEWLINE;

    // Always show Z80 memory page (bank) counters with physical page mapping
    ss << "Z80 Memory Banks (16KB each):" << NEWLINE;
    ss << "----------------------------" << NEWLINE;

    const char* bankNames[4] = {"0x0000-0x3FFF", "0x4000-0x7FFF", "0x8000-0xBFFF", "0xC000-0xFFFF"};

    // Process each bank
    for (int bank = 0; bank < 4; bank++)
    {
        uint64_t bankTotal = bankReads[bank] + bankWrites[bank] + bankExecutes[bank];

        // Get bank info using helper methods
        bool isROM = (bank < 2) ? (bank == 0 ? memory->IsBank0ROM() : memory->GetMemoryBankMode(bank) == BANK_ROM)
                                : false;  // Banks 2-3 are always RAM

        uint16_t page = memory->GetPageForBank(bank);
        const char* type = isROM ? "ROM" : "RAM";
        std::string bankName = memory->GetCurrentBankName(bank);

        // Format the output
        ss << StringHelper::Format("Bank %d (%s) -> %s page: %s", bank, bankNames[bank], type, bankName.c_str())
           << NEWLINE;
        ss << StringHelper::Format("  Reads:    %'llu", bankReads[bank]) << NEWLINE;
        ss << StringHelper::Format("  Writes:   %'llu", bankWrites[bank]) << NEWLINE;
        ss << StringHelper::Format("  Executes: %'llu", bankExecutes[bank]) << NEWLINE;
        ss << StringHelper::Format("  Total:    %'llu", bankTotal) << NEWLINE << NEWLINE;
    }

    // Show all physical pages if requested
    if (showAll)
    {
        ss << "Physical Memory Pages with Activity:" << NEWLINE;
        ss << "-----------------------------------" << NEWLINE;

        bool foundActivity = false;

        // Check RAM pages (0-255)
        for (uint16_t page = 0; page < MAX_RAM_PAGES; page++)
        {
            uint32_t reads = tracker.GetPageReadAccessCount(page);
            uint32_t writes = tracker.GetPageWriteAccessCount(page);
            uint32_t executes = tracker.GetPageExecuteAccessCount(page);

            if (reads > 0 || writes > 0 || executes > 0)
            {
                foundActivity = true;
                ss << StringHelper::Format("RAM Page %d:", page) << NEWLINE;
                ss << StringHelper::Format("  Reads:    %'u", reads) << NEWLINE;
                ss << StringHelper::Format("  Writes:   %'u", writes) << NEWLINE;
                ss << StringHelper::Format("  Executes: %'u", executes) << NEWLINE;
                ss << StringHelper::Format("  Total:    %'u", reads + writes + executes) << NEWLINE << NEWLINE;
            }
        }

        // Check ROM pages (start after RAM, cache, and misc pages)
        const uint16_t FIRST_ROM_PAGE = MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES;
        for (uint16_t page = 0; page < MAX_ROM_PAGES; page++)
        {
            uint16_t physicalPage = FIRST_ROM_PAGE + page;
            uint32_t reads = tracker.GetPageReadAccessCount(physicalPage);
            uint32_t writes = tracker.GetPageWriteAccessCount(physicalPage);
            uint32_t executes = tracker.GetPageExecuteAccessCount(physicalPage);

            if (reads > 0 || writes > 0 || executes > 0)
            {
                foundActivity = true;
                ss << StringHelper::Format("ROM Page %d:", page) << NEWLINE;
                ss << StringHelper::Format("  Reads:    %'u", reads) << NEWLINE;
                ss << StringHelper::Format("  Writes:   %'u", writes) << NEWLINE;
                ss << StringHelper::Format("  Executes: %'u", executes) << NEWLINE;
                ss << StringHelper::Format("  Total:    %'u", reads + writes + executes) << NEWLINE << NEWLINE;
            }
        }

        if (!foundActivity)
        {
            ss << "No memory access activity detected in any physical page." << NEWLINE;
        }
    }

    // Show usage if no arguments provided
    if (args.empty())
    {
        ss << "Usage: memcounters [all] [reset] | save [options]" << NEWLINE;
        ss << "  all   - Show all physical pages with activity" << NEWLINE;
        ss << "  reset - Reset counters after displaying" << NEWLINE;
        ss << "  save  - Save memory access data to files" << NEWLINE;
        ss << "    Options:" << NEWLINE;
        ss << "      --single-file, -s     Save to single file" << NEWLINE;
        ss << "      --output <path>, -o   Output path (default: memory_logs)" << NEWLINE;
        ss << "      --page <name>, -p     Filter specific pages (e.g., 'RAM 0', 'ROM 2')" << NEWLINE;
    }

    // Send the response
    session.SendResponse(ss.str());

    // Reset counters if requested
    if (resetAfter)
    {
        tracker.ResetCounters();
        session.SendResponse("Memory counters have been reset." + std::string(NEWLINE));
    }
}

void CLIProcessor::HandleCallTrace(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("Error: No emulator selected" + std::string(NEWLINE));
        return;
    }
    auto* memory = emulator->GetMemory();
    if (!memory)
    {
        session.SendResponse("Error: Memory not available" + std::string(NEWLINE));
        return;
    }
    auto& tracker = memory->GetAccessTracker();
    auto* callTrace = tracker.GetCallTraceBuffer();
    if (!callTrace)
    {
        session.SendResponse("Error: Call trace buffer not available" + std::string(NEWLINE));
        return;
    }
    if (args.empty() || args[0] == "help")
    {
        std::ostringstream oss;
        oss << "calltrace latest [N]   - Show latest N control flow events (default 10)" << NEWLINE;
        oss << "calltrace save <file> - Save full call trace history to file (binary)" << NEWLINE;
        oss << "calltrace reset       - Reset call trace buffer" << NEWLINE;
        oss << "calltrace help        - Show this help message" << NEWLINE;
        session.SendResponse(oss.str());
        return;
    }
    if (args[0] == "latest")
    {
        size_t count = 10;
        if (args.size() > 1)
        {
            try
            {
                count = std::stoul(args[1]);
            }
            catch (...)
            {
            }
        }
        auto events = callTrace->GetLatestCold(count);
        auto hotEvents = callTrace->GetLatestHot(count);
        std::ostringstream oss;
        if (!events.empty())
        {
            oss << "Latest " << events.size() << " cold control flow events:" << NEWLINE;
            oss << "Idx   m1_pc   type    target    flags   sp      opcodes        bank0    bank1    bank2    bank3    "
                   "stack_top         loop_count"
                << NEWLINE;
            for (size_t i = 0; i < events.size(); ++i)
            {
                const auto& ev = events[i];
                oss << StringHelper::Format("%4d   %04X   ", (int)i, ev.m1_pc);

                // type
                const char* typenames[] = {"JP", "JR", "CALL", "RST", "RET", "RETI", "DJNZ"};
                oss << StringHelper::Format("%-6s   ", typenames[static_cast<int>(ev.type)]);
                oss << StringHelper::Format("%04X     ", ev.target_addr);
                oss << StringHelper::Format("%02X      ", (int)ev.flags);
                oss << StringHelper::Format("%04X    ", ev.sp);
                // opcodes
                for (auto b : ev.opcode_bytes)
                    oss << StringHelper::Format("%02X ", (int)b);
                oss << std::string(12 - ev.opcode_bytes.size() * 3, ' ');
                oss << "   ";

                // banks
                for (int b = 0; b < 4; ++b)
                {
                    oss << StringHelper::Format("%s%-2d    ", ev.banks[b].is_rom ? "ROM" : "RAM",
                                                (int)ev.banks[b].page_num);
                }

                // stack top
                for (int s = 0; s < 3; ++s)
                {
                    if (ev.stack_top[s])
                        oss << StringHelper::Format("%04X ", ev.stack_top[s]);
                    else
                        oss << "     ";
                }

                oss << std::string(18 - 5 * 3, ' ');
                oss << StringHelper::Format("   %u", ev.loop_count);
                oss << NEWLINE;
            }
            oss << NEWLINE;
        }
        if (!hotEvents.empty())
        {
            oss << "Latest " << hotEvents.size() << " hot control flow events:" << NEWLINE;
            oss << "Idx   m1_pc   type    target    flags   sp      opcodes        bank0    bank1    bank2    bank3    "
                   "stack_top         loop_count   last_seen_frame"
                << NEWLINE;
            for (size_t i = 0; i < hotEvents.size(); ++i)
            {
                const auto& hot = hotEvents[i];
                const auto& ev = hot.event;
                oss << StringHelper::Format("%4d   %04X   ", (int)i, ev.m1_pc);
                // type
                const char* typenames[] = {"JP", "JR", "CALL", "RST", "RET", "RETI", "DJNZ"};
                oss << StringHelper::Format("%-6s ", typenames[static_cast<int>(ev.type)]);
                oss << StringHelper::Format("%04X     ", ev.target_addr);
                oss << StringHelper::Format("%02X     ", (int)ev.flags);
                oss << StringHelper::Format("%04X    ", ev.sp);

                // opcodes
                for (auto b : ev.opcode_bytes)
                    oss << StringHelper::Format("%02X ", (int)b);
                oss << std::string(12 - ev.opcode_bytes.size() * 3, ' ');
                oss << "   ";
                // banks
                for (int b = 0; b < 4; ++b)
                {
                    oss << StringHelper::Format("%s%-2d    ", ev.banks[b].is_rom ? "ROM" : "RAM",
                                                (int)ev.banks[b].page_num);
                }
                // stack top
                for (int s = 0; s < 3; ++s)
                {
                    if (ev.stack_top[s])
                        oss << StringHelper::Format("%04X ", ev.stack_top[s]);
                    else
                        oss << "     ";
                }
                oss << std::string(18 - 5 * 3, ' ');
                oss << StringHelper::Format("   %u   %llu", hot.loop_count, hot.last_seen_frame);
                oss << NEWLINE;
            }
            oss << NEWLINE;
        }
        session.SendResponse(oss.str());
        return;
    }
    if (args[0] == "save")
    {
        // Generate a unique filename with timestamp if not provided
        std::string filename;
        if (args.size() > 1)
        {
            filename = args[1];
        }
        else
        {
            auto now = std::chrono::system_clock::now();
            auto in_time_t = std::chrono::system_clock::to_time_t(now);
            std::stringstream ss;
            ss << "calltrace_" << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S") << ".yaml";
            filename = ss.str();
        }

        // Use CallTraceBuffer's SaveToFile method
        if (!callTrace->SaveToFile(filename))
        {
            session.SendResponse("Failed to create call trace file: " + filename + NEWLINE);
            return;
        }
        session.SendResponse("Call trace saved to " + filename + NEWLINE);
        return;
    }
    if (args[0] == "reset")
    {
        callTrace->Reset();
        session.SendResponse("Call trace buffer reset." + std::string(NEWLINE));
        return;
    }
    if (args[0] == "stats")
    {
        size_t cold_count = callTrace->ColdSize();
        size_t cold_capacity = callTrace->ColdCapacity();
        size_t hot_count = callTrace->HotSize();
        size_t hot_capacity = callTrace->HotCapacity();
        size_t cold_bytes = cold_count * sizeof(Z80ControlFlowEvent);
        size_t hot_bytes = hot_count * sizeof(HotEvent);
        auto format_bytes = [](size_t bytes) -> std::string {
            char buf[32];
            if (bytes >= 1024 * 1024)
                snprintf(buf, sizeof(buf), "%.2f MB", bytes / 1024.0 / 1024.0);
            else if (bytes >= 1024)
                snprintf(buf, sizeof(buf), "%.2f KB", bytes / 1024.0);
            else
                snprintf(buf, sizeof(buf), "%zu B", bytes);
            return buf;
        };

        std::ostringstream oss;
        oss << "CallTraceBuffer stats:" << NEWLINE;
        oss << "  Cold buffer: " << cold_count << " / " << cold_capacity << "  (" << format_bytes(cold_bytes) << ")"
            << NEWLINE;
        oss << "  Hot buffer:  " << hot_count << " / " << hot_capacity << "  (" << format_bytes(hot_bytes) << ")"
            << NEWLINE;

        // Add was_hot and top 5 loop_count info
        auto allCold = callTrace->GetAll();
        size_t was_hot_count = 0;
        std::vector<uint32_t> loop_counts;
        for (const auto& ev : allCold)
        {
            if (ev.was_hot)
                was_hot_count++;
            loop_counts.push_back(ev.loop_count);
        }

        std::sort(loop_counts.begin(), loop_counts.end(), std::greater<uint32_t>());
        oss << "  Cold buffer: " << was_hot_count << " events were previously hot (was_hot=true)" << NEWLINE;
        oss << "  Top 5 loop_count values in cold buffer: ";
        for (size_t i = 0; i < std::min<size_t>(5, loop_counts.size()); ++i)
        {
            oss << loop_counts[i];
            if (i + 1 < std::min<size_t>(5, loop_counts.size()))
                oss << ", ";
        }
        oss << NEWLINE;

        session.SendResponse(oss.str());
        return;
    }
    session.SendResponse("Unknown calltrace command. Use 'calltrace help' for usage." + std::string(NEWLINE));
}

void CLIProcessor::HandleFeature(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    auto* featureManager = emulator->GetFeatureManager();
    if (!featureManager)
    {
        session.SendResponse("FeatureManager not available for this emulator.");
        return;
    }

    std::ostringstream out;

    if (!args.empty() && args[0] == "save")
    {
        featureManager->saveToFile("features.ini");
        out << "Feature settings saved to features.ini." << NEWLINE;
        session.SendResponse(out.str());
        return;
    }

    // Feature command logic (moved from FeatureManager)
    if (args.empty() || (args.size() == 1 && args[0].empty()))
    {
        // Print all features in a table
        const int name_width = 15;
        const int state_width = 7;
        const int mode_width = 10;
        const int desc_width = 60;
        const std::string separator =
            "----------------------------------------------------------------------------------------------------------"
            "--------";

        out << separator << NEWLINE;
        out << "| " << std::left << std::setw(name_width) << "Name"
            << "| " << std::left << std::setw(state_width) << "State"
            << "| " << std::left << std::setw(mode_width) << "Mode"
            << "| " << std::left << "Description" << NEWLINE;
        out << separator << NEWLINE;

        for (const auto& f : featureManager->listFeatures())
        {
            std::string state_str = f.enabled ? Features::kStateOn : Features::kStateOff;
            std::string mode_str = f.mode.empty() ? "" : f.mode;

            out << "| " << std::left << std::setw(name_width) << f.id << "| " << std::left << std::setw(state_width)
                << state_str << "| " << std::left << std::setw(mode_width) << mode_str << "| " << std::left
                << f.description << NEWLINE;
        }
        out << separator << NEWLINE;

        session.SendResponse(out.str());
        return;
    }
    else if (args.size() == 2)
    {
        const std::string& featureName = args[0];
        const std::string& action = args[1];

        if (action == Features::kStateOn)
        {
            if (featureManager->setFeature(featureName, true))
            {
                out << "Feature '" << featureName << "' enabled." << NEWLINE;
                session.SendResponse(out.str());
                return;
            }
            else
            {
                out << "Error: Unknown feature '" << featureName << "'." << NEWLINE;
                out << "Available features:" << NEWLINE;
                for (const auto& f : featureManager->listFeatures())
                {
                    out << "  " << f.id;
                    if (!f.alias.empty())
                    {
                        out << " (alias: " << f.alias << ")";
                    }
                    out << NEWLINE;
                }
            }
        }
        else if (action == Features::kStateOff)
        {
            if (featureManager->setFeature(featureName, false))
            {
                out << "Feature '" << featureName << "' disabled." << NEWLINE;
                session.SendResponse(out.str());
                return;
            }
            else
            {
                out << "Error: Unknown feature '" << featureName << "'." << NEWLINE;
                out << "Available features:" << NEWLINE;
                for (const auto& f : featureManager->listFeatures())
                {
                    out << "  " << f.id;
                    if (!f.alias.empty())
                    {
                        out << " (alias: " << f.alias << ")";
                    }
                    out << NEWLINE;
                }
            }
        }
        else
        {
            out << "Invalid action. Use 'on' or 'off'." << NEWLINE;
        }
    }
    else if (args.size() == 3 && args[1] == "mode")
    {
        std::string feature = args[0];
        std::string mode = args[2];
        if (featureManager->setMode(feature, mode))
        {
            out << "Feature '" << feature << "' mode set to '" << mode << "'" << NEWLINE;
            session.SendResponse(out.str());
            return;
        }
        else
        {
            out << "Error: Unknown feature '" << feature << "'." << NEWLINE;
            out << "Available features:" << NEWLINE;
            for (const auto& f : featureManager->listFeatures())
            {
                out << "  " << f.id;
                if (!f.alias.empty())
                {
                    out << " (alias: " << f.alias << ")";
                }
                out << NEWLINE;
            }
        }
    }

    // Usage/help output - only shown for errors or invalid commands
    out << "Usage:" << NEWLINE << "  feature <feature> on|off" << NEWLINE << "  feature <feature> mode <mode>"
        << NEWLINE << "  feature save" << NEWLINE << "  feature" << NEWLINE;
    session.SendResponse(out.str());
}

void CLIProcessor::HandleSteps(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    // Check if the emulator is paused
    if (!emulator->IsPaused())
    {
        session.SendResponse("Emulator must be paused before stepping. Use 'pause' command first.");
        return;
    }

    // Parse step count argument
    int stepCount = 1;
    if (args.empty())
    {
        session.SendResponse("Usage: steps <count> - Execute 1 to N CPU instructions");
        return;
    }

    try
    {
        stepCount = std::stoi(args[0]);
        if (stepCount < 1)
        {
            session.SendResponse("Error: Step count must be at least 1");
            return;
        }
        if (stepCount > 1000)
        {
            session.SendResponse("Error: Step count cannot exceed 1000");
            return;
        }
    }
    catch (...)
    {
        session.SendResponse("Error: Invalid step count. Must be a number between 1 and 1000");
        return;
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
        session.SendResponse("Error: Unable to access Z80 state.");
        return;
    }

    // Store the current PC to show what's about to be executed
    uint16_t initialPC = z80State->pc;

    // Disassemble the instruction that's about to be executed
    uint8_t commandLen = 0;
    DecodedInstruction decodedBefore;
    std::vector<uint8_t> buffer(Z80Disassembler::MAX_INSTRUCTION_LENGTH);
    for (int i = 0; i < buffer.size(); i++)
    {
        buffer[i] = memory->DirectReadFromZ80Memory(initialPC + i);
    }
    std::string instructionBefore = disassembler->disassembleSingleCommandWithRuntime(buffer, initialPC, &commandLen,
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

    // Disassemble the next instruction to be executed
    for (int i = 0; i < Z80Disassembler::MAX_INSTRUCTION_LENGTH; i++)
    {
        buffer[i] = memory->DirectReadFromZ80Memory(newPC + i);
    }

    DecodedInstruction decodedAfter;
    commandLen = 0;
    std::string instructionAfter =
        disassembler->disassembleSingleCommandWithRuntime(buffer, newPC, &commandLen, z80State, memory, &decodedAfter);

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

void CLIProcessor::HandleSetting(const ClientSession& session, const std::vector<std::string>& args)
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

    CONFIG& config = context->config;

    // If no arguments, show all settings (list)
    if (args.empty())
    {
        std::stringstream ss;
        ss << "Current Settings:" << NEWLINE;
        ss << "==================" << NEWLINE;
        ss << NEWLINE;

        ss << "I/O Acceleration:" << NEWLINE;
        ss << "  fast_tape     = " << (config.tape_traps ? "on" : "off") << "  (Fast tape loading)" << NEWLINE;
        ss << "  fast_disk     = " << (config.wd93_nodelay ? "on" : "off") << "  (Fast disk I/O - no WD1793 delays)"
           << NEWLINE;
        ss << NEWLINE;

        ss << "Disk Interface:" << NEWLINE;
        ss << "  trdos_present = " << (config.trdos_present ? "on" : "off") << "  (TR-DOS Beta Disk interface)"
           << NEWLINE;
        ss << "  trdos_traps   = " << (config.trdos_traps ? "on" : "off") << "  (TR-DOS traps)" << NEWLINE;
        ss << NEWLINE;

        ss << "Performance & Speed:" << NEWLINE;
        ss << "  speed         = ";
        if (config.turbo_mode)
            ss << "unlimited";
        else
            ss << (int)config.speed_multiplier << "x";
        ss << "  (CPU speed multiplier: 1, 2, 4, 8, 16, unlimited)" << NEWLINE;
        ss << "  turbo_audio   = " << (config.turbo_mode_audio ? "on" : "off") << "  (Enable audio in turbo mode)"
           << NEWLINE;
        ss << NEWLINE;

        ss << "Use: setting <name> <value>  to change a setting" << NEWLINE;
        ss << "Example: setting fast_tape on" << NEWLINE;

        session.SendResponse(ss.str());
        return;
    }

    // Get setting name
    std::string settingName = args[0];
    std::transform(settingName.begin(), settingName.end(), settingName.begin(), ::tolower);

    // Handle special commands
    if (settingName == "list")
    {
        // Redirect to show all settings (same as no arguments)
        std::stringstream ss;
        ss << "Current Settings:" << NEWLINE;
        ss << "==================" << NEWLINE;
        ss << NEWLINE;

        ss << "I/O Acceleration:" << NEWLINE;
        ss << "  fast_tape     = " << (config.tape_traps ? "on" : "off") << "  (Fast tape loading)" << NEWLINE;
        ss << "  fast_disk     = " << (config.wd93_nodelay ? "on" : "off") << "  (Fast disk I/O - no WD1793 delays)"
           << NEWLINE;
        ss << NEWLINE;

        ss << "Disk Interface:" << NEWLINE;
        ss << "  trdos_present = " << (config.trdos_present ? "on" : "off") << "  (TR-DOS Beta Disk interface)"
           << NEWLINE;
        ss << "  trdos_traps   = " << (config.trdos_traps ? "on" : "off") << "  (TR-DOS traps)" << NEWLINE;
        ss << NEWLINE;

        ss << "Performance & Speed:" << NEWLINE;
        ss << "  speed         = ";
        if (config.turbo_mode)
            ss << "unlimited";
        else
            ss << (int)config.speed_multiplier << "x";
        ss << "  (CPU speed multiplier: 1, 2, 4, 8, 16, unlimited)" << NEWLINE;
        ss << "  turbo_audio   = " << (config.turbo_mode_audio ? "on" : "off") << "  (Enable audio in turbo mode)"
           << NEWLINE;
        ss << NEWLINE;

        ss << "Use: setting <name> <value>  to change a setting" << NEWLINE;
        ss << "Example: setting fast_tape on" << NEWLINE;

        session.SendResponse(ss.str());
        return;
    }

    // If only setting name provided, show current value
    if (args.size() == 1)
    {
        std::stringstream ss;

        if (settingName == "fast_tape")
        {
            ss << "fast_tape = " << (config.tape_traps ? "on" : "off") << NEWLINE;
            ss << "Description: Fast tape loading (bypasses audio emulation)" << NEWLINE;
        }
        else if (settingName == "fast_disk")
        {
            ss << "fast_disk = " << (config.wd93_nodelay ? "on" : "off") << NEWLINE;
            ss << "Description: Fast disk I/O (removes WD1793 controller delays)" << NEWLINE;
        }
        else if (settingName == "trdos_present")
        {
            ss << "trdos_present = " << (config.trdos_present ? "on" : "off") << NEWLINE;
            ss << "Description: Enable Beta128 TR-DOS disk interface" << NEWLINE;
        }
        else if (settingName == "trdos_traps")
        {
            ss << "trdos_traps = " << (config.trdos_traps ? "on" : "off") << NEWLINE;
            ss << "Description: Use TR-DOS traps for faster disk operations" << NEWLINE;
        }
        else if (settingName == "speed" || settingName == "max_cpu_speed")
        {
            ss << "speed = ";
            if (config.turbo_mode)
                ss << "unlimited" << NEWLINE;
            else
                ss << (int)config.speed_multiplier << "x" << NEWLINE;
            ss << "Description: Maximum CPU speed multiplier (1, 2, 4, 8, 16, unlimited)" << NEWLINE;
        }
        else if (settingName == "turbo_audio")
        {
            ss << "turbo_audio = " << (config.turbo_mode_audio ? "on" : "off") << NEWLINE;
            ss << "Description: Enable audio generation in turbo mode (high pitch)" << NEWLINE;
        }
        else
        {
            ss << "Error: Unknown setting '" << settingName << "'" << NEWLINE;
            ss << "Use 'setting' to see all available settings" << NEWLINE;
        }

        session.SendResponse(ss.str());
        return;
    }

    // Setting name and value provided - change the setting
    std::string value = args[1];
    std::string valueLower = value;
    std::transform(valueLower.begin(), valueLower.end(), valueLower.begin(), ::tolower);

    // Apply the setting
    std::stringstream ss;

    // Handle non-boolean settings first
    if (settingName == "speed" || settingName == "max_cpu_speed")
    {
        if (valueLower == "unlimited" || valueLower == "max")
        {
            emulator->EnableTurboMode(config.turbo_mode_audio);
            ss << "Setting changed: speed = unlimited (Turbo Mode)" << NEWLINE;
        }
        else
        {
            try
            {
                int m = std::stoi(valueLower);
                if (m == 1 || m == 2 || m == 4 || m == 8 || m == 16)
                {
                    emulator->DisableTurboMode();
                    emulator->SetSpeedMultiplier(m);
                    ss << "Setting changed: speed = " << m << "x" << NEWLINE;
                }
                else
                {
                    ss << "Error: Invalid speed multiplier " << m << ". Use 1, 2, 4, 8, 16, or unlimited" << NEWLINE;
                }
            }
            catch (...)
            {
                ss << "Error: Invalid value '" << value << "'. Use 1, 2, 4, 8, 16, or unlimited" << NEWLINE;
            }
        }
        session.SendResponse(ss.str());
        return;
    }

    // Parse boolean value for remaining settings
    bool boolValue = false;
    if (valueLower == "on" || valueLower == "1" || valueLower == "true" || valueLower == "yes")
    {
        boolValue = true;
    }
    else if (valueLower == "off" || valueLower == "0" || valueLower == "false" || valueLower == "no")
    {
        boolValue = false;
    }
    else
    {
        session.SendResponse(std::string("Error: Invalid value '") + value +
                             "'. Use: on/off, true/false, 1/0, or yes/no" + NEWLINE);
        return;
    }

    if (settingName == "fast_tape")
    {
        config.tape_traps = boolValue ? 1 : 0;
        ss << "Setting changed: fast_tape = " << (boolValue ? "on" : "off") << NEWLINE;
        ss << "Fast tape loading is now " << (boolValue ? "enabled" : "disabled") << NEWLINE;
    }
    else if (settingName == "fast_disk")
    {
        config.wd93_nodelay = boolValue;
        ss << "Setting changed: fast_disk = " << (boolValue ? "on" : "off") << NEWLINE;
        ss << "Fast disk I/O is now " << (boolValue ? "enabled" : "disabled") << NEWLINE;
    }
    else if (settingName == "trdos_present")
    {
        config.trdos_present = boolValue;
        ss << "Setting changed: trdos_present = " << (boolValue ? "on" : "off") << NEWLINE;
        ss << "TR-DOS interface is now " << (boolValue ? "enabled" : "disabled") << NEWLINE;
        ss << "Note: Restart emulator for this change to take effect" << NEWLINE;
    }
    else if (settingName == "trdos_traps")
    {
        config.trdos_traps = boolValue;
        ss << "Setting changed: trdos_traps = " << (boolValue ? "on" : "off") << NEWLINE;
        ss << "TR-DOS traps are now " << (boolValue ? "enabled" : "disabled") << NEWLINE;
    }
    else if (settingName == "turbo_audio")
    {
        config.turbo_mode_audio = boolValue;
        if (config.turbo_mode)
        {
            // Re-enable turbo with/without audio to apply immediately
            emulator->EnableTurboMode(boolValue);
        }
        ss << "Setting changed: turbo_audio = " << (boolValue ? "on" : "off") << NEWLINE;
        ss << "Audio in turbo mode is now " << (boolValue ? "enabled" : "disabled") << NEWLINE;
    }
    else
    {
        ss << "Error: Unknown setting '" << settingName << "'" << NEWLINE;
        ss << "Use 'setting' to see all available settings" << NEWLINE;
    }

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStart(const ClientSession& session, const std::vector<std::string>& args)
{
    if (!args.empty())
    {
        // start <model> - create emulator with specific model
        std::string modelName = args[0];

        // Create emulator with the specified model
        auto* emulatorManager = EmulatorManager::GetInstance();
        auto emulator = emulatorManager->CreateEmulatorWithModel("", modelName);

        if (emulator)
        {
            // Start the emulator
            bool startSuccess = emulatorManager->StartEmulatorAsync(emulator->GetId());

            // Auto-select only if this is the first emulator, otherwise keep current selection
            auto emulatorIds = emulatorManager->GetEmulatorIds();
            bool shouldAutoSelect = (emulatorIds.size() == 1);  // Only auto-select if this is the only emulator

            std::stringstream ss;
            if (startSuccess)
            {
                ss << "Started emulator instance: " << emulator->GetId() << NEWLINE;
                ss << "Model: " << modelName << NEWLINE;

                if (shouldAutoSelect)
                {
                    const_cast<ClientSession&>(session).SetSelectedEmulatorId(emulator->GetId());
                    ss << "Auto-selected as current emulator" << NEWLINE;
                }
            }
            else
            {
                ss << "Created emulator instance: " << emulator->GetId() << NEWLINE;
                ss << "Model: " << modelName << NEWLINE;
                ss << "Warning: Failed to start emulator automatically" << NEWLINE;

                if (shouldAutoSelect)
                {
                    const_cast<ClientSession&>(session).SetSelectedEmulatorId(emulator->GetId());
                    ss << "Auto-selected as current emulator" << NEWLINE;
                }
            }

            // Note: NC_EMULATOR_INSTANCE_CREATED notification is now automatically sent by EmulatorManager

            session.SendResponse(ss.str());
        }
        else
        {
            std::stringstream ss;
            ss << "Error: Failed to create emulator with model '" << modelName << "'" << NEWLINE;
            ss << "Use 'start' without arguments for default 48K, or specify a valid model name" << NEWLINE;
            ss << "Available models: ";

            // List available models
            auto models = emulatorManager->GetAvailableModels();
            for (size_t i = 0; i < models.size(); ++i)
            {
                if (i > 0)
                    ss << ", ";
                ss << models[i].ShortName;
            }
            ss << NEWLINE;

            session.SendResponse(ss.str());
        }
    }
    else
    {
        // start - create default emulator
        auto emulator = EmulatorManager::GetInstance()->CreateEmulator("", LoggerLevel::LogInfo);

        if (emulator)
        {
            // Start the emulator
            bool startSuccess = EmulatorManager::GetInstance()->StartEmulatorAsync(emulator->GetId());

            // Auto-select only if this is the first emulator, otherwise keep current selection
            auto* emulatorManager = EmulatorManager::GetInstance();
            auto emulatorIds = emulatorManager->GetEmulatorIds();
            bool shouldAutoSelect = (emulatorIds.size() == 1);  // Only auto-select if this is the only emulator

            std::stringstream ss;
            if (startSuccess)
            {
                ss << "Started emulator instance: " << emulator->GetId() << NEWLINE;
                ss << "Model: 48K (default)" << NEWLINE;

                if (shouldAutoSelect)
                {
                    const_cast<ClientSession&>(session).SetSelectedEmulatorId(emulator->GetId());
                    ss << "Auto-selected as current emulator" << NEWLINE;
                }
            }
            else
            {
                ss << "Created emulator instance: " << emulator->GetId() << NEWLINE;
                ss << "Model: 48K (default)" << NEWLINE;
                ss << "Warning: Failed to start emulator automatically" << NEWLINE;

                if (shouldAutoSelect)
                {
                    const_cast<ClientSession&>(session).SetSelectedEmulatorId(emulator->GetId());
                    ss << "Auto-selected as current emulator" << NEWLINE;
                }
            }

            // Send notification about instance creation
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            SimpleTextPayload* payload = new SimpleTextPayload(emulator->GetId());
            messageCenter.Post(NC_EMULATOR_INSTANCE_CREATED, payload);

            session.SendResponse(ss.str());
        }
        else
        {
            session.SendResponse("Error: Failed to create default emulator instance" + std::string(NEWLINE));
        }
    }
}

void CLIProcessor::HandleStop(const ClientSession& session, const std::vector<std::string>& args)
{
    auto* emulatorManager = EmulatorManager::GetInstance();
    auto emulatorIds = emulatorManager->GetEmulatorIds();

    if (args.empty())
    {
        // If no arguments provided, check if there's exactly one emulator
        if (emulatorIds.size() == 1)
        {
            // Stop the single emulator directly
            std::string actualId = emulatorIds[0];

            if (emulatorManager->StopEmulator(actualId))
            {
                emulatorManager->RemoveEmulator(actualId);
                std::stringstream ss;
                ss << "Stopped emulator instance: " << actualId << NEWLINE;

                // Clear selection if it was pointing to the stopped emulator
                // Check both the session's selected ID and our local _emulator reference (same logic as list command)
                bool wasSelected =
                    (session.GetSelectedEmulatorId() == actualId) ||
                    (_emulator && _emulator->GetId() == actualId && session.GetSelectedEmulatorId().empty());

                if (wasSelected)
                {
                    const_cast<ClientSession&>(session).SetSelectedEmulatorId("");
                    _emulator.reset();

                    // Auto-select the first remaining emulator (by creation time)
                    auto remainingIds = emulatorManager->GetEmulatorIds();
                    if (!remainingIds.empty())
                    {
                        const_cast<ClientSession&>(session).SetSelectedEmulatorId(remainingIds[0]);
                        ss << "Auto-selected first emulator: " << remainingIds[0] << NEWLINE;
                    }
                    else
                    {
                        ss << "Cleared emulator selection" << NEWLINE;
                    }
                }

                session.SendResponse(ss.str());
            }
            else
            {
                session.SendResponse("Error: Emulator instance '" + actualId + "' not found or could not be stopped" +
                                     std::string(NEWLINE));
            }
            return;
        }
        else if (emulatorIds.empty())
        {
            session.SendResponse("No emulators running." + std::string(NEWLINE));
            return;
        }
        else
        {
            session.SendResponse(
                "Usage: stop <emulator-id> | stop all | stop (stops single emulator if only one is running)" +
                std::string(NEWLINE));
            return;
        }
    }

    std::string targetId = args[0];

    if (targetId == "all")
    {
        // Stop all emulators
        auto* emulatorManager = EmulatorManager::GetInstance();
        auto emulatorIds = emulatorManager->GetEmulatorIds();
        size_t stoppedCount = 0;

        for (const auto& id : emulatorIds)
        {
            if (emulatorManager->StopEmulator(id))
            {
                // Remove from manager after stopping
                // Note: NC_EMULATOR_INSTANCE_DESTROYED notification is now automatically sent by EmulatorManager
                emulatorManager->RemoveEmulator(id);

                stoppedCount++;
            }
        }

        std::stringstream ss;
        ss << "Stopped " << stoppedCount << " emulator instance(s)" << NEWLINE;

        // Clear selection if it was pointing to a stopped emulator
        std::string currentSelected = session.GetSelectedEmulatorId();
        if (currentSelected != "none" &&
            std::find(emulatorIds.begin(), emulatorIds.end(), currentSelected) != emulatorIds.end())
        {
            const_cast<ClientSession&>(session).SetSelectedEmulatorId("none");
            // Also clear our cached emulator reference
            _emulator.reset();
            ss << "Cleared emulator selection" << NEWLINE;
        }

        session.SendResponse(ss.str());
    }
    else
    {
        // Check if targetId is a number (index)
        bool isIndex = true;
        int index = -1;
        try
        {
            index = std::stoi(targetId);
            if (index < 1)
                isIndex = false;  // Indices start from 1
        }
        catch (const std::exception&)
        {
            isIndex = false;
        }

        std::string actualId = targetId;

        if (isIndex)
        {
            // Convert index to emulator ID
            auto* emulatorManager = EmulatorManager::GetInstance();
            auto emulatorIds = emulatorManager->GetEmulatorIds();

            if (index > 0 && static_cast<size_t>(index) <= emulatorIds.size())
            {
                actualId = emulatorIds[index - 1];  // Convert to 0-based index
            }
            else
            {
                std::stringstream ss;
                ss << "Error: Invalid index '" << index << "'. Valid range: 1-" << emulatorIds.size() << NEWLINE;
                ss << "Use 'list' to see available instances" << NEWLINE;
                session.SendResponse(ss.str());
                return;
            }
        }

        // Stop specific emulator
        auto* emulatorManager = EmulatorManager::GetInstance();
        if (emulatorManager->StopEmulator(actualId))
        {
            // Remove from manager after stopping
            // Note: NC_EMULATOR_INSTANCE_DESTROYED notification is now automatically sent by EmulatorManager
            emulatorManager->RemoveEmulator(actualId);

            std::stringstream ss;
            ss << "Stopped emulator instance: " << actualId << NEWLINE;

            // Clear selection if it was pointing to this emulator
            // Check both the session's selected ID and our local _emulator reference (same logic as list command)
            bool wasSelected = (session.GetSelectedEmulatorId() == actualId) ||
                               (_emulator && _emulator->GetId() == actualId && session.GetSelectedEmulatorId().empty());

            if (wasSelected)
            {
                const_cast<ClientSession&>(session).SetSelectedEmulatorId("");
                // Also clear our cached emulator reference
                _emulator.reset();

                // Auto-select the first remaining emulator (by creation time)
                auto remainingIds = emulatorManager->GetEmulatorIds();
                if (!remainingIds.empty())
                {
                    const_cast<ClientSession&>(session).SetSelectedEmulatorId(remainingIds[0]);

                    // Send notification about selection change
                    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
                    EmulatorSelectionPayload* payload = new EmulatorSelectionPayload(actualId, remainingIds[0]);
                    messageCenter.Post(NC_EMULATOR_SELECTION_CHANGED, payload);

                    ss << "Auto-selected first emulator: " << remainingIds[0] << NEWLINE;
                }
                else
                {
                    ss << "Cleared emulator selection" << NEWLINE;
                }
            }

            session.SendResponse(ss.str());
        }
        else
        {
            std::stringstream ss;
            if (isIndex)
            {
                ss << "Error: Could not stop emulator at index " << index << NEWLINE;
            }
            else
            {
                ss << "Error: Emulator instance '" << actualId << "' not found or could not be stopped" << NEWLINE;
            }
            ss << "Use 'list' to see available instances" << NEWLINE;
            session.SendResponse(ss.str());
        }
    }
}
