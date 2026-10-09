#pragma once

/// @file loadernex.h
/// @brief The ZX Spectrum Next NEX loader: a self-contained program (the wiki's "NEX file format", versions V1.0 -
/// V1.3 sizes) put into RAM the way NextZXOS's nexload does: the 16K banks in the file order 5, 2, 0, 1, 3, 4, 6 ...,
/// the entry bank in slot 3, the border, SP and PC. The loading screens (a palette block and up to six screen blocks
/// before the banks) are read past, not shown: the program draws its own.

#include <cstdint>
#include <string>
#include <vector>

class EmulatorContext;
class NextBoard;

struct NexHeader
{
    std::string version;       ///< "V1.2"
    uint8_t ramRequired = 0;   ///< 0 = 768K, 1 = 1792K
    uint8_t bankCount = 0;
    uint8_t screenFlags = 0;
    uint8_t border = 0;
    uint16_t sp = 0;
    uint16_t pc = 0;
    uint8_t entryBank = 0;
    uint8_t keepRegisters = 0;  ///< byte 134: 1 = keep the NextREG state, 0 = reset the machine
    uint8_t screenFlags2 = 0;
    bool banksPresent[112] = {};
};

class LoaderNex
{
public:
    explicit LoaderNex(EmulatorContext* context) : _context(context) {}

    /// Parse the header of a NEX image; false with the reason in Error()
    bool Parse(const std::vector<uint8_t>& image, NexHeader& header);
    /// Put the program into the machine (a ZX Spectrum Next): memory, mapping, border, SP, PC
    bool Load(const std::vector<uint8_t>& image);
    bool LoadFile(const std::string& path);

    const NexHeader& Header() const { return _header; }
    const std::string& Error() const { return _error; }

private:
    static void ResetRegisters(NextBoard& board);
    bool Fail(const std::string& why);
    static size_t ScreenBytes(const NexHeader& header, bool& ok);

    EmulatorContext* _context;
    NexHeader _header;
    std::string _error;
};
