#include "memory.h"

#include <atomic>
#include <cassert>
#include <mutex>
#include <thread>

#include "base/featuremanager.h"
#include "common/bithelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "common/timehelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"
#include "stdafx.h"

// Platform-specific includes for memory mapping
#ifdef _WIN32
#include <fileapi.h>
#include <handleapi.h>
#include <memoryapi.h>
#include <tchar.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/vm_map.h>
#endif
#endif

// Global mutex to serialize shared memory migrations across ALL emulator instances
// This prevents race conditions when multiple emulators toggle shared memory simultaneously
static std::mutex s_sharedMemoryMigrationMutex;

/// region <Constructors / Destructors>

Memory::Memory(EmulatorContext* context)
{
    _context = context;
    _state = &context->emulatorState;
    _logger = context->pModuleLogger;

    // Update feature cache to see what's enabled
    FeatureManager* fm = _context->pFeatureManager;
    if (fm)
    {
        // It's probably better to have a master switch
        bool debugMode = fm->isEnabled(Features::kDebugMode);
        _feature_memorytracking_enabled = debugMode && fm->isEnabled(Features::kMemoryTracking);
        _feature_breakpoints_enabled = debugMode && fm->isEnabled(Features::kBreakpoints);
        _feature_sharedmemory_enabled = fm->isEnabled(Features::kSharedMemory);
    }

    // Allocate ZX-Spectrum memory and make it memory mapped to file for debugging
    // This checks _feature_sharedmemory_enabled internally
    AllocateAndExportMemoryToMmap();

    // Initialize all derived addresses
    _ramBase = _memory;
    _cacheBase = _memory + MAX_RAM_PAGES * PAGE_SIZE;
    _miscBase = _cacheBase + MAX_CACHE_PAGES * PAGE_SIZE;
    _romBase = _miscBase + MAX_MISC_PAGES * PAGE_SIZE;

    // Create memory access tracker
    _memoryAccessTracker = new MemoryAccessTracker(this, context);
    _memoryAccessTracker->Initialize();

    // Memory filling with random values will give a false positive on memory changes analyzer,
    // so disable it if shared memory mapping is enabled
    if (!_feature_sharedmemory_enabled)
    {
        // Make power turn-on behavior realistic: all memory cells contain random values
        RandomizeMemoryContent();
    }

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
    MLOGDEBUG("Memory::Memory() - Instance created");
    MLOGDEBUG("Memory::Memory() - Memory size: %zu bytes", _memorySize);
    MLOGDEBUG("Memory::Memory() - RAM base: %p", _ramBase);
    MLOGDEBUG("Memory::Memory() - Cache base: %p", _cacheBase);
    MLOGDEBUG("Memory::Memory() - Misc base: %p", _miscBase);
    MLOGDEBUG("Memory::Memory() - ROM base: %p", _romBase);
    /// endregion </Debug info>

    if (_memoryAccessTracker)
    {
        _memoryAccessTracker->ResetCounters();
    }
}

Memory::~Memory()
{
    UnmapMemory();

    // Clean up memory access tracker
    if (_memoryAccessTracker != nullptr)
    {
        delete _memoryAccessTracker;
        _memoryAccessTracker = nullptr;
    }

    _context = nullptr;

    MLOGDEBUG("Memory::~Memory()");
}

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
uint8_t Memory::MemoryReadFast(uint16_t addr, [[maybe_unused]] bool isExecution)
{
    // Determine CPU bank (from address bits 14 and 15)
    uint8_t bank = (addr >> 14) & 0b0000'0011;
    uint16_t addressInBank = addr & 0b0011'1111'1111'1111;

    // Read byte from the correspondent memory bank mapped to global memory buffer
    uint8_t result = *(_bank_read[bank] + addressInBank);

    return result;
}

// Implementation memory read method
/// Used from: Z80::DbgMemIf
/// \param addr 16-bit address in Z80 memory space
/// \return Byte read from Z80 memory
uint8_t Memory::MemoryReadDebug(uint16_t addr, [[maybe_unused]] bool isExecution)
{
    /// region <MemoryReadFast functionality>

    // Determine CPU bank (from address bits 14 and 15)
    uint8_t bank = (addr >> 14) & 0b0000'0011;
    uint16_t addressInBank = addr & 0b0011'1111'1111'1111;

    // Read byte from  the correspondent memory bank mapped to global memory buffer
    uint8_t result = *(_bank_read[bank] + addressInBank);

    /// endregion </MemoryReadFast functionality>

    /// region <Memory access tracking>
    if (_memoryAccessTracker != nullptr)
    {
        uint16_t pc = _context->pCore->GetZ80()->m1_pc;
        if (isExecution)
        {
            _memoryAccessTracker->TrackMemoryExecute(addr, pc);
        }
        else
        {
            _memoryAccessTracker->TrackMemoryRead(addr, result, pc);
        }
    }
    /// endregion </Memory access tracking>

    /// region <Read breakpoint logic>
    if (_feature_breakpoints_enabled && _context->pDebugManager != nullptr)
    {
        Emulator& emulator = *_context->pEmulator;
        BreakpointManager& brk = *_context->pDebugManager->GetBreakpointsManager();

        uint16_t breakpointID = brk.HandleMemoryRead(addr);
        if (breakpointID != BRK_INVALID)
        {
            // Pause emulator (single source of truth)
            emulator.Pause();

            // Broadcast notification - breakpoint triggered
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            SimpleNumberPayload* payload = new SimpleNumberPayload(breakpointID);
            messageCenter.Post(NC_EXECUTION_BREAKPOINT, payload);

            // Wait until emulator resumed externally
            emulator.WaitWhilePaused();
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

    // Write byte to the correspondent memory bank cell
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

    // Write byte to the correspondent memory bank cell
    *(_bank_write[bank] + addressInBank) = value;

    /// endregion </MemoryWriteFast functionality>

    // Track memory write if tracker is initialized
    if (_feature_memorytracking_enabled && _memoryAccessTracker != nullptr)
    {
        uint16_t pc = _context->pCore->GetZ80()->m1_pc;
        _memoryAccessTracker->TrackMemoryWrite(addr, value, pc);
    }

    // Raise a flag that video memory was changed
    if (addr >= 0x4000 && addr <= 0x5B00)
    {
        _state->video_memory_changed = true;
    }

    /// region <Write breakpoint logic>
    if (_feature_breakpoints_enabled && _context->pDebugManager != nullptr)
    {
        Emulator& emulator = *_context->pEmulator;
        BreakpointManager& brk = *_context->pDebugManager->GetBreakpointsManager();
        uint16_t breakpointID = brk.HandleMemoryWrite(addr);
        if (breakpointID != BRK_INVALID)
        {
            // Pause emulator (single source of truth)
            emulator.Pause();

            // Broadcast notification - breakpoint triggered
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            SimpleNumberPayload* payload = new SimpleNumberPayload(breakpointID);
            messageCenter.Post(NC_EXECUTION_BREAKPOINT, payload);

            // Wait until emulator resumed externally
            emulator.WaitWhilePaused();
        }
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

    // Reset memory access counters
    if (_memoryAccessTracker)
    {
        _memoryAccessTracker->ResetCounters();
    }
}

/// Fill RAM pages visible in 48K mode with random values
/// Only pages 5 (0x4000-0x7FFF) and 7 (0xC000-0xFFFF in 48K) are randomized.
/// Other pages (0,1,2,3,4,6) remain zeroed to allow empty page detection for snapshot saving.
void Memory::RandomizeMemoryContent()
{
    // Note: full memory randomization was used previously but since not visible
    // to the user, it was replaced with randomization of visible RAM pages only (screen 5 and shadow screen 7)
    // RandomizeMemoryBlock(RAMPageAddress(0), MAX_RAM_SIZE);

    // Page 5: Screen memory and lower RAM (0x4000-0x7FFF in Z80 space)
    RandomizeMemoryBlock(RAMPageAddress(5), PAGE_SIZE);
    
    // Page 7: Not typically visible in 48K but used for shadow screen in 128K
    // Randomize for consistency with visible memory behavior
    RandomizeMemoryBlock(RAMPageAddress(7), PAGE_SIZE);
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

void Memory::AllocateAndExportMemoryToMmap()
{
    // Check if shared memory feature is enabled at runtime
    if (!_feature_sharedmemory_enabled)
    {
        // Feature disabled - allocate regular heap memory
        _memory = new uint8_t[PAGE_SIZE * MAX_PAGES];
        MLOGDEBUG("Memory allocated using heap (sharedmemory feature disabled)");
        return;
    }

    // Shared memory feature is enabled - export memory via mmap/shared memory
    // If we already have a mapped memory, clean it up first
    if (_memory != nullptr)
    {
        UnmapMemory();
    }

    // Generate unique shared memory name using emulator instance ID
    // This supports multiple emulator instances in the same process
    std::string instanceId;
    if (_context && _context->pEmulator)
    {
        instanceId = _context->pEmulator->GetUUID();
        // Truncate UUID to last 12 characters for readability (still unique within process)
        if (instanceId.length() > 12)
        {
            instanceId = instanceId.substr(instanceId.length() - 12);
        }
    }
    else
    {
        // Fallback to PID if emulator not available (shouldn't happen in normal use)
        instanceId = std::to_string(getpid());
    }

#ifdef _WIN32
    // Windows implementation using named shared memory
    std::string shmName = "Local\\zxspectrum_memory-" + instanceId;

    // Clean up any existing mapping with the same name
    if (_mappedMemoryHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(_mappedMemoryHandle);
        _mappedMemoryHandle = INVALID_HANDLE_VALUE;
    }

    // Create a file mapping object with a unique name
    _mappedMemoryHandle = CreateFileMappingA(INVALID_HANDLE_VALUE,         // Use the paging file
                                             NULL,                         // Default security
                                             PAGE_READWRITE | SEC_COMMIT,  // Read/write access and commit all pages
                                             (DWORD)(_memorySize >> 32),   // High-order DWORD of size
                                             (DWORD)(_memorySize & 0xFFFFFFFF),  // Low-order DWORD of size
                                             shmName.c_str()                     // Name of mapping object
    );

    if (_mappedMemoryHandle == NULL)
    {
        DWORD error = GetLastError();
        LOGERROR("Failed to create file mapping object (Error %lu), falling back to heap allocation", error);
        _mappedMemoryHandle = INVALID_HANDLE_VALUE;
        _memory = new uint8_t[PAGE_SIZE * MAX_PAGES];
        return;
    }

    // Map the view of the file mapping into the address space
    _memory = (uint8_t*)MapViewOfFile(_mappedMemoryHandle,  // Handle to map object
                                      FILE_MAP_ALL_ACCESS,  // Read/write permission
                                      0,                    // High-order DWORD of offset
                                      0,                    // Low-order DWORD of offset
                                      _memorySize           // Number of bytes to map
    );

    if (_memory == NULL)
    {
        DWORD error = GetLastError();
        LOGERROR("Failed to map view of file (Error %lu), falling back to heap allocation", error);
        CloseHandle(_mappedMemoryHandle);
        _mappedMemoryHandle = INVALID_HANDLE_VALUE;
        _memory = new uint8_t[PAGE_SIZE * MAX_PAGES];
        return;
    }

    // Store the name for reference
    _mappedMemoryFilepath = shmName;
    LOGINFO("Memory mapped successfully using shared memory: %s (%zu bytes)", shmName.c_str(), _memorySize);

#else
    // Unix/Linux/macOS implementation using POSIX shared memory
    std::string shmName = "/zxspectrum_memory-" + instanceId;

    // Try to clean up any existing shared memory with this name
    shm_unlink(shmName.c_str());

    // Create a new shared memory object
    _mappedMemoryFd = shm_open(shmName.c_str(), O_CREAT | O_RDWR, 0666);
    if (_mappedMemoryFd == -1)
    {
        LOGERROR("Failed to create shared memory object: %s (errno=%d), falling back to heap allocation",
                 strerror(errno), errno);
        _memory = new uint8_t[PAGE_SIZE * MAX_PAGES];
        return;
    }

    // Set the size of the shared memory object
    if (ftruncate(_mappedMemoryFd, _memorySize) == -1)
    {
        LOGERROR("Failed to set size of shared memory object: %s, falling back to heap allocation", strerror(errno));
        close(_mappedMemoryFd);
        shm_unlink(shmName.c_str());
        _mappedMemoryFd = -1;
        _memory = new uint8_t[PAGE_SIZE * MAX_PAGES];
        return;
    }

    // Map the shared memory object into memory
    _memory = (uint8_t*)mmap(NULL,  // Let the kernel choose the address
                             _memorySize, PROT_READ | PROT_WRITE, MAP_SHARED, _mappedMemoryFd, 0);

    if (_memory == MAP_FAILED)
    {
        LOGERROR("Failed to map shared memory: %s (errno=%d), falling back to heap allocation", strerror(errno), errno);
        close(_mappedMemoryFd);
        shm_unlink(shmName.c_str());
        _mappedMemoryFd = -1;
        _memory = new uint8_t[PAGE_SIZE * MAX_PAGES];
        return;
    }

    // Store the name for later cleanup
    _mappedMemoryFilepath = shmName;
    LOGINFO("Memory mapped successfully using shared memory: %s", shmName.c_str());
#endif  // _WIN32

    LOGINFO("Memory mapped successfully. External tools can now access ZX-Spectrum memory in real-time.");
}

void Memory::UnmapMemory()
{
    // Check if we're using shared memory mapping (indicated by non-empty filepath)
    if (!_mappedMemoryFilepath.empty())
    {
        // Shared memory is in use - clean it up
#ifdef _WIN32
        // Windows shared memory cleanup
        if (_memory != nullptr)
        {
            if (!UnmapViewOfFile(_memory))
            {
                DWORD error = GetLastError();
                LOGWARNING("Failed to unmap view of file (Error %lu)", error);
            }
            _memory = nullptr;
        }

        if (_mappedMemoryHandle != INVALID_HANDLE_VALUE)
        {
            if (!CloseHandle(_mappedMemoryHandle))
            {
                DWORD error = GetLastError();
                LOGWARNING("Failed to close shared memory handle (Error %lu)", error);
            }
            _mappedMemoryHandle = INVALID_HANDLE_VALUE;
        }
#else
        // Unix/Linux/macOS shared memory cleanup
        if (_memory != nullptr)
        {
            if (munmap(_memory, _memorySize) == -1)
            {
                LOGWARNING("Failed to unmap shared memory: %s", strerror(errno));
            }
            _memory = nullptr;
        }

        if (_mappedMemoryFd != -1)
        {
            close(_mappedMemoryFd);
            _mappedMemoryFd = -1;
        }

        shm_unlink(_mappedMemoryFilepath.c_str());
#endif  // _WIN32
        _mappedMemoryFilepath.clear();
    }
    else
    {
        // Regular heap memory - just delete
        if (_memory)
        {
            delete[] _memory;
            _memory = nullptr;
        }
    }
}

// Sync shared memory for external viewers
// For POSIX shm_open based shared memory, we use a memory barrier
// to ensure writes are visible to other processes (msync is for disk persistence)
void Memory::SyncToDisk()
{
    // Only sync if shared memory is in use
    if (_mappedMemoryFilepath.empty() || !_memory)
    {
        return;
    }

#ifndef _WIN32
    // Use a full memory barrier to ensure all writes are visible
    // This is more appropriate for shm_open-based shared memory than msync
    __sync_synchronize();
    
    // Also use msync with MS_INVALIDATE to force cache coherence
    msync(_memory, _memorySize, MS_SYNC | MS_INVALIDATE);
#else
    if (_mappedMemoryHandle != INVALID_HANDLE_VALUE)
    {
        if (FlushViewOfFile(_memory, _memorySize) == 0)
        {
            LOGWARNING("Windows: FlushViewOfFile failed: " + std::to_string(GetLastError()));
        }
    }
#endif
}

/// @brief Migrate all cached pointers after memory base address changes.
/// 
/// This method is called during heap↔shared memory transitions when the SharedMemory
/// feature is toggled at runtime. When switching between regular heap allocation and
/// POSIX/Windows shared memory (for external tools like Screen Viewer), the entire
/// memory buffer is reallocated at a different address.
/// 
/// Calculates the offset between old and new base addresses, then applies
/// this offset to all cached pointers that reference the memory region.
/// 
/// @param oldBase The previous memory base address (heap or shared memory)
/// @param newBase The new memory base address (shared memory or heap)
void Memory::MigratePointersAfterReallocation(uint8_t* oldBase, uint8_t* newBase)
{
    // Calculate pointer offset: how much each pointer needs to shift
    // This preserves relative positioning within the memory region
    ptrdiff_t offset = newBase - oldBase;

    // Step 1: Update derived base addresses
    // These are the fundamental region pointers that other calculations depend on
    _ramBase = newBase;                                           // RAM pages start at base
    _cacheBase = newBase + MAX_RAM_PAGES * PAGE_SIZE;             // Cache follows RAM
    _miscBase = _cacheBase + MAX_CACHE_PAGES * PAGE_SIZE;         // Misc follows cache
    _romBase = _miscBase + MAX_MISC_PAGES * PAGE_SIZE;            // ROM follows misc

    // Step 2: Update Z80 bank pointers
    // These are the hot-path pointers used by Z80::DirectRead/DirectWrite
    // Stale pointers here cause reads/writes to freed memory!
    for (int i = 0; i < 4; i++)
    {
        if (_bank_read[i] != nullptr)
            _bank_read[i] += offset;     // Bank read pointer for Z80 address space window i
        if (_bank_write[i] != nullptr)
            _bank_write[i] += offset;    // Bank write pointer for Z80 address space window i
    }

    // Step 3: Update ROM base pointers
    // These shortcuts point to specific ROM pages used by SetROMPage()
    if (base_sos_rom != nullptr) base_sos_rom += offset;   // SOS 48K ROM
    if (base_dos_rom != nullptr) base_dos_rom += offset;   // TR-DOS ROM
    if (base_128_rom != nullptr) base_128_rom += offset;   // 128K ROM
    if (base_sys_rom != nullptr) base_sys_rom += offset;   // System/service ROM

    // Step 4: Notify dependent subsystems to refresh their cached pointers
    // Screen caches _activeScreenMemoryOffset for rendering performance
    if (_context->pScreen)
    {
        _context->pScreen->RefreshMemoryPointers();
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

    [[maybe_unused]] EmulatorState& state = _context->emulatorState;
    [[maybe_unused]] const CONFIG& config = _context->config;
    [[maybe_unused]] const PortDecoder& portDecoder = *_context->pPortDecoder;

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

                if (config.mem_model == MM_PLUS3)  // Disable paging
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

    // SetBanks();
}

/// input: ports 7FFD,1FFD,DFFD,FFF7,FF77,EFF7, flags CF_TRDOS,CF_CACHEON
void Memory::UpdateZ80Banks()
{
    EmulatorState& state = _context->emulatorState;

    if (state.flags & CF_TRDOS)
    {
        if (state.p7FFD & 0x10)
        {
            SetROMDOS();
        }
        else
        {
            SetROMSystem();
        }
    }
    else
    {
        if (state.p7FFD & 0x10)
        {
            SetROM48k();
        }
        else
        {
            SetROM128k();
        }
    }

    // TODO: implement support for extended ports and cache
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
        std::string message = StringHelper::Format(
            "Memory::SetROMPage - Invalid bank: %04X provided. MAX_ROM_PAGES: %04X", page, MAX_ROM_PAGES);
        throw std::logic_error(message);
    }
    /// endregion </Sanity check>

    // Set access pointers
    uint8_t* romBankHostAddress = ROMPageHostAddress(page);

    _bank_mode[0] = BANK_ROM;
    _bank_read[0] = romBankHostAddress;
    _bank_write[0] = _memory + TRASH_MEMORY_OFFSET;  // Redirect all ROM writes to special memory region

    // Set property flags (_isPage0ROM48k, _isPage0ROM128k, _isPage0ROMDOS, _isPage0ROMService)
    SetROMPageFlags();

    // Update ports information if requested
    if (updatePorts)
        _context->pPortDecoder->SetROMPage(page);

    /// region <Debug info>
    MLOGDEBUG("ROM page %d activated. pc: 0x%04X", page, _context->pCore->GetZ80()->pc);
    /// endregion </Debug info>
}

/// Switch to specified RAM Bank in RAM Page 3
/// Address space: [0x0000 - 0x3FFF]
/// \param page Page number (in 16KiB pages)
void Memory::SetRAMPageToBank0(uint16_t page, [[maybe_unused]] bool updatePorts)
{
    /// region <Override submodule>
    [[maybe_unused]]
    static const uint16_t _SUBMODULE = PlatformMemorySubmodulesEnum::SUBMODULE_MEM_RAM;
    /// endregion </Override submodule>

    /// region <Sanity check>
    if (page == MEMORY_UNMAPPABLE || page >= MAX_RAM_PAGES)
    {
        std::string message = StringHelper::Format(
            "Memory::SetRAMPageToBank0 - Invalid bank: %04X provided. MAX_RAM_PAGES: %04X", page, MAX_RAM_PAGES);
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
    [[maybe_unused]]
    static const uint16_t _SUBMODULE = PlatformMemorySubmodulesEnum::SUBMODULE_MEM_RAM;
    /// endregion </Override submodule>

    /// region <Sanity check>
    if (page == MEMORY_UNMAPPABLE || page >= MAX_RAM_PAGES)
    {
        std::string message = StringHelper::Format(
            "Memory::SetRAMPageToBank1 - Invalid bank: %04X provided. MAX_RAM_PAGES: %04X", page, MAX_RAM_PAGES);
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
    [[maybe_unused]]
    static const uint16_t _SUBMODULE = PlatformMemorySubmodulesEnum::SUBMODULE_MEM_RAM;
    /// endregion </Override submodule>

    /// region <Sanity check>
    if (page == MEMORY_UNMAPPABLE || page >= MAX_RAM_PAGES)
    {
        std::string message = StringHelper::Format(
            "Memory::SetRAMPageToBank2 - Invalid bank: %04X provided. MAX_RAM_PAGES: %04X", page, MAX_RAM_PAGES);
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
    [[maybe_unused]]
    static const uint16_t _SUBMODULE = PlatformMemorySubmodulesEnum::SUBMODULE_MEM_RAM;
    /// endregion </Override submodule>

    /// region <Sanity check>
    if (page == MEMORY_UNMAPPABLE || page >= MAX_RAM_PAGES)
    {
        std::string message = StringHelper::Format(
            "Memory::SetRAMPageToBank3 - Invalid bank: %04X provided. MAX_RAM_PAGES: %04X", page, MAX_RAM_PAGES);
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
            std::string message =
                StringHelper::Format("Memory::GetPageForBank - invalid page %04X detected for bank: %d", page, bank);
            throw std::logic_error(message);
        }
#endif
    }
#ifdef _DEBUG
    else
    {
        std::string message =
            StringHelper::Format("Memory::GetPageForBank - invalid bankPageAddress=%X detected for bank:%d. _memory=%X",
                                 bankPageAddress, bank, _memory);
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
        MLOGWARNING("Memory::GetRAMPageFromAddress - unable to map 0x%08x to any RAM page:0x%08x-0x%08x", hostAddress,
                    RAMBase(), RAMBase() + MAX_RAM_PAGES * PAGE_SIZE - 1);
    }

    return result;
}

/// Returns ROM page number (16KiB page size) for the specified host address if mapping is available
/// \param hostAddress Host address
/// \return ROM page number relative to ROMBase() or MEMORY_UNMAPPABLE =0xFF if address is outside emulated RAM space
uint16_t Memory::GetROMPageFromAddress(uint8_t* hostAddress)
{
    /// region <Override submodule>
    [[maybe_unused]]
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
        MLOGWARNING("Memory::GetRAMPageFromAddress - unable to map 0x%08x to any RAM page:0x%08x-0x%08x", hostAddress,
                    ROMBase(), ROMBase() + MAX_ROM_PAGES * PAGE_SIZE - 1);
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

/// region <Debug methods>

void Memory::SetROM48k(bool updatePorts)
{
    // Switch to 48k (SOS) ROM page
    _bank_mode[0] = BANK_ROM;
    _bank_read[0] = base_sos_rom;
    _bank_write[0] = _memory + TRASH_MEMORY_OFFSET;
    
    // Update ROM page identification flags
    SetROMPageFlags();
    
    // Update port decoder state if requested
    if (updatePorts && _context->pPortDecoder)
    {
        _context->pPortDecoder->SetROMPage(GetROMPageFromAddress(base_sos_rom));
    }
}

void Memory::SetROM128k(bool updatePorts)
{
    // Switch to 128k ROM page
    _bank_mode[0] = BANK_ROM;
    _bank_read[0] = base_128_rom;
    _bank_write[0] = _memory + TRASH_MEMORY_OFFSET;
    
    // Update ROM page identification flags
    SetROMPageFlags();
    
    // Update port decoder state if requested
    if (updatePorts && _context->pPortDecoder)
    {
        _context->pPortDecoder->SetROMPage(GetROMPageFromAddress(base_128_rom));
    }
}

void Memory::SetROMDOS(bool updatePorts)
{
    // Switch to DOS ROM page
    _bank_mode[0] = BANK_ROM;
    _bank_read[0] = base_dos_rom;
    _bank_write[0] = _memory + TRASH_MEMORY_OFFSET;
    
    // Update ROM page identification flags
    SetROMPageFlags();
    
    // Update port decoder state if requested (like regular Z80 OUT to port 1FFD)
    if (updatePorts && _context->pPortDecoder)
    {
        _context->pPortDecoder->SetROMPage(GetROMPageFromAddress(base_dos_rom));
    }
}

void Memory::SetROMSystem(bool updatePorts)
{
    // Switch to System ROM page
    _bank_mode[0] = BANK_ROM;
    _bank_read[0] = base_sys_rom;
    _bank_write[0] = _memory + TRASH_MEMORY_OFFSET;
    
    // Update ROM page identification flags
    SetROMPageFlags();
    
    // Update port decoder state if requested
    if (updatePorts && _context->pPortDecoder)
    {
        _context->pPortDecoder->SetROMPage(GetROMPageFromAddress(base_sys_rom));
    }
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
    [[maybe_unused]]
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
    static constexpr std::array<std::tuple<bool, bool, bool, bool>, 5> romFlagPatterns = {
        std::tuple(true, false, false, false),  // 48k
        std::tuple(false, true, false, false),  // 128k
        std::tuple(false, false, true, false),  // DOS
        std::tuple(false, false, false, true),  // Service
        std::tuple(false, false, false,
                   false)  // Everything else (RAM mapped to Page0, extended Scorpion rom bank, etc)
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
    else  // Everything else
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
        baseAddress = _bank_read[bank];  // Usually ROM is blocked from write so _bank_write[<bank>] pointer is set to
                                         // fake region. Use read address
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
    _bank_write[0] =
        _memory + TRASH_MEMORY_OFFSET;  // ROM is not writable - redirect such requests to unused memory bank
    _bank_read[0] = base_sos_rom;       // 48K (SOS) ROM					for [0x0000 - 0x3FFF]
    _bank_write[1] = _bank_read[1] = RAMPageAddress(5);  // Set Screen 1 (page 5) as default	for [0x4000 - 0x7FFF]
    _bank_write[2] = _bank_read[2] = RAMPageAddress(2);  // Set page 2 as default			for [0x8000 - 0xBFFF]
    _bank_write[3] = _bank_read[3] = RAMPageAddress(0);  // Set page 0 as default			for [0xC000 - 0xFFFF]

    _bank_mode[0] = MemoryBankModeEnum::BANK_ROM;  // Bank 0 is ROM [0x0000 - 0x3FFF]
    _bank_mode[1] = MemoryBankModeEnum::BANK_RAM;  // Bank 1 is RAM [0x4000 - 0x7FFF]
    _bank_mode[2] = MemoryBankModeEnum::BANK_RAM;  // Bank 2 is RAM [0x8000 - 0xBFFF]
    _bank_mode[3] = MemoryBankModeEnum::BANK_RAM;  // Bank 3 is RAM [0xC000 - 0xFFFF]
}

/// endregion </Helper methods>

/// region <Debug methods>

/// Gets the bank name for a given Z80 address
/// @param address - Address in Z80 address space (0x0000-0xFFFF)
/// @return bank name string (e.g., "ROM 0", "RAM 5")
std::string Memory::GetBankNameForAddress(uint16_t address)
{
    // Determine which 16KB bank the address belongs to (0-3)
    uint8_t bank = address >> 14;  // Divide by 16384 (0x4000) to get bank number

    // Use the existing method to get the bank name
    return GetCurrentBankName(bank);
}

/// Gets the current bank name for a given bank index
/// @param bank - Bank index (0-3)
/// @return bank name string (e.g., "ROM 0", "RAM 5")
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

/// Dumps memory bank information
/// @return string containing bank information for all four banks
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

/// Dumps all memory regions
/// @return string containing memory regions information
std::string Memory::DumpAllMemoryRegions()
{
    std::string result = "\n\nMemory regions:\n";
    result += StringHelper::Format("rambase:  0x%08x\n", _ramBase);
    result += StringHelper::Format("rombase:  0x%08x\n\n", _romBase);

    for (int i = 0; i < 4; i++)
    {
        result += StringHelper::Format("rompage%d: 0x%08x\n", i, ROMPageHostAddress(i));
    }

    result += "\n";

    for (int i = 0; i < 8; i++)
    {
        result += StringHelper::Format("rampage%d: 0x%08x\n", i, RAMPageAddress(i));
    }

    result += StringHelper::Format("\nNormal screen (Bank5): 0x%08x\n", RAMPageAddress(5));
    result += StringHelper::Format("Shadow screen (Bank7): 0x%08x\n", RAMPageAddress(7));

    result += "\n";

    return result;
}

/// endregion <Debug methods>

// Update feature cache (call when features change at runtime)
void Memory::UpdateFeatureCache()
{
    FeatureManager* fm = _context->pFeatureManager;
    if (fm)
    {
        bool debugMode = fm->isEnabled(Features::kDebugMode);
        _feature_memorytracking_enabled = debugMode && fm->isEnabled(Features::kMemoryTracking);
        _feature_breakpoints_enabled = debugMode && fm->isEnabled(Features::kBreakpoints);

        // Handle sharedmemory feature - can be toggled at runtime
        bool sharedMemoryRequested = fm->isEnabled(Features::kSharedMemory);
        if (sharedMemoryRequested != _feature_sharedmemory_enabled)
        {
            // GLOBAL LOCK: Serialize all shared memory migrations across ALL emulators
            // This prevents race conditions when multiple emulators toggle shared memory simultaneously
            std::lock_guard<std::mutex> lock(s_sharedMemoryMigrationMutex);
            
            // Re-check after acquiring lock (another thread might have changed state)
            sharedMemoryRequested = fm->isEnabled(Features::kSharedMemory);
            if (sharedMemoryRequested == _feature_sharedmemory_enabled)
            {
                // State already changed by another thread while we waited for lock
                return;
            }
            
            // CRITICAL: Pause emulator before memory migration to prevent race conditions
            // The Z80 thread would access stale bank pointers during reallocation
            Emulator* emulator = _context->pEmulator;
            bool wasRunning = emulator && emulator->IsRunning() && !emulator->IsPaused();
            
            LOGDEBUG("Memory::UpdateFeatureCache - SharedMemory toggle: requested=%s, current=%s, emulator=%p, wasRunning=%s",
                    sharedMemoryRequested ? "ON" : "OFF",
                    _feature_sharedmemory_enabled ? "ON" : "OFF",
                    (void*)emulator,
                    wasRunning ? "YES" : "NO");
            
            if (wasRunning)
            {
                LOGDEBUG("Memory::UpdateFeatureCache - Pausing emulator before migration (silent)");
                // Use silent pause (broadcast=false) to avoid triggering UI updates during migration
                // UI refresh during migration would access memory being reallocated
                emulator->Pause(false);
                // MainLoop::Pause() now waits for Z80 thread to confirm it's paused via CV
                LOGDEBUG("Memory::UpdateFeatureCache - Emulator paused, proceeding with migration");
            }

            if (sharedMemoryRequested && !_feature_sharedmemory_enabled)
            {
                // Transition: OFF -> ON (enable shared memory)
                // If we have heap memory, need to migrate to shared memory
                if (_memory != nullptr && _mappedMemoryFilepath.empty())
                {
                    // Save current memory content
                    uint8_t* oldMemory = _memory;

                    // Set flag before allocation so AllocateAndExportMemoryToMmap uses shared memory
                    _feature_sharedmemory_enabled = true;

                    // Allocate new shared memory
                    _memory = nullptr;
                    AllocateAndExportMemoryToMmap();

                    if (_memory != nullptr && !_mappedMemoryFilepath.empty())
                    {
                        // Copy content from old heap to new shared memory
                        memcpy(_memory, oldMemory, _memorySize);
                        
                        // Flush shared memory to ensure external processes see the data immediately
                        msync(_memory, _memorySize, MS_SYNC | MS_INVALIDATE);

                        // Migrate all cached pointers to reference new memory location
                        // See MigratePointersAfterReallocation() for full documentation
                        MigratePointersAfterReallocation(oldMemory, _memory);

                        // Free old heap memory (safe now that all pointers updated)
                        delete[] oldMemory;

                        LOGDEBUG("Shared memory enabled - migrated %zu bytes to shared memory", _memorySize);
                    }
                    else
                    {
                        // Fallback - shared memory creation failed, restore old memory
                        _memory = oldMemory;
                        _feature_sharedmemory_enabled = false;
                        LOGWARNING("Failed to enable shared memory - keeping heap allocation");
                    }
                }
                else
                {
                    _feature_sharedmemory_enabled = true;
                }
            }
            else if (!sharedMemoryRequested && _feature_sharedmemory_enabled)
            {
                // Transition: ON -> OFF (disable shared memory)
                // Migrate from shared memory to heap memory
                if (_memory != nullptr && !_mappedMemoryFilepath.empty())
                {
                    // Save old shared memory pointer for offset calculation
                    uint8_t* oldMemory = _memory;

                    // Allocate new heap memory
                    uint8_t* newMemory = new uint8_t[_memorySize];

                    // Copy content from shared memory to heap
                    memcpy(newMemory, _memory, _memorySize);

                    // Unmap shared memory (this sets _memory to nullptr)
                    UnmapMemory();

                    // Set new heap memory
                    _memory = newMemory;

                    // Migrate all cached pointers to reference new memory location
                    // See MigratePointersAfterReallocation() for full documentation
                    MigratePointersAfterReallocation(oldMemory, _memory);

                    LOGDEBUG("Shared memory disabled - migrated %zu bytes to heap memory", _memorySize);
                }

                _feature_sharedmemory_enabled = false;
            }
            
            // Resume emulator after migration completes
            if (wasRunning)
            {
                // Memory barrier ensures all pointer updates are visible to all CPU cores
                // before Z80 thread resumes execution (critical for ARM's weak memory model)
                std::atomic_thread_fence(std::memory_order_seq_cst);
                
                // Use silent resume (broadcast=false) to match silent pause
                emulator->Resume(false);
            }
        }

        // Update memory access tracker if it exists
        if (_memoryAccessTracker)
        {
            _memoryAccessTracker->UpdateFeatureCache();
        }
    }
    else
    {
        _feature_memorytracking_enabled = false;
        _feature_breakpoints_enabled = false;
        _feature_sharedmemory_enabled = false;
    }
}
