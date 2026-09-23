#pragma once

/// @file soundchip_gslw.h
/// @brief General Sound lightweight card - the no-coprocessor personality
/// (design: docs/inprogress/2026-09-19-general-sound/gs-card-personalities-tdd.md).
///
/// Protocol-faithful HLE: reacts on the host mailbox exactly like the gs105a
/// firmware (shared GSForwardMailbox, firmware-derived reply bytes, the
/// data-first rule, the D2-terminated COM30 stream, LOADCM discard of burst
/// commands during a load) but plays music through the in-tree ProTracker
/// player (gsmodplayer.h) instead of a second Z80. No ROM, no BASS library.
///
/// v1 coverage: music command set (memory queries, module load/start/stop/
/// continue, MODVOL/FXVOL/MTVOL, position queries, tempo) + graceful no-op
/// SFX commands (params consumed per the firmware protocol, nothing played).
///
/// Audio pipeline is byte-for-byte the LLE card's shape: two blip_t
/// accumulators clocked at 12 MHz, one sample-and-hold DAC latch update per
/// 320-cycle quantum, the computeStereo mapping (channels 1,2 -> L, 3,4 -> R,
/// 50% cross-feed) and the gs_vfx volume curve from config gs_vol.
///
/// TTD blob (PeripheralId::GeneralSoundLightweight): fixed 95-byte header
/// sharing the LLE mailbox layout ([59..94] queues, latches, timing), then
/// interpreter state, reply queue, upload store and the player runtime.

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gsmailbox.h"
#include "emulator/sound/chips/gs/gsmodplayer.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/ports/portdecoder.h"
#include "common/modulelogger.h"
#include "debugger/ttd/ttdserializable.h"

class EmulatorContext;
struct blip_t;

class SoundChip_GSLightweight : public GeneralSoundCard
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

public:
    explicit SoundChip_GSLightweight(EmulatorContext* context, size_t ramKB, size_t sampleRate = 44100);
    ~SoundChip_GSLightweight() override;

    SoundChip_GSLightweight() = delete;
    SoundChip_GSLightweight(const SoundChip_GSLightweight&) = delete;
    SoundChip_GSLightweight& operator=(const SoundChip_GSLightweight&) = delete;

    // Buffer access for the SoundManager registry
    int16_t* getBuffer() override { return _buffer; }
    const int16_t* getBuffer() const { return _buffer; }

    /// Live core-rate change: rebuild the blips, keep the row/tick position
    void setSampleRate(size_t sampleRate) override;

    /// Turbo mode: keep latch/level tracking, skip blip deltas (see Beeper)
    void setSynthesisSuppressed(bool suppressed) override;
    bool isSynthesisSuppressed() const override { return _synthesisSuppressed; }

    // Lifecycle
    void reset() override;
    void resetCard() override;
    void hostReset() override;
    void loadROM(const std::string& romPath) override; // warns + ignores

    // Frame lifecycle (SoundManager calls; expectedSamples = mixer count)
    void handleFrameStart() override;
    void handleFrameEnd(size_t expectedSamples = 0) override;

    // PortDevice interface (ZX-side ports #B3/#BB/#33)
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;

    // Read-only introspection - no side effects
    uint8_t getStatusRaw() const override { return _mb.status; }
    uint8_t getDataFromHost() const override { return _mb.dataFromHost; }
    uint8_t getDataToHost() const override { return _mb.dataToHost; }
    uint8_t getCommandFromHost() const override { return _mb.commandFromHost; }
    size_t getCommandQueueCount() const override { return (_mb.status & 0x01) ? 1 : 0; }
    size_t getDataQueueCount() const override { return _paramCount; }
    uint8_t getMPAG() const override { return 0; } // no banking on this card
    uint8_t getChannelSample(int channel) const override { return _channelData[channel & 3]; }
    uint8_t getChannelVolume(int channel) const override { return _channelVol[channel & 3]; }
    bool isROMLoaded() const override { return false; }
    size_t getRamSizeKB() const override { return _ramKB; }
    bool hadAudioActivityLastFrame() const override { return _wasActive; }

    // Coprocessor capability: this is the lightweight personality
    bool hasCoprocessor() const override { return false; }
    GSCardImplementation implementation() const override { return GSCardImplementation::LW; }

    /// region <Diagnostics: activity counters + port trace>
    const GSActivityCounters& getActivityCounters() const override { return _activityCounters; }
    void resetActivityCounters() override { _activityCounters = GSActivityCounters{}; }

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

    // Automation actions mirroring host-port semantics
    uint8_t readStatus() override;           // IN  #BB: _mb.status | 0x7E
    uint8_t readData() override;             // IN  #B3: pops the reply queue
    void sendCommand(uint8_t cmd) override;  // OUT #BB
    void sendData(uint8_t data) override;    // OUT #B3
    void triggerNMI() override;              // OUT #33 bit6: counted, no CPU

    /// region <Runtime personality switch (SoundManager::switchGeneralSoundCard)>
    GSForwardMailbox snapshotMailbox() const override;
    void restoreMailbox(const GSForwardMailbox& snapshot) override;
    void accumulateActivityCounters(const GSActivityCounters& other) override; // cpuSteps stays 0

    /// Capturing side of the v1 module handoff: the raw upload stream when
    /// a COM30 load has completed (the store survives the parse)
    bool captureModuleUpload(std::vector<uint8_t>& bytes, bool& playing) const override;

    /// Receiving side of the v1 module handoff: drives the interpreter's
    /// own host-port entry points exactly like a real host would (COM30 +
    /// payload + D2, then COM31 if the outgoing card was playing) - the
    /// instant-dispatch model needs no frame-stepping between calls
    void replayModuleUpload(const std::vector<uint8_t>& bytes, bool startPlayback) override;
    /// endregion </Runtime personality switch>

    /// region <TTDSerializable interface>
    /// Layout (little-endian, fixed-width):
    ///   [ 0.. 3] status, dataFromHost, dataToHost, commandFromHost (latches)
    ///   [ 4]    reserved (LLE mpag slot - always 0, no banking)
    ///   [ 5.. 8] channelVol[4]
    ///   [ 9..12] channelData[4]
    ///   [13..20] gsCyclesAbs (int64), [21..22] intQuantum (int16)
    ///   [23]    flags (bit0 nmi latched; LW never defers one)
    ///   [24..58] interpreter state (firmware variable mirror):
    ///            [24] MODVOL [25] FXVOL [26] FXMVOL [27] MTVOL [28] MODFADE
    ///            [29] FXFADE [30] CURMOD [31] CNTMOD [32] MODULE [33] MTSTAT
    ///            [34] ERRCODE [35] CURSMP [36] CNTSMP [37] CURFX [38] CNTFX
    ///            [39] mode flags (bit0 loading, bit1 discardLoad, bit2 covox,
    ///            bit3 subCommandExpected, bit4 subCommand80, bit5 subCommandA0)
    ///            [40] curChannel<<5 | subCommandSelector&0x1F [41] reply count
    ///            [42] reply head [43..58] reply ring[16]
    ///   [59..94] mailbox queues - the shared LLE layout verbatim
    ///   [95..  ] upload store (u32 len + bytes), player runtime blob
    ///            (u32 len + bytes)
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::GeneralSoundLightweight; }
    std::string TTDDeviceName() const override { return "GeneralSoundLightweight"; }
    uint64_t TTDHashState() const override;
    /// endregion </TTDSerializable interface>

    /// Fixed part of the TTD blob (mailbox-compatible with the LLE card)
    static constexpr size_t TTD_FIXED_STATE_SIZE = 95;

private:
    // Command interpreter (firmware-derived dispatch, see the .cpp table)
    void acceptHostData(uint8_t value);   // OUT #B3: instant consumption
    void acceptHostCommand(uint8_t value);// OUT #BB: instant dispatch
    void dispatchCommand(uint8_t command);
    void dispatchSubCommand(uint8_t sub); // COM50/80 selector continuation
    void enterLoad(bool discard);         // COM30 / FX-load modes
    void finishLoad();                    // D2: parse the upload store
    void softReset(bool post);            // COMF3 (INITVAR) / COMF4 (POST)
    void postReply(uint8_t value);        // OUTRG: card->host reply byte
    void pumpReply();                     // HSEND: make the queue head visible
    void consumeParam();                  // IN (DATRG): pop 1 buffered param byte
    void serializeFixedState(uint8_t* dst) const; // [0..94] LLE-compatible header

    // Audio pipeline (LLE-shaped)
    void makeVolumeTable();
    void emitSample();
    void computeStereo(int32_t& outL, int32_t& outR) const;
    void serviceQuantum();                // one 320-cycle player/DAC tick

    // Lazy sync core (same timing math as the LLE card)
    void flush();
    void runTo(int64_t targetGsCycles);
    int64_t totalGsCycles() const { return _gsCyclesAbs + _intQuantum; }
    int64_t frameGsLength() const;
    double gsCyclesPerZxTact() const;
    uint64_t currentZxTacts() const;
    uint32_t currentFrameNumber() const;
    void traceEvent(GSTraceSide side, uint16_t port, uint8_t value, bool isOut, uint8_t channel = 0, uint8_t extraFlags = 0);

    EmulatorContext* _context;
    const size_t _ramKB; // virtual geometry for queries 20/21/23

    // Shared protocol truth (host mailbox)
    GSForwardMailbox _mb;

    // Card->host reply queue (OUTRG + HSEND: posted in order, popped by #B3
    // reads; the firmware paces one unread byte, the queue preserves order)
    static constexpr size_t REPLY_QUEUE_CAPACITY = 16;
    uint8_t _replyQueue[REPLY_QUEUE_CAPACITY] = {};
    uint8_t _replyCount = 0;
    uint8_t _replyHead = 0;

    // Host->card param buffer: the instant interpreter takes every #B3 byte
    // in-call (bit7 never blocks the host), buffering params in write order
    // for the dispatch-time consumeParam calls (the real firmware paces one
    // hardware latch via the bit7 handshake instead)
    static constexpr size_t PARAM_QUEUE_CAPACITY = 16;
    uint8_t _paramQueue[PARAM_QUEUE_CAPACITY] = {};
    uint8_t _paramCount = 0;
    uint8_t _paramHead = 0;

    // Interpreter state (firmware variable mirror)
    uint8_t _modVol = 0x40;   // MODVOL (COM2A): music channel scaling
    uint8_t _fxVol = 0x40;    // FXVOL (COM2B): SFX output scaling
    uint8_t _fxMvol = 0x40;   // FXMVOL (COM3D): SFX channel scaling (distinct var)
    uint8_t _mtVol = 0x40;    // MTVOL (COM35): module master volume
    uint8_t _modFade = 0;     // MODFADE (COM34): get-then-set memory
    uint8_t _fxFade = 0;      // FXFADE (COM3B/3C): get-then-set memory
    uint8_t _curMod = 0;      // CURMOD: selected module
    uint8_t _cntMod = 0;      // CNTMOD: loaded module count
    uint8_t _module = 0;      // MODULE: playing module
    uint8_t _mtStat = 0;      // MTSTAT: bit7 stop, bit1 active, bit0 play
    uint8_t _errCode = 0;     // ERRCODE (COMF0)
    uint8_t _curSmp = 0;      // CURSMP/CNTSMP (COM2D selectors)
    uint8_t _cntSmp = 0;
    uint8_t _curFx = 0;       // CURFX/CNTFX (COM2E/38/39/80)
    uint8_t _cntFx = 0;
    uint8_t _curChannel = 0;  // COM04 current channel (0-7)
    bool _loading = false;    // COM30 module stream in progress
    bool _discardLoad = false; // FX-load variant: bytes dropped, not stored
    bool _covox = false;      // COM0E direct DAC loop
    bool _subCommandExpected = false; // COM50/58/80/A0 selector arrives next
    bool _subCommand80 = false;       // selector family: COM80 (bits 3-4 params)
    bool _subCommandA0 = false;       // selector family: COMA0 (no params)
    uint8_t _subCommandFlags = 0;     // raw selector & 0x1F (param consumption)

    // Module store: raw upload bytes; parsed live by the player
    std::vector<uint8_t> _store;
    GSModPlayer _player;

    // DAC channels with the gs_vfx volume curve (LLE shape)
    uint8_t _channelData[4] = {0x80, 0x80, 0x80, 0x80};
    uint8_t _channelVol[4] = {0, 0, 0, 0};
    uint32_t _vfx[65] = {};

    // Audio buffers (one frame of stereo int16)
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

    // Timing: _gsCyclesAbs counts completed 320-cycle quanta, _intQuantum is
    // the sub-quantum remainder (LLE-compatible clock domain)
    int64_t _gsCyclesAbs = 0;
    int16_t _intQuantum = 0;
    int64_t _frameStartGsCycles = 0;
    uint64_t _frameStartZxTacts = 0;
    int64_t _frameGsCycles = 0;
    bool _nmiPending = false; // counted on trigger, no CPU to deliver to

    // Diagnostics
    GSActivityCounters _activityCounters;
    GSPortTraceRecorder _portTrace;
};
