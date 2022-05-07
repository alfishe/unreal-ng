#pragma once
#include "stdafx.h"

#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"

/// region <Info>

/// See: https://worldofspectrum.org/faq/reference/z80format.htm
/// See: https://k1.spdns.de/Develop/Projects/zasm/Info/z80format.htm
/// Glossary:
///     M.G.T. - Miles Gordon Technology. British company specializing in high-quality add-ons for ZX-Spectrum. More: https://en.wikipedia.org/wiki/Miles_Gordon_Technology
///     IF1 = Interface 1 - More: https://en.wikipedia.org/wiki/ZX_Interface_1

/// endregion </Info>

/// region <Types>

struct Z80Header_v1
{
    uint8_t reg_A;
    uint8_t reg_F;
    uint16_t reg_BC;
    uint16_t reg_HL;
    uint16_t reg_PC;
    uint16_t reg_SP;
    uint8_t reg_I;
    uint8_t flags1;
    uint16_t reg_DE;
    uint16_t reg_BC1;
    uint16_t reg_DE1;
    uint16_t reg_HL1;
    uint8_t reg_A1;
    uint8_t reg_F1;
    uint16_t reg_IX;
    uint16_t reg_IY;
    uint8_t int_flags;
    uint8_t IFF2;
    uint8_t flags2;
};

enum Z80_Models_v2 : uint8_t
{
    Z80_MODEL2_48K      = 0,    // Genuine ZX-Spectrum 48k
    Z80_MODEL2_48K_IF1  = 1,    // ZX-Spectrum 48k + Interface 1
    Z80_MODEL2_SAMRAM   = 2,    // SamRam
    Z80_MODEL2_128K     = 3,    // Genuine ZX-Spectrum 128k
    Z80_MODEL2_128K_IF1 = 4,    // ZX-Spectrum 128k + Interface 1
    Z80_MODEL2_UNUSED1  = 5,
    Z80_MODEL2_UNUSED2  = 6
};

enum Z80_Models_v3 : uint8_t
{
    Z80_MODEL3_48K      = 0,    // Genuine ZX-Spectrum 48k
    Z80_MODEL3_48K_IF1  = 1,    // ZX-Spectrum 48k + Interface 1
    Z80_MODEL3_SAMRAM   = 2,    // SamRam
    Z80_MODEL3_48K_MGT  = 3,    // ZX-Spectrum 48k + M.G.T magic button
    Z80_MODEL3_128K     = 4,    // Genuine ZX-Spectrum 128k
    Z80_MODEL3_128K_IF1 = 5,    // ZX-Spectrum 128k + Interface 1
    Z80_MODEL3_128K_MGT = 6,    // ZX-Spectrum 128k + M.G.T magic button

    // Extended model list
    Z80_MODEL3_128K_3   = 7,    // Spectrum +3
    Z80_MODEL3_128k_3_1 = 8,    // Same Spectrum +3
    Z80_MODEL3_P128K    = 9,    // Pentagon 128K
    Z80_MODEL3_ZS256K   = 10,   // Scorpion ZS 256K
    Z80_MODEL3_DIDAKTIK = 11,   // Didaktik-Kompakt
    Z80_MODEL3_128K_2   = 12,   // Spectrum +2
    Z80_MODEL3_128K_2A  = 13,   // Spectrum +2A
    Z80_MODEL3_TC2048   = 14,   // Timex TC2048
    Z80_MODEL3_TC2068   = 15,   // Timex TC2068

    Z80_MODEL3_TS2068   = 128,  // Timex TS2068
};

struct Z80Header_v2 : public Z80Header_v1
{
    uint16_t extra_header_len;
    uint16_t new_PC;            // Z80Header_v1.PC = 0 for v2 and v3. PC value stored here
    Z80_Models_v2 hardware;     // Hardware platform

};


/// endregion </Types>

class LoaderZ80
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_LOADER;
    const uint16_t _SUBMODULE = PlatformLoaderSubmodulesEnum::SUBMODULE_LOADER_Z80;
    /// endregion </ModuleLogger definitions for Module/Submodule>

protected:
    EmulatorContext* _context;
    ModuleLogger* _logger = nullptr;

public:
    LoaderZ80(EmulatorContext* context);
    virtual ~LoaderZ80();

public:
    bool load(std::string& filepath);
    bool loadFromMemory(void* buffer, size_t len);

    bool validate(std::string& filepath);
    bool stageLoad(std::string& filepath);
    void commitFromStage();

    /// region <Helper methods>
protected:
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
    void compressBlock(uint8_t* buffer, size_t len, uint8_t* outBuffer, size_t outLen, size_t* resultLen);
    void decompressBlock(uint8_t* buffer, size_t len, uint8_t* outBuffer, size_t outLen, size_t* resultLen);
    /// endregion </Helper methods>

    /// region <Debug methods>
public:
    std::string dumpSnapshotInfo();
    /// endregion </Debug methods>
};
