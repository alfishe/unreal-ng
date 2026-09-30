#pragma once
#include "stdafx.h"

#include <memory>
#include <string>

#include "emulator/cpu/z80.h"
#include "emulator/io/sdcard/sdcardspi.h"
#include "emulator/io/spi/zcontrollerspi.h"
#include "emulator/media/mediaslot.h"
#include "emulator/memory/atm/evoavr.h"

#include "portdecoder_atm710.h"

/// ATM3 / PentEvo / ZX-Evo BaseConf Port Decoder
///
/// Extends ATM710 with:
/// - 4MB RAM support (256 pages)
/// - Extended 7FFD bits 5,6,7 for additional page addressing
/// - Partial FF77 decode (any port with low byte 0x77, e.g. 0xBC77)
/// - No INT gate (always passes interrupts)
/// - NMI handling maps RAM page 0xFF to window 0
/// - Additional registers: pBD, pBE, pBF
/// - Gluk clock (the AVR's MC146818 emulation, EvoAvr on Ds12887) shared with the memory manager ports:
///   data #BFF7 / address #DFF7 outside shadow (after #EFF7 bit 7),
///   data #BEF7 / address #DEF7 in shadow
/// - Z-Controller SD card (#77 chip select, #57 data; in shadow #57 with
///   A15 = 1 is the chip select): ZControllerSpi + SdCardSpi, [ZC] section
/// - Shadow = TR-DOS active or #BF bit 0: the FDC, #xx77 and the pager
///   (#xFF7 / #x7F7) answer only then; #1F is the joystick and #xx77 the
///   Z-Controller outside it
///
/// Decode rules follow the released BaseConf FPGA (pentevo
/// fpga/base_trdemu/trunk, z80/zports.v): every mainboard port decodes the
/// full low byte - see ClassifyPort().
///
/// Note: ATM3 = PentEvo = ZX-Evo BaseConf (different names, same platform)
///
/// See: Unreal Speccy io.cpp, memory.cpp, atm.cpp

class PortDecoder_ATM3 : public PortDecoder_ATM710, public IMachineM1Hook
{
public:
    static constexpr uint8_t ATM_EFF7_GLUK = 0x80;  // Bit 7: Gluk clock ports on outside shadow

    /// region <Types>
public:
    /// Which board function answers an I/O cycle. One value per decode arm of
    /// the BaseConf FPGA (fpga/base_trdemu/trunk/z80/zports.v porthit list and
    /// read mux), so every port maps to exactly one arm; `ZxBus` means the
    /// mainboard does not decode the port and ZX-Bus cards (GS, MoonSound...)
    /// see the cycle.
    enum class PortArm : uint8_t
    {
        ZxBus = 0,          ///< not a mainboard port
        KeyboardBorder,     ///< #FE / #F6 (#F6: border colors 8-15, no beeper)
        BorderAnd7FFD,      ///< #FC write: border + (A15=0) #7FFD; #FC read: #FF
        Paging7FFD,         ///< #FD with A15=0 (write); read: #FF
        Ay,                 ///< #FD with A15=1: #FFFD select/read, #BFFD data
        Eff7Gluk,           ///< #F7 outside the pager: #EFF7 and the Gluk clock ports
        Pager,              ///< #F7 in shadow with A8=1: #xFF7 / #x7F7 / #xBF7
        Atm77,              ///< #77 in shadow: ATM system port
        SdConfig,           ///< #77 outside shadow: Z-Controller chip select
        SdData,             ///< #57: Z-Controller SPI data
        Fdc,                ///< #1F/#3F/#5F/#7F/#FF in shadow: WD1793 (+ #FF palette)
        Joystick,           ///< #1F outside shadow: Kempston joystick
        Mouse,              ///< #DF: Kempston mouse
        EvoConfig,          ///< #BF
        EvoExit,            ///< #BE
        EvoReadback,        ///< #BD
        ComPort,            ///< #EF: RS-232 served by the AVR
        UlaPlus,            ///< #3B
        NemoIde,            ///< #10/#11/#30..#F0/#C8 and the #x8 aliases
        Covox,              ///< #FB write (not a porthit on the board: the DAC also lets ZX-Bus see it)
        LegacyFddLatch,     ///< legacy FPGA only: #2F/#4F/#6F/#8F in shadow, plain R/W bytes of the RAM-disk DOS
    };

    /// EmulatorState::evoTrdemu bits
    static constexpr uint8_t kTrdemuIn = 0x01;       ///< RAM page #FE is in window 0
    static constexpr uint8_t kTrdemuPending = 0x02;  ///< swap in before the next opcode fetch

    /// Classify one I/O cycle by the BaseConf decode rules
    PortArm ClassifyPort(uint16_t port, bool isWrite);

    /// Port-trace attribution of one classified cycle: the canonical port and
    /// the device the arm reaches (if-chain decoder: no rule table). The
    /// caller sets wasDecoded
    static PortDecodeDisposition TraceDisposition(PortArm arm, uint16_t port, bool isWrite);

    /// Internal port codes for the trace: every PortArm with its name
    std::vector<PortTraceCodeName> GetPortTraceCodeTable() const override;

    /// FPGA variant the ROM image expects ([EVO] Fpga=): the frozen legacy
    /// tree reads the Evo registers on #xxBE, the current "trdemu" tree on #xxBD
    bool IsLegacyFpga() const;

    /// Evo readback register selected by A12..A8 of #xxBD (trdemu) / #xxBE (legacy):
    /// fpga/base_trdemu/trunk/z80/zports.v portbdmux, fpga/baseconf/trunk portbemux
    uint8_t ReadEvoRegister(uint8_t index);

    /// region <Board NMI (fpga/base_trdemu/trunk/z80/znmi.v, zbreak.v)>
    /// Magic button (the AVR's PrintScreen NMI): released at the next frame INT
    bool RequestBoardNmi() override;
    bool OnFrameIntStartNmi() override;
    /// Z80 accepted an NMI: a board NMI forces NOP at #0066 and pages RAM #FF in
    bool OnNmiAccepted() override;
    /// DOS closes on execution from a window PROGRAMMED as RAM (atm_pager.v
    /// ram_exec_stb), not from the NMI / RAM-0 overrides mapped over a ROM window
    bool IsDosLeavingBank(uint8_t bank) const override;
    /// M1 refresh: NMI exit countdown after #xxBE, breakpoint compare
    void OnMachineM1(uint16_t address) override;
    /// Before an opcode fetch: a pending virtual-TR-DOS swap takes effect
    void BeforeMachineM1(uint16_t address) override;
    /// endregion </Board NMI>

    /// region <SD card (Z-Controller, tdd-storage-sd-ide-cd.md §2)>
    /// The card is the media manager's slot "sd.zc" (storage-manager
    /// integration-zxevo-sd.md): the manager owns the medium and applies the
    /// config ([MEDIA] sd.zc, legacy [ZC]) before the first reset. A Z80
    /// reset keeps the card and its session writes, like the board's reset
    SdCardSpi& GetSdCard() { return _sdCard; }
    ZControllerSpi& GetZController() { return _zc; }
    /// Insert an image file / any medium through the media manager (directly
    /// when the context has none: bare decoder unit tests)
    bool InsertSdCard(const std::string& path, SdCardSpi::WriteMode mode, bool writeProtect = false);
    bool InsertSdCard(std::unique_ptr<IBlockDevice> media, SdCardSpi::WriteMode mode, bool writeProtect = false);
    void EjectSdCard();

    /// ATM paging + the SD card's protocol state
    std::vector<ttd::PeripheralId> GetTTDModelStateIds() const override;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> CreateTTDSerializers() const override;
    /// endregion </SD card>
    /// endregion </Types>

    /// region <Fields>
protected:
    // The board's AVR behind the Gluk clock ports: MC146818 clock, battery-backed
    // NVRAM, EEPROM window, version / PS/2 / modes extension window. Lives with
    // the decoder so the contents survive Core::Reset() (like the real battery)
    EvoAvr _evoAvr;
    bool _nvramLoaded = false;  // [EVO] NvramFile read once, on the first reset

    // Z-Controller SD slot: the card outlives Core::Reset() like the NVRAM
    class EvoSdSlot : public IMediaSlot
    {
    public:
        explicit EvoSdSlot(PortDecoder_ATM3& owner);
        const SlotDescriptor& Descriptor() const override { return _descriptor; }
        void Attach(Medium& medium) override;
        void Detach() override;
        bool IsBusy() const override;
        void SetWriteProtectSwitch(bool on) override;

    private:
        PortDecoder_ATM3& _owner;
        SlotDescriptor _descriptor;
    };

    SdCardSpi _sdCard;
    ZControllerSpi _zc;
    EvoSdSlot _sdSlot{*this};
    bool _sdWriteProtect = false;   // the slot's write-protect switch
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    PortDecoder_ATM3() = delete;
    PortDecoder_ATM3(EmulatorContext* context);
    virtual ~PortDecoder_ATM3();
    /// endregion </Constructors / Destructors>

    /// region <Interface methods>
public:
    void reset() override;
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;

    /// The clock chip (tests, debug UI; every RTC machine has GetRtc())
    Ds12887& GetRtc() { return _evoAvr; }
    EvoAvr& GetEvoAvr() { return _evoAvr; }
    RtcBinding GetRtcBinding() override;
    /// endregion </Interface methods>

    /// region <Port detection>
public:
    /// BaseConf: the 7FFD lock bit only counts while EFF7 bit 2 (lockmem) holds
    /// the memory manager in 128K mode; in P1024 mode bits 5..7 extend the page
    bool IsPagingLocked() const override
    {
        return (_state->pEFF7 & ATM_EFF7_LOCKMEM) && (_state->p7FFD & PORT_7FFD_LOCK);
    }

    /// BaseConf clock select: 3.5, 7 or 14 MHz (updateTurboMode)
    uint8_t TtdClockUnits() const override { return 4; }

    /// ZX-Evo BaseConf turbo VG: the FPGA feeds the VG93 CLK and switches it to 2 MHz on the STEP rising
    /// edge and back to 1 MHz on the first DRQ; RCLK stays at 250 kHz (fpga/baseconf/trunk/vg93/vg93.v,
    /// fapch_zek.v). No port bit: the switching is automatic
    FdcClockPolicy DefaultFdcClockPolicy() const override { return FdcClockPolicy::AutoStepTurbo; }
    bool IsPort_FF77(uint16_t port);  // Partial decode for ATM3
    bool IsPort_37F7(uint16_t port);  // 4MB memory manager
    bool IsPort_BF(uint16_t port);    // ATM3 control
    bool IsManagerEnabled();          // CF_DOSPORTS: pBF.0 (shaden) OR ~cpm (aFF77.9=0)
    bool IsPort_BE(uint16_t port);    // ATM3 status / window readback
    bool IsPort_FFF7(uint16_t port, uint8_t& windowIndex) override;  // A11:A10=11, A8=1 (BaseConf pager)

    // ATM3 palette write decode is the EXACT #FF port (xpeccy evoPortMap
    // {0x00ff, 0x00ff, 1, ...}), unlike ATM710's 0x9F/0xBF/0xDF/0xFF group
    bool IsPort_ATM_Palette(uint16_t port) override;

    // Palette gated by the manager/shaden line (the ATM3 dos-line analog)
    bool IsPaletteWriteEnabled() override;

    // Gluk clock ports: #DFF7 / #BFF7 outside shadow (needs #EFF7 bit 7),
    // #DEF7 / #BEF7 in shadow (always on); decoded on A8/A13/A14 (zports.v)
    bool IsGlukEnabled();
    bool IsPort_CMOS_Data(uint16_t port);
    bool IsPort_CMOS_Address(uint16_t port);
    /// endregion </Port detection>

    /// region <Port handlers>
protected:
    void updateTurboMode() override;
    void Port_FF77_Out_ATM3(uint16_t port, uint8_t value, uint16_t pc);
    void Port_37F7_Out(uint16_t port, uint8_t value, uint16_t pc);
    void Port_BF_Out(uint16_t port, uint8_t value, uint16_t pc);
    void Port_BE_Out(uint16_t port, uint8_t value, uint16_t pc);
    void Port_BD_Out(uint16_t port, uint8_t value);

    // 7FFD lock honored only while EFF7 bit 2 (lockmem) keeps the manager in
    // 128K mode (xpeccy evoOut7FFD)
    void Port_7FFD_Out(uint16_t port, uint8_t value, uint16_t pc) override;


    // EFF7 z-bits (bit 0 / bit 5) fold into the video mode decode on ATM3
    // (xpeccy evoOutEFF7 -> evoSetVideoMode): re-run raster detection on change
    void Port_EFF7_Out(uint16_t port, uint8_t value, uint16_t pc) override;

    // BaseConf window mapping (fpga/base_trdemu/trunk/mem/atm_pager.v): the
    // 1 MB #7FFD page bits, #EFF7 bit 3 RAM page 0 and the NMI page override
    void updateMemoryBanks() override;

    void DecodeF7Out(uint16_t port, uint8_t value, uint16_t pc);
    /// Attach this decoder as the Z80 M1 hook only while it has work there
    /// (NMI exit countdown running or #BF breakpoint enabled)
    void RefreshM1Hook();
    /// Virtual TR-DOS (zdos.v / zports.v:797-799): for an FDC access in shadow,
    /// latch the drive number, arm the trap, and report whether the WD1793 is
    /// deselected (a masked drive's #1F-#7F accesses never reach the chip)
    bool TrdemuFdcAccess(uint8_t fdcPort, bool isWrite, uint8_t value);
    void BorderOnlyOut(uint16_t port, uint8_t value, uint16_t pc);
    /// Card presence and the slot's write-protect switch into AVR register C
    void UpdateSdStatus();
    uint8_t DecodeF7In(uint16_t port);
    /// endregion </Port handlers>
};
