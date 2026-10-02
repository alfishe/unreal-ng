#pragma once
#include "stdafx.h"

#include <memory>
#include <string>
#include <vector>

#include "emulator/cpu/z80.h"
#include "emulator/io/rtc/ds12887.h"
#include "emulator/io/z84c15/z84c15.h"
#include "emulator/memory/sprinter/sprinterwaits.h"
#include "emulator/ports/models/sprinter/sprinterpldconfiguration.h"
#include "emulator/ports/models/sprinter/sprinterpldstate.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/sprinter/sprinterintsource.h"
#include "emulator/video/sprinter/sprintervideoram.h"

class SprinterMemory;
class SprinterVideoRenderer;

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
///   - the Z84C15 package, the DS12887A CMOS (codes #1C/#1D/#1E, century #32),
///     the video RAM and the INT source (the mode table);
///   - the 21 MHz turbo (hw_turbo_ratio 6) and its wait states (SprinterWaits).
class PortDecoder_Sprinter : public PortDecoder, public IMachineStepHook, public IMachineM1Hook
{
public:
    /// Port trace internal codes: the PLD codes #00-#FF as they are; the
    /// Z84C15's own ports as kTraceZ84Base + the low address byte
    static constexpr uint16_t kTraceZ84Base = 0x100;

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

    std::vector<ttd::PeripheralId> GetTTDModelStateIds() const override;
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

    /// region <PLD state and parts>
public:
    SprinterPldState& GetPldState() { return _pld; }
    const SprinterPldState& GetPldState() const { return _pld; }
    SprinterVideoRam& GetVideoRam() { return _vram; }
    SprinterIntSource& GetIntSource() { return _intSource; }
    Z84C15& GetZ84() { return _z84; }
    Ds12887& GetRtc() { return _rtc; }
    SprinterPldConfigurationRegistry& GetRegistry() { return _registry; }
    SprinterPldConfiguration& ActiveModule() { return _registry.At(_pld.configModule < _registry.Count() ? _pld.configModule : 0); }
    const SprinterPldConfiguration& ActiveModule() const { return _registry.At(_pld.configModule < _registry.Count() ? _pld.configModule : 0); }
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
    /// Called by SprinterMemory after every remap: the wait-state slots
    void OnBanksChanged();

    /// A memory write while the PLD loads (SprinterMemory's write intercept)
    void OnConfigurationWrite(uint8_t value);
    /// The PLD resets the CPU at the next instruction boundary
    void RequestCpuReset(SprinterResetKind kind);

    /// Start the configured standard machine at the BIOS entry with the state
    /// the ROM loader would leave (fast start, tdd-ports-memory §6)
    void FastStart();
    /// Begin a full start: the CPU runs the ROM loader into the sink
    void BeginLoading();

    /// Statistics for the boot tests and the debugger: frame and PC of the
    /// first port read after the last PLD reset ("DCP opened"), -1 = not yet
    int64_t DcpOpenedFrame() const { return _dcpOpenedFrame; }
    uint16_t DcpOpenedPc() const { return _dcpOpenedPc; }
    /// endregion </PLD state and parts>

private:
    void PowerOn();
    /// The PLD's own reset of its registers (MAME machine_reset); `kind` says which reset
    void ResetPld(SprinterResetKind kind);
    /// The CPU's /RESET: PC, I, R, IM, IFF; the other registers keep their values
    /// (the BIOS reads IX / IY the loader left)
    void ResetCpu();
    void PerformPendingReset();
    void FinishLoad(bool watchdog);
    void ApplyTurbo();
    void AddPortWait();
    void RefreshStepHook();
    void InstallHooks();
    /// The renderer draws the beam up to now before a change to the picture
    void CatchUpScreen();
    void LoadFastRamImage();

    uint8_t FdcRead(uint8_t code);
    void FdcWrite(uint8_t code, uint8_t value);

    SprinterPldState _pld{};
    SprinterPldConfigurationRegistry _registry;
    SprinterVideoRam _vram;
    SprinterIntSource _intSource{_context, _vram};
    Z84C15 _z84;
    SprinterMemory* _sprinterMemory = nullptr;
    std::unique_ptr<SprinterWaits> _waits;

    /// DS12887A: 128 cells, century register #32; battery-backed through
    /// [SPRINTER] CmosFile, so it lives with the decoder and survives resets
    Ds12887 _rtc{128};
    bool _cmosLoaded = false;
    bool _poweredOn = false;

    uint8_t _cblControl = 0;      ///< code #89 (Covox-Blaster, phase S6): stored for the read-back
    uint16_t _pc = 0;             ///< PC of the I/O in progress (border writes)
    int64_t _dcpOpenedFrame = -1;
    uint16_t _dcpOpenedPc = 0;
    uint64_t _loggedUnknownCodes[4] = {};  ///< one log line per unknown code
};
