#include "stdafx.h"

#include "breakpointmanager.h"

#include "common/collectionhelper.h"
#include "common/stringhelper.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"

/// region <Constructors / destructors>

BreakpointManager::BreakpointManager(EmulatorContext* context)
{
    _context = context;
    _logger = _context->pModuleLogger;
}

BreakpointManager::~BreakpointManager()
{
    ClearBreakpoints();

    _context = nullptr;
}

/// endregion </Constructors / destructors>

/// region <Management methods>

void BreakpointManager::ClearBreakpoints()
{
    _breakpointMapByAddress.clear();
    _breakpointMapByPort.clear();
    _breakpointMapByID.clear();
}

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

bool BreakpointManager::RemoveBreakpoint(BreakpointDescriptor* descriptor)
{
    (void)descriptor;

    bool result = false;

    throw std::logic_error("BreakpointManager::RemoveBreakpoint(BreakpointDescriptor* descriptor) - Not implemented");

    return result;
}

bool BreakpointManager::RemoveBreakpointByID(uint16_t breakpointID)
{
    bool result = false;

    if (key_exists(_breakpointMapByID, breakpointID))
    {
        BreakpointDescriptor* breakpoint = _breakpointMapByID[breakpointID];

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

        _breakpointMapByID.erase(breakpoint->breakpointID);

        // Disposing BreakpointDescriptor object created during add process
        delete breakpoint;
    }

    return result;
}

size_t BreakpointManager::GetBreakpointsCount()
{
    return _breakpointMapByID.size();
}

/// endregion </Management methods>

/// region <Management assistance methods>
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

const BreakpointMapByID& BreakpointManager::GetAllBreakpoints() const
{
    return _breakpointMapByID;
}

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

std::string BreakpointManager::GetBreakpointListAsString() const
{
    std::ostringstream oss;
    
    if (_breakpointMapByID.empty())
    {
        oss << "No breakpoints set\n";
        return oss.str();
    }
    
    // Header
    oss << "ID   Type       Address  Access Status   Note\n";
    oss << "---- ---------- -------- ----- -------- ---------------\n";
    
    // List all breakpoints
    for (const auto& pair : _breakpointMapByID)
    {
        oss << FormatBreakpointInfo(pair.first) << "\n";
    }
    
    return oss.str();
}

// Breakpoint activation/deactivation

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

void BreakpointManager::ActivateAllBreakpoints()
{
    for (auto& pair : _breakpointMapByID)
    {
        pair.second->active = true;
    }
}

void BreakpointManager::DeactivateAllBreakpoints()
{
    for (auto& pair : _breakpointMapByID)
    {
        pair.second->active = false;
    }
}

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

uint16_t BreakpointManager::AddBreakpointToGroup(BreakpointDescriptor* descriptor, const std::string& groupName)
{
    if (descriptor == nullptr || groupName.empty())
    {
        return BRK_INVALID;
    }

    // If the breakpoint is already in the specified group, return its ID
    if (descriptor->group == groupName)
    {
        return descriptor->breakpointID;
    }

    // Update the group name
    descriptor->group = groupName;
    
    return descriptor->breakpointID;
}

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

bool BreakpointManager::RemoveBreakpointByAddress(uint16_t address)
{
    BreakpointDescriptor* bp = FindAddressBreakpoint(address);
    if (bp != nullptr)
    {
        return RemoveBreakpointByID(bp->breakpointID);
    }
    return false;
}

bool BreakpointManager::RemoveBreakpointByPort(uint16_t port)
{
    BreakpointDescriptor* bp = FindPortBreakpoint(port);
    if (bp != nullptr)
    {
        return RemoveBreakpointByID(bp->breakpointID);
    }
    return false;
}

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
/// endregion </Runtime methods>

/// region <Helper methods>

uint16_t BreakpointManager::GenerateNewBreakpointID()
{
    _breakpointIDSeq += 1;

    // We depleted sequence, no more breakpoints can be created
    if (_breakpointIDSeq == 0xFFFF)
    {
        throw std::logic_error("BreakpointManager::GenerateNewBreakpointID - max number of breakpoint IDs: 0xFFFF (65535) already generated. No more breakpoints can be created");
    }

    return _breakpointIDSeq;
}

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