#include "cli-processor.h"
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/platform.h>
#include <debugger/breakpoints/breakpointmanager.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

/// region <State Inspection Commands>

void CLIProcessor::HandleState(const ClientSession& session, const std::vector<std::string>& args)
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

    // If no arguments, show usage
    if (args.empty())
    {
        std::stringstream ss;
        ss << "Usage: state <subsystem> [subcommand] [args]" << NEWLINE;
        ss << NEWLINE;
        ss << "Available subsystems:" << NEWLINE;
        ss << "  memory         - Memory configuration (ROM + RAM + paging)" << NEWLINE;
        ss << "  memory ram     - RAM bank mapping (alias: ram)" << NEWLINE;
        ss << "  memory rom     - ROM configuration (alias: rom)" << NEWLINE;
        ss << "  screen         - Screen configuration (brief)" << NEWLINE;
        ss << "  screen verbose - Screen configuration (detailed)" << NEWLINE;
        ss << "  screen mode    - Detailed video mode information" << NEWLINE;
        ss << "  screen flash   - Flash state and counter" << NEWLINE;
        ss << NEWLINE;
        ss << "Examples:" << NEWLINE;
        ss << "  state memory         - Show complete memory configuration" << NEWLINE;
        ss << "  state memory ram     - Show RAM banking only" << NEWLINE;
        ss << "  state ram            - Same as above (alias)" << NEWLINE;
        ss << "  state rom            - Show ROM configuration only" << NEWLINE;
        ss << "  state screen         - Show screen configuration (brief)" << NEWLINE;
        ss << "  state screen verbose - Show screen configuration (detailed)" << NEWLINE;
        ss << "  state screen mode    - Show video mode details" << NEWLINE;
        ss << "  state screen flash   - Show flash state" << NEWLINE;
        
        session.SendResponse(ss.str());
        return;
    }

    std::string subsystem = args[0];
    std::transform(subsystem.begin(), subsystem.end(), subsystem.begin(), ::tolower);

    // Handle 'memory' subsystem or aliases
    if (subsystem == "memory" || subsystem == "ram" || subsystem == "rom")
    {
        // For aliases, convert to memory subsystem with appropriate subcommand
        if (subsystem == "ram")
        {
            HandleStateMemoryRAM(session, context);
            return;
        }
        else if (subsystem == "rom")
        {
            HandleStateMemoryROM(session, context);
            return;
        }
        
        // Check for subcommands
        if (args.size() > 1)
        {
            std::string subcommand = args[1];
            std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);
            
            if (subcommand == "ram")
            {
                HandleStateMemoryRAM(session, context);
                return;
            }
            else if (subcommand == "rom")
            {
                HandleStateMemoryROM(session, context);
                return;
            }
            else
            {
                session.SendResponse(std::string("Error: Unknown subcommand '") + args[1] + "'" + NEWLINE +
                                    "Available: ram, rom" + NEWLINE);
                return;
            }
        }
        
        // No subcommand - show complete memory state
        HandleStateMemory(session, context);
        return;
    }
    // Handle 'screen' subsystem
    else if (subsystem == "screen")
    {
        // Check for subcommands
        if (args.size() > 1)
        {
            std::string subcommand = args[1];
            std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);
            
            if (subcommand == "mode")
            {
                HandleStateScreenMode(session, context);
                return;
            }
            else if (subcommand == "flash")
            {
                HandleStateScreenFlash(session, context);
                return;
            }
            else if (subcommand == "verbose")
            {
                // Show verbose screen information
                HandleStateScreenVerbose(session, context);
                return;
            }
            else
            {
                session.SendResponse(std::string("Error: Unknown subcommand '") + args[1] + "'" + NEWLINE +
                                    "Available: mode, flash, verbose" + NEWLINE);
                return;
            }
        }
        
        // No subcommand - show brief screen state
        HandleStateScreen(session, context);
        return;
    }
    else
    {
        session.SendResponse(std::string("Error: Unknown subsystem '") + subsystem + "'" + NEWLINE +
                            "Available subsystems: memory, ram, rom, screen" + NEWLINE);
        return;
    }
}

void CLIProcessor::HandleStateScreen(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    CONFIG& config = context->config;
    EmulatorState& state = context->emulatorState;
    
    ss << "Screen Configuration (Brief)" << NEWLINE;
    ss << "============================" << NEWLINE;
    ss << NEWLINE;
    
    // Determine model
    std::string model = "ZX Spectrum 48K";
    if (config.mem_model == MM_SPECTRUM128)
        model = "ZX Spectrum 128K";
    else if (config.mem_model == MM_PENTAGON)
        model = "Pentagon 128K";
    else if (config.mem_model == MM_PLUS3)
        model = "ZX Spectrum +3";
    
    ss << "Model:        " << model << NEWLINE;
    ss << "Video Mode:   Standard (256×192, 2 colors per 8×8 block)" << NEWLINE;
    
    bool is128K = (config.mem_model == MM_SPECTRUM128 || 
                   config.mem_model == MM_PENTAGON || 
                   config.mem_model == MM_PLUS3);
    
    if (is128K)
    {
        uint8_t port7FFD = state.p7FFD;
        bool shadowScreen = (port7FFD & 0x08) != 0;
        
        ss << "Active Screen: Screen " << (shadowScreen ? "1" : "0") 
           << " (RAM page " << (shadowScreen ? "7" : "5") << ")" << NEWLINE;
    }
    else
    {
        ss << "Active Screen: Single screen (RAM page 5)" << NEWLINE;
    }
    
    ss << "Border Color: " << (int)(context->pScreen->GetBorderColor()) << NEWLINE;
    ss << NEWLINE;
    ss << "Use 'state screen verbose' for detailed information" << NEWLINE;
    
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateScreenVerbose(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    CONFIG& config = context->config;
    EmulatorState& state = context->emulatorState;
    
    ss << "Screen Configuration (Verbose)" << NEWLINE;
    ss << "==============================" << NEWLINE;
    ss << NEWLINE;
    
    // Determine model
    bool is128K = (config.mem_model == MM_SPECTRUM128 || 
                   config.mem_model == MM_PENTAGON || 
                   config.mem_model == MM_PLUS3);
    
    if (is128K)
    {
        // 128K model - show both screens
        uint8_t port7FFD = state.p7FFD;
        bool shadowScreen = (port7FFD & 0x08) != 0;  // Bit 3
        
        ss << "Model: ZX Spectrum 128K" << NEWLINE;
        ss << "Active Screen: Screen " << (shadowScreen ? "1 (shadow)" : "0 (normal)") << NEWLINE;
        ss << NEWLINE;
        
        ss << "Screen 0 (Normal - RAM Page 5):" << NEWLINE;
        ss << "  Physical Location: RAM page 5, offset 0x0000-0x1FFF" << NEWLINE;
        ss << "  Pixel Data:        Page 5 offset 0x0000-0x17FF (6144 bytes)" << NEWLINE;
        ss << "  Attributes:        Page 5 offset 0x1800-0x1AFF (768 bytes)" << NEWLINE;
        ss << "  Z80 Access:        0x4000-0x7FFF (bank 1 - always accessible)" << NEWLINE;
        ss << "  ULA Status:        " << (shadowScreen ? "Not displayed" : "CURRENTLY DISPLAYED") << NEWLINE;
        ss << "  Contention:        Active when accessed via 0x4000-0x7FFF" << NEWLINE;
        ss << NEWLINE;
        
        ss << "Screen 1 (Shadow - RAM Page 7):" << NEWLINE;
        ss << "  Physical Location: RAM page 7, offset 0x0000-0x1FFF" << NEWLINE;
        ss << "  Pixel Data:        Page 7 offset 0x0000-0x17FF (6144 bytes)" << NEWLINE;
        ss << "  Attributes:        Page 7 offset 0x1800-0x1AFF (768 bytes)" << NEWLINE;
        
        uint8_t ramBank = port7FFD & 0x07;  // Bits 0-2
        if (ramBank == 7)
        {
            ss << "  Z80 Access:        0xC000-0xFFFF (bank 3, page 7 is mapped)" << NEWLINE;
        }
        else
        {
            ss << "  Z80 Access:        Not currently mapped (page " << (int)ramBank << " at bank 3)" << NEWLINE;
        }
        ss << "  ULA Status:        " << (shadowScreen ? "CURRENTLY DISPLAYED" : "Not displayed") << NEWLINE;
        ss << "  Contention:        " << (ramBank == 7 ? "Inactive (not in contended range)" : "N/A (not mapped)") << NEWLINE;
        ss << NEWLINE;
        
        ss << "Port 0x7FFD:  0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) 
           << (int)port7FFD << std::dec << " (bin: ";
        for (int i = 7; i >= 0; i--)
            ss << ((port7FFD >> i) & 1);
        ss << ")" << NEWLINE;
        ss << "  Bits 0-2: " << (int)ramBank << " (RAM page " << (int)ramBank << " mapped to bank 3)" << NEWLINE;
        ss << "  Bit 3:    " << (shadowScreen ? "1" : "0") << " (ULA displays Screen " << (shadowScreen ? "1" : "0") << ")" << NEWLINE;
        ss << "  Bit 4:    " << ((port7FFD & 0x10) ? "1" : "0") << " (ROM: " << ((port7FFD & 0x10) ? "48K BASIC" : "128K Editor") << ")" << NEWLINE;
        ss << "  Bit 5:    " << ((port7FFD & 0x20) ? "1" : "0") << " (Paging " << ((port7FFD & 0x20) ? "LOCKED" : "enabled") << ")" << NEWLINE;
        ss << NEWLINE;
        
        ss << "Note: ULA reads screen from physical RAM page, independent of Z80 address mapping." << NEWLINE;
    }
    else
    {
        // 48K model - single screen
        ss << "Model: ZX Spectrum 48K" << NEWLINE;
        ss << "Screen: Single screen at 0x4000-0x7FFF" << NEWLINE;
        ss << NEWLINE;
        
        ss << "Physical Location: RAM page 5, offset 0x0000-0x1FFF" << NEWLINE;
        ss << "Pixel Data:        0x4000-0x57FF (6144 bytes)" << NEWLINE;
        ss << "Attributes:        0x5800-0x5AFF (768 bytes)" << NEWLINE;
        ss << "Z80 Access:        0x4000-0x7FFF (always accessible)" << NEWLINE;
        ss << "Contention:        Active during display period" << NEWLINE;
    }
    
    // Display mode (simplified for now)
    ss << NEWLINE;
    ss << "Display Mode: Standard (256×192, 2 colors per 8×8)" << NEWLINE;
    ss << "Border Color: " << (int)(context->pScreen->GetBorderColor()) << NEWLINE;
    
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateScreenMode(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    CONFIG& config = context->config;
    
    ss << "Video Mode Information" << NEWLINE;
    ss << "======================" << NEWLINE;
    ss << NEWLINE;
    
    // Determine model
    std::string model = "ZX Spectrum 48K";
    if (config.mem_model == MM_SPECTRUM128)
        model = "ZX Spectrum 128K";
    else if (config.mem_model == MM_PENTAGON)
        model = "Pentagon 128K";
    else if (config.mem_model == MM_PLUS3)
        model = "ZX Spectrum +3";
    
    ss << "Model: " << model << NEWLINE;
    ss << "Video Mode: Standard" << NEWLINE;
    ss << "============================================" << NEWLINE;
    ss << "Resolution:      256 × 192 pixels" << NEWLINE;
    ss << "Color Depth:     2 colors per attribute block" << NEWLINE;
    ss << "Attribute Size:  8 × 8 pixels" << NEWLINE;
    ss << "Memory Layout:" << NEWLINE;
    ss << "  Pixel Data:    6144 bytes (32 lines × 192 pixels)" << NEWLINE;
    ss << "  Attributes:    768 bytes (32 × 24 blocks)" << NEWLINE;
    ss << "  Total:         6912 bytes per screen" << NEWLINE;
    
    if (config.mem_model == MM_SPECTRUM128 || 
        config.mem_model == MM_PENTAGON || 
        config.mem_model == MM_PLUS3)
    {
        uint8_t port7FFD = context->emulatorState.p7FFD;
        bool shadowScreen = (port7FFD & 0x08) != 0;
        ss << "Active Screen:   Screen " << (shadowScreen ? "1" : "0") << " (RAM page " << (shadowScreen ? "7" : "5") << ")" << NEWLINE;
    }
    
    ss << "Compatibility:   48K/128K/+2/+2A/+3 standard" << NEWLINE;
    ss << NEWLINE;
    ss << "Note: Enhanced modes (Timex, Pentagon GigaScreen, etc.) not currently active." << NEWLINE;
    
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateScreenFlash(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    EmulatorState& state = context->emulatorState;
    
    ss << "Screen Flash State" << NEWLINE;
    ss << "==================" << NEWLINE;
    ss << NEWLINE;
    
    // Flash toggles every 16 frames (32 frames for full cycle)
    // Frame counter is at 0x5C78 (FRAMES system variable)
    uint8_t flashCounter = (state.frame_counter / 16) & 1;
    uint8_t framesUntilToggle = 16 - (state.frame_counter % 16);
    
    ss << "Flash Phase:         " << (flashCounter ? "Inverted" : "Normal") << NEWLINE;
    ss << "Frames Until Toggle: " << (int)framesUntilToggle << " frames" << NEWLINE;
    ss << "Flash Cycle:         " << (state.frame_counter % 32) << " / 32 frames" << NEWLINE;
    ss << NEWLINE;
    ss << "Note: Flash toggles every 16 frames (0.32 seconds at 50Hz)" << NEWLINE;
    ss << "      Full flash cycle is 32 frames (0.64 seconds)" << NEWLINE;
    
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateMemory(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    CONFIG& config = context->config;
    Memory& memory = *context->pMemory;
    EmulatorState& state = context->emulatorState;
    
    ss << "Memory Configuration" << NEWLINE;
    ss << "====================" << NEWLINE;
    ss << NEWLINE;
    
    // Determine model
    std::string model = "ZX Spectrum 48K";
    if (config.mem_model == MM_SPECTRUM128)
        model = "ZX Spectrum 128K";
    else if (config.mem_model == MM_PENTAGON)
        model = "Pentagon 128K";
    else if (config.mem_model == MM_PLUS3)
        model = "ZX Spectrum +3";
    
    ss << "Model: " << model << NEWLINE;
    ss << NEWLINE;
    
    // ROM Configuration
    ss << "ROM Configuration:" << NEWLINE;
    ss << "  Active ROM Page:  " << (int)memory.GetROMPage() << NEWLINE;
    
    // Determine ROM mode
    std::string romMode = "Unknown";
    if (config.mem_model == MM_SPECTRUM48)
        romMode = "48K BASIC";
    else if (config.mem_model == MM_SPECTRUM128)
        romMode = (memory.GetROMPage() == 0) ? "128K Editor" : "48K BASIC";
    else if (config.mem_model == MM_PENTAGON)
        romMode = (memory.GetROMPage() == 2) ? "128K Editor" : (memory.GetROMPage() == 3 ? "48K BASIC" : "Service/TR-DOS");
    else if (config.mem_model == MM_PLUS3)
        romMode = (memory.GetROMPage() == 0) ? "128K Editor" : "48K BASIC";
        
    ss << "  ROM Mode:         " << romMode << NEWLINE;
    ss << "  Bank 0 (0x0000-0x3FFF): " << memory.GetCurrentBankName(0) << NEWLINE;
    ss << NEWLINE;
    
    // RAM Configuration
    ss << "RAM Configuration:" << NEWLINE;
    ss << "  Bank 1 (0x4000-0x7FFF): " << memory.GetCurrentBankName(1) << NEWLINE;
    ss << "  Bank 2 (0x8000-0xBFFF): " << memory.GetCurrentBankName(2) << NEWLINE;
    ss << "  Bank 3 (0xC000-0xFFFF): " << memory.GetCurrentBankName(3) << NEWLINE;
    ss << NEWLINE;
    
    // Paging State
    if (config.mem_model != MM_SPECTRUM48)
    {
        ss << "Paging State:" << NEWLINE;
        ss << "  Port 0x7FFD:      0x" << std::hex << std::setw(2) << std::setfill('0') 
           << (int)state.p7FFD << std::dec << NEWLINE;
        ss << "  RAM Bank 3:       " << (int)(state.p7FFD & 0x07) << NEWLINE;
        ss << "  Screen:           " << ((state.p7FFD & 0x08) ? "1 (Shadow)" : "0 (Normal)") << NEWLINE;
        ss << "  ROM Select:       " << ((state.p7FFD & 0x10) ? "1" : "0") << NEWLINE;
        ss << "  Paging Locked:    " << ((state.p7FFD & 0x20) ? "YES" : "NO") << NEWLINE;
    }
    
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateMemoryRAM(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    CONFIG& config = context->config;
    Memory& memory = *context->pMemory;
    EmulatorState& state = context->emulatorState;
    
    ss << "RAM Bank Mapping" << NEWLINE;
    ss << "================" << NEWLINE;
    ss << NEWLINE;
    
    // Determine model
    std::string model = "ZX Spectrum 48K";
    if (config.mem_model == MM_SPECTRUM128)
        model = "ZX Spectrum 128K";
    else if (config.mem_model == MM_PENTAGON)
        model = "Pentagon 128K";
    else if (config.mem_model == MM_PLUS3)
        model = "ZX Spectrum +3";
    
    ss << "Model: " << model << NEWLINE;
    ss << NEWLINE;
    
    // Show detailed Z80 address space to RAM page mapping
    ss << "Z80 Address Space → Physical RAM Pages:" << NEWLINE;
    ss << "=========================================" << NEWLINE;
    ss << NEWLINE;
    
    // Bank 0 (might be ROM)
    if (memory.IsBank0ROM())
    {
        ss << "Bank 0 (0x0000-0x3FFF): ROM " << (int)memory.GetROMPage() << " (read-only)" << NEWLINE;
    }
    else
    {
        ss << "Bank 0 (0x0000-0x3FFF): RAM Page " << (int)memory.GetRAMPageForBank0() << " (read/write)" << NEWLINE;
    }
    
    // Bank 1 (always RAM)
    ss << "Bank 1 (0x4000-0x7FFF): RAM Page " << (int)memory.GetRAMPageForBank1() << " (read/write, contended)" << NEWLINE;
    ss << "                        [Screen 0 location]" << NEWLINE;
    
    // Bank 2 (always RAM)
    ss << "Bank 2 (0x8000-0xBFFF): RAM Page " << (int)memory.GetRAMPageForBank2() << " (read/write)" << NEWLINE;
    
    // Bank 3 (always RAM, pageable on 128K)
    ss << "Bank 3 (0xC000-0xFFFF): RAM Page " << (int)memory.GetRAMPageForBank3() << " (read/write)" << NEWLINE;
    
    if (config.mem_model != MM_SPECTRUM48)
    {
        ss << NEWLINE;
        ss << "Paging Control:" << NEWLINE;
        ss << "  Port 0x7FFD:      0x" << std::hex << std::setw(2) << std::setfill('0') 
           << (int)state.p7FFD << std::dec << " (bin: ";
        
        // Show binary
        for (int i = 7; i >= 0; --i)
        {
            ss << ((state.p7FFD >> i) & 1);
        }
        ss << ")" << NEWLINE;
        
        ss << "  Bits 0-2 (RAM):   " << (int)(state.p7FFD & 0x07) 
           << " (RAM page " << (int)(state.p7FFD & 0x07) << " at bank 3)" << NEWLINE;
        ss << "  Bit 3 (Screen):   " << ((state.p7FFD & 0x08) ? "1 (Shadow)" : "0 (Normal)") << NEWLINE;
        ss << "  Bit 4 (ROM):      " << ((state.p7FFD & 0x10) ? "1" : "0") << NEWLINE;
        ss << "  Bit 5 (Lock):     " << ((state.p7FFD & 0x20) ? "1 (Locked)" : "0 (Unlocked)") << NEWLINE;
    }
    
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateMemoryROM(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    CONFIG& config = context->config;
    Memory& memory = *context->pMemory;
    EmulatorState& state = context->emulatorState;
    
    ss << "ROM Configuration" << NEWLINE;
    ss << "=================" << NEWLINE;
    ss << NEWLINE;
    
    // Determine model
    std::string model = "ZX Spectrum 48K";
    int totalROMPages = 1;
    if (config.mem_model == MM_SPECTRUM128)
    {
        model = "ZX Spectrum 128K";
        totalROMPages = 2;
    }
    else if (config.mem_model == MM_PENTAGON)
    {
        model = "Pentagon 128K";
        totalROMPages = 4;
    }
    else if (config.mem_model == MM_PLUS3)
    {
        model = "ZX Spectrum +3";
        totalROMPages = 4;
    }
    
    ss << "Model:            " << model << NEWLINE;
    ss << "Total ROM Pages:  " << totalROMPages << NEWLINE;
    ss << "Active ROM Page:  " << (int)memory.GetROMPage() << NEWLINE;
    ss << "ROM Size:         " << (totalROMPages * 16) << " KB (" << totalROMPages << " × 16KB pages)" << NEWLINE;
    ss << NEWLINE;
    
    // Show ROM page descriptions based on model
    ss << "Available ROM Pages:" << NEWLINE;
    if (config.mem_model == MM_SPECTRUM48)
    {
        ss << "  Page 0: 48K BASIC ROM" << NEWLINE;
    }
    else if (config.mem_model == MM_SPECTRUM128)
    {
        ss << "  Page 0: 128K Editor/Menu ROM " << ((memory.GetROMPage() == 0) ? "[ACTIVE]" : "") << NEWLINE;
        ss << "  Page 1: 48K BASIC ROM " << ((memory.GetROMPage() == 1) ? "[ACTIVE]" : "") << NEWLINE;
    }
    else if (config.mem_model == MM_PENTAGON)
    {
        ss << "  Page 0: Service ROM " << ((memory.GetROMPage() == 0) ? "[ACTIVE]" : "") << NEWLINE;
        ss << "  Page 1: TR-DOS ROM " << ((memory.GetROMPage() == 1) ? "[ACTIVE]" : "") << NEWLINE;
        ss << "  Page 2: 128K Editor/Menu ROM " << ((memory.GetROMPage() == 2) ? "[ACTIVE]" : "") << NEWLINE;
        ss << "  Page 3: 48K BASIC ROM " << ((memory.GetROMPage() == 3) ? "[ACTIVE]" : "") << NEWLINE;
    }
    else if (config.mem_model == MM_PLUS3)
    {
        ss << "  Page 0: +3 Editor ROM " << ((memory.GetROMPage() == 0) ? "[ACTIVE]" : "") << NEWLINE;
        ss << "  Page 1: 48K BASIC ROM " << ((memory.GetROMPage() == 1) ? "[ACTIVE]" : "") << NEWLINE;
        ss << "  Page 2: +3DOS ROM " << ((memory.GetROMPage() == 2) ? "[ACTIVE]" : "") << NEWLINE;
        ss << "  Page 3: 48K BASIC (copy) ROM " << ((memory.GetROMPage() == 3) ? "[ACTIVE]" : "") << NEWLINE;
    }
    
    ss << NEWLINE;
    ss << "Current Mapping:" << NEWLINE;
    ss << "  Bank 0 (0x0000-0x3FFF): ";
    if (memory.IsBank0ROM())
    {
        ss << "ROM " << (int)memory.GetROMPage() << " (read-only)" << NEWLINE;
    }
    else
    {
        ss << "RAM Page " << (int)memory.GetRAMPageForBank0() << " (read/write)" << NEWLINE;
    }
    
    if (config.mem_model != MM_SPECTRUM48)
    {
        ss << NEWLINE;
        ss << "Port 0x7FFD bit 4 (ROM select): " << ((state.p7FFD & 0x10) ? "1" : "0") << NEWLINE;
    }
    
    session.SendResponse(ss.str());
}

/// endregion </State Inspection Commands>

