#pragma once

#include <cstdint>
#include <string>
#include <vector>

class EmulatorContext;

/// SPG ("Spectrum Prog") snapshot loader - the TS-Conf SDK's program format
/// (zx-evo-docs Formats/SPGv1_0.txt, SPGv1_1.txt; TSConf technical-design
/// §3.17, hardware-spec §10).
///
/// Header (1024 bytes): 0x20 magic "SpectrumProg"; 0x2C version (0x10 = v1.0,
/// 0x11 = v1.1); 0x30 PC; 0x32 SP; 0x34 page at #C000; 0x35 [1:0] CPU clock,
/// [2] INT enabled; 0x3A number of blocks; 0x100 up to 256 block descriptors
/// of 3 bytes: [0] [4:0] offset in the page in 512-byte units, [7] last block;
/// [1] [4:0] size in the file in 512-byte units - 1, [7:6] compression
/// (0 none, 1 MegaLZ, 2 Hrust); [2] RAM page. The blocks follow the header.
///
/// Load defaults (the ancestor's and the format's): BASIC-48 ROM at #0000
/// (mapped mode, ROM128 = 1), RAM 5 / 2 / the header's page, IY = #5C3A,
/// HL' = #2758, I = #3F, IM 1, #7FFD = #10. The v1.1 picture fields and the
/// pager / resident addresses (installed by the SD loader of the shell) are
/// not used; v0.x files are refused.
///
/// The whole file is parsed and depacked before the machine is touched, so a
/// corrupt file changes nothing. Only the TS-Conf machine takes an SPG.
class LoaderSPG
{
public:
    struct Block
    {
        uint32_t address = 0;          ///< physical RAM byte address
        std::vector<uint8_t> data;     ///< depacked
        uint8_t compression = 0;
    };

    struct Image
    {
        uint8_t version = 0;
        uint16_t pc = 0;
        uint16_t sp = 0;
        uint8_t page3 = 0;
        uint8_t clock = 0;             ///< SYS_CONFIG[1:0]
        bool interrupts = false;
        std::vector<Block> blocks;
    };

    LoaderSPG(EmulatorContext* context, const std::string& path);
    LoaderSPG(EmulatorContext* context, std::vector<uint8_t> data, const std::string& name);

    bool load();
    const std::string& GetError() const { return _error; }
    const Image& GetImage() const { return _image; }

    /// Is this file an SPG we can load (the header only)? Opening one on another
    /// machine switches to the TS-Conf model first (the Qt window does, as for SZX)
    static bool Probe(const std::string& path, std::string& error);
    /// The model an SPG runs on (short name, RAM in KB)
    static constexpr const char* kModel = "TSL";
    static constexpr int kRamKb = 4096;

    /// Parse and depack an SPG image (no machine needed)
    static bool Parse(const std::vector<uint8_t>& data, Image& image, std::string& error);
    /// Apply a parsed image to a TS-Conf machine
    static bool Commit(EmulatorContext* context, const Image& image, std::string& error);

private:
    EmulatorContext* _context;
    std::string _path;
    std::vector<uint8_t> _data;
    bool _fromFile;
    Image _image;
    std::string _error;
};
