// CLI Breakpoint Commands
// Extracted from cli-processor.cpp - 2026-01-08

#include "cli-processor.h"
#include "cli-callbacks.h"

#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/breakpoints/breakpointselection.h>
#include <debugger/debugmanager.h>
#include <debugger/keyboard/debugkeyboardmanager.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/platform.h>

#include <iostream>
#include <iomanip>
#include <sstream>


bool CLIProcessor::TakeCpuOption(const ClientSession& session, std::vector<std::string>& args, DebugCpuId& cpu) const
{
    cpu = DebugCpuId::Main;
    for (size_t i = 0; i < args.size(); i++)
    {
        if (args[i] != "--cpu")
            continue;
        if (i + 1 >= args.size() || !DebugManager::ParseCpuName(args[i + 1], cpu))
        {
            session.SendResponse("Error: --cpu needs main, gs or ms");
            return false;
        }
        args.erase(args.begin() + static_cast<std::ptrdiff_t>(i), args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
        break;
    }
    return true;
}

BreakpointManager* CLIProcessor::BreakpointsOf(Emulator& emulator, DebugCpuId cpu)
{
    EmulatorContext* context = emulator.GetContext();
    return context && context->pDebugManager ? context->pDebugManager->GetBreakpointsManager(cpu) : nullptr;
}

bool CLIProcessor::ParseBreakpointArgs(const std::vector<std::string>& args, size_t addressIndex, size_t flagsFrom,
                                       BreakpointSpec& spec, std::string& error) const
{
    // The address, or a range "A-B" (both ends inclusive)
    const std::string& target = args[addressIndex];
    const size_t dash = target.find('-', 1);
    uint16_t address = 0;
    if (!ParseAddress(dash == std::string::npos ? target : target.substr(0, dash), address))
    {
        error = "Invalid address '" + target + "' (0-65535, hex as 0x.., #.. or $..; a range as A-B)";
        return false;
    }
    spec.address = address;
    if (dash != std::string::npos)
    {
        uint16_t end = 0;
        if (!ParseAddress(target.substr(dash + 1), end))
        {
            error = "Invalid range end in '" + target + "'";
            return false;
        }
        spec.hasEnd = true;
        spec.addressEnd = end;
    }

    // Flags anywhere after the fixed arguments; the rest is the note. "if" takes the rest of the line as the condition
    std::string note;
    for (size_t i = flagsFrom; i < args.size(); i++)
    {
        const std::string& a = args[i];
        const bool hasValue = i + 1 < args.size();
        if (a == "if" || a == "--if")
        {
            for (size_t j = i + 1; j < args.size(); j++)
                spec.condition += (spec.condition.empty() ? "" : " ") + args[j];
            if (spec.condition.empty())
            {
                error = "if needs a condition (e.g. if A == 7 && M(5C3A) != 0)";
                return false;
            }
            break;
        }
        else if (a == "--dialect")
        {
            if (!hasValue || !BreakpointConditionProgram::ParseDialect(args[++i], spec.conditionDialect))
            {
                error = "--dialect needs unreal or c";
                return false;
            }
        }
        else if (a == "--page")
        {
            if (!hasValue || !BreakpointManager::ParsePageInto(args[++i], spec, error))
            {
                if (error.empty())
                    error = "--page needs a page: ramN, romN, cacheN or vramN (e.g. --page ram5)";
                return false;
            }
        }
        else if (a == "--slot-only")
            spec.slotOnly = true;
        else if (a == "--mask")
        {
            uint16_t mask = 0;
            if (!hasValue || !ParseAddress(args[++i], mask))
            {
                error = "--mask needs a 16-bit mask (e.g. --mask 0x00FF)";
                return false;
            }
            spec.portMask = mask;
        }
        else if (a == "--hc")
        {
            uint16_t hc = 0;
            if (!hasValue || !ParseAddress(args[++i], hc) || hc > BRK_BEAM_MAX_HC)
            {
                error = "--hc needs a pixel clock (0..511)";
                return false;
            }
            spec.beamHc = hc;
        }
        else if (a == "--via")
        {
            if (!hasValue || !BreakpointManager::ParseNextRegDoors(args[++i], spec.nrDoors, error))
            {
                if (error.empty())
                    error = "--via needs doors: nextreg, port, copper (comma separated)";
                return false;
            }
        }
        else if (a == "--unit")
        {
            uint16_t unit = 0;
            if (!hasValue || !ParseAddress(args[++i], unit) || unit > 0xFE)
            {
                error = "--unit needs the chip's number on its device (0, 1, ...)";
                return false;
            }
            spec.chipUnit = static_cast<uint8_t>(unit);
        }
        else if (a == "--value" || a == "--vmask")
        {
            uint16_t number = 0;
            if (!hasValue || !ParseAddress(args[++i], number) || number > 0xFF)
            {
                error = a + " needs a byte (0..255)";
                return false;
            }
            if (a == "--value")
            {
                spec.nrValue = static_cast<uint8_t>(number);
                if (spec.nrMask == 0)
                    spec.nrMask = 0xFF;
            }
            else
                spec.nrMask = static_cast<uint8_t>(number);
        }
        else if (a == "--group")
        {
            if (!hasValue)
            {
                error = "--group needs a group name";
                return false;
            }
            spec.group = args[++i];
        }
        else if (a == "--hits")
        {
            if (!hasValue || !BreakpointManager::ParseHitSpec(args[++i], spec.hitMode, spec.hitTarget, error))
            {
                if (error.empty())
                    error = "--hits needs N (the Nth hit), >=N (from the Nth on) or %N (every Nth)";
                return false;
            }
        }
        else
            note += (note.empty() ? "" : " ") + a;
    }
    spec.note = note;
    return true;
}

std::string CLIProcessor::AddBreakpointAndDescribe(BreakpointManager& manager, const BreakpointSpec& spec, const char* what)
{
    std::string error;
    const uint16_t id = manager.AddBreakpoint(spec, error);
    if (id == BRK_INVALID)
        return std::string("Error: ") + error;
    onBreakpointsChanged();
    // The manager's own one-line description: address, range, access, page, mask, hit policy, note
    std::string line = manager.FormatBreakpointInfo(id);
    const size_t first = line.find_first_not_of(' ');
    return std::string(what) + " #" + std::to_string(id) + " set: " + (first == std::string::npos ? line : line.substr(first));
}

// HandleBreakpoint
void CLIProcessor::HandleBreakpoint(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    // --cpu main|gs|ms anywhere: whose breakpoints
    std::vector<std::string> args = rawArgs;
    DebugCpuId cpu = DebugCpuId::Main;
    if (!TakeCpuOption(session, args, cpu))
        return;
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    if (args.empty())
    {
        stringstream ss;
        ss << "Usage: bp <address>[-<end>] [--page ramN|romN|cacheN [--slot-only]] [--hits N|>=N|%N] [note]"
           << " [if <condition>]" << NEWLINE
           << "Sets an execution breakpoint: one address or a range. With a page it fires on that page through any"
           << NEWLINE << "slot that shows it (--slot-only: only through the slot of the address). --hits stops on the"
           << NEWLINE << "Nth hit only, from the Nth on (>=N) or on every Nth (%N); every hit is counted." << NEWLINE
           << "if: the rest of the line is a condition (Unreal language, numbers hex unless 123d; --dialect c" << NEWLINE
           << "before it: C precedence, decimal numbers); only the hits where it holds count for --hits." << NEWLINE
           << "Examples:" << NEWLINE << "  bp 0x1234            - Breakpoint at 0x1234" << NEWLINE
           << "  bp 0x8000 if A == 7 && M(5C3A) != 0 - Only when A is 7 and #5C3A is not 0 (hex in the condition)" << NEWLINE
           << "  bp 0x8000-0x80FF     - Anywhere in 0x8000-0x80FF" << NEWLINE
           << "  bp 0xC000 --page ram32 - RAM page 32 offset 0, in whatever slot it is mapped" << NEWLINE
           << "  bp 0x0038 --hits 50  - The 50th interrupt" << NEWLINE
           << "  bp 1234 Main loop    - With a note" << NEWLINE << "Use 'bplist' to view all breakpoints";
        session.SendResponse(ss.str());
        return;
    }
    BreakpointManager* bpManager = BreakpointsOf(*emulator, cpu);
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }
    BreakpointSpec spec;
    spec.access = BRK_MEM_EXECUTE;
    std::string error;
    if (!ParseBreakpointArgs(args, 0, 1, spec, error))
    {
        session.SendResponse("Error: " + error);
        return;
    }
    session.SendResponse(AddBreakpointAndDescribe(*bpManager, spec, "Breakpoint"));
}

// HandleBPList - lines 1439-1468
void CLIProcessor::HandleBPList(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    // --cpu main|gs|ms anywhere: whose breakpoints
    std::vector<std::string> args = rawArgs;
    DebugCpuId cpu = DebugCpuId::Main;
    if (!TakeCpuOption(session, args, cpu))
        return;
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    BreakpointManager* bpManager = BreakpointsOf(*emulator, cpu);
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

// HandleWatchpoint
void CLIProcessor::HandleWatchpoint(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    // --cpu main|gs|ms anywhere: whose breakpoints
    std::vector<std::string> args = rawArgs;
    DebugCpuId cpu = DebugCpuId::Main;
    if (!TakeCpuOption(session, args, cpu))
        return;
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    if (args.size() < 2)
    {
        std::stringstream ss;
        ss << "Usage: wp <address>[-<end>] <r|w|rw> [--page ramN|romN|cacheN [--slot-only]] [--hits N|>=N|%N] [note]"
           << NEWLINE << "Sets a memory watchpoint on one address or a range (with a page: on that page through any slot"
           << NEWLINE << "that shows it)." << NEWLINE << "Types:" << NEWLINE << "  r    - Watch for memory reads" << NEWLINE
           << "  w    - Watch for memory writes" << NEWLINE << "  rw   - Watch for both reads and writes" << NEWLINE
           << "Examples:" << NEWLINE << "  wp 0x1234 r          - Reads at 0x1234" << NEWLINE
           << "  wp 0x4000-0x57FF w   - Writes to the screen bitmap" << NEWLINE
           << "  wp 0x0100 w --page ram7 - Writes to RAM page 7 offset #0100, in whatever slot" << NEWLINE
           << "  wp #5C3A rw --hits >=10 IY area - From the 10th access on, with a note" << NEWLINE
           << "  wp 0x4000-0x57FF w if VAL != 0 - Writes of a non-zero byte (if <condition>, as for bp)";
        session.SendResponse(ss.str());
        return;
    }
    const std::string& typeStr = args[1];
    uint8_t memoryType = BRK_MEM_NONE;
    if (typeStr.find('r') != std::string::npos)
        memoryType |= BRK_MEM_READ;
    if (typeStr.find('w') != std::string::npos)
        memoryType |= BRK_MEM_WRITE;
    if (memoryType == BRK_MEM_NONE)
    {
        session.SendResponse("Invalid watchpoint type. Use 'r', 'w', or 'rw'.");
        return;
    }
    BreakpointManager* bpManager = BreakpointsOf(*emulator, cpu);
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }
    BreakpointSpec spec;
    spec.access = memoryType;
    std::string error;
    if (!ParseBreakpointArgs(args, 0, 2, spec, error))
    {
        session.SendResponse("Error: " + error);
        return;
    }
    session.SendResponse(AddBreakpointAndDescribe(*bpManager, spec, "Watchpoint"));
}

// HandlePortBreakpoint
void CLIProcessor::HandlePortBreakpoint(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    // --cpu main|gs|ms anywhere: whose breakpoints
    std::vector<std::string> args = rawArgs;
    DebugCpuId cpu = DebugCpuId::Main;
    if (!TakeCpuOption(session, args, cpu))
        return;
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    if (args.size() < 2)
    {
        std::stringstream ss;
        ss << "Usage: bport <port> <i|o|io> [--mask M] [--hits N|>=N|%N] [note]" << NEWLINE
           << "Sets a port breakpoint. With --mask it matches every port where (port & M) == (<port> & M)." << NEWLINE
           << "Types:" << NEWLINE << "  i    - Watch for port IN operations" << NEWLINE
           << "  o    - Watch for port OUT operations" << NEWLINE << "  io   - Watch for both IN and OUT operations"
           << NEWLINE << "Examples:" << NEWLINE << "  bport 0x7FFD o       - OUT to 0x7FFD" << NEWLINE
           << "  bport 0xFE i --mask 0x00FF - IN from #FE with any high byte (the keyboard)" << NEWLINE
           << "  bport 254 io Keyboard port - IN/OUT with a note" << NEWLINE
           << "  bport 0xFE o --mask 0xFF if (VAL & 7) == 2 - Border set to red (if <condition>, as for bp)";
        session.SendResponse(ss.str());
        return;
    }
    const std::string& typeStr = args[1];
    uint8_t ioType = BRK_IO_NONE;
    if (typeStr.find('i') != std::string::npos)
        ioType |= BRK_IO_IN;
    if (typeStr.find('o') != std::string::npos)
        ioType |= BRK_IO_OUT;
    if (ioType == BRK_IO_NONE)
    {
        session.SendResponse("Invalid port breakpoint type. Use 'i', 'o', or 'io'.");
        return;
    }
    BreakpointManager* bpManager = BreakpointsOf(*emulator, cpu);
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }
    BreakpointSpec spec;
    spec.type = BRK_IO;
    spec.access = ioType;
    std::string error;
    if (!ParseBreakpointArgs(args, 0, 2, spec, error))
    {
        session.SendResponse("Error: " + error);
        return;
    }
    session.SendResponse(AddBreakpointAndDescribe(*bpManager, spec, "Port breakpoint"));
}

// HandleNextRegBreakpoint: bnextreg <reg>[-<end>] <r|w|rw> [--via doors] [--value V [--vmask M]] [--hits ..] [note]
void CLIProcessor::HandleNextRegBreakpoint(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    if (args.size() < 2)
    {
        std::stringstream ss;
        ss << "Usage: bnextreg <reg>[-<end>] <r|w|rw> [--via nextreg,port,copper] [--value V [--vmask M]] [--hits N|>=N|%N] [note]"
           << NEWLINE << "Sets a ZX Next register breakpoint: a read of the register (through port #253B) or a write (by the"
           << NEWLINE << "NEXTREG instructions, OUT #253B or a copper MOVE). It stops after the instruction that did it;"
           << NEWLINE << "the copper's writes are caught up when the beam draws the line, so a copper stop is at the line's end." << NEWLINE
           << "Examples:" << NEWLINE << "  bnextreg 0x07 w                  - any write of the CPU speed register" << NEWLINE
           << "  bnextreg 0x40-0x44 w --via copper - the copper touching the palette registers" << NEWLINE
           << "  bnextreg 0x15 w --value 0x01 --vmask 0x01 - a write that sets bit 0";
        session.SendResponse(ss.str());
        return;
    }
    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }
    const std::string& typeStr = args[1];
    uint8_t access = BRK_IO_NONE;
    if (typeStr.find('r') != std::string::npos)
        access |= BRK_IO_IN;
    if (typeStr.find('w') != std::string::npos)
        access |= BRK_IO_OUT;
    if (access == BRK_IO_NONE)
    {
        session.SendResponse("Invalid access. Use 'r', 'w' or 'rw'.");
        return;
    }
    BreakpointSpec spec;
    spec.type = BRK_NEXTREG;
    spec.access = access;
    std::string error;
    if (!ParseBreakpointArgs(args, 0, 2, spec, error))
    {
        session.SendResponse("Error: " + error);
        return;
    }
    session.SendResponse(AddBreakpointAndDescribe(*bpManager, spec, "NextREG breakpoint"));
}

// HandleBeamBreakpoint: bbeam <line> [--hc N] [--hits ..] [note]
void CLIProcessor::HandleBeamBreakpoint(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    if (args.empty())
    {
        std::stringstream ss;
        ss << "Usage: bbeam <line> [--hc N] [--hits N|>=N|%N] [note]" << NEWLINE
           << "Stops when the beam passes the place: <line> is the copper's line counter (NR #64 applied: 0 is the paper's first"
           << NEWLINE << "line unless offset), --hc the pixel clock in the row (7 MHz, default 0). The machine stops at the end of the" << NEWLINE
           << "instruction the beam passed it in. ZX Next; on TS-Conf <line> is the raster line (0..319) and --hc the CPU tact" << NEWLINE
           << "in the line (0..223, 3.5 MHz)." << NEWLINE << "Examples:" << NEWLINE
           << "  bbeam 100            - the start of line 100" << NEWLINE << "  bbeam 99 --hc 255    - where the line interrupt of line 100 fires";
        session.SendResponse(ss.str());
        return;
    }
    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }
    BreakpointSpec spec;
    spec.type = BRK_BEAM;
    spec.access = BRK_IO_OUT;
    std::string error;
    if (!ParseBreakpointArgs(args, 0, 1, spec, error))
    {
        session.SendResponse("Error: " + error);
        return;
    }
    session.SendResponse(AddBreakpointAndDescribe(*bpManager, spec, "Beam breakpoint"));
}

// HandleCopperBreakpoint: bcopper <index>[-<end>] [--hits ..] [note]
void CLIProcessor::HandleCopperBreakpoint(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    if (args.empty())
    {
        std::stringstream ss;
        ss << "Usage: bcopper <index>[-<end>] [--hits N|>=N|%N] [note]" << NEWLINE
           << "Stops when the copper completes the instruction at that list index (0..1023): a WAIT that became true, or a MOVE." << NEWLINE
           << "ZX Next only." << NEWLINE << "Examples:" << NEWLINE << "  bcopper 12        - the 13th instruction of the list" << NEWLINE
           << "  bcopper 0-1023    - any instruction";
        session.SendResponse(ss.str());
        return;
    }
    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }
    BreakpointSpec spec;
    spec.type = BRK_COPPER;
    spec.access = BRK_IO_OUT;
    std::string error;
    if (!ParseBreakpointArgs(args, 0, 1, spec, error))
    {
        session.SendResponse("Error: " + error);
        return;
    }
    session.SendResponse(AddBreakpointAndDescribe(*bpManager, spec, "Copper breakpoint"));
}

// HandleMagicBreakpoint: bmagic <edff|dd01> [--hits ..] [note]
void CLIProcessor::HandleMagicBreakpoint(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    if (args.empty() || (args[0] != "edff" && args[0] != "dd01"))
    {
        std::stringstream ss;
        ss << "Usage: bmagic <edff|dd01> [--hits N|>=N|%N] [note]" << NEWLINE
           << "Stops after the CPU ran the magic opcode other emulators use as a debug trap: ED FF (jnext, ZEsarUX) or DD 01 (CSpect)." << NEWLINE
           << "The opcode executes as on the hardware (ED FF is a NOP, DD 01 is LD BC,nn). ZX Next only." << NEWLINE << "Examples:" << NEWLINE
           << "  bmagic edff" << NEWLINE << "  bmagic dd01 --hits >=3";
        session.SendResponse(ss.str());
        return;
    }
    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }
    std::vector<std::string> rest = args;
    rest[0] = args[0] == "edff" ? "0xEDFF" : "0xDD01";  // the opcode word is the breakpoint's address
    BreakpointSpec spec;
    spec.type = BRK_MAGIC;
    spec.access = BRK_IO_OUT;
    std::string error;
    if (!ParseBreakpointArgs(rest, 0, 1, spec, error))
    {
        session.SendResponse("Error: " + error);
        return;
    }
    session.SendResponse(AddBreakpointAndDescribe(*bpManager, spec, "Magic breakpoint"));
}

// HandleEventBreakpoint: bevent <kind> [--value V [--vmask M]] [--hits ..] [--group G] [note]
void CLIProcessor::HandleEventBreakpoint(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    std::vector<std::string> args = rawArgs;
    DebugCpuId cpu = DebugCpuId::Main;
    GSCardEvent kind = GSCardEvent::Count;
    if (!args.empty() && ParseGSCardEvent(args[0].c_str(), kind) && !GSCardEventIsHostSide(kind))
        cpu = DebugCpuId::GS;  // a card event is the card CPU's: no --cpu gs needed
    if (!TakeCpuOption(session, args, cpu))
        return;
    if (args.empty() || !ParseGSCardEvent(args[0].c_str(), kind))
    {
        std::stringstream ss;
        ss << "Usage: bevent <kind> [--value V [--vmask M]] [--hits N|>=N|%N] [--group G] [note]" << NEWLINE
           << "Stops on a General Sound / NeoGS protocol event (gs-debugger design §4) or a machine event. The card's events stop the" << NEWLINE
           << "machine at the card's next instruction boundary; the host's stop the main CPU after its access." << NEWLINE
           << "Card (the gs CPU): int, nmi, reset, page (a page register written), card_command, card_data (the card" << NEWLINE
           << "  read the mailbox), card_reply (it wrote the reply), dac_fetch (a #6000-#7FFF read latched a DAC)," << NEWLINE
           << "  clock_change (NeoGS GSCFG0: the byte), dma_start, dma_done (NeoGS DMA: the module 0 ZX, 1 SD, 2 MP3)," << NEWLINE
           << "  gscfg0 (NeoGS: every GSCFG0 write, the byte)" << NEWLINE
           << "Host (the main CPU): host_command (OUT #BB), host_data (OUT #B3), host_reply (IN #B3)" << NEWLINE
           << "Machine (the main CPU, ZX Next): cpu_int (the source: 0 line, 3-10 CTC, 11 ULA), cpu_reti, cpu_nmi" << NEWLINE
           << "  (0 M1 button, 1 drive button, 2 I/O trap; bit 7 refused), cpu_retn, machine_reset (1 hard)," << NEWLINE
           << "  divmmc_map (0 RST, 1 NMI, 2 #3Dxx, 3 ROM 3), divmmc_unmap, esxdos_call (the call byte)," << NEWLINE
           << "  zxndma_start, zxndma_done" << NEWLINE
           << "Machine (the main CPU, TS-Conf): cpu_int (int_sel: 0 frame, 1 line, 2 DMA, 3 wait port)," << NEWLINE
           << "  tsdma_start (the device), tsdma_done (1 the end, 0 aborted)" << NEWLINE
           << "--value / --vmask filter the event's byte." << NEWLINE << "Examples:" << NEWLINE
           << "  bevent card_command --value 0x0F  - the card takes command #0F" << NEWLINE
           << "  bevent host_command               - the host writes any command" << NEWLINE
           << "  bevent esxdos_call --value 0x9A   - a program opens a file (F_OPEN)";
        session.SendResponse(ss.str());
        return;
    }
    BreakpointManager* bpManager = BreakpointsOf(*emulator, cpu);
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }
    std::vector<std::string> rest = args;
    rest[0] = "0";  // the kind is the breakpoint's address; the flags follow
    BreakpointSpec spec;
    spec.type = BRK_EVENT;
    spec.access = 0;
    std::string error;
    if (!ParseBreakpointArgs(rest, 0, 1, spec, error))
    {
        session.SendResponse("Error: " + error);
        return;
    }
    spec.address = static_cast<uint16_t>(kind);
    session.SendResponse(AddBreakpointAndDescribe(*bpManager, spec, "Event breakpoint"));
}

// HandleChipRegisterBreakpoint: bchip <chip> <reg>[-<end>] [--unit N] [--value V [--vmask M]] [--hits ..] [note]
void CLIProcessor::HandleChipRegisterBreakpoint(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    SoundChipKind chip = SoundChipKind::Count;
    if (args.size() < 2 || !ParseSoundChipKind(args[0].c_str(), chip))
    {
        std::stringstream ss;
        ss << "Usage: bchip <chip> <reg>[-<end>] [--unit N] [--value V [--vmask M]] [--hits N|>=N|%N] [--group G] [note]"
           << NEWLINE << "Stops the main CPU after its OUT wrote a sound chip's register (platform-debugging P3)." << NEWLINE
           << "Chips: ay (AY / YM2149, the SSG of a YM2203: 0-15), fm (YM2203 FM: #10-#FF), saa (SAA1099: 0-#1F)," << NEWLINE
           << "  opl4 (MoonSound FM: bank 0 #000-#0FF, bank 1 #100-#1FF), opl4wave (0-#FF), dac (Covox / SounDrive:"
           << NEWLINE << "  the channel 0-3)" << NEWLINE
           << "--unit picks one chip of the kind (TurboSound / TS-FM 0-1, the Next's AYs 0-2); default any." << NEWLINE
           << "--value / --vmask filter the byte written." << NEWLINE << "Examples:" << NEWLINE
           << "  bchip ay 7 --vmask 0x3F --value 0x3F  - the mixer silences all channels" << NEWLINE
           << "  bchip ay 0-5 --unit 1                 - a tone period of the second AY" << NEWLINE
           << "  bchip opl4 0x105                      - the MoonSound NEW2 register";
        session.SendResponse(ss.str());
        return;
    }
    BreakpointManager* bpManager = BreakpointsOf(*emulator, DebugCpuId::Main);
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }
    BreakpointSpec spec;
    spec.type = BRK_CHIPREG;
    spec.access = 0;
    spec.chip = static_cast<uint8_t>(chip);
    std::vector<std::string> rest(args.begin() + 1, args.end());
    std::string error;
    if (!ParseBreakpointArgs(rest, 0, 1, spec, error))
    {
        session.SendResponse("Error: " + error);
        return;
    }
    session.SendResponse(AddBreakpointAndDescribe(*bpManager, spec, "Chip register breakpoint"));
}

// HandleBPHits: bphits reset [id]
void CLIProcessor::HandleBPHits(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    // --cpu main|gs|ms anywhere: whose breakpoints
    std::vector<std::string> args = rawArgs;
    DebugCpuId cpu = DebugCpuId::Main;
    if (!TakeCpuOption(session, args, cpu))
        return;
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    BreakpointManager* bpManager = BreakpointsOf(*emulator, cpu);
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }
    if (args.empty() || args[0] != "reset")
    {
        session.SendResponse(std::string("Usage: bphits reset [id]") + NEWLINE +
                             "Sets the hit counters back to 0 (one breakpoint, or all); bplist shows them.");
        return;
    }
    if (args.size() > 1)
    {
        uint16_t id = 0;
        if (!ParseAddress(args[1], id) || !bpManager->ResetHitCount(id))
        {
            session.SendResponse("Error: no breakpoint " + args[1]);
            return;
        }
        session.SendResponse("Hit counter of #" + std::to_string(id) + " reset");
        return;
    }
    bpManager->ResetAllHitCounts();
    session.SendResponse("All hit counters reset");
}

// HandleBPClear - lines 1664-1836
void CLIProcessor::HandleBPClear(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    // --cpu main|gs|ms anywhere: whose breakpoints
    std::vector<std::string> args = rawArgs;
    DebugCpuId cpu = DebugCpuId::Main;
    if (!TakeCpuOption(session, args, cpu))
        return;
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    BreakpointManager* bpManager = BreakpointsOf(*emulator, cpu);
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
           << NEWLINE << "  group <name> - Clear all breakpoints in a group" << NEWLINE
           << "  nextreg | nextreg_read | nextreg_write | beam | copper | magic | keyboard | group:<name>";
        session.SendResponse(ss.str());

        return;
    }

    std::string option = args[0];

    if (option == "all")
    {
        bpManager->ClearUserBreakpoints();
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
            if (!ApplyBreakpointSelection(session, *bpManager, option, "bpclear"))
                session.SendResponse("Invalid option or breakpoint ID. Use 'bpclear' for help.");
        }
    }
}

// HandleBPGroup - lines 1838-1940
void CLIProcessor::HandleBPGroup(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    // --cpu main|gs|ms anywhere: whose breakpoints
    std::vector<std::string> args = rawArgs;
    DebugCpuId cpu = DebugCpuId::Main;
    if (!TakeCpuOption(session, args, cpu))
        return;
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    BreakpointManager* bpManager = BreakpointsOf(*emulator, cpu);
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
           << "  remove <id>      - Remove a breakpoint from its group (sets to 'default')" << NEWLINE
           << "  on <name>        - Enable the breakpoints of a group" << NEWLINE
           << "  off <name>       - Disable the breakpoints of a group" << NEWLINE
           << "  toggle <name>    - All of the group on (when one is off), else all off";
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
    else if ((command == "on" || command == "off" || command == "toggle") && args.size() > 1)
    {
        const BreakpointSelection selection = BreakpointSelection::Group(args[1]);
        bool active = command == "on";
        const BreakpointSelection::Outcome outcome =
            command == "toggle" ? selection.Toggle(*bpManager, active) : selection.SetActive(*bpManager, active);
        if (outcome.selected == 0)
        {
            session.SendResponse("No breakpoint in group '" + args[1] + "'");
            return;
        }
        onBreakpointsChanged();
        session.SendResponse("Group '" + args[1] + "' " + (active ? "on" : "off") + " (" + std::to_string(outcome.selected) +
                             " breakpoints)");
    }
    else
    {
        session.SendResponse("Invalid command. Use 'bpgroup' for help.");
    }
}

// HandleBPActivate - lines 1942-2073
void CLIProcessor::HandleBPActivate(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    // --cpu main|gs|ms anywhere: whose breakpoints
    std::vector<std::string> args = rawArgs;
    DebugCpuId cpu = DebugCpuId::Main;
    if (!TakeCpuOption(session, args, cpu))
        return;
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    BreakpointManager* bpManager = BreakpointsOf(*emulator, cpu);
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
           << "  group <name> - Activate all breakpoints in a group" << NEWLINE
           << "  nextreg | nextreg_read | nextreg_write | beam | copper | magic | keyboard | group:<name>";
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
            if (!ApplyBreakpointSelection(session, *bpManager, option, "bpon"))
                session.SendResponse("Invalid option or breakpoint ID. Use 'bpon' for help.");
        }
    }
}

// HandleBPDeactivate - lines 2075-2206
void CLIProcessor::HandleBPDeactivate(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    // --cpu main|gs|ms anywhere: whose breakpoints
    std::vector<std::string> args = rawArgs;
    DebugCpuId cpu = DebugCpuId::Main;
    if (!TakeCpuOption(session, args, cpu))
        return;
    auto emulator = GetSelectedEmulator(session);

    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }

    BreakpointManager* bpManager = BreakpointsOf(*emulator, cpu);
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
           << "  group <name> - Deactivate all breakpoints in a group" << NEWLINE
           << "  nextreg | nextreg_read | nextreg_write | beam | copper | magic | keyboard | group:<name>";
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
            if (!ApplyBreakpointSelection(session, *bpManager, option, "bpoff"))
                session.SendResponse("Invalid option or breakpoint ID. Use 'bpoff' for help.");
        }
    }
}


bool CLIProcessor::ApplyBreakpointSelection(const ClientSession& session, BreakpointManager& manager, const std::string& text,
                                            const char* verb)
{
    BreakpointSelection selection;
    std::string error;
    if (!BreakpointSelection::Parse(text, selection, error) || selection.GetKind() == BreakpointSelection::Kind::Id)
        return false;
    const std::string what = std::string(verb);
    bool active = what == "bpon";
    BreakpointSelection::Outcome outcome;
    if (what == "bpclear")
        outcome = selection.Remove(manager);
    else if (what == "bptoggle")
        outcome = selection.Toggle(manager, active);
    else
        outcome = selection.SetActive(manager, active);
    if (outcome.changed > 0)
        onBreakpointsChanged();
    session.SendResponse(std::to_string(outcome.selected) + " breakpoint(s) in '" + selection.Name() + "' " +
                         (what == "bpclear" ? "cleared" : active ? "activated" : "deactivated"));
    return true;
}

// HandleBPToggle: bptoggle [all | <id> | <kind> | group:<name>]
void CLIProcessor::HandleBPToggle(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    // --cpu main|gs|ms anywhere: whose breakpoints
    std::vector<std::string> args = rawArgs;
    DebugCpuId cpu = DebugCpuId::Main;
    if (!TakeCpuOption(session, args, cpu))
        return;
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    BreakpointManager* bpManager = BreakpointsOf(*emulator, cpu);
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }
    if (!args.empty() && (args[0] == "help" || args[0] == "-h"))
    {
        session.SendResponse(std::string("Usage: bptoggle [all | <id> | <kind> | group:<name>]") + NEWLINE +
                             "All of the selection on when one of it is off, else all off. Kinds: " +
                             BreakpointSelection::KindNames());
        return;
    }
    BreakpointSelection selection;
    std::string error;
    if (!BreakpointSelection::Parse(args.empty() ? std::string("all") : args[0], selection, error))
    {
        session.SendResponse("Error: " + error);
        return;
    }
    bool active = false;
    const BreakpointSelection::Outcome outcome = selection.Toggle(*bpManager, active);
    if (outcome.selected == 0)
    {
        session.SendResponse("No breakpoint in '" + selection.Name() + "'");
        return;
    }
    onBreakpointsChanged();
    session.SendResponse(std::to_string(outcome.selected) + " breakpoint(s) in '" + selection.Name() + "' " +
                         (active ? "activated" : "deactivated"));
}

// HandleKeyBreakpoint: bkey <key|any> [press|release|both] [--hits N] [--group G] [note]
void CLIProcessor::HandleKeyBreakpoint(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    if (args.empty())
    {
        std::stringstream ss;
        ss << "Usage: bkey <key|any> [press|release|both] [--hits N|>=N|%N] [--group G] [note]" << NEWLINE
           << "Stops on the first keyboard read (IN #xxFE) that sees the key go down (press, default) or up (release):"
           << NEWLINE << "the program's key handling, not the host's key event. Keys: space, enter, caps, symbol, a..z, 0..9"
           << NEWLINE << "Examples:" << NEWLINE << "  bkey space          - when the program sees SPACE pressed" << NEWLINE
           << "  bkey any release    - when it sees any key released";
        session.SendResponse(ss.str());
        return;
    }
    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }
    BreakpointSpec spec;
    spec.type = BRK_KEYBOARD;
    spec.access = BRK_KEY_PRESS;
    if (args[0] == "any")
        spec.address = BRK_KEY_ANY;
    else
    {
        const ZXKeysEnum key = DebugKeyboardManager::ResolveKeyName(args[0]);
        if (key == ZXKEY_NONE)
        {
            session.SendResponse("Error: unknown key '" + args[0] + "' (key list shows the names)");
            return;
        }
        spec.address = static_cast<uint16_t>(key);
    }
    size_t from = 1;
    if (args.size() > 1 && (args[1] == "press" || args[1] == "release" || args[1] == "both"))
    {
        spec.access = args[1] == "press" ? BRK_KEY_PRESS : args[1] == "release" ? BRK_KEY_RELEASE
                                                                                : (BRK_KEY_PRESS | BRK_KEY_RELEASE);
        from = 2;
    }
    std::string note;
    for (size_t i = from; i < args.size(); i++)
    {
        std::string error;
        if (args[i] == "--hits" && i + 1 < args.size())
        {
            if (!BreakpointManager::ParseHitSpec(args[++i], spec.hitMode, spec.hitTarget, error))
            {
                session.SendResponse("Error: " + error);
                return;
            }
        }
        else if (args[i] == "--group" && i + 1 < args.size())
            spec.group = args[++i];
        else
            note += (note.empty() ? "" : " ") + args[i];
    }
    spec.note = note;
    session.SendResponse(AddBreakpointAndDescribe(*bpManager, spec, "Keyboard breakpoint"));
}

// HandleBPCondition: bpcond <id> [--dialect c] [condition] (no condition: clear)
void CLIProcessor::HandleBPCondition(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    // --cpu main|gs|ms anywhere: whose breakpoints
    std::vector<std::string> args = rawArgs;
    DebugCpuId cpu = DebugCpuId::Main;
    if (!TakeCpuOption(session, args, cpu))
        return;
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    BreakpointManager* bpManager = BreakpointsOf(*emulator, cpu);
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }
    uint16_t id = 0;
    if (args.empty() || !ParseAddress(args[0], id))
    {
        session.SendResponse("Usage: bpcond <id> [--dialect c] [condition]   (no condition clears it)" + std::string(NEWLINE) +
                             "  bpcond 3 B == 0 && HL >= 4000");
        return;
    }
    size_t from = 1;
    BreakpointConditionDialect dialect = BreakpointConditionDialect::Unreal;
    if (args.size() > 2 && args[1] == "--dialect")
    {
        if (!BreakpointConditionProgram::ParseDialect(args[2], dialect))
        {
            session.SendResponse("Error: --dialect needs unreal or c");
            return;
        }
        from = 3;
    }
    std::string condition;
    for (size_t i = from; i < args.size(); i++)
        condition += (condition.empty() ? "" : " ") + args[i];
    std::string error;
    if (!bpManager->SetBreakpointCondition(id, condition, error, dialect))
    {
        session.SendResponse("Error: " + error);
        return;
    }
    onBreakpointsChanged();
    session.SendResponse(condition.empty() ? "Breakpoint #" + std::to_string(id) + " condition cleared"
                                           : "Breakpoint #" + std::to_string(id) + " condition set:" +
                                                 bpManager->FormatBreakpointInfo(id));
}

// HandleBPNote: bpnote <id> [text] (no text: clear)
void CLIProcessor::HandleBPNote(const ClientSession& session, const std::vector<std::string>& rawArgs)
{
    // --cpu main|gs|ms anywhere: whose breakpoints
    std::vector<std::string> args = rawArgs;
    DebugCpuId cpu = DebugCpuId::Main;
    if (!TakeCpuOption(session, args, cpu))
        return;
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    BreakpointManager* bpManager = BreakpointsOf(*emulator, cpu);
    if (!bpManager)
    {
        session.SendResponse("Breakpoint manager not available");
        return;
    }
    uint16_t id = 0;
    if (args.empty() || !ParseAddress(args[0], id))
    {
        session.SendResponse("Usage: bpnote <id> [text]   (no text clears the note)");
        return;
    }
    std::string note;
    for (size_t i = 1; i < args.size(); i++)
        note += (note.empty() ? "" : " ") + args[i];
    if (!bpManager->SetBreakpointNote(id, note))
    {
        session.SendResponse("No breakpoint found with ID " + std::to_string(id));
        return;
    }
    onBreakpointsChanged();
    session.SendResponse("Breakpoint #" + std::to_string(id) + (note.empty() ? " note cleared" : " note set"));
}

// HandleCallback: callback list | resume <id> | remove <id> | policy <id> <suspend|disable|ignore|unpaced> (the scripts' callbacks)
void CLIProcessor::HandleCallback(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    EmulatorContext* context = emulator->GetContext();
    ScriptCallbackManager* callbacks =
        context && context->pDebugManager ? context->pDebugManager->GetScriptCallbacks() : nullptr;
    session.SendResponse(ClientSession::FormatForTerminal(CliCallbacks::Run(callbacks, args)));
}
