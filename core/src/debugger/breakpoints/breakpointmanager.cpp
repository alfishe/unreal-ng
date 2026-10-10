#include "breakpointmanager.h"

#include "debugger/ttd/ttdphyspage.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "common/collectionhelper.h"
#include "common/stringhelper.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/emulator.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "3rdparty/message-center/messagecenter.h"
#include "stdafx.h"

/// region <Constructors / destructors>

/// @brief Constructs a new Breakpoint Manager with the given emulator context
/// @param context Pointer to the emulator context
BreakpointManager::BreakpointManager(EmulatorContext* context)
{
    _context = context;
    _logger = _context->pModuleLogger;
    _candidateSets.assign(1, {});  // set 0: no candidates
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

    RebuildFilters();
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
#endif  // _DEBUG

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
            std::string message = StringHelper::Format("BreakpointManager::AddBreakpoint - invalid breakpoint type: %d",
                                                       descriptor->type);
            throw std::logic_error(message);
        }
#endif  // _DEBUG
        break;
    }

    return result;
}

uint16_t BreakpointManager::AddBreakpoint(const BreakpointSpec& spec, std::string& error)
{
    const uint16_t end = spec.hasEnd ? spec.addressEnd : spec.address;
    if (end < spec.address)
    {
        error = "the range ends before it starts";
        return BRK_INVALID;
    }
    if (spec.spacePage != 0xFFFF)
    {
        // A watchpoint on another memory space: read / write, one 16 KB page
        if (spec.type != BRK_MEMORY || (spec.access & BRK_MEM_EXECUTE) || !(spec.access & (BRK_MEM_READ | BRK_MEM_WRITE)))
        {
            error = "a page of another memory space (vramN) takes read and write watchpoints";
            return BRK_INVALID;
        }
        if (spec.hasPage || spec.slotOnly || (spec.address >> 14) != (end >> 14))
        {
            error = "a vram watchpoint's range stays inside its 16 KB page (offsets 0..#3FFF)";
            return BRK_INVALID;
        }
    }
    else if (spec.type == BRK_MEMORY)
    {
        if (spec.portMask != 0xFFFF)
        {
            error = "a mask applies to port breakpoints";
            return BRK_INVALID;
        }
        if (!(spec.access & (BRK_MEM_EXECUTE | BRK_MEM_READ | BRK_MEM_WRITE)))
        {
            error = "a memory breakpoint watches execute, read or write";
            return BRK_INVALID;
        }
        if (spec.hasPage)
        {
            if (!HasPage(spec.page, spec.pageType))
            {
                error = std::string("this machine has no page ") + PageKindName(spec.pageType) + std::to_string(spec.page);
                return BRK_INVALID;
            }
            if ((spec.address >> 14) != (end >> 14))
            {
                error = "a range on a page stays inside one 16K page (its offsets come from the address)";
                return BRK_INVALID;
            }
        }
        else if (spec.slotOnly)
        {
            error = "slot_only applies to a breakpoint on a page";
            return BRK_INVALID;
        }
    }
    else if (spec.type == BRK_IO)
    {
        if (spec.hasPage || spec.slotOnly)
        {
            error = "a page applies to execution, read and write breakpoints";
            return BRK_INVALID;
        }
        if (!(spec.access & (BRK_IO_IN | BRK_IO_OUT)))
        {
            error = "a port breakpoint watches in or out";
            return BRK_INVALID;
        }
    }
    else
    {
        error = "unsupported breakpoint type";
        return BRK_INVALID;
    }
    if (spec.hitMode != BRK_HIT_ALWAYS && spec.hitTarget == 0)
    {
        error = "a hit policy needs a target of 1 or more";
        return BRK_INVALID;
    }

    auto* d = new BreakpointDescriptor();
    d->type = spec.type;
    if (spec.type == BRK_MEMORY)
        d->memoryType = spec.access;
    else
        d->ioType = spec.access;
    d->z80address = spec.address;
    d->isRange = spec.hasEnd && end != spec.address;
    d->z80addressEnd = end;
    if (spec.hasPage)
    {
        d->matchType = BRK_MATCH_BANK_ADDR;
        d->page = spec.page;
        d->pageType = spec.pageType;
        d->slotOnly = spec.slotOnly;
    }
    d->spacePage = spec.spacePage;
    d->portMask = spec.portMask;
    d->hitMode = spec.hitMode;
    d->hitTarget = spec.hitMode == BRK_HIT_ALWAYS ? 0 : spec.hitTarget;
    d->note = spec.note;
    if (!spec.group.empty())
        d->group = spec.group;
    if (!spec.owner.empty())
        d->owner = spec.owner;

    const uint16_t id = AddBreakpoint(d);
    if (id == BRK_INVALID)
    {
        delete d;
        error = "the breakpoint was not added";
        return BRK_INVALID;
    }
    if (_breakpointMapByID[id] != d)
    {
        // The same plain breakpoint existed: the note and group of the request go to it
        delete d;
        if (!spec.note.empty())
            SetBreakpointNote(id, spec.note);
        if (!spec.group.empty())
            SetBreakpointGroup(id, spec.group);
    }
    return id;
}

bool BreakpointManager::ParseHitSpec(const std::string& text, BreakpointHitModeEnum& mode, uint32_t& target, std::string& error)
{
    if (text.empty() || text == "always")
    {
        mode = BRK_HIT_ALWAYS;
        target = 0;
        return true;
    }
    std::string number = text;
    mode = BRK_HIT_EQUAL;
    if (number.rfind(">=", 0) == 0)
    {
        mode = BRK_HIT_AT_LEAST;
        number = number.substr(2);
    }
    else if (number[0] == '%')
    {
        mode = BRK_HIT_MULTIPLE;
        number = number.substr(1);
    }
    else if (number.rfind("==", 0) == 0)
        number = number.substr(2);
    char* endPtr = nullptr;
    const unsigned long value = number.empty() ? 0 : std::strtoul(number.c_str(), &endPtr, 10);
    if (number.empty() || *endPtr != '\0' || value == 0 || value > 0xFFFFFFFFul)
    {
        error = "hits must be N (the Nth hit), >=N (from the Nth on) or %N (every Nth), N >= 1; got '" + text + "'";
        return false;
    }
    target = static_cast<uint32_t>(value);
    return true;
}

std::string BreakpointManager::HitSpecName(const BreakpointDescriptor& bp)
{
    switch (bp.hitMode)
    {
        case BRK_HIT_EQUAL: return std::to_string(bp.hitTarget);
        case BRK_HIT_AT_LEAST: return ">=" + std::to_string(bp.hitTarget);
        case BRK_HIT_MULTIPLE: return "%" + std::to_string(bp.hitTarget);
        default: return {};
    }
}

const char* BreakpointManager::HitModeName(BreakpointHitModeEnum mode)
{
    switch (mode)
    {
        case BRK_HIT_EQUAL: return "equal";
        case BRK_HIT_AT_LEAST: return "at_least";
        case BRK_HIT_MULTIPLE: return "multiple";
        default: return "always";
    }
}

bool BreakpointManager::ParseHitModeName(const std::string& text, BreakpointHitModeEnum& mode)
{
    if (text == "always") mode = BRK_HIT_ALWAYS;
    else if (text == "equal") mode = BRK_HIT_EQUAL;
    else if (text == "at_least") mode = BRK_HIT_AT_LEAST;
    else if (text == "multiple") mode = BRK_HIT_MULTIPLE;
    else return false;
    return true;
}

bool BreakpointManager::ApplyScriptOptions(BreakpointSpec& spec, const std::string& page, int32_t to, bool slotOnly,
                                           int32_t mask, const std::string& hits, std::string& error)
{
    if (!page.empty() && !ParsePageInto(page, spec, error))
        return false;
    if (to >= 0)
    {
        if (to > 0xFFFF)
        {
            error = "the range end must be 0..65535";
            return false;
        }
        spec.hasEnd = true;
        spec.addressEnd = static_cast<uint16_t>(to);
    }
    spec.slotOnly = slotOnly;
    if (mask >= 0)
    {
        if (mask > 0xFFFF)
        {
            error = "the mask must be 0..65535";
            return false;
        }
        spec.portMask = static_cast<uint16_t>(mask);
    }
    return ParseHitSpec(hits, spec.hitMode, spec.hitTarget, error);
}

bool BreakpointManager::ResetHitCount(uint16_t breakpointID)
{
    auto it = _breakpointMapByID.find(breakpointID);
    if (it == _breakpointMapByID.end())
        return false;
    it->second->hitCount = 0;
    return true;
}

void BreakpointManager::ResetAllHitCounts()
{
    for (auto& [id, bp] : _breakpointMapByID)
        if (bp)
            bp->hitCount = 0;
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

    // Remove from the key maps (only plain breakpoints are in them)
    switch (breakpoint->type)
    {
        case BRK_MEMORY:
        {
            auto key = _breakpointMapByAddress.find(breakpoint->keyAddress);
            if (key != _breakpointMapByAddress.end() && key->second == breakpoint)
                _breakpointMapByAddress.erase(key);
            break;
        }
        case BRK_IO:
        {
            auto key = _breakpointMapByPort.find(breakpoint->z80address);
            if (key != _breakpointMapByPort.end() && key->second == breakpoint)
                _breakpointMapByPort.erase(key);
            break;
        }
        default:
            break;
    }

    // Remove from the ID map and delete the descriptor
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

    RebuildFilters();

    return true;
}

size_t BreakpointManager::GetBreakpointsCount()
{
    return _breakpointMapByID.size();
}

BreakpointDescriptor* BreakpointManager::GetBreakpointById(uint16_t breakpointID)
{
    auto it = _breakpointMapByID.find(breakpointID);
    if (it == _breakpointMapByID.end())
        return nullptr;
    return it->second;
}

BreakpointManager::BreakpointStatusInfo BreakpointManager::GetLastTriggeredBreakpointInfo() const
{
    BreakpointStatusInfo info;

    if (_lastTriggeredBreakpointID == BRK_INVALID)
    {
        return info;  // Returns default with valid=false
    }

    auto it = _breakpointMapByID.find(_lastTriggeredBreakpointID);
    if (it == _breakpointMapByID.end())
    {
        return info;  // Breakpoint was removed after triggering
    }

    BreakpointDescriptor* bp = it->second;

    info.valid = true;
    info.id = _lastTriggeredBreakpointID;
    info.address = bp->z80address;
    info.active = bp->active;
    info.note = bp->note;
    info.group = bp->group;
    info.page = PageSpecName(*bp);
    info.hitCount = bp->hitCount;
    if (bp->spacePage != 0xFFFF)
    {
        info.pageKind = "vram";
        info.pageNumber = static_cast<uint8_t>(bp->spacePage - ttd::kVramPageBase);
    }
    else if (bp->matchType == BRK_MATCH_BANK_ADDR)
    {
        info.pageKind = PageKindName(bp->pageType);
        info.pageNumber = bp->page;
    }

    // Breakpoint type
    switch (bp->type)
    {
        case BRK_MEMORY:
            info.type = "memory";
            break;
        case BRK_IO:
            info.type = "port";
            break;
        case BRK_KEYBOARD:
            info.type = "keyboard";
            break;
        default:
            info.type = "unknown";
            break;
    }

    // Access type (for memory: execution/read/write, for port: in/out)
    std::string accessStr;
    if (bp->type == BRK_MEMORY)
    {
        if (bp->memoryType & BRK_MEM_EXECUTE)
            accessStr += "execute,";
        if (bp->memoryType & BRK_MEM_READ)
            accessStr += "read,";
        if (bp->memoryType & BRK_MEM_WRITE)
            accessStr += "write,";
    }
    else if (bp->type == BRK_IO)
    {
        if (bp->ioType & BRK_IO_IN)
            accessStr += "in,";
        if (bp->ioType & BRK_IO_OUT)
            accessStr += "out,";
    }
    // Trim trailing comma
    if (!accessStr.empty() && accessStr.back() == ',')
        accessStr.pop_back();
    info.access = accessStr;

    return info;
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
uint16_t BreakpointManager::AddExecutionBreakpoint(uint16_t z80address, const std::string& owner)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_EXECUTE;
    breakpoint->z80address = z80address;
    breakpoint->owner = owner;

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
uint16_t BreakpointManager::AddMemReadBreakpoint(uint16_t z80address, const std::string& owner)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_READ;
    breakpoint->z80address = z80address;
    breakpoint->owner = owner;

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
uint16_t BreakpointManager::AddMemWriteBreakpoint(uint16_t z80address, const std::string& owner)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = BRK_MEM_WRITE;
    breakpoint->z80address = z80address;
    breakpoint->owner = owner;

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
uint16_t BreakpointManager::AddPortInBreakpoint(uint16_t port, const std::string& owner)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_IO;
    breakpoint->ioType = BRK_IO_IN;
    breakpoint->z80address = port;
    breakpoint->owner = owner;

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
uint16_t BreakpointManager::AddPortOutBreakpoint(uint16_t port, const std::string& owner)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_IO;
    breakpoint->ioType = BRK_IO_OUT;
    breakpoint->z80address = port;
    breakpoint->owner = owner;

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
uint16_t BreakpointManager::AddCombinedMemoryBreakpoint(uint16_t z80address, uint8_t memoryType,
                                                        const std::string& owner)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->memoryType = memoryType;
    breakpoint->z80address = z80address;
    breakpoint->owner = owner;

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
uint16_t BreakpointManager::AddCombinedPortBreakpoint(uint16_t port, uint8_t ioType, const std::string& owner)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_IO;
    breakpoint->ioType = ioType;
    breakpoint->z80address = port;
    breakpoint->owner = owner;

    result = AddBreakpoint(breakpoint);

    return result;
}

// Page-specific breakpoints (for ROM/RAM/Cache page matching)

/// @brief Adds an execution breakpoint at the specified address in a specific memory page
///
/// @param z80address The 16-bit Z80 memory address where the breakpoint should be set
/// @param page The memory page number (ROM 0-63, RAM 0-255, Cache 0-1)
/// @param pageType The memory type (BANK_ROM, BANK_RAM, or BANK_CACHE)
/// @return uint16_t The ID of the newly created breakpoint, or BRK_INVALID on failure
///
/// Creates a breakpoint that only triggers when the Z80 executes the instruction
/// at the specified memory address AND the specified page of the specified type is currently mapped.
uint16_t BreakpointManager::AddExecutionBreakpointInPage(uint16_t z80address, uint8_t page, MemoryBankModeEnum pageType,
                                                         const std::string& owner)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->matchType = BreakpointAddressMatchEnum::BRK_MATCH_BANK_ADDR;
    breakpoint->memoryType = BRK_MEM_EXECUTE;
    breakpoint->z80address = z80address;
    breakpoint->page = page;
    breakpoint->pageType = pageType;
    breakpoint->owner = owner;

    result = AddBreakpoint(breakpoint);

    return result;
}

/// @brief Adds a memory read breakpoint at the specified address in a specific memory page
///
/// @param z80address The 16-bit Z80 memory address to monitor for read operations
/// @param page The memory page number (ROM 0-63, RAM 0-255, Cache 0-1)
/// @param pageType The memory type (BANK_ROM, BANK_RAM, or BANK_CACHE)
/// @return uint16_t The ID of the newly created breakpoint, or BRK_INVALID on failure
uint16_t BreakpointManager::AddMemReadBreakpointInPage(uint16_t z80address, uint8_t page, MemoryBankModeEnum pageType,
                                                       const std::string& owner)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->matchType = BreakpointAddressMatchEnum::BRK_MATCH_BANK_ADDR;
    breakpoint->memoryType = BRK_MEM_READ;
    breakpoint->z80address = z80address;
    breakpoint->page = page;
    breakpoint->pageType = pageType;
    breakpoint->owner = owner;

    result = AddBreakpoint(breakpoint);

    return result;
}

/// @brief Adds a memory write breakpoint at the specified address in a specific memory page
///
/// @param z80address The 16-bit Z80 memory address to monitor for write operations
/// @param page The memory page number (ROM 0-63, RAM 0-255, Cache 0-1)
/// @param pageType The memory type (BANK_ROM, BANK_RAM, or BANK_CACHE)
/// @return uint16_t The ID of the newly created breakpoint, or BRK_INVALID on failure
uint16_t BreakpointManager::AddMemWriteBreakpointInPage(uint16_t z80address, uint8_t page, MemoryBankModeEnum pageType,
                                                        const std::string& owner)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->matchType = BreakpointAddressMatchEnum::BRK_MATCH_BANK_ADDR;
    breakpoint->memoryType = BRK_MEM_WRITE;
    breakpoint->z80address = z80address;
    breakpoint->page = page;
    breakpoint->pageType = pageType;
    breakpoint->owner = owner;

    result = AddBreakpoint(breakpoint);

    return result;
}

/// @brief Adds a combined memory breakpoint at the specified address in a specific memory page
///
/// @param z80address The 16-bit Z80 memory address for the breakpoint
/// @param memoryType Bitmask of BRK_MEM_* flags specifying the access types to break on
/// @param page The memory page number (ROM 0-63, RAM 0-255, Cache 0-1)
/// @param pageType The memory type (BANK_ROM, BANK_RAM, or BANK_CACHE)
/// @return uint16_t The ID of the newly created breakpoint, or BRK_INVALID on failure
uint16_t BreakpointManager::AddCombinedMemoryBreakpointInPage(uint16_t z80address, uint8_t memoryType, uint8_t page,
                                                              MemoryBankModeEnum pageType, const std::string& owner)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = new BreakpointDescriptor();
    breakpoint->type = BreakpointTypeEnum::BRK_MEMORY;
    breakpoint->matchType = BreakpointAddressMatchEnum::BRK_MATCH_BANK_ADDR;
    breakpoint->memoryType = memoryType;
    breakpoint->z80address = z80address;
    breakpoint->page = page;
    breakpoint->pageType = pageType;
    breakpoint->owner = owner;

    result = AddBreakpoint(breakpoint);

    return result;
}

bool BreakpointManager::ParsePageSpec(const std::string& text, uint8_t& page, MemoryBankModeEnum& pageType,
                                      std::string& error)
{
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string kind;
    for (const char* name : {"cache", "ram", "rom"})
        if (lower.rfind(name, 0) == 0)
            kind = name;
    if (kind.empty())
    {
        error = "page must be ramN, romN or cacheN (e.g. ram5), got '" + text + "'";
        return false;
    }
    std::string number = lower.substr(kind.size());
    int base = 10;
    if (number.rfind("0x", 0) == 0)
        number = number.substr(2), base = 16;
    else if (!number.empty() && (number[0] == '#' || number[0] == '$'))
        number = number.substr(1), base = 16;
    char* end = nullptr;
    const unsigned long value = number.empty() ? 256 : std::strtoul(number.c_str(), &end, base);
    if (number.empty() || *end != '\0' || value > 0xFF)
    {
        error = "page number must be 0..255, got '" + text + "'";
        return false;
    }
    page = static_cast<uint8_t>(value);
    pageType = kind == "ram" ? BANK_RAM : (kind == "rom" ? BANK_ROM : BANK_CACHE);
    return true;
}

bool BreakpointManager::ParsePageInto(const std::string& text, BreakpointSpec& spec, std::string& error)
{
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.rfind("vram", 0) == 0)
    {
        const std::string number = lower.substr(4);
        char* end = nullptr;
        const unsigned long value = number.empty() ? 99 : std::strtoul(number.c_str(), &end, 10);
        if (number.empty() || *end != '\0' || value >= ttd::kSpacePages)
        {
            error = "a video RAM page is vram0..vram15 (16 KB each), got '" + text + "'";
            return false;
        }
        spec.spacePage = static_cast<uint16_t>(ttd::kVramPageBase + value);
        return true;
    }
    if (!ParsePageSpec(text, spec.page, spec.pageType, error))
    {
        error += " (or vram0..vram15, the Sprinter's video RAM)";
        return false;
    }
    spec.hasPage = true;
    return true;
}

const char* BreakpointManager::PageKindName(MemoryBankModeEnum pageType)
{
    return pageType == BANK_ROM ? "rom" : (pageType == BANK_CACHE ? "cache" : "ram");
}

std::string BreakpointManager::PageSpecName(const BreakpointDescriptor& breakpoint)
{
    if (breakpoint.spacePage != 0xFFFF)
        return "vram" + std::to_string(breakpoint.spacePage - ttd::kVramPageBase);
    if (breakpoint.matchType != BRK_MATCH_BANK_ADDR)
        return {};
    return std::string(PageKindName(breakpoint.pageType)) + std::to_string(breakpoint.page);
}

bool BreakpointManager::HasPage(uint8_t page, MemoryBankModeEnum pageType) const
{
    switch (pageType)
    {
        case BANK_RAM:
        {
            const uint32_t ramKb = _context ? _context->config.ramsize : 0;
            const uint32_t pages = ramKb ? ramKb / 16 : MAX_RAM_PAGES;
            return page < pages;
        }
        case BANK_ROM:
            return page < MAX_ROM_PAGES;
        case BANK_CACHE:
            return page < MAX_CACHE_PAGES;
        default:
            return false;
    }
}

uint16_t BreakpointManager::AddMemoryBreakpointInPageSpec(uint16_t z80address, uint8_t memoryType,
                                                         const std::string& pageSpec, std::string& error)
{
    if (pageSpec.empty())
        return AddCombinedMemoryBreakpoint(z80address, memoryType);
    uint8_t page = 0;
    MemoryBankModeEnum pageType = BANK_RAM;
    if (!ParsePageSpec(pageSpec, page, pageType, error))
        return BRK_INVALID;
    if (!HasPage(page, pageType))
    {
        error = "this machine has no page " + pageSpec;
        return BRK_INVALID;
    }
    return AddCombinedMemoryBreakpointInPage(z80address, memoryType, page, pageType);
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
            // std::right: the type column left std::left set, which padded #38 as "3800"
            oss << "0x" << std::hex << std::uppercase << std::right << std::setw(4) << std::setfill('0') << bp->z80address
                << std::setfill(' ') << std::dec;
            // Ensure exact 8 characters width
            oss << std::string(8 - 6, ' ');  // 6 = "0x" + 4 hex digits
        }
        else
        {
            oss << std::setw(8) << std::left << "N/A";
        }

        // Format access type - exactly matching header width
        std::string access;
        if (bp->type == BRK_MEMORY)
        {
            if (bp->memoryType & BRK_MEM_READ)
                access += "R";
            if (bp->memoryType & BRK_MEM_WRITE)
                access += "W";
            if (bp->memoryType & BRK_MEM_EXECUTE)
                access += "X";
        }
        else if (bp->type == BRK_IO)
        {
            if (bp->ioType & BRK_IO_IN)
                access += "I";
            if (bp->ioType & BRK_IO_OUT)
                access += "O";
        }
        // Exactly 5 characters for "Access" column
        oss << " " << std::setw(5) << std::left << access;

        // Format status - exactly matching header width
        oss << " " << std::setw(8) << std::left << (bp->active ? "Active" : "Inactive");

        // The rest of what the breakpoint is: range end, page (physical, or one slot), mask, hit policy
        if (bp->isRange)
            oss << " to 0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << bp->z80addressEnd
                << std::setfill(' ') << std::dec;
        if (bp->matchType == BRK_MATCH_BANK_ADDR || bp->spacePage != 0xFFFF)
            oss << " in " << PageSpecName(*bp) << (bp->slotOnly ? " (this slot only)" : "");
        if (bp->type == BRK_IO && bp->portMask != 0xFFFF)
            oss << " mask 0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << bp->portMask
                << std::setfill(' ') << std::dec;
        if (bp->hitMode != BRK_HIT_ALWAYS)
            oss << " hits " << HitSpecName(*bp);
        if (bp->hitCount)
            oss << " (hit " << std::dec << bp->hitCount << "x)";

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
        RebuildFilters();
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
        RebuildFilters();
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
    RebuildFilters();
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
    RebuildFilters();
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
    RebuildFilters();
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
    RebuildFilters();
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
    RebuildFilters();
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
    RebuildFilters();
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
    RebuildFilters();
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
    RebuildFilters();
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
    NotifyBreakpointsChanged();

    return true;
}

bool BreakpointManager::SetBreakpointNote(uint16_t breakpointID, const std::string& note)
{
    if (!key_exists(_breakpointMapByID, breakpointID))
        return false;
    _breakpointMapByID[breakpointID]->note = note;
    NotifyBreakpointsChanged();
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
    RebuildFilters();
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
    RebuildFilters();
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

/// The out-of-line half of HandlePCChange / HandleMemoryRead / HandleMemoryWrite: the filter said "maybe"
uint16_t BreakpointManager::ResolveMemory(int kind, uint16_t address)
{
    // TTD silent-replay suppression (parent TDD 8.2 + Appendix C): the replay re-executes known history
    if (_context && (_context->ttdReplayActive || _context->toolAccessActive))
        return BRK_INVALID;

    // Stepping on from the execution breakpoint the emulator is stopped at: no hit, no count
    if (kind == BRK_KIND_EXEC && _execPassArmed && address == _execPassAddress)
    {
        _execPassArmed = false;
        return BRK_INVALID;
    }

    uint16_t result = BRK_INVALID;
    if (!_cpuHeads[kind].empty())
        result = WalkCandidates(_cpuHeads[kind][address], -1, result);

    // Physical breakpoints: the page the slot shows now, from the memory model (no remap tracking needed:
    // the filter does not depend on the mapping, design §5.2)
    if (_hasPageSlices[kind] && _context && _context->pMemory)
    {
        const MemoryPageDescriptor where = _context->pMemory->MapZ80AddressToPhysicalPage(address);
        if (where.mode == BANK_ROM || where.mode == BANK_RAM || where.mode == BANK_CACHE)
        {
            const size_t pageId = (static_cast<size_t>(where.mode) << 8) | where.page;
            if (pageId < kPageIds && _pageSlices[kind][pageId])
                result = WalkCandidates(_pageSlices[kind][pageId]->heads[address & 0x3FFF], address >> 14, result);
        }
    }

    if (result != BRK_INVALID)
        _lastTriggeredBreakpointID = result;  // Track for automation API queries
    return result;
}

uint16_t BreakpointManager::ResolveSpace(uint16_t spacePage, uint16_t offset, bool write)
{
    if (_context && (_context->ttdReplayActive || _context->toolAccessActive))
        return BRK_INVALID;
    uint16_t result = BRK_INVALID;
    for (const auto& [id, bp] : _breakpointMapByID)
    {
        if (!bp || !bp->active || bp->spacePage != spacePage || !(bp->memoryType & (write ? BRK_MEM_WRITE : BRK_MEM_READ)))
            continue;
        if (offset < (bp->z80address & 0x3FFF) || offset > (bp->EndAddress() & 0x3FFF))
            continue;
        const uint32_t n = ++bp->hitCount;
        bool stop = true;
        switch (bp->hitMode)
        {
            case BRK_HIT_ALWAYS: stop = true; break;
            case BRK_HIT_EQUAL: stop = n == bp->hitTarget; break;
            case BRK_HIT_AT_LEAST: stop = n >= bp->hitTarget; break;
            case BRK_HIT_MULTIPLE: stop = bp->hitTarget != 0 && n % bp->hitTarget == 0; break;
        }
        if (stop && result == BRK_INVALID)
            result = bp->breakpointID;
    }
    if (result != BRK_INVALID)
        _lastTriggeredBreakpointID = result;
    return result;
}

uint16_t BreakpointManager::ResolvePort(int direction, uint16_t port)
{
    if (_context && (_context->ttdReplayActive || _context->toolAccessActive))
        return BRK_INVALID;
    if (_portHeads[direction].empty())
        return BRK_INVALID;
    const uint16_t result = WalkCandidates(_portHeads[direction][port], -1, BRK_INVALID);
    if (result != BRK_INVALID)
        _lastTriggeredBreakpointID = result;
    return result;
}

uint16_t BreakpointManager::WalkCandidates(uint32_t set, int slot, uint16_t first)
{
    if (set == 0 || set >= _candidateSets.size())
        return first;
    for (BreakpointDescriptor* bp : _candidateSets[set])
    {
        // A slot-only physical breakpoint fires only through the slot it was set in
        if (bp->slotOnly && slot >= 0 && slot != (bp->z80address >> 14))
            continue;
        // Every matching access counts; the policy decides whether it stops
        const uint32_t n = ++bp->hitCount;
        bool stop = true;
        switch (bp->hitMode)
        {
            case BRK_HIT_ALWAYS: stop = true; break;
            case BRK_HIT_EQUAL: stop = n == bp->hitTarget; break;
            case BRK_HIT_AT_LEAST: stop = n >= bp->hitTarget; break;
            case BRK_HIT_MULTIPLE: stop = bp->hitTarget != 0 && n % bp->hitTarget == 0; break;
        }
        if (stop && first == BRK_INVALID)
            first = bp->breakpointID;
    }
    return first;
}

bool BreakpointManager::CoversMemory(const BreakpointDescriptor& bp, uint16_t address, const MemoryPageDescriptor& page)
{
    if (bp.type != BRK_MEMORY || bp.spacePage != 0xFFFF)
        return false;   // a space page's watchpoint never matches a CPU address
    if (bp.matchType == BRK_MATCH_ADDR)
        return address >= bp.z80address && address <= bp.EndAddress();
    // Physical: the page, the offsets, and for slot-only the slot
    if (page.mode != bp.pageType || page.page != bp.page)
        return false;
    if (bp.slotOnly && (address >> 14) != (bp.z80address >> 14))
        return false;
    const uint16_t offset = address & 0x3FFF;
    return offset >= (bp.z80address & 0x3FFF) && offset <= (bp.EndAddress() & 0x3FFF);
}

// endregion </Runtime methods>

/// region <Helper methods>

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
        throw std::logic_error(
            "BreakpointManager::GenerateNewBreakpointID - max number of breakpoint IDs: 0xFFFF (65535) already "
            "generated. No more breakpoints can be created");
    }

    // Set the next ID to be one more than the maximum
    _breakpointIDSeq = maxID + 1;
    return _breakpointIDSeq;
}

/// A single-address breakpoint without range, slot filter or hit policy: adding the same one again returns
/// the existing id (the key maps). Anything richer is its own breakpoint
static bool IsPlainMemory(const BreakpointDescriptor& d)
{
    return !d.isRange && !d.slotOnly && d.hitMode == BRK_HIT_ALWAYS;
}
static bool IsPlainPort(const BreakpointDescriptor& d)
{
    return d.portMask == 0xFFFF && d.hitMode == BRK_HIT_ALWAYS;
}

uint16_t BreakpointManager::AddMemoryBreakpoint(BreakpointDescriptor* descriptor)
{
    if (!IsPlainMemory(*descriptor))
    {
        descriptor->breakpointID = GenerateNewBreakpointID();
        descriptor->keyAddress = 0xFFFF'FFFF;
        _breakpointMapByID.insert({descriptor->breakpointID, descriptor});
        RebuildFilters();
        return descriptor->breakpointID;
    }

    uint16_t result = BRK_INVALID;
    uint32_t key = 0;
    switch (descriptor->matchType)
    {
        case BRK_MATCH_ADDR:
            // A space page's watchpoint keys apart from the CPU address of the same number
            key = descriptor->spacePage != 0xFFFF
                      ? (0xFE00'0000u | (static_cast<uint32_t>(descriptor->spacePage & 0xFF) << 16) | descriptor->z80address)
                      : (0xFFFF'0000 | descriptor->z80address);
            break;
        case BRK_MATCH_BANK_ADDR:
            // Key format: [pageType:8][page:8][z80address:16]
            key = (static_cast<uint32_t>(descriptor->pageType) << 24) |
                  (static_cast<uint32_t>(descriptor->page) << 16) | descriptor->z80address;
            break;
        default:
            break;
    }

    if (key_exists(_breakpointMapByAddress, key))
    {
        // Such breakpoint already exist, returning it's ID
        result = _breakpointMapByAddress[key]->breakpointID;
    }
    else
    {
        result = GenerateNewBreakpointID();
        descriptor->breakpointID = result;
        descriptor->keyAddress = key;
        _breakpointMapByAddress.insert({key, descriptor});
        _breakpointMapByID.insert({result, descriptor});
        RebuildFilters();
    }
    return result;
}

uint16_t BreakpointManager::AddPortBreakpoint(BreakpointDescriptor* descriptor)
{
    if (!IsPlainPort(*descriptor))
    {
        descriptor->breakpointID = GenerateNewBreakpointID();
        _breakpointMapByID.insert({descriptor->breakpointID, descriptor});
        RebuildFilters();
        return descriptor->breakpointID;
    }

    uint16_t result = BRK_INVALID;
    const uint16_t key = descriptor->z80address;
    auto it = _breakpointMapByPort.find(key);
    if (it != _breakpointMapByPort.end())
    {
        // Such breakpoint already exist, returning it's ID
        result = it->second->breakpointID;
    }
    else
    {
        result = GenerateNewBreakpointID();
        descriptor->breakpointID = result;
        _breakpointMapByPort.insert({key, descriptor});
        _breakpointMapByID.insert({result, descriptor});
        RebuildFilters();
    }
    return result;
}

BreakpointDescriptor* BreakpointManager::FindAddressBreakpoint(uint16_t address)
{
    Memory& memory = *_context->pMemory;
    return FindAddressBreakpoint(address, memory.MapZ80AddressToPhysicalPage(address));
}

/// @brief The breakpoint covering a CPU address with the given page behind it (not the hot path): a physical
/// one first, then a CPU-address one; active or not
BreakpointDescriptor* BreakpointManager::FindAddressBreakpoint(uint16_t address, const MemoryPageDescriptor& pageInfo)
{
    BreakpointDescriptor* byAddress = nullptr;
    for (const auto& [id, bp] : _breakpointMapByID)
    {
        if (!bp || !CoversMemory(*bp, address, pageInfo))
            continue;
        if (bp->matchType == BRK_MATCH_BANK_ADDR)
            return bp;
        if (!byAddress)
            byAddress = bp;
    }
    return byAddress;
}

/// @brief The port breakpoint matching a port (masks applied; not the hot path)
BreakpointDescriptor* BreakpointManager::FindPortBreakpoint(uint16_t port)
{
    for (const auto& [id, bp] : _breakpointMapByID)
        if (bp && bp->type == BRK_IO && (port & bp->portMask) == (bp->z80address & bp->portMask))
            return bp;
    return nullptr;
}

/// @brief Repaints the hot path from the breakpoint set (hotpath-matching-design.md §5.1)
///
/// Called after every mutation (add, remove, enable, disable, change), or once at the end of a batch. Clears
/// the gates, the filters and the resolve tables, then paints every active breakpoint:
///   - CPU addresses and ranges: the filter bits and the CPU-address heads;
///   - physical ones: their page slice, and the filter in all four slots (slot-only: its slot);
///   - ports: the mask expanded into the port filter and heads.
/// The heads come from a sweep over each address space: the set of covering breakpoints only changes at a
/// range boundary, so N ranges make at most 2N + 1 distinct sets, each built once and interned (shared
/// between kinds and pages). At most 64K entries per kind plus 16K per page with breakpoints.
void BreakpointManager::RebuildFilters()
{
    if (_batchDepth > 0)
    {
        _rebuildPending = true;
        return;
    }

    _hotState = BreakpointHotState{};
    _candidateSets.assign(1, {});
    std::map<std::vector<BreakpointDescriptor*>, uint32_t> interned;
    auto intern = [&](const std::vector<BreakpointDescriptor*>& set) -> uint32_t {
        if (set.empty())
            return 0;
        auto it = interned.find(set);
        if (it != interned.end())
            return it->second;
        _candidateSets.push_back(set);
        const uint32_t id = static_cast<uint32_t>(_candidateSets.size() - 1);
        interned.emplace(set, id);
        return id;
    };

    struct Interval
    {
        uint32_t from, to;
        BreakpointDescriptor* bp;
    };
    // Paints `heads[0..size)` from intervals by a sweep (the active set kept in id order)
    auto sweep = [&](std::vector<Interval>& intervals, uint32_t* heads, uint32_t size) {
        std::vector<std::pair<uint32_t, int>> events;  // position, +index (start) / -index-1 (end + 1)
        events.reserve(intervals.size() * 2);
        for (size_t i = 0; i < intervals.size(); i++)
        {
            events.emplace_back(intervals[i].from, static_cast<int>(i));
            events.emplace_back(intervals[i].to + 1, -static_cast<int>(i) - 1);
        }
        std::sort(events.begin(), events.end());
        std::vector<BreakpointDescriptor*> active;
        uint32_t current = 0;
        size_t e = 0;
        for (uint32_t pos = 0; pos < size;)
        {
            bool changed = false;
            while (e < events.size() && events[e].first == pos)
            {
                const int code = events[e].second;
                BreakpointDescriptor* bp = intervals[code >= 0 ? code : -code - 1].bp;
                auto at = std::lower_bound(active.begin(), active.end(), bp, [](BreakpointDescriptor* x, BreakpointDescriptor* y) {
                    return x->breakpointID < y->breakpointID;
                });
                if (code >= 0)
                    active.insert(at, bp);
                else if (at != active.end() && *at == bp)
                    active.erase(at);
                changed = true;
                e++;
            }
            if (changed)
                current = intern(active);
            // Fill up to the next event in one run
            const uint32_t next = e < events.size() ? std::min<uint32_t>(events[e].first, size) : size;
            for (uint32_t p = pos; p < next; p++)
                heads[p] = current;
            pos = next;
        }
    };

    std::vector<Interval> cpu[3];
    std::map<size_t, std::vector<Interval>> pages[3];
    std::map<uint16_t, std::vector<BreakpointDescriptor*>> ports[2];

    for (const auto& [id, bp] : _breakpointMapByID)
    {
        if (!bp || !bp->active)
            continue;
        if (bp->type == BRK_IO)
        {
            for (int d = 0; d < 2; d++)
            {
                if (!(bp->ioType & (d == BRK_PORT_IN ? BRK_IO_IN : BRK_IO_OUT)))
                    continue;
                (d == BRK_PORT_IN ? _hotState.hasPortIn : _hotState.hasPortOut) = 1;
                // Every port the mask lets through: the subsets of the free bits
                const uint16_t want = bp->z80address & bp->portMask;
                const uint16_t free = static_cast<uint16_t>(~bp->portMask);
                uint16_t sub = 0;
                do
                {
                    const uint16_t p = static_cast<uint16_t>(want | sub);
                    _hotState.portFilter[d][p >> 6] |= 1ull << (p & 63);
                    ports[d][p].push_back(bp);  // id order: the map is iterated by id
                    sub = static_cast<uint16_t>((sub - free) & free);
                } while (sub != 0);
            }
            continue;
        }
        if (bp->type != BRK_MEMORY)
            continue;
        if (bp->spacePage != 0xFFFF)
        {
            _hotState.hasSpace = 1;   // matched by its memory's hooks (ResolveSpace), not the CPU filters
            continue;
        }
        for (int k = 0; k < 3; k++)
        {
            const uint8_t bit = k == BRK_KIND_EXEC ? BRK_MEM_EXECUTE : (k == BRK_KIND_READ ? BRK_MEM_READ : BRK_MEM_WRITE);
            if (!(bp->memoryType & bit))
                continue;
            (k == BRK_KIND_EXEC ? _hotState.hasExec : k == BRK_KIND_READ ? _hotState.hasRead : _hotState.hasWrite) = 1;
            if (bp->matchType == BRK_MATCH_ADDR)
            {
                cpu[k].push_back({bp->z80address, bp->EndAddress(), bp});
                for (uint32_t a = bp->z80address; a <= bp->EndAddress(); a++)
                    _hotState.memoryFilter[k][a >> 6] |= 1ull << (a & 63);
                continue;
            }
            const size_t pageId = (static_cast<size_t>(bp->pageType) << 8) | bp->page;
            if (pageId >= kPageIds)
                continue;
            const uint32_t from = bp->z80address & 0x3FFF;
            const uint32_t to = bp->EndAddress() & 0x3FFF;
            pages[k][pageId].push_back({from, to, bp});
            for (uint32_t offset = from; offset <= to; offset++)
            {
                if (bp->slotOnly)
                {
                    const uint32_t a = (bp->z80address & 0xC000) | offset;
                    _hotState.memoryFilter[k][a >> 6] |= 1ull << (a & 63);
                }
                else
                    for (uint32_t slot = 0; slot < 4; slot++)
                    {
                        const uint32_t a = (slot << 14) | offset;
                        _hotState.memoryFilter[k][a >> 6] |= 1ull << (a & 63);
                    }
            }
        }
    }

    for (int k = 0; k < 3; k++)
    {
        _cpuHeads[k].clear();
        if (!cpu[k].empty())
        {
            _cpuHeads[k].assign(0x10000, 0);
            sweep(cpu[k], _cpuHeads[k].data(), 0x10000);
        }
        _pageSlices[k].clear();
        _hasPageSlices[k] = !pages[k].empty();
        if (_hasPageSlices[k])
        {
            _pageSlices[k].resize(kPageIds);
            for (auto& [pageId, intervals] : pages[k])
            {
                _pageSlices[k][pageId] = std::make_unique<PageSlice>();
                sweep(intervals, _pageSlices[k][pageId]->heads.data(), 0x4000);
            }
        }
    }
    for (int d = 0; d < 2; d++)
    {
        _portHeads[d].clear();
        if (ports[d].empty())
            continue;
        _portHeads[d].assign(0x10000, 0);
        for (auto& [port, set] : ports[d])
            _portHeads[d][port] = intern(set);
    }

    // Every mutation of the set ends here: tell the surfaces
    NotifyBreakpointsChanged();
}

void BreakpointManager::BeginBatch()
{
    _batchDepth++;
}

void BreakpointManager::EndBatch()
{
    if (_batchDepth > 0 && --_batchDepth == 0 && _rebuildPending)
    {
        _rebuildPending = false;
        RebuildFilters();
    }
}

std::string BreakpointManager::PublishedFields(const BreakpointDescriptor& bp)
{
    return std::to_string(bp.type) + "|" + std::to_string(bp.matchType) + "|" + std::to_string(bp.memoryType) + "|" +
           std::to_string(bp.ioType) + "|" + std::to_string(bp.keyType) + "|" + std::to_string(bp.z80address) + "|" +
           std::to_string(bp.page) + "|" + std::to_string(bp.pageType) + "|" + (bp.active ? "1" : "0") + "|" +
           std::to_string(bp.EndAddress()) + "|" + (bp.slotOnly ? "s" : "-") + "|" + std::to_string(bp.portMask) + "|" +
           std::to_string(bp.hitMode) + "|" + std::to_string(bp.hitTarget) + "|" +
           bp.owner + "|" + bp.group + "|" + bp.note;
}

void BreakpointManager::NotifyBreakpointsChanged()
{
    // A manager without an emulator (unit tests) has nobody to tell: the changes wait for the next report
    if (!_context || !_context->pEmulator)
        return;
    std::vector<uint16_t> ids = TakeChangedIds();
    if (ids.empty())
        return;
    auto* payload = new BreakpointsChangedPayload(_context->pEmulator->GetId());
    payload->ids = std::move(ids);
    MessageCenter::DefaultMessageCenter().Post(NC_BREAKPOINTS_CHANGED, payload, true);
}

std::vector<uint16_t> BreakpointManager::TakeChangedIds()
{
    std::map<uint16_t, std::string> current;
    for (const auto& [id, bp] : _breakpointMapByID)
        if (bp && !bp->hidden)
            current.emplace(id, PublishedFields(*bp));

    std::vector<uint16_t> ids;
    for (const auto& [id, fields] : current)
    {
        auto was = _published.find(id);
        if (was == _published.end() || was->second != fields)
            ids.push_back(id);  // added or changed
    }
    for (const auto& [id, fields] : _published)
        if (!current.count(id))
            ids.push_back(id);  // removed
    _published.swap(current);
    std::sort(ids.begin(), ids.end());
    return ids;
}

/// endregion </Helper methods>