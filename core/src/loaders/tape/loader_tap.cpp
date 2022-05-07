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
        FILE* file = FileHelper::OpenFile(_path, "r");
        if (file != nullptr)
        {
            _buffer = malloc(_fileSize);
            if (FileHelper::ReadFileToBuffer(file, (uint8_t *)_buffer, _fileSize) == _fileSize)
            {
                result = parseTAP();
            }

            free(_buffer);
            _buffer = nullptr;

            FileHelper::CloseFile(file);
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
    close();

    // Read all blocks from TAP file
    // Each block is represented by:
    //   block length - 2 bytes
    //   block body - <block length> bytes
    while (ptr < buffer + _fileSize)
    {
        // Get block length
        uint16_t blockSize = *(uint16_t*)ptr;
        ptr += 2;

        // Get block itself
        if (blockSize > 0)
        {
            TapeBlock block;
            block.data_len = blockSize;
            block.data.insert(block.data.end(), ptr, ptr + blockSize);
            block.name = getBlockName(block.data);
            block.description = getBlockDescription(block.data);
            block.data_checksum = getBlockChecksum(block.data);
            _tapeBlocks.push_back(block);

            TapeBlockWithHeader* blockHeader = (TapeBlockWithHeader*)ptr;

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

/// Calculate checksum for the tape block
/// @note The checksum is the bitwise XOR of all bytes including the flag byte
/// @see http://fizyka.umk.pl/~jacek/zx/faq/reference/formats.htm
/// @param blockData
/// @return
uint8_t LoaderTAP::getBlockChecksum(vector<uint8_t>& blockData)
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

#ifdef _DEBUG
    // Get checksum saved in a last block byte to compare with calculated
    uint8_t savedChecksum = blockData[size - 1];

    if (savedChecksum != result)
    {

    }
#endif // _DEBUG

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

void LoaderTAP::findTapeIndex()
{
    for (unsigned i = 0; i < tape_infosize; i++)
    {
        if (_tapeState.play_pointer >= tape_image + tapeinfo[i].pos)
            _tapeState.index = i;
    }
}

void LoaderTAP::findTapeSizes()
{
    /*
    for (unsigned i = 0; i < tape_infosize; i++)
    {
        unsigned end = (i == tape_infosize-1) ? tape_imagesize : tapeinfo[i+1].pos;
        unsigned sz = 0;
        for (unsigned j = tapeinfo[i].pos; j < end; j++)
        {
            sz += tape_pulse[tape_image[j]];
        }

        tapeinfo[i].t_size = sz;
    }
     */
}

void LoaderTAP::allocTapeBuffer()
{
    /*
    tapeinfo = (TAPEINFO*)realloc(tapeinfo, (tape_infosize + 1) * sizeof(TAPEINFO));
    tapeinfo[tape_infosize].pos = tape_imagesize;
    appendable = 0;
     */
}

/// endregion </Helper methods>

