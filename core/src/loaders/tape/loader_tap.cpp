#include <debugger/assembler/z80asm.h>
#include "stdafx.h"
#include "loader_tap.h"

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"

/// region <Constructors / destructors>

LoaderTAP::LoaderTAP()
{
    _context = nullptr;
    _logger = nullptr;
}

LoaderTAP::LoaderTAP(EmulatorContext* context)
{
    _context = context;
    _logger = context->pModuleLogger;
}

LoaderTAP::~LoaderTAP()
{
}

/// endregion </Constructors / destructors>

// Registration lives in TapeLoaderRegistry::Instance() (loader_tape.cpp):
// a registrar flag here is invisible to other translation units, so the
// static linker drops this object from binaries that never name LoaderTAP.

/// region <LoaderTapeBase contract>

const TapeFormatInfo& LoaderTAP::Format() const
{
    static const TapeFormatInfo format =
    {
        "tap",
        "TAP (raw ZX blocks)",
        { "tap", "spc", "sta", "ltp", "zxt" },
        &LoaderTAP::Probe
    };

    return format;
}

int LoaderTAP::Probe(std::span<const uint8_t> bytes)
{
    // Walk the [u16 len][len bytes] framing to EOF. Clean landing = high
    // score; early overrun = 0 unless at least one complete block landed
    // first. A TZX file fails immediately: its lead bytes read as a huge
    // length that never lands ("ZXTape!" -> 0x585A = 22622).
    if (bytes.empty())
    {
        return 0;
    }

    size_t offset = 0;
    size_t completeBlocks = 0;
    bool truncated = false;

    while (offset + 2 <= bytes.size())
    {
        uint16_t length = static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
        offset += 2;

        if (length == 0)
        {
            continue;  // zero-length block: legal framing, contributes nothing
        }

        if (offset + length > bytes.size())
        {
            truncated = true;  // length prefix may still end at EOF — not a clean landing
            break;
        }

        offset += length;
        completeBlocks++;
    }

    if (!truncated && offset == bytes.size() && completeBlocks > 0)
    {
        return 100;
    }

    return completeBlocks > 0 ? 25 : 0;
}

TapeImage LoaderTAP::Load(std::span<const uint8_t> bytes, const std::string& sourceName)
{
    TapeImage image;
    image.formatId = "tap";

    size_t offset = 0;
    size_t index = 0;

    while (offset + 2 <= bytes.size())
    {
        uint16_t length = static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
        offset += 2;

        if (length == 0)
        {
            continue;  // zero-length block: skip, keep scanning
        }

        if (offset + length > bytes.size())
        {
            // Truncated final block: keep the complete prefix, warn — the
            // image is degraded but usable (Warnings), not unrecoverable
            image.parseWarnings.push_back("Truncated final block: " +
                                          std::to_string(bytes.size() - offset) + " of " +
                                          std::to_string(length) + " bytes present; dropped");
            offset = bytes.size();  // consumed: stop scanning
            break;
        }

        TapeBlock block;
        block.blockIndex = index;
        block.data.assign(bytes.begin() + offset, bytes.begin() + offset + length);
        block.type = static_cast<TapeBlockFlagEnum>(block.data[0]);

        image.blocks.push_back(std::move(block));
        offset += length;
        index++;
    }

    if (offset < bytes.size())
    {
        // Dangling tail bytes: an incomplete length prefix — warn, not fatal
        image.parseWarnings.push_back("Dangling tail: " + std::to_string(bytes.size() - offset) +
                                      " trailing byte(s) after the last block");
    }

    if (image.blocks.empty())
    {
        image.status = TapeLoadStatus::Malformed;
        image.errorText = "No complete TAP blocks found in '" + sourceName + "'";
    }
    else if (!image.parseWarnings.empty())
    {
        image.status = TapeLoadStatus::Warnings;
    }

    return image;
}

/// endregion </LoaderTapeBase contract>

/// region <Methods>

std::vector<TapeBlock> LoaderTAP::loadTAP(std::string filePath)
{
    std::vector<TapeBlock> result;

    FILE* file = FileHelper::OpenExistingFile(filePath);
    if (file)
    {
        size_t index = 0;

        while (true)
        {
            TapeBlock block = readNextBlock(file);
            block.blockIndex = index;

            if (!block.data.empty())
            {
                result.push_back(block);
            }
            else
            {
                break;
            }

            index++;
        }

        FileHelper::CloseFile(file);
    }

    return result;
}

/// endregion </Methods>

/// region <Helper methods>

TapeBlock LoaderTAP::readNextBlock(FILE* file)
{
    TapeBlock result;

    if (file != nullptr)
    {
        uint8_t sizeBytes[2];

        size_t bytesRead = FileHelper::ReadFileToBuffer(file, (uint8_t*)&sizeBytes, 2);

        if (bytesRead)
        {
            uint16_t blockSize = sizeBytes[1] << 8 | sizeBytes[0];

            if (blockSize > 0)
            {
                result.data.resize(blockSize);
                FileHelper::ReadFileToBuffer(file, result.data.data(), blockSize);

                result.type = (TapeBlockFlagEnum)result.data[0];
            }
        }
    }

    return result;
}

/// Calculate checksum for the tape block
/// @note The checksum is the bitwise XOR of all paramBytes including the flag(aka block type) byte
/// @see http://fizyka.umk.pl/~jacek/zx/faq/reference/formats.htm
/// @param blockData
/// @return
uint8_t LoaderTAP::getBlockChecksum(const vector<uint8_t>& blockData)
{
    uint8_t result = 0;
    uint32_t size = blockData.size();

    if (size > 0)
    {
        result = blockData[0];

        for (uint32_t i = 1; i < size - 1; i++)
        {
            result ^= blockData[i];
        }
    }

    return result;
}

/// Checks whether block contains valid data that matches checksum
/// @param blockData Block data
/// @return Block validity status
bool LoaderTAP::isBlockValid(const vector<uint8_t>& blockData)
{
    bool result = false;

    // Block cannot have length less than 3 paramBytes since it must contain
    // [0]        - Block type
    // [1... N-1] - Block data paramBytes
    // [N]        - Checksum
    if (blockData.size() > 2)
    {
        uint8_t checksum = blockData.back();
        uint8_t calculatedChecksum = getBlockChecksum(blockData);

        if (checksum == calculatedChecksum)
        {
            result = true;
        }
    }

    return result;
}

/// endregion </Helper methods>

/// region <Debug methods>

std::string LoaderTAP::dumpBlocks(const std::vector<TapeBlock>& dataBlocks)
{
    std::stringstream ss;

    ss << "Blocks total: " << dataBlocks.size() << std::endl;

    size_t index = 1;
    std::for_each(dataBlocks.begin(), dataBlocks.end(), [this, &index, &ss](const TapeBlock& dataBlock)
    {
        ss << "Block: " << index << std::endl;
        ss << "  Size: " << dataBlock.data.size() << std::endl;

        ss << this->dumpBlock(dataBlock.data);

        index++;
    });

    return ss.str();
}

std::string LoaderTAP::dumpBlock(const std::vector<uint8_t>& dataBlock)
{
    std::stringstream ss;

    /// region <Block flag>

    TapeBlockFlagEnum flag = (TapeBlockFlagEnum)dataBlock.front();
    uint8_t checksum = dataBlock.back();
    bool isChecksumValid = isBlockValid(dataBlock);
    std::string checksumValidityString = isChecksumValid ? "Valid" : "Invalid";

    std::string flagName;
    if (flag == 0x00 || flag == 0xFF)
    {
        flagName = getTapeBlockFlagEnumName(flag);
    }
    else
    {
        flagName = StringHelper::Format("<Invalid flag (0x%x)>)", flag);
    }

    ss << StringHelper::Format("  Flag: 0x%02X (%s)", flag, flagName.c_str()) << std::endl;

    /// endregion </Block flag>

    /// region <Header block>

    if (flag == TAP_BLOCK_FLAG_HEADER)
    {
        ZXTapeBlockTypeEnum type = (ZXTapeBlockTypeEnum)dataBlock.front();
        std::string typeName;
        if (type <= TAP_BLOCK_CODE)
        {
            typeName = getTapeBlockTypeName(type);
        }
        else
        {
            typeName = StringHelper::Format("<Invalid block type (0x%x>)", type);
        }

        std::string filename(dataBlock.begin() + 2, dataBlock.begin() + 11);
        uint16_t dataBlockLength = (dataBlock[13] << 8) | dataBlock[12];
        uint16_t param1 = (dataBlock[15] << 8) | dataBlock[14];
        uint16_t param2 = (dataBlock[17] << 8) | dataBlock[16];

        ss << StringHelper::Format("    Block type: 0x%02X (%s)", type, typeName.c_str()) << std::endl;
        ss << StringHelper::Format("    Filename: '%s'", filename.c_str()) << std::endl;
        ss << StringHelper::Format("    Data block length: 0x%04X (%d)", dataBlockLength, dataBlockLength) << std::endl;
        ss << StringHelper::Format("    Param1: 0x%04X (%d)", param1, param1) << std::endl;
        ss << StringHelper::Format("    Param2: 0x%04X (%d)", param2, param2) << std::endl;
    }

    /// endregion </Header block>

    /// region <Data block>

    if (flag == TAP_BLOCK_FLAG_DATA)
    {
        uint16_t dataSize = dataBlock.size() - 2;
        ss << StringHelper::Format("    Data: 0x%04X (%d) paramBytes", dataSize, dataSize) << std::endl;
    }

    ss << StringHelper::Format("  Checksum: 0x%02X (%d) - %s", checksum, checksum, checksumValidityString.c_str()) << std::endl;

    /// endregion </Data block>

    return ss.str();
}
/// endregion </Debug methods>

