#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "3rdparty/z80ex/z80ex.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gsporttrace.h"
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
class SoundChip_GeneralSound : public PortDevice, public ttd::TTDSerializable
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_SOUND;
    const uint16_t _SUBMODULE = PlatformSoundSubmodulesEnum::SUBMODULE_SOUND_GS;
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

public:
    // Host-side port addresses (canonical keys for RegisterPortHandler; the
    // hardware decodes the low byte only, model decoders cover the aliases)
    static constexpr uint16_t PORT_DATA = 0x00B3;    // GSDAT
    static constexpr uint16_t PORT_COMMAND = 0x00BB; // GSCOM (status on read)
    static constexpr uint16_t PORT_CONTROL = 0x0033; // GSCTR (reset/NMI)

    // Fixed clocks and geometry
    static constexpr uint32_t GS_CLOCK_HZ = 12000000;
    static constexpr uint32_t GS_INT_FREQUENCY_HZ = 37500;
    static constexpr int GS_CYCLES_PER_INT = static_cast<int>(GS_CLOCK_HZ / GS_INT_FREQUENCY_HZ); // 320
    static constexpr size_t ROM_SIZE = 0x8000;  // 32 KB (2 x 16 KB pages)
    static constexpr size_t PAGE_SIZE = 0x4000; // 16 KB bank granularity
    static constexpr size_t RAM_PAIR_SIZE = 2 * PAGE_SIZE; // MPAG pair granularity

    explicit SoundChip_GeneralSound(EmulatorContext* context, size_t ramKB, size_t sampleRate = 44100);
    ~SoundChip_GeneralSound() override;

    SoundChip_GeneralSound() = delete;
    SoundChip_GeneralSound(const SoundChip_GeneralSound&) = delete;
    SoundChip_GeneralSound& operator=(const SoundChip_GeneralSound&) = delete;

    // Buffer access for the SoundManager registry
    int16_t* getBuffer() { return _buffer; }
    const int16_t* getBuffer() const { return _buffer; }

    /// Live core-rate change (device reroute with CoreRate=auto)
    void setSampleRate(size_t sampleRate);

    /// Turbo mode: keep register/level tracking, skip blip deltas (see Beeper)
    void setSynthesisSuppressed(bool suppressed);
    bool isSynthesisSuppressed() const { return _synthesisSuppressed; }

    // Lifecycle
    /// Full power-on reset (creation, ZX reset with GSReset=0): CPU, banking,
    /// mailbox, volumes, DAC levels and timing base
    void reset();

    /// #33 bit7 semantics: reset the GS CPU/banking/timing only - the host
    /// mailbox, volume latches and DAC levels survive (external flip-flops)
    void resetCard();

    /// ZX reset rule (design §5.4): GSReset=1 couples the card to the ZX
    /// reset line (Unreal z80.cpp "if (gsreset) reset_gs()"), GSReset=0
    /// keeps it running - separate subsystem with its own #33 reset line
    void hostReset();

    /// Load the 32 KB firmware ROM (warn + zero-fill when missing, design §7)
    void loadROM(const std::string& romPath);

    // Frame lifecycle (SoundManager calls; expectedSamples = mixer count)
    void handleFrameStart();
    void handleFrameEnd(size_t expectedSamples = 0);

    // PortDevice interface (ZX-side ports #B3/#BB/#33)
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;

    // Read-only introspection (automation, HUD, tests) - no flush side effects
    uint8_t getStatusRaw() const { return _status; }
    uint8_t getDataFromHost() const { return _dataFromHost; }
    uint8_t getDataToHost() const { return _dataToHost; }
    uint8_t getCommandFromHost() const { return _commandFromHost; }
    uint8_t getMPAG() const { return _mpag; }
    uint8_t getChannelSample(int channel) const { return _channelData[channel & 3]; }
    uint8_t getChannelVolume(int channel) const { return _channelVol[channel & 3]; }
    bool isROMLoaded() const { return _romLoaded; }
    size_t getRamSizeKB() const { return _ram.size() / 1024; }
    bool isCPUHalted() const { return _cpu && z80ex_doing_halt(_cpu) != 0; }
    uint16_t getCPUReg(Z80_REG_T reg) const { return _cpu ? z80ex_get_reg(_cpu, reg) : 0; }

    /// region <Diagnostics: activity counters + port/DAC trace>
    /// Cheap always-on counters - the first thing to check when triaging
    /// "is the GS coprocessor doing anything at all" (CLI/WebAPI/MCP/Lua/Python
    /// all read this via getActivityCounters()).
    const GSActivityCounters& getActivityCounters() const { return _activityCounters; }
    void resetActivityCounters() { _activityCounters = GSActivityCounters{}; }

    /// Structured event trace (host ports, GS-side ports, DAC fetches,
    /// interrupts) - opt-in, mirrors the main-Z80 port tracer's session model
    /// but scoped to this chip. See gsporttrace.h.
    void startPortTrace() { _portTrace.start(); }
    void stopPortTrace() { _portTrace.stop(); }
    void pausePortTrace() { _portTrace.pause(); }
    void resumePortTrace() { _portTrace.resume(); }
    void clearPortTrace() { _portTrace.clear(); }
    bool isPortTraceCapturing() const { return _portTrace.isCapturing(); }
    bool isPortTraceArmed() const { return _portTrace.isArmed(); }
    std::vector<GSTraceEvent> getPortTraceEvents() const { return _portTrace.getAll(); }
    std::vector<GSTraceEvent> getPortTraceLast(size_t count) const { return _portTrace.getLast(count); }
    size_t getPortTraceEventCount() const { return _portTrace.eventCount(); }
    uint64_t getPortTraceTotalProduced() const { return _portTrace.totalProduced(); }
    uint64_t getPortTraceTotalEvicted() const { return _portTrace.totalEvicted(); }
    /// endregion </Diagnostics>

    // Automation actions mirroring host-port semantics (each flushes first)
    uint8_t readStatus();           // IN  #BB: _status | 0x7E
    uint8_t readData();             // IN  #B3: clears bit7, returns _dataToHost
    void sendCommand(uint8_t cmd);  // OUT #BB: sets bit0
    void sendData(uint8_t data);    // OUT #B3: sets bit7
    void triggerNMI();              // OUT #33 bit6

    /// region <TTDSerializable interface (P1.5 - parent TDD 6.4)>
    ///
    /// Machine state: mailbox + banking + volume/DAC latches + timing counters
    /// + the full Z80 register file + the RAM array. Layout (little-endian,
    /// fixed-width, field-by-field - the z80ex context struct is not portable
    /// across platforms because of its unsigned long member):
    ///   [ 0.. 3] status, dataFromHost, dataToHost, commandFromHost
    ///   [ 4]    mpag
    ///   [ 5.. 8] channelVol[4]
    ///   [ 9..12] channelData[4]
    ///   [13..20] gsCyclesAbs (int64)
    ///   [21..22] intQuantum (int16)
    ///   [23]    nmiPending
    ///   [24..58] Z80: af bc de hl af2 bc2 de2 hl2 ix iy sp pc memptr (13x2),
    ///            i (1), r (2), r7 (1), iff1 iff2 (2), im (1), halted (1),
    ///            prefix (1)
    ///   [59..  ] RAM image (config-sized)
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::GeneralSound; }
    std::string TTDDeviceName() const override { return "GeneralSound"; }
    uint64_t TTDHashState() const override;
    /// endregion </TTDSerializable interface>

    /// Fixed part of the TTD blob (everything except the RAM image)
    static constexpr size_t TTD_FIXED_STATE_SIZE = 59;

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

    EmulatorContext* _context;

    // Dedicated GS Z80 (z80ex - never the main emulator CPU, design §4.3)
    Z80EX_CONTEXT* _cpu = nullptr;
    std::vector<uint8_t> _rom;   // 32 KB firmware
    std::vector<uint8_t> _ram;   // 128-512 KB (config [NGS] RamSize, clamped)
    bool _romLoaded = false;

    // Memory banking (§2.3 MPAG: 0 -> ROM pair, V>=1 -> RAM pair (V-1))
    uint8_t _mpag = 0;
    uint8_t _ramPairMask = 0;       // (ram_kb / 32) - 1
    const uint8_t* _bankR[4] = {};  // nullptr never happens for reads (ROM/RAM)
    uint8_t* _bankW[4] = {};        // nullptr -> write discarded (ROM windows)

    // Host mailbox (Unreal names gsdata_out/gsdata_in/gscmd/gsstat)
    uint8_t _dataFromHost = 0;   // ZX #B3 write, GS port 0x02 read
    uint8_t _dataToHost = 0;     // GS port 0x03 write, ZX #B3 read
    uint8_t _commandFromHost = 0; // ZX #BB write, GS port 0x01 read
    uint8_t _status = 0;         // bit7 = data pending, bit0 = command pending

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

    // Diagnostics: always-on counters + opt-in structured trace (gsporttrace.h)
    GSActivityCounters _activityCounters;
    GSPortTraceRecorder _portTrace;
    uint32_t currentFrameNumber() const;
    void traceEvent(GSTraceSide side, uint16_t port, uint8_t value, bool isOut, uint8_t channel = 0, uint8_t extraFlags = 0);
};
