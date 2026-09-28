#pragma once
#include "stdafx.h"

#include "emulator/memory/atm/cmos.h"

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
/// - CMOS (DS12885-style RTC + NVRAM) shared with the memory manager ports:
///   data #BFF7 / address #DFF7 outside shadow (after #EFF7 bit 7),
///   data #BEF7 / address #DEF7 in shadow
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

class PortDecoder_ATM3 : public PortDecoder_ATM710
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
    };

    /// Classify one I/O cycle by the BaseConf decode rules
    PortArm ClassifyPort(uint16_t port, bool isWrite);
    /// endregion </Types>

    /// region <Fields>
protected:
    // DS12885-style RTC/CMOS (BaseConf config storage). Lives with the decoder
    // so the contents survive Core::Reset() (like a battery-backed CMOS).
    CMOS _cmos;
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

    /// CMOS/RTC backing store (verification tests / debug UI - mirrors
    /// PortDecoder_Scorpion256::GetSMUCNvram())
    CMOS& GetCMOS() { return _cmos; }
    /// endregion </Interface methods>

    /// region <Port detection>
public:
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
    uint8_t Port_BE_In(uint8_t portHi);  // selected by A15..A8 of port #xBE

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
    void BorderOnlyOut(uint16_t port, uint8_t value, uint16_t pc);
    uint8_t DecodeF7In(uint16_t port);
    /// endregion </Port handlers>
};
