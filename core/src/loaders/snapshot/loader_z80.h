#pragma once

#include <emulator/cpu/z80.h>
#include <emulator/memory/memory.h>

#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "stdafx.h"

/// region <Info>

/// See: https://worldofspectrum.org/faq/reference/z80format.htm
/// See: https://k1.spdns.de/Develop/Projects/zasm/Info/z80format.htm
/// See: http://rk.nvg.ntnu.no/sinclair/formats/z80-format.html
/// Glossary:
///     M.G.T. - Miles Gordon Technology. British company specializing in high-quality add-ons for ZX-Spectrum. More:
///     https://en.wikipedia.org/wiki/Miles_Gordon_Technology IF1 = Interface 1 - More:
///     https://en.wikipedia.org/wiki/ZX_Interface_1

/// endregion </Info>

/// region <Types>

enum Z80SnapshotVersion : uint8_t
{
    Unknown = 0,
    Z80v1 = 1,
    Z80v2 = 2,
    Z80v3 = 3
};

enum Z80MemoryMode : uint8_t
{
    Z80_48K = 0,
    Z80_128K = 1,
    Z80_SAMCOUPE = 2,
    Z80_256K = 3
};

#pragma pack(push, 1)

struct Z80Header_v1
{
    uint8_t reg_A;
    uint8_t reg_F;
    uint16_t reg_BC;
    uint16_t reg_HL;
    uint16_t reg_PC;
    uint16_t reg_SP;
    uint8_t reg_I;
    uint8_t reg_R;
    uint8_t flags;
    uint16_t reg_DE;
    uint16_t reg_BC1;
    uint16_t reg_DE1;
    uint16_t reg_HL1;
    uint8_t reg_A1;
    uint8_t reg_F1;
    uint16_t reg_IY;
    uint16_t reg_IX;
    uint8_t IFF1;
    uint8_t IFF2;
    uint8_t im;
};

enum Z80_Models_v2 : uint8_t
{
    Z80_MODEL2_48K = 0,       // Genuine ZX-Spectrum 48k
    Z80_MODEL2_48K_IF1 = 1,   // ZX-Spectrum 48k + Interface 1
    Z80_MODEL2_SAMRAM = 2,    // SamRam
    Z80_MODEL2_128K = 3,      // Genuine ZX-Spectrum 128k
    Z80_MODEL2_128K_IF1 = 4,  // ZX-Spectrum 128k + Interface 1
    Z80_MODEL2_UNUSED1 = 5,
    Z80_MODEL2_UNUSED2 = 6,

    Z80_MODEL2_128K_3 = 7,     // Spectrum +3
    Z80_MODEL2_128k_3_1 = 8,   // Same Spectrum +3
    Z80_MODEL2_P128K = 9,      // Pentagon 128K
    Z80_MODEL2_ZS256K = 10,    // Scorpion ZS 256K
    Z80_MODEL2_DIDAKTIK = 11,  // Didaktik-Kompakt
    Z80_MODEL2_128K_2 = 12,    // Spectrum +2
    Z80_MODEL2_128K_2A = 13,   // Spectrum +2A
};

enum Z80_Models_v3 : uint8_t
{
    Z80_MODEL3_48K = 0,       // Genuine ZX-Spectrum 48k
    Z80_MODEL3_48K_IF1 = 1,   // ZX-Spectrum 48k + Interface 1
    Z80_MODEL3_SAMRAM = 2,    // SamRam
    Z80_MODEL3_48K_MGT = 3,   // ZX-Spectrum 48k + M.G.T magic button
    Z80_MODEL3_128K = 4,      // Genuine ZX-Spectrum 128k
    Z80_MODEL3_128K_IF1 = 5,  // ZX-Spectrum 128k + Interface 1
    Z80_MODEL3_128K_MGT = 6,  // ZX-Spectrum 128k + M.G.T magic button

    // Extended model list
    Z80_MODEL3_128K_3 = 7,     // Spectrum +3
    Z80_MODEL3_128k_3_1 = 8,   // Same Spectrum +3
    Z80_MODEL3_P128K = 9,      // Pentagon 128K
    Z80_MODEL3_ZS256K = 10,    // Scorpion ZS 256K
    Z80_MODEL3_DIDAKTIK = 11,  // Didaktik-Kompakt
    Z80_MODEL3_128K_2 = 12,    // Spectrum +2
    Z80_MODEL3_128K_2A = 13,   // Spectrum +2A
    Z80_MODEL3_TC2048 = 14,    // Timex TC2048
    Z80_MODEL3_TC2068 = 15,    // Timex TC2068

    Z80_MODEL3_TS2068 = 128,  // Timex TS2068
};

struct Z80Header_v2 : public Z80Header_v1
{
    uint16_t extendedHeaderLen;
    uint16_t newPC;       // Z80Header_v1.PC = 0 for v2 and v3. PC value stored here
    Z80_Models_v2 model;  // Hardware platform
    uint8_t p7FFD;
    uint8_t r1;
    uint8_t r2;
    uint8_t pFFFD;   // Last selected AY/YM register (OUT 0xFFFD)
    uint8_t ay[16];  // AY/YM PSG registers snapshot
};

struct Z80Header_v3 : public Z80Header_v2
{
    uint16_t lowTCounter;         // [55]
    uint8_t highTCounter;         // [57]
    uint8_t qlFlag;               // [58]
    uint8_t mgtROM;               // [59]
    uint8_t multifaceROM;         // [60]
    uint8_t isROM1;               // [61]
    uint8_t isROM2;               // [62]
    uint8_t joystickMapping[10];  // [63]
    uint8_t keyMappings[10];      // [73]
    uint8_t mgtType;              // [83]
    uint8_t inhibitButtonStatus;  // [84]
    uint8_t inhibitFlag;          // [85]
    uint8_t p1FFD;                // [86]
};

struct MemoryBlockDescriptor
{
    uint16_t compressedSize;
    uint8_t memoryPage;
};

#pragma pack(pop)

/// endregion </Types>

class LoaderZ80
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_LOADER;
    const uint16_t _SUBMODULE = PlatformLoaderSubmodulesEnum::SUBMODULE_LOADER_Z80;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
    EmulatorContext* _context;
    ModuleLogger* _logger = nullptr;

    std::string _path;
    FILE* _file = nullptr;
    bool _fileValidated = false;
    size_t _fileSize = 0;
    bool _stagingLoaded = false;

    Z80SnapshotVersion _snapshotVersion = Unknown;

    uint8_t* _stagingROMPages[MAX_ROM_PAGES] = {};
    uint8_t* _stagingRAMPages[MAX_RAM_PAGES] = {};
    Z80MemoryMode _memoryMode = Z80_48K;
    Z80Registers _z80Registers = {};
    uint8_t _port7FFD = 0x00;
    uint8_t _portFFFD = 0x00;
    uint8_t _borderColor = 0x00;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    LoaderZ80(EmulatorContext* context, const std::string& path);
    virtual ~LoaderZ80();
    /// endregion </Constructors / destructors>

public:
    bool load();

protected:
    bool validate();
    bool stageLoad();
    void commitFromStage();

    /// region <Helper methods>
protected:
    Z80SnapshotVersion getSnapshotFileVersion();
    bool loadZ80v1();
    bool loadZ80v2();
    bool loadZ80v3();

    Z80Registers getZ80Registers(const Z80Header_v1& header, uint16_t pc);
    Z80MemoryMode getMemoryMode(const Z80Header_v2& header);
    void applyPeripheralState(const Z80Header_v2& header);

    // Compression is used when Z80Header_v1.flags1:Bit 5 is set
    // The compression method is very simple:
    //      it replaces repetitions of at least five equal bytes by a four-byte code ED ED xx yy,
    //      which stands for "byte yy repeated xx times".
    // Only sequences of length at least 5 are coded.
    // The exception is sequences consisting of ED's;
    // if they are encountered, even two ED's are encoded into ED ED 02 ED.
    //
    // Finally, every byte directly following a single ED is not taken into a block,
    // for example ED 6*00 is not encoded into ED ED ED 06 00 but into ED 00 ED ED 05 00.
    // The block is terminated by an end marker, 00 ED ED 00.
    // @see https://sinclair.wiki.zxnet.co.uk/wiki/Z80_format
    // @see https://worldofspectrum.org/faq/reference/z80format.htm
    void compressPage(uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen);

public:
    // Exposed for benchmarking comparison
    void decompressPage(uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen);

    /// @brief Optimized decompression using memset for RLE sequences
    void decompressPage_Optimized(uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen);

    /// @brief Original decompression (for benchmarking comparison)
    void decompressPage_Original(uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstLen);

protected:
    MemoryPageDescriptor resolveSnapshotPage(uint8_t page, Z80MemoryMode mode);

    void freeStagingMemory();

    /// endregion </Helper methods>

    /// region <Debug methods>
public:
    std::string dumpSnapshotInfo();
    std::string dumpSnapshotMemoryInfo();
    /// endregion </Debug methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing /
// benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class LoaderZ80CUT : public LoaderZ80
{
public:
    LoaderZ80CUT(EmulatorContext* context, std::string path) : LoaderZ80(context, path) {};

public:
    using LoaderZ80::_context;
    using LoaderZ80::_file;
    using LoaderZ80::_fileSize;
    using LoaderZ80::_fileValidated;
    using LoaderZ80::_logger;
    using LoaderZ80::_path;
    using LoaderZ80::_stagingLoaded;

    using LoaderZ80::commitFromStage;
    using LoaderZ80::stageLoad;
    using LoaderZ80::validate;
};
#endif  // _CODE_UNDER_TEST