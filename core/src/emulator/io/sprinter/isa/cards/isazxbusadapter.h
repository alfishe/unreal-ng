#pragma once

/// @file isazxbusadapter.h
/// @brief The ZX-bus adapter in a Sprinter ISA slot (docs/inprogress/2026-10-02-sprinter-isa/tdd.md §6, phase I2):
/// a Spectrum ZX-bus edge connector on an ISA-8 card, with the General Sound / NeoGS plugged into it.
///
/// The adapter is transparent for I/O (MAME `zxbus_adapter.cpp`: the whole ISA I/O space is ZX-bus I/O): an ISA I/O
/// cycle at address A becomes a Spectrum IN / OUT at port A15-A0. Memory cycles, IRQ and WAIT do not cross it (no
/// /MREQ on the ZX-bus side, open question Q7). ISA RESET DRV (#9FBD bit 7) drives the ZX-bus /RESET (owner decision
/// Q3): the GS card resets, as by its #33 bit-7 reset (CPU and banking; the host mailbox survives).
///
/// Worked example (ProPlay sends the GS warm restart #F3): `#1FFD` <- #11, `OUT (#E2),#D4`, `#9FBD` <- #00,
/// `LD (#C0BB),A`: SprinterMemory hands the write to SprinterIsaBus (I/O, slot 1, ISA address #000BB); the bus calls
/// IoWrite here; the adapter makes it the Spectrum `OUT (#BB),#F3`, which reaches the GS card through the decoder's
/// exact port map (PeripheralPortOut, the same door every Spectrum decoder uses). The card catches up to the
/// moment of the access and latches the command.
///
/// The GS card stays owned by SoundManager (fitted there because PortDecoder::ZxBusPresent() is true while an
/// adapter is configured); the adapter reaches whatever card SoundManager publishes now, so a runtime personality
/// switch needs nothing here. One GS per machine: a second adapter has an empty ZX-bus (its report says why).

#include <cstdint>
#include <string>

#include "emulator/io/sprinter/isa/iisacard.h"

class EmulatorContext;
class GeneralSoundCard;
class PortDecoder;

namespace sprinterisa
{

class IsaZxBusAdapter final : public IIsaCard
{
public:
    /// `carriesGs`: the machine's General Sound sits on this adapter; otherwise `emptyWhy` says why the ZX-bus is empty
    IsaZxBusAdapter(EmulatorContext* context, PortDecoder* decoder, bool carriesGs, std::string emptyWhy = {});

    const char* Kind() const override { return "zxbus"; }

    bool IoRead(const IsaCycle& cycle, uint8_t& value) override;
    bool IoWrite(const IsaCycle& cycle, uint8_t value) override;
    bool IoPeek(uint32_t address, uint8_t& value) const override;

    void SetReset(bool asserted) override;

    bool IoRange(uint32_t& first, uint32_t& last) const override;
    std::string DecodeNote() const override;
    const char* RegisterName(bool io, uint32_t address, bool write) const override;
    std::string SummaryNote() const override;
    void Describe(StateNode& out) const override;

    /// The GS on this adapter now (null: the ZX-bus is empty or [SOUND] GSType=NONE)
    GeneralSoundCard* Gs() const;
    bool CarriesGs() const { return _carriesGs; }
    bool ResetHeld() const { return _resetHeld; }

    /// The Spectrum port the General Sound answers for ZX port `port` (it decodes A7-A0): #B3 data, #BB
    /// command / status, #33 control; 0 when the GS does not decode the port
    static uint16_t GsPort(uint16_t port);

private:
    EmulatorContext* _context;
    PortDecoder* _decoder;
    bool _carriesGs;
    std::string _emptyWhy;
    bool _resetHeld = false;
    uint64_t _resetPulses = 0;
};

}  // namespace sprinterisa
