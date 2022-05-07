#pragma once

#include "stdafx.h"

/// region <Documentation>
// @see TAP format: https://faqwiki.zxnet.co.uk/wiki/TAP_format
// @see: https://k1.spdns.de/Develop/Projects/zasm/Info/tap.txt
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

/// endregion </Documentation

/// region <Constants>
constexpr int MAX_TAPE_PULSES = 0x100;
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

enum TapeBlockFlagEnum : uint64_t
{
    TAP_BLOCK_FLAG_HEADER = 0x00,
    TAP_BLOCK_FLAG_DATA = 0xFF
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
    uint16_t autostart_line;
    uint16_t len_program;
};

struct TapeArrayParams
{
    uint8_t reserved;
    uint8_t var_name;
    uint16_t reserved1 = 0x8000;
};

struct TapeBytesParams
{
    uint16_t start_address;
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
    uint16_t len_data;
    
    union
    {
        uint8_t bytes[4];
        TapeProgramParams program_params;
        TapeArrayParams array_params;
        TapeBytesParams code_params;
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
    uint8_t getBlockChecksum(vector<uint8_t>& blockData);

    void makeBlock(unsigned char *data, unsigned size, unsigned pilot_t,
              unsigned s1_t, unsigned s2_t, unsigned zero_t, unsigned one_t,
              unsigned pilot_len, unsigned pause, uint8_t last = 8);
    uint16_t findPulse(uint32_t t);
    void findTapeIndex();
    void findTapeSizes();


    void parseHardware(uint8_t* data);

    void allocTapeBuffer();
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

    using LoaderTAP::validateFile;
    using LoaderTAP::parseHardware;
};
#endif // _CODE_UNDER_TEST
