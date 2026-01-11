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

// HandleBreakpoint - lines 1363-1437
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

// HandleWatchpoint - lines 1470-1564
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

// HandlePortBreakpoint - lines 1566-1662
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

