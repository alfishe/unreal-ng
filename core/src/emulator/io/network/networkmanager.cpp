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
#include "emulator/io/serial/comportspec.h"
#include "emulator/ports/portdecoder.h"

NetworkManager::NetworkManager(EmulatorContext* context) : _context(context)
{
}

NetworkManager::~NetworkManager()
{
    Unplug();
}

bool NetworkManager::Wanted() const
{
    return CardWanted() || ComWanted();
}

bool NetworkManager::CardWanted() const
{
    if (!_context || _context->config.network.card == 0)
        return false;
    return !_context->pFeatureManager || _context->pFeatureManager->isEnabled(Features::kNetwork);
}

bool NetworkManager::ComWanted() const
{
    if (!_context || _context->config.network.comPort[0] == '\0')
        return false;
    if (_context->pFeatureManager && !_context->pFeatureManager->isEnabled(Features::kNetwork))
        return false;
    // A machine whose own device owns #xxEF (TS-Conf: ZiFi, step N5) gets none
    if (_context->pPortDecoder && _context->pPortDecoder->ReservesLowByte(ComPort::kPortLowByte))
        return false;
    ComPortSpec spec;
    std::string error;
    return ComPortSpec::Parse(_context->config.network.comPort, spec, error) && spec.kind != ComPortSpec::Kind::None;
}

void NetworkManager::FitCom()
{
    ComPortSpec spec;
    std::string error;
    ComPortSpec::Parse(_context->config.network.comPort, spec, error);
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
        default:
            return;
    }
    const uint8_t flavorSetting = _context->config.network.comFlavor;
    const bool evo = flavorSetting == 1 || (flavorSetting == 0 && _context->config.mem_model == MM_ATM3);
    const Uart16550::Params params =
        Uart16550::DefaultParams(evo ? Uart16550::Flavor::EvoAvr : Uart16550::Flavor::ZxWifi);
    _com = std::make_unique<ComPort>(_context, params, std::move(peer));
    if (_context->pPortDecoder)
        _com->AttachToPorts(_context->pPortDecoder);
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
            net.card = *change->card ? 1 : 0;
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
        if (change->comFlavor)
            net.comFlavor = *change->comFlavor;
        if (change->comModemLines)
            net.comModemLines = *change->comModemLines ? 1 : 0;
        _forceRefit = true;
    }
    if (_forceRefit)
    {
        _forceRefit = false;
        Unplug();
    }

    const bool wanted = Wanted();
    if (!wanted)
    {
        Unplug();
        return;
    }
    if (_network)
        return;   // already fitted (a change of the set unplugs first)

    std::unique_ptr<IHostNet> host;
    if (_context->config.network.hostAccess)
    {
        HostNetBridge::Options options;
        options.connectTimeoutMs = _context->config.network.connectTimeoutMs;
        host = std::make_unique<HostNetBridge>(options);
    }
    _network = std::make_unique<VirtualNetwork>(_context, std::move(host), BuildConfig(_context));
    _context->pVirtualNetwork = _network.get();
    if (CardWanted())
    {
        _card = std::make_unique<ZxNetUsb>(_network.get(), _context->pCore);
        if (_context->pPortDecoder)
            _card->AttachToPorts(_context->pPortDecoder);
        _context->pZxNetUsb = _card.get();
    }
    if (ComWanted())
        FitCom();
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
    UpdateStatus();
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
}

void NetworkManager::UpdateStatus()
{
    Status st;
    st.fitted = _card != nullptr;
    if (_card && _network)
    {
        st.card = "ZXNETUSB";
        st.hostAccess = _network->Host() != nullptr;
        st.control = _card->Control();
        st.mode = _card->Mode();
        st.addressHigh = _card->AddressHigh();
        st.chipRunning = _card->ChipRunning();
        st.chipInt = st.chipRunning && _card->Chip().InterruptActive();
        st.intToZ80 = _card->InterruptActive();
        st.common = _card->Chip().CommonRegisters();
        for (int n = 0; n < W5300::kSockets; ++n)
            st.chipSockets.push_back(_card->Chip().GetSocket(n));
        st.sockets = _network->Sockets();
        st.listeners = _network->Listeners();
        for (const auto& [mac, addr] : _network->Dhcp().Leases())
            st.leases.emplace_back(mac, addr);
        st.counters = _network->GetCounters();
        st.activity.assign(_network->RecentActivity().begin(), _network->RecentActivity().end());
        st.config = _network->Config();
    }
    if (_com)
    {
        auto& c = st.com;
        const Uart16550& uart = _com->Uart();
        c.fitted = true;
        c.flavor = uart.GetParams().flavor == Uart16550::Flavor::EvoAvr ? "evo" : "zxwifi";
        if (const ISerialPeer* peer = _com->Peer())
        {
            c.peer = peer->Kind();
            c.target = peer->Target();
            c.connected = peer->Connected();
            c.pending = peer->Pending();
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
        if (!_card && _network)
        {
            st.hostAccess = _network->Host() != nullptr;
            st.sockets = _network->Sockets();
            st.counters = _network->GetCounters();
            st.activity.assign(_network->RecentActivity().begin(), _network->RecentActivity().end());
            st.config = _network->Config();
        }
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
        if (key == "card")
        {
            const std::string v = lower(value);
            if (v == "zxnetusb")
                out.card = true;
            else if (v == "none")
                out.card = false;
            else
            {
                error = "card: zxnetusb | none";
                return false;
            }
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
        else if (key == "com_modem_lines" || key == "commodemlines")
        {
            if (!flag(value, "com_modem_lines", out.comModemLines))
                return false;
        }
        else if (key == "com_flavor" || key == "comflavor")
        {
            const std::string v = lower(value);
            if (v == "auto")
                out.comFlavor = 0;
            else if (v == "evo")
                out.comFlavor = 1;
            else if (v == "zxwifi")
                out.comFlavor = 2;
            else
            {
                error = "com_flavor: auto | evo | zxwifi";
                return false;
            }
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
            error = "unknown setting '" + rawKey + "' (card, host_access, dns_mode, hosts, forwards, connect_timeout_ms)";
            return false;
        }
    }
    return true;
}
