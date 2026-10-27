#pragma once

/// @file machinestatetransferlayouts.h
/// @brief What MachineStateTransfer knows about the machines with their own memory layout (ATM, Profi, Sprinter): how to read a
/// Spectrum 128K view out of them, how to put one into them, and how the ATM Turbo 2+ line moves its own pager state.
///
/// The state transfer is the emulator's internal mechanism and has its own rules: nothing here is shared with the snapshot
/// pipeline (owner rule 2026-10-07). Capability matrix: docs/features/automation.md.

#include <cstdint>
#include <string>

class EmulatorContext;

namespace statetransfer
{
/// Bank `bank` of the machine's Spectrum view: RAM page `bank`; on a machine with a state-transfer host (a Sprinter in a ZX mode) the page its
/// own mapping names
uint8_t* BankPage(EmulatorContext& context, uint16_t bank);

/// Is the live window map a Spectrum 128K: window 0 ROM, windows 1 and 2 RAM 5 and 2, window 3 the page #7FFD names?
bool IsSpectrum128Layout(EmulatorContext& context);

/// ATM Turbo 2+ line (ATM 7.10 -> ATM3 / ZX-Evo Base): move the pager's own state - the eight window registers with their ROM
/// selectors translated to the target's ROM image by the role of the page (the standard set is the last four pages of both), the
/// #xx77 latch, the palette, the font RAM. False + why when the source maps a ROM page the target has no equivalent for
bool MoveAtmTurboState(EmulatorContext& source, EmulatorContext& target, std::string& why);
/// The same check without writing
bool CanMoveAtmTurboState(EmulatorContext& source, EmulatorContext& target, std::string& why);
}  // namespace statetransfer
