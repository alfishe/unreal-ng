#include "stdafx.h"

#include "common/modulelogger.h"

#include "loader_sna.h"

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"

/// region <Constructors / destructors>

LoaderSNA::LoaderSNA(EmulatorContext* context, const std::string& path)
{
    _context = context;
    _logger = context->pModuleLogger;

    _path = FileHelper::AbsolutePath(path, false);  // Expand tilde, don't resolve symlinks for non-existent files

    // Initialize memory pages with proper size
    for (int i = 0; i < 8; i++)
    {
        memset(_memoryPages[i], 0x00, PAGE_SIZE);
        _memoryPagesUsed[i] = false;
    }
}

LoaderSNA::~LoaderSNA()
{
    if (_file)
    {
        FileHelper::CloseFile(_file);
        _file = nullptr;
    }
}

/// endregion </Constructors / destructors>

/// region <Public methods>

/// Multi-stage snapshot loading
/// Guarantees that if SNA file is invalid / corrupted - current emulator session and memory content preserved
/// \return result for snapshot load operation
bool LoaderSNA::load()
{
    bool result = false;

    if (validate())
    {
        if (loadToStaging())
        {
            result = applySnapshotFromStaging();
        }
    }

    return result;
}

/// endregion </Public methods>

/// region <Helper methods>
bool LoaderSNA::validate()
{
    bool result = false;

    if (FileHelper::FileExists(_path))
    {
        _file = FileHelper::OpenExistingFile(_path);
        if (_file != nullptr)
        {
            _fileSize = FileHelper::GetFileSize(_file);

            if (is48kSnapshot(_file))
            {
                _snapshotMode = SNA_48;
                
                // 48K SNA must be exactly 49179 bytes (27 header + 49152 RAM)
                const size_t expected48kSize = _snaHeaderSize + 3 * PAGE_SIZE;
                if (_fileSize != expected48kSize)
                {
                    MLOGWARNING("Invalid 48K SNA file size: %zu (expected %zu)", _fileSize, expected48kSize);
                    _snapshotMode = SNA_UNKNOWN;
                }
            }
            else if (is128kSnapshot(_file))
            {
                _snapshotMode = SNA_128;
                
                // 128K SNA format is flexible - minimum is 49183 bytes (base structure),
                // plus 0-8 additional 16KB RAM banks depending on which banks were saved
                // Common sizes: 131103 bytes (5 banks), 147487 bytes (8 banks)
                const size_t min128kSize = _snaHeaderSize + 3 * PAGE_SIZE + sizeof(sna128Header);
                size_t remainingBytes = _fileSize - min128kSize;
                size_t additionalBanks = remainingBytes / PAGE_SIZE;
                
                // Validate that we have whole banks (no partial pages)
                if (remainingBytes % PAGE_SIZE != 0)
                {
                    MLOGWARNING("Invalid 128K SNA file size: %zu (has partial page: %zu bytes)", _fileSize, remainingBytes % PAGE_SIZE);
                    _snapshotMode = SNA_UNKNOWN;
                }
                // Validate we have at least 1 additional bank (128K SNA must have more than just base structure)
                else if (additionalBanks < 1)
                {
                    MLOGWARNING("Invalid 128K SNA: no additional banks (size %zu)", _fileSize);
                    _snapshotMode = SNA_UNKNOWN;
                }
                // Validate we don't have more than 8 additional banks
                else if (additionalBanks > 8)
                {
                    MLOGWARNING("Invalid 128K SNA: too many banks (%zu banks, max 8)", additionalBanks);
                    _snapshotMode = SNA_UNKNOWN;
                }
            }
        }
    }

    if (_snapshotMode != SNA_UNKNOWN)
    {
        result = true;
    }

    // Persist validation state in the field
    _fileValidated = result;

    /// region <Info logging>
    if (result)
    {
        std::string version;
        if (_snapshotMode == SNA_48)
        {
            version = "SNA48";
        }
        if (_snapshotMode == SNA_128)
        {
            version = "SNA128";
        }

        MLOGINFO("Valid SNA file, type: %s, size: %d path: '%s'", version.c_str(), _fileSize, _path.c_str());
    }
    else
    {
        std::string version = "UNKNOWN";
        if (_snapshotMode == SNA_48)
        {
            version = "SNA48";
        }
        if (_snapshotMode == SNA_128)
        {
            version = "SNA128";
        }

        MLOGWARNING("File is not valid SNA, type: %s, size: %d '%s'", version.c_str(), _fileSize, _path.c_str());
    }
    /// endregion </Info logging>

    return result;
}

bool LoaderSNA::is48kSnapshot(FILE* file)
{
    bool result = false;

    size_t fileSize = FileHelper::GetFileSize(file);
    
    // Minimum size check: must have at least header (27 bytes)
    if (fileSize < _snaHeaderSize)
    {
        return false;
    }
    
    size_t headerSize = fileSize % PAGE_SIZE;

    if (headerSize == _snaHeaderSize)
    {
        result = true;
    }

    return result;
}

bool LoaderSNA::is128kSnapshot(FILE* file)
{
    bool result = false;

    size_t fileSize = FileHelper::GetFileSize(file);
    
    // Minimum size for 128K SNA: header (27) + 3 banks (48KB) + extended header (4)
    const size_t min128kSize = _snaHeaderSize + 3 * PAGE_SIZE + sizeof(sna128Header);
    
    if (fileSize < min128kSize)
    {
        return false;
    }
    
    // 128K SNA format is flexible - after the base structure (49183 bytes),
    // it can contain anywhere from 1 to 8 additional 16KB RAM banks
    // Check if remaining data after base structure is a multiple of PAGE_SIZE and non-zero
    size_t remainingBytes = fileSize - min128kSize;
    
    if (remainingBytes > 0 && remainingBytes % PAGE_SIZE == 0)
    {
        result = true;
    }

    return result;
}

bool LoaderSNA::loadToStaging()
{
    bool result = false;

    switch (_snapshotMode)
    {
        case SNA_48:
            result = load48kToStaging();
            break;
        case SNA_128:
            result = load128kToStaging();
            break;
        default:
            break;
    }

    if (_file)
    {
        FileHelper::CloseFile(_file);
        _file = nullptr;
    }

    return result;
}

bool LoaderSNA::load48kToStaging()
{
    bool result = false;

    if (_snapshotMode == SNA_48 && _file != nullptr)
    {
        // Ensure we're reading from file start
        rewind(_file);

        // Read SNA common header
        if (fread(&_header, sizeof(_header), 1, _file) != 1)
        {
            return false;
        }

        _borderColor = _header.border & 0b0000'0111;

        // Read 48K RAM (3 x 16KB pages)
        // Bank 5 [4000:7FFF]
        if (fread(&_memoryPages[5], PAGE_SIZE, 1, _file) != 1)
        {
            return false;
        }
        _memoryPagesUsed[5] = true;

        // Bank 2 [8000:BFFF]
        if (fread(&_memoryPages[2], PAGE_SIZE, 1, _file) != 1)
        {
            return false;
        }
        _memoryPagesUsed[2] = true;

        // Bank 0 [C000:FFFF]
        if (fread(&_memoryPages[0], PAGE_SIZE, 1, _file) != 1)
        {
            return false;
        }
        _memoryPagesUsed[0] = true;

        _stagingLoaded = true;
        result = true;
    }

    return result;
}

bool LoaderSNA::load128kToStaging()
{
    bool result = true;

    if (_snapshotMode == SNA_128 && _file != nullptr)
    {
        int memoryPagesToLoad = (_fileSize - sizeof(snaHeader) - 3 * PAGE_SIZE - sizeof (sna128Header)) / PAGE_SIZE;

        // Ensure we're reading from file start
        rewind(_file);

        // Read SNA common header
        if (fread(&_header, sizeof(_header), 1, _file) != 1)
        {
            result = false;
        }

        // Read Bank 5 [4000:7FFF]
        if (result)
        {
            if (fread(&_memoryPages[5], PAGE_SIZE, 1, _file) != 1)
            {
                result = false;
            }
            else
            {
                _memoryPagesUsed[5] = true;
            }
        }

        // Read Bank 2 [8000:BFFF]
        if (result)
        {
            if (fread(&_memoryPages[2], PAGE_SIZE, 1, _file) != 1)
            {
                result = false;
            }
            else
            {
                _memoryPagesUsed[2] = true;
            }
        }

        // Read Bank N [C000:FFFF]
        // It will go to the page mapped by port #7FFD value
        if (result)
        {
            if (fread(&_memoryPages[0], PAGE_SIZE, 1, _file) != 1)
            {
                result = false;
            }
            else
            {
                _memoryPagesUsed[0] = true;
            }
        }

        // Read extended SNA header
        if (result)
        {
            if (fread(&_ext128Header, sizeof(_ext128Header), 1, _file) != 1)
            {
                result = false;
            }
        }

        // Memory page mapped to [C000:FFFF]
        if (result)
        {
            uint8_t currentTopPage = _ext128Header.port_7FFD & 0x07u;

            // Move Page 0 content loaded previously to mapped RAM page
            if (currentTopPage != 0)
            {
                _memoryPagesUsed[0] = false;
                memcpy(&_memoryPages[currentTopPage], &_memoryPages[0], PAGE_SIZE);
                memset(&_memoryPages[0], 0x00, PAGE_SIZE);
                _memoryPagesUsed[currentTopPage] = true;
            }

            // Load all the rest RAM pages from 128k extended section
            if (memoryPagesToLoad > 0)
            {
                int pagesRead = 0;
                for (int pageNum = 0; pageNum < 8; pageNum++)
                {
                    if (pagesRead == memoryPagesToLoad)
                        break;

                    // All those pages were already loaded
                    if (_memoryPagesUsed[pageNum])
                        continue;

                    // Load next page
                    if (fread(&_memoryPages[pageNum], PAGE_SIZE, 1, _file) != 1)
                    {
                        result = false;
                        break;
                    }
                    pagesRead++;
                    _memoryPagesUsed[pageNum] = true;
                }
            }
        }

        _borderColor = _header.border & 0b0000'0111;

        _stagingLoaded = true;
    }

    return result;
}

bool LoaderSNA::applySnapshotFromStaging()
{
    bool result = false;

    Memory& memory = *_context->pMemory;
    Screen& screen = *_context->pScreen;
    Core& core = *_context->pCore;
    Z80& z80 = *_context->pCore->GetZ80();
    int ramPagesLoaded = 0;

    if (_stagingLoaded)
    {
        // Reset Z80 and all peripherals
        core.Reset();

        // Transfer RAM data to emulator (only pages existed in snapshot will be updated)
        for (int pageNum = 0; pageNum < 8; pageNum++)
        {
            if (_memoryPagesUsed[pageNum])
            {
                memory.LoadRAMPageData(pageNum, _memoryPages[pageNum], PAGE_SIZE);

                ramPagesLoaded++;
            }
        }

        // Transfer registers
        z80.alt.h = _header._h;
        z80.alt.l = _header._l;
        z80.alt.d = _header._d;
        z80.alt.e = _header._e;
        z80.alt.b = _header._b;
        z80.alt.c = _header._c;
        z80.alt.a = _header._a;
        z80.alt.f = _header._f;

        z80.h = _header.h;
        z80.l = _header.l;
        z80.d = _header.d;
        z80.e = _header.e;
        z80.b = _header.b;
        z80.c = _header.c;
        z80.a = _header.a;
        z80.f = _header.f;

        z80.xh = _header.hx;
        z80.xl = _header.lx;
        z80.yh = _header.hy;
        z80.yl = _header.ly;

        z80.sph = _header.hsp;
        z80.spl = _header.lsp;

        z80.i = _header.i;
        z80.r_low = _header.r;
        z80.r_hi = _header.r & 0x80u;
        z80.im = _header.imod & 0x03u;
        z80.iff1 = (_header.flag19 & 0b00000'0100u) >> 2;
        z80.iff2 = 1;

        // Initialize undocumented registers (not stored in SNA format)
        z80.memptr = 0;
        z80.q = 0;

        // Set up ports
        if (_snapshotMode == SNA_48)
        {
            // Set default 48k mode RAM pages
            memory.SetRAMPageToBank1(5);
            memory.SetRAMPageToBank2(2);
            memory.SetRAMPageToBank3(0);

            // Set 48k ROM as active (ROM page 3 is the 48k BASIC ROM)
            memory.SetROMPage(3);

            // 48k SNA files store Z80 PC on stack, so we need to pop it and load to PC
            // Z80 is little-endian: low byte at SP, high byte at SP+1
            uint8_t pc_low = memory.DirectReadFromZ80Memory(z80.sp++);
            uint8_t pc_high = memory.DirectReadFromZ80Memory(z80.sp++);
            z80.pc = (pc_high << 8) | pc_low;
        }

        if (_snapshotMode == SNA_128)
        {
            // Memory page mapped to [C000:FFFF]
            uint8_t currentTopPage = _ext128Header.port_7FFD & 0x07u;

            // Step 1: Unlock paging for state-independent loading
            // Ensures snapshot loads correctly even if port 7FFD was previously locked
            _context->pPortDecoder->UnlockPaging();
            
            // Step 2: Configure 128K memory banks
            memory.SetRAMPageToBank1(5);
            memory.SetRAMPageToBank2(2);
            memory.SetRAMPageToBank3(currentTopPage);

            z80.pc = _ext128Header.reg_PC;

            // Step 3: Set port values via decoder
            _context->pPortDecoder->DecodePortOut(0x7FFD, _ext128Header.port_7FFD, z80.pc);
            
            // Step 4: Explicit state assignment (including lock bit if present)
            _context->emulatorState.p7FFD = _ext128Header.port_7FFD;

            // Step 5: Activate TR-DOS ROM if needed
            if (_ext128Header.is_TRDOS)
            {
                // Set CF_TRDOS flag to indicate TR-DOS is active
                _context->emulatorState.flags |= CF_TRDOS;
                
                // Activate TR-DOS ROM
                _context->pMemory->SetROMDOS();
            }
        }

        // Pre-fill border with color
        screen.FillBorderWithColor(_borderColor);

        // Trigger screen redraw to show snapshot screen immediately
        screen.RenderOnlyMainScreen();

        result = true;
    }

    /// region <Info logging>
    std::string version = "UNKNOWN";
    if (_snapshotMode == SNA_48)
    {
        version = "SNA48";
    }
    if (_snapshotMode == SNA_128)
    {
        version = "SNA128";
    }

    if (result)
    {
        std::string pcAddress = StringHelper::ToHexWithPrefix((uint16_t)z80.pc, "$");
        MLOGINFO("%s, %d RAM pages loaded, PC=%s", version.c_str(), ramPagesLoaded, pcAddress.c_str());
    }
    else
    {
        MLOGWARNING("Unable to apply loaded SNA data, type: %s, size: %d '%s'", version.c_str(), _fileSize, _path.c_str());
    }
    /// endregion </Info logging>

    return result;
}

/// region <Save methods>

/// Determine output format based on current emulator mode
/// If paging is locked (bit 5 of port 7FFD set), use 48K format
/// Otherwise use 128K format
SNA_MODE LoaderSNA::determineOutputFormat()
{
    EmulatorState& state = _context->emulatorState;
    
    // Check if paging is locked (48K mode)
    if (state.p7FFD & PORT_7FFD_LOCK)
    {
        return SNA_48;
    }
    
    return SNA_128;
}

/// Check if a RAM page contains only zeros (empty)
/// Used for optimizing 128K save by skipping empty pages
bool LoaderSNA::isPageEmpty(int pageNum)
{
    if (pageNum < 0 || pageNum >= MAX_RAM_PAGES)
    {
        return true;  // Invalid page treated as empty
    }
    
    Memory& memory = *_context->pMemory;
    uint8_t* pageData = memory.RAMPageAddress(pageNum);
    
    // Check 32-bit blocks for speed
    uint32_t* data32 = (uint32_t*)pageData;
    size_t count = PAGE_SIZE / sizeof(uint32_t);
    
    for (size_t i = 0; i < count; i++)
    {
        if (data32[i] != 0)
        {
            return false;
        }
    }
    
    return true;
}

/// Capture current emulator state to staging buffers
bool LoaderSNA::captureStateToStaging()
{
    // Null pointer checks - all these must be valid for save to work
    if (!_context)
    {
        MLOGERROR("captureStateToStaging: _context is null");
        return false;
    }
    
    if (!_context->pMemory)
    {
        MLOGERROR("captureStateToStaging: pMemory is null");
        return false;
    }
    
    if (!_context->pCore)
    {
        MLOGERROR("captureStateToStaging: pCore is null");
        return false;
    }
    
    if (!_context->pCore->GetZ80())
    {
        MLOGERROR("captureStateToStaging: GetZ80() returned null");
        return false;
    }
    
    Memory& memory = *_context->pMemory;
    Z80& z80 = *_context->pCore->GetZ80();
    EmulatorState& state = _context->emulatorState;
    
    // Clear staging
    memset(&_header, 0, sizeof(_header));
    memset(&_ext128Header, 0, sizeof(_ext128Header));
    memset(_memoryPages, 0, sizeof(_memoryPages));
    memset(_memoryPagesUsed, 0, sizeof(_memoryPagesUsed));
    
    // Capture Z80 registers to header
    // Alternate registers
    _header._h = z80.alt.h;
    _header._l = z80.alt.l;
    _header._d = z80.alt.d;
    _header._e = z80.alt.e;
    _header._b = z80.alt.b;
    _header._c = z80.alt.c;
    _header._a = z80.alt.a;
    _header._f = z80.alt.f;
    
    // Main registers
    _header.h = z80.h;
    _header.l = z80.l;
    _header.d = z80.d;
    _header.e = z80.e;
    _header.b = z80.b;
    _header.c = z80.c;
    _header.a = z80.a;
    _header.f = z80.f;
    
    // Index and control registers
    _header.hx = z80.xh;
    _header.lx = z80.xl;
    _header.hy = z80.yh;
    _header.ly = z80.yl;
    
    _header.hsp = z80.sph;
    _header.lsp = z80.spl;
    
    _header.i = z80.i;
    _header.r = (z80.r_hi & 0x80) | (z80.r_low & 0x7F);
    _header.imod = z80.im & 0x03;
    _header.flag19 = (z80.iff2 & 1) << 2;
    
    // Border color (lower 3 bits) - use screen if available, else default to 0
    if (_context->pScreen)
    {
        _header.border = _context->pScreen->GetBorderColor() & 0x07;
    }
    else
    {
        _header.border = 0;  // Default border color
    }
    _borderColor = _header.border;
    
    // Determine format
    _snapshotMode = determineOutputFormat();
    
    // Capture 128K extended header if needed
    if (_snapshotMode == SNA_128)
    {
        _ext128Header.reg_PC = z80.pc;
        _ext128Header.port_7FFD = state.p7FFD;
        _ext128Header.is_TRDOS = (state.flags & CF_TRDOS) ? 1 : 0;
    }
    
    // Capture memory pages
    // SNA format only supports 8 RAM pages (128K Spectrum), not MAX_RAM_PAGES (256)
    constexpr int SNA_RAM_PAGES = 8;
    for (int pageNum = 0; pageNum < SNA_RAM_PAGES; pageNum++)
    {
        memcpy(_memoryPages[pageNum], memory.RAMPageAddress(pageNum), PAGE_SIZE);
        _memoryPagesUsed[pageNum] = true;
    }
    
    _stagingLoaded = true;
    return true;
}

/// Save 48K SNA format
/// Structure: 27-byte header + 48KB RAM (pages 5, 2, 0)
/// PC is pushed to stack (not stored in header)
bool LoaderSNA::save48kFromStaging()
{
    Z80& z80 = *_context->pCore->GetZ80();
    
    // Open file for writing
    FILE* file = fopen(_path.c_str(), "wb");
    if (!file)
    {
        MLOGERROR("Cannot create file: %s", _path.c_str());
        return false;
    }
    
    // For 48K, PC is pushed to stack
    // We need to modify the snapshot to reflect this
    // Decrement SP by 2 and write PC to that location in page 5
    uint16_t sp = (z80.sph << 8) | z80.spl;
    uint16_t pc = z80.pc;
    
    // Push PC to stack (little-endian)
    sp -= 2;
    
    // Update SP in header
    _header.lsp = sp & 0xFF;
    _header.hsp = (sp >> 8) & 0xFF;
    
    // Write PC to stack in memory (pages 5, 2, 0 map to 0x4000-0xFFFF)
    // Calculate which page and offset for SP address
    if (sp >= 0x4000)
    {
        // Stack is in addressable RAM
        uint16_t offset = sp - 0x4000;
        int pageIdx = offset / PAGE_SIZE;  // 0=page5, 1=page2, 2=page0
        int pageOffset = offset % PAGE_SIZE;
        
        int pageMap[3] = {5, 2, 0};
        if (pageIdx < 3)
        {
            int actualPage = pageMap[pageIdx];
            _memoryPages[actualPage][pageOffset] = pc & 0xFF;       // Low byte
            _memoryPages[actualPage][pageOffset + 1] = (pc >> 8);   // High byte
        }
    }
    
    // Write header
    if (fwrite(&_header, sizeof(_header), 1, file) != 1)
    {
        fclose(file);
        remove(_path.c_str());
        MLOGERROR("Failed to write header");
        return false;
    }
    
    // Write RAM pages: 5, 2, 0 (in order from 0x4000)
    const int pages48k[] = {5, 2, 0};
    for (int i = 0; i < 3; i++)
    {
        if (fwrite(_memoryPages[pages48k[i]], PAGE_SIZE, 1, file) != 1)
        {
            fclose(file);
            remove(_path.c_str());
            MLOGERROR("Failed to write RAM page %d", pages48k[i]);
            return false;
        }
    }
    
    fclose(file);
    
    MLOGINFO("Saved 48K SNA: %s", _path.c_str());
    return true;
}

/// Save 128K SNA format
/// Structure: 27-byte header + pages 5, 2, N (current paged) + 4-byte ext header + remaining pages (0,1,3,4,6 or 7)
bool LoaderSNA::save128kFromStaging()
{
    EmulatorState& state = _context->emulatorState;
    
    // Open file for writing  
    FILE* file = fopen(_path.c_str(), "wb");
    if (!file)
    {
        MLOGERROR("Cannot create file: %s", _path.c_str());
        return false;
    }
    
    // Write header
    if (fwrite(&_header, sizeof(_header), 1, file) != 1)
    {
        fclose(file);
        remove(_path.c_str());
        MLOGERROR("Failed to write header");
        return false;
    }
    
    // Get current paged bank (bits 0-2 of 7FFD)
    uint8_t currentPage = state.p7FFD & 0x07;
    
    // Write base pages: 5, 2, currentPage (in order from 0x4000)
    if (fwrite(_memoryPages[5], PAGE_SIZE, 1, file) != 1 ||
        fwrite(_memoryPages[2], PAGE_SIZE, 1, file) != 1 ||
        fwrite(_memoryPages[currentPage], PAGE_SIZE, 1, file) != 1)
    {
        fclose(file);
        remove(_path.c_str());
        MLOGERROR("Failed to write base RAM pages");
        return false;
    }
    
    // Write extended header (PC, port 7FFD, TR-DOS flag)
    if (fwrite(&_ext128Header, sizeof(_ext128Header), 1, file) != 1)
    {
        fclose(file);
        remove(_path.c_str());
        MLOGERROR("Failed to write extended header");
        return false;
    }
    
    // Write remaining pages in ascending order (skip 5, 2, and currentPage)
    // SNA format only supports 8 RAM pages (128K Spectrum)
    constexpr int SNA_RAM_PAGES = 8;
    for (int pageNum = 0; pageNum < SNA_RAM_PAGES; pageNum++)
    {
        if (pageNum == 5 || pageNum == 2 || pageNum == currentPage)
        {
            continue;  // Already written
        }
        
        if (fwrite(_memoryPages[pageNum], PAGE_SIZE, 1, file) != 1)
        {
            fclose(file);
            remove(_path.c_str());
            MLOGERROR("Failed to write RAM page %d", pageNum);
            return false;
        }
    }
    
    fclose(file);
    
    MLOGINFO("Saved 128K SNA: %s", _path.c_str());
    return true;
}

/// Main save method - saves current emulator state directly to SNA file
/// No staging buffers needed - writes directly from emulator state
bool LoaderSNA::save()
{
    // Null pointer checks
    if (!_context || !_context->pMemory || !_context->pCore || !_context->pCore->GetZ80())
    {
        MLOGERROR("save: Invalid emulator context");
        return false;
    }
    
    Memory& memory = *_context->pMemory;
    Z80& z80 = *_context->pCore->GetZ80();
    EmulatorState& state = _context->emulatorState;
    
    // Determine output format based on paging lock
    _snapshotMode = determineOutputFormat();
    
    // Build header directly from Z80 state
    snaHeader header;
    memset(&header, 0, sizeof(header));
    
    // Alternate registers
    header._h = z80.alt.h;
    header._l = z80.alt.l;
    header._d = z80.alt.d;
    header._e = z80.alt.e;
    header._b = z80.alt.b;
    header._c = z80.alt.c;
    header._a = z80.alt.a;
    header._f = z80.alt.f;
    
    // Main registers
    header.h = z80.h;
    header.l = z80.l;
    header.d = z80.d;
    header.e = z80.e;
    header.b = z80.b;
    header.c = z80.c;
    header.a = z80.a;
    header.f = z80.f;
    
    // Index and control registers
    header.hx = z80.xh;
    header.lx = z80.xl;
    header.hy = z80.yh;
    header.ly = z80.yl;
    
    header.i = z80.i;
    header.r = (z80.r_hi & 0x80) | (z80.r_low & 0x7F);
    header.imod = z80.im & 0x03;
    header.flag19 = (z80.iff2 & 1) << 2;
    
    // Border color
    if (_context->pScreen)
    {
        header.border = _context->pScreen->GetBorderColor() & 0x07;
    }
    
    // Open file for writing
    FILE* file = fopen(_path.c_str(), "wb");
    if (!file)
    {
        MLOGERROR("Cannot create file: %s", _path.c_str());
        return false;
    }
    
    if (_snapshotMode == SNA_48)
    {
        // 48K: PC pushed to stack, SP decremented by 2
        uint16_t sp = (z80.sph << 8) | z80.spl;
        uint16_t pc = z80.pc;
        
        sp -= 2;
        header.lsp = sp & 0xFF;
        header.hsp = (sp >> 8) & 0xFF;
        
        // Write header
        if (fwrite(&header, sizeof(header), 1, file) != 1)
        {
            fclose(file);
            remove(_path.c_str());
            MLOGERROR("Failed to write header");
            return false;
        }
        
        // Write PC to stack location in memory before saving
        // Stack is in pages 5, 2, 0 (0x4000-0xFFFF)
        if (sp >= 0x4000)
        {
            uint16_t offset = sp - 0x4000;
            int pageIdx = offset / PAGE_SIZE;
            int pageOffset = offset % PAGE_SIZE;
            int pageMap[3] = {5, 2, 0};
            
            if (pageIdx < 3)
            {
                uint8_t* pagePtr = memory.RAMPageAddress(pageMap[pageIdx]);
                if (pagePtr)
                {
                    pagePtr[pageOffset] = pc & 0xFF;
                    pagePtr[pageOffset + 1] = (pc >> 8) & 0xFF;
                }
            }
        }
        
        // Write RAM pages: 5, 2, 0 directly from memory
        const int pages48k[] = {5, 2, 0};
        for (int i = 0; i < 3; i++)
        {
            uint8_t* pagePtr = memory.RAMPageAddress(pages48k[i]);
            if (!pagePtr || fwrite(pagePtr, PAGE_SIZE, 1, file) != 1)
            {
                fclose(file);
                remove(_path.c_str());
                MLOGERROR("Failed to write RAM page %d", pages48k[i]);
                return false;
            }
        }
        
        fclose(file);
        MLOGINFO("Saved 48K SNA: %s", _path.c_str());
    }
    else if (_snapshotMode == SNA_128)
    {
        // 128K: SP unchanged, PC in extended header
        header.lsp = z80.spl;
        header.hsp = z80.sph;
        
        // Write header
        if (fwrite(&header, sizeof(header), 1, file) != 1)
        {
            fclose(file);
            remove(_path.c_str());
            MLOGERROR("Failed to write header");
            return false;
        }
        
        // Get current paged bank
        uint8_t currentPage = state.p7FFD & 0x07;
        
        // Write base pages: 5, 2, currentPage
        const int basePages[] = {5, 2, (int)currentPage};
        for (int i = 0; i < 3; i++)
        {
            uint8_t* pagePtr = memory.RAMPageAddress(basePages[i]);
            if (!pagePtr || fwrite(pagePtr, PAGE_SIZE, 1, file) != 1)
            {
                fclose(file);
                remove(_path.c_str());
                MLOGERROR("Failed to write base RAM page %d", basePages[i]);
                return false;
            }
        }
        
        // Write extended header
        sna128Header ext128Header;
        ext128Header.reg_PC = z80.pc;
        ext128Header.port_7FFD = state.p7FFD;
        ext128Header.is_TRDOS = (state.flags & CF_TRDOS) ? 1 : 0;
        
        if (fwrite(&ext128Header, sizeof(ext128Header), 1, file) != 1)
        {
            fclose(file);
            remove(_path.c_str());
            MLOGERROR("Failed to write extended header");
            return false;
        }
        
        // Write remaining pages in ascending order (0,1,3,4,6,7)
        // Standard 128K SNA has 8 pages (0-7), not MAX_RAM_PAGES
        const int SNA_128_PAGES = 8;
        for (int pageNum = 0; pageNum < SNA_128_PAGES; pageNum++)
        {
            if (pageNum == 5 || pageNum == 2 || pageNum == currentPage)
                continue;
            
            uint8_t* pagePtr = memory.RAMPageAddress(pageNum);
            if (!pagePtr || fwrite(pagePtr, PAGE_SIZE, 1, file) != 1)
            {
                fclose(file);
                remove(_path.c_str());
                MLOGERROR("Failed to write RAM page %d", pageNum);
                return false;
            }
        }
        
        fclose(file);
        MLOGINFO("Saved 128K SNA: %s", _path.c_str());
    }
    else
    {
        fclose(file);
        remove(_path.c_str());
        MLOGERROR("Unknown snapshot mode for save");
        return false;
    }
    
    return true;
}

/// endregion </Save methods>

/// endregion </Helper methods>