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

/// endregion </Management assistance methods>

/// region <Runtime methods>
uint16_t BreakpointManager::HandlePCChange(uint16_t pc)
{
    uint16_t result = BRK_INVALID;

    BreakpointDescriptor* breakpoint = FindAddressBreakpoint(pc);
    if (breakpoint)
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
    if (breakpoint)
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
    if (breakpoint)
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
    if (breakpoint)
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
    if (breakpoint)
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

        _breakpointMapByAddress.insert( { key, descriptor });
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