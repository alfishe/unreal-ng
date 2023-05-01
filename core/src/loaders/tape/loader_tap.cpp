#include "stdafx.h"
#include "loader_tap.h"

#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"

/// region <Constructors / destructors>

LoaderTAP::LoaderTAP(EmulatorContext* context, std::string path)
{
    _context = context;
    _logger = context->pModuleLogger;

    _path = path;
}

LoaderTAP::~LoaderTAP()
{
    if (_file)
    {
        FileHelper::CloseFile(_file);
        _file = nullptr;
    }

    if (_buffer)
    {
        free(_buffer);
    }
}

/// endregion </Constructors / destructors>

/// region <Methods>

void LoaderTAP::reset()
{
    _tapeState.play_pointer = nullptr;
    _tapeState.edge_change = 0x7FFF'FFFF'FFFF'FFFFLL;
    _tapeState.tape_bit = -1;
    _tapeState.index = 0;
}

int LoaderTAP::read()
{
    int result = 0;

    if (validateFile())
    {
        _buffer = malloc(_fileSize);

        if (_buffer)
        {
            if (FileHelper::ReadFileToBuffer(_path, (uint8_t *)_buffer, _fileSize) == _fileSize)
            {
                result = parseTAP();
            }

            free(_buffer);
            _buffer = nullptr;
        }
    }

    return result;
}

void LoaderTAP::close()
{
}

/// endregion </Methods>

/// region <Helper methods>

bool LoaderTAP::validateFile()
{
    bool result = false;

    if (FileHelper::FileExists(_path))
    {
        _file = FileHelper::OpenFile(_path);
        if (_file != nullptr)
        {
            _fileSize = FileHelper::GetFileSize(_file);

            result = true;
        }
    }

    // Persist validation state in the field
    _fileValidated = result;

    return result;
}

bool LoaderTAP::parseTAP()
{
    bool result = false;

    uint8_t* buffer = (uint8_t *)_buffer;
    uint8_t* ptr = buffer;

    // Read all blocks from TAP file
    // Each block is represented by:
    //   block length - 2 bytes
    //   block body - <block length> bytes
    while (ptr < buffer + _fileSize)
    {
        // Get block length
        uint16_t blockSize = *(uint16_t*)ptr;
        ptr += 2;

        // Parse block
        if (blockSize > 0)
        {
            TapeBlock block;
            block.data_len = blockSize;
            block.data.insert(block.data.end(), ptr, ptr + blockSize);
            block.name = getBlockName(block.data);
            block.description = getBlockDescription(block.data);
            block.data_checksum = getBlockChecksum(block.data);

            // Add block to block list
            _tapeBlocks.push_back(block);

            ptr += blockSize;
        }
    }

    int blocks = _tapeBlocks.size();
    int a = 2;

    /*
    unsigned char *ptr = snbuf;
    close();

    while (ptr < snbuf+snapsize)
    {
        unsigned size = *(unsigned short*)ptr; ptr += 2;
        if (!size) break;
        alloc_infocell();
        desc(ptr, size, tapeinfo[tape_infosize].desc);
        tape_infosize++;
        makeblock(ptr, size, 2168, 667, 735, 855, 1710, (*ptr < 4) ? 8064 : 3220, 1000);
        ptr += size;
    }

    find_tape_sizes();
    result = (ptr == snbuf+snapsize);
*/

    return result;
}

vector<uint8_t> LoaderTAP::readNextBlock(FILE* file)
{
    vector<uint8_t> result;

    if (file != nullptr)
    {
        uint8_t sizeBytes[2];

        size_t bytesRead = FileHelper::ReadFileToBuffer(file, (uint8_t*)&sizeBytes, 2);

        if (bytesRead)
        {
            uint16_t blockSize = sizeBytes[1] << 8 | sizeBytes[0];

            if (blockSize > 0)
            {
                result.resize(blockSize);
                FileHelper::ReadFileToBuffer(file, result.data(), blockSize);
            }
        }
    }

    return result;
}

/// Calculate checksum for the tape block
/// @note The checksum is the bitwise XOR of all bytes including the flag(aka block type) byte
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

    // Block cannot have length less than 3 bytes since it must contain
    // [0]        - Block type
    // [1... N-1] - Block data bytes
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

std::string LoaderTAP::getBlockName(vector<uint8_t>& blockData)
{
    std::string result;

    uint32_t size = blockData.size();
    uint8_t flag = blockData[0];

    if (flag == TAP_BLOCK_FLAG_HEADER && size == 19)
    {
        TapeHeader* header = (TapeHeader*)&blockData[1];
        result = string(header->filename, 10);
    }

    return result;
}

std::string LoaderTAP::getBlockDescription(vector<uint8_t>& blockData)
{
    std::string result;

    uint32_t size = blockData.size();
    uint8_t flag = blockData[0];

    if (flag == TAP_BLOCK_FLAG_HEADER)
    {
        TapeHeader* header = (TapeHeader*)&blockData[1];
        std::string name = string(header->filename, 10);
        TapeBlockTypeEnum type = header->headerType;

        result = StringHelper::Format("Header");
    }
    else if (flag == TAP_BLOCK_FLAG_DATA)
    {
        result = StringHelper::Format("Data");
    }
    else
    {
        result = "<Invalid>";
    }

    return result;
}



void LoaderTAP::makeBlock(unsigned char *data, unsigned size, unsigned pilot_t,
               unsigned s1_t, unsigned s2_t, unsigned zero_t, unsigned one_t,
               unsigned pilot_len, unsigned pause, uint8_t last)
{
    //reserve(size*16 + pilot_len + 3);

    if (pilot_len != -1)
    {
        unsigned t = findPulse(pilot_t);

        for (unsigned i = 0; i < pilot_len; i++)
            tape_image[tape_imagesize++] = t;

        tape_image[tape_imagesize++] = findPulse(s1_t);
        tape_image[tape_imagesize++] = findPulse(s2_t);
    }

    unsigned t0 = findPulse(zero_t), t1 = findPulse(one_t);
    for (; size>1; size--, data++)
    {
        for (unsigned char j = 0x80; j; j >>= 1)
            tape_image[tape_imagesize++] = (*data & j) ? t1 : t0,
                    tape_image[tape_imagesize++] = (*data & j) ? t1 : t0;
    }

    // Process last byte for the block
    for (unsigned char j = 0x80; j != (unsigned char)(0x80 >> last); j >>= 1)
    {
        tape_image[tape_imagesize++] = (*data & j) ? t1 : t0,
        tape_image[tape_imagesize++] = (*data & j) ? t1 : t0;
    }

    if (pause)
    {
        tape_image[tape_imagesize++] = findPulse(pause * 3500);
    }
}

uint16_t LoaderTAP::findPulse(uint32_t t)
{
    uint16_t result;

    if (max_pulses < MAX_TAPE_PULSES)
    {
        for (unsigned i = 0; i < max_pulses; i++)
        {
            if (tape_pulse[i] == t)
                return i;
        }

        tape_pulse[max_pulses] = t;
        return max_pulses++;
    }

    if (!tape_err)
    {
        //errmsg("pulse table full");
        tape_err = 1;
    }

    uint32_t nearest = 0;
    int32_t delta = 0x7FFF'FFFF;
    for (unsigned i = 0; i < MAX_TAPE_PULSES; i++)
    {
        int32_t value = abs((int)t - (int)tape_pulse[i]);

        if (delta > value)
        {
            nearest = i;
            delta = value;
        }
    }

    return result;
}

/// endregion </Helper methods>

/// region <Debug methods>

std::string LoaderTAP::dumpBlocks(const std::vector<std::vector<uint8_t>>& dataBlocks)
{
    std::stringstream ss;

    ss << "Blocks total: " << dataBlocks.size() << std::endl;

    size_t index = 1;
    std::for_each(dataBlocks.begin(), dataBlocks.end(), [this, &index, &ss](const std::vector<uint8_t>& dataBlock)
    {
        ss << "Block: " << index << std::endl;
        ss << "  Size: " << dataBlock.size() << std::endl;

        ss << this->dumpBlock(dataBlock);

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
        flagName = getTapeBlockFlagName(flag);
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
        TapeBlockTypeEnum type = (TapeBlockTypeEnum)dataBlock.front();
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
        ss << StringHelper::Format("    Data: 0x%04X (%d) bytes", dataSize, dataSize) << std::endl;
    }

    ss << StringHelper::Format("  Checksum: 0x%02X (%d) - %s", checksum, checksum, checksumValidityString.c_str()) << std::endl;

    /// endregion </Data block>

    return ss.str();
}
/// endregion </Debug methods>

