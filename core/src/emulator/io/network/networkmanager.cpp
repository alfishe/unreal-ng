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
    if (!_context || _context->config.network.card == 0)
        return false;
    return !_context->pFeatureManager || _context->pFeatureManager->isEnabled(Features::kNetwork);
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
    if (_card)
        return;   // already fitted

    std::unique_ptr<IHostNet> host;
    if (_context->config.network.hostAccess)
    {
        HostNetBridge::Options options;
        options.connectTimeoutMs = _context->config.network.connectTimeoutMs;
        host = std::make_unique<HostNetBridge>(options);
    }
    _network = std::make_unique<VirtualNetwork>(_context, std::move(host), BuildConfig(_context));
    _card = std::make_unique<ZxNetUsb>(_network.get(), _context->pCore);
    if (_context->pPortDecoder)
        _card->AttachToPorts(_context->pPortDecoder);

    _context->pVirtualNetwork = _network.get();
    _context->pZxNetUsb = _card.get();
    UpdateStatus();
}

void NetworkManager::Unplug()
{
    if (_context)
    {
        _context->pZxNetUsb = nullptr;
        _context->pVirtualNetwork = nullptr;
    }
    // The card first: its sockets close through the virtual network
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
    if (_network)
        _network->Reset();
    UpdateStatus();
}

void NetworkManager::OnFrame()
{
    if (_refitPending)
        Refit();
    if (_network)
    {
        _network->Pump();
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
