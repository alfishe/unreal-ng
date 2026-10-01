#include "emulator/io/network/networkmanager.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "base/featuremanager.h"
#include "common/network/hostnetbridge.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/mainloop.h"
#include "emulator/io/network/networkspec.h"
#include "emulator/io/serial/comportspec.h"
#include "emulator/io/serial/esp/atmodule.h"
#include "emulator/io/serial/esp/espnetmodule.h"
#include "emulator/ports/portdecoder.h"

NetworkManager::NetworkManager(EmulatorContext* context) : _context(context)
{
}

NetworkManager::~NetworkManager()
{
    Unplug();
}

NetworkManager::Plan NetworkManager::MakePlan() const
{
    Plan plan;
    if (!_context)
        return plan;
    const auto& net = _context->config.network;
    const bool networkOn = !_context->pFeatureManager || _context->pFeatureManager->isEnabled(Features::kNetwork);
    PortDecoder* decoder = _context->pPortDecoder;
    const PortDecoder::NetworkCapabilities caps = decoder ? decoder->DescribeNetwork() : PortDecoder::NetworkCapabilities();
    using SerialPort = PortDecoder::NetworkCapabilities::SerialPort;

    auto peerOf = [](const char* text, const char* fallback) {
        ComPortSpec spec;
        std::string error;
        if (!text || !*text || !ComPortSpec::Parse(text, spec, error))
            ComPortSpec::Parse(fallback, spec, error);
        return spec.kind == ComPortSpec::Kind::None ? std::string() : spec.ToString();
    };
    const std::string comPeer = peerOf(net.comPort, "NONE");

    // The machine's own serial port: mainboard hardware, there with the network off
    if (caps.serialPort == SerialPort::EvoAvr)
    {
        plan.serial = Plan::Serial::EvoAvr;
        if (networkOn)
            plan.peer = comPeer;
    }
    else if (caps.serialPort == SerialPort::ZiFi && !comPeer.empty())
        plan.notes.push_back("ComPort: the machine's serial port (ZiFi) is not emulated yet");
    else if (caps.serialPort == SerialPort::None && !comPeer.empty())
        plan.notes.push_back("ComPort: the machine has no serial port of its own - a ZX-WiFi card adds one (Card=ZXWIFI)");

    if (!networkOn)
        return plan;
    const uint8_t cards = net.card;
    if (cards && !caps.zxBus)
    {
        plan.notes.push_back("Card: the machine has no ZX-Bus");
        return plan;
    }
    plan.zxNetUsb = (cards & networkspec::kCardZxNetUsb) != 0;
    if (cards & networkspec::kCardZxWifi)
    {
        // The card's 16550 sits on #F8EF..#FFEF: a clash with the machine's own #xxEF device disables the card
        if (plan.serial != Plan::Serial::None || caps.serialPort != SerialPort::None ||
            (decoder && decoder->ReservesLowByte(ComPort::kPortLowByte)))
            plan.notes.push_back(caps.serialPort == SerialPort::EvoAvr
                                     ? "ZXWIFI: not fitted - ports #F8EF..#FFEF are the ZX-Evo AVR's COM port"
                                     : "ZXWIFI: not fitted - the machine owns ports #xxEF");
        else
        {
            plan.serial = Plan::Serial::ZxWifi;
            plan.peer = peerOf(net.zxWifi, "AT");
        }
    }
    return plan;
}

void NetworkManager::FitCom(const Plan& plan, const Uart16550::State* keep)
{
    ComPortSpec spec;
    std::string error;
    ComPortSpec::Parse(plan.peer.empty() ? "NONE" : plan.peer, spec, error);
    const EspModule::Chip chip =
        _context->config.network.espChip == 1 ? EspModule::Chip::Esp8266 : EspModule::Chip::Esp32;
    std::unique_ptr<ISerialPeer> peer;
    switch (spec.kind)
    {
        case ComPortSpec::Kind::Loopback:
            peer = std::make_unique<LoopbackPeer>();
            break;
        case ComPortSpec::Kind::Tcp:
        case ComPortSpec::Kind::Serial:
            peer = std::make_unique<StreamPeer>(_network.get(), spec, _context->config.network.comModemLines != 0);
            break;
        case ComPortSpec::Kind::At:
            peer = std::make_unique<AtModule>(_network.get(), chip);
            break;
        case ComPortSpec::Kind::Espnet:
            peer = std::make_unique<EspnetModule>(_network.get(), chip);
            break;
        default:
            break;   // nothing on the line: the registers still answer
    }
    PortDecoder* decoder = _context->pPortDecoder;
    Uart16550::Params params = Uart16550::DefaultParams(Uart16550::Flavor::Chip16550);
    ComPort::RegisterOf registerOf;
    if (plan.serial == Plan::Serial::EvoAvr && decoder)
    {
        PortDecoder::NetworkCapabilities caps = decoder->DescribeNetwork();
        params = caps.uart;
        registerOf = std::move(caps.serialRegister);
    }
    _com = std::make_unique<ComPort>(_context, params, std::move(peer), std::move(registerOf));
    if (keep)
        _com->Uart().LoadState(*keep);   // the same chip, a new cable: its registers stay
    if (decoder)
        _com->AttachToPorts(decoder);
    _context->pComPort = _com.get();
}

void NetworkManager::ApplyConfiguration()
{
    if (!_context)
        return;
    const bool loopRunning = _context->pEmulator && _context->pEmulator->IsRunning();
    const bool onLoopThread = _context->pMainLoop && _context->pMainLoop->IsRunThread();
    if (loopRunning && !onLoopThread)
    {
        _refitPending = true;   // the machine thread picks it up at the next frame boundary
        return;
    }
    Refit();
}

void NetworkManager::Refit()
{
    _refitPending = false;

    // A settings change from automation: into the machine config, here on the machine thread
    std::optional<Change> change;
    {
        std::lock_guard<std::mutex> lock(_changeMutex);
        change.swap(_pendingChange);
    }
    if (change && _context)
    {
        auto& net = _context->config.network;
        auto copy = [](char* dst, size_t size, const std::string& src) {
            const size_t n = std::min(src.size(), size - 1);
            std::memcpy(dst, src.data(), n);
            dst[n] = '\0';
        };
        if (change->card)
            net.card = *change->card;
        if (change->hostAccess)
            net.hostAccess = *change->hostAccess ? 1 : 0;
        if (change->dnsPass)
            net.dnsPass = *change->dnsPass ? 1 : 0;
        if (change->hosts)
            copy(net.hosts, sizeof(net.hosts), *change->hosts);
        if (change->forwards)
            copy(net.forwards, sizeof(net.forwards), *change->forwards);
        if (change->connectTimeoutMs)
            net.connectTimeoutMs = *change->connectTimeoutMs;
        if (change->comPort)
            copy(net.comPort, sizeof(net.comPort), *change->comPort);
        if (change->zxWifi)
            copy(net.zxWifi, sizeof(net.zxWifi), *change->zxWifi);
        if (change->comModemLines)
            net.comModemLines = *change->comModemLines ? 1 : 0;
        if (change->espChip)
            net.espChip = *change->espChip;
        _forceRefit = true;
    }
    Plan plan = MakePlan();
    const bool same = plan == _plan && !_forceRefit && (_network || _com || (!plan.zxNetUsb && plan.serial == Plan::Serial::None));
    _forceRefit = false;
    if (same)
    {
        _plan.notes = plan.notes;
        UpdateStatus();
        return;
    }

    // The machine's own serial port keeps its registers across a refit (the
    // cable changes, not the chip)
    std::optional<Uart16550::State> keep;
    if (_com && plan.serial == Plan::Serial::EvoAvr && _plan.serial == Plan::Serial::EvoAvr)
    {
        keep.emplace();
        _com->Uart().SaveState(*keep);
    }
    Unplug();
    _plan = plan;

    if (plan.zxNetUsb || !plan.peer.empty())
    {
        std::unique_ptr<IHostNet> host;
        if (_context->config.network.hostAccess)
        {
            HostNetBridge::Options options;
            options.connectTimeoutMs = _context->config.network.connectTimeoutMs;
            host = std::make_unique<HostNetBridge>(options);
        }
        _network = std::make_unique<VirtualNetwork>(_context, std::move(host), BuildConfig(_context));
        _context->pVirtualNetwork = _network.get();
    }
    if (plan.zxNetUsb)
    {
        _card = std::make_unique<ZxNetUsb>(_network.get(), _context->pCore);
        if (_context->pPortDecoder)
            _card->AttachToPorts(_context->pPortDecoder);
        _context->pZxNetUsb = _card.get();
    }
    if (plan.serial != Plan::Serial::None)
        FitCom(plan, keep ? &*keep : nullptr);
    UpdateStatus();
}

void NetworkManager::Unplug()
{
    if (_context)
    {
        _context->pZxNetUsb = nullptr;
        _context->pComPort = nullptr;
        _context->pVirtualNetwork = nullptr;
    }
    // The adapters first: their sockets close through the virtual network
    _com.reset();
    _card.reset();
    _network.reset();
    _plan = Plan();
}

bool NetworkManager::RequestChange(const Change& change, std::string& error)
{
    if (_context && _context->pTimeTravelManager && _context->pTimeTravelManager->IsRecording())
    {
        error = "a TTD recording is running: the network settings are fixed until it stops";
        return false;
    }
    if (change.hosts && change.hosts->size() >= sizeof(_context->config.network.hosts))
    {
        error = "hosts: too long";
        return false;
    }
    if (change.forwards && change.forwards->size() >= sizeof(_context->config.network.forwards))
    {
        error = "forwards: too long";
        return false;
    }
    if (change.connectTimeoutMs && (*change.connectTimeoutMs < 500 || *change.connectTimeoutMs > 120000))
    {
        error = "connect timeout: 500..120000 ms";
        return false;
    }
    if (change.comPort)
    {
        ComPortSpec spec;
        if (!ComPortSpec::Parse(*change.comPort, spec, error))
        {
            error = "com_port: " + error;
            return false;
        }
        if (change.comPort->size() >= sizeof(_context->config.network.comPort))
        {
            error = "com_port: too long";
            return false;
        }
    }
    if (change.zxWifi)
    {
        ComPortSpec spec;
        if (!ComPortSpec::Parse(*change.zxWifi, spec, error))
        {
            error = "zx_wifi: " + error;
            return false;
        }
        if (change.zxWifi->size() >= sizeof(_context->config.network.zxWifi))
        {
            error = "zx_wifi: too long";
            return false;
        }
    }
    {
        std::lock_guard<std::mutex> lock(_changeMutex);
        _pendingChange = change;
    }
    ApplyConfiguration();
    return true;
}

void NetworkManager::Reset()
{
    if (_card)
        _card->Reset();
    if (_com)
        _com->Reset();
    // The card's sockets close; the COM port's link (its cable) stays
    if (_network)
        _network->Reset(_com ? _com->NetGuest() : nullptr);
    UpdateStatus();
}

void NetworkManager::OnFrame()
{
    if (_refitPending)
        Refit();
    if (_network)
    {
        _network->Pump();
        if (_com)
            _com->OnFrame();
        UpdateStatus();
    }
    else if (_com && _context && _context->emulatorState.frame_counter % 25 == 0)
    {
        // A serial port with nothing on its line (a ZX-Evo's AVR UART): no
        // peer to pump, the status copy twice a second is enough
        UpdateStatus();
    }
}

void NetworkManager::UpdateStatus()
{
    Status st;
    st.fitted = _card != nullptr;
    st.notes = _plan.notes;
    {
        uint8_t cards = _card ? networkspec::kCardZxNetUsb : 0;
        if (_com && _plan.serial == Plan::Serial::ZxWifi)
            cards |= networkspec::kCardZxWifi;
        st.cards = networkspec::CardsToString(cards);
    }
    if (_context && _context->pPortDecoder)
    {
        const PortDecoder::NetworkCapabilities caps = _context->pPortDecoder->DescribeNetwork();
        using SerialPort = PortDecoder::NetworkCapabilities::SerialPort;
        st.zxBus = caps.zxBus;
        st.serialPort = caps.serialPort == SerialPort::EvoAvr ? "evo-avr"
                        : caps.serialPort == SerialPort::ZiFi ? "zifi"
                                                              : "none";
    }
    else
        st.serialPort = "none";
    // The virtual network serves the card and the COM port alike
    if (_network)
    {
        st.hostAccess = _network->Host() != nullptr;
        st.sockets = _network->Sockets();
        st.listeners = _network->Listeners();
        for (const auto& [mac, addr] : _network->Dhcp().Leases())
            st.leases.emplace_back(mac, addr);
        st.counters = _network->GetCounters();
        st.activity.assign(_network->RecentActivity().begin(), _network->RecentActivity().end());
        st.config = _network->Config();
    }
    if (_card && _network)
    {
        st.card = "ZXNETUSB";
        st.control = _card->Control();
        st.mode = _card->Mode();
        st.addressHigh = _card->AddressHigh();
        st.chipRunning = _card->ChipRunning();
        st.chipInt = st.chipRunning && _card->Chip().InterruptActive();
        st.intToZ80 = _card->InterruptActive();
        st.common = _card->Chip().CommonRegisters();
        for (int n = 0; n < W5300::kSockets; ++n)
            st.chipSockets.push_back(_card->Chip().GetSocket(n));
    }
    if (_com)
    {
        auto& c = st.com;
        const Uart16550& uart = _com->Uart();
        c.fitted = true;
        const bool evo = uart.GetParams().flavor == Uart16550::Flavor::EvoAvr;
        c.flavor = evo ? "evo" : "zxwifi";
        if (evo)
            c.firmware = Uart16550::AvrFirmwareName(uart.GetParams().avr);
        if (const ISerialPeer* peer = _com->Peer())
        {
            c.peer = peer->Kind();
            c.target = peer->Target();
            c.connected = peer->Connected();
            c.pending = peer->Pending();
            if (const auto* esp = dynamic_cast<const EspModule*>(peer))
            {
                c.requests = esp->RequestsServed();
                for (const EspModule::Exchange& e : esp->RecentExchanges())
                    c.exchanges.emplace_back(e.request, e.reply);
            }
            if (const auto* stream = dynamic_cast<const StreamPeer*>(peer))
            {
                static const char* const kPhases[] = {"idle", "resolving", "connecting", "connected"};
                c.phase = kPhases[static_cast<int>(stream->GetPhase())];
                c.error = stream->LastError();
            }
        }
        c.modemLines = _context && _context->config.network.comModemLines != 0;
        c.uart = uart.GetView();
        c.baud = uart.Baud();
        c.frameBits = uart.FrameBits();

    }
    if (_context)
        st.frame = _context->emulatorState.frame_counter;
    std::lock_guard<std::mutex> lock(_statusMutex);
    _status = std::move(st);
}

NetworkManager::Status NetworkManager::GetStatus() const
{
    std::lock_guard<std::mutex> lock(_statusMutex);
    return _status;
}

VirtualNetworkConfig NetworkManager::BuildConfig(const EmulatorContext* context)
{
    VirtualNetworkConfig config;
    if (!context)
        return config;
    const auto& net = context->config.network;
    config.dnsMode = net.dnsPass ? VirtualNetworkConfig::DnsMode::Pass : VirtualNetworkConfig::DnsMode::Host;

    // Hosts=name=a.b.c.d,name=a.b.c.d (',' - ';' starts an INI comment)
    std::stringstream hosts(net.hosts);
    std::string item;
    while (std::getline(hosts, item, ','))
    {
        const size_t eq = item.find('=');
        if (eq == std::string::npos)
            continue;
        std::string name = item.substr(0, eq);
        const std::string ip = item.substr(eq + 1);
        name.erase(0, name.find_first_not_of(" \t"));
        name.erase(name.find_last_not_of(" \t") + 1);
        for (char& c : name)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }
        uint32_t addr = 0;
        if (!name.empty() && NetIpFromString(ip, addr))
            config.hosts[name] = addr;
    }

    // Forward=tcp:<hostport>:<guestport>,...
    std::stringstream forwards(net.forwards);
    while (std::getline(forwards, item, ','))
    {
        unsigned hostPort = 0;
        unsigned guestPort = 0;
        if (std::sscanf(item.c_str(), " tcp:%u:%u", &hostPort, &guestPort) == 2 && hostPort > 0 && hostPort < 65536 &&
            guestPort > 0 && guestPort < 65536)
            config.forwards[static_cast<uint16_t>(guestPort)] = static_cast<uint16_t>(hostPort);
    }
    return config;
}

bool NetworkManager::ParseChange(const std::vector<std::pair<std::string, std::string>>& settings, Change& out,
                                 std::string& error)
{
    auto lower = [](std::string v) {
        for (char& c : v)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }
        return v;
    };
    auto flag = [&](const std::string& value, const char* key, std::optional<bool>& dst) {
        const std::string v = lower(value);
        if (v == "on" || v == "1" || v == "true" || v == "yes")
            dst = true;
        else if (v == "off" || v == "0" || v == "false" || v == "no")
            dst = false;
        else
        {
            error = std::string(key) + ": on | off";
            return false;
        }
        return true;
    };
    for (const auto& [rawKey, value] : settings)
    {
        const std::string key = lower(rawKey);
        if (key == "card" || key == "cards")
        {
            uint8_t mask = 0;
            std::string why;
            if (!networkspec::ParseCards(value, mask, why))
            {
                error = "card: " + why;
                return false;
            }
            out.card = mask;
        }
        else if (key == "host_access" || key == "hostaccess" || key == "host")
        {
            if (!flag(value, "host_access", out.hostAccess))
                return false;
        }
        else if (key == "dns_mode" || key == "dnsmode" || key == "dns")
        {
            const std::string v = lower(value);
            if (v == "host")
                out.dnsPass = false;
            else if (v == "pass")
                out.dnsPass = true;
            else
            {
                error = "dns_mode: host | pass";
                return false;
            }
        }
        else if (key == "hosts")
        {
            out.hosts = value;
        }
        else if (key == "forwards" || key == "forward")
        {
            out.forwards = value;
        }
        else if (key == "com_port" || key == "comport" || key == "com")
        {
            ComPortSpec spec;
            std::string why;
            if (!ComPortSpec::Parse(value, spec, why))
            {
                error = "com_port: " + why;
                return false;
            }
            out.comPort = spec.ToString();
        }
        else if (key == "esp_chip" || key == "espchip")
        {
            const std::string v = lower(value);
            if (v == "esp32")
                out.espChip = 0;
            else if (v == "esp8266")
                out.espChip = 1;
            else
            {
                error = "esp_chip: esp32 | esp8266";
                return false;
            }
        }
        else if (key == "com_modem_lines" || key == "commodemlines")
        {
            if (!flag(value, "com_modem_lines", out.comModemLines))
                return false;
        }
        else if (key == "zx_wifi" || key == "zxwifi")
        {
            ComPortSpec spec;
            std::string why;
            if (!ComPortSpec::Parse(value, spec, why))
            {
                error = "zx_wifi: " + why;
                return false;
            }
            out.zxWifi = spec.ToString();
        }
        else if (key == "connect_timeout_ms" || key == "timeout")
        {
            char* end = nullptr;
            const unsigned long ms = std::strtoul(value.c_str(), &end, 10);
            if (value.empty() || !end || *end != '\0')
            {
                error = "connect_timeout_ms: a number";
                return false;
            }
            out.connectTimeoutMs = static_cast<unsigned>(ms);
        }
        else
        {
            error = "unknown setting '" + rawKey +
                    "' (card, host_access, dns_mode, hosts, forwards, connect_timeout_ms, com_port, zx_wifi, "
                    "com_modem_lines, esp_chip)";
            return false;
        }
    }
    return true;
}
