#include "cli-processor.h"

#include <3rdparty/message-center/eventqueue.h>
#include <3rdparty/message-center/messagecenter.h>
#include <debugger/analyzers/basic-lang/basicextractor.h>
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

/// region <ClientSession>

// ClientSession implementation
void ClientSession::SendResponse(const std::string& message) const
{
    if (_clientSocket != INVALID_SOCKET)
    {
        send(_clientSocket, message.c_str(), static_cast<int>(message.length()), 0);
    }
}

std::string ClientSession::FormatForTerminal(const std::string& text)
{
    std::ostringstream result;
    
    for (size_t i = 0; i < text.length(); ++i)
    {
        char c = text[i];
        
        if (c == '\r')
        {
            // Handle CRLF (\r\n) - consume both and emit single NEWLINE
            if (i + 1 < text.length() && text[i + 1] == '\n')
            {
                ++i; // Skip the \n
            }
            result << CLIProcessor::NEWLINE;
        }
        else if (c == '\n')
        {
            // Handle LFCR (\n\r) - consume both and emit single NEWLINE
            if (i + 1 < text.length() && text[i + 1] == '\r')
            {
                ++i; // Skip the \r
            }
            result << CLIProcessor::NEWLINE;
        }
        else
        {
            result << c;
        }
    }
    
    return result.str();
}

/// endregion </ClientSession>

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
                        {"run_tstates", &CLIProcessor::HandleRunTStates},        // Run N t-states
                        {"run_to_scanline", &CLIProcessor::HandleRunToScanline}, // Run to scanline N
                        {"run_scanlines", &CLIProcessor::HandleRunNScanlines},   // Run N scanlines
                        {"run_to_pixel", &CLIProcessor::HandleRunToPixel},       // Run to next screen pixel
                        {"run_to_interrupt", &CLIProcessor::HandleRunToInterrupt}, // Run to interrupt
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
                        {"disasm", &CLIProcessor::HandleDisasm},
                        {"disasm_page", &CLIProcessor::HandleDisasmPage},
                        {"u", &CLIProcessor::HandleDisasm},  // Shortcut like debug monitors

                        // BASIC commands
                        {"basic", &CLIProcessor::HandleBasic},

                        // Analyzer commands
                        {"analyzer", &CLIProcessor::HandleAnalyzer},

                        // Profiler commands
                        {"profiler", &CLIProcessor::HandleProfiler},

                        // Settings commands
                        {"setting", &CLIProcessor::HandleSetting},
                        {"settings", &CLIProcessor::HandleSetting},
                        {"set", &CLIProcessor::HandleSetting},

                        // State inspection commands
                        {"state", &CLIProcessor::HandleState},

                        // Instance management commands
                        {"start", &CLIProcessor::HandleStart},
                        {"create", &CLIProcessor::HandleCreate},
                        {"stop", &CLIProcessor::HandleStop},
                        {"remove", &CLIProcessor::HandleStop},  // Alias for stop (stop also removes the instance)
                        {"models", &CLIProcessor::HandleModels},

                        // Tape control commands
                        {"tape", &CLIProcessor::HandleTape},

                        // Disk control commands
                        {"disk", &CLIProcessor::HandleDisk},


                        // Memory aliases
                        {"mem", &CLIProcessor::HandleMemory},
                        {"m", &CLIProcessor::HandleMemory},
                        {"regs", &CLIProcessor::HandleRegisters},
                        {"r", &CLIProcessor::HandleRegisters},

                        // Snapshot control commands
                        {"snapshot", &CLIProcessor::HandleSnapshot},

                        // Interpreter control commands
                        {"python", &CLIProcessor::HandlePython},
                        {"py", &CLIProcessor::HandlePython},  // Alias
                        {"lua", &CLIProcessor::HandleLua},

                        // Capture commands (OCR, screen, ROM text)
                        {"capture", &CLIProcessor::HandleCapture},

                        // Keyboard injection commands
                        {"key", &CLIProcessor::HandleKey},
                        {"keyboard", &CLIProcessor::HandleKey}};
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
    if (!_emulator)
    {
        auto* emulatorManager = EmulatorManager::GetInstance();
        if (emulatorManager)
        {
            auto emulatorIds = emulatorManager->GetEmulatorIds();
            auto selectedId = emulatorManager->GetSelectedEmulatorId();

            // Auto-select only if there's exactly one emulator and none is globally selected (stateless)
            if (emulatorIds.size() == 1 && selectedId.empty())
            {
                _emulator = emulatorManager->GetEmulator(emulatorIds[0]);
                // Note: We don't change global selection for stateless CLI behavior
            }
            // If there's already a global selection, use it
            else if (!selectedId.empty())
            {
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
    auto* emulatorManager = EmulatorManager::GetInstance();
    if (!emulatorManager)
    {
        return nullptr;
    }

    // Get the globally selected emulator ID
    std::string selectedId = emulatorManager->GetSelectedEmulatorId();

    // If a specific emulator is globally selected, try to use it
    if (!selectedId.empty())
    {
        auto emulator = emulatorManager->GetEmulator(selectedId);
        if (emulator)
        {
            _emulator = emulator;
            return _emulator;
        }

        // Selected emulator no longer exists, clear the global selection
        emulatorManager->SetSelectedEmulatorId("");
    }

    // No global selection or selected emulator is gone - auto-select if only one emulator exists
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
    oss << "  remove        - Alias for stop (stops and removes instance)" << NEWLINE;
    oss << "  models        - List available ZX Spectrum models" << NEWLINE;
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
    oss << "Disassembly:" << NEWLINE;
    oss << "  disasm [addr] [count]   - Disassemble at address (or, u for short)" << NEWLINE;
    oss << "  disasm_page <ram|rom> <page> [offset] [count] - Disassemble from physical page" << NEWLINE;
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
    oss << "Analyzer Commands:" << NEWLINE;
    oss << "  analyzer list          - List all registered analyzers" << NEWLINE;
    oss << "  analyzer enable <name> - Activate an analyzer" << NEWLINE;
    oss << "  analyzer disable <name>- Deactivate an analyzer" << NEWLINE;
    oss << "  analyzer status [name] - Show analyzer status" << NEWLINE;
    oss << "  analyzer <name> events - Get captured events" << NEWLINE;
    oss << NEWLINE;
    oss << "Disk Inspection:" << NEWLINE;
    oss << "  disk list              - List all disk drives and status" << NEWLINE;
    oss << "  disk sector <drv> <cyl> <side> <sec> - Read sector data" << NEWLINE;
    oss << "  disk track <drv> <cyl> <side>        - Read track summary" << NEWLINE;
    oss << "  disk sysinfo <drv>     - Show TR-DOS system info (sector 9)" << NEWLINE;
    oss << "  disk catalog <drv>     - Show TR-DOS file catalog" << NEWLINE;
    oss << NEWLINE;
    oss << "Snapshot Commands:" << NEWLINE;
    oss << "  snapshot load <file>           - Load snapshot (.sna, .z80)" << NEWLINE;
    oss << "  snapshot save <file> [--force] - Save snapshot (.sna)" << NEWLINE;
    oss << "  snapshot info                  - Show current snapshot status" << NEWLINE;
    oss << NEWLINE;
    oss << "Capture Commands:" << NEWLINE;
    oss << "  capture ocr                    - OCR text from screen (ROM font)" << NEWLINE;
    oss << "  capture romtext                - Capture ROM print output (TODO)" << NEWLINE;
    oss << "  capture screen [5|7|shadow]    - Capture screen bitmap (TODO)" << NEWLINE;
    oss << NEWLINE;
    oss << "Keyboard Injection:" << NEWLINE;
    oss << "  key tap <key>                  - Tap a key (press and release)" << NEWLINE;
    oss << "  key press <key>                - Press and hold a key" << NEWLINE;
    oss << "  key release <key>              - Release a held key" << NEWLINE;
    oss << "  key combo <key1> <key2>...     - Tap multiple keys simultaneously" << NEWLINE;
    oss << "  key macro <name>               - Execute predefined macro (e_mode, format, cat, etc.)" << NEWLINE;
    oss << "  key type <text>                - Type text with auto modifier handling" << NEWLINE;
    oss << "  key list                       - List all recognized key names" << NEWLINE;
    oss << "  key clear                      - Release all keys" << NEWLINE;
    oss << NEWLINE;
    oss << "  open [file]   - Open a file or show file dialog" << NEWLINE;
    oss << "  exit, quit    - Exit the CLI" << NEWLINE;
    oss << NEWLINE;
    oss << "Type any command followed by -h or --help for more information.";

    session.SendResponse(oss.str());
}
