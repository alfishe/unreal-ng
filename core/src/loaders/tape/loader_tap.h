#pragma once

#include "stdafx.h"

/// region <Documentation>
// TAP format: https://faqwiki.zxnet.co.uk/wiki/TAP_format
// TAP structures: https://formats.kaitai.io/zx_spectrum_tap/index.html

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
struct TAPEINFO
{
    char desc[280];
    uint32_t pos;
    uint32_t t_size;
};

enum TAP_BLOCK_TYPE : uint8_t
{
    TAP_BLOCK_PROGRAM = 0,
    TAP_BLOCK_NUM_ARRAY,
    TAP_BLOCK_CHAR_ARRAY,
    TAP_BLOCK_CODE
};

struct TAP_PROGRAM_PARAMS
{
    uint16_t autostart_line;
    uint16_t len_program;
};

struct TAP_ARRAY_PARAMS
{
    uint8_t reserved;
    uint8_t var_name;
    uint16_t reserved1 = 0x8000;
};

struct TAP_CODE_PARAMS
{
    uint16_t start_address;
    uint16_t reserved;
};

struct TAP_HEADER
{
    TAP_BLOCK_TYPE header_type;
    char filename[10];
    uint16_t len_data;
    union
    {
        uint8_t bytes[4];
        TAP_PROGRAM_PARAMS program_params;
        TAP_ARRAY_PARAMS array_params;
        TAP_CODE_PARAMS code_params;
    };
    uint8_t checksum;
};

struct TAP_BLOCK
{
    uint16_t len_block;
    uint8_t flag;
    TAP_HEADER header;
};

/// endregion </Types>

class LoaderTAP
{
public:
    void reset();
    void start();
    void stop();
    int read();
    void close();

protected:
    void makeBlock(unsigned char *data, unsigned size, unsigned pilot_t,
              unsigned s1_t, unsigned s2_t, unsigned zero_t, unsigned one_t,
              unsigned pilot_len, unsigned pause, uint8_t last = 8);
    uint16_t findPulse(uint32_t t);
    void findTapeIndex();
    void findTapeSizes();

    void allocTapeBuffer();

protected:
    uint32_t tape_pulse[MAX_TAPE_PULSES];
    uint32_t max_pulses = 0;
    uint32_t tape_err = 0;

    uint8_t* tape_image = 0;
    uint32_t tape_imagesize = 0;

    int64_t _edge_change = 0x7FFF'FFFF'FFFF'FFFF;
    uint8_t* _playPointer = nullptr;
    uint8_t* _endOfTape = nullptr;
    uint32_t _index = 0;
    uint32_t _tapeBit = 0;
};

