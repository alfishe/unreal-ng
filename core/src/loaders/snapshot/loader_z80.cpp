#include "stdafx.h"

#include "loader_z80.h"

#include "common/filehelper.h"
#include "common/modulelogger.h"

LoaderZ80::LoaderZ80(EmulatorContext* context, const std::string& path)
{
    _context = context;
    _logger = context->pModuleLogger;

    _path = path;
}

LoaderZ80::~LoaderZ80()
{
}

bool LoaderZ80::load()
{
    bool result = false;

    if (validate())
    {
        if (stageLoad())
        {
            commitFromStage();
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

    return result;
}

void LoaderZ80::commitFromStage()
{
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
        size_t headerRead = fread(&header, sizeof(header), 1, _file);

        if (headerRead == 1)
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
        throw std::logic_error("Not supported yet");
    }

    return result;
}

bool LoaderZ80::loadZ80v2()
{
    bool result = false;

    if (_fileValidated && _snapshotVersion == Z80v2 && _fileSize > 0)
    {
        auto buffer = std::unique_ptr<uint8_t[]>(new uint8_t(_fileSize));
        uint8_t* pBuffer = buffer.get();

        // Ensure we're reading from file start
        rewind(_file);
        fread(pBuffer, _fileSize, 1, _file);

        Z80Header_v2* pHeaderV2 = (Z80Header_v2*)pBuffer;

        // Start memory blocks processing after all headers
        uint8_t* memBlock = pBuffer + sizeof(Z80Header_v1) + pHeaderV2->extended_header_len + 2;
        size_t processedBytes = memBlock - pBuffer;

        while (memBlock)
        {
            MemoryBlockDescriptor* memoryBlockDescriptor = (MemoryBlockDescriptor*)memBlock;
            uint8_t* pageBlock = memBlock + sizeof(MemoryBlockDescriptor);
            size_t compressedBlockSize = memoryBlockDescriptor->compressedSize;
            uint8_t targetPage = memoryBlockDescriptor->memoryPage;
            auto pageBuffer = std::unique_ptr<uint8_t[]>(new uint8_t[0x4000]);

            if (compressedBlockSize == 0xFFFF)
            {
                // Block is not compressed and has fixed length 0x4000 (16384)
                compressedBlockSize = 0x4000;
                memcpy(pageBuffer.get(), pageBlock, 0x4000);
            }
            else
            {
                // Block is compressed so we need to decompress it
                decompressPage(pageBlock, compressedBlockSize, pageBuffer.get(), 0x4000);
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
            break;
        case Z80_128K:
            if (page < 3)
            {
                result.mode = MemoryBankModeEnum::BANK_ROM;
                result.page = page;
            }
            else
            {
                result.mode = MemoryBankModeEnum::BANK_RAM;
                result.page = page - 3;
            }
            break;
        case Z80_SAMCOUPE:
            break;
        default:
            break;
    }

    return result;
}

/// endregion </Helper methods>

/// region <Debug methods>

std::string LoaderZ80::dumpSnapshotInfo()
{
    std::string result;
    std::stringstream ss;

    return result;
}

/// endregion </Debug methods>