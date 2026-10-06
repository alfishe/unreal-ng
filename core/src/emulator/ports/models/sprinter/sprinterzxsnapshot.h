#pragma once

/// @file sprinterzxsnapshot.h
/// @brief The Sprinter's snapshot commit policy (snapshot pipeline P4 = Sprinter phase Z5, PLAN #84): a ZX Spectrum
/// snapshot (SNA, Z80, SZX) goes into the running Spectrum mode through the PLD's cell table, never into the physical
/// pages 0-7, which are the Sprinter's own system pages.
///
/// Design: docs/inprogress/2026-09-28-sprinter/tdd-zx-mode.md section 3.5.
///
/// Worked example: P128 mode on a 4 MB board, cells #F0-#F7 = #9F, #9E, ..., #98 (allocated by the BIOS). action.sna holds
/// Spectrum bank 3 in its extra-page list: it is written to physical page #9C. #7FFD = #14 from the header: window 3
/// shows cell #F4, PC = #D055. The Spectrum screen shadow in video RAM follows bank 5 and bank 7 as it follows a CPU's
/// writes. The machine is NOT reset (a reset would leave the Spectrum mode for the DSS prompt).

#include "loaders/snapshot/snapshotcapture.h"
#include "loaders/snapshot/snapshotpolicy.h"

class SprinterZxSnapshot : public snapshot::ISnapshotCommitPolicy
{
public:
    /// The one instance every Sprinter shares (stateless); also registered under its name for `commit=sprinter-zx`
    static SprinterZxSnapshot& Instance();

    std::string Name() const override { return "sprinter-zx"; }
    snapshot::Verdict Examine(const snapshot::Image& image, EmulatorContext& context) const override;
    bool Commit(const snapshot::Image& image, EmulatorContext& context, snapshot::Report& report) override;
};

/// The save side (P6): the Sprinter shows a snapshot the Spectrum machine its mode launcher made it. Banks 0-7 (Scorpion: 0-15) are read
/// through the PLD cells #F0-#F7 (#F8-#FF) in the order the Spectrum numbers them, so the file restores on any machine of that kind
/// and back on a Sprinter. The machine the file names follows the launcher's mode: SP.ZX / SPRINTER.ZX / ORIGIN.ZX give a 128K,
/// P128.ZX / PENT128.ZX a Pentagon 128, SC256.ZX / SCORPION.ZX a Scorpion; a mode without #7FFD paging is a 48K. Outside a ZX mode (DSS,
/// the BIOS) and in the 512 KB modes there is no view
class SprinterZxCapture : public snapshot::ISnapshotCapturePolicy
{
public:
    static SprinterZxCapture& Instance();

    std::string Name() const override { return "sprinter-zx"; }
    snapshot::MachineView Examine(EmulatorContext& context) const override;

    /// What a mode's snapshot says the machine is, from the launcher's mode name ("Pentagon 128", "Sprinter ZX", "Scorpion 256",
    /// "Original ZX Spectrum", ...) and whether the mode has #7FFD paging. An unknown name is a 128K
    struct Identity
    {
        MEM_MODEL model = MM_SPECTRUM128;
        uint32_t ramKb = 128;
        std::string machineHint = "128k";
        std::string timingHint = "128k";
        bool layout48 = false;
        bool scorpion = false;
        uint16_t bankCount = 8;
    };
    static Identity IdentityOf(const std::string& modeName, bool paging7ffd);
};
