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
