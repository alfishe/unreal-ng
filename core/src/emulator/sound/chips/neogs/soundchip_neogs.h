#pragma once

/// @file soundchip_neogs.h
/// @brief NeoGS sound card (NedoPC): a real Z80 plus FPGA glue - 2/4 MB RAM,
/// 512 KB flash, four switchable windows, eight DAC channels, an interrupt
/// controller, SPI masters for an SD card and an MP3 decoder, DMA
/// (design: docs/inprogress/2026-09-19-general-sound/neogs-tdd.md).
///
/// The third personality of the General Sound slot (GSType=NGS). It runs the
/// card's own flash firmware unchanged: the loader from flash page 0, which
/// copies the GS-compatible main ROM into RAM (or loads NEOGS.ROM from the SD
/// card) and starts it at 20 MHz.
///
/// Time is counted in base ticks of 1/120,000,000 s (§5.2): the card CPU runs
/// at 24, 20, 12 or 10 MHz (5, 6, 10 or 12 ticks per cycle, GSCFG0 bits 5:4,
/// taking effect from the next instruction), while the timer and the DAC run
/// from the 24 MHz crystal (5 ticks per clock), phase-locked to each other and
/// free-running from power-on.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "3rdparty/unreal-z80/z80cpu.h"
#include "common/modulelogger.h"
#include "emulator/io/flash/flash29f040b.h"
#include "emulator/io/sdcard/sdcardspi.h"
#include "emulator/platform.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/chips/gs/gsaudioout.h"
#include "emulator/sound/chips/gs/gscardrunner.h"
#include "emulator/sound/chips/gs/gscpuregisters.h"
#include "emulator/sound/chips/gs/gsmailbox.h"
#include "emulator/sound/chips/gs/gsmodulereplay.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/sound/chips/neogs/neogsdma.h"
#include "emulator/sound/chips/neogs/neogsinterrupts.h"
#include "emulator/sound/chips/neogs/neogsmemory.h"
#include "emulator/sound/chips/neogs/neogssound.h"
#include "emulator/sound/chips/neogs/neogsspi.h"
#include "emulator/sound/chips/neogs/neogszxdma.h"
#include "emulator/sound/chips/neogs/vs10xx.h"

class EmulatorContext;
namespace ttd
{
enum class TTDExternalEventKind : uint8_t;
}

class SoundChip_NeoGS : public GeneralSoundCard, private NeoGSDma::Host, private NeoGSZxDma::Host
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

public:
    // Time base (§5.2)
    static constexpr double TICKS_PER_SECOND = 120e6;
    static constexpr int64_t TICKS_PER_CRYSTAL = 5;            // 24 MHz crystal
    static constexpr int64_t CRYSTAL_PER_DAC_SIDE = 320;       // 75 kHz, L and R alternate
    static constexpr int64_t TICKS_PER_DAC_SIDE = CRYSTAL_PER_DAC_SIDE * TICKS_PER_CRYSTAL; // 1,600
    static constexpr int64_t TIMER_SYNC_CYCLES = 2;            // tick -> controller, card clocks

    /// Base ticks per card CPU cycle for GSCFG0 bits 5:4 (#00 24 MHz, #10 12,
    /// #20 20, #30 10)
    static constexpr int64_t ticksPerCycleFor(uint8_t gscfg0)
    {
        constexpr int64_t table[4] = {5, 10, 6, 12};
        return table[(gscfg0 >> 4) & 3];
    }

    // GSCFG0
    static constexpr uint8_t GSCFG0_RESET = 0x30;
    static constexpr uint8_t GSCFG0_MAIN_ROM = 0x23; // RAM mode, RAMRO, 20 MHz

    // Main ROM v1.11 facts (neogs-tdd.md §4.2)
    static constexpr uint16_t MAIN_ROM_COMINT = 0x026E; // command poll loop (IN A,(ZXSTAT))
    static constexpr uint32_t FLASH_MAIN_ROM = 0x10000;  // GS105 source in flash
    static constexpr size_t MAIN_ROM_SIZE = 0x8000;

    SoundChip_NeoGS(EmulatorContext* context, const NeoGSConfig& config, size_t sampleRate = 44100);
    ~SoundChip_NeoGS() override;

    SoundChip_NeoGS() = delete;
    SoundChip_NeoGS(const SoundChip_NeoGS&) = delete;
    SoundChip_NeoGS& operator=(const SoundChip_NeoGS&) = delete;

    /// region <GeneralSoundCard>
    void reset() override;       // cold boot of the whole card
    void resetCard() override;   // host #33 = 100: FPGA registers + CPU, from the loader
    void hostReset() override;   // any emulator reset is a cold boot (§3.4)
    void loadROM(const std::string& flashPath) override;

    void handleFrameStart() override;
    void handleFrameEnd(size_t expectedSamples = 0) override;
    void onEmulatorPaused() override;

    int16_t* getBuffer() override { return _buffer; }
    int16_t* getAuxBuffer() override { return _mp3 ? _mp3Buffer : nullptr; }
    bool hadAuxAudioActivityLastFrame() const override { return _mp3WasActive; }
    void setSampleRate(size_t sampleRate) override;
    void setSynthesisSuppressed(bool suppressed) override;
    bool isSynthesisSuppressed() const override { return _synthesisSuppressed; }

    uint8_t readStatus() override;
    uint8_t readData() override;
    void sendCommand(uint8_t cmd) override;
    void sendData(uint8_t data) override;
    void triggerNMI() override;

    uint8_t getStatusRaw() const override { return _mb.status; }
    uint8_t getDataFromHost() const override { return _mb.dataFromHost; }
    uint8_t getDataToHost() const override { return _mb.dataToHost; }
    uint8_t getCommandFromHost() const override { return _mb.commandFromHost; }
    size_t getCommandQueueCount() const override { return (_mb.status & 0x01) ? 1 : 0; }
    size_t getDataQueueCount() const override { return (_mb.status & 0x80) ? 1 : 0; }
    uint8_t getMPAG() const override { return _mem.lastMpag(); }
    uint8_t getChannelSample(int channel) const override { return _snd.sample(channel); }
    uint8_t getChannelVolume(int channel) const override { return _snd.volume(channel); }
    bool isROMLoaded() const override { return _flashLoaded; }
    size_t getRamSizeKB() const override { return _mem.ramSize() / 1024; }
    bool hadAudioActivityLastFrame() const override { return _wasActive; }
    int channelCount() const override { return NeoGSSound::CHANNELS; }
    bool isReadyForCommands() const override;

    bool hasCoprocessor() const override { return true; }
    GSCardImplementation implementation() const override { return GSCardImplementation::NGS; }
    std::string deviceDescription() const override;
    bool isCPUHalted() const override { return _cpu && Z80CpuHalted(_cpu) != 0; }
    uint16_t getCPUReg(GSCpuRegister reg) const override;

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

    GSForwardMailbox snapshotMailbox() const override;
    void restoreMailbox(const GSForwardMailbox& snapshot) override;
    void accumulateActivityCounters(const GSActivityCounters& other) override;
    bool captureModuleUpload(std::vector<uint8_t>& bytes, bool& playing) const override;
    void replayModuleUpload(const std::vector<uint8_t>& bytes, bool startPlayback) override;
    /// endregion </GeneralSoundCard>

    // PortDevice (host ports #B3/#BB/#33)
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;

    /// region <TTDSerializable>
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::NeoGS; }
    std::string TTDDeviceName() const override { return "NeoGS"; }
    /// Hashes the machine-visible state: registers, devices, not RAM, flash
    /// or decoded audio
    uint64_t TTDHashState() const override;
    /// endregion </TTDSerializable>

    /// region <NeoGS introspection (automation, debugger, tests)>
    uint8_t gscfg0() const { return _gscfg0; }
    uint8_t pageRegister(int window) const { return _mem.page(window); }
    bool windowIsFlash(int window) const { return _mem.isFlash(window); }
    bool ledOn() const { return (_led & 1) == 0; } // #01 d0: 0 = on
    uint32_t cardClockHz() const { return static_cast<uint32_t>(TICKS_PER_SECOND / static_cast<double>(ticksPerCycleFor(_gscfg0))); }
    int64_t cardTicks() const { return _runner.now(); }
    const NeoGSInterrupts& interrupts() const { return _irq; }
    const NeoGSSpi& spi() const { return _spi; }
    const NeoGSDma& dma() const { return _dma; }
    const NeoGSZxDma& zxDma() const { return _zx; }
    NeoGSSpi& spi() { return _spi; }
    NeoGSMemory& memory() { return _mem; }
    const NeoGSMemory& memory() const { return _mem; }
    Flash29F040B& flash() { return _flash; }
    const NeoGSConfig& config() const { return _config; }
    const std::string& flashTitle() const { return _flashTitle; }
    uint8_t peek(uint16_t addr) const { return _mem.peek(addr); }
    void poke(uint16_t addr, uint8_t value) { _mem.poke(addr, value); }
    /// Flash persistence ([NGS] FlashWrite, neogs-tdd.md §5.8). In persist
    /// mode a modified flash is saved as neogs-flash-<sha256 of the shipped
    /// image>.rom in the writable folder, and loaded next time in its place;
    /// the shipped image is never modified. saveFlash() also works on demand.
    bool saveFlash() override;
    const std::string& flashPersistPath() const { return _flashPersistPath; }
    /// Where the persisted copy lives (tests and automation may redirect it;
    /// set before loadROM)
    void setFlashPersistFolder(const std::string& folder) { _flashPersistFolder = folder; }

    /// SD card slot: insert an image (settings from [NGS]), eject, inspect
    bool insertSdCard(const std::string& path) override;
    bool ejectSdCard() override;
    std::string sdCardImage() const override { return sdCardPresent() ? _sd->path() : std::string(); }
    bool neogsState(NeoGSStateInfo& out) const override;
    bool peekCardMemory(uint16_t addr, uint8_t& out) const override
    {
        out = _mem.peek(addr);
        return true;
    }
    std::string firmwareDescription() const override
    {
        return _flashLoaded ? _flashTitle + (_flash.modified() ? " (modified)" : "") : "Missing (no flash image)";
    }
    SdCardSpi* sdCard() { return _sd.get(); }
    Vs10xxDecoder* mp3Decoder() { return _mp3.get(); }
    bool sdCardPresent() const { return _sd && _sd->present(); }

    /// Run the card by itself for `ticks` (tests, module replay)
    void runFor(int64_t ticks) { _runner.runTo(_runner.now() + ticks); }
    /// endregion

    /// TTD blob (neogs-tdd.md §7.4, TTD v1: every checkpoint carries it all):
    /// the fixed card state, the SD card's protocol, the decoder, the DMA
    /// modules, ZX-DMA, then RAM and flash contents
    static constexpr size_t TTD_FIXED_STATE_SIZE = 256;
    static constexpr size_t TTD_SD_OFFSET = TTD_FIXED_STATE_SIZE;
    static constexpr size_t TTD_MP3_OFFSET = TTD_SD_OFFSET + SdCardSpi::STATE_SIZE;
    static constexpr size_t TTD_DMA_OFFSET = TTD_MP3_OFFSET + Vs10xxDecoder::STATE_SIZE;
    static constexpr size_t TTD_ZX_OFFSET = TTD_DMA_OFFSET + NeoGSDma::STATE_SIZE;
    static constexpr size_t TTD_DEVICE_STATE_END = TTD_ZX_OFFSET + NeoGSZxDma::STATE_SIZE;
    static constexpr uint8_t TTD_LAYOUT = 3; // 3: + ZX-DMA (neogs-zxdma-design.md §5.9)

private:
    friend class GSCardRunner<SoundChip_NeoGS>;
    template <class Card>
    friend GSModuleReplayResult gsReplayModuleUpload(Card&, const std::vector<uint8_t>&, bool, size_t*);

    // GSCardRunner hooks
    static constexpr bool kCanStall = true;
    bool nmiPending() const { return _nmiPending; }
    void onNmiAccepted();
    bool intLine() const { return _irq.intLine(); }
    void onIntAccepted();
    void runEvents(int64_t now);
    int64_t unitsPerCycle() const { return _ticksPerCycle; }
    void onStep()
    {
        _activityCounters.cpuSteps++;
        _ticksPerCycle = _nextTicksPerCycle; // a GSCFG0 clock change applies from the next instruction
    }
    void reschedule();
    void scheduleTimer(int64_t tickCrystal);

    // GSModuleReplay hooks
    void replayAdvanceFrame();
    void replayDrainReply();
    void replayRunFor(int64_t ticks) { runFor(ticks); }
    int64_t replayByteStepUnits() const { return 2 * 3200; }                   // two timer periods
    int64_t replayMaxByteWaitUnits() const { return 200 * 239602 * int64_t{10}; } // as the classic card, in ticks

    // Z80 bus callbacks
    static uint8_t memReadCb(Z80CPU* cpu, uint16_t addr, int m1State, void* userData);
    static void memWriteCb(Z80CPU* cpu, uint16_t addr, uint8_t value, void* userData);
    static uint8_t portReadCb(Z80CPU* cpu, uint16_t port, void* userData);
    static void portWriteCb(Z80CPU* cpu, uint16_t port, uint8_t value, void* userData);
    static uint8_t intVectorCb(Z80CPU* cpu, void* userData);

    uint8_t readMem(uint16_t addr)
    {
        const uint8_t value = _mem.read(addr, _runner.now());
        if ((addr & 0xE000) == 0x6000)
            dacCapture(addr, value);
        return value;
    }
    void dacCapture(uint16_t addr, uint8_t value);
    uint8_t cardIn(uint16_t port);
    void cardOut(uint16_t port, uint8_t value);

    // Reset paths
    enum class ResetKind : uint8_t { None, Card, Cold };
    void coldBoot();         // power-on: everything, then the boot mode
    void fpgaReset();        // FPGA registers (the #33 / J1 reset)
    void applyBootMode();
    void requestReset(ResetKind kind);
    void writeGscfg0(uint8_t value);
    void writeSctrl(uint8_t value);

    // NeoGSDma::Host
    void dmaStall(int64_t units) override { _runner.stall(units); }
    int64_t dmaUnitsPerCycle() const override { return _ticksPerCycle; }
    void dmaSdByteDone(uint8_t received) override { _spi.state(NeoGSSpi::SD).rx = received; }

    // NeoGSZxDma::Host (neogs-zxdma-design.md §5.5)
    void zxCatchUp() override { flush(); }
    bool zxHostNowUnits(int64_t& now) const override;
    double zxUnitsPerHostT() const override;
    void zxAddHostWait(uint32_t tStates) override;
    int64_t zxUnitsPerCycle() const override { return _ticksPerCycle; }
    void zxStall(int64_t units) override { _runner.stall(units); }
    int64_t zxStallUntil() const override { return _runner.stallUntil(); }
    bool zxInstall(bool installed) override;
    uint32_t zxFrame() const override { return currentFrameNumber(); }
    void zxReschedule() override { reschedule(); }
    void zxLateStart(int64_t units) override;
    void zxTrace(bool write, uint32_t address, uint8_t value) override;

    // Lazy sync with the host
    void flush();
    /// A host GS port access: catch up, and let ZX-DMA renew its watch window
    void hostPortSync()
    {
        flush();
        _zx.onHostPortAccess();
    }
    int64_t crystalNow() const { return (_runner.now() - _phaseOrigin) / TICKS_PER_CRYSTAL; }
    int64_t crystalToTicks(int64_t crystal) const { return _phaseOrigin + crystal * TICKS_PER_CRYSTAL; }

    // Audio
    void dacSideEvent(int64_t crystal);
    int64_t blipPosition() const { return _runner.now() / TICKS_PER_CRYSTAL - _frameStartTicks / TICKS_PER_CRYSTAL; }
    int64_t blipFrameLength() const
    {
        return (_frameStartTicks + _frameTicks) / TICKS_PER_CRYSTAL - _frameStartTicks / TICKS_PER_CRYSTAL;
    }

    // Host mailbox writes (ports and automation)
    void onHostDataWrite(uint8_t value);
    void onHostCommandWrite(uint8_t value);

    void traceEvent(GSTraceSide side, uint16_t port, uint8_t value, bool isOut, uint8_t channel = 0, uint8_t extraFlags = 0);
    uint32_t currentFrameNumber() const;
    void markReplayBarrier(ttd::TTDExternalEventKind kind, const char* reason);
    bool ttdRecording() const;
    bool openSdImage(const std::string& path); // no TTD guard: the configured card at construction
    void markSdWrite();
    void serializeFixedState(uint8_t* dst) const;
    void serializeDeviceState(uint8_t* dst, bool machineVisibleOnly) const;

    EmulatorContext* _context;
    NeoGSConfig _config;
    size_t _sampleRate;

    Z80CPU* _cpu = nullptr;
    Flash29F040B _flash;
    NeoGSMemory _mem;
    NeoGSInterrupts _irq;
    NeoGSSound _snd;
    NeoGSSpi _spi;
    std::unique_ptr<SdCardSpi> _sd;
    std::unique_ptr<Vs10xxDecoder> _mp3; // absent with MP3Support=none
    NeoGSDma _dma;
    NeoGSZxDma _zx{*this, _dma, _mem};
    bool _zxLateStartLogged = false;
    GSForwardMailbox _mb;
    GSCardRunner<SoundChip_NeoGS> _runner;

    bool _flashLoaded = false;
    std::string _flashTitle;
    std::string _flashPersistFolder;  // empty: FileHelper::GetWritablePath()
    std::string _flashPersistPath;    // the persisted copy for the loaded image
    bool _mainRomV111 = false;

    // FPGA registers not owned by a component
    uint8_t _gscfg0 = GSCFG0_RESET;
    uint8_t _port09Bit5 = 0;
    uint8_t _led = 0;

    // Timing
    int64_t _ticksPerCycle = 12;      // in effect for the current instruction
    int64_t _nextTicksPerCycle = 12;  // after a GSCFG0 write
    int64_t _phaseOrigin = 0;         // ticks at power-on: crystal phase 0
    int64_t _nextTimerCrystal = 0;    // next regular tick, crystal clocks
    int64_t _timerStrobeAt = 0;       // ... reaching the controller (ticks)
    int64_t _extraStrobeAt = 0;       // TIM_FREQ switch edge (INT64_MAX: none)
    int64_t _nextDacCrystal = 0;      // next mixer side, crystal clocks
    bool _nmiPending = false;
    ResetKind _resetRequest = ResetKind::None;

    // Ready detection (§7.1)
    bool _ready = false;
    int64_t _mainRomStartTicks = -1;  // when GSCFG0 = #23 was first written

    // Host frame bases
    uint64_t _frameStartZxTacts = 0;
    int64_t _frameStartTicks = 0;
    int64_t _frameTicks = 0;

    // Audio
    AudioFrameDescriptor _audioDescriptor;
    int16_t* const _buffer = reinterpret_cast<int16_t*>(_audioDescriptor.memoryBuffer);
    GSAudioOut _audio;
    bool _synthesisSuppressed = false;
    int32_t _outL = 0;
    int32_t _outR = 0;
    bool _frameHadActivity = false;
    uint32_t _sdWriteMarkerFrame = UINT32_MAX; // last frame with an SD-write TTD marker
    bool _wasActive = false;
    AudioFrameDescriptor _mp3Descriptor;
    int16_t* const _mp3Buffer = reinterpret_cast<int16_t*>(_mp3Descriptor.memoryBuffer);
    bool _mp3WasActive = false;

    GSUploadCapture _upload;
    GSActivityCounters _activityCounters;
    GSPortTraceRecorder _portTrace;
};
