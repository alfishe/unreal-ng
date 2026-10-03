#pragma once

#include "emulator/io/iiobusdevice.h"
#include "stdafx.h"

#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/atm2kbc.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"

/// ATM Turbo 2+ v7.10 Port Decoder
///
/// Key ports:
/// - 0x7FFD: Standard 128K paging (A15=0, A2=1, A1=0)
/// - 0xFF77: ATM control register (video mode, turbo, mem swap, INT gate)
/// - 0xFFF7: Memory manager registers for 4 windows (low byte F7, window = A15:A14)
/// - 0xEFF7: Extended control (turbo 3.5MHz, ROCACHE)
///
/// Memory Map (with ATM paging enabled):
/// - Window 0 ($0000-$3FFF): ROM or RAM per FFF7[0]
/// - Window 1 ($4000-$7FFF): RAM per FFF7[1]
/// - Window 2 ($8000-$BFFF): RAM per FFF7[2]
/// - Window 3 ($C000-$FFFF): RAM per FFF7[3]
///
/// Video Modes (pFF77 bits 4,2,1):
/// - 0: EGA 16-color 320x200
/// - 2: Hardware multicolor 320x200
/// - 3: ZX Standard 256x192
/// - 6: Text mode 80x25
///
/// See: Unreal Speccy atm.cpp, io.cpp, memory.cpp

class Atm710TurboOverlay;

/// A device on the ATM Turbo 2+ INTERNAL I/O connector (the board's "external
/// devices port"): OUT (#FB) latches an 8-bit bus address on CT0..CT7 (the
/// same latch drives the printer data and the Covox DAC), IN / OUT (#FA)
/// strobe IORD' / IOWR'. A device answers the addresses it decodes from the
/// latch (docs/inprogress/2026-10-02-atm2ioesp/reference-atm2ioesp.md).
/// No interrupt and no wait line on the connector
class PortDecoder_ATM710 : public PortDecoder
{
    /// region <Constants>
public:
    // EFF7 bit masks (values match macros in platform.h)
    static constexpr uint8_t ATM_EFF7_TURBO_3_5   = 0x10;  // Bit 4: 3.5MHz when pFF77.3=0
    static constexpr uint8_t ATM_EFF7_LOCKMEM     = 0x04;  // Bit 2: Lock memory configuration
    static constexpr uint8_t ATM_EFF7_ROCACHE     = 0x08;  // Bit 3: RAM at $0000 instead of ROM

    // FF77 bit masks (per original Unreal Speccy atm.cpp set_atm_FF77). Bit 0
    // belongs to the video-mode field only - the original's bit0-transition
    // atm_memswap() is gated behind the default-OFF "AtmMemSwap" ini option
    // and is not emulated (see Port_FF77_Out).
    static constexpr uint8_t ATM_FF77_VMODE_MASK  = 0x07;  // Bits 0,1,2: Video mode (0-7)
    static constexpr uint8_t ATM_FF77_TURBO       = 0x08;  // Bit 3: 14MHz turbo
    static constexpr uint8_t ATM_FF77_INTGATE     = 0x20;  // Bit 5: INT gate (1=pass)

    // aFF77 address bits
    static constexpr uint16_t ATM_AFF77_PEN       = 0x100; // Bit 8: Enable ATM paging
    static constexpr uint16_t ATM_AFF77_CPM       = 0x200; // Bit 9: ~CPM (0=TR-DOS mode)
    static constexpr uint16_t ATM_AFF77_PEN2      = 0x4000; // Bit 14 (A14 of #xx77): 1 = palette writes disabled ("pen2")
    /// endregion </Constants>

    /// region <Constructors / Destructors>
public:
    PortDecoder_ATM710() = delete;
    PortDecoder_ATM710(EmulatorContext* context);
protected:
    /// `v710Board`: the ATM Turbo 2+ v7.xx board itself - its keyboard
    /// controller ([ATM] Kbc=), the INTERNAL I/O connector, the #FE decode on
    /// A2..A0 and its RAM waits at 7 MHz. The ZX-Evo (ATM3) and the ATM Turbo
    /// 4.50 derive from this decoder without them
    PortDecoder_ATM710(EmulatorContext* context, bool v710Board);
public:
    virtual ~PortDecoder_ATM710();
    /// endregion </Constructors / Destructors>

    /// region <Interface methods>
public:
    void reset() override;
    void OnFrameEnd() override;

    /// Fit the keyboard controller [ATM] Kbc= / [ROM] ATM2KBC= name (power-on of
    /// the controller; a runtime firmware change); its peer stays attached
    bool ReloadKeyboardController(std::string& error);

    /// The controller's RS-232 is the machine's own serial port (Atm2Kbc)
    NetworkCapabilities DescribeNetwork() override;

    /// The keyboard controller (nullptr when [ATM] Kbc=NONE or on the ZX-Evo)
    Atm2Kbc* GetKeyboardController() const { return _kbc && _kbc->Present() ? _kbc.get() : nullptr; }

    /// The INTERNAL I/O connector (v7.10 board only): devices plug in by address
    bool HasInternalIo() const { return _v710Board; }
    void AttachIoDevice(IIoBusDevice* device);
    /// v7.10 at 7 MHz: one wait state on the WD1793's ports (#1F / #3F / #5F / #7F)
    void AddFdcTurboWait(uint16_t decodedPort);
    void DetachIoDevice(IIoBusDevice* device);
    /// The #FB latch: the bus address (TTD; not readable by the Z80 - an IN #FB is the printer status)
    uint8_t IoBusAddress() const { return _ioBusAddress; }
    void SetIoBusAddress(uint8_t address) { _ioBusAddress = address; }

    /// Install the 7 MHz RAM wait overlay (Atm710TurboOverlay) while #FF77 bit 3
    /// selects turbo, remove it otherwise (updateTurboMode, reset, a TTD
    /// restore). Nothing on the ZX-Evo
    void SyncTurboRamWaits();
    bool AreTurboRamWaitsInstalled() const { return _turboRamWaitsInstalled; }
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    /// An external ZX-bus Kempston mouse card ([INPUT] Mouse=KEMPSTON): the board has no mouse
    /// of its own. Low byte #DF, A8 / A10 select the register, not gated by the shadow ports
    bool IsPort_KempstonMouse(uint16_t port, uint8_t& outRegister) const override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;

    void SetRAMPage(uint8_t page) override;
    void SetROMPage(uint8_t page) override;

    void ApplyBootROMDefaults(ROMModeEnum mode) override;
    void UpdateModelMemoryBanks() override;
    /// endregion </Interface methods>

    /// region <Port detection>
public:
    /// region <TTD model-specific state>
    /// The ATM memory map (pFFF7), the address-swap flag and the ATM/ATM3 port
    /// latches that the model-agnostic TTDChipsetState does not carry. Declared
    /// here because this decoder owns them; PortDecoder_ATM3 inherits both.
    std::vector<ttd::PeripheralId> GetTTDModelStateIds() const override;

    /// #FF77 turbo: 3.5 or 7 MHz
    uint8_t TtdClockUnits() const override { return 2; }
    std::vector<std::unique_ptr<ttd::TTDSerializable>> CreateTTDSerializers() const override;
    /// endregion </TTD model-specific state>

    bool IsPort_FE(uint16_t port);
    bool IsPort_7FFD(uint16_t port);
    bool IsPort_FF77(uint16_t port);
    virtual bool IsPort_FFF7(uint16_t port, uint8_t& windowIndex);
    bool IsPort_EFF7(uint16_t port);
    bool IsPort_BFFD(uint16_t port);
    bool IsPort_FFFD(uint16_t port);
    bool IsBeta128Port(uint16_t decodedPort);

    // ATM palette RAM write decode (port #FF group - data-bus assisted).
    // ATM710: (port & 0x9F) == 0x9F (low byte 9F/BF/DF/FF, xpeccy atm2PortMap
    // {0x009f, 0x00ff}); ATM3 overrides with the exact #FF decode
    // ({0x00ff, 0x00ff} in evoPortMap)
    virtual bool IsPort_ATM_Palette(uint16_t port);

    // Manager circuit enable: PEN (aFF77 bit 8) - the persistent hardware
    // latch gating the xx77/xFF7 port group and the window mapping (the
    // `pen=0` branch of the original set_banks() forces all windows to the
    // last ROM page)
    bool IsDosPortsEnabled();        // DOSEN || SYSEN: CF_DOSPORTS session OR ~CPM (aFF77.9=0)
    /// The ATM IDE answers with the DOS ports (UnrealSpeccy io.cpp: CF_DOSPORTS)
    IdeAdapter::Gate IdeGate() override;

    // Palette write gate: same dos/shadow line that gates the xx77 group on
    // the machine (xpeccy marks the palette entry dos=1). ATM710 uses the
    // DOSEN || SYSEN line, ATM3 the manager/shaden line
    virtual bool IsPaletteWriteEnabled();

    /// Where the low bit of each palette channel comes from: false = the data byte (the DD scheme, 2 bits per
    /// channel), true = the port's high address byte (ZX-Evo `#BF` bit 5, 4 bits per channel)
    virtual bool PaletteLowBitsFromAddress() const { return false; }

    uint16_t decodePort(uint16_t port);
    /// endregion </Port detection>

    /// region <Port handlers>
protected:
    virtual void Port_7FFD_Out(uint16_t port, uint8_t value, uint16_t pc);
    void Port_FF77_Out(uint16_t port, uint8_t value, uint16_t pc);
    void Port_FFF7_Out(uint16_t port, uint8_t value, uint8_t windowIndex, uint16_t pc);
    virtual void Port_EFF7_Out(uint16_t port, uint8_t value, uint16_t pc);
    void Port_ATM_Palette_Out(uint16_t port, uint8_t value);

    // 7FFD store + paging work without the lock gate (so the ATM3 override
    // can apply its own EFF7-conditional lock rule, xpeccy evoOut7FFD)
    void Apply7FFDWrite(uint16_t port, uint8_t value, uint16_t pc);

    virtual void updateMemoryBanks();
    virtual void updateTurboMode();
    /// endregion </Port handlers>

    /// region <Debug methods>
protected:
    std::string Dump_7FFD_value(uint8_t value);
    std::string Dump_FF77_value(uint8_t value);
    std::string Dump_FFF7_value(unsigned value);
    std::string Dump_EFF7_value(uint8_t value);
    /// endregion </Debug methods>

    /// The v7.xx keyboard controller behind IN #FE (Atm2Kbc), when fitted
    std::unique_ptr<Atm2Kbc> _kbc;
    bool _v710Board = false;
    std::unique_ptr<Atm710TurboOverlay> _turboRamOverlay;   ///< created on the first switch to turbo
    bool _turboRamWaitsInstalled = false;
    uint8_t _ioBusAddress = 0x00;                     ///< the #FB latch (CT0..CT7)
    std::vector<IIoBusDevice*> _ioDevices;            ///< on the INTERNAL I/O connector
};
