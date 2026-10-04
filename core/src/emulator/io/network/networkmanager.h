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
#include "emulator/io/network/atm2ioesp.h"
#include "emulator/io/network/ethernet/etherlink3.h"
#include "emulator/io/network/ethernet/ethernetcard.h"
#include "emulator/io/network/ethernet/ne2000board.h"
#include "emulator/io/network/pcserialcard.h"
#include "emulator/io/network/vnet/ethernetgateway.h"
#include "emulator/state/statenode.h"
#include "emulator/io/network/zifi.h"
#include "emulator/io/serial/comport.h"
#include "emulator/io/serial/esp/espmodule.h"

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
        std::optional<uint8_t> espChip;        ///< EspModule::Firmware: 0 ESP32, 1 ESP8266 (NonOS), 2 / 3 ESP8266 AT 2.2.1 / 2.2.2
        std::optional<uint8_t> avrFirmware;    ///< ZX-Evo: Uart16550::AvrFirmware ([EVO] Avr=)
        std::optional<uint8_t> kbcFirmware;    ///< ATM Turbo 2+: Atm2Kbc::Firmware ([ATM] Kbc=)
        std::optional<std::string> atm2IoEsp;  ///< Atm2IoEsp= value (ComPortSpec): the ATM2IOESP card's ESP
        std::optional<uint8_t> atm2IoEspAddress;   ///< Atm2IoEspAddress=: its bus address (#F0 / #F8)
        std::optional<std::string> zifi;       ///< ZiFi= value (ComPortSpec): the TS AVR's ZiFi UART
        std::optional<std::string> modemPhonebook;   ///< ModemPhonebook=: "<number>=<host>[:<port>],..." (every MODEM peer)
        /// isa1_peer / isa2_peer ([ISA] SlotNPeer, ComPortSpec): what a UART card in that expansion slot is wired to;
        /// isa1_peer_b / isa2_peer_b ([ISA] SlotNPeerB): a two-UART card's second line. Keys "isa1", "isa1.b"
        std::vector<std::pair<std::string, std::string>> slotPeers;
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
    /// none | v22-7 .. v41), isa1_peer / isa2_peer (ComPortSpec: what the UART card in that Sprinter ISA slot -
    /// the SprinterESP - is wired to, default at), atm2ioesp (ComPortSpec: what the ATM2IOESP card's
    /// 16550 is wired to, default at), atm2ioesp_address (its bus address, a
    /// multiple of 8: 0xF0 Rev 1.5 / 2.0, 0xF8 Rev 1.0), zifi (ComPortSpec:
    /// what the TS AVR firmware's ZiFi UART is wired to, default none; at =
    /// the original ZiFi board's ESP-01), modem_phonebook (the Hayes modem's numbers:
    /// "<number>=<host>[:<port>],...").
    /// Unknown keys and bad values are errors
    static bool ParseChange(const std::vector<std::pair<std::string, std::string>>& settings, Change& out,
                            std::string& error);

    /// Frame boundary on the machine thread: OnFrameDevices, then OnFrameHost
    void OnFrame();
    /// The devices' own frame work (pending refit, cards, gateway, serial peers): before the TTD checkpoint
    void OnFrameDevices();
    /// The host's answers (the network pump: journaled inputs) and the status copy: after the checkpoint
    void OnFrameHost();

    VirtualNetwork* Network() const { return _network.get(); }
    ZxNetUsb* Card() const { return _card.get(); }
    ComPort* Com() const { return _com.get(); }
    Atm2IoEsp* Atm2IoEspCard() const { return _atm2IoEsp.get(); }
    ZiFi* ZiFiBlock() const { return _zifi.get(); }

    /// The network cards in expansion slots (the Sprinter's ISA slots; network tdd §5.2), by slot id
    struct SlotCard
    {
        std::string slotId;                 ///< "isa2"
        std::unique_ptr<IEthernetCard> ethernet;   ///< a frame-level card (NE2000, 3C509B)
        std::unique_ptr<PcSerialCard> serial;   ///< a UART card (SprinterESP, ISA modem, SprinterSerial)
    };
    /// The UART card in this slot ("isa1"), or null
    PcSerialCard* SerialCard(const std::string& slotId) const;
    const std::vector<SlotCard>& SlotCards() const { return _slotCards; }
    /// The frame card with this port key ("isa2.eth"), or null
    IEthernetCard* EthernetCard(const std::string& portKey) const;
    /// Whether a slot card kind is a frame-level Ethernet card ("ne2000", "el3c509b") - the gateway's kinds
    static bool IsFrameCardKind(const std::string& kind) { return kind == "ne2000" || kind == "el3c509b"; }
    /// The switch + router of the frame-level cards (null without one, or with the network off)
    EthernetGateway* Gateway() const { return _gateway.get(); }

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
        bool internalIo = false;          ///< the ATM Turbo 2+ INTERNAL I/O connector (ATM2IOESP)
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
            std::string atm2IoEsp;        ///< ComPortSpec text, AT when empty
            unsigned atm2IoEspAddress = 0xF0;
            std::string zifi;             ///< ComPortSpec text, NONE when empty
            std::string modemPhonebook;   ///< ModemPhonebook=
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
            std::vector<std::pair<std::string, std::string>> exchanges;   ///< ESP module: recent requests / replies (modem: commands / results)
            StateNode modem;              ///< a Hayes modem peer: HayesModemPeer::Describe (null otherwise)
            uint64_t requests = 0;
        } com;

        /// The machine's own serial port when it is no 16550 on #xxEF (ATM
        /// Turbo 2+ keyboard controller): fitted beside a ZX-WiFi card's `com`
        Com machineSerial;

        /// The ATM2IOESP card (a 16550 on the INTERNAL I/O connector)
        Com atm2IoEsp;
        unsigned atm2IoEspAddress = 0;

        /// The TS AVR firmware's ZiFi: `zifi` is its UART to the ESP (USART0) and the peer, `zifiRegisters`
        /// the API block (the rings: `zfRx` / `zfTx` ZiFi, `rsRx` / `rsTx` the 16550's)
        Com zifi;
        ZiFi::View zifiRegisters;
        bool zifiMachine = false;         ///< the machine has the ZiFi block (TS-Conf, ZX-Evo + TS firmware)

        /// Expansion slots a network card can take (network tdd §14): what the config puts there, what is fitted
        struct Slot
        {
            std::string id, bus, label;
            std::string configured;       ///< the card kind of the config ("" = none)
            std::string card;             ///< the network card fitted ("" = none)
            std::string note;             ///< why a configured network card is not fitted
            StateNode details;            ///< the card's own report (chip, base, MAC, registers, counters)
        };
        std::vector<Slot> expansionSlots;   ///< not "slots": a Qt macro
        /// The Ethernet gateway (EthernetGateway::Describe): ports, leases, ARP, TCP / UDP, counters
        StateNode ethernetGateway;
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
        bool atm2IoEsp = false;           ///< the ATM2IOESP card on the INTERNAL I/O connector
        std::string atm2IoEspPeer;        ///< ComPortSpec of its ESP
        uint8_t atm2IoEspAddress = 0xF0;  ///< its bus address
        bool zifi = false;                ///< the AVR firmware's ZiFi block (with the EvoAvr port)
        std::string zifiPeer;             ///< ComPortSpec of its UART's peer
        /// Network cards for expansion slots (NE2000, 3C509B, SprinterESP)
        struct SlotCard
        {
            std::string slotId, kind, chip, portKey;
            uint16_t base = 0;
            uint8_t irq = 0;
            std::array<uint8_t, 6> mac{};
            std::string peer;             ///< UART cards: ComPortSpec of the (first) line ("" = nothing on it)
            std::string peerB;            ///< two-UART cards: the second line
            uint8_t irqB = 0;             ///< two-UART cards: the second UART's jumper (0 = open)
            bool partialDecode = false;   ///< SprinterSerial without D3
            uint8_t espFirmware = 0;      ///< UART cards with an ESP: EspModule::Firmware
            bool operator==(const SlotCard& o) const
            {
                return slotId == o.slotId && kind == o.kind && chip == o.chip && portKey == o.portKey && base == o.base &&
                       irq == o.irq && mac == o.mac && peer == o.peer && peerB == o.peerB && irqB == o.irqB &&
                       partialDecode == o.partialDecode && espFirmware == o.espFirmware;
            }
        };
        std::vector<SlotCard> slotCards;
        bool ethernetLink = false;        ///< the slot cards get the gateway (the network feature is on)
        std::vector<std::string> notes;
        bool operator==(const Plan& o) const
        {
            return zxNetUsb == o.zxNetUsb && serial == o.serial && avr == o.avr && peer == o.peer &&
                   machineSerial == o.machineSerial && machinePeer == o.machinePeer && atm2IoEsp == o.atm2IoEsp &&
                   atm2IoEspPeer == o.atm2IoEspPeer && atm2IoEspAddress == o.atm2IoEspAddress && zifi == o.zifi &&
                   zifiPeer == o.zifiPeer && slotCards == o.slotCards && ethernetLink == o.ethernetLink;
        }
    };
    Plan MakePlan() const;
    void Refit();
    /// `keepSlotCards`: the cards in expansion slots stay (hardware: a settings change re-cables them, the chip
    /// keeps its registers and packet RAM)
    void Unplug(bool keepSlotCards = false);
    /// What a refit keeps of the AVR (the cable changes, not the chip): its UARTs' registers and the ZiFi block
    struct AvrKeep
    {
        std::optional<Uart16550::State> com;
        std::optional<Uart16550::State> zifiLine;
        std::optional<ZiFi::State> zifi;
    };
    void FitCom(const Plan& plan, const AvrKeep* keep);
    /// The peer a ComPortSpec names (nullptr for NONE); an ESP module without
    /// its own ,<baud> ships at `espBaud` (the port's default)
    static constexpr uint32_t kDefaultEspBaud = 115200;
    /// `firmware` / `mac`: an ESP module of a given build and station address (a card's own module); default: the
    /// [NETWORK] EspChip firmware and the chip family's MAC
    std::unique_ptr<ISerialPeer> MakePeer(const std::string& specText, uint32_t espBaud,
                                          std::optional<EspModule::Firmware> firmware = std::nullopt,
                                          const std::array<uint8_t, 6>* mac = nullptr) const;
    /// Plug `_machinePeer` into the machine's own non-16550 port
    void FitMachineSerial(const Plan& plan);
    void FitAtm2IoEsp(const Plan& plan);
    void FitSlotCards(const Plan& plan);
    void UnplugSlotCards();
    void FillPeerStatus(const ISerialPeer* peer, Status::Com& c) const;

    EmulatorContext* _context = nullptr;
    std::unique_ptr<VirtualNetwork> _network;
    std::unique_ptr<ZxNetUsb> _card;
    std::unique_ptr<ComPort> _com;
    std::unique_ptr<ISerialPeer> _machinePeer;   ///< on the machine's own non-16550 port (Atm2Kbc)
    std::unique_ptr<Atm2IoEsp> _atm2IoEsp;       ///< the card on the ATM Turbo 2+ INTERNAL I/O connector
    std::unique_ptr<ZiFi> _zifi;                 ///< the TS AVR firmware's ZiFi block (on `_com`'s #xxEF)
    std::vector<SlotCard> _slotCards;            ///< network cards in expansion slots (they outlive a network-off refit)
    /// UART cards' 16550 registers across a refit by port key (the chip stays; its line's peer is rebuilt with the
    /// network)
    std::vector<std::pair<std::string, Uart16550::State>> _serialKeep;
    std::unique_ptr<EthernetGateway> _gateway;   ///< the slot cards' wire to the virtual network
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
