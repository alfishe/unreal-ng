#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "3rdparty/unreal-z80/z80cpu.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gsaudioout.h"
#include "emulator/sound/chips/gs/gscardrunner.h"
#include "emulator/sound/chips/gs/gshostclock.h"
#include "emulator/sound/chips/gs/gsmodulereplay.h"
#include "emulator/sound/chips/gs/gsmailbox.h"
#include "emulator/sound/chips/gs/gsprofile.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/ports/portdecoder.h"
#include "common/modulelogger.h"
#include "debugger/ttd/engine/ttdregiontracker.h"
#include "debugger/ttd/ttdserializable.h"  // TTDSerializable (P1.5 peripheral serializer)

class EmulatorContext;

/// General Sound (GS) expansion card - LLE model with a dedicated Z80
/// coprocessor (unreal-z80 library, core/src/3rdparty/unreal-z80) (design: docs/inprogress/2026-09-19-general-sound/gs-tdd.md).
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
///
/// Board profile (gsprofile.h): the clocks, the INT divider, the RAM range,
/// the host port set, the memory map, the GS-side port rules and an optional
/// DAC sink come from a GSProfile given at construction. The default is the
/// classic card above, unchanged; GSProfile::MultiSound is the ZX-MultiSound's
/// GS (16 MHz, INT 12 MHz / 321 as a 33-clock pulse, 1-2 MB, no #33, DACs
/// handed to the board's shared DAC block).
class SoundChip_GeneralSound : public GeneralSoundCard, public ttd::ITTDRegionSource
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

public:
    // Host-side port addresses: inherited from GeneralSoundCard (existing
    // SoundChip_GeneralSound::PORT_* call sites resolve unchanged)

    // Classic GS board clocks (12 MHz, 37.5 kHz interrupt divider)
    static constexpr uint32_t GS_CLOCK_HZ = GSClassicTiming::CLOCK_HZ;
    static constexpr uint32_t GS_INT_FREQUENCY_HZ = GSClassicTiming::INT_FREQUENCY_HZ;
    static constexpr int GS_CYCLES_PER_INT = GSClassicTiming::CYCLES_PER_INT; // 320

    // LLE-only geometry
    static constexpr size_t ROM_SIZE = 0x8000;  // 32 KB (2 x 16 KB pages)
    static constexpr size_t PAGE_SIZE = 0x4000; // 16 KB bank granularity
    static constexpr size_t RAM_PAIR_SIZE = 2 * PAGE_SIZE; // MPAG pair granularity
    /// 16 KB RAM page (index into _ram) behind the fixed window 0x4000-0x7FFF:
    /// the upper half of RAM chip 1, i.e. the same cells as 0xC000-0xFFFF
    /// under MPAG 1. GS schematic (GeneralSound v1.0 GS_GENER.TXT): the window
    /// decoder's 0x4000 output and the page decoder's page-1 output both drive
    /// chip select RAM1 through diodes, and every 32 KB chip takes CPU A0-A14,
    /// so A14=1 picks its upper half. The firmware RAM probe relies on this
    /// alias (INIT_L.a80: writes each page's number to 0xFFFF, reads 0x7FFF).
    static constexpr size_t FIXED_WINDOW_RAM_PAGE = 1;
    static constexpr size_t RAM_SIZE_STANDARD_KB = 128; // stock card; 256/512 KB were expansions

    explicit SoundChip_GeneralSound(EmulatorContext* context, size_t ramKB, size_t sampleRate = 44100,
                                    const GSProfile& profile = GSProfile::Classic());
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
    /// The board profile this card was built with
    const GSProfile& profile() const { return _profile; }

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
    void onEmulatorPaused() override;

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
    bool isReadyForCommands() const override { return _activityCounters.volumeLatchWrites >= 4; }
    bool peekCardMemory(uint16_t addr, uint8_t& out) const override
    {
        out = _bankR[(addr >> 14) & 3][addr & (PAGE_SIZE - 1)];
        return true;
    }
    uint8_t getChannelSample(int channel) const override { return _channelData[channel & 3]; }
    uint8_t getChannelVolume(int channel) const override { return _channelVol[channel & 3]; }
    bool isROMLoaded() const override { return _romLoaded; }
    size_t getRamSizeKB() const override { return _ram.size() / 1024; }
    bool hadAudioActivityLastFrame() const override { return _wasActive; }
    bool isCPUHalted() const override { return _cpu && Z80CpuHalted(_cpu) != 0; }
    uint16_t getCPUReg(GSCpuRegister reg) const override;

    // Coprocessor capability: this is the LLE personality
    bool hasCoprocessor() const override { return true; }
    GSCardImplementation implementation() const override { return GSCardImplementation::LLE; }
    std::string deviceDescription() const override;

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

    /// Shared DACs (GSProfile::dacSink): another source on the board wrote
    /// this channel's volume register, which the GS shares (MultiSound: a
    /// SounDrive write sets it to 63, and GS port #0B reads volume 3 bit 5).
    /// The card runs to the host's now first, so its earlier accesses see the
    /// old value; the sink is not told (the board already has the write)
    void sharedVolumeWrite(int channel, uint8_t volume);

    /// Bus /RESET of a board with its own host clock (GSProfile::hostClock):
    /// the power-on reset (reset()) at the host's now. The card's time 0 is
    /// anchored to the host's current tact and the rest of the host frame is
    /// what the frame end still runs, so the firmware starts at the reset's
    /// time (reset() alone keeps the frame base: the card would replay the
    /// frame's elapsed time from the reset state, the #33 semantics)
    void resetAtHostNow();

    /// region <Runtime personality switch (SoundManager::switchGeneralSoundCard)>
    GSForwardMailbox snapshotMailbox() const override;
    void restoreMailbox(const GSForwardMailbox& snapshot) override;
    void accumulateActivityCounters(const GSActivityCounters& other) override;

    /// Receiving side of the v1 module handoff: mailbox-paced upload of the
    /// captured COM30 stream (param + command + stream + D2), paced a few
    /// interrupt periods at a time per byte so the single-latch protocol
    /// never overwrites a byte the firmware hasn't consumed yet; COM31
    /// restarts playback when the outgoing card was playing
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
    /// + the full Z80 state + the RAM array. Layout (little-endian,
    /// fixed-width, field-by-field):
    ///   [ 0.. 3] status, dataFromHost, dataToHost, commandFromHost (latches)
    ///   [ 4]    mpag
    ///   [ 5.. 8] channelVol[4]
    ///   [ 9..12] channelData[4]
    ///   [13..20] gsCyclesAbs (int64)
    ///   [21..22] intQuantum (int16)
    ///   [23]    nmiPending (bit0) | intPending (bit1); bits 2/3 reserved
    ///            (queue-era pending flags, always 0)
    ///   [24..58] Z80 (Z80CpuRegisters): af bc de hl af2 bc2 de2 hl2 ix iy sp
    ///            pc memptr (13x2), i, r (with R7), q, boundary
    ///            (Z80CpuBoundary: EI shadow, pending prefix, ...), iff1, iff2,
    ///            im, halted, nmiInProgress (1 each) - everything the next
    ///            instruction and the next INT/NMI acceptance depend on
    ///   [59..76] reserved (queue-era slots, always 0)
    ///   [77..80] GS cycles since the frame start (u32)
    ///   [81..84] ZX tacts at the frame start (u32, AudioTstate domain)
    ///   [85..88] frame length in GS cycles (u32)
    ///   [89..94] reserved (always 0)
    ///   [95..  ] RAM image (config-sized)
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::GeneralSound; }
    ttd::TTDDeviceDescriptor TTDDescribe() const override
    {
        ttd::TTDDeviceDescriptor d = ttd::TTDSerializable::TTDDescribe();
        d.runsBehindCpu = true;
        d.firmwareFingerprint = _romHash;   // the 32 KB ROM: configuration, not recorded
        return d;
    }
    /// Synced: as NeoGS - the card at or after its frame base (where its CPU
    /// stood at the frame start, after the frame end ran it through the
    /// frame) and less than a frame past it
    bool TTDSyncedTime(int64_t& offset) const override
    {
        offset = totalGsCycles() - _frameStartGsCycles;
        return offset >= 0 && (_frameGsCycles <= 0 || offset < _frameGsCycles);
    }
    std::string TTDDeviceName() const override { return "GeneralSound"; }
    uint64_t TTDHashState() const override;
    /// endregion </TTDSerializable interface>

    /// Fixed part of the TTD blob (everything except the RAM image)
    static constexpr size_t TTD_FIXED_STATE_SIZE = 95;

    /// Time-travel engine region (Phase 1, Step 6): the card RAM (128-512 KB);
    /// the engine's blob is the fixed state only
    void TTDRegions(std::vector<ttd::TTDDeviceRegion>& out) override;
    void TTDArmRegions(bool on) override { _ramTrackerArmed = on ? &_ramTracker : nullptr; }
    bool TTDStateWithoutRegions(uint8_t& peripheralId, std::vector<uint8_t>& state) const override;
    bool TTDLoadStateWithoutRegions(const uint8_t* state, size_t size) override;

private:
    /// The ROM file into _rom (loadROM then takes its fingerprint)
    void readROM(const std::string& romPath);

    /// TTD load, split around the RAM the engine restores as a region
    void loadFixedState(const uint8_t* src);
    void finishLoad(const uint8_t* src);
    // Shared catch-up loop and module replay drive the card through the
    // private hooks below (gscardrunner.h, gsmodulereplay.h)
    friend class GSCardRunner<SoundChip_GeneralSound>;
    template <class Card>
    friend GSModuleReplayResult gsReplayModuleUpload(Card&, const std::vector<uint8_t>&, bool, size_t*);

    // GSCardRunner hooks. Card time is in profile units (gsprofile.h; classic:
    // 12 MHz cycles, one unit per cycle); the only event is the interrupt
    // period boundary. A pulse profile (intLowUnits > 0) drops the request
    // once the pulse is over: the line is sampled against the period start
    bool nmiPending() const { return _nmiPending; }
    void onNmiAccepted();
    bool intLine() const { return _intPending && (_intLowUnits == 0 || _runner.now() < _gsCyclesAbs + _intLowUnits); }
    void onIntAccepted();
    void scheduleNextPeriod() { _runner.setNextEvent(_gsCyclesAbs + _intPeriodUnits); }
    void runEvents(int64_t now);
    int64_t unitsPerCycle() const { return _unitsPerCycle; }
    static constexpr bool kCanStall = false; // no DMA on the classic card
    void onStep() { _activityCounters.cpuSteps++; }

    // GSModuleReplay hooks
    void replayRunFor(int64_t cycles) { runTo(totalGsCycles() + cycles); }
    int64_t replayByteStepUnits() const { return 2 * _intPeriodUnits; }
    // one HSEND timeout is ~73 frames; 239602 = one Pentagon frame of 12 MHz cycles
    int64_t replayMaxByteWaitUnits() const
    {
        return 200 * 239602 * static_cast<int64_t>(_profile.UnitsPerSecond() / GSClassicTiming::CLOCK_HZ);
    }

    // Z80 bus callbacks (userData = this)
    static uint8_t gsMemRead(Z80CPU* cpu, uint16_t addr, int m1State, void* userData);
    static void gsMemWrite(Z80CPU* cpu, uint16_t addr, uint8_t value, void* userData);
    static uint8_t gsPortRead(Z80CPU* cpu, uint16_t port, void* userData);
    static void gsPortWrite(Z80CPU* cpu, uint16_t port, uint8_t value, void* userData);
    static uint8_t gsPortReadMultiSound(Z80CPU* cpu, uint16_t port, void* userData);
    static void gsPortWriteMultiSound(Z80CPU* cpu, uint16_t port, uint8_t value, void* userData);
    static uint8_t gsIntRead(Z80CPU* cpu, void* userData);

    // GS-side port handlers (ports 0x00-0x0B): classic, and the MultiSound
    // CPLD's rules (GSPortRules), bound per profile at construction
    uint8_t gsIn(uint16_t port);
    void gsOut(uint16_t port, uint8_t value);
    uint8_t gsInMultiSound(uint16_t port);
    void gsOutMultiSound(uint16_t port, uint8_t value);
    void volumeWrite(uint16_t port, uint8_t value);  // ports 6-9 (own mix or DAC sink)

    // Memory subsystem
    void applyBanking();              // rebuild _bankR/_bankW from _mpag
    void applyPort0A();               // status bit 7 <- NOT MPAG bit 0
    void applyPort0B(int channel);    // status bit 0 <- bit 5 of that channel's volume
    void applyMultiSoundBanking();    // MultiSound memory maps (MultiSoundLogic::GsMemoryMapFor)
    uint8_t readMem(uint16_t addr);    // includes DAC fetch trigger
    void writeMem(uint16_t addr, uint8_t value);
    void dacFetch(uint16_t addr, uint8_t value); // caller checks the 0x6000-0x7FFF window

    // Audio pipeline
    void makeVolumeTable();            // rebuild _vfx from config gs_vol
    void emitSample();                 // recompute L/R, add blip deltas
    void computeStereo(int32_t& outL, int32_t& outR) const;
    void sinkSample(int channel, uint8_t value);  // DAC sink path (profile.dacSink)
    void sinkVolume(int channel, uint8_t volume);
    uint64_t hostTimeNow() const;      // card time now -> host time (AudioTstate domain)
    double hostUnitsPerTact() const;   // card units per host tact (profile.hostClock or the machine's clock)
    uint64_t hostTactsNow(uint64_t fallback) const;  // host time now (profile.hostClock or the machine's Z80)

    // TTD serialization internals
    void serializeFixedState(uint8_t* dst) const;

    // Lazy sync core
    void flush();                      // run GS to the current ZX tact
    void runTo(int64_t targetGsCycles)
    {
        // The classic timing (1 unit per cycle, INT held until accepted) runs
        // its own policy: its per-instruction path is the pre-profile one
        if (_classicTiming)
            _runner.runTo<ClassicRunPolicy>(targetGsCycles);
        else
            _runner.runTo(targetGsCycles);
    }
    struct ClassicRunPolicy
    {
        static int64_t units(const SoundChip_GeneralSound*, int t) { return t; }
        static bool intLine(const SoundChip_GeneralSound* card) { return card->_intPending; }
    };
    int64_t totalGsCycles() const { return _runner.now(); }
    int64_t frameGsLength() const;     // ZX frame -> card units

    // replayModuleUpload internals (GSModuleReplay hooks)
    void replayDrainReply();    // consume one pending card->host byte
    void replayAdvanceFrame();  // run the firmware one frame of GS card time

    // Shared host-port write handling (ZX-side ports + automation actions):
    // latch update plus the v1 module handoff capture
    void onHostDataWrite(uint8_t value);
    void onHostCommandWrite(uint8_t value);

    EmulatorContext* _context;

    // Board profile and its derived timing (gsprofile.h)
    const GSProfile _profile;
    const double _unitsPerSecond;      // blip clock and host -> card conversion
    const int64_t _unitsPerCycle;      // classic 1, MultiSound 3 (48 MHz units, 16 MHz CPU)
    const int64_t _intPeriodUnits;     // classic 320, MultiSound 1284
    const int64_t _intLowUnits;        // 0 = held until accepted (classic)
    const bool _classicTiming;         // unitsPerCycle 1 and a held INT: GSCardRunner's constant policy
    double _unitsPerZxTact = 0;        // the current frame's conversion (DAC sink times)

    // Dedicated GS Z80 (unreal-z80 library, callback bus - never the main
    // emulator CPU, design §4.3)
    Z80CPU* _cpu = nullptr;
    std::vector<uint8_t> _rom;   // 32 KB firmware
    std::vector<uint8_t> _ram;   // profile range: classic 128-512 KB, MultiSound 1-2 MB (512 KB chips in order)
    ttd::TTDRegionTracker _ramTracker;
    ttd::TTDRegionTracker* _ramTrackerArmed = nullptr;   // set while the engine records
    bool _romLoaded = false;
    uint64_t _romHash = 0;   ///< ttd::FirmwareFingerprint of _rom, kept with every change of it

    // Memory banking (§2.3 MPAG: 0 -> ROM pair, V>=1 -> RAM pair (V-1);
    // the MultiSound map: MultiSoundLogic::GsMemoryMapFor)
    uint8_t _mpag = 0;
    size_t _ramPairMask = 0;        // (ram_kb / 32) - 1 (classic map)
    const uint8_t* _bankR[4] = {};  // nullptr never happens for reads (ROM/RAM)
    uint8_t* _bankW[4] = {};        // nullptr -> write discarded (ROM windows)

    // Host mailbox: shadow latches and the ZX-visible status byte (shared
    // command/data flip-flop, original hardware semantics) - the shared
    // protocol truth for every card personality (gsmailbox.h)
    GSForwardMailbox _mb;

    // DAC channels with the gs_vfx volume curve (Unreal gsz80.cpp:249)
    uint8_t _channelData[4] = {0x80, 0x80, 0x80, 0x80};
    uint8_t _channelVol[4] = {0, 0, 0, 0};
    uint32_t _vfx[65] = {};      // per-256 scaled curve from config gs_vol

    // Audio buffers (one frame of stereo int16, Covox pattern) and the
    // shared output stage (blip pair in the 12 MHz card clock)
    AudioFrameDescriptor _audioDescriptor;
    int16_t* const _buffer = reinterpret_cast<int16_t*>(_audioDescriptor.memoryBuffer);
    size_t _sampleRate;
    GSAudioOut _audio;
    bool _synthesisSuppressed = false;

    // Activity tracking for HUD notification
    bool _frameHadActivity = false;
    bool _wasActive = false;

    // Timing: the runner counts card cycles (totalGsCycles); _gsCyclesAbs is
    // the start of the current 320-cycle interrupt period (Unreal
    // gs_t_states), so the position inside it (Unreal gscpu.t, the TTD
    // intQuantum field) is totalGsCycles() - _gsCyclesAbs
    GSCardRunner<SoundChip_GeneralSound> _runner;
    int64_t _gsCyclesAbs = 0;
    int64_t _frameStartGsCycles = 0;
    uint64_t _frameStartZxTacts = 0;
    int64_t _frameGsCycles = 0;
    bool _nmiPending = false;

    // Level-held 37.5 kHz INT request (design §2.4): asserted at each quantum
    // boundary, cleared only by acceptance - the request survives firmware
    // ISR/QTDONE stretches that run with IFF1 off (see runTo)
    bool _intPending = false;

    // v1 module handoff capture (host-port layer, personality-agnostic)
    GSUploadCapture _upload;

    // Diagnostics: always-on counters + opt-in structured trace (gsporttrace.h)
    GSActivityCounters _activityCounters;
    GSPortTraceRecorder _portTrace;
    uint32_t currentFrameNumber() const;
    void traceEvent(GSTraceSide side, uint16_t port, uint8_t value, bool isOut, uint8_t channel = 0, uint8_t extraFlags = 0);
};
