#pragma once

#include "stdafx.h"

/// region <Documentation>

/// region <TAP format>
// @see TAP format: https://faqwiki.zxnet.co.uk/wiki/TAP_format
// @see: https://k1.spdns.de/Develop/Projects/zasm/Info/tap.txt
// @see: https://documentation.help/BASin/format_tape.html
// @see: http://web.archive.org/web/20110711141601/http://www.zxmodules.de/fileformats/tapformat.html
// @see: https://shred.zone/cilla/page/440/r-tape-loading-error.html
// @see: https://shred.zone/cilla/page/441/r-tape-loading-error-part-2.html
// @see TAP structures: https://formats.kaitai.io/zx_spectrum_tap/index.html

//    The .TAP files contain blocks of tape-saved data.
//    All blocks start with two bytes specifying how many bytes will follow (not counting the two length bytes).
//    Then raw tape data follows, including the flag and checksum bytes. The checksum is the bitwise XOR of all bytes
//    including the flag byte.
//
//    For example, when you execute the line SAVE "ROM" CODE 0,2 this will result:
//          |------ Spectrum-generated data -------|       |---------|
//
//    13 00 00 03 52 4f 4d 7x20 02 00 00 00 00 80 f1 04 00 ff f3 af a3
//
//    ^^_^^...... first block is 19 bytes (17 bytes+flag+checksum)
//          ^^... flag byte (A reg, 00 for headers, ff for data blocks)
//             ^^ first byte of header, indicating a code block
//
//    file name ..^^^^^^^^^^^^^
//    header info ..............^^^^^^^^^^^^^^^^^
//    checksum of header .........................^^
//    length of second block ........................^^_^^
//    flag byte ...........................................^^
//    first two bytes of rom .................................^^_^^
//    checksum ......................................................^^
/// endregion </TAP format>

/// region <ZX-Spectrum tape timings>

/// Tape signal is frequency-modulation encoded
/// Signal types:
/// 1. Pilot tone - 807Hz (2168 high + 2168 low Z80 t-states @3.5MHz). Pilot Freq = 3500000 / (2168 + 2168) = 887Hz
/// 2. Synchronization signal - asymmetrical: 668 t-states high (190.6 uS) and 735 t-states low (210 uS)
/// 3. Data: 0-encoding - 2047Hz (855 high + 855 low t-states). Zero Freq = 3500000 / (855 + 855) = 2047Hz
/// 4. Data: 1-encoding - 1023Hz (1710 high + 1710 low t-states). One Freq = 3500000 / (1710 + 1710) = 1023Hz

/// endregion </ZX-Spectrum tape timings>

/// endregion </Documentation

/// region <Constants>
constexpr int MAX_TAPE_PULSES = 0x100;
constexpr uint16_t PILOT_TONE_HALF_PERIOD = 2168;
constexpr uint16_t SYNCHO1 = 667;
constexpr uint16_t SYNCHRO2 = 735;
constexpr uint16_t ZERO_ENCODE_HALF_PERIOD = 855;
constexpr uint16_t ONE_ENCODE_HALF_PERIOD = 1710;


//2168, 667, 735, 855, 1710, (*ptr < 4) ? 8064 : 3220, 1000

/// endregion </Constants>

/// region <Types>

/// region <TAP parsing structures>

enum TapeBlockTypeEnum : uint8_t
{
    TAP_BLOCK_PROGRAM = 0,          // Block contains BASIC program
    TAP_BLOCK_NUM_ARRAY,            // Block contains numeric array
    TAP_BLOCK_CHAR_ARRAY,           // Block contains symbolic array
    TAP_BLOCK_CODE                  // Block contains code
};

inline const char* getTapeBlockTypeName(TapeBlockTypeEnum value)
{
    static const char* names[] =
    {
    "Program",
    "Numeric array",
    "Symbolic array",
    "Code"
    };

    return names[value];
};

enum TapeBlockFlagEnum : uint8_t
{
    TAP_BLOCK_FLAG_HEADER = 0x00,
    TAP_BLOCK_FLAG_DATA = 0xFF
};

inline const char* getTapeBlockFlagName(TapeBlockFlagEnum value)
{
    const char* header = "Header";
    const char* data = "Data";
    const char* unknown = "<Unknown value";

    const char* result;
    switch (value)
    {
        case 0x00:
            result = header;
            break;
        case 0xFF:
            result = data;
            break;
        default:
            result = unknown;
            break;
    }

    return result;
};

/// Tape information (header)
struct TapeInfo
{
    char desc[280];     // Tape name
    uint32_t pos;       // Data start offset
    uint32_t t_size;    // Data size
};

struct TapeProgramParams
{
    uint16_t autostartLine;
    uint16_t programLength;
};

struct TapeArrayParams
{
    uint8_t reserved;
    uint8_t varName;
    uint16_t reserved1 = 0x8000;
};

struct TapeBytesParams
{
    uint16_t startAddress;
    uint16_t reserved;
};

struct TapeHeader
{
    union
    {
        TapeBlockTypeEnum headerType;
        uint8_t uHeaderType;
    };

    char filename[10];
    uint16_t dataLength;
    
    union
    {
        uint8_t bytes[4];
        TapeProgramParams programParams;
        TapeArrayParams arrayParams;
        TapeBytesParams codeParams;
    };

    uint8_t checksum;
} __attribute__((packed));

struct TapeBlockWithHeader
{
    uint8_t flag;
    TapeHeader header;
} __attribute__((packed));

struct TapeBlockDescriptor
{
    uint16_t len_block;
    uint8_t flag;
};

/// endregion </TAP parsing structures>

/// region <Emulator-specific structures>
struct TapeBlock
{
    std::string name;
    std::string description;
    uint32_t data_len;
    uint8_t data_checksum;
    std::vector<uint8_t> data;
};

struct TapeState
{
    int64_t edge_change     = 0x7FFF'FFFF'FFFF'FFFFLL;
    int32_t tape_bit        = -1;
    uint8_t* play_pointer   = nullptr;      // or NULL if tape stopped
    uint8_t* end_of_tape    = nullptr;     // where to stop tape
    int32_t index           = 0;           // current tape block
};

/// endregion </Emulator-specific structures>

/// endregion </Types>

class EmulatorContext;
class ModuleLogger;

class LoaderTAP
{
    /// region <Fields>
protected:
    EmulatorContext* _context;
    ModuleLogger* _logger;

    // File-related fields
    std::string _path;
    FILE* _file = nullptr;
    bool _fileValidated = false;
    size_t _fileSize = 0;
    void* _buffer = nullptr;

    // Parsed blocks-related fields
    std::list<TapeBlock> _tapeBlocks;

    uint32_t tape_pulse[MAX_TAPE_PULSES];
    uint32_t max_pulses = 0;
    uint32_t tape_err = 0;

    uint8_t* tape_image = 0;
    uint32_t tape_imagesize = 0;

    TapeInfo* tapeinfo;
    uint32_t tape_infosize;

    TapeState _tapeState;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LoaderTAP(EmulatorContext* context, std::string path);
    virtual ~LoaderTAP();
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    void reset();
    void start();
    void stop();
    int read();
    void close();
    /// endregion </Methods>

protected:
    bool validateFile();
    bool parseTAP();
    std::string getBlockName(vector<uint8_t>& blockData);
    std::string getBlockDescription(vector<uint8_t>& blockData);

    vector<uint8_t> readNextBlock(FILE* file);
    bool isBlockValid(const vector<uint8_t>& blockData);
    uint8_t getBlockChecksum(const vector<uint8_t>& blockData);
    void convertPayloadDataToBitstream(const vector<uint8_t>& payloadData);


    void makeBlock(unsigned char *data, unsigned size, unsigned pilot_t,
              unsigned s1_t, unsigned s2_t, unsigned zero_t, unsigned one_t,
              unsigned pilot_len, unsigned pause, uint8_t last = 8);
    uint16_t findPulse(uint32_t t);
    void findTapeIndex();
    void findTapeSizes();


    void parseHardware(uint8_t* data);

    /// region <Debug methods>

public:
    std::string dumpBlocks(const std::vector<std::vector<uint8_t>>& dataBlocks);
    std::string dumpBlock(const std::vector<uint8_t>& dataBlock);

    /// endregion </Debug methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class LoaderTAPCUT : public LoaderTAP
{
public:
    LoaderTAPCUT(EmulatorContext* context, std::string path) : LoaderTAP(context, path) {};

public:
    using LoaderTAP::_context;
    using LoaderTAP::_logger;
    using LoaderTAP::_path;
    using LoaderTAP::_file;

    using LoaderTAP::readNextBlock;
    using LoaderTAP::isBlockValid;
    using LoaderTAP::getBlockChecksum;

    using LoaderTAP::validateFile;
    using LoaderTAP::parseTAP;
    using LoaderTAP::parseHardware;
    using LoaderTAP::getBlockName;
    using LoaderTAP::getBlockDescription;
};
#endif // _CODE_UNDER_TEST
