#include <debugger/breakpoints/breakpointmanager.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/platform.h>

#include <algorithm>
#include <bitset>
#include <iomanip>
#include <sstream>

#include "cli-processor.h"


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
        ss << "  audio          - Audio device overview" << NEWLINE;
        ss << "  audio ay       - Brief state for all AY chips (1=standard, 2=TurboSound, 3=ZX Next)" << NEWLINE;
        ss << "  audio ay <N>   - Detailed information about AY chip N (0-based index)" << NEWLINE;
        ss << "  audio ay <N> reg <R> - Specific AY register R of chip N (0-15)" << NEWLINE;
        ss << "  audio beeper   - Beeper state and activity" << NEWLINE;
        ss << "  audio gs       - General Sound device state" << NEWLINE;
        ss << "  audio covox    - Covox DAC state" << NEWLINE;
        ss << "  audio channels - Audio mixer state for all sound sources" << NEWLINE;
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
        ss << "  state audio ay       - Show brief AY chip overview" << NEWLINE;
        ss << "  state audio ay 0     - Show detailed info for first AY chip" << NEWLINE;
        ss << "  state audio ay reg 0 - Show detailed decoding for AY register 0" << NEWLINE;
        ss << "  state audio beeper   - Show beeper state" << NEWLINE;
        ss << "  state audio channels - Show all audio sources mixer state" << NEWLINE;

        ss << "  state audio beeper   - Show beeper state" << NEWLINE;
        ss << "  state audio channels - Show all audio sources mixer state" << NEWLINE;

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
    // Handle 'audio' subsystem
    else if (subsystem == "audio")
    {
        // Check for subcommands
        if (args.size() > 1)
        {
            std::string subcommand = args[1];
            std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

            if (subcommand == "ay")
            {
                // Handle different AY command syntaxes
                // args[0] = subsystem ("audio"), args[1] = subcommand ("ay")
                // AY-specific args start at args[2]

                if (args.size() <= 2)
                {
                    // state audio ay - show brief info for all AY chips
                    HandleStateAudioAY(session, context);
                    return;
                }

                // We have additional arguments after "ay"
                std::string ayArg0 = args[2];  // First arg after "ay"

                // Check for: state audio ay <chip> reg <register>
                if (args.size() >= 5 && (args[3] == "reg" || args[3] == "register"))
                {
                    HandleStateAudioAYRegister(session, context, ayArg0, args[4]);
                    return;
                }
                // Check for legacy: state audio ay reg <register> (defaults to chip 0)
                else if ((ayArg0 == "reg" || ayArg0 == "register") && args.size() >= 4)
                {
                    HandleStateAudioAYRegister(session, context, "0", args[3]);
                    return;
                }
                else
                {
                    // state audio ay <index> - show detailed info for specific chip
                    HandleStateAudioAYIndex(session, context, ayArg0);
                    return;
                }
            }
            else if (subcommand == "beeper")
            {
                HandleStateAudioBeeper(session, context);
                return;
            }
            else if (subcommand == "gs")
            {
                HandleStateAudioGS(session, context);
                return;
            }
            else if (subcommand == "covox")
            {
                HandleStateAudioCovox(session, context);
                return;
            }
            else if (subcommand == "channels")
            {
                HandleStateAudioChannels(session, context);
                return;
            }
            else
            {
                session.SendResponse(std::string("Error: Unknown audio subcommand '") + args[1] + "'" + NEWLINE +
                                     "Available: ay, beeper, gs, covox, channels" + NEWLINE);
                return;
            }
        }
        else
        {
            // state audio - show brief overview of all audio devices
            HandleStateAudio(session, context);
            return;
        }
    }
    else
    {
        session.SendResponse(std::string("Error: Unknown subsystem '") + subsystem + "'" + NEWLINE +
                             "Available subsystems: memory, ram, rom, screen, audio" + NEWLINE);
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

    bool is128K =
        (config.mem_model == MM_SPECTRUM128 || config.mem_model == MM_PENTAGON || config.mem_model == MM_PLUS3);

    if (is128K)
    {
        uint8_t port7FFD = state.p7FFD;
        bool shadowScreen = (port7FFD & 0x08) != 0;

        ss << "Active Screen: Screen " << (shadowScreen ? "1" : "0") << " (RAM page " << (shadowScreen ? "7" : "5")
           << ")" << NEWLINE;
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
    bool is128K =
        (config.mem_model == MM_SPECTRUM128 || config.mem_model == MM_PENTAGON || config.mem_model == MM_PLUS3);

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
        ss << "  Contention:        " << (ramBank == 7 ? "Inactive (not in contended range)" : "N/A (not mapped)")
           << NEWLINE;
        ss << NEWLINE;

        ss << "Port 0x7FFD:  0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (int)port7FFD
           << std::dec << " (bin: ";
        for (int i = 7; i >= 0; i--)
            ss << ((port7FFD >> i) & 1);
        ss << ")" << NEWLINE;
        ss << "  Bits 0-2: " << (int)ramBank << " (RAM page " << (int)ramBank << " mapped to bank 3)" << NEWLINE;
        ss << "  Bit 3:    " << (shadowScreen ? "1" : "0") << " (ULA displays Screen " << (shadowScreen ? "1" : "0")
           << ")" << NEWLINE;
        ss << "  Bit 4:    " << ((port7FFD & 0x10) ? "1" : "0")
           << " (ROM: " << ((port7FFD & 0x10) ? "48K BASIC" : "128K Editor") << ")" << NEWLINE;
        ss << "  Bit 5:    " << ((port7FFD & 0x20) ? "1" : "0") << " (Paging "
           << ((port7FFD & 0x20) ? "LOCKED" : "enabled") << ")" << NEWLINE;
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

    if (config.mem_model == MM_SPECTRUM128 || config.mem_model == MM_PENTAGON || config.mem_model == MM_PLUS3)
    {
        uint8_t port7FFD = context->emulatorState.p7FFD;
        bool shadowScreen = (port7FFD & 0x08) != 0;
        ss << "Active Screen:   Screen " << (shadowScreen ? "1" : "0") << " (RAM page " << (shadowScreen ? "7" : "5")
           << ")" << NEWLINE;
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
        romMode =
            (memory.GetROMPage() == 2) ? "128K Editor" : (memory.GetROMPage() == 3 ? "48K BASIC" : "Service/TR-DOS");
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
        ss << "  Port 0x7FFD:      0x" << std::hex << std::setw(2) << std::setfill('0') << (int)state.p7FFD << std::dec
           << NEWLINE;
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
    ss << "Bank 1 (0x4000-0x7FFF): RAM Page " << (int)memory.GetRAMPageForBank1() << " (read/write, contended)"
       << NEWLINE;
    ss << "                        [Screen 0 location]" << NEWLINE;

    // Bank 2 (always RAM)
    ss << "Bank 2 (0x8000-0xBFFF): RAM Page " << (int)memory.GetRAMPageForBank2() << " (read/write)" << NEWLINE;

    // Bank 3 (always RAM, pageable on 128K)
    ss << "Bank 3 (0xC000-0xFFFF): RAM Page " << (int)memory.GetRAMPageForBank3() << " (read/write)" << NEWLINE;

    if (config.mem_model != MM_SPECTRUM48)
    {
        ss << NEWLINE;
        ss << "Paging Control:" << NEWLINE;
        ss << "  Port 0x7FFD:      0x" << std::hex << std::setw(2) << std::setfill('0') << (int)state.p7FFD << std::dec
           << " (bin: ";

        // Show binary
        for (int i = 7; i >= 0; --i)
        {
            ss << ((state.p7FFD >> i) & 1);
        }
        ss << ")" << NEWLINE;

        ss << "  Bits 0-2 (RAM):   " << (int)(state.p7FFD & 0x07) << " (RAM page " << (int)(state.p7FFD & 0x07)
           << " at bank 3)" << NEWLINE;
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

/// region <Audio State Commands>

void CLIProcessor::HandleStateAudio(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    ss << "Audio Device Overview" << NEWLINE;
    ss << "====================" << NEWLINE;
    ss << NEWLINE;

    SoundManager* soundManager = context->pSoundManager;
    if (!soundManager)
    {
        ss << "Error: Sound manager not available" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    // Check available audio devices
    bool hasBeeper = true;  // Beeper is always available
    bool hasAY = soundManager->hasTurboSound();
    int ayCount = hasAY ? soundManager->getAYChipCount() : 0;
    bool hasGS = false;     // General Sound not implemented yet
    bool hasCovox = false;  // Covox not implemented yet

    ss << "Available Audio Devices:" << NEWLINE;
    ss << "  Beeper:      " << (hasBeeper ? "Available" : "Not available") << NEWLINE;
    ss << "  AY Chips:    " << (ayCount > 0 ? std::to_string(ayCount) + (ayCount == 2 ? " (TurboSound)" : "") : "None")
       << NEWLINE;
    ss << "  General Sound: " << (hasGS ? "Available" : "Not available") << NEWLINE;
    ss << "  Covox DAC:   " << (hasCovox ? "Available" : "Not available") << NEWLINE;
    ss << NEWLINE;

    ss << "Use 'state audio <device>' for detailed information:" << NEWLINE;
    ss << "  state audio ay       - AY chip overview" << NEWLINE;
    ss << "  state audio beeper   - Beeper state" << NEWLINE;
    ss << "  state audio gs       - General Sound state" << NEWLINE;
    ss << "  state audio covox    - Covox DAC state" << NEWLINE;
    ss << "  state audio channels - All audio channels mixer state" << NEWLINE;

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateAudioAY(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    ss << "AY Chip Overview" << NEWLINE;
    ss << "===============" << NEWLINE;
    ss << NEWLINE;

    SoundManager* soundManager = context->pSoundManager;
    if (!soundManager || !soundManager->hasTurboSound())
    {
        ss << "Error: AY chips not available (TurboSound not initialized)" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    // Count available AY chips
    int ayCount = soundManager->getAYChipCount();

    ss << "AY Chips Available: " << ayCount << " (";

    if (ayCount == 0)
        ss << "None";
    else if (ayCount == 1)
        ss << "Standard AY-3-8912";
    else if (ayCount == 2)
        ss << "TurboSound (dual AY-3-8912)";
    else if (ayCount == 3)
        ss << "ZX Next (triple AY-3-8912)";

    ss << ")" << NEWLINE;
    ss << NEWLINE;

    // Show brief info for each chip
    for (int i = 0; i < ayCount; i++)
    {
        SoundChip_AY8910* chip = soundManager->getAYChip(i);
        if (!chip)
            continue;

        ss << "AY Chip " << i << ":" << NEWLINE;
        ss << "  Type: AY-3-8912" << NEWLINE;

        // Check if any channels are active (tone or noise enabled)
        bool hasActiveChannels = false;
        const auto* toneGens = chip->getToneGenerators();
        for (int ch = 0; ch < 3; ch++)
        {
            if (toneGens[ch].toneEnabled() || toneGens[ch].noiseEnabled())
            {
                hasActiveChannels = true;
                break;
            }
        }

        ss << "  Active Channels: " << (hasActiveChannels ? "Yes" : "No") << NEWLINE;
        ss << "  Envelope Active: " << (chip->getEnvelopeGenerator().out() > 0 ? "Yes" : "No") << NEWLINE;
        ss << "  Sound Played: " << "No (tracking not implemented)"
           << NEWLINE;  // TODO: Implement sound played tracking
        ss << NEWLINE;
    }

    ss << "Use 'state audio ay <N>' for detailed information about a specific chip" << NEWLINE;

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateAudioAYIndex(const ClientSession& session, EmulatorContext* context,
                                           const std::string& indexStr)
{
    std::stringstream ss;
    SoundManager* soundManager = context->pSoundManager;

    if (!soundManager || !soundManager->hasTurboSound())
    {
        ss << "Error: AY chips not available (TurboSound not initialized)" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    // Parse chip index
    int chipIndex = -1;
    try
    {
        chipIndex = std::stoi(indexStr);
    }
    catch (const std::exception&)
    {
        ss << "Error: Invalid chip index '" << indexStr << "' (must be 0-based integer)" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    // Get the requested chip
    SoundChip_AY8910* chip = soundManager->getAYChip(chipIndex);
    if (!chip)
    {
        ss << "Error: AY chip " << chipIndex << " not available" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    ss << "AY Chip " << chipIndex << " Detailed Information" << NEWLINE;
    ss << std::string(35, '=') << NEWLINE;
    ss << NEWLINE;

    ss << "Chip Type: AY-3-8912" << NEWLINE;
    ss << "Index: " << chipIndex << NEWLINE;
    ss << NEWLINE;

    // Show register values
    ss << "Register Values:" << NEWLINE;
    const uint8_t* registers = chip->getRegisters();
    for (int reg = 0; reg < 16; reg++)
    {
        ss << "  R" << std::setw(2) << std::setfill('0') << reg << " (" << SoundChip_AY8910::AYRegisterNames[reg]
           << "): 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)registers[reg] << std::dec << NEWLINE;
    }
    ss << NEWLINE;

    // Show channel information
    ss << "Channel Information:" << NEWLINE;
    const char* channelNames[] = {"A", "B", "C"};
    const auto* toneGens = chip->getToneGenerators();
    for (int ch = 0; ch < 3; ch++)
    {
        const auto& toneGen = toneGens[ch];
        uint8_t fine = registers[ch * 2];
        uint8_t coarse = registers[ch * 2 + 1];
        uint16_t period = (coarse << 8) | fine;

        ss << "  Channel " << channelNames[ch] << ":" << NEWLINE;
        ss << "    Period: " << period << " (" << fine << " fine + " << coarse << " coarse)" << NEWLINE;

        // Calculate frequency (approximate)
        double freq = 1750000.0 / (16.0 * (period + 1));  // 1.75MHz AY clock / 16 / period
        ss << "    Frequency: ~" << (int)freq << " Hz" << NEWLINE;

        ss << "    Volume: " << (int)toneGen.volume() << "/15" << NEWLINE;
        ss << "    Tone Enabled: " << (toneGen.toneEnabled() ? "Yes" : "No") << NEWLINE;
        ss << "    Noise Enabled: " << (toneGen.noiseEnabled() ? "Yes" : "No") << NEWLINE;
        ss << "    Envelope Enabled: " << (toneGen.envelopeEnabled() ? "Yes" : "No") << NEWLINE;
        ss << NEWLINE;
    }

    // Show envelope information
    ss << "Envelope Generator:" << NEWLINE;
    uint8_t envShape = registers[13];
    uint16_t envPeriod = (registers[12] << 8) | registers[11];
    ss << "  Shape: " << (int)envShape << NEWLINE;
    ss << "  Period: " << envPeriod << NEWLINE;
    ss << "  Current Output: " << (int)chip->getEnvelopeGenerator().out() << "/15" << NEWLINE;
    ss << NEWLINE;

    // Show noise information
    uint8_t noisePeriod = registers[6] & 0x1F;
    ss << "Noise Generator:" << NEWLINE;
    ss << "  Period: " << (int)noisePeriod << NEWLINE;
    double noiseFreq = 1750000.0 / (16.0 * (noisePeriod + 1));
    ss << "  Frequency: ~" << (int)noiseFreq << " Hz" << NEWLINE;
    ss << NEWLINE;

    // Show mixer state
    ss << "Mixer State:" << NEWLINE;
    uint8_t mixer = registers[7];
    ss << "  Register 7: 0x" << std::hex << (int)mixer << std::dec << NEWLINE;
    ss << "  Channel A Tone: " << ((mixer & 0x01) ? "OFF" : "ON") << NEWLINE;
    ss << "  Channel B Tone: " << ((mixer & 0x02) ? "OFF" : "ON") << NEWLINE;
    ss << "  Channel C Tone: " << ((mixer & 0x04) ? "OFF" : "ON") << NEWLINE;
    ss << "  Channel A Noise: " << ((mixer & 0x08) ? "OFF" : "ON") << NEWLINE;
    ss << "  Channel B Noise: " << ((mixer & 0x10) ? "OFF" : "ON") << NEWLINE;
    ss << "  Channel C Noise: " << ((mixer & 0x20) ? "OFF" : "ON") << NEWLINE;
    ss << "  I/O Port A: " << ((mixer & 0x40) ? "Input" : "Output") << NEWLINE;
    ss << "  I/O Port B: " << ((mixer & 0x80) ? "Input" : "Output") << NEWLINE;
    ss << NEWLINE;

    // Show I/O ports
    ss << "I/O Ports:" << NEWLINE;
    ss << "  Port A: 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)registers[14] << std::dec;
    ss << " (" << ((mixer & 0x40) ? "Input" : "Output") << ")" << NEWLINE;
    ss << "  Port B: 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)registers[15] << std::dec;
    ss << " (" << ((mixer & 0x80) ? "Input" : "Output") << ")" << NEWLINE;
    ss << NEWLINE;

    ss << "Sound Played Since Reset: No (tracking not implemented)"
       << NEWLINE;  // TODO: Implement sound played tracking

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateAudioAYRegister(const ClientSession& session, EmulatorContext* context,
                                              const std::string& chipStr, const std::string& regStr)
{
    std::stringstream ss;
    SoundManager* soundManager = context->pSoundManager;

    if (!soundManager || !soundManager->hasTurboSound())
    {
        ss << "Error: AY chips not available (TurboSound not initialized)" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    // Parse chip index
    int chipIndex = -1;
    try
    {
        chipIndex = std::stoi(chipStr);
    }
    catch (const std::exception&)
    {
        ss << "Error: Invalid chip index '" << chipStr << "' (must be 0-based integer)" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    // Parse register number
    int regNum = -1;
    try
    {
        regNum = std::stoi(regStr);
    }
    catch (const std::exception&)
    {
        ss << "Error: Invalid register number '" << regStr << "' (must be 0-15)" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    if (regNum < 0 || regNum > 15)
    {
        ss << "Error: Register number must be between 0 and 15" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    SoundChip_AY8910* chip = soundManager->getAYChip(chipIndex);
    if (!chip)
    {
        ss << "Error: AY chip " << chipIndex << " not available" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    const uint8_t* registers = chip->getRegisters();
    uint8_t regValue = registers[regNum];

    ss << "AY Register " << regNum << " (" << SoundChip_AY8910::AYRegisterNames[regNum] << ")" << NEWLINE;
    ss << std::string(50, '=') << NEWLINE;
    ss << NEWLINE;

    ss << "Raw Value: 0x" << std::hex << std::setw(2) << std::setfill('0') << (int)regValue << " (" << std::dec
       << (int)regValue << ")" << NEWLINE;
    ss << "Binary: " << std::bitset<8>(regValue) << NEWLINE;
    ss << NEWLINE;

    // Provide specific decoding based on register
    switch (regNum)
    {
        case 0:
        case 2:
        case 4:  // Fine period registers
        {
            int channel = regNum / 2;
            const char* channelNames[] = {"A", "B", "C"};
            ss << "Channel " << channelNames[channel] << " Tone Period (Fine):" << NEWLINE;
            ss << "  This is the lower 8 bits of the 12-bit period value" << NEWLINE;
            ss << "  Combined with coarse register R" << (regNum + 1) << " for full period" << NEWLINE;
            uint8_t coarse = registers[regNum + 1];
            uint16_t period = (coarse << 8) | regValue;
            ss << "  Current full period: " << period << NEWLINE;
            double freq = 1750000.0 / (16.0 * (period + 1));
            ss << "  Approximate frequency: " << (int)freq << " Hz" << NEWLINE;
            break;
        }

        case 1:
        case 3:
        case 5:  // Coarse period registers
        {
            int channel = (regNum - 1) / 2;
            const char* channelNames[] = {"A", "B", "C"};
            ss << "Channel " << channelNames[channel] << " Tone Period (Coarse):" << NEWLINE;
            ss << "  This is the upper 4 bits of the 12-bit period value" << NEWLINE;
            ss << "  Combined with fine register R" << (regNum - 1) << " for full period" << NEWLINE;
            uint8_t fine = registers[regNum - 1];
            uint16_t period = (regValue << 8) | fine;
            ss << "  Current full period: " << period << NEWLINE;
            double freq = 1750000.0 / (16.0 * (period + 1));
            ss << "  Approximate frequency: " << (int)freq << " Hz" << NEWLINE;
            break;
        }

        case 6:  // Noise period
            ss << "Noise Generator Period:" << NEWLINE;
            ss << "  5-bit value (0-31)" << NEWLINE;
            ss << "  Actual period: " << ((int)regValue & 0x1F) << NEWLINE;
            {
                double noiseFreq = 1750000.0 / (16.0 * (((int)regValue & 0x1F) + 1));
                ss << "  Approximate frequency: " << (int)noiseFreq << " Hz" << NEWLINE;
            }
            break;

        case 7:  // Mixer control
            ss << "Mixer Control:" << NEWLINE;
            ss << "  Bit 0: Channel A Tone - " << ((regValue & 0x01) ? "Disabled" : "Enabled") << NEWLINE;
            ss << "  Bit 1: Channel B Tone - " << ((regValue & 0x02) ? "Disabled" : "Enabled") << NEWLINE;
            ss << "  Bit 2: Channel C Tone - " << ((regValue & 0x04) ? "Disabled" : "Enabled") << NEWLINE;
            ss << "  Bit 3: Channel A Noise - " << ((regValue & 0x08) ? "Disabled" : "Enabled") << NEWLINE;
            ss << "  Bit 4: Channel B Noise - " << ((regValue & 0x10) ? "Disabled" : "Enabled") << NEWLINE;
            ss << "  Bit 5: Channel C Noise - " << ((regValue & 0x20) ? "Disabled" : "Enabled") << NEWLINE;
            ss << "  Bit 6: Port A Direction - " << ((regValue & 0x40) ? "Input" : "Output") << NEWLINE;
            ss << "  Bit 7: Port B Direction - " << ((regValue & 0x80) ? "Input" : "Output") << NEWLINE;
            break;

        case 8:
        case 9:
        case 10:  // Volume registers
        {
            int channel = regNum - 8;
            const char* channelNames[] = {"A", "B", "C"};
            ss << "Channel " << channelNames[channel] << " Volume:" << NEWLINE;
            ss << "  4-bit volume value: " << ((int)regValue & 0x0F) << "/15" << NEWLINE;
            ss << "  Bit 4 (MSB): Envelope mode - " << ((regValue & 0x10) ? "Enabled" : "Disabled") << NEWLINE;
            if (regValue & 0x10)
                ss << "  Volume controlled by envelope generator" << NEWLINE;
            else
                ss << "  Fixed volume level" << NEWLINE;
            break;
        }

        case 11:  // Envelope period fine
            ss << "Envelope Period (Fine):" << NEWLINE;
            ss << "  Lower 8 bits of 16-bit envelope period" << NEWLINE;
            ss << "  Combined with coarse register R12 for full period" << NEWLINE;
            {
                uint8_t coarse = registers[12];
                uint16_t period = (coarse << 8) | regValue;
                ss << "  Current full period: " << period << NEWLINE;
                double envFreq = 1750000.0 / (256.0 * (period + 1));
                ss << "  Approximate frequency: " << std::fixed << std::setprecision(2) << envFreq << " Hz" << NEWLINE;
            }
            break;

        case 12:  // Envelope period coarse
            ss << "Envelope Period (Coarse):" << NEWLINE;
            ss << "  Upper 8 bits of 16-bit envelope period" << NEWLINE;
            ss << "  Combined with fine register R11 for full period" << NEWLINE;
            {
                uint8_t fine = registers[11];
                uint16_t period = (regValue << 8) | fine;
                ss << "  Current full period: " << period << NEWLINE;
                double envFreq = 1750000.0 / (256.0 * (period + 1));
                ss << "  Approximate frequency: " << std::fixed << std::setprecision(2) << envFreq << " Hz" << NEWLINE;
            }
            break;

        case 13:  // Envelope shape
            ss << "Envelope Shape:" << NEWLINE;
            ss << "  4-bit shape value: " << ((int)regValue & 0x0F) << NEWLINE;
            ss << "  Bit 0: Continue" << NEWLINE;
            ss << "  Bit 1: Attack" << NEWLINE;
            ss << "  Bit 2: Alternate" << NEWLINE;
            ss << "  Bit 3: Hold" << NEWLINE;
            // TODO: Add shape name interpretation
            break;

        case 14:  // I/O Port A
            ss << "I/O Port A:" << NEWLINE;
            ss << "  Direction: " << ((registers[7] & 0x40) ? "Input" : "Output") << NEWLINE;
            ss << "  Value: 0x" << std::hex << (int)regValue << std::dec << NEWLINE;
            break;

        case 15:  // I/O Port B
            ss << "I/O Port B:" << NEWLINE;
            ss << "  Direction: " << ((registers[7] & 0x80) ? "Input" : "Output") << NEWLINE;
            ss << "  Value: 0x" << std::hex << (int)regValue << std::dec << NEWLINE;
            break;
    }

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateAudioBeeper(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    ss << "Beeper State" << NEWLINE;
    ss << "============" << NEWLINE;
    ss << NEWLINE;

    SoundManager* soundManager = context->pSoundManager;
    if (!soundManager)
    {
        ss << "Error: Sound manager not available" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    Beeper& beeper = soundManager->getBeeper();

    // Note: Beeper doesn't have public methods to check current state
    // This is a simplified implementation
    ss << "Device: Beeper (ULA integrated)" << NEWLINE;
    ss << "Output Port: 0xFE (ULA port)" << NEWLINE;
    ss << "Current Level: Unknown (internal state not accessible)" << NEWLINE;
    ss << "Last Output: Unknown (internal state not accessible)" << NEWLINE;
    ss << "Frequency Range: ~20Hz - ~10kHz" << NEWLINE;
    ss << "Bit Resolution: 1-bit (square wave)" << NEWLINE;
    ss << NEWLINE;
    ss << "Sound Played Since Reset: No (tracking not implemented)"
       << NEWLINE;  // TODO: Implement sound played tracking

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateAudioGS(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    ss << "General Sound Device State" << NEWLINE;
    ss << "==========================" << NEWLINE;
    ss << NEWLINE;

    // General Sound is not implemented yet
    ss << "Status: Not implemented" << NEWLINE;
    ss << NEWLINE;
    ss << "General Sound (GS) is a sound expansion device that was planned" << NEWLINE;
    ss << "for the ZX Spectrum but never released commercially." << NEWLINE;
    ss << NEWLINE;
    ss << "This command is reserved for future implementation." << NEWLINE;

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateAudioCovox(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    ss << "Covox DAC State" << NEWLINE;
    ss << "===============" << NEWLINE;
    ss << NEWLINE;

    // Covox is not implemented yet
    ss << "Status: Not implemented" << NEWLINE;
    ss << NEWLINE;
    ss << "Covox is an 8-bit DAC (Digital-to-Analog Converter) that connects" << NEWLINE;
    ss << "to various ports on the ZX Spectrum for sample playback." << NEWLINE;
    ss << NEWLINE;
    ss << "This command is reserved for future implementation." << NEWLINE;

    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateAudioChannels(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    ss << "Audio Channels Mixer State" << NEWLINE;
    ss << "==========================" << NEWLINE;
    ss << NEWLINE;

    SoundManager* soundManager = context->pSoundManager;
    if (!soundManager)
    {
        ss << "Error: Sound manager not available" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    // Beeper state
    ss << "Beeper:" << NEWLINE;
    ss << "  Status: Available" << NEWLINE;
    ss << "  Current Level: Unknown" << NEWLINE;
    ss << "  Active: Unknown" << NEWLINE;
    ss << NEWLINE;

    // AY channels
    bool hasAY = soundManager->hasTurboSound();
    ss << "AY Channels:" << NEWLINE;
    if (hasAY)
    {
        int ayCount = soundManager->getAYChipCount();
        for (int chipIdx = 0; chipIdx < ayCount; chipIdx++)
        {
            SoundChip_AY8910* chip = soundManager->getAYChip(chipIdx);
            if (!chip)
                continue;

            ss << "  Chip " << chipIdx << " (AY-3-8912):" << NEWLINE;
            const auto* toneGens = chip->getToneGenerators();
            const char* channelNames[] = {"A", "B", "C"};
            for (int ch = 0; ch < 3; ch++)
            {
                const auto& toneGen = toneGens[ch];
                ss << "    Channel " << channelNames[ch] << ": "
                   << (toneGen.toneEnabled() || toneGen.noiseEnabled() ? "ON" : "OFF");
                ss << " (Vol: " << (int)toneGen.volume() << "/15";
                if (toneGen.envelopeEnabled())
                    ss << ", Envelope";
                ss << ")" << NEWLINE;
            }
        }
    }
    else
    {
        ss << "  No AY chips available" << NEWLINE;
    }
    ss << NEWLINE;

    // General Sound (not implemented)
    ss << "General Sound:" << NEWLINE;
    ss << "  Status: Not available" << NEWLINE;
    ss << NEWLINE;

    // Covox (not implemented)
    ss << "Covox DAC:" << NEWLINE;
    ss << "  Status: Not available" << NEWLINE;
    ss << NEWLINE;

    // Master state
    ss << "Master Audio:" << NEWLINE;
    ss << "  Muted: " << (soundManager->isMuted() ? "Yes" : "No") << NEWLINE;
    ss << "  Sample Rate: 44100 Hz" << NEWLINE;
    ss << "  Channels: Stereo" << NEWLINE;
    ss << "  Bit Depth: 16-bit" << NEWLINE;

    session.SendResponse(ss.str());
}

/// endregion </Audio State Commands>

/// endregion </State Inspection Commands>
