#pragma once

/// @file memorywaitoverlay.h
/// @brief A machine's wait states on memory accesses (PLAN #60(d)): turbo RAM
/// that stretches every access (the Sprinter at 21 MHz), a DRAM arbiter or a
/// cache miss (TSConf at 14 MHz).
///
/// Built on the host bus overlay (hostbusoverlay.h), so a machine without
/// waits pays nothing: the Z80 uses the overlay memory interfaces only while
/// an overlay is installed. The machine installs its wait overlay with
/// Core::AddBusOverlay, marks the 16 KB slots whose accesses wait whenever its
/// banks change (SetSlotWaits), and supplies the rule (ExtraClocks). The rule
/// lives in one function, so a measured model can replace a first guess.
///
/// Timing: the overlay runs after the normal access, so the wait follows the
/// byte transfer instead of preceding it. The instruction's length and every
/// later T-state are exact; only the position of the byte inside the stretched
/// cycle differs, which nothing that reads the bus mid-instruction depends on
/// for these machines. The rule gets the CPU clock the access started at
/// (Z80::AccessStartClock), so phase-dependent rules (the Sprinter's
/// `t mod 6`) see the true phase.
///
/// Port waits need no hook: the machine's port decoder handles every port
/// access itself and calls Z80::AddWaitStates (TSConf's IDE stall,
/// hardware-spec §8.3).
///
/// Worked example (a rule "3 clocks on slot 2"): SetSlotWaits(2, true);
/// LD A,(#8000) then takes 13 + 3 T, LD A,(#4000) stays 13 T.

#include <cstdint>

#include "emulator/memory/hostbusoverlay.h"

class Z80;

enum class MemoryWaitAccess : uint8_t
{
    Code,   ///< opcode or operand fetch (Memory read with isExecution)
    Read,   ///< data read
    Write,  ///< data write
};

class MemoryWaitOverlay : public HostBusOverlay
{
public:
    explicit MemoryWaitOverlay(Z80* cpu) : _cpu(cpu) {}

    /// Accesses in `slot` (0-3: #0000, #4000, #8000, #C000) wait. Set by the
    /// machine when its banks change; all clear by default
    void SetSlotWaits(uint8_t slot, bool waits) { _slotWaits[slot & 0x03] = waits; }
    bool SlotWaits(uint8_t slot) const { return _slotWaits[slot & 0x03]; }

    /// The machine's rule: extra CPU clocks (at the current clock rate) for an
    /// access of `kind` to `addr` that started at CPU clock `startClock` of
    /// the frame. Called only for slots marked by SetSlotWaits
    virtual uint32_t ExtraClocks(MemoryWaitAccess kind, uint16_t addr, uint32_t startClock) = 0;

    uint8_t onRead(uint16_t addr, uint8_t normal, bool isExecution, bool romPaged) final;
    void onWrite(uint16_t addr, uint8_t value, bool romPaged) final;

protected:
    Z80* _cpu = nullptr;

private:
    void Wait(MemoryWaitAccess kind, uint16_t addr);

    bool _slotWaits[4] = {false, false, false, false};
};
