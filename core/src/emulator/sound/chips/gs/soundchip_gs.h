#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "3rdparty/z80ex/z80ex.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gsmailbox.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/ports/portdecoder.h"
#include "common/modulelogger.h"
#include "debugger/ttd/ttdserializable.h"  // TTDSerializable (P1.5 peripheral serializer)

class EmulatorContext;
struct blip_t;

/// General Sound (GS) expansion card - LLE model with a dedicated Z80ex
/// coprocessor (design: docs/inprogress/2026-09-19-general-sound/gs-tdd.md).
///
/// Hardware: Z80 @ 12 MHz, 32 KB ROM, 128-512 KB RAM, 4 x 8-bit DAC channels
/// with 6-bit volume each, 37.5 kHz periodic interrupt (every 320 cycles).
///
/// Host ports (ZX side, low-byte decoded): #B3 data, #BB command/status,
/// #33 control (bit7 = reset, bit6 = NMI). GS-side ports 0x00-0x0B implement
/// MPAG banking, the host mailbox and the volume latches.
///
/// The host mailbox (command/data FIFOs, shadow latches, direction-split
/// pending flags) is the shared GSForwardMailbox - gsmailbox.h carries the
/// full protocol rationale (burst writes without ack polling, param-first
/// ordering, destructive COMRG/DATRG reads, the non-popping RSCOM ack and
/// the direction-split data-pending bits), verified against the gs105a
/// firmware.
///
/// Timing: lazy sync (Xpeccy model) - the GS CPU is flushed to the ZX clock
/// on every host port access and at the frame boundary, so ZX software
/// polling #BB observes protocol-level timing. The GS blip_buf clock is the
/// fixed 12 MHz GS clock; frame length in that domain is
/// frame_tstates * 12 MHz / zx_base_hz, independent of the host speed
/// multiplier (GS hardware does not speed up with ZX turbo).
///
/// Audio: per-write sample-and-hold emission into two blip_buf accumulators
/// (Covox pattern). Channel mixing follows Unreal Speccy: channels 1,2 -> L,
/// 3,4 -> R with 50% cross-feed and the gs_vfx volume curve rebuilt from
/// config gs_vol.
class SoundChip_GeneralSound : public GeneralSoundCard
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

public:
    // Host-side port addresses, clocks and geometry: inherited from
    // GeneralSoundCard (kept name-compatible via inheritance - existing
    // SoundChip_GeneralSound::PORT_* call sites resolve unchanged)

    // LLE-only geometry
    static constexpr size_t ROM_SIZE = 0x8000;  // 32 KB (2 x 16 KB pages)
    static constexpr size_t PAGE_SIZE = 0x4000; // 16 KB bank granularity
    static constexpr size_t RAM_PAIR_SIZE = 2 * PAGE_SIZE; // MPAG pair granularity
    static constexpr size_t RAM_SIZE_STANDARD_KB = 128; // stock card; 256/512 KB were expansions

    explicit SoundChip_GeneralSound(EmulatorContext* context, size_t ramKB, size_t sampleRate = 44100);
    ~SoundChip_GeneralSound() override;

    SoundChip_GeneralSound() = delete;
    SoundChip_GeneralSound(const SoundChip_GeneralSound&) = delete;
    SoundChip_GeneralSound& operator=(const SoundChip_GeneralSound&) = delete;

    // Buffer access for the SoundManager registry
    int16_t* getBuffer() override { return _buffer; }
    const int16_t* getBuffer() const { return _buffer; }

    /// Live core-rate change (device reroute with CoreRate=auto)
    void setSampleRate(size_t sampleRate) override;

    /// Turbo mode: keep register/level tracking, skip blip deltas (see Beeper)
    void setSynthesisSuppressed(bool suppressed) override;
    bool isSynthesisSuppressed() const override { return _synthesisSuppressed; }

    // Lifecycle
    /// Full power-on reset (creation, ZX reset with GSReset=0): CPU, banking,
    /// mailbox, volumes, DAC levels and timing base
    void reset() override;

    /// #33 bit7 semantics: reset the GS CPU/banking/timing only - the host
    /// mailbox, volume latches and DAC levels survive (external flip-flops)
    void resetCard() override;

    /// ZX reset rule (design §5.4): GSReset=1 couples the card to the ZX
    /// reset line (Unreal z80.cpp "if (gsreset) reset_gs()"), GSReset=0
    /// keeps it running - separate subsystem with its own #33 reset line
    void hostReset() override;

    /// Load the 32 KB firmware ROM (warn + zero-fill when missing, design §7)
    void loadROM(const std::string& romPath) override;

    // Frame lifecycle (SoundManager calls; expectedSamples = mixer count)
    void handleFrameStart() override;
    void handleFrameEnd(size_t expectedSamples = 0) override;

    // PortDevice interface (ZX-side ports #B3/#BB/#33)
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;

    // Read-only introspection (automation, HUD, tests) - no flush side effects
    uint8_t getStatusRaw() const override { return _mb.status; }
    uint8_t getDataFromHost() const override { return _mb.dataFromHost; }
    uint8_t getDataToHost() const override { return _mb.dataToHost; }
    uint8_t getCommandFromHost() const override { return _mb.commandFromHost; }
    size_t getCommandQueueCount() const override { return (_mb.status & 0x01) ? 1 : 0; }
    size_t getDataQueueCount() const override { return (_mb.status & 0x80) ? 1 : 0; }
    uint8_t getMPAG() const override { return _mpag; }
    uint8_t getChannelSample(int channel) const override { return _channelData[channel & 3]; }
    uint8_t getChannelVolume(int channel) const override { return _channelVol[channel & 3]; }
    bool isROMLoaded() const override { return _romLoaded; }
    size_t getRamSizeKB() const override { return _ram.size() / 1024; }
    bool isCPUHalted() const override { return _cpu && z80ex_doing_halt(_cpu) != 0; }
    uint16_t getCPUReg(Z80_REG_T reg) const override { return _cpu ? z80ex_get_reg(_cpu, reg) : 0; }

    // Coprocessor capability: this is the LLE personality
    bool hasCoprocessor() const override { return true; }
    GSCardImplementation implementation() const override { return GSCardImplementation::LLE; }

    /// region <Diagnostics: activity counters + port/DAC trace>
    /// Cheap always-on counters - the first thing to check when triaging
    /// "is the GS coprocessor doing anything at all" (CLI/WebAPI/MCP/Lua/Python
    /// all read this via getActivityCounters()).
    const GSActivityCounters& getActivityCounters() const override { return _activityCounters; }
    void resetActivityCounters() override { _activityCounters = GSActivityCounters{}; }

    /// Structured event trace (host ports, GS-side ports, DAC fetches,
    /// interrupts) - opt-in, mirrors the main-Z80 port tracer's session model
    /// but scoped to this chip. See gsporttrace.h.
    void startPortTrace() override { _portTrace.start(); }
    void stopPortTrace() override { _portTrace.stop(); }
    void pausePortTrace() override { _portTrace.pause(); }
    void resumePortTrace() override { _portTrace.resume(); }
    void clearPortTrace() override { _portTrace.clear(); }
    bool isPortTraceCapturing() const override { return _portTrace.isCapturing(); }
    bool isPortTraceArmed() const override { return _portTrace.isArmed(); }
    std::vector<GSTraceEvent> getPortTraceEvents() const override { return _portTrace.getAll(); }
    std::vector<GSTraceEvent> getPortTraceLast(size_t count) const override { return _portTrace.getLast(count); }
    size_t getPortTraceEventCount() const override { return _portTrace.eventCount(); }
    uint64_t getPortTraceTotalProduced() const override { return _portTrace.totalProduced(); }
    uint64_t getPortTraceTotalEvicted() const override { return _portTrace.totalEvicted(); }
    /// endregion </Diagnostics>

    // Automation actions mirroring host-port semantics (each flushes first)
    uint8_t readStatus() override;           // IN  #BB: _mb.status | 0x7E
    uint8_t readData() override;             // IN  #B3: clears the GS->host pending bit
    void sendCommand(uint8_t cmd) override;  // OUT #BB: sets bit0
    void sendData(uint8_t data) override;    // OUT #B3: sets the host->GS pending bit
    void triggerNMI() override;              // OUT #33 bit6

    /// region <Runtime personality switch (SoundManager::switchGeneralSoundCard)>
    GSForwardMailbox snapshotMailbox() const override;
    void restoreMailbox(const GSForwardMailbox& snapshot) override;
    void accumulateActivityCounters(const GSActivityCounters& other) override;

    /// Receiving side of the v1 module handoff: mailbox-paced upload of the
    /// captured COM30 stream (param + command + stream + D2), one frame of
    /// GS card time per paced chunk so the 16-deep FIFOs never overflow;
    /// COM31 restarts playback when the outgoing card was playing
    void replayModuleUpload(const std::vector<uint8_t>& bytes, bool startPlayback) override;

    /// Capturing side of the v1 module handoff (bidirectional): the raw
    /// COM30 payload stream is mirrored at the host-port layer as it goes
    /// by (independent of where the firmware parks it in GS RAM), so any
    /// completed upload is replayable onto the other personality
    bool captureModuleUpload(std::vector<uint8_t>& bytes, bool& playing) const override;
    /// endregion </Runtime personality switch>

    /// region <TTDSerializable interface (P1.5 - parent TDD 6.4)>
    ///
    /// Machine state: mailbox + banking + volume/DAC latches + timing counters
    /// + the full Z80 register file + the RAM array. Layout (little-endian,
    /// fixed-width, field-by-field - the z80ex context struct is not portable
    /// across platforms because of its unsigned long member):
    ///   [ 0.. 3] status, dataFromHost, dataToHost, commandFromHost (latches)
    ///   [ 4]    mpag
    ///   [ 5.. 8] channelVol[4]
    ///   [ 9..12] channelData[4]
    ///   [13..20] gsCyclesAbs (int64)
    ///   [21..22] intQuantum (int16)
    ///   [23]    nmiPending (bit0) | intPending (bit1); bits 2/3 reserved
    ///            (queue-era pending flags, always 0)
    ///   [24..58] Z80: af bc de hl af2 bc2 de2 hl2 ix iy sp pc memptr (13x2),
    ///            i (1), r (2), r7 (1), iff1 iff2 (2), im (1), halted (1),
    ///            prefix (1)
    ///   [59..94] reserved (queue-era slots, always 0)
    ///   [95..  ] RAM image (config-sized)
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::GeneralSound; }
    std::string TTDDeviceName() const override { return "GeneralSound"; }
    uint64_t TTDHashState() const override;
    /// endregion </TTDSerializable interface>

    /// Fixed part of the TTD blob (everything except the RAM image);
    /// [59..94] are reserved queue-era slots kept for layout compatibility
    static constexpr size_t TTD_FIXED_STATE_SIZE = 95;

private:
    // z80ex callbacks (user_data = this)
    static Z80EX_BYTE gsMemRead(Z80EX_CONTEXT* cpu, Z80EX_WORD addr, int m1State, void* userData);
    static void gsMemWrite(Z80EX_CONTEXT* cpu, Z80EX_WORD addr, Z80EX_BYTE value, void* userData);
    static Z80EX_BYTE gsPortRead(Z80EX_CONTEXT* cpu, Z80EX_WORD port, void* userData);
    static void gsPortWrite(Z80EX_CONTEXT* cpu, Z80EX_WORD port, Z80EX_BYTE value, void* userData);
    static Z80EX_BYTE gsIntRead(Z80EX_CONTEXT* cpu, void* userData);

    // GS-side port handlers (ports 0x00-0x0B)
    uint8_t gsIn(uint16_t port);
    void gsOut(uint16_t port, uint8_t value);

    // Memory subsystem
    void applyBanking();               // rebuild _bankR/_bankW from _mpag
    uint8_t readMem(uint16_t addr);    // includes DAC fetch trigger
    void writeMem(uint16_t addr, uint8_t value);
    void dacFetch(uint16_t addr, uint8_t value); // (addr & 0xE000) == 0x6000 window

    // Audio pipeline
    void makeVolumeTable();            // rebuild _vfx from config gs_vol
    void emitSample();                 // recompute L/R, add blip deltas
    void computeStereo(int32_t& outL, int32_t& outR) const;

    // TTD serialization internals
    void serializeFixedState(uint8_t* dst) const;

    // Lazy sync core
    void flush();                      // run GS to the current ZX tact
    void runTo(int64_t targetGsCycles);
    int64_t totalGsCycles() const { return _gsCyclesAbs + _intQuantum; }
    int64_t frameGsLength() const;     // ZX frame -> GS cycles (12 MHz domain)
    double gsCyclesPerZxTact() const;  // 12 MHz / effective ZX clock
    uint64_t currentZxTacts() const;   // AudioTstate domain (hw turbo descaled)

    // replayModuleUpload internals
    void replayDrainReply();    // consume one pending card->host byte
    void replayAdvanceFrame();  // run the firmware one frame of GS card time

    // Shared host-port write handling (ZX-side ports + automation actions):
    // latch update plus the v1 module handoff capture
    void onHostDataWrite(uint8_t value);
    void onHostCommandWrite(uint8_t value);

    EmulatorContext* _context;

    // Dedicated GS Z80 (z80ex - never the main emulator CPU, design §4.3)
    Z80EX_CONTEXT* _cpu = nullptr;
    std::vector<uint8_t> _rom;   // 32 KB firmware
    std::vector<uint8_t> _ram;   // 128-512 KB (stock 128 KB unless expanded via ctor)
    bool _romLoaded = false;

    // Memory banking (§2.3 MPAG: 0 -> ROM pair, V>=1 -> RAM pair (V-1))
    uint8_t _mpag = 0;
    uint8_t _ramPairMask = 0;       // (ram_kb / 32) - 1
    const uint8_t* _bankR[4] = {};  // nullptr never happens for reads (ROM/RAM)
    uint8_t* _bankW[4] = {};        // nullptr -> write discarded (ROM windows)

    // Host mailbox: shadow latches, the ZX-visible status byte,
    // direction-split pending flags and both FIFO rings - the shared
    // protocol truth for every card personality (gsmailbox.h)
    GSForwardMailbox _mb;

    // DAC channels with the gs_vfx volume curve (Unreal gsz80.cpp:249)
    uint8_t _channelData[4] = {0x80, 0x80, 0x80, 0x80};
    uint8_t _channelVol[4] = {0, 0, 0, 0};
    uint32_t _vfx[65] = {};      // per-256 scaled curve from config gs_vol

    // Audio buffers (one frame of stereo int16, Covox pattern)
    AudioFrameDescriptor _audioDescriptor;
    int16_t* const _buffer = reinterpret_cast<int16_t*>(_audioDescriptor.memoryBuffer);
    blip_t* _blipL = nullptr;
    blip_t* _blipR = nullptr;
    bool _synthesisSuppressed = false;
    size_t _sampleRate;
    int32_t _lastL = 0;
    int32_t _lastR = 0;

    // Activity tracking for HUD notification
    bool _frameHadActivity = false;
    bool _wasActive = false;

    // Timing: _gsCyclesAbs counts completed 320-cycle quanta (Unreal
    // gs_t_states), _intQuantum is the position inside the current quantum
    // (Unreal gscpu.t). Total executed = sum of both.
    int64_t _gsCyclesAbs = 0;
    int16_t _intQuantum = 0;
    int64_t _frameStartGsCycles = 0;
    uint64_t _frameStartZxTacts = 0;
    int64_t _frameGsCycles = 0;
    bool _nmiPending = false;

    // Level-held 37.5 kHz INT request (design §2.4): asserted at each quantum
    // boundary, cleared only by acceptance - the request survives firmware
    // ISR/QTDONE stretches that run with IFF1 off (see runTo)
    bool _intPending = false;

    // v1 module handoff capture (host-port layer, personality-agnostic):
    // mirrors the COM30 payload stream as the host writes it, independent
    // of the firmware's own RAM layout. _uploadStore holds the last
    // COM30..D2 stream that completed with a non-empty payload; _uploadLive
    // is true while a stream is currently open (mid-upload = not capturable)
    std::vector<uint8_t> _uploadStore;
    bool _uploadLive = false;
    bool _uploadHadModule = false;
    bool _uploadPlaying = false;

    // Diagnostics: always-on counters + opt-in structured trace (gsporttrace.h)
    GSActivityCounters _activityCounters;
    GSPortTraceRecorder _portTrace;
    uint32_t currentFrameNumber() const;
    void traceEvent(GSTraceSide side, uint16_t port, uint8_t value, bool isOut, uint8_t channel = 0, uint8_t extraFlags = 0);
};
