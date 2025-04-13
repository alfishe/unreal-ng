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

    _path = path;

    memset(_memoryPages, 0x00, sizeof(_memoryPagesUsed));
    memset(_memoryPagesUsed, 0x00, sizeof(_memoryPagesUsed));
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
            }

            if (is128kSnapshot(_file))
            {
                _snapshotMode = SNA_128;
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
    size_t headerSize = fileSize % PAGE_SIZE;

    if (headerSize == _sna128HeaderSize)
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
        [[maybe_unused]] size_t headerRead = fread(&_header, sizeof(_header), 1, _file);

        _borderColor = _header.border & 0b0000'0111;
    }

    return result;
}

bool LoaderSNA::load128kToStaging()
{
    bool result = false;

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

        // Set up ports
        if (_snapshotMode == SNA_48)
        {
            // Set default 48k mode RAM pages
            memory.SetRAMPageToBank1(5);
            memory.SetRAMPageToBank2(2);
            memory.SetRAMPageToBank3(0);

            // 48k SNA files store Z80 PC on stack, so we need to pop it and load to PC
            uint16_t pc_high = memory.DirectReadFromZ80Memory(z80.sp++);
            uint8_t pc_low = memory.DirectReadFromZ80Memory(z80.sp++);
            uint16_t pcAddress = (pc_high << 8) | pc_low;

            z80.pc = pcAddress;
        }

        if (_snapshotMode == SNA_128)
        {
            // Memory page mapped to [C000:FFFF]
            uint8_t currentTopPage = _ext128Header.port_7FFD & 0x07u;

            memory.SetRAMPageToBank1(5);
            memory.SetRAMPageToBank2(2);
            memory.SetRAMPageToBank3(currentTopPage);

            z80.pc = _ext128Header.reg_PC;

            // Switch to proper RAM/ROM pages configuration
            _context->pPortDecoder->DecodePortOut(0x7FFD, _ext128Header.port_7FFD, z80.pc);

            // Activate TR-DOS ROM if needed
            if (_ext128Header.is_TRDOS)
            {
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
/// endregion </Helper methods>