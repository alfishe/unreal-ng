#include <emulator/io/ide/cdaudiocontrol.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <emulator/config.h>
#include <emulator/emulator.h>
#include <emulator/cpu/core.h>
#include <emulator/emulatorcontext.h>
#include <emulator/memory/rom.h>
#include <emulator/platform.h>
#include <emulator/video/screen.h>

#include <algorithm>
#include <bitset>
#include <iomanip>
#include <sstream>

#include "cli-processor.h"
#include "cli-sprinter-format.h"
#include <emulator/state/devicestate.h>


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
        ss << "  audio fm       - TurboSound FM overview (board latches, both YM2203 FM halves)" << NEWLINE;
        ss << "  audio fm <N>   - Full FM report of chip N (0/1): mode, timers, channels, operators, envelopes" << NEWLINE;
        ss << "  fdc            - Beta Disk WD1793: registers, status bits, FSM, signals, drives" << NEWLINE;
        ss << "  ide            - IDE board: scheme, latches, both units' task file, command, CD sense" << NEWLINE;
        ss << "  cdaudio        - CD drives' audio: disc, tracks, status, head, volume, mixer row" << NEWLINE;
        ss << "  tsconf         - TS-Conf machine: memory map, video, TSU, interrupts, DMA, clock, SD" << NEWLINE;
        ss << "  tsconf tsu     - TS-Conf TSU objects (tile layers, 85 sprites) and the 256 CRAM cells" << NEWLINE;
        ss << "  sprinter       - Sprinter Sp2000: PLD, windows, cells, clock + waits, video, accelerator, sound, Z84C15, floppy, BIOS" << NEWLINE;
        ss << "  sprinter ports [map=0-3] [dos=0|1] [pn5=0|1] [rw=r|w|rw] - the decoded port table (page #40)" << NEWLINE;
        ss << "  sprinter port <hex> [rw=r|w] [map=..] [dos=..] [pn5=..]  - one port: index, code, name" << NEWLINE;
        ss << "  sprinter text  - the screen text of the mode table's text squares (BIOS, DSS)" << NEWLINE;
        ss << "  sprinter video [page=0|1] [all=1] [squares=1] - the mode table per square (map: one letter a square)" << NEWLINE;
        ss << "  sprinter palette [0-7|all|used] - the palettes, R G B per pen as video RAM holds them" << NEWLINE;
        ss << "  sprinter ring  - the Covox-Blaster sample ring (play / write index marked)" << NEWLINE;
        ss << "  sprinter bios [<3.04|3.06|3.07|file|-> [fast_start=0|1] [accel_int_suspend=0|1] [reset=0|1]] - BIOS images; select" << NEWLINE;
        ss << "  contention     - Memory contention: rule, switch, interface, contended slots, statistics" << NEWLINE;
        ss << "  audio beeper   - Beeper state and activity" << NEWLINE;
        ss << "  audio gs       - General Sound device state (--verbose adds coprocessor registers)" << NEWLINE;
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
        ss << "  state audio fm 1     - Show the full FM report of TSFM chip 1" << NEWLINE;
        ss << "  state fdc            - Show the Beta Disk controller and drives" << NEWLINE;
        ss << "  state ide            - Show the IDE board and its units" << NEWLINE;
        ss << "  state cdaudio        - Show the CD drives' audio state" << NEWLINE;
        ss << "  state tsconf         - Show the TS-Conf machine state (also: ts)" << NEWLINE;
        ss << "  state tsconf tsu     - Show the TSU objects and the palette (debug views)" << NEWLINE;
        ss << "  state sprinter       - Show the Sprinter machine state (also: sp)" << NEWLINE;
        ss << "  state sprinter ports map=0 dos=0 rw=w - Show the OUT half of map 0 with TR-DOS off" << NEWLINE;
        ss << "  state sprinter port 21BC rw=w         - Which device answers OUT (#21BC)" << NEWLINE;
        ss << "  state rtc            - Show the CMOS clock (also: rtc, cmos)" << NEWLINE;
        ss << "  state contention     - Show where the CPU waits for the video logic" << NEWLINE;
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
            else if (subcommand == "attributes")
            {
                HandleStateScreenAttributes(session, context);
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
                                     "Available: mode, flash, attributes, verbose" + NEWLINE);
                return;
            }
        }

        // No subcommand - show brief screen state
        HandleStateScreen(session, context);
        return;
    }
    // Handle 'audio' subsystem
    else if (subsystem == "fdc" || subsystem == "disk" || subsystem == "wd1793")
    {
        HandleStateFdc(session, context);
        return;
    }
    else if (subsystem == "cdaudio" || subsystem == "cdda")
    {
        session.SendResponse(std::string("CD audio") + NEWLINE + "========" + NEWLINE +
                             CdAudioStateText(CdAudioControl::State(context)));
    }
    else if (subsystem == "ide" || subsystem == "hdd" || subsystem == "cdrom")
    {
        std::stringstream ide;
        ide << "IDE board" << NEWLINE << "=========" << NEWLINE << DeviceState::ToText(DeviceState::Ide(context));
        session.SendResponse(ide.str());
        return;
    }
    else if ((subsystem == "tsconf" || subsystem == "ts") && args.size() > 1 && args[1] == "tsu")
    {
        std::stringstream ts;
        ts << "TS-Conf TSU" << NEWLINE << "===========" << NEWLINE << DeviceState::ToText(DeviceState::TsConfTsu(context));
        session.SendResponse(ts.str());
        return;
    }
    else if (subsystem == "tsconf" || subsystem == "ts")
    {
        std::stringstream ts;
        ts << "TS-Conf" << NEWLINE << "=======" << NEWLINE << DeviceState::ToText(DeviceState::TsConf(context));
        session.SendResponse(ts.str());
        return;
    }
    else if (subsystem == "sprinter" || subsystem == "sp")
    {
        session.SendResponse(CliSprinter::StateText(context, args, NEWLINE));
        return;
    }
    else if (subsystem == "rtc" || subsystem == "cmos")
    {
        session.SendResponse(RtcReportText(context));
        return;
    }
    else if (subsystem == "contention")
    {
        HandleStateContention(session, context);
        return;
    }
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
            else if (subcommand == "fm")
            {
                HandleStateAudioFM(session, context, args.size() > 2 ? args[2] : "");
                return;
            }
            else if (subcommand == "moonsound")
            {
                // state audio moonsound [fm|pcm] (DeviceState::MoonSound / MoonSoundFm / MoonSoundPcm)
                const std::string part = args.size() > 2 ? args[2] : "";
                if (!part.empty() && part != "fm" && part != "pcm")
                {
                    session.SendResponse("Error: state audio moonsound [fm|pcm]" + std::string(NEWLINE));
                    return;
                }
                const StateNode report = part == "fm"    ? DeviceState::MoonSoundFm(context)
                                         : part == "pcm" ? DeviceState::MoonSoundPcm(context)
                                                         : DeviceState::MoonSound(context);
                session.SendResponse(std::string("MoonSound (OPL4)") + (part.empty() ? "" : " " + part) + NEWLINE +
                                     DeviceState::ToText(report));
                return;
            }
            else if (subcommand == "beeper")
            {
                HandleStateAudioBeeper(session, context);
                return;
            }
            else if (subcommand == "gs")
            {
                // state audio gs [verbose|--verbose] (GS design §11.4)
                HandleStateAudioGS(session, context, args.size() > 2 ? args[2] : "");
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
                                     "Available: ay, fm, moonsound, beeper, gs, covox, channels" + NEWLINE);
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

// Screen reports come from the core (DeviceState::Screen / ScreenMode /
// ScreenFlash) - the same data every automation module returns
void CLIProcessor::HandleStateScreen(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    ss << "Screen State" << NEWLINE << "============" << NEWLINE;
    ss << DeviceState::ToText(DeviceState::Screen(context, false));
    ss << NEWLINE << "Use 'state screen verbose' for per-screen details" << NEWLINE;
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateScreenVerbose(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    ss << "Screen State (Verbose)" << NEWLINE << "======================" << NEWLINE;
    ss << DeviceState::ToText(DeviceState::Screen(context, true));
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateScreenMode(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    ss << "Video Mode" << NEWLINE << "==========" << NEWLINE;
    ss << DeviceState::ToText(DeviceState::ScreenMode(context));
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateScreenFlash(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    ss << "Screen Flash" << NEWLINE << "============" << NEWLINE;
    ss << DeviceState::ToText(DeviceState::ScreenFlash(context));
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateScreenAttributes(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    ss << "Screen Attributes" << NEWLINE << "=================" << NEWLINE;
    ss << DeviceState::ToText(DeviceState::ScreenAttributes(context));
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
    std::string model = Config::GetModelFullName(config.mem_model);
    ss << "Model: " << model << NEWLINE;
    ss << NEWLINE;

    // ROM Configuration
    ss << "ROM Configuration:" << NEWLINE;
    ss << "  Active ROM Page:  " << (int)memory.GetROMPage() << NEWLINE;

    // Determine ROM mode
    std::string romMode = "Unknown";
    if (config.mem_model == MM_SPECTRUM48)
        romMode = "48K BASIC";
    else if (config.mem_model == MM_SPECTRUM128 || config.mem_model == MM_PLUS2)
        romMode = (memory.GetROMPage() == 0) ? "128K Editor" : "48K BASIC";
    else if (config.mem_model == MM_PENTAGON)
        romMode =
            (memory.GetROMPage() == 2) ? "128K Editor" : (memory.GetROMPage() == 3 ? "48K BASIC" : "Service/TR-DOS");
    else if (config.mem_model == MM_PLUS3 || config.mem_model == MM_PLUS2A)
        romMode = (memory.GetROMPage() == 0)   ? "+3 Editor"
                  : (memory.GetROMPage() == 1) ? "128 BASIC Syntax"
                  : (memory.GetROMPage() == 2) ? "+3DOS"
                                               : "48K BASIC";
    else if (config.mem_model == MM_PROFI)
        romMode = (memory.GetROMPage() == 0)   ? "SYS/Menu"
                  : (memory.GetROMPage() == 1) ? "TR-DOS"
                  : (memory.GetROMPage() == 2) ? "128K Editor + STS Monitor"
                                               : "48K BASIC";
    else if (config.mem_model == MM_SPRINTER)
    {
        ROM* sprinterRom = context->pCore ? context->pCore->GetROM() : nullptr;
        romMode = memory.IsBank0ROM() && sprinterRom ? sprinterRom->GetROMPageRole(static_cast<uint8_t>(memory.GetROMPage()))
                                                     : std::string("none in window 0 (fast RAM or vROM)");
    }

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
    if (config.mem_model == MM_SPRINTER)
    {
        // The PLD maps the windows: kind and physical page (DeviceState::SprinterPaging)
        ss << "Sprinter Paging (PLD):" << NEWLINE << DeviceState::ToText(DeviceState::SprinterPaging(context), 1);
    }
    else if (config.mem_model != MM_SPECTRUM48)
    {
        ss << "Paging State:" << NEWLINE;
        ss << "  Port 0x7FFD:      0x" << std::hex << std::setw(2) << std::setfill('0') << (int)state.p7FFD << std::dec
           << NEWLINE;
        ss << "  RAM Bank 3:       " << (int)(state.p7FFD & 0x07) << NEWLINE;
        ss << "  Screen:           " << ((state.p7FFD & 0x08) ? "1 (Shadow)" : "0 (Normal)") << NEWLINE;
        ss << "  ROM Select:       " << ((state.p7FFD & 0x10) ? "1" : "0") << NEWLINE;
        ss << "  Paging Locked:    " << ((state.p7FFD & 0x20) ? "YES" : "NO") << NEWLINE;
        if (config.mem_model == MM_PROFI)
        {
            ss << "  Port 0xDFFD:      0x" << std::hex << std::setw(2) << std::setfill('0') << (int)state.pDFFD
               << std::dec << NEWLINE;
            ss << "  RAM High Bits:    " << (int)(state.pDFFD & 0x07) << NEWLINE;
            ss << "  SCO/WOROM/CPM/SCR: " << ((state.pDFFD & 0x08) ? "1" : "0") << "/"
               << ((state.pDFFD & 0x10) ? "1" : "0") << "/" << ((state.pDFFD & 0x20) ? "1" : "0") << "/"
               << ((state.pDFFD & 0x40) ? "1" : "0") << NEWLINE;
            ss << "  512x240 Video:    " << ((state.pDFFD & 0x80) ? "ON" : "OFF") << NEWLINE;
        }
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
    std::string model = Config::GetModelFullName(config.mem_model);
    ss << "Model: " << model << NEWLINE;
    ss << NEWLINE;

    // Show detailed Z80 address space to RAM page mapping
    ss << "Z80 Address Space → Physical RAM Pages:" << NEWLINE;
    ss << "=========================================" << NEWLINE;
    ss << NEWLINE;

    // Contended: the CPU waits for the video logic there (Core::IsSlotContended)
    auto contended = [context](uint8_t slot) {
        return (context->pCore && context->pCore->IsSlotContended(slot)) ? ", contended" : "";
    };

    // Bank 0 (might be ROM)
    if (memory.IsBank0ROM())
    {
        ss << "Bank 0 (0x0000-0x3FFF): ROM " << (int)memory.GetROMPage() << " (read-only)" << NEWLINE;
    }
    else
    {
        ss << "Bank 0 (0x0000-0x3FFF): RAM Page " << (int)memory.GetRAMPageForBank0() << " (read/write" << contended(0)
           << ")" << NEWLINE;
    }

    // Bank 1 (always RAM)
    ss << "Bank 1 (0x4000-0x7FFF): RAM Page " << (int)memory.GetRAMPageForBank1() << " (read/write" << contended(1) << ")"
       << NEWLINE;
    ss << "                        [Screen 0 location]" << NEWLINE;

    // Bank 2 (always RAM)
    ss << "Bank 2 (0x8000-0xBFFF): RAM Page " << (int)memory.GetRAMPageForBank2() << " (read/write" << contended(2) << ")"
       << NEWLINE;

    // Bank 3 (always RAM, pageable on 128K)
    ss << "Bank 3 (0xC000-0xFFFF): RAM Page " << (int)memory.GetRAMPageForBank3() << " (read/write" << contended(3) << ")"
       << NEWLINE;

    if (config.mem_model == MM_SPRINTER)
    {
        ss << NEWLINE << "Sprinter windows (PLD):" << NEWLINE << DeviceState::ToText(DeviceState::SprinterPaging(context), 1);
    }
    else if (config.mem_model != MM_SPECTRUM48)
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

    // Determine model and ROM pages
    std::string model = Config::GetModelFullName(config.mem_model);
    int totalROMPages = 1;
    switch (config.mem_model)
    {
        case MM_SPECTRUM128:
        case MM_PLUS2:
            totalROMPages = 2;
            break;
        case MM_PENTAGON:
        case MM_PLUS2A:
        case MM_PLUS3:
        case MM_SCORP:
        case MM_PROFSCORP:
        case MM_ATM3:
        case MM_ATM710:
        case MM_ATM450:
        case MM_PROFI:
            totalROMPages = 4;
            break;
        case MM_SPRINTER:
            totalROMPages = 16;  // the 256 KB flash (Sprinter bios-versions.md §2)
            break;
        default:
            totalROMPages = 1;
            break;
    }

    // Machines whose ROM image holds more pages than the four standard slots
    // (ZX-Evo 512 KB = 32 pages) report what is actually loaded
    if (ROM* loadedRom = context->pCore ? context->pCore->GetROM() : nullptr;
        loadedRom && loadedRom->GetROMBanksLoaded() > totalROMPages)
        totalROMPages = loadedRom->GetROMBanksLoaded();

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
    else if (config.mem_model == MM_SPECTRUM128 || config.mem_model == MM_PLUS2)
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
    else if (config.mem_model == MM_PROFI)
    {
        ss << "  Page 0: SYS/Menu ROM " << ((memory.GetROMPage() == 0) ? "[ACTIVE]" : "") << NEWLINE;
        ss << "  Page 1: TR-DOS ROM " << ((memory.GetROMPage() == 1) ? "[ACTIVE]" : "") << NEWLINE;
        ss << "  Page 2: 128K Editor + STS Monitor ROM " << ((memory.GetROMPage() == 2) ? "[ACTIVE]" : "") << NEWLINE;
        ss << "  Page 3: 48K BASIC ROM " << ((memory.GetROMPage() == 3) ? "[ACTIVE]" : "") << NEWLINE;
    }
    else if (config.mem_model == MM_SPRINTER)
    {
        // Roles from the core layout table (ROM::GetROMPageRole), the same names /state/paging shows
        ROM* sprinterRom = context->pCore ? context->pCore->GetROM() : nullptr;
        for (int page = 0; page < totalROMPages && sprinterRom; page++)
            ss << "  Page " << page << ": " << sprinterRom->GetROMPageRole(static_cast<uint8_t>(page)) << " "
               << ((memory.IsBank0ROM() && memory.GetROMPage() == page) ? "[ACTIVE]" : "") << NEWLINE;
    }
    else if (config.mem_model == MM_PLUS3 || config.mem_model == MM_PLUS2A)
    {
        ss << "  Page 0: +3 Editor ROM " << ((memory.GetROMPage() == 0) ? "[ACTIVE]" : "") << NEWLINE;
        ss << "  Page 1: 128 BASIC Syntax ROM " << ((memory.GetROMPage() == 1) ? "[ACTIVE]" : "") << NEWLINE;
        ss << "  Page 2: +3DOS ROM " << ((memory.GetROMPage() == 2) ? "[ACTIVE]" : "") << NEWLINE;
        ss << "  Page 3: 48K BASIC ROM " << ((memory.GetROMPage() == 3) ? "[ACTIVE]" : "") << NEWLINE;
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

    if (config.mem_model != MM_SPECTRUM48 && config.mem_model != MM_SPRINTER)  // the Sprinter's #7FFD is in the PLD
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
    const bool hasGS = soundManager && soundManager->hasGeneralSound();
    const bool hasCovox = soundManager && soundManager->hasCovox();

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

void CLIProcessor::HandleStateAudioFM(const ClientSession& session, EmulatorContext* context, const std::string& chipArg)
{
    // Core DeviceState report (the same tree the WebAPI, Lua, Python and MCP return)
    std::stringstream ss;
    if (chipArg.empty())
    {
        ss << "TurboSound FM (2 x YM2203)" << NEWLINE << "==========================" << NEWLINE;
        ss << DeviceState::ToText(DeviceState::Fm(context));
    }
    else
    {
        int chip = -1;
        try { chip = std::stoi(chipArg); } catch (const std::exception&) { chip = -1; }
        ss << "TurboSound FM chip " << chipArg << NEWLINE << "=====================" << NEWLINE;
        ss << DeviceState::ToText(DeviceState::FmChip(context, chip));
    }
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateFdc(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    ss << "Beta Disk WD1793" << NEWLINE << "================" << NEWLINE;
    ss << DeviceState::ToText(DeviceState::Fdc(context));
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateContention(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    ss << "Memory contention" << NEWLINE << "=================" << NEWLINE;
    ss << DeviceState::ToText(DeviceState::Contention(context));
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

void CLIProcessor::HandleStateAudioCovox(const ClientSession& session, EmulatorContext* context)
{
    std::stringstream ss;
    ss << "Covox / SoundDrive State" << NEWLINE;
    ss << "========================" << NEWLINE;
    ss << NEWLINE;
    // One report for every interface (DeviceState::Covox)
    ss << DeviceState::ToText(DeviceState::Covox(context));
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleStateAudioChannels(const ClientSession& session, EmulatorContext* context)
{
    // One report for every interface (DeviceState::AudioChannels: the mixer devices included)
    std::stringstream ss;
    ss << "Audio Channels Mixer State" << NEWLINE;
    ss << "==========================" << NEWLINE;
    ss << DeviceState::ToText(DeviceState::AudioChannels(context));
    session.SendResponse(ss.str());
}

/// endregion </Audio State Commands>

/// endregion </State Inspection Commands>
