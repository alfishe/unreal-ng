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
/// - SoundChip_NeoGS         - NeoGS: FPGA card + flash firmware (GSType=NGS,
///                             neogs-tdd.md)
///
/// SoundManager, the TTD peripheral registry and every automation surface
/// (CLI/WebAPI/MCP/Lua/Python) talk to this type only. The host-port mailbox
/// semantics (single-latch command/data flip-flops, original hardware
/// behavior - a same-direction write before the card consumes the previous
/// one just overwrites it, no queueing) are part of the contract and are
/// shared code (gsmailbox.h).

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/modulelogger.h" // PlatformModulesEnum (shared submodule id)
#include "emulator/platform.h"          // GSTypeKind
#include "emulator/ports/portdecoder.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/sound/chips/gs/gsmailbox.h"  // GSForwardMailbox (switch snapshots)
#include "emulator/sound/chips/gs/gshostclock.h" // GSClassicTiming

/// Which personality is fitted (introspection/switching; GSCardImplementation
/// names the implementation, GSTypeKind the config input that selected it)
enum class GSCardImplementation : uint8_t
{
    LLE, // SoundChip_GeneralSound (Z80 + firmware)
    LW,  // SoundChip_GSLightweight (in-tree mod player)
    NGS  // SoundChip_NeoGS (neogs-tdd.md)
};

/// Automation label of a personality: the `implementation` field of the GS
/// state on every surface ("lle" | "lightweight" | "ngs")
inline const char* gsImplementationLabel(GSCardImplementation impl)
{
    switch (impl)
    {
        case GSCardImplementation::LLE: return "lle";
        case GSCardImplementation::LW: return "lightweight";
        case GSCardImplementation::NGS: return "ngs";
    }
    return "unknown";
}

/// Short personality name, as accepted by switch_personality and echoed in
/// its reply ("z80" | "lw" | "ngs")
inline const char* gsImplementationShortName(GSCardImplementation impl)
{
    switch (impl)
    {
        case GSCardImplementation::LLE: return "z80";
        case GSCardImplementation::LW: return "lw";
        case GSCardImplementation::NGS: return "ngs";
    }
    return "unknown";
}

/// The personality a GSTypeKind selects (NONE and BASS have none: false)
inline bool gsImplementationOf(GSTypeKind kind, GSCardImplementation& out)
{
    switch (kind)
    {
        case GSTypeKind::Z80: out = GSCardImplementation::LLE; return true;
        case GSTypeKind::LW: out = GSCardImplementation::LW; return true;
        case GSTypeKind::NGS: out = GSCardImplementation::NGS; return true;
        default: return false;
    }
}

/// Personality names accepted on every automation surface (case-insensitive):
/// z80 | lle, lw | lightweight, ngs | neogs
inline bool gsParsePersonality(std::string name, GSTypeKind& out)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name == "z80" || name == "lle")
        out = GSTypeKind::Z80;
    else if (name == "lw" || name == "lightweight")
        out = GSTypeKind::LW;
    else if (name == "ngs" || name == "neogs")
        out = GSTypeKind::NGS;
    else
        return false;
    return true;
}

/// For error messages
constexpr const char* GS_PERSONALITY_NAMES = "z80, lle, lw, lightweight, ngs or neogs";

/// NeoGS-only state for automation (CLI/WebAPI/MCP/Lua/Python), so those
/// layers need no NeoGS headers (neogs-tdd.md §7.5). Plain data, a snapshot.
struct NeoGSStateInfo
{
    // Card
    std::string flashTitle;
    bool flashModified = false;
    uint8_t gscfg0 = 0;
    uint32_t clockHz = 0;
    uint8_t pages[4] = {};
    bool windowFlash[4] = {};
    uint8_t mpag = 0;
    bool ledOn = false;
    bool readyForCommands = false;
    // Interrupts
    uint8_t intEnable = 0;
    uint8_t intRequest = 0;
    uint8_t timFreq = 0;
    // SPI / SD card
    uint8_t sctrl = 0;
    bool sdPresent = false;
    std::string sdPath;
    bool sdSdhc = false;
    uint64_t sdSizeBytes = 0;
    uint64_t sdBlocksRead = 0;
    uint64_t sdBlocksWritten = 0;
    // MP3 decoder
    bool mp3Fitted = false;
    const char* mp3Chip = "";
    bool mp3Dreq = false;
    uint32_t mp3Rate = 0;
    int mp3Channels = 0;
    uint64_t mp3Frames = 0;
    uint32_t mp3DecodeSeconds = 0;
    size_t mp3InputFill = 0;
    // DMA (0 ZX, 1 SD, 2 MP3)
    uint8_t dmaSelect = 0;
    bool dmaRunning[3] = {};
    uint32_t dmaAddress[3] = {};
    // ZX-DMA: the host's view (neogs-zxdma-design.md §7)
    const char* zxMode = "off";       // off | watch | divert
    bool zxOverlayInstalled = false;
    uint8_t zxReadLatch = 0;          // the byte the next host read gets
    const char* zxPending = "none";   // none | read | write
    uint32_t zxPendingAddress = 0;
    uint64_t zxBytesRead = 0;
    uint64_t zxBytesWritten = 0;
    uint64_t zxBytesDropped = 0;
    uint64_t zxWaitTStates = 0;
    uint64_t zxLateStarts = 0;
    uint64_t zxLateStartUnits = 0;    // 120 MHz card ticks the late starts were seen after
    const char* zxWatchSetting = "selected"; // selected | always
    uint32_t zxWatchFrames = 0;
    int32_t zxWatchFramesLeft = -1;   // -1: window closed
};

/// Coprocessor register selector for the introspection API (automation, HUD,
/// tests): the full register file. Independent of the Z80 core the LLE card
/// runs on. R includes bit 7; MEMPTR is the undocumented WZ register.
enum class GSCpuRegister : uint8_t
{
    AF, BC, DE, HL, AFAlt, BCAlt, DEAlt, HLAlt, IX, IY, SP, PC, I, R, IM, IFF1, IFF2, MEMPTR
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

    // Clock constants are per card: the classic GS and the lightweight player
    // share GSClassicTiming (gshostclock.h); NeoGS has its own clocks

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

    /// Emulator paused: no further handleFrameStart/End calls will arrive
    /// until resumed, so the card's own internal state (coprocessor timing,
    /// module playback position) is already frozen by construction - nothing
    /// to do there. What DOES need an explicit push here: getBuffer() still
    /// holds whatever the last rendered frame produced (silence or not), and
    /// _wasActive still holds whatever it was at the moment of pause - with
    /// no more handleFrameEnd calls to update either, a card that happened to
    /// be mid-playback when pause hit would keep reporting "active" (buffer
    /// non-silent, activity indicator lit) for as long as the pause lasts.
    /// Zeroes the buffer and clears _wasActive so the audio-settings LED
    /// (and the HUD nudge held from it, SoundManager::onEmulatorPaused) go
    /// dark immediately rather than staying stuck on the pre-pause state.
    virtual void onEmulatorPaused() = 0;

    // Buffer access for the SoundManager registry (one frame of stereo int16)
    virtual int16_t* getBuffer() = 0;
    /// A second output with its own analogue path on the board (the NeoGS
    /// MP3 decoder), same format; nullptr when the card has none
    virtual int16_t* getAuxBuffer() { return nullptr; }
    virtual bool hadAuxAudioActivityLastFrame() const { return false; }
    /// NeoGS: the card's own DMA (SD card, MP3 decoder) moved data during the
    /// last frame (HUD activity; no sound needed - software may use the card
    /// as an accelerator)
    virtual bool hadDmaActivityLastFrame() const { return false; }
    /// NeoGS: ZX-DMA moved data between the ZX and the card during the last frame
    virtual bool hadHostTransferActivityLastFrame() const { return false; }

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

    /// True when the stereo mix actually changed during the most recently
    /// finished frame (handleFrameEnd) - the GS audio-settings LED, which
    /// the HUD nudge is held from (AudioActivityIndicators). Deliberately NOT "is any sample in getBuffer()
    /// non-zero": a DAC channel latched to a non-centre value by a command
    /// and then left alone (e.g. a one-shot digi sample's last byte, or a
    /// firmware self-test tone) renders as a constant-but-non-zero PCM level
    /// forever after, which a naive peak-amplitude check reports as
    /// perpetually "active" even though nothing is actually playing anymore.
    /// This is what a UI activity indicator should poll instead of getBuffer().
    virtual bool hadAudioActivityLastFrame() const = 0;

    // Coprocessor capability: the register/debug views below carry real
    // values only on the LLE card; non-coprocessor personalities return
    // zeros/false and automation prints a placeholder instead
    virtual bool hasCoprocessor() const = 0;
    virtual GSCardImplementation implementation() const = 0;
    /// One-line hardware description for automation ("General Sound (...)")
    virtual std::string deviceDescription() const = 0;
    virtual bool isCPUHalted() const { return false; }

    /// The card's firmware is past its boot and polls the host mailbox, so a
    /// command sent now is processed rather than lost (module replay after a
    /// personality switch waits for this, neogs-tdd.md §7.1). Cards without
    /// firmware are always ready.
    virtual bool isReadyForCommands() const { return true; }

    /// DAC channels the card mixes (introspection loops use this, not 4)
    virtual int channelCount() const { return 4; }

    /// NeoGS extras (other cards: false / no-op)
    virtual bool neogsState(NeoGSStateInfo& out) const
    {
        (void)out;
        return false;
    }
    /// SD slot: false when refused (no slot, the image cannot be opened, or
    /// a TTD recording runs - the machine's configuration is fixed while
    /// recording). Call on the machine's thread: automation goes through
    /// neogsmedia.h, which hands the request over
    virtual bool insertSdCard(const std::string& path)
    {
        (void)path;
        return false;
    }
    virtual bool ejectSdCard() { return false; }
    /// The inserted SD card image, empty when the slot is empty or absent
    virtual std::string sdCardImage() const { return {}; }
    /// Card memory as the card CPU sees it now, no side effects (no DAC latch,
    /// no flash state change). False on cards without a CPU.
    virtual bool peekCardMemory(uint16_t addr, uint8_t& out) const
    {
        (void)addr;
        out = 0xFF;
        return false;
    }
    virtual bool saveFlash() { return false; }
    /// Firmware/flash description for automation ("32 KB ROM", "NeoGS flash v1.11")
    virtual std::string firmwareDescription() const { return isROMLoaded() ? "Loaded (32 KB)" : "Missing (zero-filled)"; }
    virtual uint16_t getCPUReg(GSCpuRegister) const { return 0; }

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
