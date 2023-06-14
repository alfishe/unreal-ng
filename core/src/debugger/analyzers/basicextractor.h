#pragma once

#include "stdafx.h"

enum NumericFormat : uint8_t
{
    Integral = 0,
    FloatingPoint = 1
};

#pragma pack(push, 1) // Set struct packing to 1 byte alignment

struct NumericValue
{
    NumericFormat format = Integral;

    union Value
    {
        struct IntegralFormat
        {
            std::uint8_t start;    // Always 0
            std::uint8_t sign;     // 0 if the number is positive or 0xFF if the number is negative
            std::uint16_t number;  // Little-endian unsigned integer [0..65535]
            std::uint8_t end;      // Always 0
        } integral;

        struct FloatingPointFormat
        {
            std::uint8_t exponent;  // Exponent + 128 (0 -> e=-128, 255 -> e=127)
            std::uint32_t mantissa; // Big-endian mantissa
        } floatingPoint;
    };
};

#pragma pack(pop) // Restore the default struct packing


/// Extracts BASIC program and formats it as ASCII for display / analysis use
/// Inspired by https://github.com/FuseEmulator/fuse-emulator-svn/blob/master/fuse-utils/listbasic.c
class BasicExtractor
{
    /// region <Constants>
public:
// ZX Spectrum 48/128 BASIC Tokens
/// @see http://fileformats.archiveteam.org/wiki/Sinclair_BASIC_tokenized_file
    static constexpr const char* BasicTokens[] =
    {
        " SPECTRUM ",   // 0xA3
        " PLAY ",       // 0xA4
        "RND",          // 0xA5
        "INKEY$",       // 0xA6
        "PI",           // 0xA7
        "FN ",          // 0xA8
        "POINT ",       // 0xA9
        "SCREEN$ ",     // 0xAA
        "ATTR ",        // 0xAB
        "AT ",          // 0xAC
        "TAB ",         // 0xAD
        "VAL$ ",        // 0xAE
        "CODE ",        // 0xAF
        "VAL ",         // 0xB0
        "LEN ",         // 0xB1
        "SIN ",         // 0xB2
        "COS ",         // 0xB3
        "TAN ",         // 0xB4
        "ASN ",         // 0xB5
        "ACS ",         // 0xB6
        "ATN ",         // 0xB7
        "LN ",          // 0xB8
        "EXP ",         // 0xB9
        "INT ",         // 0xBA
        "SQR ",         // 0xBB
        "SGN ",         // 0xBC
        "ABS ",         // 0xBD
        "PEEK ",        // 0xBE
        "IN ",          // 0xBF
        "USR ",         // 0xC0
        "STR$ ",        // 0xC1
        "CHR$ ",        // 0xC2
        "NOT ",         // 0xC3
        "BIN ",         // 0xC4
        " OR ",         // 0xC5
        " AND ",        // 0xC6
        "<=",           // 0xC7
        ">=",           // 0xC8
        "<>",           // 0xC9
        " LINE ",       // 0xCA
        " THEN ",       // 0xCB
        " TO ",         // 0xCC
        " STEP ",       // 0xCD
        " DEF FN ",     // 0xCE
        " CAT ",        // 0xCF
        " FORMAT ",     // 0xD0
        " MOVE ",       // 0xD1
        " ERASE ",      // 0xD2
        " OPEN #",      // 0xD3
        " CLOSE #",     // 0xD4
        " MERGE ",      // 0xD5
        " VERIFY ",     // 0xD6
        " BEEP ",       // 0xD7
        " CIRCLE ",     // 0xD8
        " INK ",        // 0xD9
        " PAPER ",      // 0xDA
        " FLASH ",      // 0xDB
        " BRIGHT ",     // 0xDC
        " INVERSE ",    // 0xDD
        " OVER ",       // 0xDE
        " OUT ",        // 0xDF
        " LPRINT ",     // 0xE0
        " LLIST ",      // 0xE1
        " STOP ",       // 0xE2
        " READ ",       // 0xE3
        " DATA ",       // 0xE4
        " RESTORE ",    // 0xE5
        " NEW ",        // 0xE6
        " BORDER ",     // 0xE7
        " CONTINUE ",   // 0xE8
        " DIM ",        // 0xE9
        " REM ",        // 0xEA
        " FOR ",        // 0xEB
        " GO TO ",      // 0xEC
        " GO SUB ",     // 0xED
        " INPUT ",      // 0xEE
        " LOAD ",       // 0xEF
        " LIST ",       // 0xF0
        " LET ",        // 0xF1
        " PAUSE ",      // 0xF2
        " NEXT ",       // 0xF3
        " POKE ",       // 0xF4
        " PRINT ",      // 0xF5
        " PLOT ",       // 0xF6
        " RUN ",        // 0xF7
        " SAVE ",       // 0xF8
        " RANDOMIZE ",  // 0xF9
        " IF ",         // 0xFA
        " CLS ",        // 0xFB
        " DRAW ",       // 0xFC
        " CLEAR ",      // 0xFD
        " RETURN ",     // 0xFE
        " COPY "        // 0xFF
    };
    /// endregion </Constants>

public:
    std::string extractBasic(uint8_t* data, size_t len);

protected:
    void detokenize();
    static std::string printNumeric(NumericValue value);
};
