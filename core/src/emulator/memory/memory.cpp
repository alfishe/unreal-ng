#include "stdafx.h"

#include "common/modulelogger.h"

#include "memory.h"

#include "common/bithelper.h"
#include "common/stringhelper.h"
#include "common/timehelper.h"
#include "debugger/debugmanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/emulator.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include <cassert>

/// region <Constructors / Destructors>

Memory::Memory(EmulatorContext* context)
{
    _context = context;
    _state = &context->emulatorState;
    _logger = context->pModuleLogger;

    // Make power turn-on behavior realistic: all memory cells contain random values
    RandomizeMemoryContent();

    // Reset counters
    ResetCounters();

    // Initialize with default (non-platform specific)
    // base_sos_rom should point to ROM Bank 0 (unit tests depend on that)
    base_sos_rom = ROMPageHostAddress(0);
    base_dos_rom = ROMPageHostAddress(1);
    base_128_rom = ROMPageHostAddress(2);
    base_sys_rom = ROMPageHostAddress(3);

    // Set default memory banks mode
    _bank_mode[0] = BANK_ROM;
    _bank_mode[1] = BANK_RAM;
    _bank_mode[2] = BANK_RAM;
    _bank_mode[3] = BANK_RAM;

    /// region <Debug info>

#ifdef _DEBUG
    // Dump information about all memory regions
    MLOGDEBUG(DumpAllMemoryRegions());
#endif // _DEBUG

    /// endregion </Debug info>
}

Memory::~Memory()
{
    _context = nullptr;

    MLOGDEBUG("Memory::~Memory()");
}

/// endregion </Constructors / Destructors>

/// region <Memory access implementation methods>

MemoryInterface* Memory::GetFastMemoryInterface()
{
    MemoryInterface* result = new MemoryInterface(&Memory::MemoryReadFast, &Memory::MemoryWriteFast);

    return result;
}

MemoryInterface* Memory::GetDebugMemoryInterface()
{
    MemoryInterface* result = new MemoryInterface(&Memory::MemoryReadDebug, &Memory::MemoryWriteDebug);

    return result;
}

/// Implementation memory read method
/// Used from: Z80::FastMemIf
/// \param addr 16-bit address in Z80 memory space
/// \return Byte read from Z80 memory
uint8_t Memory::MemoryReadFast(uint16_t addr, bool isExecution)
{
    // Determine CPU bank (from address bits 14 and 15)
    uint8_t bank = (addr >> 14) & 0b0000'0011;
    uint16_t addressInBank = addr & 0b0011'1111'1111'1111;

    // Read byte from correspondent memory bank mapped to global memory buffer
    uint8_t result = *(_bank_read[bank] + addressInBank);

    return result;
}

// Implementation memory read method
/// Used from: Z80::DbgMemIf
/// \param addr 16-bit address in Z80 memory space
/// \return Byte read from Z80 memory
uint8_t Memory::MemoryReadDebug(uint16_t addr, bool isExecution)
{
    /// region <MemoryReadFast functionality>

    // Determine CPU bank (from address bits 14 and 15)
    uint8_t bank = (addr >> 14) & 0b0000'0011;
    uint16_t addressInBank = addr & 0b0011'1111'1111'1111;

    // Read byte from correspondent memory bank mapped to global memory buffer
    uint8_t result = *(_bank_read[bank] + addressInBank);

    /// endregion </MemoryReadFast functionality>

    /// region  <Update counters>

    uint16_t physicalPage = GetPageForBank(bank);
    size_t physicalOffset = GetPhysicalOffsetForZ80Address(addr);

    if (isExecution)
    {
        // Mark Z80 bank as accessed for code execution
        _memZ80BankExecuteMarks |= (1 << bank);

        // Mark bank as accessed for code execution
        uint8_t pageExecuteMarksByte = physicalPage >> 3;
        uint8_t pageExecuteMarksBit = physicalPage & 0b0000'0111;
        _memPageExecuteMarks[pageExecuteMarksByte] |= (1 << pageExecuteMarksBit);

        // Increment execute access counter for Z80 address
        _memZ80ExecuteCounters[addr] += 1;

        // Increment execute access counter for physical page
        _memPageExecuteCounters[physicalPage] += 1;

        // Increment execute access counter for physical address
        _memAccessExecuteCounters[physicalOffset] += 1;
    }
    else
    {
        // Mark Z80 bank as accessed for read
        _memZ80BankReadMarks |= (1 << bank);

        // Mark bank as accessed for read
        uint8_t pageReadMarksByte = physicalPage >> 3;
        uint8_t pageReadMarksBit = physicalPage & 0b0000'0111;
        _memPageReadMarks[pageReadMarksByte] |= (1 << pageReadMarksBit);

        // Increment read access counter for Z80 address
        _memZ80ReadCounters[addr] += 1;

        // Increment read access counter for physical page
        _memPageReadCounters[physicalPage] += 1;

        // Increment read access counter for physical address
        _memAccessReadCounters[physicalOffset] += 1;
    }

    /// endregion </Update counters>

    /// region <Read breakpoint logic>

    Emulator& emulator = *_context->pEmulator;
    Z80& z80 = *_context->pCore->GetZ80();
    BreakpointManager& brk = *_context->pDebugManager->GetBreakpointsManager();
    uint16_t breakpointID = brk.HandleMemoryRead(addr);
    if (breakpointID != BRK_INVALID)
    {
        // Request to pause emulator
        // Important note: Emulator.Pause() is needed, not CPU.Pause() or Z80.Pause() for successful resume later
        emulator.Pause();

        // Broadcast notification - breakpoint triggered
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        SimpleNumberPayload* payload = new SimpleNumberPayload(breakpointID);
        messageCenter.Post(NC_EXECUTION_BREAKPOINT, payload);

        // Wait until emulator resumed externally (by debugger or scripting engine)
        // Pause emulation until upper-level controller (emulator / scripting) resumes execution
        if (z80.IsPaused())
        {
            while (z80.IsPaused())
            {
                sleep_ms(20);
            }
        }
    }

    /// endregion </Read breakpoint logic>

    return result;
}

/// Implementation memory write method
/// Used from Z80::FastMemIf
/// \param addr 16-bit address in Z80 memory space
/// \param value 8-bit value to write into Z80 memory
void Memory::MemoryWriteFast(uint16_t addr, uint8_t value)
{
    // Determine CPU bank (from address bits 14 and 15)
    uint8_t bank = (addr >> 14) & 0b0000'0011;
    uint16_t addressInBank = addr & 0b0011'1111'1111'1111;

    // Write byte to correspondent memory bank cell
    *(_bank_write[bank] + addressInBank) = value;
}

/// Implementation memory write method
/// Used from Z80::DbgMemIf
/// \param addr 16-bit address in Z80 memory space
/// \param value 8-bit value to write into Z80 memory
void Memory::MemoryWriteDebug(uint16_t addr, uint8_t value)
{
    /// region <MemoryWriteFast functionality>

    // Determine CPU bank (from address bits 14 and 15)
    uint8_t bank = (addr >> 14) & 0b0000'0011;
    uint16_t addressInBank = addr & 0b0011'1111'1111'1111;

    // Write byte to correspondent memory bank cell
    *(_bank_write[bank] + addressInBank) = value;

    /// endregion </MemoryWriteFast functionality>

    /// region  <Update counters>

    uint16_t physicalPage = GetPageForBank(bank);
    size_t physicalOffset = GetPhysicalOffsetForZ80Address(addr);

    // Mark Z80 bank as accessed for write
    _memZ80BankWriteMarks |= (1 << bank);

    // Mark page as accessed for write
    uint8_t pageWriteMarksByte = physicalPage >> 3;
    uint8_t pageWriteMarksBit = physicalPage & 0b0000'0111;
    _memPageWriteMarks[pageWriteMarksByte] |= (1 << pageWriteMarksBit);

    // Increment write access counter for Z80 address
    _memZ80WriteCounters[addr] += 1;

    // Increment write access counter for physical page
    _memPageWriteCounters[physicalPage] += 1;

    // Increment write access counter for physical address
    _memAccessWriteCounters[physicalOffset] += 1;

    /// endregion </Update counters>

    // Raise flag that video memory was changed
    if (addr >= 0x4000 && addr <= 0x5B00)
    {
        _state->video_memory_changed = true;
    }

    /// region <Write breakpoint logic>

    Emulator& emulator = *_context->pEmulator;
    Z80& z80 = *_context->pCore->GetZ80();
    BreakpointManager& brk = *_context->pDebugManager->GetBreakpointsManager();
    uint16_t breakpointID = brk.HandleMemoryWrite(addr);
    if (breakpointID != BRK_INVALID)
    {
        // Request to pause emulator
        // Important note: Emulator.Pause() is needed, not CPU.Pause() or Z80.Pause() for successful resume later
        emulator.Pause();

        // Broadcast notification - breakpoint triggered
        MessageCenter &messageCenter = MessageCenter::DefaultMessageCenter();
        SimpleNumberPayload *payload = new SimpleNumberPayload(breakpointID);
        messageCenter.Post(NC_EXECUTION_BREAKPOINT, payload);

        // Wait until emulator resumed externally (by debugger or scripting engine)
        // Pause emulation until upper-level controller (emulator / scripting) resumes execution
        z80.WaitUntilResumed();
    }

    /// endregion </Write breakpoint logic>
}

/// endregion /<Memory access implementation methods>

/// region <Initialization>

void Memory::Reset()
{
    // Set default banks mapping
    // Bank 0 [0000:3FFF] - ROM
    // Bank 1 [4000:7FFF] - RAM
    // Bank 2 [8000:BFFF] - RAM
    // Bank 3 [C000:FFFF] - RAM
    DefaultBanksFor48k();

    // Reset counters
    ResetCounters();
}

/// Fill whole physical RAM with random values
void Memory::RandomizeMemoryContent()
{
    RandomizeMemoryBlock(RAMPageAddress(0), MAX_RAM_SIZE);
}

/// Fill block of memory with specified size with random values
/// Note: It's assumed that buffer is 32 bit (4 bytes) aligned
/// \param buffer Buffer pointer
/// \param size Size in bytes
void Memory::RandomizeMemoryBlock(uint8_t* buffer, size_t size)
{
    if (buffer == nullptr || size == 0)
    {
        LOGWARNING("Memory::RandomizeMemoryBlock: unable to randomize non-existing block");
        return;
    }

    // Use 32-bit blocks to fill faster
    uint32_t* buffer32bit = (uint32_t*)buffer;
    size_t size32bit = size / 4;

    for (uint32_t i = 0; i < size32bit; i++)
    {
        *(buffer32bit + i) = rand();
    }
}

/// endregion </Initialization>

/// region <Runtime methods>

//
// Switch to certain ROM section RM_SOS | RM_128 | RM_DOS | RM_SYS
// Address space: [0x0000 - 0x3FFF]
void Memory::SetROMMode(ROMModeEnum mode)
{
    throw std::runtime_error("SetROMMode is deprecated");

    EmulatorState& state = _context->emulatorState;
    const CONFIG& config = _context->config;
    const PortDecoder& portDecoder = *_context->pPortDecoder;

	if (mode == RM_NOCHANGE)
		return;


	if (mode == RM_CACHE)
	{
		state.flags |= CF_CACHEON;
	}
	else
	{
		// No RAM/cache/SERVICE
		state.p1FFD &= ~7;
		state.pDFFD &= ~0x10;
		state.flags &= ~CF_CACHEON;

		// comp.aFF77 |= 0x100; // enable ATM memory

		switch (mode)
		{
			case RM_128:
				state.flags &= ~CF_TRDOS;
				state.p7FFD &= ~0x10;
				break;
			case RM_SOS:
				state.flags &= ~CF_TRDOS;
				state.p7FFD |= 0x10;

				if (config.mem_model == MM_PLUS3) // Disable paging
					state.p7FFD |= 0x20;
				break;
			case RM_SYS:
				state.flags |= CF_TRDOS;
				state.p7FFD &= ~0x10;
				break;
			case RM_DOS:
				state.flags |= CF_TRDOS;
				state.p7FFD |= 0x10;

				if (config.mem_model == MM_ATM710 || config.mem_model == MM_ATM3)
					state.p7FFD &= ~0x10;
				break;
		    default:
		        break;
		}
	}

	//SetBanks();
}

/// Set ROM page
/// Address space: [0x0000 - 0x3FFF]
/// \param page ROM page number
void Memory::SetROMPage(uint16_t page, bool updatePorts)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformMemorySubmodulesEnum::SUBMODULE_MEM_ROM;
    /// endregion </Override submodule>

    /// region <Sanity check>
    if (page == MEMORY_UNMAPPABLE || page >= MAX_RAM_PAGES)
    {
        std::string message = StringHelper::Format("Memory::SetROMPage - Invalid bank: %04X provided. MAX_ROM_PAGES: %04X", page, MAX_ROM_PAGES);
        throw std::logic_error(message);
    }
    /// endregion </Sanity check>

    // Set access pointers
    uint8_t* romBankHostAddress = ROMPageHostAddress(page);

    _bank_mode[0] = BANK_ROM;
    _bank_read[0] = romBankHostAddress;
    _bank_write[0] = _memory + TRASH_MEMORY_OFFSET; // Redirect all ROM writes to special memory region

    // Set property flags (_isPage0ROM48k, _isPage0ROM128k, _isPage0ROMDOS, _isPage0ROMService)
    SetROMPageFlags();

    // Update ports information if requested
    if (updatePorts)
        _context->pPortDecoder->SetROMPage(page);

    /// region <Debug info>
    MLOGDEBUG("ROM page %d activated", page);
    /// endregion </Debug info>
}

/// Switch to specified RAM Bank in RAM Page 3
/// Address space: [0x0000 - 0x3FFF]
/// \param page Page number (in 16KiB pages)
void Memory::SetRAMPageToBank0(uint16_t page, bool updatePorts)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformMemorySubmodulesEnum::SUBMODULE_MEM_RAM;
    /// endregion </Override submodule>

    /// region <Sanity check>
    if (page == MEMORY_UNMAPPABLE || page >= MAX_RAM_PAGES)
    {
        std::string message = StringHelper::Format("Memory::SetRAMPageToBank0 - Invalid bank: %04X provided. MAX_RAM_PAGES: %04X", page, MAX_RAM_PAGES);
        throw std::logic_error(message);
    }
    /// endregion </Sanity check>

    _bank_mode[0] = BANK_RAM;
    _bank_write[0] = _bank_read[0] = RAMPageAddress(page);
}

/// Switch to specified RAM Bank in RAM Page 1
/// Address space: [0x4000 - 0x7FFF]
/// \param page Page number (in 16KiB pages)
void Memory::SetRAMPageToBank1(uint16_t page)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformMemorySubmodulesEnum::SUBMODULE_MEM_RAM;
    /// endregion </Override submodule>

    /// region <Sanity check>
    if (page == MEMORY_UNMAPPABLE || page >= MAX_RAM_PAGES)
    {
        std::string message = StringHelper::Format("Memory::SetRAMPageToBank1 - Invalid bank: %04X provided. MAX_RAM_PAGES: %04X", page, MAX_RAM_PAGES);
        throw std::logic_error(message);
    }
    /// endregion </Sanity check>

    _bank_mode[1] = BANK_RAM;
    _bank_write[1] = _bank_read[1] = RAMPageAddress(page);
}

/// Switch to specified RAM Bank in RAM Page 2
/// Address space: [0x8000 - 0xBFFF]
/// \param page Page number (in 16KiB pages)
void Memory::SetRAMPageToBank2(uint16_t page)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformMemorySubmodulesEnum::SUBMODULE_MEM_RAM;
    /// endregion </Override submodule>

    /// region <Sanity check>
    if (page == MEMORY_UNMAPPABLE || page >= MAX_RAM_PAGES)
    {
        std::string message = StringHelper::Format("Memory::SetRAMPageToBank2 - Invalid bank: %04X provided. MAX_RAM_PAGES: %04X", page, MAX_RAM_PAGES);
        throw std::logic_error(message);
    }
    /// endregion </Sanity check>

    _bank_mode[2] = BANK_RAM;
    _bank_write[2] = _bank_read[2] = RAMPageAddress(page);
}

/// Switch to specified RAM Bank in RAM Page 3
/// Address space: [0xC000 - 0xFFFF]
/// \param page Page number (in 16KiB pages)
void Memory::SetRAMPageToBank3(uint16_t page, bool updatePorts)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformMemorySubmodulesEnum::SUBMODULE_MEM_RAM;
    /// endregion </Override submodule>

    /// region <Sanity check>
    if (page == MEMORY_UNMAPPABLE || page >= MAX_RAM_PAGES)
    {
        std::string message = StringHelper::Format("Memory::SetRAMPageToBank3 - Invalid bank: %04X provided. MAX_RAM_PAGES: %04X", page, MAX_RAM_PAGES);
        throw std::logic_error(message);
    }
    /// endregion </Sanity check>

    _bank_mode[3] = BANK_RAM;
    _bank_write[3] = _bank_read[3] = RAMPageAddress(page);

    if (updatePorts)
        _context->pPortDecoder->SetRAMPage(page);
}

bool Memory::IsBank0ROM()
{
    return _bank_mode[0] == BANK_ROM;
}

uint16_t Memory::GetROMPage()
{
    return GetROMPageFromAddress(_bank_read[0]);
}

uint16_t Memory::GetRAMPageForBank0()
{
    return GetRAMPageFromAddress(_bank_read[0]);
}

uint16_t Memory::GetRAMPageForBank1()
{
    return GetRAMPageFromAddress(_bank_read[1]);
}

uint16_t Memory::GetRAMPageForBank2()
{
    return GetRAMPageFromAddress(_bank_read[2]);
}

uint16_t Memory::GetRAMPageForBank3()
{
    return GetRAMPageFromAddress(_bank_read[3]);
}

///
/// \param bank Z80 bank [0:3]
/// \return
uint16_t Memory::GetROMPageForBank(uint8_t bank)
{
    uint16_t result = MEMORY_UNMAPPABLE;
    bank = bank & 0b0000'0011;

    if (_bank_mode[bank] == BANK_ROM)
    {
        result = GetROMPageFromAddress(_bank_read[bank]);
    }

    return result;
}

///
/// \param bank Z80 bank [0:3]
/// \return
uint16_t Memory::GetRAMPageForBank(uint8_t bank)
{
    uint16_t result = MEMORY_UNMAPPABLE;
    bank = bank & 0b0000'0011;

    if (_bank_mode[bank] == BANK_RAM)
    {
        result = GetRAMPageFromAddress(_bank_read[bank]);
    }

    return result;
}

/// Returns absolute page number without distinction to RAM/Cache/Misc/ROM - since they all located in the same block
/// \param bank Z80 bank [0:3]
/// \return
uint16_t Memory::GetPageForBank(uint8_t bank)
{
    uint16_t result = MEMORY_UNMAPPABLE;
    bank = bank & 0b0000'0011;

    uint8_t* bankPageAddress = _bank_read[bank];
    if (bankPageAddress && bankPageAddress >= _memory)
    {
        size_t page = (bankPageAddress - _memory) / PAGE_SIZE;
        if (page < MAX_PAGES)
        {
            result = page;
        }
#ifdef _DEBUG
        else
        {
            std::string message = StringHelper::Format("Memory::GetPageForBank - invalid page %04X detected for bank: %d", page, bank);
            throw std::logic_error(message);
        }
#endif
    }
#ifdef _DEBUG
    else
    {
        std::string message = StringHelper::Format("Memory::GetPageForBank - invalid bankPageAddress=%X detected for bank:%d. _memory=%X", bankPageAddress, bank, _memory);
        throw std::logic_error(message);
    }

    if (result == MEMORY_UNMAPPABLE)
    {
        std::string message = StringHelper::Format("Memory::GetPageForBank - unable to get page for bank:%d", bank);
        throw std::logic_error(message);
    }
#endif

    return result;
}

/// endregion </Runtime methods>

/// region  <Address helper methods>

uint8_t Memory::GetZ80BankFromAddress(uint16_t address)
{
    uint8_t bank = (address >> 14) & 0b0000'0000'0000'0011;

    return bank;
}

/// Up to MAX_RAM_PAGES 256 pages
/// \param page
/// \return
uint8_t* Memory::RAMPageAddress(uint16_t page)
{
    uint8_t* result = nullptr;

    if (page < MAX_RAM_PAGES)
    {
        result = _ramBase + (PAGE_SIZE * page);
    }

    return result;
}

/// Up to MAX_ROM_PAGES 64 pages
/// \param page
/// \return
uint8_t* Memory::ROMPageHostAddress(uint8_t page)
{
    uint8_t* result = nullptr;

    if (page < MAX_ROM_PAGES)
    {
        result = _romBase + (PAGE_SIZE * page);
    }

    return result;
}

/// Returns RAM page number (16KiB page size) for the specified host address if mapping is available
/// \param hostAddress Host address
/// \return RAM page number relative to RAMBase() or MEMORY_UNMAPPABLE =0xFF if address is outside emulated RAM space
uint16_t Memory::GetRAMPageFromAddress(uint8_t* hostAddress)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformMemorySubmodulesEnum::SUBMODULE_MEM_RAM;
    /// endregion </Override submodule>

    uint16_t result = MEMORY_UNMAPPABLE;

    if (hostAddress >= RAMBase() && hostAddress < RAMBase() + MAX_RAM_PAGES * PAGE_SIZE)
    {
        result = (hostAddress - RAMBase()) / PAGE_SIZE;
    }
    else
    {
        MLOGWARNING("Memory::GetRAMPageFromAddress - unable to map 0x%08x to any RAM page:0x%08x-0x%08x", hostAddress, RAMBase(), RAMBase() + MAX_RAM_PAGES * PAGE_SIZE - 1);
    }

    return result;
}

/// Returns ROM page number (16KiB page size) for the specified host address if mapping is available
/// \param hostAddress Host address
/// \return ROM page number relative to ROMBase() or MEMORY_UNMAPPABLE =0xFF if address is outside emulated RAM space
uint16_t Memory::GetROMPageFromAddress(uint8_t* hostAddress)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformMemorySubmodulesEnum::SUBMODULE_MEM_ROM;
    /// endregion </Override submodule>

    uint16_t result = MEMORY_UNMAPPABLE;
    uint8_t* romBase = ROMBase();

#ifndef NDEBUG
    if (hostAddress >= romBase && hostAddress < romBase + MAX_ROM_PAGES * PAGE_SIZE)
    {
        result = (hostAddress - romBase) / PAGE_SIZE;
    }
    else
    {
        MLOGWARNING("Memory::GetRAMPageFromAddress - unable to map 0x%08x to any RAM page:0x%08x-0x%08x", hostAddress, ROMBase(), ROMBase() + MAX_ROM_PAGES * PAGE_SIZE - 1);
    }
#else
    result = (hostAddress - romBase) / PAGE_SIZE;
#endif

    return result;
}

/// Get offset in physical memory area for Z80 address
/// \param address Z80 space adress
/// \return Offset in '_memory' array
size_t Memory::GetPhysicalOffsetForZ80Address(uint16_t address)
{
    uint8_t* physicalAddress = MapZ80AddressToPhysicalAddress(address);

    size_t result = physicalAddress - _memory;

    return result;
}

/// Get offset in physical memory area for Z80 bank start address
/// \param bank Z80 bank index [0:3]
/// \return Offset in '_memory' array
size_t Memory::GetPhysicalOffsetForZ80Bank(uint8_t bank)
{
    bank = bank & 0b0000'0011;

    size_t result = GetPhysicalOffsetForZ80Address(bank * 0x4000);

    return result;
}

uint8_t* Memory::GetPhysicalAddressForZ80Page(uint8_t bank)
{
    uint8_t* result = RAMBase() + GetPhysicalOffsetForZ80Bank(bank);

    return result;
}

/// Returns pointer in Host memory for the address in Z80 space mapped to current bank configuration
/// \param address Z80 space address
/// \return Pointer to Host memory mapped
uint8_t* Memory::MapZ80AddressToPhysicalAddress(uint16_t address)
{
    uint8_t bank = (address >> 14) & 0b0000'0000'0000'0011;
    uint16_t addressInBank = address & 0b0011'1111'1111'1111;

    uint8_t* result = _bank_read[bank] + addressInBank;

    return result;
}

/// Returns MemoryPageDescriptor filled with information about memory page corresponding to specified Z80 address
/// \param address Z80 space address
/// \return memory page descriptor
MemoryPageDescriptor Memory::MapZ80AddressToPhysicalPage(uint16_t address)
{
    MemoryPageDescriptor result;

    uint8_t bank = (address >> 14) & 0b0000'0000'0000'0011;
    uint16_t addressInBank = address & 0b0011'1111'1111'1111;

    result.mode = _bank_mode[bank];
    switch (result.mode)
    {
        case BANK_ROM:
            result.page = GetROMPageFromAddress(_bank_read[bank]);
            break;
        case BANK_RAM:
            result.page = GetRAMPageFromAddress(_bank_read[bank]);
            break;
        default:
            break;
    }

    result.addressInPage = addressInBank;

    return result;
}

/// endregion </Address helper methods>

/// region <Counters>

void Memory::ResetCounters()
{
    memset(_memZ80ReadCounters, 0, sizeof(_memZ80ReadCounters));
    memset(_memZ80WriteCounters, 0, sizeof(_memZ80WriteCounters));
    memset(_memZ80ExecuteCounters, 0, sizeof(_memZ80ExecuteCounters));

    memset(_memAccessReadCounters, 0, sizeof(_memAccessReadCounters));
    memset(_memAccessWriteCounters, 0, sizeof(_memAccessWriteCounters));
    memset(_memAccessExecuteCounters, 0, sizeof(_memAccessExecuteCounters));

    memset(_memPageReadCounters, 0, sizeof(_memPageReadCounters));
    memset(_memPageWriteCounters, 0, sizeof(_memPageWriteCounters));
    memset(_memPageExecuteCounters, 0, sizeof(_memPageExecuteCounters));

    _memZ80BankReadMarks = 0;
    _memZ80BankWriteMarks = 0;
    _memZ80BankExecuteMarks = 0;

    memset(_memPageReadMarks, 0, sizeof(_memPageReadMarks));
    memset(_memPageWriteMarks, 0, sizeof(_memPageWriteMarks));
    memset(_memPageExecuteMarks, 0, sizeof(_memPageExecuteMarks));
}

size_t Memory::GetZ80BankTotalAccessCount(uint8_t bank)
{
    size_t result = 0;

    /// region <Sanity checks>

#ifdef _DEBUG
    if (bank > 3)
        throw std::logic_error("Invalid Z80 bank");
#endif

    bank = bank & 0b0000'0011;

    /// endregion </Sanity checks>

    uint16_t absolutePage = GetPageForBank(bank);
    uint8_t bankBit = 1 << bank;
    bool bankWasRead = _memZ80BankReadMarks & bankBit;
    bool bankWasWritten = _memZ80BankWriteMarks & bankBit;
    bool bankWasExecuted = _memZ80BankExecuteMarks & bankBit;

    if (bankWasRead || bankWasWritten || bankWasExecuted)
    {
        size_t physicalOffset = GetPhysicalOffsetForZ80Bank(bank);

        for (size_t i = physicalOffset; i < physicalOffset + PAGE_SIZE; i++)
        {
            if (bankWasRead)
            {
                result += _memAccessReadCounters[i];
            }

            if (bankWasWritten)
            {
                result += _memAccessWriteCounters[i];
            }

            if (bankWasExecuted)
            {
                result += _memAccessExecuteCounters[i];
            }
        }
    }

    return result;
}

size_t Memory::GetZ80BankReadAccessCount(uint8_t bank)
{
    size_t result = 0;

    /// region <Sanity checks>

#ifdef _DEBUG
    if (bank > 3)
        throw std::logic_error("Invalid Z80 bank");
#endif

    bank = bank & 0b0000'0011;

    /// endregion </Sanity checks>

    uint8_t bankBit = 1 << bank;
    bool bankWasRead = _memZ80BankReadMarks & bankBit;

    if (bankWasRead)
    {
        size_t physicalOffset = GetPhysicalOffsetForZ80Bank(bank);

        for (size_t i = physicalOffset; i < physicalOffset + PAGE_SIZE; i++)
        {
            result += _memAccessReadCounters[i];
        }
    }

    return result;
}

size_t Memory::GetZ80BankWriteAccessCount(uint8_t bank)
{
    size_t result = 0;

    /// region <Sanity checks>

#ifdef _DEBUG
    if (bank > 3)
        throw std::logic_error("Invalid Z80 bank");
#endif

    bank = bank & 0b0000'0011;

    /// endregion </Sanity checks>

    uint8_t bankBit = 1 << bank;
    bool bankWasWritten = _memZ80BankWriteMarks & bankBit;

    if (bankWasWritten)
    {
        size_t physicalOffset = GetPhysicalOffsetForZ80Bank(bank);

        for (size_t i = physicalOffset; i < physicalOffset + PAGE_SIZE; i++)
        {
            result += _memAccessWriteCounters[i];
        }
    }

    return result;
}

size_t Memory::GetZ80BankExecuteAccessCount(uint8_t bank)
{
    size_t result = 0;

    /// region <Sanity checks>

#ifdef _DEBUG
    if (bank > 3)
        throw std::logic_error("Invalid Z80 bank");
#endif

    bank = bank & 0b0000'0011;

    /// endregion </Sanity checks>

    uint8_t bankBit = 1 << bank;
    bool bankWasExecuted = _memZ80BankExecuteMarks & bankBit;

    if (bankWasExecuted)
    {
        size_t physicalOffset = GetPhysicalOffsetForZ80Bank(bank);

        for (size_t i = physicalOffset; i < physicalOffset + PAGE_SIZE; i++)
        {
            result += _memAccessExecuteCounters[i];
        }
    }

    return result;
}

size_t Memory::GetZ80BankTotalAccessCountExclScreen(uint8_t bank)
{
    size_t result = 0;

    /// region <Sanity checks>

#ifdef _DEBUG
    if (bank > 3)
        throw std::logic_error("Invalid Z80 bank");
#endif

    bank = bank & 0b0000'0011;

    /// endregion </Sanity checks>

    if (bank == 1)  // Bank 1 [4000:7F00] contains screen
    {
        // Include access count for all addresses except from [0x4000:0x5AFF] range - standard spectrum screen

        uint8_t bankBit = 1 << bank;
        bool bankWasRead = _memZ80BankReadMarks & bankBit;
        bool bankWasWritten = _memZ80BankWriteMarks & bankBit;
        bool bankWasExecuted = _memZ80BankExecuteMarks & bankBit;

        if (bankWasRead || bankWasWritten || bankWasExecuted)
        {
            size_t physicalOffset = GetPhysicalOffsetForZ80Bank(bank);

            for (size_t i = physicalOffset + 0x1B00; i < physicalOffset + PAGE_SIZE; i++)
            {
                if (bankWasRead)
                {
                    result += _memAccessReadCounters[i];
                }

                if (bankWasWritten)
                {
                    result += _memAccessWriteCounters[i];
                }

                if (bankWasExecuted)
                {
                    result += _memAccessExecuteCounters[i];
                }
            }
        }
    }
    else
    {
        result = GetZ80BankTotalAccessCount(bank);
    }

    return result;
}

size_t Memory::GetZ80BankReadAccessCountExclScreen(uint8_t bank)
{
    size_t result = 0;

    /// region <Sanity checks>

#ifdef _DEBUG
    if (bank > 3)
        throw std::logic_error("Invalid Z80 bank");
#endif

    bank = bank & 0b0000'0011;

    /// endregion </Sanity checks>

    if (bank == 1)  // Bank 1 [4000:7F00] contains screen
    {
        // Include access count for all addresses except from [0x4000:0x5AFF] range - standard spectrum screen

        uint8_t bankBit = 1 << bank;
        bool bankWasRead = _memZ80BankReadMarks & bankBit;
        if (bankWasRead)
        {
            size_t physicalOffset = GetPhysicalOffsetForZ80Bank(bank);

            for (size_t i = physicalOffset + 0x1B00; i < physicalOffset + PAGE_SIZE; i++)
            {
                result += _memAccessReadCounters[i];
            }
        }
    }
    else
    {
        result = GetZ80BankReadAccessCount(bank);
    }

    return result;
}

size_t Memory::GetZ80BankWriteAccessCountExclScreen(uint8_t bank)
{
    size_t result = 0;

    /// region <Sanity checks>

#ifdef _DEBUG
    if (bank > 3)
        throw std::logic_error("Invalid Z80 bank");
#endif

    bank = bank & 0b0000'0011;

    /// endregion </Sanity checks>

    if (bank == 1)  // Bank 1 [4000:7F00] contains screen
    {
        // Include access count for all addresses except from [0x4000:0x5AFF] range - standard spectrum screen

        uint8_t bankBit = 1 << bank;
        bool bankWasWritten = _memZ80BankWriteMarks & bankBit;
        if (bankWasWritten)
        {
            size_t physicalOffset = GetPhysicalOffsetForZ80Bank(bank);

            for (size_t i = physicalOffset + 0x1B00; i < physicalOffset + PAGE_SIZE; i++)
            {
                result += _memAccessWriteCounters[i];
            }
        }
    }
    else
    {
        result = GetZ80BankWriteAccessCount(bank);
    }

    return result;
}

size_t Memory::GetZ80BankExecuteAccessCountExclScreen(uint8_t bank)
{
    size_t result = 0;

    /// region <Sanity checks>

#ifdef _DEBUG
    if (bank > 3)
        throw std::logic_error("Invalid Z80 bank");
#endif

    bank = bank & 0b0000'0011;

    /// endregion </Sanity checks>

    if (bank == 1)  // Bank 1 [4000:7F00] contains screen
    {
        // Include access count for all addresses except from [0x4000:0x5AFF] range - standard spectrum screen

        uint8_t bankBit = 1 << bank;
        bool bankWasExecuted = _memZ80BankExecuteMarks & bankBit;
        if (bankWasExecuted)
        {
            size_t physicalOffset = GetPhysicalOffsetForZ80Bank(bank);

            for (size_t i = physicalOffset + 0x1B00; i < physicalOffset + PAGE_SIZE; i++)
            {
                result += _memAccessExecuteCounters[i];
            }
        }
    }
    else
    {
        result = GetZ80BankExecuteAccessCount(bank);
    }

    return result;
}

/// endregion </Counters>


/// region <Debug methods>

void Memory::SetROM48k(bool updatePorts)
{
    // Set all port values accordingly
    SetROMMode(RM_SOS);

    // Switch to 48k ROM page
    _bank_read[0] = base_sos_rom;
    _bank_write[0] = _memory + TRASH_MEMORY_OFFSET;;
}

void Memory::SetROM128k(bool updatePorts)
{
    // Set all port values accordingly
    SetROMMode(RM_128);

    // Switch to 128k ROM page
    _bank_read[0] = base_128_rom;
    _bank_write[0] = _memory + TRASH_MEMORY_OFFSET;;
}

void Memory::SetROMDOS(bool updatePorts)
{
    // Set all port values accordingly
    SetROMMode(RM_DOS);

    // Switch to DOS ROM page
    _bank_read[0] = base_dos_rom;
    _bank_write[0] = _memory + TRASH_MEMORY_OFFSET;;
}

void Memory::SetROMSystem(bool updatePorts)
{
    // Set all port values accordingly
    SetROMMode(RM_SYS);

    // Switch to DOS ROM page
    _bank_read[0] = base_sys_rom;
    _bank_write[0] = _memory + TRASH_MEMORY_OFFSET;;
}

/// endregion </Debug methods>

/// region <Service methods>

/// Load content from contentBuffer with length size to emulated memory starting specified address in Z80 space
/// Note: current active bank configuration will be used. No overflow after 0xFFFF address available
/// \param contentBuffer
/// \param size
/// \param z80address
void Memory::LoadContentToMemory(uint8_t* contentBuffer, size_t size, uint16_t z80address)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformMemorySubmodulesEnum::SUBMODULE_MEM_RAM;
    /// endregion </Override submodule>

    if (contentBuffer == nullptr || size <= 0)
    {
        MLOGWARNING("Memory::LoadContentToMemory: Nothing to load");
        return;
    }

    // Fill Z80 memory only until 0xFFFF address, then stop
    uint16_t sizeAvailable = 0xFFFF - z80address;
    if (sizeAvailable > size)
        sizeAvailable = size;

    for (uint16_t addr = z80address; addr < z80address + sizeAvailable; addr++)
    {

    }
}

///
/// \param page - RAM page to populate with data
/// \param fromBuffer - source buffer address
/// \param bufferSize - source buffer size
/// Note: if source buffer size > 16k - only first 16k will be loaded
///       if source buffer size < 16k - all it's data will be copied to RAM page, remaining memory kept untouched
void Memory::LoadRAMPageData(uint8_t page, uint8_t* fromBuffer, size_t bufferSize)
{
    /// region <Override submodule>
    static const uint16_t _SUBMODULE = PlatformMemorySubmodulesEnum::SUBMODULE_MEM_RAM;
    /// endregion </Override submodule>

    if (fromBuffer != nullptr && bufferSize > 0)
    {
        if (bufferSize > PAGE_SIZE)
            bufferSize = PAGE_SIZE;

        uint8_t* pageAddress = RAMPageAddress(page);
        memcpy(pageAddress, fromBuffer, bufferSize);
    }
}

/// Set _isPage0ROM48k, _isPage0ROM128k, _isPage0ROMDOS, _isPage0ROMService flags accordingly
/// for current bank mapped to Page0
void Memory::SetROMPageFlags()
{
    // User lookup array
    static constexpr std::array<std::tuple<bool, bool, bool, bool>, 5> romFlagPatterns =
    {
        std::tuple(true, false, false, false),  // 48k
        std::tuple(false, true, false, false),  // 128k
        std::tuple(false, false, true, false),  // DOS
        std::tuple(false, false, false, true),  // Service
        std::tuple(false, false, false, false)  // Everything else (RAM mapped to Page0, extended Scorpion rom bank, etc)
    };

    // Determine current Page0 bank host address
    uint8_t* romBankHostAddress = _bank_read[0];
    std::tuple<bool, bool, bool, bool> flags;

    if (romBankHostAddress == base_sos_rom)
    {
        flags = romFlagPatterns[0];
    }
    else if (romBankHostAddress == base_128_rom)
    {
        flags = romFlagPatterns[1];
    }
    else if (romBankHostAddress == base_dos_rom)
    {
        flags = romFlagPatterns[2];
    }
    else if (romBankHostAddress == base_sys_rom)
    {
        flags = romFlagPatterns[3];
    }
    else // Everything else
    {
        flags = romFlagPatterns[4];
    }

    _isPage0ROM48k = std::get<0>(flags);
    _isPage0ROM128k = std::get<1>(flags);
    _isPage0ROMDOS = std::get<2>(flags);
    _isPge0ROMService = std::get<3>(flags);
}

/// endregion </Service methods>

/// region <Helper methods>


//
//
//
MemoryBankModeEnum Memory::GetMemoryBankMode(uint8_t bank)
{
	if (bank >= 4)
	{
		LOGERROR("Memory::GetMemoryBankMode() - Z80 memory bank can only be [0:3]. Found: %d", bank);
		assert("Invalid Z80 bank");
	}

	return _bank_mode[bank];
}

/// Read single byte from address mapped to Z80 address space
/// Note: Direct access method. Not shown in any traces, memory counters are not incremented
/// \param address - Z80 space address
/// \return 8-bit value from memory on specified address
uint8_t Memory::DirectReadFromZ80Memory(uint16_t address)
{
    uint8_t result = 0x00;

    // Address bits 14 and 15 contain bank number
    uint8_t bank = (address >> 14) & 0b0000'0000'0000'0011;
    address = address & 0b0011'1111'1111'1111;
    result = *(_bank_read[bank] + address);

    return result;
}

/// Write single byte by address mapped to Z80 address space
/// Note: Direct access method. Not shown in any traces, memory counters are not incremented
/// \param address - Z80 space address
/// \param value - Single byte value to be written by address
void Memory::DirectWriteToZ80Memory(uint16_t address, uint8_t value)
{
    // Address bits 14 and 15 contain bank number
    uint8_t bank = (address >> 14) & 0b0000000000000011;
    address = address & 0b0011'1111'1111'1111;
    uint8_t* baseAddress = nullptr;

    if (_bank_mode[bank] == BANK_ROM)
    {
        baseAddress = _bank_read[bank];     // Usually ROM is blocked from write so _bank_write[<bank>] pointer is set to fake region. Use read address
    }
    else
    {
        baseAddress = _bank_write[bank];
    }

    *(baseAddress + address) = value;
}

//
// Initialize according Spectrum 48K standard address space settings
//
void Memory::DefaultBanksFor48k()
{
	// Initialize according Spectrum 128K standard address space settings
	_bank_write[0] = _memory + TRASH_MEMORY_OFFSET;             // ROM is not writable - redirect such requests to unused memory bank
	_bank_read[0] = base_sos_rom;                 		        // 48K (SOS) ROM					for [0x0000 - 0x3FFF]
	_bank_write[1] = _bank_read[1] = RAMPageAddress(5);	        // Set Screen 1 (page 5) as default	for [0x4000 - 0x7FFF]
	_bank_write[2] = _bank_read[2] = RAMPageAddress(2);	        // Set page 2 as default			for [0x8000 - 0xBFFF]
	_bank_write[3] = _bank_read[3] = RAMPageAddress(0);	        // Set page 0 as default			for [0xC000 - 0xFFFF]

	_bank_mode[0] = MemoryBankModeEnum::BANK_ROM;		        // Bank 0 is ROM [0x0000 - 0x3FFF]
	_bank_mode[1] = MemoryBankModeEnum::BANK_RAM;		        // Bank 1 is RAM [0x4000 - 0x7FFF]
	_bank_mode[2] = MemoryBankModeEnum::BANK_RAM;		        // Bank 2 is RAM [0x8000 - 0xBFFF]
	_bank_mode[3] = MemoryBankModeEnum::BANK_RAM;		        // Bank 3 is RAM [0xC000 - 0xFFFF]
}

/// endregion </Helper methods>

/// region <Debug methods>

std::string Memory::GetCurrentBankName(uint8_t bank)
{
    std::string result = "<UNKNOWN>";

    if (bank > 3)
    {
        throw std::logic_error("ZX-Spectrum can only have 4 banks [0:3], 16KiB each");
    }

    bool isROM = _bank_mode[bank] == BANK_ROM;
    uint16_t bankPage = 0;

    if (isROM)
    {
        bankPage = GetROMPageFromAddress(_bank_read[bank]);
        if (bankPage != MEMORY_UNMAPPABLE)
        {
            result = StringHelper::Format("ROM %d", bankPage);
        }
    }
    else
    {
        bankPage = GetRAMPageFromAddress(_bank_read[bank]);
        if (bankPage != MEMORY_UNMAPPABLE)
        {
            result = StringHelper::Format("RAM %d", bankPage);
        }
    }

    return result;
}

std::string Memory::DumpMemoryBankInfo()
{
    std::string result;
    std::stringstream ss;

    for (uint8_t i = 0; i < 4; i++)
    {
        ss << StringHelper::Format("Bank%d: %s; ", i, GetCurrentBankName(i).c_str());
    }
    ss << std::endl;

    result = ss.str();

    return result;
}

std::string Memory::DumpAllMemoryRegions()
{
    std::string result = "\n\nMemory regions:\n";
    result += StringHelper::Format("rambase:  0x%08x\n", _ramBase);
    result += StringHelper::Format("rombase:  0x%08x\n\n", _romBase);

    for (int i = 0; i < 4; i ++)
    {
        result += StringHelper::Format("rompage%d: 0x%08x\n", i, ROMPageHostAddress(i));
    }

    result += "\n";

    for (int i = 0; i < 8; i ++)
    {
        result += StringHelper::Format("rampage%d: 0x%08x\n", i, RAMPageAddress(i));
    }

    result += StringHelper::Format("\nNormal screen (Bank5): 0x%08x\n", RAMPageAddress(5));
    result += StringHelper::Format("Shadow screen (Bank7): 0x%08x\n", RAMPageAddress(7));

    result += "\n";

    return result;
}

/// endregion <Debug methods>
