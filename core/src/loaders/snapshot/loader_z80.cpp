#include "loader_z80.h"

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/soundmanager.h"
#include "stdafx.h"

LoaderZ80::LoaderZ80(EmulatorContext* context, const std::string& path)
{
    _context = context;
    _logger = context->pModuleLogger;

    _path = path;
}

LoaderZ80::~LoaderZ80()
{
    freeStagingMemory();
}

bool LoaderZ80::load()
{
    bool result = false;

    if (validate())
    {
        if (stageLoad())
        {
            commitFromStage();

            result = true;
        }

        FileHelper::CloseFile(_file);
        _file = nullptr;
    }

    return result;
}

bool LoaderZ80::save()
{
    bool result = false;

    // Capture current emulator state to staging buffers
    if (!captureStateToStaging())
    {
        MLOGERROR("Failed to capture emulator state for Z80 save");
        return false;
    }

    // Determine output format based on current emulator mode
    _memoryMode = determineOutputFormat();

    // Write V3 format file
    result = saveV3FromStaging();

    // Clean up staging memory
    freeStagingMemory();

    return result;
}

Z80MemoryMode LoaderZ80::determineOutputFormat()
{
    // Check if emulator is in 128K mode by examining port 7FFD lock bit
    // If locked and in 48K mode, use 48K format; otherwise use 128K
    uint8_t port7FFD = _context->emulatorState.p7FFD;
    bool isLocked = (port7FFD & 0x20) != 0;  // Bit 5 = lock

    // Check current model - some models are always 48K
    // For simplicity: if lock bit is set AND we're running as 48K, save as 48K
    // Otherwise save as 128K to preserve all banks
    if (isLocked)
    {
        // Check if only using 48K-visible pages
        return Z80_48K;
    }

    return Z80_128K;
}

bool LoaderZ80::captureStateToStaging()
{
    if (_context == nullptr || _context->pCore == nullptr || _context->pMemory == nullptr)
    {
        return false;
    }

    // Get current Z80 state from CPU - Z80 inherits from Z80Registers
    Z80* z80 = _context->pCore->GetZ80();
    if (z80 == nullptr)
    {
        return false;
    }
    
    // Copy register values directly from Z80 (which inherits Z80Registers)
    _z80Registers.pc = z80->pc;
    _z80Registers.sp = z80->sp;
    _z80Registers.af = z80->af;
    _z80Registers.bc = z80->bc;
    _z80Registers.de = z80->de;
    _z80Registers.hl = z80->hl;
    _z80Registers.ix = z80->ix;
    _z80Registers.iy = z80->iy;
    _z80Registers.i = z80->i;
    _z80Registers.r_low = z80->r_low;
    _z80Registers.r_hi = z80->r_hi;
    _z80Registers.iff1 = z80->iff1;
    _z80Registers.iff2 = z80->iff2;
    _z80Registers.im = z80->im;
    _z80Registers.alt.af = z80->alt.af;
    _z80Registers.alt.bc = z80->alt.bc;
    _z80Registers.alt.de = z80->alt.de;
    _z80Registers.alt.hl = z80->alt.hl;

    // Capture port state
    _port7FFD = _context->emulatorState.p7FFD;
    _portFFFD = _context->emulatorState.pFFFD;

    // Capture border color
    _borderColor = _context->pScreen->GetBorderColor() & 0x07;

    // Capture all RAM pages
    Memory* memory = _context->pMemory;
    for (int page = 0; page < MAX_RAM_PAGES; page++)
    {
        uint8_t* srcPage = memory->RAMPageAddress(page);
        if (srcPage != nullptr)
        {
            _stagingRAMPages[page] = new uint8_t[PAGE_SIZE];
            memcpy(_stagingRAMPages[page], srcPage, PAGE_SIZE);
        }
    }

    _stagingLoaded = true;
    return true;
}

bool LoaderZ80::saveV3FromStaging()
{
    // Open file for writing
    FILE* outFile = fopen(_path.c_str(), "wb");
    if (outFile == nullptr)
    {
        MLOGERROR("Failed to open '%s' for writing", _path.c_str());
        return false;
    }

    // Build V3 header
    Z80Header_v3 header = {};

    // V1 fields - CPU registers
    header.reg_A = _z80Registers.a;
    header.reg_F = _z80Registers.f;
    header.reg_BC = _z80Registers.bc;
    header.reg_DE = _z80Registers.de;
    header.reg_HL = _z80Registers.hl;
    header.reg_SP = _z80Registers.sp;
    header.reg_I = _z80Registers.i;
    header.reg_R = _z80Registers.r_low | (_z80Registers.r_hi & 0x80);

    // Flags byte: bit 0 = R bit 7, bits 1-3 = border, bit 5 = compression
    header.flags = (_z80Registers.r_hi >> 7) | ((_borderColor & 0x07) << 1) | 0x20;  // 0x20 = compressed

    header.reg_DE1 = _z80Registers.alt.de;
    header.reg_BC1 = _z80Registers.alt.bc;
    header.reg_HL1 = _z80Registers.alt.hl;
    header.reg_A1 = _z80Registers.alt.a;
    header.reg_F1 = _z80Registers.alt.f;
    header.reg_IY = _z80Registers.iy;
    header.reg_IX = _z80Registers.ix;
    header.IFF1 = _z80Registers.iff1 ? 1 : 0;
    header.IFF2 = _z80Registers.iff2 ? 1 : 0;
    header.im = _z80Registers.im & 0x03;

    // V1: PC = 0 indicates v2/v3 format
    header.reg_PC = 0;

    // V2 fields
    header.extendedHeaderLen = 54;  // V3 standard
    header.newPC = _z80Registers.pc;
    header.model = static_cast<Z80_Models_v2>(getModelCodeV3());
    header.p7FFD = _port7FFD;
    header.pFFFD = _portFFFD;

    // AY registers - get from sound manager
    if (_context->pSoundManager != nullptr)
    {
        SoundChip_AY8910* psg = _context->pSoundManager->getAYChip(0);
        if (psg != nullptr)
        {
            for (int i = 0; i < 16; i++)
            {
                header.ay[i] = psg->readRegister(static_cast<uint8_t>(i));
            }
        }
    }

    // V3 fields - T-state counter (optional, set to 0)
    header.lowTCounter = 0;
    header.highTCounter = 0;

    // Write header (first 30 bytes of V1)
    fwrite(&header, sizeof(Z80Header_v1), 1, outFile);

    // Write extended header length (2 bytes) then remaining V2/V3 header
    uint16_t extLen = header.extendedHeaderLen;
    fwrite(&extLen, sizeof(extLen), 1, outFile);
    fwrite(&header.newPC, extLen, 1, outFile);

    // Write memory pages
    uint8_t compressBuffer[PAGE_SIZE + 1024];  // Extra space for worst case

    if (_memoryMode == Z80_48K)
    {
        // 48K: write pages 5, 2, 0 (mapped to Z80 pages 8, 4, 5)
        int pageMap48K[] = {8, 4, 5};    // Z80 page numbers
        int ramMap48K[] = {5, 2, 0};     // Our RAM page numbers

        for (int i = 0; i < 3; i++)
        {
            if (_stagingRAMPages[ramMap48K[i]] != nullptr)
            {
                size_t compressedSize = compressPage(_stagingRAMPages[ramMap48K[i]], PAGE_SIZE,
                                                     compressBuffer, sizeof(compressBuffer));

                MemoryBlockDescriptor desc;
                desc.compressedSize = static_cast<uint16_t>(compressedSize);
                desc.memoryPage = pageMap48K[i];

                fwrite(&desc, sizeof(desc), 1, outFile);
                fwrite(compressBuffer, compressedSize, 1, outFile);
            }
        }
    }
    else
    {
        // 128K: write all 8 RAM pages (Z80 pages 3-10)
        for (int page = 0; page < 8; page++)
        {
            if (_stagingRAMPages[page] != nullptr)
            {
                size_t compressedSize = compressPage(_stagingRAMPages[page], PAGE_SIZE,
                                                     compressBuffer, sizeof(compressBuffer));

                MemoryBlockDescriptor desc;
                desc.compressedSize = static_cast<uint16_t>(compressedSize);
                desc.memoryPage = page + 3;  // Z80 pages: 3=RAM0, 4=RAM1, ..., 10=RAM7

                fwrite(&desc, sizeof(desc), 1, outFile);
                fwrite(compressBuffer, compressedSize, 1, outFile);
            }
        }
    }

    fclose(outFile);

    MLOGINFO("Saved Z80 v3 snapshot to '%s' (%s mode)",
             _path.c_str(), (_memoryMode == Z80_48K) ? "48K" : "128K");

    return true;
}

uint8_t LoaderZ80::getModelCodeV3()
{
    // Map current emulator model to Z80 v3 model code
    // For now, default to standard models based on memory mode
    if (_memoryMode == Z80_48K)
    {
        return Z80_MODEL3_48K;  // 0
    }
    else
    {
        return Z80_MODEL3_128K;  // 4
    }
}

bool LoaderZ80::validate()
{
    bool result = false;

    // 1. Check that file exists
    if (FileHelper::FileExists(_path))
    {
        _file = FileHelper::OpenExistingFile(_path);
        if (_file != nullptr)
        {
            // 2. Check file has appropriate size (header + data bytes)
            _fileSize = FileHelper::GetFileSize(_path);
            if (_fileSize > 0)
            {
                size_t dataSize = _fileSize - sizeof(Z80Header_v1);
                if (dataSize > 0)
                {
                    // 3. Detect snapshot version
                    Z80SnapshotVersion ver = getSnapshotFileVersion();
                    if (ver != Unknown)
                    {
                        // 4. Validate header sanity for detected version
                        if (!validateHeaderSanity(ver))
                        {
                            MLOGWARNING("Z80 snapshot file '%s' failed header sanity checks", _path.c_str());
                            return false;
                        }

                        // 5. Validate minimum file size for detected version
                        bool sizeValid = true;
                        switch (ver)
                        {
                            case Z80v1:
                                // v1: 30-byte header + at least some data
                                if (_fileSize < sizeof(Z80Header_v1) + 1)
                                {
                                    MLOGWARNING("Z80 v1 snapshot file '%s' too small (size=%zu)", _path.c_str(), _fileSize);
                                    sizeValid = false;
                                }
                                break;
                            case Z80v2:
                                // v2: 30-byte header + 2-byte length + 23-byte extended header = 55 bytes minimum
                                if (_fileSize < 55)
                                {
                                    MLOGWARNING("Z80 v2 snapshot file '%s' too small (size=%zu, need at least 55)", _path.c_str(), _fileSize);
                                    sizeValid = false;
                                }
                                break;
                            case Z80v3:
                                // v3: 30-byte header + 2-byte length + 54-byte extended header = 86 bytes minimum
                                if (_fileSize < 86)
                                {
                                    MLOGWARNING("Z80 v3 snapshot file '%s' too small (size=%zu, need at least 86)", _path.c_str(), _fileSize);
                                    sizeValid = false;
                                }
                                break;
                            default:
                                sizeValid = false;
                                break;
                        }

                        if (sizeValid)
                        {
                            _snapshotVersion = ver;
                            result = true;
                        }
                    }
                }
                else
                {
                    MLOGWARNING("Z80 snapshot file '%s' has incorrect size %zu", _path.c_str(), _fileSize);
                }
            }
        }
    }
    else
    {
        MLOGWARNING("Z80 snapshot file '%s' not found", _path.c_str());
    }

    // Persist validation state in the field
    _fileValidated = result;

    return result;
}

bool LoaderZ80::stageLoad()
{
    bool result = false;

    if (_fileValidated && _snapshotVersion != Unknown)
    {
        switch (_snapshotVersion)
        {
            case Z80v1:
                result = loadZ80v1();
                break;
            case Z80v2:
                result = loadZ80v2();
                break;
            case Z80v3:
                result = loadZ80v3();
                break;
            default:
                break;
        }
    }

    if (result)
    {
        _stagingLoaded = true;

        /// region <Info logging>
        std::string message = dumpSnapshotMemoryInfo();
        MLOGINFO(message);
        /// endregion </Info logging>
    }

    return result;
}

void LoaderZ80::commitFromStage()
{
    if (_stagingLoaded)
    {
        Memory& memory = *_context->pMemory;
        Screen& screen = *_context->pScreen;
        PortDecoder& ports = *_context->pPortDecoder;

        /// region <Apply port configuration>
        switch (_memoryMode)
        {
            case Z80_48K: {
                uint8_t port7FFD =
                    PORT_7FFD_RAM_BANK_0 | PORT_7FFD_SCREEN_NORMAL | PORT_7FFD_ROM_BANK_1 | PORT_7FFD_LOCK;
                ports.PeripheralPortOut(0x7FFD, port7FFD);
                ports.PeripheralPortOut(0xFFFD, _portFFFD);

                memory.SetRAMPageToBank1(5);
                memory.SetRAMPageToBank2(2);
                memory.SetRAMPageToBank3(0);

                memory.SetROM48k();
            }
            break;
            case Z80_128K:
            {
                // Initialize 128K memory configuration
                // CRITICAL: Must fully unlock emulator state before applying snapshot
                
                // Extract RAM page for bank 3 from port 7FFD (bits 0-2)
                uint8_t bank3Page = _port7FFD & 0x07;
                
                // Step 1: Unlock paging via PortDecoder interface
                // This allows subsequent port writes to succeed even if previously locked
                ports.UnlockPaging();
                
                // Step 2: Set the actual snapshot port values via port decoder
                ports.PeripheralPortOut(0x7FFD, _port7FFD);
                ports.PeripheralPortOut(0xFFFD, _portFFFD);
                
                // Step 3: Ensure emulatorState reflects snapshot's port value (including lock bit)
                // PeripheralPortOut may not update emulatorState directly
                _context->emulatorState.p7FFD = _port7FFD;
                
                // Step 4: Set up standard 128K memory mapping
                // Bank 0 (0x0000-0x3FFF): ROM (set by UpdateZ80Banks based on port 7FFD bit 4)
                // Bank 1 (0x4000-0x7FFF): RAM page 5 (fixed)
                // Bank 2 (0x8000-0xBFFF): RAM page 2 (fixed)  
                // Bank 3 (0xC000-0xFFFF): RAM page from port 7FFD bits 0-2
                memory.SetRAMPageToBank1(5);
                memory.SetRAMPageToBank2(2);
                memory.SetRAMPageToBank3(bank3Page);
                
                // Step 5: Trigger ROM selection based on port 7FFD bit 4
                memory.UpdateZ80Banks();
                break;
            }
            case Z80_256K:
                break;
            default:
                throw std::logic_error("Not supported");
                break;
        }

        // Pre-fill whole border with color
        screen.FillBorderWithColor(_borderColor);
        ports.Default_Port_FE_Out(0x00FE, _borderColor, _z80Registers.pc);

        /// endregion </Apply port configuration>

        /// region <Transfer memory content>

        for (size_t idx = 0; idx < MAX_ROM_PAGES; idx++)
        {
            uint8_t* ptr = _stagingROMPages[idx];

            if (ptr != nullptr)
            {
                throw std::logic_error("Z80 snapshot loader: ROM pages transfer from snapshot not implemented yet");
            }
        }

        for (size_t idx = 0; idx < MAX_RAM_PAGES; idx++)
        {
            uint8_t* ptr = _stagingRAMPages[idx];

            if (ptr != nullptr)
            {
                uint8_t* targetPage = memory.RAMPageAddress(idx);
                memcpy(targetPage, ptr, PAGE_SIZE);
            }
        }

        // Free used staging memory
        freeStagingMemory();

        /// endregion </Transfer memory content>

        /// region <Transfer Z80 registers>
        Z80Registers* actualRegisters = static_cast<Z80Registers*>(_context->pCore->GetZ80());
        memcpy(actualRegisters, &_z80Registers, sizeof(Z80Registers));
        /// endregion </Transfer Z80 registers>

        // Trigger screen redraw to show snapshot screen immediately
        screen.RenderOnlyMainScreen();
    }
}

/// region <Helper methods>
Z80SnapshotVersion LoaderZ80::getSnapshotFileVersion()
{
    Z80SnapshotVersion result = Unknown;

    if (_file != nullptr)
    {
        Z80Header_v1 header;

        // Ensure we're reading from file start
        rewind(_file);

        // Read Z80 common header
        size_t headerRead = FileHelper::ReadFileToBuffer(_file, (uint8_t*)&header, sizeof(header));

        if (headerRead == sizeof(header))
        {
            if (header.reg_PC == 0x0000)
            {
                // PC register is zero, indicating Z80 v2 or newer format
                rewind(_file);
                Z80Header_v2 headerV2;
                headerRead = fread(&headerV2, sizeof(headerV2), 1, _file);

                if (headerRead == 1)
                {
                    uint16_t extendedHeaderSize = headerV2.extendedHeaderLen;

                    switch (extendedHeaderSize)
                    {
                        case 23:
                            result = Z80v2;
                            break;
                        case 54:
                            result = Z80v3;
                            break;
                        case 55:
                            result = Z80v3;
                            break;
                        default:
                            break;
                    }
                }
            }
            else
            {
                // PC register is not zero, it's Z80 v1 format
                result = Z80v1;
            }
        }
    }

    return result;
}

bool LoaderZ80::loadZ80v1()
{
    bool result = false;

    if (_fileValidated && _snapshotVersion == Z80v1)
    {
        _memoryMode = Z80_48K;

        auto buffer = std::unique_ptr<uint8_t[]>(new uint8_t[_fileSize]);
        uint8_t* pBuffer = buffer.get();

        // Ensure we're reading from file start
        rewind(_file);

        // Read the whole file content to temporary buffer
        FileHelper::ReadFileToBuffer(_file, pBuffer, _fileSize);

        // Provide access to header structure
        Z80Header_v1& headerV1 = *(Z80Header_v1*)pBuffer;

        // Extract Z80 registers information
        _z80Registers = getZ80Registers(headerV1, headerV1.reg_PC);

        // Handle flags byte: if 255, treat as 1 (per specification)
        uint8_t flags = headerV1.flags;
        if (flags == 255)
        {
            flags = 1;
        }

        // Remember border color (bits 1-3)
        _borderColor = (flags & 0b0000'1110) >> 1;

        // Check if data is compressed (bit 5)
        bool isCompressed = (flags & 0b0010'0000) != 0;

        // Allocate buffer for 48K of memory (3 pages: 0x4000-0xFFFF)
        constexpr size_t MEMORY_48K_SIZE = 3 * PAGE_SIZE;
        auto unpackedMemory = std::unique_ptr<uint8_t[]>(new uint8_t[MEMORY_48K_SIZE]);
        uint8_t* pUnpacked = unpackedMemory.get();

        // Data starts after the 30-byte header
        uint8_t* dataStart = pBuffer + sizeof(Z80Header_v1);
        size_t dataSize = _fileSize - sizeof(Z80Header_v1);

        if (isCompressed)
        {
            decompressV1Data(dataStart, dataSize, pUnpacked, MEMORY_48K_SIZE);
        }
        else
        {
            // Uncompressed: copy directly (capped at 48K)
            size_t copySize = std::min(dataSize, MEMORY_48K_SIZE);
            memcpy(pUnpacked, dataStart, copySize);

            // Zero remaining if source is smaller (shouldn't happen for valid files)
            if (copySize < MEMORY_48K_SIZE)
            {
                memset(pUnpacked + copySize, 0, MEMORY_48K_SIZE - copySize);
            }
        }

        // Map 48K memory to RAM pages:
        // 0x4000-0x7FFF (offset 0x0000 in unpacked) -> RAM Page 5
        // 0x8000-0xBFFF (offset 0x4000 in unpacked) -> RAM Page 2
        // 0xC000-0xFFFF (offset 0x8000 in unpacked) -> RAM Page 0
        uint8_t* page5 = new uint8_t[PAGE_SIZE];
        uint8_t* page2 = new uint8_t[PAGE_SIZE];
        uint8_t* page0 = new uint8_t[PAGE_SIZE];

        memcpy(page5, pUnpacked, PAGE_SIZE);                  // 0x4000-0x7FFF
        memcpy(page2, pUnpacked + PAGE_SIZE, PAGE_SIZE);      // 0x8000-0xBFFF
        memcpy(page0, pUnpacked + 2 * PAGE_SIZE, PAGE_SIZE);  // 0xC000-0xFFFF

        _stagingRAMPages[5] = page5;
        _stagingRAMPages[2] = page2;
        _stagingRAMPages[0] = page0;

        result = true;
    }

    return result;
}

bool LoaderZ80::loadZ80v2()
{
    bool result = false;

    if (_fileValidated && _snapshotVersion == Z80v2 && _fileSize > 0)
    {
        auto buffer = std::unique_ptr<uint8_t[]>(new uint8_t[_fileSize]);
        uint8_t* pBuffer = buffer.get();

        // Ensure we're reading from file start
        rewind(_file);

        // Read the whole file content to temporary buffer
        FileHelper::ReadFileToBuffer(_file, pBuffer, _fileSize);

        // Provide access to header structure
        Z80Header_v1& headerV1 = *(Z80Header_v1*)pBuffer;
        Z80Header_v2& headerV2 = *(Z80Header_v2*)pBuffer;

        // Extract Z80 registers information
        _z80Registers = getZ80Registers(headerV1, headerV2.newPC);

        // Determine snapshot memory model based on model
        _memoryMode = getMemoryModeV2(static_cast<uint8_t>(headerV2.model));

        // Retrieve ports configuration
        _port7FFD = headerV2.p7FFD;
        _portFFFD = headerV2.pFFFD;

        // Remember border color
        _borderColor = (headerV1.flags & 0b0000'1110) >> 1;

        // Start memory blocks processing after all headers
        uint8_t* memBlock = pBuffer + sizeof(Z80Header_v1) + headerV2.extendedHeaderLen + 2;
        uint8_t* pBufferEnd = pBuffer + _fileSize;

        while (memBlock)
        {
            // Bounds check: ensure memory block descriptor is within file
            if (memBlock + sizeof(MemoryBlockDescriptor) > pBufferEnd)
            {
                MLOGWARNING("Z80 v2 snapshot truncated: memory block descriptor at offset %zu exceeds file size %zu",
                            static_cast<size_t>(memBlock - pBuffer), _fileSize);
                break;
            }

            MemoryBlockDescriptor* memoryBlockDescriptor = (MemoryBlockDescriptor*)memBlock;
            uint8_t* pageBlock = memBlock + sizeof(MemoryBlockDescriptor);
            size_t compressedBlockSize = memoryBlockDescriptor->compressedSize;
            uint8_t targetPage = memoryBlockDescriptor->memoryPage;

            // Determine emulator target page
            MemoryPageDescriptor targetPageDescriptor = resolveSnapshotPage(targetPage, _memoryMode);

            // Skip invalid/unknown pages (don't allocate or crash)
            if (targetPageDescriptor.mode == BANK_INVALID)
            {
                MLOGWARNING("Z80 v2 snapshot: unknown page %d in %s mode, skipping",
                            targetPage, (_memoryMode == Z80_48K) ? "48K" : "128K");
                // Advance to next block
                size_t skipSize = (compressedBlockSize == 0xFFFF) ? PAGE_SIZE : compressedBlockSize;
                memBlock += skipSize + sizeof(MemoryBlockDescriptor);
                if (memBlock >= pBufferEnd)
                {
                    memBlock = nullptr;
                }
                continue;
            }

            // Bounds check: ensure compressed data is within file
            size_t actualBlockSize = (compressedBlockSize == 0xFFFF) ? PAGE_SIZE : compressedBlockSize;
            if (pageBlock + actualBlockSize > pBufferEnd)
            {
                MLOGWARNING("Z80 v2 snapshot truncated: block data at offset %zu (size %zu) exceeds file size %zu",
                            static_cast<size_t>(pageBlock - pBuffer), actualBlockSize, _fileSize);
                break;
            }

            // Allocate memory page and register it in one of staging collections (ROM or RAM)
            // De-allocation will be performed after staging changes applied to main emulator memory
            // or in loader destructor
            uint8_t* pageBuffer = new uint8_t[PAGE_SIZE];
            switch (targetPageDescriptor.mode)
            {
                case BANK_ROM:
                    _stagingROMPages[targetPageDescriptor.page] = pageBuffer;
                    break;
                case BANK_RAM:
                    _stagingRAMPages[targetPageDescriptor.page] = pageBuffer;
                    break;
                default:
                    // Should not reach here - already handled above
                    delete[] pageBuffer;
                    break;
            }

            // Unpack memory block to target staging page
            if (compressedBlockSize == 0xFFFF)
            {
                // Block is not compressed and has fixed length 0x4000 (16384)
                compressedBlockSize = PAGE_SIZE;
                memcpy(pageBuffer, pageBlock, PAGE_SIZE);
            }
            else
            {
                // Block is compressed so we need to decompress it
                decompressPage(pageBlock, compressedBlockSize, pageBuffer, PAGE_SIZE);
            }

            uint8_t* nextMemBlock = memBlock + compressedBlockSize + sizeof(MemoryBlockDescriptor);
            if (nextMemBlock >= pBufferEnd || nextMemBlock == memBlock)
            {
                memBlock = nullptr;
            }
            else
            {
                memBlock = nextMemBlock;
            }
        }

        result = true;
    }

    return result;
}

bool LoaderZ80::loadZ80v3()
{
    bool result = false;

    if (_fileValidated && _snapshotVersion == Z80v3 && _fileSize > 0)
    {
        auto buffer = std::unique_ptr<uint8_t[]>(new uint8_t[_fileSize]);
        uint8_t* pBuffer = buffer.get();

        // Ensure we're reading from file start
        rewind(_file);

        // Read the whole file content to temporary buffer
        FileHelper::ReadFileToBuffer(_file, pBuffer, _fileSize);

        // Provide access to header structure
        Z80Header_v1& headerV1 = *(Z80Header_v1*)pBuffer;
        Z80Header_v3& headerV3 = *(Z80Header_v3*)pBuffer;

        // Extract Z80 registers information
        _z80Registers = getZ80Registers(headerV1, headerV3.newPC);

        // Determine snapshot memory model based on model (v3 has different model interpretation)
        _memoryMode = getMemoryModeV3(static_cast<uint8_t>(headerV3.model));

        // Retrieve ports configuration
        _port7FFD = headerV3.p7FFD;
        _portFFFD = headerV3.pFFFD;

        // Remember border color
        _borderColor = (headerV1.flags & 0b0000'1110) >> 1;

        // Start memory blocks processing after all headers
        uint8_t* memBlock = pBuffer + sizeof(Z80Header_v1) + headerV3.extendedHeaderLen + 2;
        uint8_t* pBufferEnd = pBuffer + _fileSize;

        while (memBlock)
        {
            // Bounds check: ensure memory block descriptor is within file
            if (memBlock + sizeof(MemoryBlockDescriptor) > pBufferEnd)
            {
                MLOGWARNING("Z80 v3 snapshot truncated: memory block descriptor at offset %zu exceeds file size %zu",
                            static_cast<size_t>(memBlock - pBuffer), _fileSize);
                break;
            }

            MemoryBlockDescriptor* memoryBlockDescriptor = (MemoryBlockDescriptor*)memBlock;
            uint8_t* pageBlock = memBlock + sizeof(MemoryBlockDescriptor);
            size_t compressedBlockSize = memoryBlockDescriptor->compressedSize;
            uint8_t targetPage = memoryBlockDescriptor->memoryPage;

            // Determine emulator target page
            MemoryPageDescriptor targetPageDescriptor = resolveSnapshotPage(targetPage, _memoryMode);

            // Skip invalid/unknown pages (don't allocate or crash)
            if (targetPageDescriptor.mode == BANK_INVALID)
            {
                MLOGWARNING("Z80 v3 snapshot: unknown page %d in %s mode, skipping",
                            targetPage, (_memoryMode == Z80_48K) ? "48K" : "128K");
                // Advance to next block
                size_t skipSize = (compressedBlockSize == 0xFFFF) ? PAGE_SIZE : compressedBlockSize;
                memBlock += skipSize + sizeof(MemoryBlockDescriptor);
                if (memBlock >= pBufferEnd)
                {
                    memBlock = nullptr;
                }
                continue;
            }

            // Bounds check: ensure compressed data is within file
            size_t actualBlockSize = (compressedBlockSize == 0xFFFF) ? PAGE_SIZE : compressedBlockSize;
            if (pageBlock + actualBlockSize > pBufferEnd)
            {
                MLOGWARNING("Z80 v3 snapshot truncated: block data at offset %zu (size %zu) exceeds file size %zu",
                            static_cast<size_t>(pageBlock - pBuffer), actualBlockSize, _fileSize);
                break;
            }

            // Allocate memory page and register it in one of staging collections (ROM or RAM)
            // De-allocation will be performed after staging changes applied to main emulator memory
            // or in loader destructor
            uint8_t* pageBuffer = new uint8_t[PAGE_SIZE];
            switch (targetPageDescriptor.mode)
            {
                case BANK_ROM:
                    _stagingROMPages[targetPageDescriptor.page] = pageBuffer;
                    break;
                case BANK_RAM:
                    _stagingRAMPages[targetPageDescriptor.page] = pageBuffer;
                    break;
                default:
                    // Should not reach here - already handled above
                    delete[] pageBuffer;
                    break;
            }

            // Unpack memory block to target staging page
            if (compressedBlockSize == 0xFFFF)
            {
                // Block is not compressed and has fixed length 0x4000 (16384)
                compressedBlockSize = PAGE_SIZE;
                memcpy(pageBuffer, pageBlock, PAGE_SIZE);
            }
            else
            {
                // Block is compressed so we need to decompress it
                decompressPage(pageBlock, compressedBlockSize, pageBuffer, PAGE_SIZE);
            }

            uint8_t* nextMemBlock = memBlock + compressedBlockSize + sizeof(MemoryBlockDescriptor);
            if (nextMemBlock >= pBufferEnd || nextMemBlock == memBlock)
            {
                memBlock = nullptr;
            }
            else
            {
                memBlock = nextMemBlock;
            }
        }

        result = true;
    }

    return result;
}

bool LoaderZ80::validateHeaderSanity(Z80SnapshotVersion version)
{
    // Validate Z80-specific header constraints based on detected version
    // NOTE: Generic file type detection (ASCII, etc.) will be handled by a shared component
    rewind(_file);
    Z80Header_v1 header;
    size_t read = fread(&header, sizeof(header), 1, _file);
    if (read != 1)
        return false;

    // Note: IFF flags are sanitized during register extraction rather than rejected
    // This allows loading of files with corrupted IFF values but still valid otherwise

    // For v2/v3, validate extended header constraints
    if (version == Z80v2 || version == Z80v3)
    {
        rewind(_file);
        Z80Header_v2 headerV2;
        read = fread(&headerV2, sizeof(headerV2), 1, _file);
        if (read != 1)
            return false;

        // Validate extended header length based on expected version
        uint16_t extLen = headerV2.extendedHeaderLen;
        if (version == Z80v2 && extLen != 23)
        {
            MLOGWARNING("Z80 file '%s' v2 has invalid extended header length %d (expected 23)", 
                        _path.c_str(), extLen);
            return false;
        }
        if (version == Z80v3 && extLen != 54 && extLen != 55)
        {
            MLOGWARNING("Z80 file '%s' v3 has invalid extended header length %d (expected 54 or 55)", 
                        _path.c_str(), extLen);
            return false;
        }

        // Validate model number is in valid range (0-13 per Z80 format spec)
        if (headerV2.model > 13)
        {
            MLOGWARNING("Z80 file '%s' has invalid model number %d (max 13)", 
                        _path.c_str(), headerV2.model);
            return false;
        }
    }

    return true;
}

Z80MemoryMode LoaderZ80::getMemoryModeV2(uint8_t model)
{
    Z80MemoryMode result = Z80_48K;

    switch (model)
    {
        case 0:  // 48K
        case 1:  // 48K + IF1
            result = Z80_48K;
            break;
        case 2:  // SamRam
            result = Z80_SAMCOUPE;
            break;
        case 3:   // 128K (v2 only)
        case 4:   // 128K + IF1 (v2 only)
        case 7:   // +3
        case 8:   // +3 (alternate)
        case 9:   // Pentagon 128K
        case 12:  // +2
        case 13:  // +2A
            result = Z80_128K;
            break;
        case 10:  // Scorpion 256K
            result = Z80_256K;
            break;
        default:
            result = Z80_48K;
            break;
    }

    return result;
}

Z80MemoryMode LoaderZ80::getMemoryModeV3(uint8_t model)
{
    Z80MemoryMode result = Z80_48K;

    switch (model)
    {
        case 0:  // 48K
        case 1:  // 48K + IF1
        case 3:  // 48K + MGT (v3: model 3 is 48K, not 128K!)
            result = Z80_48K;
            break;
        case 2:  // SamRam
            result = Z80_SAMCOUPE;
            break;
        case 4:   // 128K (v3: model 4 is 128K base)
        case 5:   // 128K + IF1
        case 6:   // 128K + MGT
        case 7:   // +3
        case 8:   // +3 (alternate)
        case 9:   // Pentagon 128K
        case 12:  // +2
        case 13:  // +2A
            result = Z80_128K;
            break;
        case 10:  // Scorpion 256K
            result = Z80_256K;
            break;
        default:
            result = Z80_48K;
            break;
    }

    return result;
}

Z80Registers LoaderZ80::getZ80Registers(const Z80Header_v1& header, uint16_t pc)
{
    Z80Registers result = {};
    result.a = header.reg_A;
    result.f = header.reg_F;
    result.bc = header.reg_BC;
    result.de = header.reg_DE;
    result.hl = header.reg_HL;
    result.alt.a = header.reg_A1;
    result.alt.f = header.reg_F1;
    result.alt.bc = header.reg_BC1;
    result.alt.de = header.reg_DE1;
    result.alt.hl = header.reg_HL1;
    result.ix = header.reg_IX;
    result.iy = header.reg_IY;

    result.sp = header.reg_SP;

    result.iff1 = header.IFF1 ? 1 : 0;
    result.iff2 = header.IFF2 ? 1 : 0;
    result.i = header.reg_I;

    // Per spec: reg_R contains lower 7 bits; flags bit 0 contains bit 7 of R
    // Handle flags=255 compatibility case
    uint8_t flags = (header.flags == 255) ? 1 : header.flags;
    result.r_low = header.reg_R & 0x7F;
    result.r_hi = (flags & 0x01) << 7;

    // Interrupt mode: Z80 only supports modes 0, 1, 2
    // Mask to lower 2 bits and validate
    uint8_t im = header.im & 0x03;
    if (im > 2)
    {
        MLOGWARNING("Invalid interrupt mode %d in Z80 snapshot, using mode 0", header.im);
        im = 0;
    }
    result.im = im;

    result.pc = pc;

    result.memptr = 0;
    result.q = 0;

    return result;
}

void LoaderZ80::applyPeripheralState(const Z80Header_v2& header)
{
    (void)header;
}

/// @brief Compress memory page using Z80 RLE compression
/// @details RLE format: ED ED nn bb = repeat byte 'bb' nn times
///          - Only sequences of ≥5 identical bytes are compressed
///          - ED bytes are special: even 2 consecutive EDs → ED ED 02 ED
///          - Single ED followed by non-ED is written as-is (ED xx)
/// @param src Source uncompressed data
/// @param srcLen Source data length
/// @param dst Destination buffer for compressed data
/// @param dstLen Destination buffer size
/// @return Number of bytes written to dst
size_t LoaderZ80::compressPage(uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
    if (src == nullptr || dst == nullptr || srcLen == 0 || dstLen == 0)
    {
        return 0;
    }

    size_t srcPos = 0;
    size_t dstPos = 0;

    while (srcPos < srcLen && dstPos < dstLen)
    {
        uint8_t byte = src[srcPos];

        // Count consecutive identical bytes
        size_t runLen = 1;
        while (srcPos + runLen < srcLen && src[srcPos + runLen] == byte && runLen < 255)
        {
            runLen++;
        }

        // Special case: ED bytes must always be encoded if there are 2+ of them
        if (byte == 0xED)
        {
            if (runLen >= 2)
            {
                // Encode ED sequence: ED ED count ED
                if (dstPos + 4 > dstLen) break;
                dst[dstPos++] = 0xED;
                dst[dstPos++] = 0xED;
                dst[dstPos++] = static_cast<uint8_t>(runLen);
                dst[dstPos++] = 0xED;
                srcPos += runLen;
            }
            else
            {
                // Single ED: write as-is
                if (dstPos + 1 > dstLen) break;
                dst[dstPos++] = byte;
                srcPos++;
            }
        }
        else if (runLen >= 5)
        {
            // Encode RLE sequence: ED ED count value
            if (dstPos + 4 > dstLen) break;
            dst[dstPos++] = 0xED;
            dst[dstPos++] = 0xED;
            dst[dstPos++] = static_cast<uint8_t>(runLen);
            dst[dstPos++] = byte;
            srcPos += runLen;
        }
        else
        {
            // Write literal bytes (run too short for compression)
            for (size_t i = 0; i < runLen && dstPos < dstLen; i++)
            {
                dst[dstPos++] = byte;
            }
            srcPos += runLen;
        }
    }

    return dstPos;
}

void LoaderZ80::decompressPage(uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
    decompressPage_Optimized(src, srcLen, dst, dstLen);
}

/// @brief Original implementation - byte-by-byte RLE decompression (for benchmarking)
void LoaderZ80::decompressPage_Original(uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
    /// region <Sanity check>
    if (src == nullptr || dst == nullptr || srcLen == 0 || dstLen == 0)
    {
        return;
    }
    /// endregion </Sanity check>

    memset(dst, 0, dstLen);
    while (srcLen > 0 && dstLen > 0)
    {
        if (srcLen >= 4 && src[0] == 0xED && src[1] == 0xED)
        {
            for (uint8_t i = src[2]; i; i--)
            {
                *dst++ = src[3], dstLen--;
            }
            srcLen -= 4;
            src += 4;
        }
        else
        {
            *dst++ = *src++;
            --dstLen;
            --srcLen;
        }
    }
}

/// @brief Optimized implementation - uses memset for RLE sequences (3-7x faster)
void LoaderZ80::decompressPage_Optimized(uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
    /// region <Sanity check>
    if (src == nullptr || dst == nullptr || srcLen == 0 || dstLen == 0)
    {
        return;
    }
    /// endregion </Sanity check>

    uint8_t* dstEnd = dst + dstLen;

    while (srcLen > 0 && dst < dstEnd)
    {
        if (srcLen >= 4 && src[0] == 0xED && src[1] == 0xED)
        {
            // RLE sequence: ED ED nn bb = repeat 'bb' nn times
            uint8_t count = src[2];
            uint8_t value = src[3];

            size_t remaining = dstEnd - dst;
            size_t fillLen = std::min<size_t>(count, remaining);
            
            // Warn if compressed data would overflow buffer
            if (count > remaining)
            {
                MLOGWARNING("Z80 decompression overflow: RLE sequence requests %d bytes but only %zu available, truncating",
                            count, remaining);
            }

            // Use memset for bulk fill (SIMD-accelerated)
            memset(dst, value, fillLen);
            dst += fillLen;

            srcLen -= 4;
            src += 4;
        }
        else
        {
            // Literal byte copy
            *dst++ = *src++;
            --srcLen;
        }
    }

    // Zero-fill any remaining destination (if source exhausted early)
    if (dst < dstEnd)
    {
        memset(dst, 0, dstEnd - dst);
    }
}

size_t LoaderZ80::decompressV1Data(uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen)
{
    if (src == nullptr || dst == nullptr || srcLen == 0 || dstLen == 0)
    {
        return 0;
    }

    uint8_t* srcStart = src;
    uint8_t* dstEnd = dst + dstLen;

    while (srcLen > 0 && dst < dstEnd)
    {
        // Check for end marker: 00 ED ED 00
        if (srcLen >= 4 && src[0] == 0x00 && src[1] == 0xED && src[2] == 0xED && src[3] == 0x00)
        {
            break;
        }

        // Check for RLE sequence: ED ED nn bb
        if (srcLen >= 4 && src[0] == 0xED && src[1] == 0xED)
        {
            uint8_t count = src[2];
            uint8_t value = src[3];

            size_t remaining = dstEnd - dst;
            size_t fillLen = std::min<size_t>(count, remaining);
            
            // Warn if compressed data would overflow buffer
            if (count > remaining)
            {
                MLOGWARNING("Z80 decompression overflow: RLE sequence requests %d bytes but only %zu available, truncating",
                            count, remaining);
            }
            
            memset(dst, value, fillLen);
            dst += fillLen;

            srcLen -= 4;
            src += 4;
        }
        else
        {
            *dst++ = *src++;
            --srcLen;
        }
    }

    // Zero-fill remaining destination
    if (dst < dstEnd)
    {
        memset(dst, 0, dstEnd - dst);
    }

    return static_cast<size_t>(src - srcStart);
}

MemoryPageDescriptor LoaderZ80::resolveSnapshotPage(uint8_t page, Z80MemoryMode mode)
{
    MemoryPageDescriptor result;
    result.mode = MemoryBankModeEnum::BANK_INVALID;  // Default to invalid - must be explicitly set
    result.page = 0;
    result.addressInPage = 0x0000;

    switch (mode)
    {
        case Z80_48K:
            switch (page)
            {
                case 0:
                    result.mode = MemoryBankModeEnum::BANK_ROM;
                    result.page = 0;
                    break;
                case 1:
                    result.mode = MemoryBankModeEnum::BANK_ROM;
                    result.page = 0;
                    break;
                case 4:
                    // 0x8000 - 0xBFFF -> RAM Page 2
                    result.mode = MemoryBankModeEnum::BANK_RAM;
                    result.page = 2;
                    break;
                case 5:
                    // 0xC000 - 0xFFFF -> RAM Page 0
                    result.mode = MemoryBankModeEnum::BANK_RAM;
                    result.page = 0;
                    break;
                case 8:
                    // 0x4000 - 0x7FFF -> RAM Page 5
                    result.mode = MemoryBankModeEnum::BANK_RAM;
                    result.page = 5;
                    break;
                default:
                    // Unknown page for 48K mode - leave as BANK_INVALID
                    break;
            }
            break;
        case Z80_128K:
            if (page < 3)
            {
                result.mode = MemoryBankModeEnum::BANK_ROM;
                result.page = page;
            }
            else if (page < 11)
            {
                // 3 -> RAM Page 0
                // 4 -> RAM Page 1
                // 10 -> RAM Page 7
                result.mode = MemoryBankModeEnum::BANK_RAM;
                result.page = page - 3;
            }
            // else: page >= 11, leave as BANK_INVALID
            break;
        case Z80_256K:
            // Not implemented - leave as BANK_INVALID
            MLOGWARNING("Z80 256K mode not implemented, skipping page %d", page);
            break;
        case Z80_SAMCOUPE:
            // Not implemented - leave as BANK_INVALID
            MLOGWARNING("Z80 SamCoupe mode not implemented, skipping page %d", page);
            break;
        default:
            break;
    }

    return result;
}

/// @brief Free all memory allocated for snapshot staging
void LoaderZ80::freeStagingMemory()
{
    // Clean-up staging ROM pages
    for (size_t idx = 0; idx < MAX_ROM_PAGES; idx++)
    {
        uint8_t* ptr = _stagingROMPages[idx];
        if (ptr != nullptr)
        {
            delete ptr;
            _stagingROMPages[idx] = nullptr;
        }
    }

    // Clean-up staging RAM pages
    for (size_t idx = 0; idx < MAX_RAM_PAGES; idx++)
    {
        uint8_t* ptr = _stagingRAMPages[idx];
        if (ptr != nullptr)
        {
            delete ptr;
            _stagingRAMPages[idx] = nullptr;
        }
    }
}

/// endregion </Helper methods>

/// region <Debug methods>

std::string LoaderZ80::dumpSnapshotInfo()
{
    std::string result;
    std::stringstream ss;

    result = ss.str();
    return result;
}

std::string LoaderZ80::dumpSnapshotMemoryInfo()
{
    std::string result;
    std::stringstream ss;

    if (_stagingLoaded)
    {
        ss << "Z80 snapshot memory pages usage: " << std::endl;

        for (size_t idx = 0; idx < MAX_ROM_PAGES; idx++)
        {
            if (_stagingROMPages[idx] != nullptr)
            {
                ss << StringHelper::Format("ROM %d", idx) << std::endl;
            }
        }

        for (size_t idx = 0; idx < MAX_RAM_PAGES; idx++)
        {
            if (_stagingRAMPages[idx] != nullptr)
            {
                ss << StringHelper::Format("RAM %d", idx) << std::endl;
            }
        }
    }

    result = ss.str();
    return result;
}

/// endregion </Debug methods>