#include "stdafx.h"

#include "breakpointmanager.h"

#include "common/collectionhelper.h"
#include "common/stringhelper.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"

/// region <Constructors / destructors>

/// @brief Constructs a new Breakpoint Manager with the given emulator context
/// @param context Pointer to the emulator context
BreakpointManager::BreakpointManager(EmulatorContext* context)
{
    _context = context;
    _logger = _context->pModuleLogger;
}

/// @brief Destroys the Breakpoint Manager and cleans up all breakpoints
BreakpointManager::~BreakpointManager()
{
    ClearBreakpoints();

    _context = nullptr;
}

/// endregion </Constructors / destructors>

/// region <Management methods>

/// @brief Clears all breakpoints from the manager
/// 
/// Removes all breakpoints from internal storage maps, effectively
/// clearing all breakpoints that were previously set.
void BreakpointManager::ClearBreakpoints()
{
    _breakpointMapByAddress.clear();
    _breakpointMapByPort.clear();
    _breakpointMapByID.clear();
}

/// @brief Adds a new breakpoint with the given descriptor
/// 
/// @param descriptor Pointer to a BreakpointDescriptor containing breakpoint configuration
/// @return uint16_t The ID of the newly created breakpoint, or BRK_INVALID on failure
/// 
/// @note The descriptor object is owned by the BreakpointManager after this call
/// @throws std::logic_error if an invalid breakpoint type is specified (debug builds only)
uint16_t BreakpointManager::AddBreakpoint(BreakpointDescriptor* descriptor)
{
    uint16_t result = BRK_INVALID;

    /// region <Input parameter(s) validation>
    if (descriptor == nullptr)
    {
#ifdef _DEBUG
        MLOGWARNING("BreakpointManager::AddBreakpoint - null descriptor passed as parameter");
#endif // _DEBUG

        return result;
    }
    /// endregion </Input parameter(s) validation>

    switch (descriptor->type)
    {
        case BRK_MEMORY:
            result = AddMemoryBreakpoint(descriptor);
            break;
        case BRK_IO:
            result = AddPortBreakpoint(descriptor);
            break;
        default:
#ifdef _DEBUG
        {
            std::string message = StringHelper::Format("BreakpointManager::AddBreakpoint - invalid breakpoint type: %d", descriptor->type);
            throw std::logic_error(message);
        }
#endif // _DEBUG
            break;
    }

    return result;
}

/// @brief Removes a breakpoint using its descriptor
/// 
/// @param descriptor Pointer to the breakpoint descriptor to remove
/// @return bool True if the breakpoint was found and removed, false otherwise
/// 
/// @note This method is currently not implemented and will throw an exception
/// @throws std::logic_error Always throws as this method is not implemented
bool BreakpointManager::RemoveBreakpoint(BreakpointDescriptor* descriptor)
{
    (void)descriptor;

    bool result = false;

    throw std::logic_error("BreakpointManager::RemoveBreakpoint(BreakpointDescriptor* descriptor) - Not implemented");

    return result;
}

/// @brief Removes a breakpoint by its unique ID
/// 
/// @param breakpointID The ID of the breakpoint to remove
/// @return bool True if the breakpoint was found and removed, false otherwise
/// 
/// This method removes the breakpoint from all internal storage maps and
/// frees the associated descriptor memory.
bool BreakpointManager::RemoveBreakpointByID(uint16_t breakpointID)
{
    auto it = _breakpointMapByID.find(breakpointID);
    if (it == _breakpointMapByID.end())
        return false;

    BreakpointDescriptor* breakpoint = it->second;

    // Remove from type-specific maps
    switch (breakpoint->type)
    {
        case BRK_MEMORY:
            _breakpointMapByAddress.erase(breakpoint->keyAddress);
            break;
        case BRK_IO:
            _breakpointMapByPort.erase(breakpoint->z80address);
            break;
        default:
            break;
    }

    // Remove from ID map and delete the descriptor
    _breakpointMapByID.erase(it);
    delete breakpoint;

    // Update the next ID to be one more than the current maximum ID
    if (_breakpointMapByID.empty())
    {
        _breakpointIDSeq = 1;
    }
    else
    {
        // Since the map is ordered by key, the last element has the highest ID
        _breakpointIDSeq = _breakpointMapByID.rbegin()->first + 1;
    }

    return true;
}

size_t BreakpointManager::GetBreakpointsCount()
{
    return _breakpointMapByID.size();
}

/// endregion </Management methods>

/// region <Management assistance methods>

/// @brief Adds an execution breakpoint at the specified Z80 address
/// 
/// @param z80address The 16-bit Z80 memory address where the breakpoint should be set
/// @return uint16_t The ID of the newly created breakpoint, or BRK_INVALID on failure
/// 
/// Creates a breakpoint that triggers when the Z80 executes the instruction
/// at the specified memory address.
uint16_t BreakpointManager::AddExecutionBreakpoint(uint16_t z80address)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_EXECUTE;
    breakpoint->z80address = z80address;

    result = AddBreakpoint(breakpoint);

    return result;
}

/// @brief Adds a memory read breakpoint at the specified Z80 address
/// 
/// @param z80address The 16-bit Z80 memory address to monitor for read operations
/// @return uint16_t The ID of the newly created breakpoint, or BRK_INVALID on failure
/// 
/// Creates a breakpoint that triggers when the Z80 reads from the specified
/// memory address.
uint16_t BreakpointManager::AddMemReadBreakpoint(uint16_t z80address)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_READ;
    breakpoint->z80address = z80address;

    result = AddBreakpoint(breakpoint);

    return result;
}

/// @brief Adds a memory write breakpoint at the specified Z80 address
/// 
/// @param z80address The 16-bit Z80 memory address to monitor for write operations
/// @return uint16_t The ID of the newly created breakpoint, or BRK_INVALID on failure
/// 
/// Creates a breakpoint that triggers when the Z80 writes to the specified
/// memory address.
uint16_t BreakpointManager::AddMemWriteBreakpoint(uint16_t z80address)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_WRITE;
    breakpoint->z80address = z80address;

    result = AddBreakpoint(breakpoint);

    return result;
}

/// @brief Adds an input port breakpoint for the specified Z80 I/O port
/// 
/// @param port The 16-bit Z80 I/O port address to monitor for input operations
/// @return uint16_t The ID of the newly created breakpoint, or BRK_INVALID on failure
/// 
/// Creates a breakpoint that triggers when the Z80 performs an IN instruction
/// on the specified I/O port.
uint16_t BreakpointManager::AddPortInBreakpoint(uint16_t port)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_IO;
    breakpoint->ioType = BRK_IO_IN;
    breakpoint->z80address = port;

    result = AddBreakpoint(breakpoint);

    return result;
}

/// @brief Adds an output port breakpoint for the specified Z80 I/O port
/// 
/// @param port The 16-bit Z80 I/O port address to monitor for output operations
/// @return uint16_t The ID of the newly created breakpoint, or BRK_INVALID on failure
/// 
/// Creates a breakpoint that triggers when the Z80 performs an OUT instruction
/// on the specified I/O port.
uint16_t BreakpointManager::AddPortOutBreakpoint(uint16_t port)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_IO;
    breakpoint->ioType = BRK_IO_OUT;
    breakpoint->z80address = port;

    result = AddBreakpoint(breakpoint);

    return result;
}

// Combined breakpoint types

/// @brief Adds a memory breakpoint with combined access types
/// 
/// @param z80address The 16-bit Z80 memory address for the breakpoint
/// @param memoryType Bitmask of BRK_MEM_* flags specifying the access types to break on
/// @return uint16_t The ID of the newly created breakpoint, or BRK_INVALID on failure
/// 
/// Creates a memory breakpoint that can trigger on multiple access types (read/write/execute).
/// The memoryType parameter should be a bitwise OR of the desired BRK_MEM_* flags.
uint16_t BreakpointManager::AddCombinedMemoryBreakpoint(uint16_t z80address, uint8_t memoryType)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = memoryType;
    breakpoint->z80address = z80address;

    result = AddBreakpoint(breakpoint);

    return result;
}

/// @brief Adds an I/O port breakpoint with combined access types
/// 
/// @param port The 16-bit Z80 I/O port address for the breakpoint
/// @param ioType Bitmask of BRK_IO_* flags specifying the I/O operations to break on
/// @return uint16_t The ID of the newly created breakpoint, or BRK_INVALID on failure
/// 
/// Creates an I/O port breakpoint that can trigger on input, output, or both operations.
/// The ioType parameter should be a bitwise OR of the desired BRK_IO_* flags.
uint16_t BreakpointManager::AddCombinedPortBreakpoint(uint16_t port, uint8_t ioType)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_IO;
    breakpoint->ioType = ioType;
    breakpoint->z80address = port;

    result = AddBreakpoint(breakpoint);

    return result;
}

// Breakpoint listing

// Retrieves a reference to the map containing all breakpoints
// 
// @return const BreakpointMapByID& Reference to the internal breakpoint map
// 
// This method provides direct access to the internal breakpoint storage.
// Use with caution as modifications to the map may break internal consistency.
const BreakpointMapByID& BreakpointManager::GetAllBreakpoints() const
{
    return _breakpointMapByID;
}

/// @brief Formats information about a specific breakpoint as a human-readable string
/// 
/// @param breakpointID The ID of the breakpoint to format
/// @return std::string A formatted string containing breakpoint details
/// 
/// The output format is designed to be displayed in a fixed-width console and includes:
/// - Breakpoint ID
/// - Type (Memory/Port/Keyboard)
/// - Address/Port
/// - Access type (R/W/X for memory, I/O for ports)
/// - Status (Active/Inactive)
/// - Optional note
std::string BreakpointManager::FormatBreakpointInfo(uint16_t breakpointID) const
{
    std::ostringstream oss;
    
    if (_breakpointMapByID.find(breakpointID) != _breakpointMapByID.end())
    {
        const BreakpointDescriptor* bp = _breakpointMapByID.at(breakpointID);
        
        // Format ID - align right with width 4
        oss << std::setw(4) << std::right << breakpointID << " ";
        
        // Format type - fixed width 10 characters
        std::string typeStr;
        switch (bp->type)
        {
            case BRK_MEMORY:
                typeStr = "Memory";
                break;
            case BRK_IO:
                typeStr = "Port";
                break;
            case BRK_KEYBOARD:
                typeStr = "Keyboard";
                break;
            default:
                typeStr = "Unknown";
                break;
        }
        oss << std::setw(10) << std::left << typeStr << " ";
        
        // Format address - fixed width 8 characters
        if (bp->type == BRK_MEMORY || bp->type == BRK_IO)
        {
            oss << "0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << bp->z80address << std::setfill(' ');
            // Ensure exact 8 characters width
            oss << std::string(8 - 6, ' '); // 6 = "0x" + 4 hex digits
        }
        else
        {
            oss << std::setw(8) << std::left << "N/A";
        }
        
        // Format access type - exactly matching header width
        std::string access;
        if (bp->type == BRK_MEMORY)
        {
            if (bp->memoryType & BRK_MEM_READ) access += "R";
            if (bp->memoryType & BRK_MEM_WRITE) access += "W";
            if (bp->memoryType & BRK_MEM_EXECUTE) access += "X";
        }
        else if (bp->type == BRK_IO)
        {
            if (bp->ioType & BRK_IO_IN) access += "I";
            if (bp->ioType & BRK_IO_OUT) access += "O";
        }
        // Exactly 5 characters for "Access" column
        oss << " " << std::setw(5) << std::left << access;
        
        // Format status - exactly matching header width
        oss << " " << std::setw(8) << std::left << (bp->active ? "Active" : "Inactive");
        
        // Format note if available
        if (!bp->note.empty())
        {
            oss << " - " << bp->note;
        }
    }
    else
    {
        oss << "Breakpoint #" << breakpointID << " not found";
    }
    
    return oss.str();
}

/// @brief Generates a formatted string listing all breakpoints
/// 
/// @param newline The line ending sequence to use (e.g., "\n" or "\r\n")
/// @return std::string A formatted string containing information about all breakpoints
/// 
/// The output includes a header row followed by one row per breakpoint.
/// If no breakpoints are set, returns a message indicating this.
std::string BreakpointManager::GetBreakpointListAsString(const std::string& newline) const
{
    std::ostringstream oss;

    if (_breakpointMapByID.empty())
    {
        oss << "No breakpoints set" << newline;
        return oss.str();
    }

    // Header
    oss << "ID   Type       Address  Access Status   Note" << newline;
    oss << "---- ---------- -------- ----- -------- ---------------" << newline;

    // List all breakpoints
    for (const auto& pair : _breakpointMapByID)
    {
        oss << FormatBreakpointInfo(pair.first) << newline;
    }

    return oss.str();
}

// Breakpoint activation/deactivation

/// @brief Activates a breakpoint by its ID
/// 
/// @param breakpointID The ID of the breakpoint to activate
/// @return bool True if the breakpoint was found and activated, false otherwise
/// 
/// An active breakpoint will trigger when its conditions are met during emulation.
bool BreakpointManager::ActivateBreakpoint(uint16_t breakpointID)
{
    if (_breakpointMapByID.find(breakpointID) != _breakpointMapByID.end())
    {
        BreakpointDescriptor* bp = _breakpointMapByID[breakpointID];
        bp->active = true;
        return true;
    }
    return false;
}

/// @brief Deactivates a breakpoint by its ID
/// 
/// @param breakpointID The ID of the breakpoint to deactivate
/// @return bool True if the breakpoint was found and deactivated, false otherwise
/// 
/// A deactivated breakpoint will not trigger when its conditions are met during emulation.
bool BreakpointManager::DeactivateBreakpoint(uint16_t breakpointID)
{
    if (_breakpointMapByID.find(breakpointID) != _breakpointMapByID.end())
    {
        BreakpointDescriptor* bp = _breakpointMapByID[breakpointID];
        bp->active = false;
        return true;
    }
    return false;
}

/// @brief Activates all breakpoints in the manager
/// 
/// This method enables all breakpoints regardless of their type or previous state.
/// After this call, all breakpoints will trigger when their conditions are met.
void BreakpointManager::ActivateAllBreakpoints()
{
    for (auto& pair : _breakpointMapByID)
    {
        pair.second->active = true;
    }
}

/// @brief Deactivates all breakpoints in the manager
/// 
/// This method disables all breakpoints regardless of their type or previous state.
/// After this call, no breakpoints will trigger until they are explicitly activated.
void BreakpointManager::DeactivateAllBreakpoints()
{
    for (auto& pair : _breakpointMapByID)
    {
        pair.second->active = false;
    }
}

/// @brief Activates all breakpoints of a specific type
/// 
/// @param type The type of breakpoints to activate (BRK_MEMORY, BRK_IO, etc.)
/// 
/// This method enables all breakpoints that match the specified type.
/// Other breakpoints remain in their current state.
void BreakpointManager::ActivateBreakpointsByType(BreakpointTypeEnum type)
{
    for (auto& pair : _breakpointMapByID)
    {
        if (pair.second->type == type)
        {
            pair.second->active = true;
        }
    }
}

/// @brief Deactivates all breakpoints of a specific type
/// 
/// @param type The type of breakpoints to deactivate (BRK_MEMORY, BRK_IO, etc.)
/// 
/// This method disables all breakpoints that match the specified type.
/// Other breakpoints remain in their current state.
void BreakpointManager::DeactivateBreakpointsByType(BreakpointTypeEnum type)
{
    for (auto& pair : _breakpointMapByID)
    {
        if (pair.second->type == type)
        {
            pair.second->active = false;
        }
    }
}

/// @brief Activates memory breakpoints matching specific access types
/// 
/// @param memoryType Bitmask of BRK_MEM_* flags specifying which memory access types to activate
/// 
/// This method enables all memory breakpoints that have any of the specified
/// access types (read, write, execute) set in the memoryType bitmask.
void BreakpointManager::ActivateMemoryBreakpointsByType(uint8_t memoryType)
{
    for (auto& pair : _breakpointMapByID)
    {
        if (pair.second->type == BRK_MEMORY && (pair.second->memoryType & memoryType))
        {
            pair.second->active = true;
        }
    }
}

/// @brief Deactivates memory breakpoints matching specific access types
/// 
/// @param memoryType Bitmask of BRK_MEM_* flags specifying which memory access types to deactivate
/// 
/// This method disables all memory breakpoints that have any of the specified
/// access types (read, write, execute) set in the memoryType bitmask.
void BreakpointManager::DeactivateMemoryBreakpointsByType(uint8_t memoryType)
{
    for (auto& pair : _breakpointMapByID)
    {
        if (pair.second->type == BRK_MEMORY && (pair.second->memoryType & memoryType))
        {
            pair.second->active = false;
        }
    }
}

/// @brief Activates I/O port breakpoints matching specific access types
/// 
/// @param ioType Bitmask of BRK_IO_* flags specifying which I/O operations to activate
/// 
/// This method enables all I/O port breakpoints that have any of the specified
/// operation types (input, output) set in the ioType bitmask.
void BreakpointManager::ActivatePortBreakpointsByType(uint8_t ioType)
{
    for (auto& pair : _breakpointMapByID)
    {
        if (pair.second->type == BRK_IO && (pair.second->ioType & ioType))
        {
            pair.second->active = true;
        }
    }
}

/// @brief Deactivates I/O port breakpoints matching specific access types
/// 
/// @param ioType Bitmask of BRK_IO_* flags specifying which I/O operations to deactivate
/// 
/// This method disables all I/O port breakpoints that have any of the specified
/// operation types (input, output) set in the ioType bitmask.
void BreakpointManager::DeactivatePortBreakpointsByType(uint8_t ioType)
{
    for (auto& pair : _breakpointMapByID)
    {
        if (pair.second->type == BRK_IO && (pair.second->ioType & ioType))
        {
            pair.second->active = false;
        }
    }
}

// Breakpoint group management

/// @brief Adds a breakpoint to a named group
/// 
/// @param descriptor Pointer to the breakpoint descriptor to add to the group
/// @param groupName Name of the group to add the breakpoint to
/// @return uint16_t The breakpoint ID if successful, BRK_INVALID on failure
/// 
/// This method assigns a breakpoint to a named group, allowing for batch operations
/// on related breakpoints. The group is created if it doesn't already exist.
uint16_t BreakpointManager::AddBreakpointToGroup(BreakpointDescriptor* descriptor, const std::string& groupName)
{
    if (descriptor == nullptr || groupName.empty() || descriptor->breakpointID == BRK_INVALID)
    {
        return BRK_INVALID;
    }

    // If the breakpoint is already in the specified group, return its ID
    if (descriptor->group != groupName)
    {
        // Update the group name
        descriptor->group = groupName;
    }

    return descriptor->breakpointID;
}

/// @brief Assigns a breakpoint to a named group
/// 
/// @param breakpointID The ID of the breakpoint to assign
/// @param groupName Name of the group to assign the breakpoint to
/// @return bool True if the breakpoint was found and assigned, false otherwise
/// 
/// This method moves an existing breakpoint into the specified group.
/// The group is created if it doesn't already exist.
bool BreakpointManager::SetBreakpointGroup(uint16_t breakpointID, const std::string& groupName)
{
    if (groupName.empty() || !key_exists(_breakpointMapByID, breakpointID))
    {
        return false;
    }

    BreakpointDescriptor* breakpoint = _breakpointMapByID[breakpointID];
    breakpoint->group = groupName;

    return true;
}

/// @brief Retrieves a list of all breakpoint group names
/// 
/// @return std::vector<std::string> A list of unique group names that have breakpoints assigned to them
/// 
/// This method scans all breakpoints and collects the names of all groups that
/// have at least one breakpoint assigned to them.
std::vector<std::string> BreakpointManager::GetBreakpointGroups() const
{
    std::set<std::string> groups;
    
    for (const auto& [id, breakpoint] : _breakpointMapByID)
    {
        if (!breakpoint->group.empty())
        {
            groups.insert(breakpoint->group);
        }
    }
    
    return std::vector<std::string>(groups.begin(), groups.end());
}

/// @brief Retrieves all breakpoint IDs belonging to a specific group
/// 
/// @param groupName The name of the group to query
/// @return std::vector<uint16_t> A list of breakpoint IDs in the specified group
/// 
/// If the group doesn't exist or is empty, returns an empty vector.
std::vector<uint16_t> BreakpointManager::GetBreakpointsByGroup(const std::string& groupName) const
{
    std::vector<uint16_t> breakpointIDs;
    
    if (groupName.empty())
    {
        return breakpointIDs;
    }
    
    for (const auto& [id, breakpoint] : _breakpointMapByID)
    {
        if (breakpoint->group == groupName)
        {
            breakpointIDs.push_back(id);
        }
    }
    
    return breakpointIDs;
}

/// @brief Generates a formatted string listing all breakpoints in a specific group
/// 
/// @param groupName The name of the group to list breakpoints for
/// @return std::string A formatted string with breakpoint information
/// 
/// The output format is similar to GetBreakpointListAsString() but only includes
/// breakpoints that belong to the specified group.
std::string BreakpointManager::GetBreakpointListAsStringByGroup(const std::string& groupName) const
{
    if (groupName.empty())
    {
        return "No group name specified\n";
    }
    
    std::string result = StringHelper::Format("Breakpoints in group '%s':\n", groupName.c_str());
    bool found = false;
    
    for (const auto& [id, breakpoint] : _breakpointMapByID)
    {
        if (breakpoint->group == groupName)
        {
            result += FormatBreakpointInfo(id) + "\n";
            found = true;
        }
    }
    
    if (!found)
    {
        result += "  No breakpoints found in this group\n";
    }
    
    return result;
}

/// @brief Activates all breakpoints in a specific group
/// 
/// @param groupName The name of the group to activate
/// 
/// This method enables all breakpoints that belong to the specified group.
/// Breakpoints in other groups are not affected.
void BreakpointManager::ActivateBreakpointGroup(const std::string& groupName)
{
    if (groupName.empty())
    {
        return;
    }
    
    for (auto& [id, breakpoint] : _breakpointMapByID)
    {
        if (breakpoint->group == groupName)
        {
            breakpoint->active = true;
        }
    }
}

/// @brief Deactivates all breakpoints in a specific group
/// 
/// @param groupName The name of the group to deactivate
/// 
/// This method disables all breakpoints that belong to the specified group.
/// Breakpoints in other groups are not affected.
void BreakpointManager::DeactivateBreakpointGroup(const std::string& groupName)
{
    if (groupName.empty())
    {
        return;
    }
    
    for (auto& [id, breakpoint] : _breakpointMapByID)
    {
        if (breakpoint->group == groupName)
        {
            breakpoint->active = false;
        }
    }
}

/// @brief Removes a breakpoint from its current group
/// 
/// @param breakpointID The ID of the breakpoint to remove from its group
/// @return bool True if the breakpoint was found and removed from its group, false otherwise
/// 
/// This method clears the group assignment for the specified breakpoint.
/// The breakpoint itself remains active and functional.
bool BreakpointManager::RemoveBreakpointFromGroup(uint16_t breakpointID)
{
    if (!key_exists(_breakpointMapByID, breakpointID))
    {
        return false;
    }

    BreakpointDescriptor* breakpoint = _breakpointMapByID[breakpointID];
    
    // Remove from group by setting to default group
    breakpoint->group = "default";
    
    return true;
}

/// @brief Removes a breakpoint group and all its breakpoints
/// 
/// @param groupName The name of the group to remove
/// 
/// This method removes all breakpoints that belong to the specified group.
/// The group itself is also removed from the manager.
void BreakpointManager::RemoveBreakpointGroup(const std::string& groupName)
{
    if (groupName.empty())
    {
        return;
    }
    
    // First collect all breakpoint IDs to remove
    std::vector<uint16_t> breakpointsToRemove;
    for (const auto& [id, breakpoint] : _breakpointMapByID)
    {
        if (breakpoint->group == groupName)
        {
            breakpointsToRemove.push_back(id);
        }
    }
    
    // Then remove them (can't remove while iterating)
    for (uint16_t id : breakpointsToRemove)
    {
        RemoveBreakpointByID(id);
    }
}

// Breakpoint removal by address/port/type

/// @brief Removes all breakpoints at a specific memory address
/// 
/// @param address The 16-bit Z80 memory address to remove breakpoints from
/// @return bool True if any breakpoints were found and removed, false otherwise
/// 
/// This method removes all breakpoints (regardless of type) that are set at
/// the specified memory address.
bool BreakpointManager::RemoveBreakpointByAddress(uint16_t address)
{
    BreakpointDescriptor* bp = FindAddressBreakpoint(address);
    if (bp != nullptr)
    {
        return RemoveBreakpointByID(bp->breakpointID);
    }
    return false;
}

/// @brief Removes all breakpoints for a specific I/O port
/// 
/// @param port The 16-bit Z80 I/O port address to remove breakpoints from
/// @return bool True if any breakpoints were found and removed, false otherwise
/// 
/// This method removes all I/O breakpoints that are set for the specified port,
/// regardless of whether they are for input, output, or both operations.
bool BreakpointManager::RemoveBreakpointByPort(uint16_t port)
{
    BreakpointDescriptor* bp = FindPortBreakpoint(port);
    if (bp != nullptr)
    {
        return RemoveBreakpointByID(bp->breakpointID);
    }
    return false;
}

/// @brief Removes all breakpoints of a specific type
/// 
/// @param type The type of breakpoints to remove (BRK_MEMORY, BRK_IO, etc.)
/// 
/// This method completely removes all breakpoints that match the specified type.
/// Use with caution as this operation cannot be undone.
void BreakpointManager::RemoveBreakpointsByType(BreakpointTypeEnum type)
{
    // Create a list of IDs to remove to avoid modifying the map during iteration
    std::vector<uint16_t> idsToRemove;
    
    for (const auto& pair : _breakpointMapByID)
    {
        if (pair.second->type == type)
        {
            idsToRemove.push_back(pair.first);
        }
    }
    
    // Remove all breakpoints of the specified type
    for (uint16_t id : idsToRemove)
    {
        RemoveBreakpointByID(id);
    }
}

/// @brief Removes all memory breakpoints matching specific access types
/// 
/// @param memoryType Bitmask of BRK_MEM_* flags specifying which memory access types to remove
/// 
/// This method removes all memory breakpoints that have any of the specified
/// access types (read, write, execute) set in the memoryType bitmask.
void BreakpointManager::RemoveMemoryBreakpointsByType(uint8_t memoryType)
{
    // Create a list of IDs to remove to avoid modifying the map during iteration
    std::vector<uint16_t> idsToRemove;
    
    for (const auto& pair : _breakpointMapByID)
    {
        if (pair.second->type == BRK_MEMORY && (pair.second->memoryType & memoryType))
        {
            idsToRemove.push_back(pair.first);
        }
    }
    
    // Remove all memory breakpoints of the specified type
    for (uint16_t id : idsToRemove)
    {
        RemoveBreakpointByID(id);
    }
}

/// @brief Removes all I/O port breakpoints matching specific access types
/// 
/// @param ioType Bitmask of BRK_IO_* flags specifying which I/O operations to remove
/// 
/// This method removes all I/O port breakpoints that have any of the specified
/// operation types (input, output) set in the ioType bitmask.
void BreakpointManager::RemovePortBreakpointsByType(uint8_t ioType)
{
    // Create a list of IDs to remove to avoid modifying the map during iteration
    std::vector<uint16_t> idsToRemove;
    
    for (const auto& pair : _breakpointMapByID)
    {
        if (pair.second->type == BRK_IO && (pair.second->ioType & ioType))
        {
            idsToRemove.push_back(pair.first);
        }
    }
    
    // Remove all port breakpoints of the specified type
    for (uint16_t id : idsToRemove)
    {
        RemoveBreakpointByID(id);
    }
}

/// endregion </Management assistance methods>

/// region <Runtime methods>
uint16_t BreakpointManager::HandlePCChange(uint16_t pc)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = FindAddressBreakpoint(pc);
    if (breakpoint && breakpoint->active)
    {
        if (breakpoint->memoryType & BRK_MEM_EXECUTE)
        {
            result = breakpoint->breakpointID;

            /// region <Debug info>
#ifdef _DEBUG
            Memory& memory = *_context->pMemory;
            MemoryPageDescriptor page = memory.MapZ80AddressToPhysicalPage(pc);

            // Precise bank + address
            if (breakpoint->matchType == BRK_MATCH_BANK_ADDR)
            {
                const char *pageType = page.mode == BANK_ROM ? "ROM" : "RAM";
                std::string message = StringHelper::Format(
                        "[EXEC] Breakpoint ID: %d fired on PC: %04X (%s %d page, addr: %04X)",
                        breakpoint->breakpointID, pc, pageType, page.page, page.addressInPage);
                MLOGDEBUG(message.c_str());
            }

            // Wildcard address in Z80 (no bank distinction)
            if (breakpoint->matchType == BRK_MATCH_ADDR)
            {
                const char *pageType = page.mode == BANK_ROM ? "ROM" : "RAM";
                std::string message = StringHelper::Format(
                        "[EXEC] Breakpoint ID: %d fired on wildcard PC: %04X (%s %d page, addr: %04X)",
                        breakpoint->breakpointID, pc, pageType, page.page, page.addressInPage);
                MLOGDEBUG(message.c_str());
            }
#endif // _DEBUG
            /// endregion </Debug info>
        }
    }

    return result;
}

/// @brief Handles memory read operations and checks for read breakpoints
/// 
/// @param readAddress The memory address being read from
/// @return uint16_t The ID of the triggered breakpoint, or BRK_INVALID if none
/// 
/// This method is called whenever the Z80 performs a memory read operation.
/// It checks if there's a read breakpoint at the specified address
/// and returns the breakpoint ID if triggered.
uint16_t BreakpointManager::HandleMemoryRead(uint16_t readAddress)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = FindAddressBreakpoint(readAddress);
    if (breakpoint && breakpoint->active)
    {
        if (breakpoint->memoryType & BRK_MEM_READ)
        {
            result = breakpoint->breakpointID;
        }
    }

    return result;
}

/// @brief Handles memory write operations and checks for write breakpoints
/// 
/// @param writeAddress The memory address being written to
/// @return uint16_t The ID of the triggered breakpoint, or BRK_INVALID if none
/// 
/// This method is called whenever the Z80 performs a memory write operation.
/// It checks if there's a write breakpoint at the specified address
/// and returns the breakpoint ID if triggered.
uint16_t BreakpointManager::HandleMemoryWrite(uint16_t writeAddress)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = FindAddressBreakpoint(writeAddress);
    if (breakpoint && breakpoint->active)
    {
        if (breakpoint->memoryType & BRK_MEM_WRITE)
        {
            result = breakpoint->breakpointID;
        }
    }

    return result;
}

/// @brief Handles input port operations and checks for input breakpoints
/// 
/// @param portAddress The I/O port address being read from
/// @return uint16_t The ID of the triggered breakpoint, or BRK_INVALID if none
/// 
/// This method is called whenever the Z80 performs an IN instruction.
/// It checks if there's an input breakpoint for the specified port
/// and returns the breakpoint ID if triggered.
uint16_t BreakpointManager::HandlePortIn(uint16_t portAddress)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = FindPortBreakpoint(portAddress);
    if (breakpoint && breakpoint->active)
    {
        if (breakpoint->ioType & BRK_IO_IN)
        {
            result = breakpoint->breakpointID;
        }
    }

    return result;
}

/// @brief Handles output port operations and checks for output breakpoints
/// 
/// @param portAddress The I/O port address being written to
/// @return uint16_t The ID of the triggered breakpoint, or BRK_INVALID if none
/// 
/// This method is called whenever the Z80 performs an OUT instruction.
/// It checks if there's an output breakpoint for the specified port
/// and returns the breakpoint ID if triggered.
uint16_t BreakpointManager::HandlePortOut(uint16_t portAddress)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = FindPortBreakpoint(portAddress);
    if (breakpoint && breakpoint->active)
    {
        if (breakpoint->ioType & BRK_IO_OUT)
        {
            result = breakpoint->breakpointID;
        }
    }

    return result;
}

// Generates a new unique breakpoint ID
// 
// @return uint16_t A new unique breakpoint ID
// 
// This method generates a new breakpoint ID that is guaranteed to be unique
// within this BreakpointManager instance. The ID starts at 1 and increments
// for each new breakpoint, wrapping around to 1 if it would overflow.
uint16_t BreakpointManager::GenerateNewBreakpointID()
{
    // If no breakpoints exist, start with ID 1
    if (_breakpointMapByID.empty())
    {
        _breakpointIDSeq = 1;
        return _breakpointIDSeq;
    }

    // Since the map is ordered by key, the last element has the highest ID
    uint16_t maxID = _breakpointMapByID.rbegin()->first;
    
    // Check for overflow (unlikely with uint16_t, but good practice)
    if (maxID == 0xFFFF)
    {
        throw std::logic_error("BreakpointManager::GenerateNewBreakpointID - max number of breakpoint IDs: 0xFFFF (65535) already generated. No more breakpoints can be created");
    }

    // Set the next ID to be one more than the maximum
    _breakpointIDSeq = maxID + 1;
    return _breakpointIDSeq;
}

// Internal helper to add a memory breakpoint
// 
// @param descriptor The breakpoint descriptor containing configuration
// @return uint16_t The ID of the newly created breakpoint, or BRK_INVALID on failure
// 
// This is an internal helper method that handles the common logic for
// adding memory breakpoints. It validates the descriptor, generates a new
// breakpoint ID, and adds the breakpoint to the appropriate internal maps.
uint16_t BreakpointManager::AddMemoryBreakpoint(BreakpointDescriptor* descriptor)
{
    uint16_t result = BRK_INVALID;

    BreakpointAddressMatchEnum matchType = descriptor->matchType;
    uint32_t key;

    switch (matchType)
    {
        case BRK_MATCH_ADDR:
            key = 0xFFFF'0000 | descriptor->z80address;
            break;
        case BRK_MATCH_BANK_ADDR:
            key = descriptor->bank << 16;
            key |= descriptor->bankAddress;
            break;
        default:
            break;
    }

    if (key_exists(_breakpointMapByAddress, key))
    {
        // Such breakpoint already exist, returning it's ID
        BreakpointDescriptor* existing = _breakpointMapByAddress[key];
        result = existing->breakpointID;
    }
    else
    {
        result = GenerateNewBreakpointID();
        descriptor->breakpointID = result;
        descriptor->keyAddress = key;

        _breakpointMapByAddress.insert( { key, descriptor });
        _breakpointMapByID.insert( { result, descriptor });
    }

    return result;
}

/// @brief Internal helper to add an I/O port breakpoint
/// 
/// @param descriptor The breakpoint descriptor containing configuration
/// @return uint16_t The ID of the created breakpoint, or BRK_INVALID on failure
/// 
/// This method handles the common logic for adding I/O port breakpoints.
/// It validates the descriptor, generates a new ID, and updates internal mappings.
uint16_t BreakpointManager::AddPortBreakpoint(BreakpointDescriptor* descriptor)
{
    uint16_t result = BRK_INVALID;

    uint16_t key = descriptor->z80address;
    if (key_exists(_breakpointMapByAddress, key))
    {
        // Such breakpoint already exist, returning it's ID
        BreakpointDescriptor* existing = _breakpointMapByAddress[key];
        result = existing->breakpointID;
    }
    else
    {
        result = GenerateNewBreakpointID();
        descriptor->breakpointID = result;

        _breakpointMapByPort.insert( { key, descriptor });
        _breakpointMapByID.insert( { result, descriptor });
    }

    return result;
}

/// @brief Finds a memory breakpoint by its address
/// 
/// @param address The memory address to search for
/// @return BreakpointDescriptor* Pointer to the found breakpoint, or nullptr if not found
/// 
/// This helper method searches for a memory breakpoint at the specified address.
/// It checks both the address-based map and the wildcard breakpoints.
BreakpointDescriptor* BreakpointManager::FindAddressBreakpoint(uint16_t address)
{
    BreakpointDescriptor* result = nullptr;

    Memory& memory = *_context->pMemory;

    // Determine memory page for address
    MemoryPageDescriptor page = memory.MapZ80AddressToPhysicalPage(address);
    uint32_t fullKey = (page.page << 16) | page.addressInPage;
    uint32_t wildcardKey = 0xFFFF'0000 | address;

    // Try to match address in specified memory page first
    if (key_exists(_breakpointMapByAddress, fullKey))
    {
        result = _breakpointMapByAddress[fullKey];
    }
    // Address in any bank matching
    else if (key_exists(_breakpointMapByAddress, wildcardKey))
    {
        result = _breakpointMapByAddress[wildcardKey];
    }

    return result;
}

/// @brief Finds an I/O port breakpoint by its port number
/// 
/// @param port The I/O port number to search for
/// @return BreakpointDescriptor* Pointer to the found breakpoint, or nullptr if not found
/// 
/// This helper method searches for an I/O port breakpoint for the specified port.
/// It checks both the port-based map and the wildcard breakpoints.
BreakpointDescriptor* BreakpointManager::FindPortBreakpoint(uint16_t port)
{
    BreakpointDescriptor* result = nullptr;

    if (key_exists(_breakpointMapByPort, port))
    {
        result = _breakpointMapByPort[port];
    }

    return result;
}

/// endregion </Helper methods>