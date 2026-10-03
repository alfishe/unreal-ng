#pragma once
#include "stdafx.h"

#include <memory>
#include <string>
#include <vector>

#include "emulator/cpu/z80.h"
#include "emulator/io/rtc/ds12887.h"
#include "emulator/io/sprinter/isa/sprinterisabus.h"
#include "emulator/io/z84c15/z84c15engine.h"
#include "emulator/machineeventjournal.h"
#include "emulator/memory/sprinter/sprinteraccelerator.h"
#include "emulator/memory/sprinter/sprinterwaits.h"
#include "emulator/ports/models/sprinter/sprinterinput.h"
#include "emulator/ports/models/sprinter/sprinterpldconfiguration.h"
#include "emulator/ports/models/sprinter/sprinterpldstate.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/sprinter/covoxblaster.h"
#include "emulator/video/sprinter/sprinterintsource.h"
#include "emulator/video/sprinter/sprintervideoram.h"
#include "emulator/video/sprinter/sprintervramregion.h"

class ScreenSprinter;
class SprinterMemory;
class SprinterVideoRenderer;
namespace ttd
{
class TTDSprinterPld;
}

/// Peters Plus Sprinter Sp2000 port decoder: owns the PLD state
/// (docs/inprogress/2026-09-28-sprinter/tdd-ports-memory.md).
///
/// Every port access that the Z84C15 does not answer itself reads one byte of
/// RAM page #40 - the port table the BIOS writes at start-up - and that byte,
/// the internal code, picks the device (decision D1, MAME sprinter.cpp:584):
///
///   index = CNF[4:3] << 12 | PN5 << 11 | /DOS << 10 | /WR << 9 | A15 A14 << 7
///         | A6 A5 << 5 | A13 << 4 | A7 << 3 | A2 A1 A0
///
/// Worked example (MAN p. 28): port #7785, write, DOS on, PN5 = 0, map 0:
/// A15..A13 = 0 1 1, A7 = 1, A6 A5 = 0 0, A2..A0 = 1 0 1 -> index #009D.
///
/// What the decoder owns besides the table lookup:
///   - the PLD cells #C0-#FF and registers (SprinterPldState);
///   - the start-up gate: after a PLD reset window 3 shows page #40 and port
///     writes are ignored until the first port read ("DCP opened");
///   - the configuration loader: the ROM loader's 473 720 writes go to a sink
///     (full start, the user default) or the machine starts configured
///     ([SPRINTER] FastStart=1, the test default) - tdd-ports-memory §6;
///   - the configuration modules (SprinterPldConfiguration): the active module
///     is asked first for every code and for the bank mapping, Standard after;
///   - the CPU: the Z84C15 on its own library (core/src/3rdparty/z84c15) - the
///     chip's on-chip ports and registers, and the engine adapter that runs the
///     CPU on it (Z84C15Engine, installed with the INT source behind the chip's
///     daisy chain);
///   - the keyboard and the serial mouse on the chip's SIO (SprinterInput: the
///     host's PS/2 sink, the keyboard INT, Ctrl+Alt+Del, the F12 turbo switch);
///   - the Covox / Covox-Blaster DAC (codes #88 / #89, page #FD; CovoxBlaster,
///     mixed by SoundManager in the COVOX slot) - the AY is the shared chip on
///     codes #90 / #91 / #52;
///   - the DS12887A CMOS (codes #1C/#1D/#1E, century #32),
///     the video RAM and the INT source (the mode table);
///   - the 21 MHz turbo (hw_turbo_ratio 6) and its wait states (SprinterWaits), and the ZX mode's
///     "original waits" at 3.5 MHz (SprinterOrigWaits).
class PortDecoder_Sprinter : public PortDecoder, public IMachineStepHook, public IMachineM1Hook
{
public:
    /// Port trace internal codes: the PLD codes #00-#FF as they are; the
    /// Z84C15's own ports as kTraceZ84Base + the low address byte
    static constexpr uint16_t kTraceZ84Base = 0x100;
    /// Port trace internal codes of ISA cycles (they are memory cycles in window 3, shown beside the port
    /// accesses): kTraceIsaBase + (memory ? 2 : 0) + slot index; the decoded port is the ISA address's low
    /// 16 bits, the raw port the CPU address (Sprinter ISA tdd §10)
    static constexpr uint16_t kTraceIsaBase = 0x200;
    /// The Z84C15's time base: the board crystal X_SP = 42 MHz, 12 ticks per base T-state (3.5 MHz = X_SP / 12)
    static constexpr uint32_t kChipTicksPerBaseT = 12;
    static constexpr uint64_t kChipClockHz = 42'000'000;
    /// CTC TRG0-TRG2: X_SP / 48 = 875 kHz, independent of the CPU clock (MAME sprinter.cpp:1993-1995)
    static constexpr uint32_t kCtcTriggerHz = 875'000;

    /// region <Constructors / Destructors>
public:
    PortDecoder_Sprinter() = delete;
    explicit PortDecoder_Sprinter(EmulatorContext* context);
    ~PortDecoder_Sprinter() override;
    /// endregion </Constructors / Destructors>

    /// region <PortDecoder interface>
public:
    /// Core::Reset: the RESET button (and, the first time, power-on): the PLD
    /// loads its configuration again (or starts configured with FastStart=1);
    /// RAM, fast RAM, the cells and the CMOS are kept
    void reset() override;
    void PowerCycle() override;
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;

    /// The #7FFD lock is the port table's PN5 bit: the PLD keeps no lock latch
    bool IsPagingLocked() const override { return false; }
    /// 3.5 or 21 MHz
    uint8_t TtdClockUnits() const override { return 6; }
    /// The IM2 vector (the Z84C15 daisy chain, the PLD's INT answering #FF) and the stepped engines (PLD
    /// resets, the loader watchdog, the CTC) follow the checkpointed state and the TTD input journal only
    /// (s7-ttd-outcome.md): the port journals record on the Sprinter
    bool TtdEnginesSealed() const override { return true; }
    /// A session recorded with another ISA slot population is refused (blob 33's kinds against the fitted cards)
    bool TtdSessionMatches(const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs, std::string& why) const override;
    /// No ZX-bus until the ISA ZX-bus adapter exists (2026-10-02-sprinter-isa/tdd.md §2, phase I2): the
    /// General Sound / NeoGS of [SOUND] GSType is not fitted
    bool ZxBusPresent() const override { return false; }
    /// The PLD journal (PldJournal below)
    MachineEventJournal* GetMachineEventJournal() override { return &_journal; }
    /// The WD1793 clock and data separator follow the #BD density latch (codes #16 / #17)
    /// alone: STEP and DRQ never change them, [Beta128] TurboVG= cannot override them
    FdcClockPolicy DefaultFdcClockPolicy() const override { return FdcClockPolicy::Latched; }
    /// Code #15 (port #FF in TR-DOS, #1F / #0F outside it) carries the Kempston bits
    bool HasKempstonJoystick() const override { return true; }

    std::vector<ttd::PeripheralId> GetTTDModelStateIds() const override;
    /// Port code #58 reads the board mouse (SprinterInput::ReadMouseView)
    bool PeekMouseRegister(uint8_t reg, uint8_t& value) const override
    {
        static constexpr uint16_t kPorts[3] = {0xFADF, 0xFBDF, 0xFFDF};
        value = reg < 3 ? _input.ReadMouseView(kPorts[reg]) : 0xFF;
        return true;
    }
    std::vector<std::unique_ptr<ttd::TTDSerializable>> CreateTTDSerializers() const override;
    std::vector<PortTraceCodeName> GetPortTraceCodeTable() const override;
    RtcBinding GetRtcBinding() override;
    /// endregion </PortDecoder interface>

    /// region <IMachineM1Hook: the TR-DOS signal>
    /// The PLD's /DOS follows the opcode fetches (MAME map_fetch, sprinter.cpp:1383-1400):
    /// a fetch from #3D00-#3DFF with #7FFD bit 4 set turns DOS on, a fetch from #4000 up
    /// turns it off, before the opcode is read. The BIOS relies on it: its calls go through
    /// #3D13 (ToBios3D13), where the port table's "DOS on" half answers
    void BeforeMachineM1(uint16_t address) override;
    void OnMachineM1(uint16_t address) override { (void)address; }
    /// endregion

    /// region <IMachineStepHook: PLD-driven CPU resets, the loader watchdog>
    void OnMachineStep(uint32_t t) override;
    void OnMachineFrameRollover(uint32_t frameLength) override;
    /// endregion

    /// The frame boundary: the host speed control's next multiplier reaches the Z84C15's clock
    void OnFrameEnd() override;

    /// region <PLD state and parts>
public:
    SprinterPldState& GetPldState() { return _pld; }
    const SprinterPldState& GetPldState() const { return _pld; }
    SprinterVideoRam& GetVideoRam() { return _vram; }
    /// The PC the decoder got with the I/O in progress (the Z84C15 library keeps no m1_pc)
    uint16_t IoPc() const override { return _pc; }
    /// The video RAM by name on every automation interface (devicememory.h)
    void CollectMemoryRegions(std::vector<IDeviceMemoryRegion*>& out) override { out.push_back(&_vramRegion); }
    SprinterIntSource& GetIntSource() { return _intSource; }
    Z84Lib::Z84C15& GetZ84() { return _z84; }
    SprinterInput& GetInput() { return _input; }
    /// The two ISA-8 slots (Sprinter ISA tdd §4): window 3 in ISA mode reaches them, code #1B writes the latch
    SprinterIsaBus& GetIsaBus() { return _isaBus; }
    const SprinterIsaBus& GetIsaBus() const { return _isaBus; }
    Ds12887& GetRtc() { return _rtc; }
    SprinterPldConfigurationRegistry& GetRegistry() { return _registry; }
    SprinterPldConfiguration& ActiveModule() { return _registry.At(_pld.configModule < _registry.Count() ? _pld.configModule : 0); }
    const SprinterPldConfiguration& ActiveModule() const { return _registry.At(_pld.configModule < _registry.Count() ? _pld.configModule : 0); }
    /// The standard configuration's accelerator (owned here, supplied by SprinterPldStandard, hook 4)
    SprinterAccelerator& StandardAccelerator() { return _accelerator; }
    /// The accelerator in use: the active module's, Standard's when it brings none; null while the
    /// PLD is not configured. Its state (mode, length, function, buffer, INT block) is what the
    /// debugger and automation show (SprinterAccelerator::State, ModeName, FunctionName)
    SprinterAccelerator* GetAccelerator() const { return _activeAccelerator; }
    /// The picture of the active module (hook 3), Standard's when it brings none
    const SprinterVideoRenderer& VideoRenderer() const;

    /// Port table index and code (§3.1)
    uint16_t LookupIndex(uint16_t port, bool isRead) const;
    uint8_t LookupCode(uint16_t port, bool isRead) const;
    /// Window 3's cell index from #7FFD / #1FFD / CNF (MAME update_memory pg3)
    static uint8_t ComputePg3(const SprinterPldState& pld);

    /// The standard configuration's code semantics (reached through SprinterPldStandard)
    uint8_t StandardReadCode(uint8_t code, uint16_t port);
    void StandardWriteCode(uint8_t code, uint16_t port, uint8_t value);

    /// Re-derive window 3's cell and map the windows
    void UpdateBanks();

    /// Density latch (codes #16 / #17): what the WD1793 runs at
    bool IsFdcHighDensity() const { return _pld.fdcHd != 0; }
    /// Called by SprinterMemory after every remap: the wait-state slots
    void OnBanksChanged();

    /// A memory write while the PLD loads (SprinterMemory's write intercept)
    void OnConfigurationWrite(uint8_t value);
    /// The PLD resets the CPU at the next instruction boundary
    void RequestCpuReset(SprinterResetKind kind);
    /// SprinterMemory: a CPU write to page #A0 with #1FFD = #10 (the soft restart): journaled, then the reset
    void OnResetPageWrite();

    /// Start the configured standard machine at the BIOS entry with the state
    /// the ROM loader would leave (fast start, tdd-ports-memory §6)
    void FastStart();
    /// Begin a full start: the CPU runs the ROM loader into the sink
    void BeginLoading();

    /// Statistics for the boot tests and the debugger: frame and PC of the
    /// first port read after the last PLD reset ("DCP opened"), -1 = not yet
    int64_t DcpOpenedFrame() const { return _dcpOpenedFrame; }
    uint16_t DcpOpenedPc() const { return _dcpOpenedPc; }
    /// Code #89 (Covox-Blaster control): the last value written
    uint8_t CblControl() const { return _cbl.State().control; }
    /// The turbo wait overlay (SprinterWaits; null until the CPU exists): the windows it marks
    /// (SlotWaits) are the ones whose accesses wait at 21 MHz - automation reports it
    const SprinterWaits* GetWaits() const { return _waits.get(); }
    /// The "original waits" overlay (SprinterOrigWaits): installed while ALL_MODE bit 2 = 0 and the CPU runs
    /// at 3.5 MHz on the configured PLD; automation reports whether it is (OrigWaitsActive)
    const SprinterOrigWaits* GetOrigWaits() const { return _origWaits.get(); }
    bool OrigWaitsActive() const;
    /// The original waits follow ALL_MODE bit 2, the clock and #7FFD bit 2 (window 3): install or remove the
    /// overlay, mark its windows. Called on every change of those; public for a PLD state set directly (tests)
    void ApplyOrigWaits();
    /// The Covox / Covox-Blaster DAC (S6, tdd-accel-sound-input §2)
    CovoxBlaster& GetCovoxBlaster() { return _cbl; }
    const CovoxBlaster& GetCovoxBlaster() const { return _cbl; }
    /// A CPU or accelerator store into RAM page #FD (SprinterMemory's write intercept): the PLD's
    /// CBL_WR page term - an accelerator copy (ACC_DIR bit 1) while the Covox-Blaster INT is on
    void OnCblPageWrite(uint16_t addr, uint8_t value);
    /// Base T-state (3.5 MHz) of the CPU within the frame: the time base of the frame-locked devices
    uint32_t BaseTstate() const;
    SprinterMemory* GetSprinterMemory() const { return _sprinterMemory; }
    /// endregion </PLD state and parts>

    /// region <PLD journal (machineeventjournal.h; tdd-zx-mode.md §12)>
public:
    /// What changed the PLD's setup, with frame, T and PC: port table writes (one event per frame, with
    /// the decodes of the key ZX ports it changed), CNF/SYS (turbo request, map, clean rules), the CPU
    /// clock, ALL_MODE, RGMOD, HOLD, the frame length, #7FFD / #1FFD (on a change of the value), the
    /// bitstream load and the module chosen, F12, Ctrl+Alt+Del, the page #A0 reset, RESET, power on.
    /// On by default; off costs nothing (no table watch, no event built)
    MachineEventJournal& PldJournal() { return _journal; }
    void SetPldJournalEnabled(bool on);
    /// SprinterMemory's write intercept: a store into page #40 (the port table) while the journal is on
    void OnPortTableWrite(uint16_t addr);
    /// The table codes of the key ZX ports (SprinterZxPorts) for every map, DOS state and direction
    /// (PN5 = 0): the journal's "what a table write changed"
    std::vector<uint8_t> KeyPortDecodes() const;
    /// endregion

    /// region <TTD (phase S7; debugger/ttd/sprinter/ttdsprinter.h)>
public:
    /// A TTD serializer loaded part of the machine's state: re-derive what follows from it - the
    /// engine's boundary hand-over, the turbo and its wait overlay, the bank windows, the step hook
    void OnTtdStateLoaded();

    /// The standard block accelerator's state (SprinterAccelState: mode, length, function, the INT block,
    /// the alternate addressing, the 256-byte buffer, the counters), carried in the PLD blob
    /// (tdd-integration §2.1). A module's own accelerator travels in its module state. The write
    /// in progress between BeforeWrite and AfterWrite is inside one bus cycle, never at a boundary
    size_t AccelStateSize() const { return sizeof(SprinterAccelState); }
    void SaveAccelState(uint8_t* dst) const;
    void LoadAccelState(const uint8_t* src);
    /// endregion </TTD>

private:
    friend class ttd::TTDSprinterPld;  ///< the PLD blob carries the decoder fields below SprinterPldState

    void PowerOn();
    /// The PLD's own reset of its registers (MAME machine_reset); `kind` says which reset
    void ResetPld(SprinterResetKind kind);
    /// The CPU's /RESET: PC, I, R, IM, IFF; the other registers keep their values
    /// (the BIOS reads IX / IY the loader left)
    void ResetCpu();
    void PerformPendingReset();
    void FinishLoad(bool watchdog);
    void ApplyTurbo();
    /// The Z84C15's clock: ticks of the 42 MHz crystal since the machine's power-on (monotonic, real time)
    uint64_t ChipClock() const;
    /// The CPU clock the Z84C15 counts (CTC timers, watchdog) at `multiplier` x 3.5 MHz
    void SyncChipClock(uint8_t multiplier);
    void AddPortWait();
    void RefreshStepHook();
    void InstallHooks();
    /// Ask the active module for its accelerator (hook 4) and make it the CPU's bus agent
    void RefreshAccelerator();
    /// The renderer draws the beam up to now before a change to the picture
    void CatchUpScreen();
    /// A CPU write is about to change a video RAM byte (the graphics pages, the Spectrum screen shadow, the
    /// accelerator): the renderer draws the beam up to the moment the byte lands (ScreenSprinter::CatchUpToWrite)
    void CatchUpScreenToWrite();
    /// A border write: the renderer draws the beam up to the moment the PLD latches it (ScreenSprinter::CatchUpToBorderLatch)
    void CatchUpScreenToBorderLatch();
    /// The context's screen as a ScreenSprinter while a CPU runs it, else null
    ScreenSprinter* SprinterScreen();
    /// A video latch changed (RGMOD, HOLD, PORT_Y, ALL_MODE, frame height): the video change log notes it
    void NoteVideoLatches();
    void LoadFastRamImage();

    /// The journal is on and the machine runs live (a TTD replay re-executes history: nothing is noted)
    bool JournalOn() const { return _journal.Enabled() && !_context->ttdReplayActive; }
    /// Append an event at the current frame, base T and `pc`
    void JournalEvent(const char* kind, uint16_t pc, int port, int value, int previous, std::string text,
                      std::vector<std::string> details = {});
    /// The CPU's PC (host actions: keys, the RESET button)
    uint16_t CpuPc() const;
    /// The frame end: one "port_table" event for the table bytes written this frame
    void FlushPortTableWrites();

    uint8_t FdcRead(uint8_t code);
    void FdcWrite(uint8_t code, uint8_t value);
    /// The #BD density latch to the WD1793 (Latched policy): DD = 1 MHz + 250 kbit/s, HD = 2 MHz + 500 kbit/s
    void ApplyFdcDensity();
    /// The #1F operand rewrite (hardware-reference §4.4 "Fixed ports"): `IN A,(#1F)` / `OUT (#1F),A`
    /// with the operand in RAM reach the bus as port #xx0F
    uint16_t RewriteIoOperand(uint16_t port) const;

    SprinterPldState _pld{};
    SprinterPldConfigurationRegistry _registry;
    SprinterVideoRam _vram;
    /// The Sprinter screen CatchUpScreenToWrite draws on (the context's screen, checked when it changes)
    ScreenSprinter* _screen = nullptr;
    const Screen* _screenSeen = nullptr;
    SprinterVramRegion _vramRegion{_vram};
    SprinterIntSource _intSource{_context, _vram};
    /// The standard accelerator and the one in use (hook 4)
    SprinterAccelerator _accelerator{_context, _pld};
    SprinterAccelerator* _activeAccelerator = nullptr;
    Z84Lib::Z84C15 _z84;
    /// The keyboard (SIO A) and the serial mouse (SIO B)
    SprinterInput _input{_context, _z84, _intSource, _pld};
    /// The Z84C15 as the CPU's engine (created once the Z80 exists, installed by InstallHooks)
    std::unique_ptr<Z84C15Engine> _cpuEngine;
    SprinterMemory* _sprinterMemory = nullptr;
    std::unique_ptr<SprinterWaits> _waits;
    std::unique_ptr<SprinterOrigWaits> _origWaits;

    /// DS12887A: 128 cells, century register #32; battery-backed through
    /// [SPRINTER] CmosFile, so it lives with the decoder and survives resets
    Ds12887 _rtc{128};
    bool _cmosLoaded = false;
    bool _poweredOn = false;

    /// The Covox / Covox-Blaster (codes #88 / #89, page #FD), its INT through _intSource
    CovoxBlaster _cbl{_context};
    /// The ISA-8 slots and the #9FBD latch; the population comes from [ISA] at creation
    SprinterIsaBus _isaBus;
    /// An ISA cycle into the port trace (only while a capture runs)
    void TraceIsaCycle(bool write, SprinterIsaBus::Space space, int slot, uint32_t address, uint8_t value);
    uint16_t _pc = 0;             ///< PC of the I/O in progress (border writes)
    int64_t _dcpOpenedFrame = -1;
    uint16_t _dcpOpenedPc = 0;
    uint64_t _loggedUnknownCodes[4] = {};  ///< one log line per unknown code

    /// The PLD journal and what it compares against (observation only, not machine state: not in TTD)
    MachineEventJournal _journal;
    struct TableWrites
    {
        uint32_t count = 0;
        uint32_t firstT = 0;
        uint32_t lastT = 0;
        uint16_t firstPc = 0;
        uint16_t lastPc = 0;
        uint16_t firstOffset = 0;
        uint16_t lastOffset = 0;
    } _tableWrites;
    std::vector<uint8_t> _journalDecodes;  ///< KeyPortDecodes at the last port_table event (empty: none yet)
    int _journalLast7ffd = -1;             ///< the last raw #7FFD / #1FFD value journaled
    int _journalLast1ffd = -1;
};
