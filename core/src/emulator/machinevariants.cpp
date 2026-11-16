#include "stdafx.h"

#include "machinevariants.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "emulator/io/z80n/nextboardissue.h"
#include "emulator/platform.h"

namespace
{

/// TS-Conf with the TS-Labs VDAC2 card (FT812) on its IDE connector: the
/// firmware's VDAC2 build (STATUS VDAC_VER = 7, BLT2) and no IDE board, the card
/// takes the connector (docs/inprogress/2026-10-01-tsconf-vdac2/vdac2-integration-design.md §3)
void ApplyTsConfVdac2(CONFIG& config)
{
    config.ts_vdac = 7;
    config.ide_scheme = IDE_NONE;
}

bool IsTsConfVdac2(const CONFIG& config)
{
    return config.mem_model == MM_TSL && config.ts_vdac == 7;
}

bool TsConfVdac2Supported(std::string* reason)
{
#ifdef ENABLE_VDAC2
    (void)reason;
    return true;
#else
    if (reason)
        *reason = "this build has no VDAC2 support (CMake ENABLE_VDAC2=OFF)";
    return false;
#endif
}

/// ZX Profi+ ("Personal Computer PROFI Plus"): a v5 with Djoni's V0.03 port decoder PROM, which opens the extended
/// ports to the SYS ROM, running Vadim's ROM BIOS Plus - the machine PQ-DOS and DOS Navigator are written for
/// (docs/inprogress/2026-10-04-profi-plus/design.md)
constexpr const char* kProfiPlusRom = "rom\\profi\\bios-plus-041h1.rom";

void ApplyProfiPlus(CONFIG& config)
{
    std::snprintf(config.profi_rom_path, sizeof(config.profi_rom_path), "%s", kProfiPlusRom);
    config.profi_ext_ports = 2;
}

bool IsProfiPlus(const CONFIG& config)
{
    return config.mem_model == MM_PROFI && config.profi_ext_ports == 2 &&
           std::string(config.profi_rom_path).find("bios-plus") != std::string::npos;
}

bool ProfiPlusSupported(std::string* reason)
{
    (void)reason;
    return true;
}

/// The ZX Spectrum Next on its other boards (research-variants.md; NEXT itself is the KS2, Issue 4): the KS1's Issue 2 (Spartan-6:
/// two video timings, no DNA / XADC) with its 1 MB, the KS3's Issue 5 (Artix-7, NR #04 drives SRAM A21)
void ApplyNextKs1(CONFIG& config)
{
    config.next_board_issue = static_cast<uint8_t>(NextBoardIssue::Issue2);
}

bool IsNextKs1(const CONFIG& config)
{
    return config.mem_model == MM_NEXT && config.next_board_issue == static_cast<uint8_t>(NextBoardIssue::Issue2);
}

void ApplyNextKs3(CONFIG& config)
{
    config.next_board_issue = static_cast<uint8_t>(NextBoardIssue::Issue5);
}

bool IsNextKs3(const CONFIG& config)
{
    return config.mem_model == MM_NEXT && config.next_board_issue == static_cast<uint8_t>(NextBoardIssue::Issue5);
}

/// The Next with the zxnDDMA core (3.03.00): the blitter commands of the DMA on port #6B, on the KS2 board (Issue 4)
void ApplyNextDdma(CONFIG& config)
{
    config.next_zxnddma = 1;
}

bool IsNextDdma(const CONFIG& config)
{
    return config.mem_model == MM_NEXT && config.next_zxnddma != 0;
}

bool NextBoardSupported(std::string* reason)
{
    (void)reason;
    return true;
}

/// A 48K with the Technology Research Beta 128 disk interface on its edge connector: the interface's own TR-DOS 5.03
/// ROM (the version the 48K Beta 128 shipped with) pages in on an M1 at #3Dxx and out on an M1 at #4000 and above;
/// the WD1793 #1F #3F #5F #7F and the system register #FF answer only while it is paged in. No #7FFD
/// (.recipe/machines/48k-beta128.md)
constexpr const char* kBeta128TrDosRom = "rom\\trdos503.rom";

void Apply48kBeta128(CONFIG& config)
{
    config.trdos_present = true;
    std::snprintf(config.dos_rom_path, sizeof(config.dos_rom_path), "%s", kBeta128TrDosRom);
}

bool Is48kBeta128(const CONFIG& config)
{
    return config.mem_model == MM_SPECTRUM48 && config.trdos_present && config.dos_rom_path[0] != '\0';
}

bool Beta128Supported(std::string* reason)
{
    (void)reason;
    return true;
}

const std::vector<MachineVariant> kVariants = {
    {"48K-BETA128", "ZX-Spectrum 48k + Beta 128 (TR-DOS 5.03)",
     "ZX-Spectrum 48k with the Beta 128 disk interface: TR-DOS 5.03 paged in at #3Dxx, 4 floppy drives, no #7FFD",
     "48K", 48, Apply48kBeta128, Is48kBeta128, Beta128Supported},
    {"TSL-VDAC2", "TS-Conf + VDAC2 (FT812)",
     "TS-Conf with the TS-Labs VDAC2 card: FT812 graphics on the IDE connector (no IDE)", "TSL", 4096,
     ApplyTsConfVdac2, IsTsConfVdac2, TsConfVdac2Supported},
    {"PROFI-PLUS", "ZX Profi+ (BIOS Plus, PQ-DOS)",
     "ZX Profi v5 with the V0.03 port decoder and ROM BIOS Plus 0.41h1: PQ-DOS, DOS Navigator, IDE", "PROFI", 1024,
     ApplyProfiPlus, IsProfiPlus, ProfiPlusSupported},
    {"NEXT-KS1", "ZX Spectrum Next KS1 (Issue 2)",
     "ZX Spectrum Next on the Kickstarter 1 board: Spartan-6, video timings VGA 0 / 1 only, no DNA / XADC, 1 MB", "NEXT", 1024,
     ApplyNextKs1, IsNextKs1, NextBoardSupported},
    {"NEXT-KS3", "ZX Spectrum Next KS3 (Issue 5)",
     "ZX Spectrum Next on the Kickstarter 3 board: Artix-7, NR #04 reaches SRAM A21 in config mode", "NEXT", 2048,
     ApplyNextKs3, IsNextKs3, NextBoardSupported},
    {"NEXT-DDMA", "ZX Next 2048 DDMA",
     "ZX Spectrum Next 2 MB with the zxnDDMA core (3.02.16+ / 3.03.00): the DMA blitter - physical 22-bit addresses, 2D copies, transparent, fill, Layer 2 X,Y",
     "NEXT", 2048, ApplyNextDdma, IsNextDdma, NextBoardSupported},
};

/// Other names a create request may use for a variant
struct Alias
{
    const char* alias;
    const char* name;
};
const Alias kAliases[] = {
    {"48K-TRDOS", "48K-BETA128"},
    {"48K-BETA", "48K-BETA128"},
    {"TSCONF-VDAC2", "TSL-VDAC2"},
    {"PROFIPLUS", "PROFI-PLUS"},
    {"NEXT-ISSUE2", "NEXT-KS1"},
    {"NEXT-ISSUE5", "NEXT-KS3"},
};

bool SameName(const std::string& a, const char* b)
{
    const std::string other = b;
    return a.size() == other.size() &&
           std::equal(a.begin(), a.end(), other.begin(), [](char x, char y) {
               return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
           });
}

} // namespace

namespace MachineVariants
{

const std::vector<MachineVariant>& All()
{
    return kVariants;
}

const MachineVariant* Find(const std::string& name)
{
    std::string resolved = name;
    for (const Alias& alias : kAliases)
        if (SameName(name, alias.alias))
            resolved = alias.name;
    for (const MachineVariant& variant : kVariants)
        if (SameName(resolved, variant.name))
            return &variant;
    return nullptr;
}

const MachineVariant* Of(const CONFIG& config)
{
    for (const MachineVariant& variant : kVariants)
        if (variant.matches(config))
            return &variant;
    return nullptr;
}

} // namespace MachineVariants
