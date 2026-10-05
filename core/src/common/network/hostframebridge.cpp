#include "common/network/hostframebridge.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <type_traits>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif

#include "common/network/mactranslator.h"
#include "platform/dynamiclibrary.h"
#include "platform/hostadaptermac.h"

namespace
{
// The pieces of the libpcap ABI used here (pcap/pcap.h), declared locally so no pcap header is needed to build
struct PcapAddr
{
    PcapAddr* next;
    sockaddr* addr;
    sockaddr* netmask;
    sockaddr* broadaddr;
    sockaddr* dstaddr;
};

struct PcapIf
{
    PcapIf* next;
    char* name;
    char* description;
    PcapAddr* addresses;
    uint32_t flags;
};

#if defined(_WIN32)
struct PcapTimeval
{
    long tv_sec;
    long tv_usec;
};
#else
using PcapTimeval = timeval;
#endif

struct PcapPacketHeader
{
    PcapTimeval ts;
    uint32_t caplen;
    uint32_t len;
};

constexpr uint32_t kIfLoopback = 0x00000001;
constexpr uint32_t kIfUp = 0x00000002;
constexpr uint32_t kIfRunning = 0x00000004;
constexpr uint32_t kIfWireless = 0x00000008;
constexpr int kDltEthernet = 1;
constexpr int kErrbufSize = 256;

std::vector<std::string> LibraryNames()
{
#if defined(_WIN32)
    std::vector<std::string> names;
    if (const char* root = std::getenv("SystemRoot"))
        names.push_back(std::string(root) + "\\System32\\Npcap\\wpcap.dll");
    names.push_back("wpcap.dll");
    return names;
#elif defined(__APPLE__)
    return {"libpcap.A.dylib", "/usr/lib/libpcap.A.dylib", "libpcap.dylib"};
#else
    return {"libpcap.so.1", "libpcap.so.0.8", "libpcap.so"};
#endif
}

const char* PermissionHint()
{
#if defined(_WIN32)
    return "install Npcap (https://npcap.com) and start the emulator again";
#elif defined(__APPLE__)
    return "the bridge needs read / write access to /dev/bpf*: install Wireshark's ChmodBPF (it adds you to the "
           "access_bpf group) or run the emulator as root";
#else
    return "the bridge needs CAP_NET_RAW and CAP_NET_ADMIN: sudo setcap cap_net_raw,cap_net_admin=eip <the emulator "
           "binary>, or run it as root";
#endif
}
}  // namespace

/// libpcap / Npcap entry points, resolved at run time
class PcapLibrary
{
public:
    using FindAllDevs = int (*)(PcapIf**, char*);
    using FreeAllDevs = void (*)(PcapIf*);
    using Create = void* (*)(const char*, char*);
    using SetInt = int (*)(void*, int);
    using Activate = int (*)(void*);
    using NextEx = int (*)(void*, PcapPacketHeader**, const unsigned char**);
    using SendPacket = int (*)(void*, const unsigned char*, int);
    using CloseFn = void (*)(void*);
    using GetErr = char* (*)(void*);
    using Datalink = int (*)(void*);
    using LibVersion = const char* (*)();

    DynamicLibrary lib;
    FindAllDevs findAllDevs = nullptr;
    FreeAllDevs freeAllDevs = nullptr;
    Create create = nullptr;
    SetInt setSnaplen = nullptr;
    SetInt setPromisc = nullptr;
    SetInt setTimeout = nullptr;
    SetInt setImmediateMode = nullptr;   ///< optional (libpcap 1.5+)
    Activate activate = nullptr;
    NextEx nextEx = nullptr;
    SendPacket sendPacket = nullptr;
    CloseFn close = nullptr;
    GetErr getErr = nullptr;
    Datalink datalink = nullptr;
    LibVersion libVersion = nullptr;

    bool Load(std::string& error)
    {
        if (!lib.Open(LibraryNames(), error))
        {
            error = "the packet library is not available (" + error + "); " + PermissionHint();
            return false;
        }
        bool ok = true;
        auto get = [this, &ok, &error](auto& fn, const char* name, bool required = true) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(lib.Symbol(name));
            if (!fn && required)
            {
                ok = false;
                error += (error.empty() ? "" : ", ") + std::string(name);
            }
        };
        get(findAllDevs, "pcap_findalldevs");
        get(freeAllDevs, "pcap_freealldevs");
        get(create, "pcap_create");
        get(setSnaplen, "pcap_set_snaplen");
        get(setPromisc, "pcap_set_promisc");
        get(setTimeout, "pcap_set_timeout");
        get(setImmediateMode, "pcap_set_immediate_mode", false);
        get(activate, "pcap_activate");
        get(nextEx, "pcap_next_ex");
        get(sendPacket, "pcap_sendpacket");
        get(close, "pcap_close");
        get(getErr, "pcap_geterr");
        get(datalink, "pcap_datalink");
        get(libVersion, "pcap_lib_version", false);
        if (!ok)
            error = lib.Name() + " lacks " + error;
        return ok;
    }
};

HostFrameBridge::HostFrameBridge() = default;

HostFrameBridge::~HostFrameBridge()
{
    Close();
}

bool HostFrameBridge::LoadPcap(std::string& error)
{
    if (_pcap)
        return true;
    auto pcap = std::make_unique<PcapLibrary>();
    if (!pcap->Load(error))
        return false;
    _pcap = std::move(pcap);
    return true;
}

std::vector<HostAdapter> HostFrameBridge::Adapters(std::string& error)
{
    std::vector<HostAdapter> out;
    if (!LoadPcap(error))
        return out;
    char errbuf[kErrbufSize] = {};
    PcapIf* list = nullptr;
    if (_pcap->findAllDevs(&list, errbuf) != 0)
    {
        error = std::string("listing the adapters failed: ") + errbuf + "; " + PermissionHint();
        return out;
    }
    for (PcapIf* it = list; it; it = it->next)
    {
        HostAdapter a;
        a.name = it->name ? it->name : "";
        a.description = it->description ? it->description : "";
        a.loopback = (it->flags & kIfLoopback) != 0;
        a.up = (it->flags & kIfUp) != 0;
        a.running = (it->flags & kIfRunning) != 0;
        a.wireless = (it->flags & kIfWireless) != 0;
        for (PcapAddr* address = it->addresses; address; address = address->next)
        {
            if (!a.hasMac && address->addr && LinkAddressOf(address->addr, a.mac.data()))
                a.hasMac = true;
            if (address->addr && address->addr->sa_family == AF_INET)
            {
                char text[INET_ADDRSTRLEN] = {};
                const sockaddr_in* in = reinterpret_cast<const sockaddr_in*>(address->addr);
                if (inet_ntop(AF_INET, &in->sin_addr, text, sizeof(text)))
                    a.ipv4.emplace_back(text);
            }
        }
        if (!a.hasMac)
            a.hasMac = AdapterMacByName(a.name, a.mac.data());
        out.push_back(std::move(a));
    }
    _pcap->freeAllDevs(list);
    return out;
}

bool HostFrameBridge::Open(const std::string& adapter, std::string& error)
{
    Close();
    error.clear();
    if (adapter.empty())
    {
        error = "no adapter named (BridgeAdapter=)";
        return false;
    }
    std::vector<HostAdapter> adapters = Adapters(error);
    if (!error.empty())
        return false;
    const HostAdapter* found = nullptr;
    for (const HostAdapter& a : adapters)
    {
        if (a.name == adapter)
            found = &a;
    }
    if (!found)
    {
        error = "no host adapter '" + adapter + "'";
        return false;
    }
    if (found->loopback)
    {
        error = "'" + adapter + "' is a loopback adapter: no LAN behind it";
        return false;
    }
    // Wi-Fi (SN6b): an access point drops frames from the card's own MAC - they leave with the adapter's
    const bool translate = found->wireless;
    if (translate && !found->hasMac)
    {
        error = "'" + adapter + "' is a Wi-Fi adapter whose own MAC address is unknown: the bridge needs it to "
                "translate the card's frames";
        return false;
    }
    const Mac hostMac = found->mac;

    auto openHandle = [this, &adapter, &error](bool promisc) -> void* {
        char errbuf[kErrbufSize] = {};
        void* handle = _pcap->create(adapter.c_str(), errbuf);
        if (!handle)
        {
            error = std::string("cannot open '") + adapter + "': " + errbuf;
            return nullptr;
        }
        _pcap->setSnaplen(handle, static_cast<int>(kMaxFrame + 4));
        _pcap->setPromisc(handle, promisc ? 1 : 0);
        _pcap->setTimeout(handle, 20);
        if (_pcap->setImmediateMode)
            _pcap->setImmediateMode(handle, 1);
        const int status = _pcap->activate(handle);
        if (status < 0)
        {
            const char* text = _pcap->getErr(handle);
            error = std::string("cannot open '") + adapter + "': " + (text && *text ? text : "activation failed") + "; " +
                    PermissionHint();
            _pcap->close(handle);
            return nullptr;
        }
        if (_pcap->datalink(handle) != kDltEthernet)
        {
            error = "'" + adapter + "' is not an Ethernet adapter";
            _pcap->close(handle);
            return nullptr;
        }
        return handle;
    };

    _rx = openHandle(true);
    if (!_rx)
        return false;
    _tx = openHandle(false);
    if (!_tx)
    {
        _pcap->close(_rx);
        _rx = nullptr;
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _adapter = adapter;
        _translate = translate;
        _hostMac = hostMac;
        _guestIps.clear();
        _queue.clear();
        _counters = Counters{};
        _lastError.clear();
    }
    _running = true;
    _thread = std::thread(&HostFrameBridge::CaptureLoop, this);
    return true;
}

void HostFrameBridge::Close()
{
    _running = false;
    if (_thread.joinable())
        _thread.join();
    if (_pcap)
    {
        if (_rx)
            _pcap->close(_rx);
        if (_tx)
            _pcap->close(_tx);
    }
    _rx = nullptr;
    _tx = nullptr;
    std::lock_guard<std::mutex> lock(_mutex);
    _adapter.clear();
    _queue.clear();
}

std::string HostFrameBridge::Adapter() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _adapter;
}

void HostFrameBridge::SetStations(const std::vector<Mac>& stations)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _stations = stations;
}

void HostFrameBridge::SetGuestIps(const std::vector<uint32_t>& ips)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _guestIps = ips;
}

bool HostFrameBridge::WantsFrame(const uint8_t* frame, size_t length, const std::vector<Mac>& stations)
{
    if (length < 14)
        return false;
    for (const Mac& mac : stations)
    {
        if (std::memcmp(frame + 6, mac.data(), 6) == 0)
            return false;   // our own frame, seen again on the adapter
    }
    if (frame[0] & 1)
        return true;   // broadcast or multicast
    for (const Mac& mac : stations)
    {
        if (std::memcmp(frame, mac.data(), 6) == 0)
            return true;
    }
    return false;
}

void HostFrameBridge::Send(const uint8_t* frame, size_t length)
{
    if (!_tx || !_pcap || length == 0 || length > kMaxFrame)
        return;
    const int status = _pcap->sendPacket(_tx, frame, static_cast<int>(length));
    std::lock_guard<std::mutex> lock(_mutex);
    if (status == 0)
        ++_counters.sent;
    else
    {
        ++_counters.sendErrors;
        const char* text = _pcap->getErr(_tx);
        _lastError = std::string("send: ") + (text ? text : "failed");
    }
}

void HostFrameBridge::Drain(std::vector<std::vector<uint8_t>>& out)
{
    std::lock_guard<std::mutex> lock(_mutex);
    while (!_queue.empty())
    {
        out.push_back(std::move(_queue.front()));
        _queue.pop_front();
    }
}

IHostFrames::Counters HostFrameBridge::GetCounters() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _counters;
}

std::string HostFrameBridge::LastError() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _lastError;
}

std::string HostFrameBridge::Library() const
{
    if (!_pcap)
        return {};
    return _pcap->libVersion ? std::string(_pcap->libVersion()) : _pcap->lib.Name();
}

void HostFrameBridge::CaptureLoop()
{
    while (_running)
    {
        PcapPacketHeader* header = nullptr;
        const unsigned char* data = nullptr;
        const int status = _pcap->nextEx(_rx, &header, &data);
        if (status == 0)
            continue;   // the read timeout: check _running again
        if (status < 0)
        {
            const char* text = _pcap->getErr(_rx);
            std::lock_guard<std::mutex> lock(_mutex);
            _lastError = std::string("capture stopped: ") + (text ? text : "error");
            break;
        }
        const size_t length = std::min<size_t>(header->caplen, kMaxFrame);
        std::lock_guard<std::mutex> lock(_mutex);
        const bool wanted = _translate ? MacTranslator::WantsInbound(data, length, _hostMac, _guestIps)
                                       : WantsFrame(data, length, _stations);
        if (!wanted)
        {
            ++_counters.filtered;
            continue;
        }
        if (_queue.size() >= kMaxQueuedFrames)
        {
            ++_counters.dropped;
            continue;
        }
        _queue.emplace_back(data, data + length);
        ++_counters.received;
    }
}
