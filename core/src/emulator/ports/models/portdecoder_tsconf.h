#pragma once
#include "stdafx.h"

#include <memory>
#include <string>

#include "emulator/cpu/z80.h"
#include "emulator/io/sdcard/sdcardspi.h"
#include "emulator/io/spi/zcontrollerspi.h"
#include "emulator/media/mediaslot.h"
#include "emulator/memory/atm/evoavr.h"
#include "emulator/memory/hostbusoverlay.h"
#include "emulator/platforms/tsconf/tsconfdma.h"
#include "emulator/platforms/tsconf/tsconfarbiter.h"
#include "emulator/platforms/tsconf/tsconfengine.h"
#include "emulator/platforms/tsconf/tsconfinterrupts.h"
#include "emulator/platforms/tsconf/tsconfstate.h"
#include "emulator/ports/portdecoder.h"

class TsConfMemory;
class Vdac2Card;

/// TS-Conf (ZX-Evo with the TS-Labs FPGA configuration) port decoder.
///
/// Design: docs/inprogress/2026-09-27-tsconf/technical-design.md §3.7;
/// hardware facts: hardware-spec.md (sections cited in the code). Owns the
/// machine state (TsConfState) and attaches it to TsConfMemory, which maps
/// the windows from it.
///
/// Ports (every mainboard port decodes the full low byte, [V] zports.v):
///   #xxAF     TS registers, register = A[15:8] (§3)
///   #7FFD     A15 = 0, low byte #FD: 128K paging folded into PAGE3 / MEM_CONFIG (§2.3)
///   #xxFD     A15 = 1: AY (BC1 = A14)
///   #xxFE     keyboard / tape / border / beeper
///   #xxFB     Covox (write)
///   #1F..#FF  Beta-128 while DOS or FDD_VIRT[7]; #1F is the joystick otherwise (§8.2)
///   #xxF7     A8 = 1: #EFF7 and the Gluk CMOS (EvoAvr, the board's AVR) (§9)
///   #xxDF     Kempston mouse
///   #57/#77   SD card SPI: the Z-Controller registers (media slot "sd.zc")
///   #xxEF     the TS AVR firmware: 16550 COM port (#F8EF..#FFEF) and ZiFi (NetworkManager fits them)
///   Nemo IDE  checked first (TryIdePortIn / TryIdePortOut)
///
/// M1 hook (IMachineM1Hook, installed only while needed): the DOS trap
/// (#3Dxx fetch in mapped mode with ROM128 = 1), its exit (fetch at >= #4000)
/// and the auto-LCK128 opcode latch.
class PortDecoder_TSConf : public PortDecoder, public IMachineM1Hook
{
public:
    /// The board function that answers one I/O cycle
    enum class PortArm : uint8_t
    {
        ZxBus = 0,       ///< not a mainboard port
        TsRegister,      ///< #xxAF
        Paging7FFD,      ///< #FD with A15 = 0
        Ay,              ///< #FD with A15 = 1
        KeyboardBorder,  ///< #FE
        Covox,           ///< #FB
        Fdc,             ///< #1F/#3F/#5F/#7F/#FF while DOS || FDD_VIRT[7]
        Joystick,        ///< #1F otherwise
        Gluk,            ///< #F7 with A8 = 1
        Mouse,           ///< #DF
        SdData,          ///< #57
        SdConfig,        ///< #77
        ComPort,         ///< #EF
    };

    /// STATUS [2:0] VDAC_VER of the emulated firmware build ([MISC] TS_VDAC):
    /// 0 = the standard `quartus` build (no VDAC, Nemo IDE), 3 = `quartus_vdac`
    /// (5-bit), 7 = `quartus_vdac2` (hardware-spec §0.1, §3.3)
    uint8_t VdacVersion() const { return _context->config.ts_vdac & 0x07; }

    /// region <Constructors / Destructors>
public:
    PortDecoder_TSConf() = delete;
    explicit PortDecoder_TSConf(EmulatorContext* context);
    ~PortDecoder_TSConf() override;
    /// endregion </Constructors / Destructors>

    /// region <Interface methods>
public:
    void reset() override;
    void PowerCycle() override { PowerOn(); }
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;

    /// The 7FFD lock is TSConf's own latch (§2.3)
    bool IsPagingLocked() const override { return _ts.lock48 != 0; }

    /// SYS_CONFIG selects 3.5, 7 or 14 MHz
    uint8_t TtdClockUnits() const override { return 4; }

    /// #1F outside DOS answers Joystick::Read()
    bool HasKempstonJoystick() const override { return true; }

    /// #xxEF belongs to the AVR (its 16550 and ZiFi): a ZX-WiFi card does not fit
    bool ReservesLowByte(uint8_t lowByte) const override { return lowByte == 0xEF; }

    /// ZX-Bus cards fit; the serial port is the TS AVR firmware's 16550 + ZiFi
    /// (docs/inprogress/2026-10-02-tsconf-zifi/tdd.md)
    NetworkCapabilities DescribeNetwork() override;

    std::vector<ttd::PeripheralId> GetTTDModelStateIds() const override;
    /// The mouse ports read the AVR's PS/2 mouse registers
    bool PeekMouseRegister(uint8_t reg, uint8_t& value) const override
    {
        value = _evoAvr.Ps2Mouse().PeekRegister(reg);
        return true;
    }
    bool HasMachineMouse() const override { return true; }
    std::vector<std::unique_ptr<ttd::TTDSerializable>> CreateTTDSerializers() const override;

    /// region <SD card (hardware-spec §8.1)>
    /// The card is the media manager's slot "sd.zc", as on the ZX-Evo
    /// BaseConf; it outlives Core::Reset() like the board's
    SdCardSpi& GetSdCard() { return _sdCard; }
    ZControllerSpi& GetZController() { return _zc; }
    /// The VDAC2 card (FT812) on the IDE connector: present in the VDAC2
    /// build ([MISC] TS_VDAC2=1) of a binary with ENABLE_VDAC2, else nullptr
    /// (vdac2-integration-design.md §5)
    Vdac2Card* GetVdac2Card() const { return _vdac2.get(); }
    /// Insert an image file / a medium through the media manager (directly
    /// when the context has none: bare decoder tests)
    bool InsertSdCard(const std::string& path, SdCardSpi::WriteMode mode, bool writeProtect = false);
    bool InsertSdCard(std::unique_ptr<IBlockDevice> media, SdCardSpi::WriteMode mode, bool writeProtect = false);
    void EjectSdCard();
    /// endregion

    Ds12887& GetRtc() { return _evoAvr; }
    EvoAvr& GetEvoAvr() { return _evoAvr; }
    /// Changes when CRAM may have changed (port / FM-window writes, DMA, a state
    /// load): the screen rebuilds its palette only then. Direct writes into
    /// GetState().cram are seen by the whole-frame renders only
    uint32_t CramVersion() const { return _cramVersion + _dma.CramWrites(); }
    RtcBinding GetRtcBinding() override;

    void BeforeMachineM1(uint16_t address) override;
    void OnMachineM1(uint16_t address) override;
    /// endregion </Interface methods>

    /// region <TS-Conf state>
public:
    TsConfState& GetState() { return _ts; }
    const TsConfState& GetState() const { return _ts; }
    TsConfInterrupts& GetInterrupts() { return _interrupts; }
    TsConfEngine& GetEngine() { return _engine; }
    TsConfDma& GetDma() { return _dma; }
    /// Bring the engine (line starts, DRAM budget, DMA) up to the current CPU T-state
    void CatchUpEngine();

    /// Registers the picture depends on (hardware-spec §3.2): a write first
    /// brings the engine and the screen up to the write
    static bool IsVideoRegister(uint8_t reg);
    /// Engine line starts, then the screen, up to the current CPU T-state
    void FlushVideo();

    /// Classify one I/O cycle
    PortArm ClassifyPort(uint16_t port) const;

    /// Port trace internal codes: each PortArm, and #100 + n for TS register n
    /// (#xxAF) with the register's name
    static constexpr uint16_t kTraceRegisterBase = 0x100;
    std::vector<PortTraceCodeName> GetPortTraceCodeTable() const override;
    /// The register's name (V_CONFIG, PAGE3, DMA_CTRL ...), empty when not built
    static const char* RegisterName(uint8_t reg);

    /// `OUT (reg << 8 | #AF), value` - also reached through the FM window (§2.4)
    void WriteRegister(uint8_t reg, uint8_t value);
    /// `IN (reg << 8 | #AF)`: only 0x00, 0x12, 0x13, 0x27 are readable (§3.2)
    uint8_t ReadRegister(uint8_t reg);
    /// A #7FFD write (§2.3)
    void Write7FFD(uint8_t value);

    /// Re-derive everything that follows from the state (banks, hooks,
    /// overlays, clock, screen) - after a reset and a TTD restore
    void ApplyState();

    /// Power-on values: the "not reset" registers zeroed, CRAM from the
    /// firmware's power-on table, PWR_UP set (§10). The first reset() runs it
    void PowerOn();
    /// endregion </TS-Conf state>

private:
    /// FM window (§2.4): a write-only host bus overlay installed while MEN = 1
    class FmWindow : public HostBusOverlay
    {
    public:
        explicit FmWindow(PortDecoder_TSConf& owner) : _owner(owner) { observesReads = false; }
        uint8_t onRead(uint16_t, uint8_t normal, bool, bool) override { return normal; }
        void onWrite(uint16_t addr, uint8_t value, bool romPaged) override;

    private:
        PortDecoder_TSConf& _owner;
    };

    /// Cache invalidation (§2.5): a write-only overlay over the whole space,
    /// installed while any window has the cache enabled
    class CacheWriteSnoop : public HostBusOverlay
    {
    public:
        explicit CacheWriteSnoop(PortDecoder_TSConf& owner) : _owner(owner) { observesReads = false; }
        uint8_t onRead(uint16_t, uint8_t normal, bool, bool) override { return normal; }
        void onWrite(uint16_t addr, uint8_t value, bool romPaged) override;

    private:
        PortDecoder_TSConf& _owner;
    };

    /// 14 MHz write waits (TsConfArbiter): a write-only overlay installed
    /// while the CPU runs at 14 MHz
    class DramWriteWait : public HostBusOverlay
    {
    public:
        explicit DramWriteWait(PortDecoder_TSConf& owner) : _owner(owner) { observesReads = false; }
        uint8_t onRead(uint16_t, uint8_t normal, bool, bool) override { return normal; }
        void onWrite(uint16_t addr, uint8_t value, bool romPaged) override;

    private:
        PortDecoder_TSConf& _owner;
    };

    void RefreshM1Hook();
    /// Fit or remove the VDAC2 card to match the configured firmware build
    void RefreshVdac2Card();
    /// TS-BIOS settings that boot Wild Commander from the SD card ([EVO] TsBiosNvram=SDBOOT)
    void ApplyTsBiosSdBootNvram();
    void UpdateSdStatus();
    /// [HDD] IdeStall: the CPU waits for an IDE bus cycle (hardware-spec §8.3)
    void ApplyIdeStall();
    void ApplyExternalIoStall(uint16_t port, PortArm arm);
    void InstallInterrupts();
    void RefreshFmWindow();
    void RefreshCache();
    void ApplyClock();
    void ApplyVideoPage();
    void UpdateBanks();

    uint8_t FdcAccess(uint8_t port, bool isWrite, uint8_t value);
    /// #FE write: border, tape out, and the beeper bit into the sound DAC
    void PortFeOut(uint16_t port, uint8_t value, uint16_t pc);
    /// The board's one 8-bit sound DAC (hardware-spec §7, [V] sound.v)
    void DacWrite(uint8_t value);
    uint8_t DecodeF7In(uint16_t port);
    void DecodeF7Out(uint16_t port, uint8_t value);
    bool CmosReachable() const;

    static PortDecodeDisposition TraceDisposition(PortArm arm, uint16_t port);

    TsConfState _ts{};
    TsConfInterrupts _interrupts{_context, _ts};
    TsConfDma _dma{_ts, _interrupts};
    TsConfEngine _engine{_context, _ts, _interrupts, _dma};
    TsConfArbiter _arbiter{_engine};
    TsConfMemory* _tsMemory = nullptr;
    uint32_t _cramVersion = 0;
    bool _poweredOn = false;

    FmWindow _fmWindow{*this};
    CacheWriteSnoop _cacheSnoop{*this};
    DramWriteWait _dramWriteWait{*this};

    // The board's AVR behind the Gluk CMOS ports (clock, NVRAM, extension
    // registers F0-FF), shared with ATM3: lives with the decoder so the
    // contents survive Core::Reset() like the battery
    EvoAvr _evoAvr;
    bool _nvramLoaded = false;

    /// The "sd.zc" media slot
    class SdSlot : public IMediaSlot
    {
    public:
        explicit SdSlot(PortDecoder_TSConf& owner);
        const SlotDescriptor& Descriptor() const override { return _descriptor; }
        void Attach(Medium& medium) override;
        void Detach() override;
        bool IsBusy() const override;
        void SetWriteProtectSwitch(bool on) override;

    private:
        PortDecoder_TSConf& _owner;
        SlotDescriptor _descriptor;
    };

    SdCardSpi _sdCard;
    ZControllerSpi _zc;
    SdSlot _sdSlot{*this};
    bool _sdWriteProtect = false;

    /// The VDAC2 card's INT_N as the line interrupt's source on msel lines
    /// (vdac2-integration-design.md §6)
    class Vdac2LineSource : public ITsConfLineSource
    {
    public:
        explicit Vdac2LineSource(PortDecoder_TSConf& owner) : _owner(owner) {}
        bool DrivesLine(uint32_t line) const override;
        size_t TakeLineEdges(uint32_t raster, uint32_t* out, size_t max) override;

    private:
        PortDecoder_TSConf& _owner;
    };

    // The VDAC2 card: slot 1 of the SPI hub (declared after _zc, so it is
    // destroyed first; the destructor detaches it)
    std::unique_ptr<Vdac2Card> _vdac2;
    Vdac2LineSource _vdac2Lines{*this};
};
