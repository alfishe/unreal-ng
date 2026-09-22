#pragma once

/// @file generalsoundcard.h
/// @brief Personality-agnostic contract for the General Sound expansion slot.
///
/// One mailbox command interface, three interchangeable implementations
/// (design: docs/inprogress/2026-09-19-general-sound/gs-card-personalities-tdd.md):
///
/// - SoundChip_GeneralSound  - LLE: second Z80 + gs105a firmware (GSType=Z80)
/// - SoundChip_GSLightweight - HLE: in-tree ProTracker player, no coprocessor
///                             (GSType=LW; replaces upstream's BASS path)
/// - NeoGS                   - reserved (neogs-tdd.md, P2)
///
/// SoundManager, the TTD peripheral registry and every automation surface
/// (CLI/WebAPI/MCP/Lua/Python) talk to this type only. The host-port mailbox
/// semantics (single-latch command/data flip-flops, original hardware
/// behavior - a same-direction write before the card consumes the previous
/// one just overwrites it, no queueing) are part of the contract and are
/// shared code (gsmailbox.h).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "3rdparty/z80ex/z80ex.h" // Z80_REG_T (coprocessor introspection)
#include "common/modulelogger.h" // PlatformModulesEnum (shared submodule id)
#include "emulator/ports/portdecoder.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/sound/chips/gs/gsmailbox.h"  // GSForwardMailbox (switch snapshots)

/// Which personality is fitted (introspection/switching; GSCardImplementation
/// names the implementation, GSTypeKind the config input that selected it)
enum class GSCardImplementation : uint8_t
{
    LLE, // SoundChip_GeneralSound (Z80 + firmware)
    LW,  // SoundChip_GSLightweight (in-tree mod player)
    NGS  // NeoGS (reserved, P2)
};

class GeneralSoundCard : public PortDevice, public ttd::TTDSerializable
{
public:
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_SOUND;
    const uint16_t _SUBMODULE = PlatformSoundSubmodulesEnum::SUBMODULE_SOUND_GS;
    /// endregion </ModuleLogger definitions for Module/Submodule>

public:
    // Host-side port addresses (canonical keys for RegisterPortHandler; the
    // hardware decodes the low byte only, model decoders cover the aliases)
    static constexpr uint16_t PORT_DATA = 0x00B3;    // GSDAT
    static constexpr uint16_t PORT_COMMAND = 0x00BB; // GSCOM (status on read)
    static constexpr uint16_t PORT_CONTROL = 0x0033; // GSCTR (reset/NMI)

    // Shared clock/geometry constants (fixed on every personality: the card
    // crystal and the 37.5 kHz interrupt divider are board facts, not
    // implementation details)
    static constexpr uint32_t GS_CLOCK_HZ = 12000000;
    static constexpr uint32_t GS_INT_FREQUENCY_HZ = 37500;
    static constexpr int GS_CYCLES_PER_INT = static_cast<int>(GS_CLOCK_HZ / GS_INT_FREQUENCY_HZ); // 320

    // PortDevice has no virtual destructor (upstream); the personality base
    // provides one so SoundManager can own cards through this pointer
    ~GeneralSoundCard() override = default;
    GeneralSoundCard() = default;
    GeneralSoundCard(const GeneralSoundCard&) = delete;
    GeneralSoundCard& operator=(const GeneralSoundCard&) = delete;

    // Lifecycle
    /// Full power-on reset (creation, ZX reset with GSReset=0)
    virtual void reset() = 0;
    /// #33 bit7 semantics: card reset, host mailbox survives (external
    /// flip-flops). LLE resets CPU/banking/timing; LW resets the interpreter
    /// and stops playback
    virtual void resetCard() = 0;
    /// ZX reset rule (GS design §5.4): GSReset=1 couples the card to the ZX
    /// reset line; GSReset=0 keeps it running
    virtual void hostReset() = 0;
    /// Firmware image. LLE: the 32 KB gs105a ROM. LW: accepted for API
    /// symmetry, warned and ignored (no coprocessor to run it)
    virtual void loadROM(const std::string& romPath) = 0;

    // Frame lifecycle (SoundManager calls; expectedSamples = mixer count)
    virtual void handleFrameStart() = 0;
    virtual void handleFrameEnd(size_t expectedSamples = 0) = 0;

    // Buffer access for the SoundManager registry (one frame of stereo int16)
    virtual int16_t* getBuffer() = 0;

    /// Live core-rate change (device reroute with CoreRate=auto)
    virtual void setSampleRate(size_t sampleRate) = 0;

    /// Turbo mode: keep register/level tracking, skip synthesis deltas
    virtual void setSynthesisSuppressed(bool suppressed) = 0;
    virtual bool isSynthesisSuppressed() const = 0;

    // Automation actions mirroring host-port semantics
    virtual uint8_t readStatus() = 0;           // IN  #BB
    virtual uint8_t readData() = 0;             // IN  #B3
    virtual void sendCommand(uint8_t cmd) = 0;  // OUT #BB
    virtual void sendData(uint8_t data) = 0;    // OUT #B3
    virtual void triggerNMI() = 0;              // OUT #33 bit6

    // Read-only introspection (automation, HUD, tests) - no side effects
    virtual uint8_t getStatusRaw() const = 0;
    virtual uint8_t getDataFromHost() const = 0;
    virtual uint8_t getDataToHost() const = 0;
    virtual uint8_t getCommandFromHost() const = 0;
    virtual size_t getCommandQueueCount() const = 0;
    virtual size_t getDataQueueCount() const = 0;
    virtual uint8_t getMPAG() const = 0;              // LW: always 0 (no banking)
    virtual uint8_t getChannelSample(int channel) const = 0;
    virtual uint8_t getChannelVolume(int channel) const = 0;
    virtual bool isROMLoaded() const = 0;             // LW: always false
    virtual size_t getRamSizeKB() const = 0;          // LW: virtual geometry

    // Coprocessor capability: the register/debug views below carry real
    // values only on the LLE card; non-coprocessor personalities return
    // zeros/false and automation prints a placeholder instead
    virtual bool hasCoprocessor() const = 0;
    virtual GSCardImplementation implementation() const = 0;
    virtual bool isCPUHalted() const { return false; }
    virtual uint16_t getCPUReg(Z80_REG_T) const { return 0; }

    /// region <Diagnostics: activity counters + port/DAC trace>
    /// Cheap always-on counters - the first thing to check when triaging
    /// "is the card doing anything at all" (CLI/WebAPI/MCP/Lua/Python all
    /// read this via getActivityCounters()). Identical data model on every
    /// personality; LLE fills the coprocessor fields, LW the player ones
    /// (dacFetches = player sample fetches, so triage reads the same).
    virtual const GSActivityCounters& getActivityCounters() const = 0;
    virtual void resetActivityCounters() = 0;

    /// Structured event trace (host ports, card-side activity, DAC fetches,
    /// interrupts) - opt-in, mirrors the main-Z80 port tracer's session
    /// model but scoped to this card. See gsporttrace.h.
    virtual void startPortTrace() = 0;
    virtual void stopPortTrace() = 0;
    virtual void pausePortTrace() = 0;
    virtual void resumePortTrace() = 0;
    virtual void clearPortTrace() = 0;
    virtual bool isPortTraceCapturing() const = 0;
    virtual bool isPortTraceArmed() const = 0;
    virtual std::vector<GSTraceEvent> getPortTraceEvents() const = 0;
    virtual std::vector<GSTraceEvent> getPortTraceLast(size_t count) const = 0;
    virtual size_t getPortTraceEventCount() const = 0;
    virtual uint64_t getPortTraceTotalProduced() const = 0;
    virtual uint64_t getPortTraceTotalEvicted() const = 0;
    /// endregion </Diagnostics>

    /// region <Runtime personality switch (SoundManager::switchGeneralSoundCard)>
    ///
    /// v1 semantics (design: gs-card-interface.md §Runtime switching): the
    /// forward mailbox queues/latches/pending flags and the activity
    /// counters survive the handoff; a module captured by the lightweight
    /// card is replayed through a fresh LLE firmware. The reverse direction
    /// keeps the mailbox and simply stops playback - the module lives
    /// inside firmware RAM and cannot be extracted in v1.
public:
    /// Copy the queues, shadow latches and pending flags for a handoff.
    /// The counters back-pointer is owner state, not protocol state - it is
    /// nulled in the snapshot and rebound by restoreMailbox.
    virtual GSForwardMailbox snapshotMailbox() const = 0;

    /// Adopt a snapshot (queues/latches/flags only; this card's activity
    /// counters stay bound to its own mailbox accounting)
    virtual void restoreMailbox(const GSForwardMailbox& snapshot) = 0;

    /// Fold another card's counters into this one (triage totals survive
    /// the handoff; see accumulateGSActivityCounters)
    virtual void accumulateActivityCounters(const GSActivityCounters& other) = 0;

    /// Capturing side of the v1 module handoff (bidirectional): the raw
    /// COM30..D2 upload stream when a load has completed, plus whether
    /// playback was active. Both personalities mirror this at the host-port
    /// layer as the stream goes by, independent of where each one parks
    /// the bytes internally. Default: nothing to hand over.
    virtual bool captureModuleUpload(std::vector<uint8_t>& bytes, bool& playing) const
    {
        bytes.clear();
        playing = false;
        return false;
    }

    /// Receiving side: push the captured upload through this card's own
    /// interpreter (COM30 + payload + D2, then COM31 if it was playing).
    /// Default: ignored (same-personality switching is a no-op)
    virtual void replayModuleUpload(const std::vector<uint8_t>& bytes, bool startPlayback)
    {
        (void)bytes;
        (void)startPlayback;
    }
    /// endregion </Runtime personality switch>
};
