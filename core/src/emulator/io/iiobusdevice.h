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
#include <vector>

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
    /// Further I/O addresses the device decodes beside IoRange, for the slot report (the 3C509B's ID port: any of
    /// #100-#1F0 in steps of #10)
    struct AuxIoRange
    {
        std::string name;             ///< "id_port"
        uint32_t first = 0, last = 0;
        uint32_t step = 1;            ///< the decoded addresses are first, first + step, ... last
        std::string note;             ///< what the device does there, now
    };
    virtual std::vector<AuxIoRange> AuxIoRanges() const { return {}; }
    /// The register name at an offset, for access journals ("CR", "ISR", "data port"); empty = unnamed
    virtual const char* RegisterName(uint16_t offset, bool write) const
    {
        (void)offset;
        (void)write;
        return "";
    }

    /// The level of the device's interrupt output (true = the request is active), and who wants to hear when it
    /// may have changed. The listener is also called when NextIrqEventAt may have moved (a character started on a
    /// UART's line): the bus then looks again. Calling Irq / IrqDriven / NextIrqEventAt from it is allowed; nothing
    /// that changes the device
    virtual bool Irq() const { return false; }
    virtual void SetIrqListener(std::function<void()> changed) { (void)changed; }
    /// Whether the device drives its interrupt pin now (a totem-pole output: low while Irq() is false). False = the
    /// pin is high impedance (an RTL8019AS with CONFIG1.IRQEN clear, a PC UART card with MCR.OUT2 clear, a line the
    /// slot does not have): the bus's pull-up decides the level
    virtual bool IrqDriven() const { return IrqLine() >= 0; }
    /// The earliest machine time (base T-states, ComPort::Now's clock) at which Irq() may change with no access to
    /// the device (a character arrives at a UART, a transmit ends on the wire); UINT64_MAX = none. The bus calls
    /// CatchUp at that time when someone waits for the interrupt
    virtual uint64_t NextIrqEventAt() const { return UINT64_MAX; }
    /// Bring the device to the current machine time (what its next access would do first)
    virtual void CatchUp() {}
    /// Why the interrupt output is where it is, in words for the slot report ("ISR #01 & IMR #11: PRX"); empty: none
    virtual std::string IrqCause() const { return {}; }

    /// A cycle the device does not finish: the bus hangs while it is set (a UMC UM9003's reset port read, network
    /// tdd §6.4). The owner of the bus decides what that does to the machine
    virtual bool Stalled() const { return false; }

    /// The frame boundary (machine thread)
    virtual void OnFrame() {}

    /// The device's own report fields (registers, counters), added to its slot's row
    virtual void Describe(StateNode& out) const { (void)out; }
};
