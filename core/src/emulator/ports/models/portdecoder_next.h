#pragma once
#include "stdafx.h"

#include <map>
#include <memory>

#include "emulator/bus/bustap.h"
#include "emulator/io/z80n/nextboard.h"
#include "emulator/io/z80n/nextdma.h"
#include "emulator/sound/nextaudio.h"
#include "emulator/io/z80n/nextctc.h"
#include "emulator/io/z80n/nextuart.h"
#include "emulator/io/z80n/nextdivmmc.h"
#include "emulator/io/z80n/nextmultiface.h"
#include "emulator/io/z80n/nextsdjournal.h"
#include "emulator/io/z80n/nextintjournal.h"
#include "emulator/io/z80n/nextextendedinput.h"
#include "emulator/io/z80n/nextmagicport.h"
#include "emulator/io/z80n/nexti2c.h"
#include "emulator/io/z80n/nextinterrupts.h"
#include "emulator/io/z80n/z80nengine.h"
#include "emulator/io/sdcard/sdcardspi.h"
#include "emulator/media/mediaslot.h"
#include "emulator/memory/next/nextmemory.h"
#include <vector>

#include "emulator/ports/models/portdecoder_spectrum128.h"

class ScreenNext;

/// ZX Spectrum Next, skeleton personality: the 128K board's ports (ULA #FE, AY, #7FFD) with the Next's wider
/// decode of the paging ports - #7FFD, #1FFD, #DFFD rewrite the MMU slot table - and the NEXTREG pair #243B /
/// #253B. Owns the board (register file) and installs the Z80N CPU engine.
class BreakpointManager;
enum class BreakpointHitKind : uint8_t;

class PortDecoder_Next : public PortDecoder_Spectrum128, public INextMachine
{
public:
    PortDecoder_Next() = delete;
    explicit PortDecoder_Next(EmulatorContext* context);
    ~PortDecoder_Next() override;

    void reset() override;
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;
    void UpdateModelMemoryBanks() override;

    static constexpr uint16_t kPortRegSelect = 0x243B;
    static constexpr uint16_t kPortRegData = 0x253B;

    static bool IsPort_7FFD_Next(uint16_t port) { return (port & 0xC002) == 0x4000; }
    static bool IsPort_1FFD_Next(uint16_t port) { return (port & 0xF002) == 0x1000; }
    static bool IsPort_DFFD_Next(uint16_t port) { return (port & 0xF002) == 0xD000; }

    /// INextMachine: NR #02. Runs between two instructions
    void PerformReset(bool hard) override;
    void SystemClockChanged() override;
    void SetCpuSpeed(uint8_t ratio) override;
    void SetMachineTiming(uint8_t timing) override;
    void SetContentionDisabled(bool disabled) override;
    void ResetContentionDisabled() override;
    bool ContentionDisabled() const override { return _nr08NoContention; }
    void WriteMemoryMapping(uint8_t value) override;
    void ClearDivMmcMapram() override { _divMmc->ClearMapram(); }
    void DivMmcEnableChanged() override { _divMmc->OnEnableChanged(); }
    void StepDma() override;
    /// INextMachine: with a beam, copper-instruction or copper-door NextREG breakpoint set, the copper is watched here, at the
    /// instruction boundary (WatchCopper); otherwise one load and one test
    void OnInstructionDone() override;
    void OnRetn() override;
    bool GenerateMultifaceNmi() override;
    /// An access of the +3 FDC ports #2FFD / #3FFD with NR #D8 bit 0 on: the cause and the byte are latched and the M1 NMI path is taken
    void IoTrap(uint16_t port, bool write, uint8_t value);
    /// The DRIVE button (F10 on the board; the host's NMI action): NR #06 bit 4 enables it, one NMI at a time
    bool RequestBoardNmi() override;
    bool GenerateDriveNmi() override;
    bool BoardHasKempstonJoystick() const override { return true; }
    bool PagingLocked() const override { return IsPagingLocked(); }  // #7FFD bit 5 (zxnext.vhd port_7ffd_locked)
    bool ShadowScreen() const override { return (_context->emulatorState.p7FFD & 0x08) != 0; }
    void SetShadowScreen(bool on) override
    {
        _context->emulatorState.p7FFD = static_cast<uint8_t>((_context->emulatorState.p7FFD & ~0x08) | (on ? 0x08 : 0));
    }
    void UnlockPaging() override { _context->emulatorState.p7FFD &= static_cast<uint8_t>(~0x20); }
    void AudioConfigChanged() override;
    void DacMirrorWrite(uint8_t reg, uint8_t value) override;
    void PiAudioChanged(unsigned left, unsigned right) override;
    NextAudio& Audio() { return *_audio; }
    /// What a read of port #1F returns now (the joystick's state; no side effect), for the input report
    uint8_t KempstonJoystickNow() const { return Default_Port_KempstonJoystick_In(); }

    /// NR #05 joystick mode codes (zxnext.vhd nr_05_joy0 / joy1 = bit 3 or 1 & bits 7:6 or 5:4)
    static constexpr unsigned kJoyKempston1 = 1, kJoyKempston2 = 4, kJoyMd1 = 5, kJoyMd2 = 6;
    /// The code of the left (`left` true) or right joystick as NR #05 holds it now
    unsigned JoystickMode(bool left) const;
    /// Whether the board drives port #1F (`port37` false) or #37 now, and what a read gives (zxnext.vhd 2396, 2616-2617, 3436-3470): a side
    /// that is Kempston 1 / MD 1 feeds #1F, Kempston 2 / MD 2 feeds #37, bits 7:6 only for an MD pad; the port also needs its bit in
    /// NR #82 (6 and 7). The host joystick is the LEFT connector; the right one is set by a tool (NextExtendedInput::Right). False: the read is not answered (the bus idles at #FF)
    bool JoystickPortAnswers(bool port37, uint8_t& value) const;
    NextDma& Dma() { return _dma; }
    void BindDma();
    uint8_t ReadMemoryMapping() const override;
    void OnFrameEnd() override;

    /// The SPI port pair #E7 (select) / #EB (data): one SD card per select line (NR #0A bit 5 swaps them)
    static constexpr uint16_t kPortSpiSelect = 0x00E7;
    static constexpr uint16_t kPortSpiData = 0x00EB;
    static constexpr double kSpiByteClocks = 16.0;  ///< CPU clocks per byte at any speed
    SdCardSpi& SdCard(unsigned index) { return _sd[index & 1]; }
    BusTap& SpiTap() { return _spiTap; }
    bool InsertSdCard(unsigned index, std::unique_ptr<IBlockDevice> media, SdCardSpi::WriteMode mode);
    bool InsertSdCard(unsigned index, const std::string& path, SdCardSpi::WriteMode mode);
    /// Ports no part of the machine answered (the bring-up list): raw port -> {count, last value}, in and out
    struct PortUse
    {
        uint32_t count = 0;
        uint8_t last = 0;
    };
    void SetPortLog(std::map<uint16_t, PortUse>* in, std::map<uint16_t, PortUse>* out) { _inLog = in; _outLog = out; }
    /// The end of the byte in flight, in base T-states (see the definition: a speed change inside the byte rescales what is left)
    double SpiBusyUntil();
    /// Accesses that began before the previous byte was done and were ignored, as on the board
    uint32_t SpiTooFastCount() const { return _spiTooFast; }
    /// The SD journal (automation-gaps-2026-10-11.md, P1) and the SPI engine's state for the report
    /// The interrupt, NMI and reset journal (automation-gaps-2026-10-11.md, P4)
    NextExtendedInput& ExtendedInput() { return _extendedInput; }
    const NextExtendedInput& ExtendedInput() const { return _extendedInput; }
    uint8_t ReadExtendedInput(uint8_t reg) const override;
    bool HasPads() const override { return true; }
    uint16_t PadState(unsigned connector) const override;
    bool ApplyPadState(unsigned connector, uint16_t state) override;
    NextMagicPort& MagicPort() { return _magicPort; }
    const NextMagicPort& MagicPort() const { return _magicPort; }
    NextIntJournal& IntJournal() { return _intJournal; }
    const NextIntJournal& IntJournal() const { return _intJournal; }
    /// Why a button NMI would be refused now (nullptr: it would fire), for the report and the journal
    const char* MultifaceNmiRefusal() const;
    const char* DriveNmiRefusal() const;
    NextSdJournal& SdJournal() { return _sdJournal; }
    const NextSdJournal& SdJournal() const { return _sdJournal; }
    int SpiSelectedLine() const { return _spiSelected; }
    bool SpiByteInFlight() const { return _spiPendingValid; }
    uint8_t SpiLastReceived() const { return _spiRx; }

    NextBoard& Board() { return *_board; }
    NextInterruptSource& Interrupts() { return *_interrupts; }
    NextCtc& Ctc() { return _ctc; }
    /// The level on pin 7 of the joystick connector in its I/O mode (NR #0B bits 7:4, zxnext.vhd 3507-3534): bit 0 in mode 00, toggled
    /// by CTC channel 3's ZC/TO in mode 01, the UART's TX (idle high; the waveform is not modelled) in modes 1x
    bool JoystickPin7() const { return _joyPin7; }
    /// Bit `bit` of the internal port enable word (reports)
    bool PortEnabledForReport(unsigned bit) const { return PortEnabled(bit); }
    /// A line's other end (the network manager owns the peer); nullptr takes it off. The peer's clock is the machine's base clock
    void SetUartPeer(unsigned channel, ISerialPeer* peer);
    /// The device plugged into the joystick connector (nullptr: nothing); it is the other end of the UART NR #0B bit 0 selects while
    /// the register's UART mode (bits 7 and 5) is on
    void SetJoystickUartPeer(ISerialPeer* peer);
    /// NR #0B -> the connector's UART route (zxnext.vhd: joy_iomode_uart_en, nr_0b_joy_iomode_0); `advance` brings the UARTs up to the
    /// clock first
    void UpdateUartRoute(bool advance = true);
    /// NR #A8 / #A9: the ESP's GPIO0 pin (low while the board drives it low): an ESP module reboots into its ROM's download mode with it low
    void BoardPinsChanged() override;
    NetworkCapabilities DescribeNetwork() override;
    /// The two UARTs (#133B-#163B): ESP (0) and Pi (1)
    NextUart& Uart() { return _uart; }
    const NextUart& Uart() const { return _uart; }
    NextDivMmc& DivMmc() { return *_divMmc; }
    NextMultiface& Multiface() { return *_multiface; }
    NextI2c& I2c() { return _i2c; }
    /// The 28 MHz system clock since the machine started
    uint64_t Now28() const;
    Z80NEngine* Engine() { return _engine.get(); }

    /// The TTD state of the Next (PeripheralId::NextState, nextstatecodec.h)
    std::vector<ttd::TTDDeviceType> GetTTDModelStateTypes() const override;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> CreateTTDSerializers() const override;

    /// zxnext.vhd: the ULA byte reaches the bus only on port #xxFF under the 48K / 128K timing (port_ff_dat_ula), and on the
    /// +3 port (A15:12 = 0, A1:0 = 01) under the +3 timing with NR #82 bit 4; every other undecoded port reads #FF
    bool FloatingBusDrives(uint16_t port) const override;
    bool OddPortContended(uint16_t port) const;
    bool SixtyHzWanted(uint8_t timing) const;
    void BeforeRelease() override;

private:
    friend struct NextStateCodec;  // the TTD state codec (nextstatecodec.cpp)
    /// NextREG breakpoints (BreakpointManager::HandleNextReg). A hit of the CPU's doors stops the machine where it happens (as a port
    /// breakpoint does), before the register takes the write.
    ///
    /// The copper is another matter: the picture runs it lazily, a line at a time, in the middle of drawing, where the machine must not
    /// park and the CPU is up to a line ahead of the copper's time. With a breakpoint on the copper (its door, its instructions, the beam)
    /// WatchCopper looks at what the copper does up to the beam *now* on a copy (NextCopper::Probe) after every instruction, and stops
    /// at the first instruction boundary after the beam has done it, before the real copper's catch-up executes the write. The real
    /// catch-up then neither checks nor counts them again
    void NextRegAccess(bool write, uint8_t reg, uint8_t value, NextRegSource source);
    void FireBreakpoint(uint16_t id, uint16_t address, BreakpointHitKind kind);
    void WatchCopper(BreakpointManager& manager);
    bool OnCopperStep(const NextCopper::Step& step);
    void WatchMagic(BreakpointManager& manager);
    bool _breakpointActive = false;  // a stop is being served: nothing nests in it
    bool _watchValid = false;
    uint64_t _watchClock = 0;  // the copper clock up to which the copper was watched
    NextMemory& Mem() const { return *static_cast<NextMemory*>(_context->pMemory); }
    void Port_7FFD_Next(uint16_t port, uint8_t value, uint16_t pc);
    void Port_1FFD_Next(uint8_t value);
    void Port_DFFD_Next(uint8_t value);

    void InsertConfiguredCard();
    void ApplyTiming(uint8_t timing);
    /// CLK_28 of NR #11 to the UARTs, the frame's real time and the sound's playback rate
    void ApplySystemClock();
    /// The real time of a frame of `frame` T-states at CLK_28
    uint32_t FrameDurationUs(uint32_t frame) const;
    /// Contention follows the frame family, the speed (3.5 MHz only) and NR #08 bit 6
    void UpdateContention();
    /// Apply the NR #08 bit 6 write once the beam has left the line's contention window
    void SettleContentionDisabled();
    bool _nr08NoContention = false;      // eff_nr_08_contention_disable
    bool _nr08Pending = false;           // a write waits for hc(8) = 1
    bool _nr08PendingValue = false;
    uint32_t _nr08ApplyAt = 0;           // the CPU T it takes effect at
    uint64_t _nr08PendingFrame = 0;      // in this frame
    uint8_t _ratio = 1;
    /// What the machine type allows: 48K has no paging ports, 128K / Pentagon no #1FFD, +3 all
    bool PagingAllowed(uint16_t port) const;
    uint8_t SpiRead();
    uint8_t SpiTapSlot() const { return _spiSelected >= 0 ? static_cast<uint8_t>(_spiSelected) : BusTap::kNoSlot; }
    void SpiSettle(double now);
    void SpiWrite(uint8_t value);
    void SpiSelect(uint8_t value);
    /// Time in 3.5 MHz T-states since the machine started (the frame's own t is in CPU clocks of the current speed)
    double Now() const;
    /// Bit `bit` (0-27) of internal_port_enable (zxnext.vhd 2392): NR #82-#85, and with the expansion bus on (NR #80 bit 7) also NR #86-#89
    bool PortEnabled(unsigned bit) const;
    /// A read of a port whose enable bit is off: nothing on the board answers, the bus is idle
    uint8_t IdleRead(uint16_t port, uint16_t pc);
    uint8_t ReadMouse(uint8_t reg);
    void ApplyMouseDpi(uint8_t nr0a);
    bool _joyPin7 = true;  ///< zxnext.vhd joy_iomode_pin7 (reset 1)
    void UpdateJoystickPin7(uint8_t nr0b);
    bool RaiseMultifaceNmi(const char* source);
    void SyncPiLine();
    ScreenNext* _screenNext = nullptr;  ///< the picture, told before the program changes what the sprite engine fills from
    /// #FADF / #FBDF / #FFDF: A7-A0 = #DF and A11-A8 = A / B / F (zxnext.vhd), the register 0 buttons, 1 X, 2 Y
    static bool MouseRegisterOf(uint16_t port, uint8_t& reg)
    {
        if ((port & 0xFF) != 0xDF)
            return false;
        switch ((port >> 8) & 0x0F)
        {
            case 0x0A: reg = 0; return true;
            case 0x0B: reg = 1; return true;
            case 0x0F: reg = 2; return true;
            default: return false;
        }
    }
    bool IsPort_KempstonMouse(uint16_t port, uint8_t& outRegister) const override;
    /// NR #83 bit 3: ports #E7 / #EB are decoded (port_spi_io_en)
    bool SpiPortEnabled() const { return (_board->Stored(0x83) & 0x08) != 0; }
    double SpeedRatio() const { return _state->hw_turbo_ratio_applied ? _state->hw_turbo_ratio_applied : 1.0; }

    std::map<uint16_t, PortUse>* _inLog = nullptr;
    std::map<uint16_t, PortUse>* _outLog = nullptr;
    // The bus analyzer's view of the SPI port pair (docs/inprogress/2026-10-11-bus-analyzer); slot = SD line
    BusTap _spiTap{"next.spi", BusProtocol::Spi, "Next SPI (#E7 chip selects, #EB data): SD cards 0 and 1"};
    SdCardSpi _sd[2];

    /// The media manager's slots "sd.next0" / "sd.next1": a card is never opened by the decoder itself, every insert goes through the manager
    class SdSlot : public IMediaSlot
    {
    public:
        SdSlot(PortDecoder_Next& owner, unsigned index);
        const SlotDescriptor& Descriptor() const override { return _descriptor; }
        void Attach(Medium& medium) override;
        void Detach() override;
        bool IsBusy() const override;

    private:
        PortDecoder_Next& _owner;
        unsigned _index;
        SlotDescriptor _descriptor;
    };
    SdSlot _sdSlot0{*this, 0};
    SdSlot _sdSlot1{*this, 1};
    SdSlot& Slot(unsigned index) { return index & 1 ? _sdSlot1 : _sdSlot0; }
    uint8_t _pendingTiming = 0;  ///< applied at the frame end
    int _spiSelected = -1;
    uint8_t _spiRx = 0xFF;       ///< miso_dat: the last finished byte
    uint8_t _spiPending = 0xFF;  ///< the answer of the byte in flight, visible when it is done
    bool _spiPendingValid = false;
    double _spiBusyUntil = 0;
    double _spiBusyRatio = 1.0;  ///< the CPU speed ratio the byte in flight was timed at
    /// NR #07 was written and the clock is not switched yet: zxnext.vhd takes it over at the first idle bus clock after the write,
    /// the emulator does it at the next instruction fetch (the instruction boundary)
    bool _speedPending = false;
    double _speedChangedAt = 0;  ///< Now() of the last mid-frame switch (the boundary the SPI byte in flight is rescaled at)
    void ApplyPendingSpeed();
    uint32_t _spiTooFast = 0;
    NextSdJournal _sdJournal;
    NextIntJournal _intJournal;
    NextMagicPort _magicPort;
    NextExtendedInput _extendedInput;  ///< the keyboard's extended keys and the pads' extra buttons; the machine's PS/2 sink
    void RecordInt(NextIntEvent event);
    void RecordSdEvent(unsigned card, const SdTraceEvent& event);
    std::unique_ptr<NextBoard> _board;
    std::unique_ptr<NextInterruptSource> _interrupts;
    std::unique_ptr<NextDivMmc> _divMmc;
    std::unique_ptr<NextMultiface> _multiface;
    /// Both devices watch the M1 cycles (the Multiface first: its memory has priority)
    struct M1Chain final : IMachineM1Hook
    {
        NextMultiface* multiface = nullptr;
        NextDivMmc* divMmc = nullptr;
        PortDecoder_Next* owner = nullptr;
        void BeforeMachineM1(uint16_t address) override
        {
            if (owner->_speedPending) [[unlikely]]
                owner->ApplyPendingSpeed();
            multiface->BeforeMachineM1(address);
            divMmc->BeforeMachineM1(address);
        }
        void OnMachineM1(uint16_t address) override
        {
            multiface->OnMachineM1(address);
            divMmc->OnMachineM1(address);
        }
    } _m1Chain;
    NextCtc _ctc;
    NextUart _uart;
    NextDma _dma;
    bool _dmaRequestLate = false;  // the enable came in this instruction: the bus goes over after the next one's M1
    std::unique_ptr<NextAudio> _audio;
    bool WriteDacPort(uint16_t port, uint8_t value);
    NextI2c _i2c;
    std::unique_ptr<Z80NEngine> _engine;
};
