// CLI Breakpoint Commands
// Extracted from cli-processor.cpp - 2026-01-08

#include "cli-processor.h"

#include <debugger/breakpoints/breakpointmanager.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/platform.h>

#include <iostream>
#include <iomanip>
#include <sstream>


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

    // Flags anywhere after the fixed arguments; the rest is the note
    std::string note;
    for (size_t i = flagsFrom; i < args.size(); i++)
    {
        const std::string& a = args[i];
        const bool hasValue = i + 1 < args.size();
        if (a == "--page")
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
        ss << "Usage: bp <address>[-<end>] [--page ramN|romN|cacheN [--slot-only]] [--hits N|>=N|%N] [note]" << NEWLINE
           << "Sets an execution breakpoint: one address or a range. With a page it fires on that page through any"
           << NEWLINE << "slot that shows it (--slot-only: only through the slot of the address). --hits stops on the"
           << NEWLINE << "Nth hit only, from the Nth on (>=N) or on every Nth (%N); every hit is counted." << NEWLINE
           << "Examples:" << NEWLINE << "  bp 0x1234            - Breakpoint at 0x1234" << NEWLINE
           << "  bp 0x8000-0x80FF     - Anywhere in 0x8000-0x80FF" << NEWLINE
           << "  bp 0xC000 --page ram32 - RAM page 32 offset 0, in whatever slot it is mapped" << NEWLINE
           << "  bp 0x0038 --hits 50  - The 50th interrupt" << NEWLINE
           << "  bp 1234 Main loop    - With a note" << NEWLINE << "Use 'bplist' to view all breakpoints";
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

// HandleWatchpoint
void CLIProcessor::HandleWatchpoint(const ClientSession& session, const std::vector<std::string>& args)
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
        ss << "Usage: wp <address>[-<end>] <r|w|rw> [--page ramN|romN|cacheN [--slot-only]] [--hits N|>=N|%N] [note]"
           << NEWLINE << "Sets a memory watchpoint on one address or a range (with a page: on that page through any slot"
           << NEWLINE << "that shows it)." << NEWLINE << "Types:" << NEWLINE << "  r    - Watch for memory reads" << NEWLINE
           << "  w    - Watch for memory writes" << NEWLINE << "  rw   - Watch for both reads and writes" << NEWLINE
           << "Examples:" << NEWLINE << "  wp 0x1234 r          - Reads at 0x1234" << NEWLINE
           << "  wp 0x4000-0x57FF w   - Writes to the screen bitmap" << NEWLINE
           << "  wp 0x0100 w --page ram7 - Writes to RAM page 7 offset #0100, in whatever slot" << NEWLINE
           << "  wp #5C3A rw --hits >=10 IY area - From the 10th access on, with a note";
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
    BreakpointManager* bpManager = emulator->GetBreakpointManager();
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
void CLIProcessor::HandlePortBreakpoint(const ClientSession& session, const std::vector<std::string>& args)
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
        ss << "Usage: bport <port> <i|o|io> [--mask M] [--hits N|>=N|%N] [note]" << NEWLINE
           << "Sets a port breakpoint. With --mask it matches every port where (port & M) == (<port> & M)." << NEWLINE
           << "Types:" << NEWLINE << "  i    - Watch for port IN operations" << NEWLINE
           << "  o    - Watch for port OUT operations" << NEWLINE << "  io   - Watch for both IN and OUT operations"
           << NEWLINE << "Examples:" << NEWLINE << "  bport 0x7FFD o       - OUT to 0x7FFD" << NEWLINE
           << "  bport 0xFE i --mask 0x00FF - IN from #FE with any high byte (the keyboard)" << NEWLINE
           << "  bport 254 io Keyboard port - IN/OUT with a note";
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
    BreakpointManager* bpManager = emulator->GetBreakpointManager();
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

// HandleBPHits: bphits reset [id]
void CLIProcessor::HandleBPHits(const ClientSession& session, const std::vector<std::string>& args)
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

// HandleBPGroup - lines 1838-1940
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

// HandleBPActivate - lines 1942-2073
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

// HandleBPDeactivate - lines 2075-2206
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

