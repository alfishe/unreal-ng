#include "emulator/io/serial/hayesmodempeer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "emulator/io/network/virtualnetwork.h"

namespace
{
// S-registers (Hayes Smartmodem / V.250) and their factory values
constexpr int kSAutoAnswer = 0;    ///< rings before answering (0: never)
constexpr int kSRingCount = 1;
constexpr int kSEscape = 2;        ///< '+'
constexpr int kSCr = 3;
constexpr int kSLf = 4;
constexpr int kSBs = 5;
constexpr int kSWaitCarrier = 7;   ///< seconds a dialed call may take to connect
constexpr int kSGuard = 12;        ///< escape guard time, 1/50 s

// Numeric result codes (Hayes; the CONNECT rates as USRobotics / Hayes Optima number them)
constexpr int kOk = 0, kConnect = 1, kRing = 2, kNoCarrier = 3, kError = 4, kNoDialtone = 6, kBusy = 7,
              kNoAnswer = 8;

std::string Trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
        ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])))
        --e;
    return s.substr(b, e - b);
}

std::string DigitsOf(const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        if (c >= '0' && c <= '9')
            out.push_back(c);
    }
    return out;
}

int ConnectCode(uint32_t baud)
{
    switch (baud)
    {
        case 300: return 1;
        case 1200: return 5;
        case 2400: return 10;
        case 4800: return 11;
        case 9600: return 12;
        case 7200: return 13;
        case 12000: return 14;
        case 14400: return 15;
        case 19200: return 16;
        case 38400: return 17;
        case 57600: return 18;
        case 115200: return 19;
        default: return kConnect;
    }
}

constexpr uint8_t kFactoryS[netstate::kModemSRegs] = {
    0, 0, 43, 13, 10, 8, 2, 50, 2, 6, 14, 95, 50, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
}  // namespace

HayesModemPeer::HayesModemPeer(VirtualNetwork* network, const ComPortSpec& spec, const std::string& phonebook)
    : _network(network), _link(network, StreamPeer::Dialer{this}), _listenPort(spec.port)
{
    std::string error;
    ParsePhonebook(phonebook, _phonebook, error);   // NetworkManager checked it; a bad entry is skipped
    _link.onLinkChange = [this](StreamPeer::Phase phase, NetEventStatus status, const std::string& why) {
        OnLinkChange(phase, status, why);
    };
    FactoryReset();
    if (_network && _listenPort)
    {
        _listenSocket = _network->Open(NetProto::Tcp, this, kCookieListen);
        if (_listenSocket)
            _network->Listen(_listenSocket, _listenPort);
    }
}

HayesModemPeer::~HayesModemPeer()
{
    _link.onLinkChange = nullptr;
    _link.HangUp();
    if (_network)
    {
        if (_listenSocket)
            _network->Close(_listenSocket);
        if (_ringSocket)
            _network->Close(_ringSocket);
        for (uint16_t id : _closeLater)
            _network->Close(id);
        if (_link.Socket())
            _network->Close(_link.Socket());
    }
}

bool HayesModemPeer::ParsePhonebook(const std::string& text, std::map<std::string, std::string>& out, std::string& error)
{
    out.clear();
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find(',', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string entry = Trim(text.substr(start, end - start));
        start = end + 1;
        if (entry.empty())
        {
            if (end == text.size())
                break;
            continue;
        }
        const size_t eq = entry.find('=');
        const std::string number = DigitsOf(eq == std::string::npos ? std::string() : entry.substr(0, eq));
        const std::string target = eq == std::string::npos ? std::string() : Trim(entry.substr(eq + 1));
        ComPortSpec spec;
        std::string why;
        std::string tcp = target;
        if (tcp.find(':') == std::string::npos)
            tcp += ":23";
        if (number.empty() || target.empty() || !ComPortSpec::Parse("TCP:" + tcp, spec, why))
        {
            error = "phone book entry '" + entry + "': expected <number>=<host>[:<port>]";
            return false;
        }
        out[number] = spec.ToString();
        if (end == text.size())
            break;
    }
    return true;
}

void HayesModemPeer::FactoryReset()
{
    std::memcpy(_s, kFactoryS, sizeof(_s));
    _echo = true;
    _quiet = false;
    _verbose = true;
    _resultSet = 4;
    _dcdMode = 1;
    _dtrMode = 2;
    _dsrMode = 0;
}

uint64_t HayesModemPeer::GuardT() const
{
    return MicrosToT(static_cast<uint64_t>(_s[kSGuard]) * 20000u);
}

bool HayesModemPeer::Dsr() const
{
    return _dsrMode == 0 || Carrier();
}

bool HayesModemPeer::Dcd() const
{
    return _dcdMode == 0 || Carrier();
}

std::string HayesModemPeer::Target() const
{
    if (_dialed.empty())
        return {};
    std::string target = _dialed;
    const NetEndpoint remote = _link.Remote();
    if (remote.addr)
        target += " (" + NetIpToString(remote.addr) + ":" + std::to_string(remote.port) + ")";
    return target;
}

size_t HayesModemPeer::Pending() const
{
    return _out.size() + (_mode == Mode::Online ? _link.Pending() : 0);
}

bool HayesModemPeer::HasByte() const
{
    if (!_out.empty())
        return true;
    return _mode == Mode::Online && _link.HasByte();
}

uint8_t HayesModemPeer::TakeByte()
{
    if (!_out.empty())
    {
        const uint8_t b = _out.front();
        _out.pop_front();
        return b;
    }
    const uint8_t b = _link.TakeByte();
    ++_bytesFromLine;
    if (_dropPending && !_link.HasByte())
        CallEnded();   // the call's last byte reached the ZX: now the carrier is gone
    return b;
}

void HayesModemPeer::Notify()
{
    // Not from inside the UART's own call (Transmit runs in its Advance): it looks at the peer right after
    if (onReceive && !_inTransmit)
        onReceive();
}

void HayesModemPeer::Emit(const std::string& bytes)
{
    for (char c : bytes)
    {
        if (_out.size() >= static_cast<size_t>(netstate::kModemOut))
            break;
        _out.push_back(static_cast<uint8_t>(c));
    }
}

void HayesModemPeer::Echo(uint8_t byte)
{
    if (_echo && _out.size() < static_cast<size_t>(netstate::kModemOut))
        _out.push_back(byte);
}

void HayesModemPeer::Log(const std::string& command, const std::string& result)
{
    if (!command.empty() || _exchanges.empty() || !_exchanges.back().result.empty() || _exchanges.back().command.empty())
    {
        _exchanges.push_back({Now(), command, result});
        while (_exchanges.size() > kExchanges)
            _exchanges.pop_front();
        return;
    }
    _exchanges.back().result = result;   // the result of the command logged last (ATD's CONNECT)
}

void HayesModemPeer::Result(int code, const std::string& text)
{
    _lastResult = text;
    Log(std::string(), text);
    if (_quiet)
        return;
    const char cr = static_cast<char>(_s[kSCr]), lf = static_cast<char>(_s[kSLf]);
    if (_verbose)
        Emit(std::string{cr, lf} + text + std::string{cr, lf});
    else
        Emit(std::to_string(code) + std::string{cr});
}

void HayesModemPeer::ResultConnect()
{
    const uint32_t baud = _lineBaud ? _lineBaud : 57600;
    if (_resultSet == 0)
        Result(kConnect, "CONNECT");
    else
        Result(ConnectCode(baud), "CONNECT " + std::to_string(baud));
}

void HayesModemPeer::Transmit(uint8_t byte)
{
    struct Guard
    {
        bool& flag;
        explicit Guard(bool& f) : flag(f) { flag = true; }
        ~Guard() { flag = false; }
    } guard(_inTransmit);
    const uint64_t now = Now();
    if (_mode == Mode::Online)
    {
        // Escape: S12 of silence, three S2 characters each within S12 of the one before, then S12 of silence (that
        // last part is OnFrame's). The pluses go to the other end as well, as on a real modem
        const uint64_t guardT = GuardT();
        if (byte == _s[kSEscape] && _s[kSEscape] < 128)
        {
            if (_plusCount == 0 && now - _lastDataAt >= guardT)
                _plusCount = 1;
            else if (_plusCount > 0 && _plusCount < 3 && now - _lastPlusAt < guardT)
                ++_plusCount;
            else
                _plusCount = 0;
            _lastPlusAt = now;
        }
        else
            _plusCount = 0;
        _lastDataAt = now;
        _link.Transmit(byte);
        ++_bytesToLine;
        return;
    }
    if (_mode == Mode::Dialing)
    {
        // Any character aborts the dial
        HangUp();
        _mode = Mode::Command;
        ++_failures;
        Result(kNoCarrier, "NO CARRIER");
        return;
    }

    // Command mode (no call, or a call held after +++)
    const uint8_t c = static_cast<uint8_t>(byte & 0x7F);   // parity bit ignored, as a modem's autobaud does
    if (c == _s[kSBs])
    {
        if (!_command.empty())
        {
            _command.pop_back();
            if (_echo)
                Emit(std::string{static_cast<char>(_s[kSBs]), ' ', static_cast<char>(_s[kSBs])});
        }
        return;
    }
    Echo(byte);
    if (c == _s[kSCr])
    {
        const std::string line = _command;
        _command.clear();
        // Only a line that starts with AT is a command (anything else typed in command mode is ignored)
        size_t at = std::string::npos;
        for (size_t i = 0; i + 1 < line.size(); ++i)
        {
            if ((line[i] == 'A' || line[i] == 'a') && (line[i + 1] == 'T' || line[i + 1] == 't'))
            {
                at = i;
                break;
            }
        }
        if (at == std::string::npos)
            return;
        _lastCommand = line.substr(at);
        Execute(_lastCommand);
        return;
    }
    if (c == '/' && !_command.empty() && (_command.back() == 'A' || _command.back() == 'a'))
    {
        // A/ repeats the last command line at once
        _command.clear();
        Emit(std::string{static_cast<char>(_s[kSCr]), static_cast<char>(_s[kSLf])});
        if (!_lastCommand.empty())
            Execute(_lastCommand);
        return;
    }
    if (c >= 0x20 && _command.size() < static_cast<size_t>(netstate::kModemCommand - 1))
        _command.push_back(static_cast<char>(c));
}

void HayesModemPeer::Execute(const std::string& line)
{
    Log(line, std::string());
    // Commands after "AT"; spaces between them are ignored
    std::string body;
    for (size_t i = 2; i < line.size(); ++i)
    {
        if (line[i] != ' ')
            body.push_back(line[i]);
    }
    size_t i = 0;
    auto number = [&](int fallback) {
        if (i >= body.size() || !std::isdigit(static_cast<unsigned char>(body[i])))
            return fallback;
        int v = 0;
        while (i < body.size() && std::isdigit(static_cast<unsigned char>(body[i])) && v < 1000)
            v = v * 10 + (body[i++] - '0');
        return v;
    };
    auto error = [&]() { Result(kError, "ERROR"); };

    while (i < body.size())
    {
        const char cmd = static_cast<char>(std::toupper(static_cast<unsigned char>(body[i++])));
        switch (cmd)
        {
            case 'D':
            {
                // The rest of the line is the number (dial modifiers inside)
                std::string rest;
                for (size_t k = 2; k < line.size(); ++k)
                    rest.push_back(line[k]);
                const size_t d = rest.find_first_of("Dd");
                Dial(Trim(rest.substr(d + 1)));
                return;
            }
            case 'A':
                Answer();
                return;
            case 'O':
                number(0);
                if (_mode == Mode::Escaped && _link.GetPhase() == StreamPeer::Phase::Connected)
                {
                    _mode = Mode::Online;
                    _plusCount = 0;
                    _lastDataAt = Now();
                    ResultConnect();
                }
                else
                    Result(kNoCarrier, "NO CARRIER");
                return;
            case 'H':
            {
                const int v = number(0);
                if (v > 1)
                    return error();
                if (_mode == Mode::Escaped || _mode == Mode::Online)
                    HangUp();
                _mode = Mode::Command;
                _offHook = v == 1;
                break;
            }
            case 'Z':
                number(0);
                if (_mode != Mode::Command)
                    HangUp();
                _mode = Mode::Command;
                FactoryReset();
                StopRinging();
                break;
            case 'E':
            {
                const int v = number(0);
                if (v > 1)
                    return error();
                _echo = v == 1;
                break;
            }
            case 'Q':
            {
                const int v = number(0);
                if (v > 1)
                    return error();
                _quiet = v == 1;
                break;
            }
            case 'V':
            {
                const int v = number(0);
                if (v > 1)
                    return error();
                _verbose = v == 1;
                break;
            }
            case 'X':
            {
                const int v = number(0);
                if (v > 4)
                    return error();
                _resultSet = static_cast<uint8_t>(v);
                break;
            }
            case 'S':
            {
                const int reg = number(-1);
                if (reg < 0 || reg >= netstate::kModemSRegs)
                    return error();
                if (i < body.size() && body[i] == '=')
                {
                    ++i;
                    const int v = number(0);
                    if (v > 255)
                        return error();
                    _s[reg] = static_cast<uint8_t>(v);
                }
                else if (i < body.size() && body[i] == '?')
                {
                    ++i;
                    char text[8];
                    std::snprintf(text, sizeof(text), "%03u", _s[reg]);
                    Emit(std::string{static_cast<char>(_s[kSCr]), static_cast<char>(_s[kSLf])} + text +
                         std::string{static_cast<char>(_s[kSCr]), static_cast<char>(_s[kSLf])});
                }
                break;
            }
            case 'I':
            {
                const int v = number(0);
                static const char* const kInfo[] = {"960", "000", "OK", "UNREAL-NG HAYES MODEM (DIALS HOST:PORT)",
                                                    "TCP OVER THE VIRTUAL NETWORK"};
                if (v > 4)
                    return error();
                if (v != 2)
                    Emit(std::string{static_cast<char>(_s[kSCr]), static_cast<char>(_s[kSLf])} + kInfo[v] +
                         std::string{static_cast<char>(_s[kSCr]), static_cast<char>(_s[kSLf])});
                break;
            }
            case 'L': case 'M': case 'B': case 'N': case 'W': case 'Y': case 'P': case 'T':
                number(0);   // speaker, protocol, dial method: nothing to do here
                break;
            case '&':
            {
                if (i >= body.size())
                    return error();
                const char sub = static_cast<char>(std::toupper(static_cast<unsigned char>(body[i++])));
                const int v = number(0);
                switch (sub)
                {
                    case 'C':
                        if (v > 1)
                            return error();
                        _dcdMode = static_cast<uint8_t>(v);
                        break;
                    case 'D':
                        if (v > 3)
                            return error();
                        _dtrMode = static_cast<uint8_t>(v);
                        break;
                    case 'S':
                        if (v > 1)
                            return error();
                        _dsrMode = static_cast<uint8_t>(v);
                        break;
                    case 'F':
                        FactoryReset();
                        break;
                    case 'V':
                    {
                        char text[160];
                        std::snprintf(text, sizeof(text), "E%d Q%d V%d X%u &C%u &D%u &S%u S0:%03u S2:%03u S7:%03u S12:%03u",
                                      _echo ? 1 : 0, _quiet ? 1 : 0, _verbose ? 1 : 0, _resultSet, _dcdMode, _dtrMode,
                                      _dsrMode, _s[0], _s[2], _s[7], _s[12]);
                        Emit(std::string{static_cast<char>(_s[kSCr]), static_cast<char>(_s[kSLf])} + text +
                             std::string{static_cast<char>(_s[kSCr]), static_cast<char>(_s[kSLf])});
                        break;
                    }
                    case 'K': case 'Q': case 'W': case 'Y': case 'G': case 'J': case 'B': case 'H': case 'I':
                    case 'R': case 'M': case 'A': case 'N': case 'U': case 'T': case 'P': case 'E': case 'X':
                        break;   // flow control, storage, line options: accepted
                    default:
                        return error();
                }
                break;
            }
            case '\\':
            case '%':
                if (i < body.size())
                    ++i;
                number(0);
                break;
            case '+':
                i = body.size();   // extended (V.250) commands: accepted as a whole
                break;
            default:
                return error();
        }
    }
    Result(kOk, "OK");
}

void HayesModemPeer::Dial(const std::string& dialString)
{
    if (_mode == Mode::Escaped || _mode == Mode::Online)
    {
        Result(kError, "ERROR");   // a call is up: ATH first
        return;
    }
    StopRinging();
    std::string s = dialString;
    // ';' ends the number (return to command mode after dialing: a data modem connects anyway)
    const size_t semi = s.find(';');
    if (semi != std::string::npos)
        s = s.substr(0, semi);
    s = Trim(s);
    if (!s.empty() && (s[0] == 'L' || s[0] == 'l') && s.size() == 1)
        s = _dialed;   // ATDL: the last number
    else if (!s.empty() && (s[0] == 'T' || s[0] == 't' || s[0] == 'P' || s[0] == 'p'))
        s = Trim(s.substr(1));   // tone / pulse
    std::string compact;
    for (char c : s)
    {
        if (c != ' ')
            compact.push_back(c);
    }
    if (compact.empty())
    {
        // ATD alone: off hook, no number - a data modem waits for a carrier that never comes
        Result(kNoCarrier, "NO CARRIER");
        return;
    }
    ++_dials;
    _dialed = compact;
    bool host = false;
    for (char c : compact)
    {
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '.' || c == ':')
            host = true;
    }
    // A number of digits, '-', '(', ')' and the pause modifiers W , @ ! is looked up; W and '@' in a host name keep it
    // a host name
    std::string text;
    if (host && compact.find_first_not_of("0123456789-()Ww,@!") != std::string::npos)
        text = "TCP:" + (compact.find(':') == std::string::npos ? compact + ":23" : compact);
    else
    {
        const auto it = _phonebook.find(DigitsOf(compact));
        if (it == _phonebook.end())
        {
            ++_failures;
            Result(kNoCarrier, "NO CARRIER");   // nobody answers that number
            return;
        }
        text = it->second;
    }
    ComPortSpec spec;
    std::string why;
    if (!ComPortSpec::Parse(text, spec, why) || spec.kind != ComPortSpec::Kind::Tcp)
    {
        ++_failures;
        Result(kNoCarrier, "NO CARRIER");
        return;
    }
    _mode = Mode::Dialing;
    _dialDeadline = Now() + MicrosToT(static_cast<uint64_t>(_s[kSWaitCarrier] ? _s[kSWaitCarrier] : 1) * 1000000u);
    _link.Dial(spec);
    if (_link.GetPhase() == StreamPeer::Phase::Idle && _mode == Mode::Dialing)
    {
        // Refused on the spot (no network, no sockets)
        _mode = Mode::Command;
        ++_failures;
        Result(_resultSet >= 2 ? kNoDialtone : kNoCarrier, _resultSet >= 2 ? "NO DIALTONE" : "NO CARRIER");
    }
}

void HayesModemPeer::OnLinkChange(StreamPeer::Phase phase, NetEventStatus status, const std::string& why)
{
    (void)why;
    if (_mode == Mode::Dialing)
    {
        if (phase == StreamPeer::Phase::Connected)
        {
            _mode = Mode::Online;
            ++_connects;
            _plusCount = 0;
            _lastDataAt = Now();
            ResultConnect();
        }
        else
        {
            _mode = Mode::Command;
            ++_failures;
            if (status == NetEventStatus::Refused && _resultSet >= 3)
                Result(kBusy, "BUSY");
            else if ((status == NetEventStatus::Timeout || status == NetEventStatus::Unreachable) && _resultSet >= 3)
                Result(kNoAnswer, "NO ANSWER");
            else
                Result(kNoCarrier, "NO CARRIER");
        }
        Notify();
        return;
    }
    if (phase != StreamPeer::Phase::Connected && (_mode == Mode::Online || _mode == Mode::Escaped))
    {
        // The other end hung up: what it sent before still reaches the ZX, then NO CARRIER
        if (_mode == Mode::Online && _link.HasByte())
            _dropPending = true;
        else
            CallEnded();
        Notify();
        return;
    }
    if (phase != StreamPeer::Phase::Connected && _ringing)
    {
        StopRinging();   // the caller gave up
        Notify();
    }
}

void HayesModemPeer::CallEnded()
{
    _dropPending = false;
    _mode = Mode::Command;
    _plusCount = 0;
    _link.HangUp();
    Result(kNoCarrier, "NO CARRIER");
}

void HayesModemPeer::HangUp()
{
    _link.HangUp();
    _dropPending = false;
    _plusCount = 0;
    _offHook = false;
}

void HayesModemPeer::Answer()
{
    if (!_ringing || !_ringSocket)
    {
        Result(kNoCarrier, "NO CARRIER");   // no call: no carrier to find
        return;
    }
    const uint16_t socket = _ringSocket;
    _ringSocket = 0;
    StopRinging();
    _mode = Mode::Online;
    ++_answered;
    ++_connects;
    _plusCount = 0;
    _lastDataAt = Now();
    _dialed = "(incoming)";
    (void)socket;   // the link adopted the socket when the call came in
    ResultConnect();
}

void HayesModemPeer::StopRinging()
{
    if (_ringing && _ringSocket)
    {
        // An unanswered call: the caller is turned away
        _link.HangUp();
        _ringSocket = 0;
    }
    _ringing = false;
    _ri = false;
    _ringCount = 0;
    _s[kSRingCount] = 0;
}

void HayesModemPeer::CloseLater(uint16_t socket)
{
    if (socket && _closeLater.size() < static_cast<size_t>(netstate::kModemCloses))
        _closeLater.push_back(socket);
}

void HayesModemPeer::OnModemLines(bool rts, bool dtr)
{
    (void)rts;
    const bool fell = _dtr && !dtr;
    _dtr = dtr;
    if (!fell || (_mode != Mode::Online && _mode != Mode::Escaped && _mode != Mode::Dialing))
        return;
    switch (_dtrMode)
    {
        case 1:
            if (_mode == Mode::Online)
            {
                _mode = Mode::Escaped;
                Result(kOk, "OK");
            }
            break;
        case 2:
        case 3:
            HangUp();
            _mode = Mode::Command;
            if (_dtrMode == 3)
                FactoryReset();
            Result(kOk, "OK");
            break;
        default:
            break;
    }
}

void HayesModemPeer::OnNetEvent(uint32_t cookie, NetEventType type, NetEventStatus status, const NetEndpoint& peer,
                                const uint8_t* data, uint32_t length, uint32_t source)
{
    if (cookie != kCookieListen)
    {
        _link.OnNetEvent(cookie, type, status, peer, data, length, source);
        if (type == NetEventType::Data && _mode == Mode::Online && length)
            Notify();
        return;
    }
    if (type != NetEventType::Accepted)
        return;
    // A caller: the listening socket is its connection now; a new listener comes at the frame boundary
    const uint16_t caller = _listenSocket;
    _listenSocket = 0;
    if (_mode != Mode::Command || _ringing || _link.GetPhase() != StreamPeer::Phase::Idle)
    {
        CloseLater(caller);   // busy
        return;
    }
    if (_network)
        _network->Rebind(caller, this, StreamPeer::kCookieLink);
    _link.Adopt(caller, peer);
    _ringSocket = caller;
    _ringing = true;
    _ringCount = 0;
    _nextRingAt = Now();   // the first ring at the next frame boundary
}

void HayesModemPeer::OnFrame()
{
    const uint64_t now = Now();
    for (uint16_t id : _closeLater)
    {
        if (_network)
            _network->Close(id);
    }
    _closeLater.clear();
    if (_network && _listenPort && !_listenSocket)
    {
        _listenSocket = _network->Open(NetProto::Tcp, this, kCookieListen);
        if (_listenSocket)
            _network->Listen(_listenSocket, _listenPort);
    }

    bool changed = false;
    // The escape's trailing guard time
    if (_mode == Mode::Online && _plusCount == 3 && now - _lastPlusAt >= GuardT())
    {
        _plusCount = 0;
        _mode = Mode::Escaped;
        _escapedAt = now;
        ++_escapes;
        Result(kOk, "OK");
        changed = true;
    }
    if (_mode == Mode::Dialing && now >= _dialDeadline)
    {
        HangUp();
        _mode = Mode::Command;
        ++_failures;
        Result(_resultSet >= 3 ? kNoAnswer : kNoCarrier, _resultSet >= 3 ? "NO ANSWER" : "NO CARRIER");
        changed = true;
    }
    // Ringing: RI 2 s on, 4 s off; RING at each start; S0 rings answer
    if (_ringing)
    {
        if (_ri && now >= _riOffAt)
        {
            _ri = false;
            changed = true;
        }
        if (now >= _nextRingAt)
        {
            if (_ringCount >= 30)
                StopRinging();   // nobody answers: the caller is let go
            else
            {
                ++_ringCount;
                ++_rings;
                _s[kSRingCount] = static_cast<uint8_t>(_ringCount);
                _ri = true;
                _riOffAt = now + MicrosToT(2000000u);
                _nextRingAt = now + MicrosToT(6000000u);
                Result(kRing, "RING");
                if (_s[kSAutoAnswer] && _ringCount >= _s[kSAutoAnswer])
                    Answer();
            }
            changed = true;
        }
    }
    _link.OnFrame();
    if (changed)
        Notify();
}

void HayesModemPeer::Reset()
{
    // The machine's reset does not reach a modem on a cable; an internal card's reset is the card's (its UART)
}

void HayesModemPeer::Describe(StateNode& out) const
{
    static const char* const kModes[] = {"command", "dialing", "online", "online_command"};
    out["mode"] = kModes[static_cast<int>(_mode) & 3];
    out["carrier"] = Carrier();
    StateNode lines = StateNode::Object();
    lines["cts"] = Cts();
    lines["dsr"] = Dsr();
    lines["dcd"] = Dcd();
    lines["ri"] = _ri;
    lines["dtr_from_zx"] = _dtr;
    out["lines"] = lines;
    StateNode call = StateNode::Object();
    call["dialed"] = _dialed;
    static const char* const kPhases[] = {"idle", "resolving", "connecting", "connected"};
    call["link"] = kPhases[static_cast<int>(_link.GetPhase()) & 3];
    const NetEndpoint remote = _link.Remote();
    if (remote.addr)
        call["remote"] = NetIpToString(remote.addr) + ":" + std::to_string(remote.port);
    if (!_link.LastError().empty())
        call["error"] = _link.LastError();
    call["ringing"] = _ringing;
    call["rings"] = static_cast<uint64_t>(_ringCount);
    call["held_bytes"] = static_cast<uint64_t>(_link.Pending());
    out["call"] = call;
    out["last_result"] = _lastResult;
    // What is typed so far in command mode (printable; a command runs at S3)
    std::string typed;
    for (char c : _command)
        typed.push_back(c >= 0x20 && c < 0x7F ? c : '.');
    out["command_line"] = typed;
    if (_listenPort)
        out["answers_port"] = static_cast<int>(_listenPort);
    StateNode settings = StateNode::Object();
    settings["echo"] = _echo;
    settings["quiet"] = _quiet;
    settings["verbose"] = _verbose;
    settings["result_set"] = static_cast<int>(_resultSet);
    settings["dcd_mode"] = static_cast<int>(_dcdMode);
    settings["dtr_mode"] = static_cast<int>(_dtrMode);
    settings["dsr_mode"] = static_cast<int>(_dsrMode);
    settings["s0_auto_answer"] = static_cast<int>(_s[kSAutoAnswer]);
    settings["s7_wait_carrier_s"] = static_cast<int>(_s[kSWaitCarrier]);
    settings["s12_guard_50ths"] = static_cast<int>(_s[kSGuard]);
    out["settings"] = settings;
    StateNode phonebook = StateNode::Array();
    for (const auto& [number, target] : _phonebook)
        phonebook.push(StateNode(number + "=" + target.substr(4)));
    out["phonebook"] = phonebook;
    StateNode counters = StateNode::Object();
    counters["dials"] = _dials;
    counters["connects"] = _connects;
    counters["failures"] = _failures;
    counters["escapes"] = _escapes;
    counters["rings"] = _rings;
    counters["answered"] = _answered;
    counters["bytes_to_line"] = _bytesToLine;
    counters["bytes_from_line"] = _bytesFromLine;
    out["counters"] = counters;
    StateNode log = StateNode::Array();
    for (const Exchange& e : _exchanges)
    {
        StateNode x = StateNode::Object();
        x["t"] = e.at;
        x["command"] = e.command;
        x["result"] = e.result;
        log.push(std::move(x));
    }
    out["journal"] = log;
}

void HayesModemPeer::SaveState(netstate::HayesModemState& o) const
{
    std::memset(&o, 0, sizeof(o));
    o.present = 1;
    o.mode = static_cast<uint8_t>(_mode);
    o.echo = _echo;
    o.quiet = _quiet;
    o.verbose = _verbose;
    o.resultSet = _resultSet;
    o.dcdMode = _dcdMode;
    o.dtrMode = _dtrMode;
    o.dsrMode = _dsrMode;
    o.dtr = _dtr;
    o.plusCount = _plusCount;
    o.dropPending = _dropPending;
    o.ringing = _ringing;
    o.ri = _ri;
    o.offHook = _offHook;
    std::memcpy(o.sregs, _s, sizeof(o.sregs));
    o.commandLength = static_cast<uint16_t>(std::min<size_t>(_command.size(), netstate::kModemCommand));
    std::memcpy(o.command, _command.data(), o.commandLength);
    o.lastCommandLength = static_cast<uint16_t>(std::min<size_t>(_lastCommand.size(), netstate::kModemCommand));
    std::memcpy(o.lastCommand, _lastCommand.data(), o.lastCommandLength);
    o.outLength = static_cast<uint16_t>(std::min<size_t>(_out.size(), netstate::kModemOut));
    for (size_t k = 0; k < o.outLength; ++k)
        o.out[k] = _out[k];
    o.listenSocket = _listenSocket;
    o.ringSocket = _ringSocket;
    o.closeCount = static_cast<uint16_t>(std::min<size_t>(_closeLater.size(), netstate::kModemCloses));
    for (size_t k = 0; k < o.closeCount; ++k)
        o.closeLater[k] = _closeLater[k];
    const ComPortSpec& spec = _link.Spec();
    std::snprintf(o.specHost, sizeof(o.specHost), "%s", spec.host.c_str());
    o.specAddr = spec.addr;
    o.specPort = spec.port;
    o.remoteAddr = _link.Remote().addr;
    o.remotePort = _link.Remote().port;
    o.lineBaud = _lineBaud;
    o.ringCount = _ringCount;
    o.lastDataAt = _lastDataAt;
    o.lastPlusAt = _lastPlusAt;
    o.dialDeadline = _dialDeadline;
    o.nextRingAt = _nextRingAt;
    o.riOffAt = _riOffAt;
    o.escapedAt = _escapedAt;
    o.dials = _dials;
    o.connects = _connects;
    o.failures = _failures;
    o.escapes = _escapes;
    o.rings = _rings;
    o.answered = _answered;
    o.bytesToLine = _bytesToLine;
    o.bytesFromLine = _bytesFromLine;
    std::snprintf(o.dialed, sizeof(o.dialed), "%s", _dialed.c_str());
    std::snprintf(o.lastResult, sizeof(o.lastResult), "%s", _lastResult.c_str());
}

void HayesModemPeer::LoadState(const netstate::HayesModemState& o)
{
    if (!o.present)
        return;
    _mode = o.mode <= static_cast<uint8_t>(Mode::Escaped) ? static_cast<Mode>(o.mode) : Mode::Command;
    _echo = o.echo != 0;
    _quiet = o.quiet != 0;
    _verbose = o.verbose != 0;
    _resultSet = o.resultSet;
    _dcdMode = o.dcdMode;
    _dtrMode = o.dtrMode;
    _dsrMode = o.dsrMode;
    _dtr = o.dtr != 0;
    _plusCount = o.plusCount;
    _dropPending = o.dropPending != 0;
    _ringing = o.ringing != 0;
    _ri = o.ri != 0;
    _offHook = o.offHook != 0;
    std::memcpy(_s, o.sregs, sizeof(_s));
    _command.assign(reinterpret_cast<const char*>(o.command), std::min<size_t>(o.commandLength, netstate::kModemCommand));
    _lastCommand.assign(reinterpret_cast<const char*>(o.lastCommand),
                        std::min<size_t>(o.lastCommandLength, netstate::kModemCommand));
    _out.assign(o.out, o.out + std::min<size_t>(o.outLength, netstate::kModemOut));
    _listenSocket = o.listenSocket;
    _ringSocket = o.ringSocket;
    _closeLater.assign(o.closeLater, o.closeLater + std::min<size_t>(o.closeCount, netstate::kModemCloses));
    ComPortSpec spec;
    spec.kind = ComPortSpec::Kind::Tcp;
    spec.host.assign(o.specHost, strnlen(o.specHost, sizeof(o.specHost)));
    spec.addr = o.specAddr;
    spec.port = o.specPort;
    _link.SetDialSpec(spec);
    _link.SetRemote(NetEndpoint{o.remoteAddr, o.remotePort});
    _lineBaud = o.lineBaud;
    _ringCount = o.ringCount;
    _lastDataAt = o.lastDataAt;
    _lastPlusAt = o.lastPlusAt;
    _dialDeadline = o.dialDeadline;
    _nextRingAt = o.nextRingAt;
    _riOffAt = o.riOffAt;
    _escapedAt = o.escapedAt;
    _dials = o.dials;
    _connects = o.connects;
    _failures = o.failures;
    _escapes = o.escapes;
    _rings = o.rings;
    _answered = o.answered;
    _bytesToLine = o.bytesToLine;
    _bytesFromLine = o.bytesFromLine;
    _dialed.assign(o.dialed, strnlen(o.dialed, sizeof(o.dialed)));
    _lastResult.assign(o.lastResult, strnlen(o.lastResult, sizeof(o.lastResult)));
}
