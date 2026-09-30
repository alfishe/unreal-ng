#pragma once

/// @file networkmanager.h
/// @brief Owns the machine's network adapters and their virtual network
/// (network adapters TDD §3.1, §8). Nothing exists while no adapter is fitted:
/// no bridge thread, no port claim, no per-frame work.
///
/// Fitting follows the machine config ([NETWORK] Card=) and the runtime
/// feature "network". A refit asked from another thread while the machine
/// runs is applied at the next frame boundary, on the machine's thread.

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "emulator/io/network/virtualnetwork.h"
#include "emulator/io/network/zxnetusb.h"
#include "emulator/io/serial/comport.h"

class EmulatorContext;

class NetworkManager
{
public:
    explicit NetworkManager(EmulatorContext* context);
    ~NetworkManager();

    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    /// Fit or unplug adapters to match config + feature (machine thread, or
    /// deferred to the next frame boundary when the machine runs elsewhere)
    void ApplyConfiguration();

    /// Machine reset: the card sees ZX-Bus /RESET, the network drops its sockets
    void Reset();

    /// A runtime change of the [NETWORK] settings (automation). Unset fields
    /// keep their value. Applied on the machine thread at the next frame
    /// boundary (at once when the machine is not running): the card is
    /// unplugged and fitted again, so every connection closes. Refused while
    /// TTD records (the recording's device set is fixed) or with a bad value
    struct Change
    {
        std::optional<bool> card;         ///< ZXNETUSB fitted
        std::optional<bool> hostAccess;
        std::optional<bool> dnsPass;
        std::optional<std::string> hosts;     ///< "name=a.b.c.d,..."
        std::optional<std::string> forwards;  ///< "tcp:<hostport>:<guestport>,..."
        std::optional<unsigned> connectTimeoutMs;
        std::optional<std::string> comPort;    ///< ComPort= value (ComPortSpec)
        std::optional<uint8_t> comFlavor;      ///< 0 auto, 1 evo, 2 zxwifi
        std::optional<bool> comModemLines;     ///< a serial device's RTS / DTR / CTS / DSR / RI / DCD
    };
    bool RequestChange(const Change& change, std::string& error);

    /// The one parser every interface uses: keys card (zxnetusb | none),
    /// host_access (on | off), dns_mode (host | pass), hosts, forwards,
    /// connect_timeout_ms, com_port (ComPortSpec: none | loopback |
    /// tcp:<host>:<port> | serial:<device>[,<baud>]), com_flavor (auto | evo |
    /// zxwifi), com_modem_lines (on | off). Unknown keys and bad values are errors
    static bool ParseChange(const std::vector<std::pair<std::string, std::string>>& settings, Change& out,
                            std::string& error);

    /// Frame boundary on the machine thread: pending refit, then the network pump
    void OnFrame();

    VirtualNetwork* Network() const { return _network.get(); }
    ZxNetUsb* Card() const { return _card.get(); }
    ComPort* Com() const { return _com.get(); }

    /// Build a virtual-network config from the machine config (hosts, forwards, DNS mode)
    static VirtualNetworkConfig BuildConfig(const EmulatorContext* context);

    /// Everything automation shows, copied on the machine thread at each frame
    /// boundary: other threads read this, never the live tables
    struct Status
    {
        bool fitted = false;
        std::string card;                 ///< "ZXNETUSB" or empty
        bool hostAccess = false;
        uint8_t control = 0, mode = 0, addressHigh = 0;   ///< card ports #83AB / #82AB / #81AB
        bool chipRunning = false;
        bool chipInt = false;             ///< W5300 INTn asserted ((IR & IMR) != 0)
        bool intToZ80 = false;            ///< the card holds the Z80's /INT low (#83AB b2 and b6 set)
        std::array<uint8_t, 256> common{};                ///< W5300 common registers
        std::vector<W5300::SocketView> chipSockets;
        std::vector<VirtualNetwork::SocketInfo> sockets;
        std::vector<VirtualNetwork::ListenerInfo> listeners;
        std::vector<std::pair<std::array<uint8_t, 6>, uint32_t>> leases;
        VirtualNetwork::Counters counters;
        std::vector<VirtualNetwork::Activity> activity;
        VirtualNetworkConfig config;
        uint64_t frame = 0;               ///< frame of the snapshot

        /// The COM port (TDD §7): UART registers and the peer
        struct Com
        {
            bool fitted = false;
            std::string flavor;           ///< evo (the ZX-Evo AVR) | zxwifi (a 16550 card)
            std::string peer;             ///< loopback | tcp | serial
            std::string target;           ///< host:port (resolved address), device,baud
            std::string phase;            ///< idle | resolving | connecting | connected (stream peers)
            std::string error;            ///< why the last attempt failed
            bool modemLines = false;
            bool connected = false;
            Uart16550::View uart;
            uint32_t baud = 0;
            uint32_t frameBits = 0;
            size_t pending = 0;           ///< bytes the peer holds for the ZX
        } com;
    };
    Status GetStatus() const;

private:
    void Refit();
    void Unplug();
    bool Wanted() const;

    EmulatorContext* _context = nullptr;
    std::unique_ptr<VirtualNetwork> _network;
    std::unique_ptr<ZxNetUsb> _card;
    std::unique_ptr<ComPort> _com;
    bool CardWanted() const;
    bool ComWanted() const;
    void FitCom();
    std::atomic<bool> _refitPending{false};
    bool _forceRefit = false;             ///< the next refit unplugs first (settings changed)
    std::mutex _changeMutex;
    std::optional<Change> _pendingChange; ///< applied to the machine config on the machine thread

    void UpdateStatus();
    mutable std::mutex _statusMutex;
    Status _status;
};
