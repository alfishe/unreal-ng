#pragma once

/// @file hayesmodempeer.h
/// @brief An emulated Hayes-compatible modem on the far end of a UART (network tdd §10, phase SN4): the peer of
/// ComPortSpec MODEM. Shared by every machine's serial port - the Sprinter's ISA modem card and SprinterSerial, the
/// ZX-Evo COM port, ZX-WiFi, the ATM2IOESP - because it is an ISerialPeer like the loopback, the stream and the ESP
/// modules: the UART times every character, the modem only answers.
///
/// There is no telephone network: a "number" is a host TCP endpoint reached through the machine's virtual network
/// (DNS, hosts table, host bridge, TTD journal - the StreamPeer link, in its Dialer form). What a dialed number
/// means:
///   - `ATDT bbs.example.org:2323`, `ATDT 192.0.2.10` (port 23): letters, '.' or ':' make it a host[:port];
///   - `ATDT 555-1234`: digits are looked up in the phone book ([NETWORK] ModemPhonebook=5551234=host:port,...);
///     a number that is not there gets NO CARRIER (nobody answers it).
///
/// Behavior (Hayes Smartmodem / V.250 command set, the subset period terminal programs use):
///   - command mode: `AT` (or `at`) + commands + CR (S3); `A/` repeats the last line; BS (S5) edits; echo (E1),
///     results verbose (V1: CR LF text CR LF) or numeric (V0: digits CR), quiet (Q1); X0-X4 pick the result set;
///   - D (dial, modifiers T P W , @ ! ; L = redial), A (answer), H (hang up), O (back online), Z / &F (reset),
///     E Q V X, Sn=v / Sn?, I0-I4, &C (DCD: 0 always on, 1 follows the carrier - default), &D (DTR on-to-off:
///     0 ignored, 1 command mode, 2 hang up - default, 3 reset), &S (DSR), and the settings period init strings
///     carry (L M B N W Y, &K &Q &W &Y ..., \N, %C, +...) accepted without effect;
///   - online: every byte goes to the call; `+++` with S12 guard time before and after (S2 = '+') returns to
///     command mode with OK, the call kept (ATO resumes it, ATH ends it);
///   - result codes: OK, CONNECT [rate], RING, NO CARRIER, ERROR, NO DIALTONE, BUSY (a refused connection),
///     NO ANSWER; the rate is the DTE rate the UART is programmed for;
///   - lines: CTS always (the modem buffers), DSR (&S0: always), DCD (&C), RI while a call rings (2 s on, 4 s off).
///
/// Inbound calls (`MODEM,<guest port>`): a host client of that guest port ([NETWORK] Forward=host:guest) rings
/// the modem - RING, RI - and ATA (or S0 > 0 rings) answers it; a caller arriving while the line is busy is turned
/// away (its connection closes).
///
/// Time is the machine's (base T-states through ISerialPeer's clock); everything that comes from outside is a
/// journaled network event, so a TTD replay repeats a session byte for byte with no host.
///
/// Worked example (BC-Term on the Sprinter's ISA modem): the program sends "ATZ" CR - the modem echoes it and
/// answers CR LF "OK" CR LF; "ATDT 5551234" CR with ModemPhonebook=5551234=bbs.test:23 resolves bbs.test, connects
/// and answers "CONNECT 57600" with DCD on; the BBS's banner arrives as received characters (the UART's interrupt);
/// "+++" after a second of silence, a second more: "OK"; "ATH" CR: the call ends, "OK", DCD off.

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "emulator/io/network/netstate.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/state/statenode.h"

class VirtualNetwork;

class HayesModemPeer final : public ISerialPeer, public INetGuest
{
public:
    enum class Mode : uint8_t
    {
        Command = 0,   ///< no call: AT commands
        Dialing = 1,   ///< ATD: resolving / connecting (any key aborts)
        Online = 2,    ///< a call, data mode
        Escaped = 3,   ///< a call, command mode after +++ (ATO resumes, ATH ends)
    };

    /// @param phonebook "5551234=host:port,5550000=10.0.2.2:2323" ([NETWORK] ModemPhonebook)
    HayesModemPeer(VirtualNetwork* network, const ComPortSpec& spec, const std::string& phonebook);
    ~HayesModemPeer() override;

    HayesModemPeer(const HayesModemPeer&) = delete;
    HayesModemPeer& operator=(const HayesModemPeer&) = delete;

    /// "5551234=host:port,..." -> number (digits only) -> ComPortSpec text; false with the reason
    static bool ParsePhonebook(const std::string& text, std::map<std::string, std::string>& out, std::string& error);

    // ISerialPeer
    void Transmit(uint8_t byte) override;
    bool HasByte() const override;
    uint8_t TakeByte() override;
    bool Cts() const override { return true; }
    bool Dsr() const override;
    bool Dcd() const override;
    bool Ri() const override { return _ri; }
    void OnModemLines(bool rts, bool dtr) override;
    void OnLineSettings(const SerialLine& line) override { _lineBaud = line.baud; }
    void OnFrame() override;
    void Reset() override;
    const char* Kind() const override { return "modem"; }
    std::string Target() const override;
    size_t Pending() const override;
    bool Connected() const override { return Carrier(); }

    // INetGuest: the link's sockets (StreamPeer cookies 0 / 1) and the listener (kCookieListen)
    void OnNetEvent(uint32_t cookie, NetEventType type, NetEventStatus status, const NetEndpoint& peer,
                    const uint8_t* data, uint32_t length, uint32_t source) override;

    Mode GetMode() const { return _mode; }
    bool Carrier() const { return _link.GetPhase() == StreamPeer::Phase::Connected && (_mode == Mode::Online || _mode == Mode::Escaped); }
    bool Ringing() const { return _ringing; }
    const std::string& LastResult() const { return _lastResult; }
    const std::string& Dialed() const { return _dialed; }
    uint8_t SRegister(int n) const { return n >= 0 && n < netstate::kModemSRegs ? _s[n] : 0; }
    StreamPeer& Link() { return _link; }
    const StreamPeer& Link() const { return _link; }
    uint16_t ListenPort() const { return _listenPort; }

    /// Commands and what the modem answered (observation, newest last; a replay fills it again)
    struct Exchange
    {
        uint64_t at = 0;          ///< base T-state of the command's CR
        std::string command;
        std::string result;
    };
    static constexpr size_t kExchanges = 32;
    const std::deque<Exchange>& RecentExchanges() const { return _exchanges; }

    /// The report every automation surface prints (state/network, state/isa): mode, lines, call, settings, counters
    void Describe(StateNode& out) const;

    /// TTD (netstate::HayesModemState; the link's state and received bytes travel in Com::link / Com::runs)
    void SaveState(netstate::HayesModemState& out) const;
    void LoadState(const netstate::HayesModemState& in);

    static constexpr uint32_t kCookieListen = 2;

private:
    void FactoryReset();
    void Execute(const std::string& line);
    /// One result code in the current format; `code` numeric, `text` verbose
    void Result(int code, const std::string& text);
    void ResultConnect();
    void Emit(const std::string& bytes);
    void Echo(uint8_t byte);
    void Dial(const std::string& number);
    void HangUp();
    void Answer();
    void OnLinkChange(StreamPeer::Phase phase, NetEventStatus status, const std::string& why);
    void CallEnded();   ///< the carrier dropped: NO CARRIER, command mode
    void StopRinging();
    void Notify();      ///< bytes or lines changed outside a UART access: the UART looks now
    void CloseLater(uint16_t socket);
    uint64_t GuardT() const;   ///< S12 in base T-states
    void Log(const std::string& command, const std::string& result);

    VirtualNetwork* _network = nullptr;
    StreamPeer _link;
    std::map<std::string, std::string> _phonebook;
    uint16_t _listenPort = 0;

    Mode _mode = Mode::Command;
    uint8_t _s[netstate::kModemSRegs] = {};
    bool _echo = true, _quiet = false, _verbose = true;
    uint8_t _resultSet = 4;      ///< X
    uint8_t _dcdMode = 1;        ///< &C
    uint8_t _dtrMode = 2;        ///< &D
    uint8_t _dsrMode = 0;        ///< &S
    bool _dtr = false;
    bool _offHook = false;
    std::string _command;
    std::string _lastCommand;
    std::deque<uint8_t> _out;
    bool _dropPending = false;   ///< the call ended: NO CARRIER once its last bytes reached the ZX
    uint8_t _plusCount = 0;
    uint64_t _lastDataAt = 0, _lastPlusAt = 0, _escapedAt = 0;
    uint64_t _dialDeadline = 0;
    bool _ringing = false, _ri = false;
    uint16_t _listenSocket = 0, _ringSocket = 0;
    uint32_t _ringCount = 0;
    uint64_t _nextRingAt = 0, _riOffAt = 0;
    std::vector<uint16_t> _closeLater;
    uint32_t _lineBaud = 0;
    std::string _dialed, _lastResult;

    uint64_t _dials = 0, _connects = 0, _failures = 0, _escapes = 0, _rings = 0, _answered = 0;
    uint64_t _bytesToLine = 0, _bytesFromLine = 0;
    std::deque<Exchange> _exchanges;
    bool _inTransmit = false;
};
