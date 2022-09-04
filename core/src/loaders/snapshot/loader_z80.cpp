#include "stdafx.h"

#include "loader_z80.h"

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include <common/stringhelper.h>

#include <debugger/debugmanager.h>
#include <debugger/breakpoints/breakpointmanager.h>


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

bool LoaderZ80::validate()
{
    bool result = false;

    // 1. Check that file exists
    if (FileHelper::FileExists(_path))
    {
        _file = FileHelper::OpenFile(_path);
        if (_file != nullptr)
        {
            // 2. Check file has appropriate size (header + data bytes)
            _fileSize = FileHelper::GetFileSize(_path);
            if (_fileSize > 0)
            {
                size_t dataSize = _fileSize - sizeof(Z80Header_v1);
                if (dataSize > 0)
                {
                    Z80SnapshotVersion ver = getSnapshotFileVersion();
                    if (ver != Unknown)
                    {
                        _snapshotVersion = ver;
                        result = true;
                    }
                }
                else
                {
                    MLOGWARNING("Z80 snapshot file '%s' has incorrect size %d", _path.c_str(), _fileSize);
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
        PortDecoder& ports = *_context->pPortDecoder;

        /// region <Apply port configuration>
        switch (_memoryMode)
        {
            case Z80_48K:
            {
                uint8_t port7FFD = PORT_7FFD_RAM_BANK_0 | PORT_7FFD_SCREEN_NORMAL | PORT_7FFD_ROM_BANK_1 | PORT_7FFD_LOCK;
                ports.PeripheralPortOut(0x7FFD, port7FFD);
                ports.PeripheralPortOut(0xFFFD, _portFFFD);
            }
                break;
            case Z80_128K:
                ports.PeripheralPortOut(0x7FFD, _port7FFD);
                ports.PeripheralPortOut(0xFFFD, _portFFFD);
                break;
            case Z80_256K:
                break;
            default:
                throw std::logic_error("Not supported");
                break;
        }

        /// endregion </Apply port configuration>

        /// region <Transfer memory content>

        for (size_t idx = 0; idx < MAX_ROM_PAGES; idx++)
        {
            uint8_t *ptr = _stagingROMPages[idx];

            if (ptr != nullptr)
            {
                throw std::logic_error("Z80 snapshot loader: ROM pages transfer from snapshot not implemented yet");
            }
        }

        for (size_t idx = 0; idx < MAX_RAM_PAGES; idx++)
        {
            uint8_t *ptr = _stagingRAMPages[idx];

            if (ptr != nullptr)
            {
                uint8_t *targetPage = memory.RAMPageAddress(idx);
                memcpy(targetPage, ptr, PAGE_SIZE);
            }
        }

        // Free used staging memory
        freeStagingMemory();

        /// endregion </Transfer memory content>

        /// region <Transfer Z80 registers>
        Z80Registers* actualRegisters = static_cast<Z80Registers*>(_context->pCPU->GetZ80());
        memcpy(actualRegisters, &_z80Registers, sizeof(Z80Registers));
        /// endregion </Transfer Z80 registers>
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
                // PC register is not zero, it's Z80 v2 or newer format
                rewind(_file);
                Z80Header_v2 headerV2;
                headerRead = fread(&headerV2, sizeof(headerV2), 1, _file);

                if (headerRead == 1)
                {
                    uint16_t extendedHeaderSize = headerV2.extended_header_len;

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
        Z80MemoryMode memoryMode = Z80_48K;

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

        auto unpackedMemory = std::unique_ptr<uint8_t[]>(new uint8_t[3 * PAGE_SIZE]);
        size_t dataSize = _fileSize - sizeof(headerV1);


        throw std::logic_error("Not supported yet");
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
        _z80Registers = getZ80Registers(headerV1, headerV2.new_PC);

        // Determine snapshot memory model based on model
        _memoryMode = getMemoryMode(headerV2);

        // Retrieve ports configuration
        _port7FFD = headerV2.p7FFD;
        _portFFFD = headerV2.pFFFD;

        // Start memory blocks processing after all headers
        uint8_t* memBlock = pBuffer + sizeof(Z80Header_v1) + headerV2.extended_header_len + 2;
        size_t processedBytes = memBlock - pBuffer;

        while (memBlock)
        {
            MemoryBlockDescriptor* memoryBlockDescriptor = (MemoryBlockDescriptor*)memBlock;
            uint8_t* pageBlock = memBlock + sizeof(MemoryBlockDescriptor);
            size_t compressedBlockSize = memoryBlockDescriptor->compressedSize;
            uint8_t targetPage = memoryBlockDescriptor->memoryPage;

            // Determine emulator target page
            MemoryPageDescriptor targetPageDescriptor = resolveSnapshotPage(targetPage, _memoryMode);

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
            processedBytes = nextMemBlock - pBuffer;
            if (processedBytes >= _fileSize || nextMemBlock == memBlock)
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

    if (_fileValidated && _snapshotVersion == Z80v3)
    {
        throw std::logic_error("Not supported yet");
    }

    return result;
}

Z80MemoryMode LoaderZ80::getMemoryMode(const Z80Header_v2& header)
{
    Z80MemoryMode result = Z80_48K;

    switch (header.model)
    {
        case Z80_MODEL2_48K:
        case Z80_MODEL2_48K_IF1:
            result = Z80_48K;
            break;
        case Z80_MODEL2_SAMRAM:
            result = Z80_SAMCOUPE;
            break;
        case Z80_MODEL2_128K:
        case Z80_MODEL2_128K_IF1:
        case Z80_MODEL2_128K_2:
        case Z80_MODEL2_128K_2A:
        case Z80_MODEL2_128K_3:
        case Z80_MODEL2_128k_3_1:
        case Z80_MODEL2_P128K:
            result = Z80_128K;
            break;
        case Z80_MODEL2_ZS256K:
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
    result.r_low = header.reg_R;
    result.r_hi = header.reg_R & 0x80;
    result.im = header.im & 0x03;

    result.pc = pc;

    return result;
}

void LoaderZ80::applyPeripheralState(const Z80Header_v2& header)
{

}

void LoaderZ80::decompressPage(uint8_t *src, size_t srcLen, uint8_t *dst, size_t dstLen)
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
        if  (srcLen >= 4 && src[0] == 0xED && src[1] == 0xED)
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

MemoryPageDescriptor LoaderZ80::resolveSnapshotPage(uint8_t page, Z80MemoryMode mode)
{
    MemoryPageDescriptor result;
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
            break;
        case Z80_256K:
            throw std::logic_error("Not implemented yet");
            break;
        case Z80_SAMCOUPE:
            throw std::logic_error("Not implemented yet");
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