#pragma once

/// @file atm2kbc.h
/// @brief The ATM Turbo 2+ (v7.xx) keyboard controller: an i8031 / AT89S52
/// running its real firmware (data/rom/atm2kbc) behind `IN #FE`. It answers
/// every keyboard read while the Z80 waits on /WAIT, turns the PC keyboard
/// into the Spectrum matrix or CP/M / scan codes, keeps a clock and is the
/// board's RS-232 port.
///
/// The board around the MCU (v7.10 schematic cp7_2): the Z80 read latches
/// A15..A8 (D23) and sets the WAIT flip-flop (D71, unless W_ON = P1.7 is 1),
/// and pulses INT1. The MCU reads the latch with MOVX (P2.0 = 0), the native
/// keyboard / tape port D45 with MOVX (P2.0 = 1), and answers with a MOVX
/// write (P2.0 = 1, /VWR) that drives the Z80 data bus and ends the wait.
/// VE1 (#FF77 bit 6) on P3.4 turns the controller off; P1.6 resets the Z80,
/// P1.5 pulls its /INT.
///
/// Design: docs/inprogress/2026-10-01-atm2-keyboard-controller/tdd-atm2-kbc.md

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <array>

#include "emulator/cpu/mcs51/mcs51.h"
#include "emulator/io/keyboard/pckey.h"

class ISerialPeer;

class EmulatorContext;

class Atm2Kbc : public IPs2KeySink
{
public:
    /// The firmware images (data/rom/atm2kbc/README.md): the version and the
    /// crystal it was built for
    enum class Firmware : uint8_t
    {
        None,       ///< no controller: #FE is the plain matrix port
        V22At7,     ///< 2.2, 7 MHz (no RS-232)
        V22At11,    ///< 2.2, 11.0592 MHz
        V22At12,    ///< 2.2, 12 MHz
        V31At7,     ///< 3.1, 7 MHz (RS-232; 256-byte RAM)
        V31At11,    ///< 3.1, 11.0592 MHz
        V32At7,     ///< 3.2m, 7 MHz (the stock F0 clock)
        V32At11,    ///< 3.2m, 11.0592 MHz
        V40,        ///< 4.0, AT89S52 at 11.0592 MHz
        V41,        ///< 4.1, AT89S52 at 11.0592 MHz (default)
    };
    static constexpr Firmware kDefaultFirmware = Firmware::V41;

    struct FirmwareInfo
    {
        Firmware firmware;
        const char* name;     ///< config name: V22-7 .. V41
        const char* file;     ///< rom/atm2kbc/...
        uint32_t crystalHz;
        bool i8052;           ///< needs 256 bytes of RAM / timer 2
        bool serialPort;      ///< has the RS-232 commands
        const char* description;
    };
    static const FirmwareInfo* Info(Firmware firmware);
    /// NONE, V22-7, V22-11, V22-12, V31-7, V31-11, V32-7, V32-11, V40, V41 (case-insensitive)
    static bool ParseFirmware(const char* text, Firmware& out);
    static const char* FirmwareName(Firmware firmware);

    explicit Atm2Kbc(EmulatorContext* context);
    ~Atm2Kbc() override;

    /// The PC (AT) keyboard on the controller's clock / data lines (P3.2 =
    /// INT0, P3.5): host keys become scan code set 2 frames on the wire
    void OnPcKey(PcKey key, bool pressed) override;
    void ReleaseAllPcKeys() override;

    /// Fit a firmware (power-on of the controller); `romPath` overrides the
    /// preset's image (the preset still gives the crystal). False + reason
    bool Load(Firmware firmware, const std::string& romPath, std::string& error);
    bool Present() const { return _firmware != Firmware::None && _cpu != nullptr; }
    Firmware GetFirmware() const { return _firmware; }
    uint32_t CrystalHz() const { return _crystalHz; }

    /// The native port D45 for a port address (matrix AND, tape, ...): the decoder's plain #FE read
    void SetNativePort(std::function<uint8_t(uint16_t port)> native) { _native = std::move(native); }

    /// A Z80 `IN #FE`. Runs the controller until it answers, adds the wait to
    /// the Z80 and returns the byte it put on the bus. Not present, blocked
    /// (VE1, W_ON): the native port, no wait
    uint8_t ReadPort(uint16_t port);

    /// #FF77 bit 6
    void SetVe1(bool ve1);

    /// The RS-232 port (the MCU's own UART through the 170AP2 / 170UP2 line
    /// drivers): what is plugged in, or nullptr. The controller does not own it
    void SetSerialPeer(ISerialPeer* peer);
    ISerialPeer* SerialPeer() const { return _peer; }
    /// The line the firmware programmed (baud from its timer 1 / 2), as the peer sees it
    uint32_t SerialBaud() const;
    /// RTS / DTR as driven at the connector (P1.4 / P1.3 low = asserted)
    bool Rts() const { return _cpu && (_cpu->Latch(1) & 0x10) == 0; }
    bool Dtr() const { return _cpu && (_cpu->Latch(1) & 0x08) == 0; }

    /// Frame end: the controller's clock, keyboard and UART run between reads
    void OnFrameEnd();

    /// The emulated machine restarted its clock (reset, snapshot load): time base follows
    void Rebase();

    /// The board's reset line (the reset button, the controller's own /RES):
    /// the MCU restarts, its RAM kept
    void BoardReset();

    mcs51::Mcs51* Cpu() const { return _cpu.get(); }

    /// Board latches, for the state report and TTD
    struct Board
    {
        uint8_t latchedHigh = 0xFF;   ///< D23: A15..A8 of the last read
        uint8_t dataOut = 0xFF;       ///< D102: the byte for the Z80
        bool waitSet = false;         ///< D71: the Z80 waits
        bool ve1 = false;
        bool answered = false;        ///< the MCU wrote D102 since the read
    };
    const Board& GetBoard() const { return _board; }

    /// Statistics
    uint64_t Reads() const { return _reads; }
    uint64_t LastWaitClocks() const { return _lastWaitMcu; }

    /// The keyboard's side of the wire (state report, TTD). Trivial (no member
    /// initializers): the TTD blob is cleared and copied as bytes; PcKeyboard{}
    /// is the idle keyboard (all zero, repeatKey None)
    struct PcKeyboard
    {
        uint8_t queue[64];                 ///< bytes waiting to be sent (a real keyboard buffers 16)
        uint8_t head, count;
        uint8_t sending;
        uint8_t frameByte;
        uint8_t bit;                       ///< 0 start, 1..8 data, 9 parity, 10 stop
        uint8_t phase;                     ///< 0 set data, 1 clock low, 2 clock high
        uint64_t nextEdge;                 ///< MCU clock of the next line change
        uint64_t idleUntil;                ///< the gap between frames ends here
        PcKey repeatKey;                   ///< typematic: the last key made, still held
        uint64_t repeatAt;
        uint8_t held[16];                  ///< bitmap of the keys held
        uint8_t overflow;                  ///< the buffer overflowed: an 00 code is due
    };
    const PcKeyboard& GetKeyboard() const { return _kbd; }

    /// The RS-232 line between the MCU and its peer (state report, TTD)
    struct SerialLineState
    {
        uint8_t rxBusy;                ///< a frame from the peer is on RXD
        uint8_t rxByte;
        uint8_t rts, dtr;              ///< as last told to the peer
        uint32_t baud;                 ///< as last told to the peer
        uint64_t rxDoneAt;             ///< its stop bit is sampled here (MCU clock)
        uint64_t rxNextAt;             ///< the next frame cannot start before
        uint64_t bytesIn, bytesOut, lost;
    };
    const SerialLineState& GetSerialLine() const { return _line; }

    /// TTD (fixed size, trivially copyable): everything that runs, not the ROM image
    struct State
    {
        uint32_t version;              ///< kStateVersion
        uint8_t firmware;              ///< Firmware: a blob of another image is refused
        uint8_t latchedHigh, dataOut, waitSet, ve1, answered, resetLow, p3;
        uint8_t inRead, reserved[3];
        uint16_t readPort, reserved2;
        uint64_t tBase, mcuBase, frac, lastNow, answerClock, reads, lastWaitMcu;
        mcs51::Mcs51::State cpu;
        PcKeyboard keyboard;
        SerialLineState line;
    };
    static constexpr uint32_t kStateVersion = 2;   ///< 2: the RS-232 line
    void SaveState(State& out) const;
    /// False (and nothing changed) when the blob is of another firmware or version
    bool LoadState(const State& in);


private:
    uint64_t NowBase() const;              ///< emulated time in base T-states
    uint64_t McuClockAt(uint64_t baseT);   ///< the MCU oscillator clock for an emulated time
    void CatchUp(uint64_t baseT);
    /// Run the MCU to `target` in pieces, the keyboard's line changes in
    /// between; with `untilAnswer` it ends at the /VWR of a read
    void RunTo(uint64_t target, bool untilAnswer);
    /// Keyboard events due by `clock`: line changes, the next frame, typematic repeats
    void KeyboardProcess(uint64_t clock);
    uint64_t KeyboardNextEvent() const;
    void KeyboardEnqueue(const std::vector<uint8_t>& bytes);
    /// RS-232: modem inputs, the frame in from the peer, the line settings
    void SerialProcess(uint64_t clock);
    uint64_t SerialNextEvent() const;
    void SerialOut(uint8_t byte);
    void TellLine();
    uint64_t KeyboardBitClocks() const { return static_cast<uint64_t>(_crystalHz) * 80u / 1000000u; }
    void OnPortOut(int port, uint8_t latch);
    uint8_t MovxRead(uint16_t address);
    void MovxWrite(uint16_t address, uint8_t value);
    /// /VWR: `value` into D102, the wait ends `offset` clocks after the instruction start
    void DataStrobe(uint8_t value, uint64_t offset);

    EmulatorContext* _context = nullptr;
    std::unique_ptr<mcs51::Mcs51> _cpu;
    Firmware _firmware = Firmware::None;
    uint32_t _crystalHz = 0;
    std::function<uint8_t(uint16_t)> _native;

    Board _board;
    uint16_t _readPort = 0xFFFE;     ///< the port of the read in progress (native reads use its high byte)
    bool _inRead = false;

    // Time base: MCU clock = _mcuBase + (baseT - _tBase) * crystal / baseHz
    uint64_t _tBase = 0;
    uint64_t _mcuBase = 0;
    uint64_t _frac = 0;              ///< remainder of the conversion (exact: no drift)
    uint64_t _lastNow = 0;
    uint64_t _answerClock = 0;       ///< MCU clock of the /VWR that ended the wait

    bool _resetLow = false;          ///< P1.6 holds the Z80 in reset
    uint8_t _p3 = 0xFF;              ///< P3 latch as last seen (manual /VWR, /VRD strobes)
    PcKeyboard _kbd{};
    ISerialPeer* _peer = nullptr;
    SerialLineState _line{};
    uint64_t _reads = 0;
    uint64_t _lastWaitMcu = 0;
};
