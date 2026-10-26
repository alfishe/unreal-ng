#pragma once

/// @file loadernex.h
/// @brief The ZX Spectrum Next NEX loader: a self-contained program (the wiki's "NEX file format", versions V1.0 -
/// V1.3 sizes) put into RAM the way NextZXOS's nexload does: the 16K banks in the file order 5, 2, 0, 1, 3, 4, 6 ...,
/// the entry bank in slot 3, the border, SP and PC. The loading screens (a palette block and up to six screen blocks
/// before the banks) are put on the display as nexload does; V1.3 adds the tilemap screen (byte 152 = 3: the NextREG values of
/// bytes 154-157), a 2048-byte copper block (byte 153) and the first bank's file offset (bytes 144-147), which a loader that knows
/// the version uses instead of summing the block sizes. NEXLOAD of the NextZXOS distribution refuses V1.3 ("Please update to the
/// latest .nexload version"); the V1.3 files of ped7g's NEXLOAD2 run through here.

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
    bool hasCopperCode = false;      ///< V1.3 byte 153: a 2048-byte copper block follows the last screen
    uint8_t tilemapConfig[4] = {};   ///< V1.3 bytes 154-157: NextREG #6B, #6C, #6E, #6F for the tilemap screen (byte 152 = 3)
    uint32_t banksOffset = 0;        ///< V1.3 bytes 144-147: where the first bank starts (0 in V1.0 - V1.2)
    bool banksPresent[112] = {};
};

class LoaderNex
{
public:
    static constexpr const char* kModel = "NEXT";  ///< the machine a NEX runs on
    static constexpr int kRamKb = 2048;
    explicit LoaderNex(EmulatorContext* context) : _context(context) {}

    /// Parse the header of a NEX image; false with the reason in Error()
    bool Parse(const std::vector<uint8_t>& image, NexHeader& header);
    /// Put the program into the machine (a ZX Spectrum Next): memory, mapping, border, SP, PC
    bool Load(const std::vector<uint8_t>& image);
    bool LoadFile(const std::string& path);
    /// nexload's register reset (palettes, clips, transparency, priorities, 28 MHz): also the state the boot leaves for the
    /// real-board test programs
    static void ResetRegisters(NextBoard& board);
    /// The ULA palette as the firmware fills it: the 16 default colours repeated through all 256 entries
    static void FillUlaPalette(NextBoard& board);

    /// The running Next as a V1.2 NEX (the save side): every RAM bank the file can name (0-111) that is not all zero, banks 5
    /// and 2 always, the bank in slots 6-7 as the entry bank, SP, PC, the border, "keep the NextREG state" (byte 134). A NEX
    /// starts a program, it does not restore a machine: what it cannot carry goes into `warnings`. False with `error` on
    /// another machine or an MMU the loader cannot rebuild at #C000
    /// Whether Capture would succeed (the machine and the slots 6-7 check, no copy): false with `reason`
    static bool Capturable(EmulatorContext& context, std::string& reason);
    /// What every .nex leaves out of the machine (the note of the save formats)
    static const char* kLeftOut;
    static bool Capture(EmulatorContext& context, std::vector<uint8_t>& out, std::vector<std::string>& warnings, std::string& error);
    static bool SaveFile(EmulatorContext& context, const std::string& path, std::vector<std::string>& warnings, std::string& error);

    const NexHeader& Header() const { return _header; }
    const std::string& Error() const { return _error; }

private:
    bool Fail(const std::string& why);
    static size_t ScreenBytes(const NexHeader& header, bool& ok);

    EmulatorContext* _context;
    NexHeader _header;
    std::string _error;
};
