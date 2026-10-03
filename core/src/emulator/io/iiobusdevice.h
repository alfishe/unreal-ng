#pragma once

/// @file iiobusdevice.h
/// @brief A device on a bus that is not the Z80's own port funnel: an ISA slot (the Sprinter), the ATM Turbo 2+
/// INTERNAL I/O connector, later the shared ZX-bus of PLAN #82 (network tdd §5.1; the first concrete piece of the
/// "machine -> buses -> slots -> devices" model).
///
/// Addresses are the bus's own: an ISA I/O address (20 bits), the ATM connector's #FB latch value. The device says
/// which of them it answers (Decodes) and turns them into its register offset; the bus wrapper does nothing but
/// route. Every call runs on the emulator thread.
///
/// Worked example: the NE2000 at ISA base #300 answers `Decodes(#30A, offset)` with offset #0A; the Sprinter's
/// slot wrapper then calls Read(#0A) for a CPU read of #C30A.

#include <cstdint>
#include <functional>
#include <string>

#include "emulator/state/statenode.h"

class IIoBusDevice
{
public:
    virtual ~IIoBusDevice() = default;

    /// "ne2000", "atm2ioesp", ...
    virtual const char* Kind() const = 0;

    /// Whether the device answers this bus address, and at which register offset
    virtual bool Decodes(uint32_t address, uint16_t& offset) const = 0;
    /// The card's decoder does not look at AEN (ISA): it answers DMA-flagged cycles too (the SprinterESP's 74HC30)
    virtual bool IgnoresAen() const { return false; }
    /// How the card decodes, in words, for the slot report (empty: the bus's generic text)
    virtual std::string DecodeNote() const { return {}; }
    /// A real bus cycle (side effects)
    virtual uint8_t Read(uint16_t offset) = 0;
    virtual void Write(uint16_t offset, uint8_t value) = 0;
    /// Debugger / automation: what a read would return, no side effect
    virtual uint8_t Peek(uint16_t offset) const = 0;

    /// The bus RESET line (ISA RESET DRV, the ATM connector's RS on a machine reset)
    virtual void Reset() = 0;

    /// The bus resources the device occupies (reports: "what is plugged where and what it uses"): its I/O range
    /// (first..last bus address it decodes, before mirrors), the IRQ line its configuration names (-1 none)
    virtual bool IoRange(uint32_t& first, uint32_t& last) const
    {
        (void)first;
        (void)last;
        return false;
    }
    virtual int IrqLine() const { return -1; }
    /// The register name at an offset, for access journals ("CR", "ISR", "data port"); empty = unnamed
    virtual const char* RegisterName(uint16_t offset, bool write) const
    {
        (void)offset;
        (void)write;
        return "";
    }

    /// The level of the device's interrupt output, and who wants to hear when it may have changed
    virtual bool Irq() const { return false; }
    virtual void SetIrqListener(std::function<void()> changed) { (void)changed; }

    /// A cycle the device does not finish: the bus hangs while it is set (a UMC UM9003's reset port read, network
    /// tdd §6.4). The owner of the bus decides what that does to the machine
    virtual bool Stalled() const { return false; }

    /// The frame boundary (machine thread)
    virtual void OnFrame() {}

    /// The device's own report fields (registers, counters), added to its slot's row
    virtual void Describe(StateNode& out) const { (void)out; }
};
