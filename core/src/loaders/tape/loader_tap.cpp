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

    while (ptr < buffer + _fileSize)
    {
        uint16_t blockSize = *(uint16_t*)ptr;

        if (blockSize > 0)
        {
            TapeBlock block;
            block.data_len = 
            TapeBlockWithHeader* block = (TapeBlockWithHeader*)ptr;

            ptr += blockSize;
        }
    }

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

void LoaderTAP::parseHardware(uint8_t* data)
{
    /// region <Hardware IDs>
    static const char* UNKNOWN_ID = "??";

    static const char hardwareIDs[] =
        "computer\0"
            "ZX Spectrum 16k\0"
            "ZX Spectrum 48k, Plus\0"
            "ZX Spectrum 48k ISSUE 1\0"
            "ZX Spectrum 128k (Sinclair)\0"
            "ZX Spectrum 128k +2 (Grey case)\0"
            "ZX Spectrum 128k +2A, +3\0"
            "Timex Sinclair TC-2048\0"
            "Timex Sinclair TS-2068\0"
            "Pentagon 128\0"
            "Sam Coupe\0"
            "Didaktik M\0"
            "Didaktik Gama\0"
            "ZX-81 or TS-1000 with  1k RAM\0"
            "ZX-81 or TS-1000 with 16k RAM or more\0"
            "ZX Spectrum 128k, Spanish version\0"
            "ZX Spectrum, Arabic version\0"
            "TK 90-X\0"
            "TK 95\0"
            "Byte\0"
            "Elwro\0"
            "ZS Scorpion\0"
            "Amstrad CPC 464\0"
            "Amstrad CPC 664\0"
            "Amstrad CPC 6128\0"
            "Amstrad CPC 464+\0"
            "Amstrad CPC 6128+\0"
            "Jupiter ACE\0"
            "Enterprise\0"
            "Commodore 64\0"
            "Commodore 128\0"
            "\0"
        "ext. storage\0"
            "Microdrive\0"
            "Opus Discovery\0"
            "Disciple\0"
            "Plus-D\0"
            "Rotronics Wafadrive\0"
            "TR-DOS (BetaDisk)\0"
            "Byte Drive\0"
            "Watsford\0"
            "FIZ\0"
            "Radofin\0"
            "Didaktik disk drives\0"
            "BS-DOS (MB-02)\0"
            "ZX Spectrum +3 disk drive\0"
            "JLO (Oliger) disk interface\0"
            "FDD3000\0"
            "Zebra disk drive\0"
            "Ramex Millenia\0"
            "Larken\0"
            "\0"
        "ROM/RAM type add-on\0"
            "Sam Ram\0"
            "Multiface\0"
            "Multiface 128k\0"
            "Multiface +3\0"
            "MultiPrint\0"
            "MB-02 ROM/RAM expansion\0"
            "\0"
        "sound device\0"
            "Classic AY hardware\0"
            "Fuller Box AY sound hardware\0"
            "Currah microSpeech\0"
            "SpecDrum\0"
            "AY ACB stereo; Melodik\0"
            "AY ABC stereo\0"
            "\0"
        "joystick\0"
            "Kempston\0"
            "Cursor, Protek, AGF\0"
            "Sinclair 2\0"
            "Sinclair 1\0"
            "Fuller\0"
            "\0"
        "mice\0"
            "AMX mouse\0"
            "Kempston mouse\0"
            "\0"
            "other controller\0"
            "Trickstick\0"
            "ZX Light Gun\0"
            "Zebra Graphics Tablet\0"
            "\0"
        "serial port\0"
            "ZX Interface 1\0"
            "ZX Spectrum 128k\0"
            "\0"
        "parallel port\0"
            "Kempston S\0"
            "Kempston E\0"
            "ZX Spectrum 128k +2A, +3\0"
            "Tasman\0"
            "DK'Tronics\0"
            "Hilderbay\0"
            "INES Printerface\0"
            "ZX LPrint Interface 3\0"
            "MultiPrint\0"
            "Opus Discovery\0"
            "Standard 8255 chip with ports 31,63,95\0"
            "\0"
        "printer\0"
            "ZX Printer, Alphacom 32 & compatibles\0"
            "Generic Printer\0"
            "EPSON Compatible\0"
            "\0"
        "modem\0"
            "VTX 5000\0"
            "T/S 2050 or Westridge 2050\0"
            "\0"
        "digitaiser\0"
            "RD Digital Tracer\0"
            "DK'Tronics Light Pen\0"
            "British MicroGraph Pad\0"
            "\0"
        "network adapter\0"
            "ZX Interface 1\0"
            "\0"
        "keyboard / keypad\0"
            "Keypad for ZX Spectrum 128k\0"
            "\0"
        "AD/DA converter\0"
            "Harley Systems ADC 8.2\0"
            "Blackboard Electronics\0"
            "\0"
        "EPROM Programmer\0"
            "Orme Electronics\0"
            "\0"
        "\0";
    /// endregion </Hardware IDs>

    uint8_t* ptr = data;
    uint16_t hardwareRecords = *data;

    for (uint16_t i = 0; i < hardwareRecords; i++)
    {
        uint8_t type_n = *ptr++;
        uint8_t id_n = *ptr++;
        uint8_t value_n = *ptr++;
        const char *type = hardwareIDs;
        const char *ptrID;
        const char *value;

        for (uint16_t j = 0; j < type_n; j++)
        {
            if (!*type)
                break;

            while (*type)
                type++;
            type += 2;
        }

        if (!*type)
        {
            type = UNKNOWN_ID;
            ptrID = UNKNOWN_ID;
            break;
        }
        else
        {
            ptrID = type + strlen(type) + 1;

            for (uint16_t k = 0; k < id_n; k++)
            {
                if (!*ptrID)
                {
                    ptrID = UNKNOWN_ID;
                    break;
                }

                ptrID += strlen(ptrID) + 1;
            }
        }

        switch (value_n)
        {
            case 0: value = "compatible with"; break;
            case 1: value = "uses"; break;
            case 2: value = "compatible, but doesn't use"; break;
            case 3: value = "incompatible with"; break;
            default: value = "??";
        }

        char bf[512]; sprintf(bf, "%s %s: %s", value, type, ptrID);
        //named_cell(bf);
    }
    //named_cell("-");
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

