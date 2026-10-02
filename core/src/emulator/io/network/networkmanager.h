#pragma once

/// @file networkmanager.h
/// @brief Owns the machine's network adapters and their virtual network
/// (network adapters TDD §3.1, §8). Nothing exists while no adapter is fitted:
/// no bridge thread, no port claim, no per-frame work.
///
/// Fitting follows what the machine offers (PortDecoder::DescribeNetwork: the
/// ZX-Bus, its own serial port), the machine config ([NETWORK] Card=,
/// ComPort=, ZxWifi=) and the runtime feature "network". A device that clashes
/// with the machine (a ZX-WiFi card on a ZX-Evo: #xxEF is the AVR's) is not
/// fitted, and the status says why. A refit asked from another thread while
/// the machine runs is applied at the next frame boundary, on its thread.
///
/// A machine's own serial port is mainboard hardware: it is there with the
/// network off or ComPort=NONE (a ZX-Evo's AVR answers on #xxEF regardless).

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
        std::optional<uint8_t> card;      ///< ZX-Bus cards, networkspec::kCard* mask
        std::optional<bool> hostAccess;
        std::optional<bool> dnsPass;
        std::optional<std::string> hosts;     ///< "name=a.b.c.d,..."
        std::optional<std::string> forwards;  ///< "tcp:<hostport>:<guestport>,..."
        std::optional<unsigned> connectTimeoutMs;
        std::optional<std::string> comPort;    ///< ComPort= value (ComPortSpec): the machine's own serial port
        std::optional<std::string> zxWifi;     ///< ZxWifi= value (ComPortSpec): the ZX-WiFi card's ESP
        std::optional<bool> comModemLines;     ///< a serial device's RTS / DTR / CTS / DSR / RI / DCD
        std::optional<uint8_t> espChip;        ///< 0 ESP32, 1 ESP8266
        std::optional<uint8_t> avrFirmware;    ///< ZX-Evo: Uart16550::AvrFirmware ([EVO] Avr=)
        std::optional<uint8_t> kbcFirmware;    ///< ATM Turbo 2+: Atm2Kbc::Firmware ([ATM] Kbc=)
    };
    bool RequestChange(const Change& change, std::string& error);

    /// The one parser every interface uses: keys card (none | zxnetusb |
    /// zxwifi | zxnetusb,zxwifi), host_access (on | off), dns_mode (host |
    /// pass), hosts, forwards, connect_timeout_ms, com_port and zx_wifi
    /// (ComPortSpec: none | loopback | tcp:<host>:<port> |
    /// serial:<device>[,<baud>] | espnet | at), com_modem_lines (on | off),
    /// esp_chip (esp32 | esp8266), avr_firmware (ZX-Evo, [EVO] Avr= names:
    /// baseconf | base2010 .. base2023 | ts | ts2013 | ts2016-02 | ts2016-04),
    /// kbc_firmware (ATM Turbo 2+ keyboard controller, [ATM] Kbc= names:
    /// none | v22-7 .. v41).
    /// Unknown keys and bad values are errors
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
        bool fitted = false;              ///< the ZXNETUSB card
        std::string card;                 ///< "ZXNETUSB" or empty
        std::string cards;                ///< the ZX-Bus cards fitted: "ZXNETUSB,ZXWIFI" | "NONE"
        bool zxBus = true;                ///< the machine takes ZX-Bus cards
        std::string serialPort;           ///< the machine's own: none | evo-avr | zifi | atm2-kbc
        std::vector<std::string> notes;   ///< configured devices not fitted, and why

        /// The settings in force (the machine config), in ParseChange's terms
        struct Settings
        {
            std::string card;             ///< NONE | ZXNETUSB | ZXWIFI | ZXNETUSB,ZXWIFI
            std::string comPort;          ///< ComPortSpec text, NONE when empty
            std::string zxWifi;           ///< ComPortSpec text, AT when empty
            std::string espChip;          ///< ESP32 | ESP8266
            std::string avrFirmware;      ///< [EVO] Avr= name
            std::string kbcFirmware;      ///< [ATM] Kbc= name; empty: no controller socket on this board
            std::string dnsMode;          ///< HOST | PASS
            std::string hosts;
            std::string forwards;
            bool comModemLines = false;
            bool hostAccess = true;
            unsigned connectTimeoutMs = 10000;
        } settings;
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
            std::string flavor;           ///< evo (the ZX-Evo AVR) | zxwifi (a 16550 card) | atm2kbc (ATM Turbo 2+ keyboard controller)
            std::string firmware;         ///< evo: the AVR firmware ([EVO] Avr=); atm2kbc: [ATM] Kbc=
            std::string peer;             ///< loopback | tcp | serial
            std::string target;           ///< host:port (resolved address), device,baud
            std::string phase;            ///< idle | resolving | connecting | connected (stream peers)
            std::string error;            ///< why the last attempt failed
            bool modemLines = false;
            bool connected = false;
            Uart16550::View uart;
            uint32_t baud = 0;
            uint32_t frameBits = 0;
            /// atm2kbc: no 16550 (`uart` stays empty) - the MCU's line
            bool rts = false, dtr = false;
            uint64_t bytesIn = 0, bytesOut = 0, lost = 0;
            size_t pending = 0;           ///< bytes the peer holds for the ZX
            uint32_t peerBaud = 0;        ///< ESP module: its firmware's rate (0: not an ESP module)
            std::vector<std::pair<std::string, std::string>> exchanges;   ///< ESP module: recent requests / replies
            uint64_t requests = 0;
        } com;

        /// The machine's own serial port when it is no 16550 on #xxEF (ATM
        /// Turbo 2+ keyboard controller): fitted beside a ZX-WiFi card's `com`
        Com machineSerial;
    };
    Status GetStatus() const;

private:
    /// What should be fitted now, from the machine, the config and the feature
    struct Plan
    {
        bool zxNetUsb = false;
        enum class Serial : uint8_t { None, EvoAvr, ZxWifi } serial = Serial::None;
        uint8_t avr = 0;                  ///< EvoAvr: the AVR firmware
        std::string peer;                 ///< ComPortSpec of the serial port's peer
        bool machineSerial = false;       ///< the machine's own port is no 16550 (Atm2Kbc)
        std::string machinePeer;          ///< ComPortSpec of its peer
        std::vector<std::string> notes;
        bool operator==(const Plan& o) const
        {
            return zxNetUsb == o.zxNetUsb && serial == o.serial && avr == o.avr && peer == o.peer &&
                   machineSerial == o.machineSerial && machinePeer == o.machinePeer;
        }
    };
    Plan MakePlan() const;
    void Refit();
    void Unplug();
    void FitCom(const Plan& plan, const Uart16550::State* keep);
    /// The peer a ComPortSpec names (nullptr for NONE); an ESP module without
    /// its own ,<baud> ships at `espBaud` (the port's default)
    static constexpr uint32_t kDefaultEspBaud = 115200;
    std::unique_ptr<ISerialPeer> MakePeer(const std::string& specText, uint32_t espBaud) const;
    /// Plug `_machinePeer` into the machine's own non-16550 port
    void FitMachineSerial(const Plan& plan);
    void FillPeerStatus(const ISerialPeer* peer, Status::Com& c) const;

    EmulatorContext* _context = nullptr;
    std::unique_ptr<VirtualNetwork> _network;
    std::unique_ptr<ZxNetUsb> _card;
    std::unique_ptr<ComPort> _com;
    std::unique_ptr<ISerialPeer> _machinePeer;   ///< on the machine's own non-16550 port (Atm2Kbc)
    Plan _plan;                           ///< what is fitted
    std::atomic<bool> _refitPending{false};
    bool _forceRefit = false;
    std::string _firmwareNote;            ///< why the last firmware change did not load             ///< the next refit unplugs first (settings changed)
    std::mutex _changeMutex;
    std::optional<Change> _pendingChange; ///< applied to the machine config on the machine thread

    void UpdateStatus();
    mutable std::mutex _statusMutex;
    Status _status;
};
