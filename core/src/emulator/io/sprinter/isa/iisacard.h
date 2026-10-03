#pragma once

/// @file iisacard.h
/// @brief A card in one of the Sprinter's ISA-8 slots (docs/inprogress/2026-10-02-sprinter-isa/tdd.md §4.2).
///
/// The bus (SprinterIsaBus) hands every cycle of the slot to its card with the 20-bit ISA address
/// (`#9FBD` bits 5-0 << 14 | CPU A13-A0) and AEN. A card that does not drive the data bus answers false: the
/// bus then reads #FF (the pull-ups). Peeks come from debuggers and automation: no side effect.
///
/// Network cards are shared devices (IIoBusDevice, owned by NetworkManager); the Sprinter reaches them
/// through one wrapper, IsaBusDeviceCard. Their state travels in their own TTD blobs; a card with state the
/// bus must carry (the ISA RAM card, later) reports StateSize() and saves it into the SprinterIsa blob.

#include <cstddef>
#include <cstdint>

#include "emulator/state/statenode.h"

namespace sprinterisa
{

struct IsaCycle
{
    uint32_t address = 0;   ///< 20-bit ISA address
    bool aen = false;       ///< #9FBD bit 6: an I/O card ignores the cycle (a DMA cycle on a PC)
};

class IIsaCard
{
public:
    virtual ~IIsaCard() = default;

    /// "zxbus", "ram", "ne2000", ...
    virtual const char* Kind() const = 0;

    /// A real I/O cycle; false = the card does not drive the bus
    virtual bool IoRead(const IsaCycle& cycle, uint8_t& value) = 0;
    virtual bool IoWrite(const IsaCycle& cycle, uint8_t value) = 0;
    virtual bool MemRead(const IsaCycle& cycle, uint8_t& value)
    {
        (void)cycle;
        (void)value;
        return false;
    }
    virtual bool MemWrite(const IsaCycle& cycle, uint8_t value)
    {
        (void)cycle;
        (void)value;
        return false;
    }
    /// Debugger / automation reads: no side effect
    virtual bool IoPeek(uint32_t address, uint8_t& value) const = 0;
    virtual bool MemPeek(uint32_t address, uint8_t& value) const
    {
        (void)address;
        (void)value;
        return false;
    }

    /// The slot's RESET DRV (#9FBD bit 7): held = the card is in reset
    virtual void SetReset(bool asserted) = 0;

    /// The card's interrupt request level (ISA phase I4 routes it to the Z84C15 PIO port B)
    virtual bool Irq() const { return false; }

    /// What the card occupies on the ISA bus (the slot report's resources): its I/O range and memory window as
    /// first..last ISA addresses (false: none), the IRQ line its configuration names (-1: none)
    virtual bool IoRange(uint32_t& first, uint32_t& last) const
    {
        (void)first;
        (void)last;
        return false;
    }
    virtual bool MemRange(uint32_t& first, uint32_t& last) const
    {
        (void)first;
        (void)last;
        return false;
    }
    virtual int IrqLine() const { return -1; }
    /// The register a cycle reaches, for the access journal ("CR", "data port"; empty: unnamed). Asked before the
    /// cycle runs (a page switch in that cycle names the old page's register, as the card decoded it)
    virtual const char* RegisterName(bool io, uint32_t address, bool write) const
    {
        (void)io;
        (void)address;
        (void)write;
        return "";
    }
    /// Whether the card hangs the bus (a cycle it never finishes)
    virtual bool Stalled() const { return false; }

    /// The frame boundary
    virtual void FrameEnd() {}

    /// What the card shows in the slot report (card-specific members of the slot's object)
    virtual void Describe(StateNode& out) const { (void)out; }

    /// State the SprinterIsa blob carries for the card (bytes; fixed for the card's lifetime)
    virtual size_t StateSize() const { return 0; }
    virtual void SaveState(uint8_t* dst) const { (void)dst; }
    virtual void LoadState(const uint8_t* src) { (void)src; }
};

}  // namespace sprinterisa
