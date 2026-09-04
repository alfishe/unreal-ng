#pragma once

#include "stdafx.h"

enum NumericFormat : uint8_t
{
    Integral = 0,
    FloatingPoint = 1
};

#pragma pack(push, 1)  // Set struct packing to 1 byte alignment

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
            std::uint8_t exponent;   // Exponent + 128 (0 -> e=-128, 255 -> e=127)
            std::uint32_t mantissa;  // Big-endian mantissa
        } floatingPoint;
    };
};

#pragma pack(pop)  // Restore the default struct packing

class Memory;

/// One decoded listing line (r9 structured extraction for the Tape Manager
/// popup listing). `text` follows the same token and spacing rules as
/// extractBasic; `leadingSpace` records whether the number-to-text separator
/// was emitted for this line.
struct BasicLine
{
    std::uint16_t lineNumber = 0;    // as stored in the header (big-endian in the body)
    std::size_t startOffset = 0;     // byte offset of the line header within the source buffer
    std::size_t endOffset = 0;       // one past the last byte consumed for this line
    bool leadingSpace = true;   // raw first byte did not absorb the separator
    bool variablesArea = false; // number > MaxLineNumber → saved-program vars tail
    std::string text;           // detokenized statement text (no number, no newline)
};

/// Result of the structured walk. A SAVE'd BASIC program carries its
/// variables area after the listing; var-name bytes (single-letter vars are
/// 0xA1+, multi-letter names 0x41+) decode as huge "line numbers" (e.g.
/// 0xEE00 = 60928), which is how the tail is detected and delimited.
struct BasicListing
{
    std::vector<BasicLine> lines;  // includes the trailing variables-area line when present
    size_t programEndOffset = 0;   // offset where the program proper ends (buffer end when no vars)
    size_t variablesBytes = 0;     // bytes from programEndOffset to the buffer end
};

struct BasicExtractor
{
    /// region <Constants>
public:
    /// Highest line number the ZX editor accepts — anything above is not a
    /// line number but the variables-area tail of a SAVE'd program
    static constexpr std::uint16_t MaxLineNumber = 9999;

    /// ZX Spectrum 48/128 BASIC Tokens
    /// @see http://fileformats.archiveteam.org/wiki/Sinclair_BASIC_tokenized_file
    static constexpr const char* BasicTokens[] = {
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

    /// Structured variant of extractBasic: one BasicLine per [number|len|text]
    /// header, with the variables-area tail detected and delimited. The
    /// extractBasic output is exactly the join of `lines`
    /// (number + separator + text + '\n').
    BasicListing extractBasicLines(uint8_t* data, size_t len);

    /// Extracts BASIC program from the provided Memory instance using system variables PROG and VARS
    /// @param memory Pointer to the emulator memory instance
    std::string extractFromMemory(Memory* memory);

protected:
    void detokenize();
    static std::string printNumeric(NumericValue value);
};
