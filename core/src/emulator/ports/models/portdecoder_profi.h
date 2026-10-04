#pragma once
#include "stdafx.h"

#include "emulator/emulatorcontext.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/profi/profiwaitoverlay.h"
#include "emulator/io/keyboard/profixtkbc.h"
#include "emulator/io/rtc/ds12887.h"
#include "emulator/io/ppi/ppi8255.h"
#include "emulator/io/serial/usart8251.h"
#include "emulator/io/timer/pit8253.h"
#include "emulator/ports/models/profiboard.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"

/// ZX Profi 1024 port decoder.
///
/// Behaviour follows the UnrealSpeccy reference implementation (io.cpp / memory.cpp),
/// cross-checked with ZXMAK2 and Xpeccy. Design: docs/inprogress/2026-09-21-profi/technical-design.md
///
/// Latches:
///   #7FFD  bits 2:0 RAM low, 3 screen (5|7), 4 ROM14 (1 = 48K/DOS side), 5 lock
///   #DFFD  bits 2:0 RAM high, 3 SCO, 4 WOROM, 5 CPM, 6 SCR, 7 DS80 (512x240 + palette)
///   DOS latch = CF_TRDOS (set by the $3Dxx M1 trap, cleared on fetch from >= $4000)
class PortDecoder_Profi : public PortDecoder, public IMachineStepHook
{
    /// region <Constructors / Destructors>
public:
    PortDecoder_Profi() = delete;                 // Disable default constructor; C++ 11 feature
    PortDecoder_Profi(EmulatorContext* context);
    virtual ~PortDecoder_Profi();
    /// endregion </Constructors / Destructors>

    /// region <Interface methods>
public:
    void reset() override;
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;

    void SetRAMPage(uint8_t page) override;
    void SetROMPage(uint8_t page) override;

    /// Latch-to-bank translation (called from Memory::UpdateZ80Banks after the ROM slot is chosen)
    void UpdateModelMemoryBanks() override;

    /// #1F in the NORMAL port set reads Joystick::Read() while a joystick is fitted
    bool HasKempstonJoystick() const override { return true; }

    std::vector<ttd::PeripheralId> GetTTDModelStateIds() const override;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> CreateTTDSerializers() const override;

    /// The clock chip (tests, debug UI; every RTC machine has GetRtc())
    Ds12887& GetRtc() { return _rtc; }
    Ppi8255& GetPpi() { return _ppi; }
    const ProfiBoard& GetBoard() const { return _board; }
    /// The v5 board's COM port: the 8253 baud timer and the 8251 (extended port map only)
    Pit8253& GetPit() { return _pit; }
    Usart8251& GetUsart() { return _usart; }
    /// v5: the 8251 is the machine's own serial port ([NETWORK] ComPort= puts the peer on it)
    NetworkCapabilities DescribeNetwork() override;

    /// The keyboard on the connector ([PROFI] Keyboard=, resolved for the board): Matrix, Xt or XtTable
    ProfiKeyboard GetKeyboardKind() const { return _keyboardKind; }
    /// The PROFI-XT controller when it is fitted (Xt / XtTable), else nullptr
    ProfiXtKbc* GetKeyboardController() const { return _xtKbc ? _xtKbc.get() : nullptr; }
    /// Frame end: the PROFI-XT controller runs between reads
    void OnFrameEnd() override;
    RtcBinding GetRtcBinding() override;
    /// endregion </Interface methods>

    /// region <Helper methods>
public:
    /// #7FFD: A15=0, A1=0 (A2 is not decoded)
    static bool IsPort_7FFD(uint16_t port);
    /// #DFFD: A15=1, A13=0, A1=0
    static bool IsPort_DFFD(uint16_t port);

    /// Decode a Profi FDC port for the current mode.
    /// @return canonical Beta128 port (#1F/#3F/#5F/#7F/#FF) or 0 when the address is not an FDC port
    uint16_t DecodeFDCPort(uint16_t port) const;

    /// Palette write (OUT to #xx7E family with DFFD.7): colour from ~A15..A8, index from the previous #FE value
    void Port_Palette_Out(uint16_t port);

    /// #FE read bit 7 ("GX0" / UniCopy palette-present flag, 5.xx boards): in DS80, the palette
    /// entry selected by the previous #FE write's index reports (bit6 XOR bit0) on readback;
    /// outside DS80 the wire is pulled high. Karabas video.vhd:219, ZXMAK2 UlaProfi5XX.cs:34-53.
    uint8_t Port_FE_In_GX0() const;

    /// EXT mode qualifier (UnrealSpeccy default: cpm && rom14; Karabas additionally allows
    /// dosAct && !rom14, not implemented here - unproven by UnrealSpeccy sources)
    bool IsExtMode() const;
    /// ExtPorts=v003 only: TR-DOS running with ROM14 = 1 (CP/M off). The V0.03 PROM answers the long ports there as
    /// well, beside the VG93 at #1F..#7F - except the ones whose A6 A5 A1 A0 = 1111 (#E3 #E7 #EB #EF #F3 #F7 #FB
    /// #FF), which stay the system register. Not IsExtMode(): the short VG93 map stays
    bool IsLongBesideShort() const;
    /// Whether a long (extended-map) port answers now: the whole map in IsExtMode(), the V0.03 subset in
    /// IsLongBesideShort()
    bool LongPortOpen(uint16_t port) const;
    /// The IDE ports #8B / #AB / #CB / #EB: #EB loses to the system register in IsLongBesideShort()
    bool IdeShadowedBySysRegister(uint16_t port) const;
    /// The 8255 register an address selects in the current port map, or #FF
    uint8_t PpiRegister(uint16_t port, bool dosPorts) const;
    /// The COM port device an address selects in the extended map (v5 only), or ComDevice::None
    enum class ComDevice : uint8_t
    {
        None,
        Pit,       ///< 8253: #8F / #AF / #CF counters 0..2, #EF control (A6 A5 = A1 A0 of the chip)
        Usart,     ///< 8251: #D3 data, #F3 control / status (A5 = C/D)
        Control,   ///< COM control register #B3 (and #93): write D0 interrupt enable; read D0 RI, D7 DCD
    };
    ComDevice ComPortDevice(uint16_t port) const;
    /// The emulated time in base (3.5 MHz) T-states: the frames so far plus the CPU position scaled back from the
    /// CPU clock (turbo, hi-res); the COM port's clock
    uint64_t NowBase() const;
    /// The Profi IDE answers in EXT mode only (IDE design §3.2)
    IdeAdapter::Gate IdeGate() override;

    /// The front-panel TURBO switch (both boards): 7 MHz while it is pressed and, on v3, while the VG93's HLD is low
    /// (the HLD pin drives the board's /TURBO; research-profi-v3-turbo-floatbus.md A2)
    /// TURBO on both boards; the CP/M switch on v5 (the v3 drawings have none)
    /// TTD units per base T: the numerators the board can select (ProfiTtdClockUnits)
    uint8_t TtdClockUnits() const override;

    bool HasFrontPanelSwitch(FrontPanelSwitch sw) const override
    {
        return sw == FrontPanelSwitch::Turbo || (sw == FrontPanelSwitch::Cpm && _board.palette);
    }
    /// Whether an OUT to `port` writes #DFFD under [PROFI] DffdDecode
    bool DffdAnswers(uint16_t port) const;
    bool GetFrontPanelSwitch(FrontPanelSwitch sw) const override;
    bool SetFrontPanelSwitch(FrontPanelSwitch sw, bool on) override;

    /// The clock the board runs at now, from the switch and (v3) the HLD pin; applies a change at once
    void SyncTurbo();
    /// The frame and INT of the sync PROM half in use (hi-res: the upper half) into CONFIG
    void SyncFrame(bool hires);
    /// Installs or removes the wait-state overlay (ProfiWaitOverlay) for the board's mode and clock
    void SyncWaits();
    bool AreWaitsInstalled() const { return _waitsInstalled; }
    const ProfiWaitOverlay* GetWaitOverlay() const { return _waitOverlay.get(); }

    /// The v3 floating bus (research-profi-v3-turbo-floatbus.md B): what an IN that no device answers reads when
    /// its T3 starts at frame T `t3` - the pixel byte the video latched, #FF outside the read window
    uint8_t FloatingBusV3(uint32_t t3) const;
    /// The v3 floating bus in hi-res, at T3 given in ns from the frame start (design-hires.md H3)
    uint8_t FloatingBusV3Hires(double t3Ns) const;

    /// IMachineStepHook: on v3 with the switch pressed, follows the HLD pin
    void OnMachineStep(uint32_t t) override;
    /// #DFFD bit 4 (WOROM) lifts the #7FFD lock (UnrealSpeccy io.cpp)
    bool IsPagingLocked() const override
    {
        return (_state->p7FFD & PORT_7FFD_LOCK) && !(_state->pDFFD & 0x10);
    }
    /// endregion <Helper methods>

protected:
    void Port_7FFD(uint8_t value, uint16_t pc);

    void Port_DFFD(uint8_t value, uint16_t pc);
    /// Set the #DFFD latch and follow its paging and video mode (the CP/M switch clears it through here)
    void ApplyDffd(uint8_t value);
    void ResetPalette();

    /// RTC, EXT mode only. Address: #BF/#FF, data: #9F/#DF (UnrealSpeccy io.cpp:
    /// `(port & 0x9F) == 0x9F`, bit 5 selects address vs data).
    ///
    /// Modelled as the shared MC146818 chip with 256 cells, as UnrealSpeccy and
    /// ZXMAK2 (CmosProfi.cs, a DS12885) do. The karabas-pro clone differs: its
    /// "RTC" is a 256-byte RAM the board's AVR refreshes from its own clock, so
    /// the flags and UIP timing there follow the AVR firmware, not the datasheet.
    /// Battery-backed through [PROFI] NvramFile; lives with the decoder so the
    /// contents survive Core::Reset(), like the real battery
    /// The board (v3 or v5), fixed by the model when the decoder is created: what differs between the two
    const ProfiBoard _board;
    Ds12887 _rtc{256};
    /// The board's 8255 (KR580VV55): Kempston joystick on port A, printer / Covox on B and C. Its addresses are
    /// #1F/#3F/#5F/#7F outside the DOS / CP/M port set and #87/#A7/#C7/#E7 in the extended map (decoder-prom.md)
    Ppi8255 _ppi;
    /// The v5 board's COM port (Profi+ documentation, design.md section 2.1): a KR580VI53 clocked at 1.5 MHz, whose
    /// counter 0 output is the TxC / RxC of a KR580VV51A. What counters 1 / 2 drive on the board is not known
    static constexpr uint32_t kProfiPitClockHz = 1500000;
    static constexpr uint32_t kProfiBaseClockHz = 3500000;
    Pit8253 _pit{kProfiPitClockHz, kProfiBaseClockHz};
    Usart8251 _usart{kProfiBaseClockHz};
    uint8_t ComIn(ComDevice device, uint16_t port);
    void ComOut(ComDevice device, uint16_t port, uint8_t value);
    void SetSerialPeer(ISerialPeer* peer);
    /// The keyboard on X9 (v5) / KEYB (v3), fixed at power-on; the PROFI-XT controller when fitted. Every even-port
    /// read that reaches the #FE arm is its /CSKBD (design section "Keyboard")
    ProfiKeyboard _keyboardKind = ProfiKeyboard::Matrix;
    std::unique_ptr<ProfiXtKbc> _xtKbc;
    /// Fit the keyboard the config asks for (constructor)
    void FitKeyboard();
    std::unique_ptr<ProfiWaitOverlay> _waitOverlay;
    bool _waitsInstalled = false;
    bool _switchFromConfig = false;  // [PROFI] Turbo read once, at power-on (the switch is not touched by a reset)
    bool _nvramLoaded = false;  // [PROFI] NvramFile read once, on the first reset

    /// Tracks whether Covox currently has a live port set - either #3F/#5F (NORMAL,
    /// !dosPorts) or #C7/#A7 (CP/M-extended mode, IsExtMode()) - so the transition into a
    /// plain TR-DOS/Beta128 FDC session (dosPorts && !IsExtMode(), where Covox has no
    /// ports at all) can silence the DAC exactly once (see DecodePortOut).
    bool _covoxWasReachable = true;
};
