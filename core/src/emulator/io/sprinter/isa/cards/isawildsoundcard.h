#pragma once

/// @file isawildsoundcard.h
/// @brief Robus's ISA Wild Sound in a Sprinter ISA slot (docs/inprogress/2026-10-09-wildsound/design.md §2.1): the
/// card's STM32 behind two ISA I/O ports.
///
/// The board's glue is not documented; what the one Sprinter program for it (`prosiak.exe`) does fixes the map:
///
/// | ISA cycle | the card's AY bus |
/// |:--|:--|
/// | write base + 0 | address (register select) |
/// | write base + 1 | data |
/// | read base + 1 (and base + 0) | read |
///
/// Decoded on A9-A1 against the base (default #000), mirrored every #400 like the other cards; a cycle with AEN = 1
/// is ignored. RESET DRV (#9FBD bit 7) restarts the firmware when it is released (the STM32 resets on the AY RESET
/// line, reference-firmware.md §A.10).
///
/// Worked example (prosiak.exe probes slot 0): `#1FFD` <- #11, `OUT (#E2),#D4`, `#9FBD` <- #00, `LD (#C000),#F0`:
/// the bus hands the write (ISA I/O #00000) here, it becomes the card's address write #F0; `LD (#C001),A` x 20 send
/// "Wild Sound Enable ", #00, #00 as data; `LD A,(#C001)` reads the byte the card prepared: #DF.

#include <cstdint>
#include <memory>
#include <string>

#include "debugger/ttd/engine/ttdregiontracker.h"
#include "emulator/io/sprinter/isa/iisacard.h"
#include "emulator/sound/wildsound/wildsoundaudio.h"
#include "emulator/sound/wildsound/wildsoundcore.h"

class EmulatorContext;

namespace sprinterisa
{

class IsaWildSoundCard final : public IIsaCard, public ttd::ITTDRegionSource
{
public:
    /// With a context the card attaches its sound to the machine's SoundManager (null: the bus side alone, for tests)
    IsaWildSoundCard(EmulatorContext* context, uint32_t base = 0);
    ~IsaWildSoundCard() override;
    IsaWildSoundCard(const IsaWildSoundCard&) = delete;
    IsaWildSoundCard& operator=(const IsaWildSoundCard&) = delete;

    const char* Kind() const override { return "wildsound"; }

    bool IoRead(const IsaCycle& cycle, uint8_t& value) override;
    bool IoWrite(const IsaCycle& cycle, uint8_t value) override;
    bool IoPeek(uint32_t address, uint8_t& value) const override;

    void SetReset(bool asserted) override;

    bool IoRange(uint32_t& first, uint32_t& last) const override;
    std::string DecodeNote() const override;
    const char* RegisterName(bool io, uint32_t address, bool write) const override;
    std::string SummaryNote() const override;
    void Describe(StateNode& out) const override;

    size_t StateSize() const override;
    void SaveState(uint8_t* dst) const override;
    void LoadState(const uint8_t* src) override;

    /// The upload store as the engine region `wildsound.flash` (ttd::TTDRegionId::WildSoundFlash), owned by the
    /// SprinterIsa blob; `slot` names the instance ("isa.1")
    void SetRegionInstance(std::string instance) { _regionInstance = std::move(instance); }
    void TTDRegions(std::vector<ttd::TTDDeviceRegion>& out) override;
    void TTDArmRegions(bool on) override { _regionsArmed = on; }

    /// The machine goes away: leave SoundManager while it exists (PortDecoder_Sprinter::BeforeRelease)
    void DetachAudio();
    wildsound::WildSoundAudio* Audio() { return _audio.get(); }

    wildsound::WildSoundCore& Core() { return _core; }
    const wildsound::WildSoundCore& Core() const { return _core; }
    uint32_t Base() const { return _base; }
    bool ResetHeld() const { return _resetHeld; }

private:
    /// 0 = port 0 (address), 1 = port 1 (data); -1 = not this card's address
    int PortOf(uint32_t address) const;
    /// Frame-relative host time of the access, the Sprinter's turbo removed (the AY write queues' time base)
    uint64_t Now() const;
    /// Milliseconds since the card's reset (#EB)
    uint32_t UptimeMs() const;
    uint64_t AbsoluteMs() const;

    EmulatorContext* _context;
    uint32_t _base;
    wildsound::WildSoundCore _core;
    std::unique_ptr<wildsound::WildSoundAudio> _audio;
    bool _audioAttached = false;
    ttd::TTDRegionTracker _flashTracker;
    bool _regionsArmed = false;
    std::string _regionInstance = "isa";
    bool _resetHeld = false;
    uint64_t _resetPulses = 0;
    uint64_t _resetAtMs = 0;
};

}  // namespace sprinterisa
