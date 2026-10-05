#include "emulator/io/network/networkmanager.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "base/featuremanager.h"
#include "common/network/hostnetbridge.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/io/keyboard/atm2kbc.h"
#include "emulator/ports/models/portdecoder_atm710.h"
#include "emulator/ports/models/portdecoder_profi.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/mainloop.h"
#include "emulator/io/network/networkspec.h"
#include "emulator/io/serial/comportspec.h"
#include "emulator/io/serial/esp/atmodule.h"
#include "emulator/io/serial/esp/espdescribe.h"
#include "emulator/io/serial/esp/zifinativemodule.h"
#include "emulator/io/serial/esp/espnetmodule.h"
#include "emulator/io/serial/hayesmodempeer.h"
#include "emulator/io/serial/serialpeer.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/io/sprinter/isa/isaslotconfig.h"

NetworkManager::NetworkManager(EmulatorContext* context) : _context(context)
{
    _traffic = std::make_unique<NetworkTrafficTap>([context]() { return VirtualNetwork::TrafficTimeOf(context); });
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
    const std::string zifiPeer = peerOf(net.zifi, "NONE");
    if (caps.serialPort == SerialPort::EvoAvr || caps.serialPort == SerialPort::ZiFi)
    {
        // The ZX-Evo's AVR (TS-Conf: the TS firmware): its 16550 and, with a TS-Labs firmware, ZiFi
        plan.serial = Plan::Serial::EvoAvr;
        plan.avr = static_cast<uint8_t>(caps.uart.avr);
        if (networkOn)
            plan.peer = comPeer;
        plan.zifi = caps.zifi;
        if (caps.zifi && networkOn)
            plan.zifiPeer = zifiPeer;
    }
    else if (caps.serialPort == SerialPort::Atm2Kbc || caps.serialPort == SerialPort::Profi8251)
    {
        // Not on #xxEF: a ZX-WiFi card fits beside it
        plan.machineSerial = true;
        if (networkOn)
            plan.machinePeer = comPeer;
    }
    else if (caps.serialPort == SerialPort::None && !comPeer.empty())
    {
        const bool atm = decoder && caps.reloadFirmware;
        plan.notes.push_back(atm ? "ComPort: the keyboard controller firmware has no RS-232 (V22-*: use V31-* or later)"
                                 : "ComPort: the machine has no serial port of its own - a ZX-WiFi card adds one (Card=ZXWIFI)");
    }

    if (!caps.zifi && !zifiPeer.empty())
        plan.notes.push_back("ZiFi: the machine has no ZiFi (TS-Conf, or a ZX-Evo with [EVO] Avr=TS2016-02 / TS2016-04)");

    // Network cards in expansion slots: hardware the machine config fits (like the machine's own serial port they
    // are there with the network off - then with no cable)
    for (const PortDecoder::NetworkCapabilities::Slot& slot : caps.expansionSlots)
    {
        if (!slot.networkCard)
            continue;
        if (!IsFrameCardKind(slot.configured) && slot.configured != "sprinteresp" && slot.configured != "modem" &&
            slot.configured != "dual16552")
        {
            const std::string why = slot.configured + ": not built yet";
            plan.notes.push_back(slot.id + ": " + why);
            continue;
        }
        Plan::SlotCard card;
        card.slotId = slot.id;
        card.kind = slot.configured;
        card.chip = slot.chip;
        card.portKey = slot.portKey;
        card.base = slot.base;
        card.irq = slot.irq;
        card.mac = slot.mac;
        if (slot.configured == "sprinteresp")
        {
            // The board's ESP-12F is an ESP8266: an ESP8266 build from [NETWORK] EspChip, else ESP-AT 2.2.2 (what the
            // Sprinter ESP Network Kit expects); automatic MAC in Espressif's form, per instance and slot
            card.chip = "TL16C550C";
            const auto firmware = static_cast<EspModule::Firmware>(net.espChip);
            card.espFirmware = static_cast<uint8_t>(EspModule::ChipOf(firmware) == EspModule::Chip::Esp8266
                                                        ? firmware
                                                        : EspModule::Firmware::Esp8266At222);
            if (slot.macAuto)
                card.mac = {0x5C, 0xCF, 0x7F, 0x5A, slot.instance, card.mac[5]};
            card.peer = peerOf(slot.peer.c_str(), "AT");
        }
        if (slot.configured == "modem")
        {
            // The card's own modem on its UART (peer MODEM; another ComPortSpec replaces it, for tests)
            card.chip = "16550A";
            card.peer = peerOf(slot.peer.c_str(), "MODEM");
        }
        if (slot.configured == "dual16552")
        {
            // SprinterSerial: COM1 (USB) and COM2 (RS-232) lines, nothing on them by default
            card.chip = "PC16552D";
            card.peer = peerOf(slot.peer.c_str(), "NONE");
            card.peerB = peerOf(slot.peerB.c_str(), "NONE");
            card.irqB = slot.irqB;
            card.partialDecode = slot.partialDecode;
        }
        plan.slotCards.push_back(card);
    }
    plan.ethernetLink = false;
    for (const Plan::SlotCard& card : plan.slotCards)
        plan.ethernetLink = plan.ethernetLink || (networkOn && IsFrameCardKind(card.kind));

    if (!networkOn)
        return plan;

    // ATM2IOESP: on the ATM Turbo 2+ INTERNAL I/O connector, not the ZX-Bus
    if (net.card & networkspec::kCardAtm2IoEsp)
    {
        if (caps.internalIo)
        {
            plan.atm2IoEsp = true;
            plan.atm2IoEspPeer = peerOf(net.atm2IoEsp, "AT");
            plan.atm2IoEspAddress = net.atm2IoEspAddress;
        }
        else
            plan.notes.push_back("ATM2IOESP: the machine has no ATM Turbo 2+ INTERNAL I/O connector");
    }

    const uint8_t cards = static_cast<uint8_t>(net.card & ~networkspec::kCardAtm2IoEsp);   // the ZX-Bus ones
    if (cards && !caps.zxBus)
    {
        plan.notes.push_back("Card: the machine has no ZX-Bus");
        return plan;
    }
    plan.zxNetUsb = (cards & networkspec::kCardZxNetUsb) != 0;
    if (cards & networkspec::kCardZxWifi)
    {
        // The card's 16550 sits on #F8EF..#FFEF: a clash with the machine's own #xxEF device disables the card
        const bool ownOnEf = caps.serialPort != SerialPort::None && caps.serialPort != SerialPort::Atm2Kbc &&
                             caps.serialPort != SerialPort::Profi8251;
        if (plan.serial != Plan::Serial::None || ownOnEf || (decoder && decoder->ReservesLowByte(ComPort::kPortLowByte)))
        {
            const char* note = caps.serialPort == SerialPort::EvoAvr
                                   ? "ZXWIFI: not fitted - ports #F8EF..#FFEF are the ZX-Evo AVR's COM port"
                                   : "ZXWIFI: not fitted - the machine owns ports #xxEF";
            plan.notes.push_back(note);
        }
        else
        {
            plan.serial = Plan::Serial::ZxWifi;
            plan.peer = peerOf(net.zxWifi, "AT");
        }
    }
    return plan;
}

std::unique_ptr<ISerialPeer> NetworkManager::MakePeer(const std::string& specText, uint32_t espBaud,
                                                      std::optional<EspModule::Firmware> firmware,
                                                      const std::array<uint8_t, 6>* mac) const
{
    ComPortSpec spec;
    std::string error;
    ComPortSpec::Parse(specText.empty() ? "NONE" : specText, spec, error);
    const EspModule::Firmware build =
        firmware ? *firmware : static_cast<EspModule::Firmware>(_context->config.network.espChip);
    const EspModule::Chip chip = EspModule::ChipOf(build);
    std::unique_ptr<EspModule> esp;
    switch (spec.kind)
    {
        case ComPortSpec::Kind::Loopback:
            return std::make_unique<LoopbackPeer>();
        case ComPortSpec::Kind::Plug:
            return std::make_unique<LoopbackPeer>(true);
        case ComPortSpec::Kind::Tcp:
        case ComPortSpec::Kind::Serial:
            return std::make_unique<StreamPeer>(_network.get(), spec, _context->config.network.comModemLines != 0);
        case ComPortSpec::Kind::At:
            // The spec's own firmware (AT,ESP8266-AT222) wins over the board's and [NETWORK] EspChip
            esp = std::make_unique<AtModule>(_network.get(),
                                             spec.firmware != ComPortSpec::kDefaultFirmware
                                                 ? static_cast<EspModule::Firmware>(spec.firmware)
                                                 : build,
                                             mac);
            break;
        case ComPortSpec::Kind::ZiFiNative:
            esp = std::make_unique<ZiFiNativeModule>(_network.get(),
                                                     spec.firmware != ComPortSpec::kDefaultFirmware
                                                         ? static_cast<ZiFiNativeModule::Variant>(spec.firmware)
                                                         : ZiFiNativeModule::Variant::S3,
                                                     mac);
            break;
        case ComPortSpec::Kind::Espnet:
            esp = std::make_unique<EspnetModule>(_network.get(), chip);
            break;
        case ComPortSpec::Kind::Modem:
            return std::make_unique<HayesModemPeer>(_network.get(), spec, _context->config.network.modemPhonebook);
        default:
            return nullptr;   // nothing on the line
    }
    // The firmware's own rate: as written, else the port's
    esp->SetFactoryBaud(spec.baud ? spec.baud : espBaud);
    return esp;
}

void NetworkManager::FitMachineSerial(const Plan& plan)
{
    PortDecoder* decoder = _context->pPortDecoder;
    const PortDecoder::NetworkCapabilities caps = decoder ? decoder->DescribeNetwork() : PortDecoder::NetworkCapabilities();
    if (!caps.attachSerialPeer)
        return;
    _machinePeer = MakePeer(plan.machinePeer, caps.espBaud);
    if (!_machinePeer)
        return;   // nothing on the line: the firmware's UART still runs
    caps.attachSerialPeer(_machinePeer.get());
    _context->pMachineSerialPeer = _machinePeer.get();
}

void NetworkManager::FitAtm2IoEsp(const Plan& plan)
{
    PortDecoder* decoder = _context->pPortDecoder;
    const PortDecoder::NetworkCapabilities caps = decoder ? decoder->DescribeNetwork() : PortDecoder::NetworkCapabilities();
    if (!caps.internalIo)
        return;
    // The card's ESP32 ships with the AT firmware at 115200 (RTS / CTS)
    _atm2IoEsp = std::make_unique<Atm2IoEsp>(_context, MakePeer(plan.atm2IoEspPeer, kDefaultEspBaud), plan.atm2IoEspAddress);
    caps.internalIo(_atm2IoEsp.get(), true);
    _context->pAtm2IoEsp = _atm2IoEsp.get();
}

void NetworkManager::FitCom(const Plan& plan, const AvrKeep* keep)
{
    std::unique_ptr<ISerialPeer> peer = MakePeer(plan.peer, kDefaultEspBaud);
    PortDecoder* decoder = _context->pPortDecoder;
    Uart16550::Params params = Uart16550::DefaultParams(Uart16550::Flavor::Chip16550);
    ComPort::RegisterOf registerOf;
    std::function<void()> waitPortInterrupt;
    if (plan.serial == Plan::Serial::EvoAvr && decoder)
    {
        PortDecoder::NetworkCapabilities caps = decoder->DescribeNetwork();
        params = caps.uart;
        registerOf = std::move(caps.serialRegister);
        waitPortInterrupt = std::move(caps.waitPortInterrupt);
    }
    _com = std::make_unique<ComPort>(_context, params, std::move(peer), std::move(registerOf));
    if (keep && keep->com)
        _com->Uart().LoadState(*keep->com);   // the same chip, a new cable: its registers stay
    if (plan.zifi)
    {
        // The ZiFi board's ESP ships at 115200, the only rate the AVR's USART0 runs. The board carries an ESP-01
        // (ESP8266, 1 MB flash): an ESP8266 build from [NETWORK] EspChip, else NonOS AT 1.7.4 (what original
        // ZiFi users flashed; ESP-AT 2.2.x on request: ZiFi=AT,ESP8266-AT222)
        const auto chipBuild = static_cast<EspModule::Firmware>(_context->config.network.espChip);
        std::unique_ptr<ISerialPeer> zifiPeer =
            MakePeer(plan.zifiPeer, kDefaultEspBaud,
                     EspModule::ChipOf(chipBuild) == EspModule::Chip::Esp8266 ? chipBuild : EspModule::Firmware::Esp8266NonOs174);
        if (auto* at = dynamic_cast<AtModule*>(zifiPeer.get()); at && at->GetChip() == EspModule::Chip::Esp8266)
            at->SetFlash(atdialect::Flash::OneMb);
        _zifi = std::make_unique<ZiFi>(_context, _com->Uart(), std::move(zifiPeer),
                                       std::move(waitPortInterrupt));
        if (keep && keep->zifiLine)
            _zifi->Line().Uart().LoadState(*keep->zifiLine);
        if (keep && keep->zifi)
            _zifi->LoadState(*keep->zifi);
        _com->SetZiFi(_zifi.get());
        _context->pZiFi = _zifi.get();
    }
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
        if (change->remoteAccess)
            net.remoteAccess = *change->remoteAccess ? 1 : 0;
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
        if (change->avrFirmware)
            _context->config.atm.evo_avr = *change->avrFirmware;
        if (change->atm2IoEsp)
            copy(net.atm2IoEsp, sizeof(net.atm2IoEsp), *change->atm2IoEsp);
        if (change->atm2IoEspAddress)
            net.atm2IoEspAddress = *change->atm2IoEspAddress;
        if (change->zifi)
            copy(net.zifi, sizeof(net.zifi), *change->zifi);
        if (change->modemPhonebook)
            copy(net.modemPhonebook, sizeof(net.modemPhonebook), *change->modemPhonebook);
        if (change->ethernetMode)
            net.ethernetMode = *change->ethernetMode;
        if (change->bridgeAdapter)
            copy(net.bridgeAdapter, sizeof(net.bridgeAdapter), *change->bridgeAdapter);
        if (!change->slotPeers.empty() && _context->pPortDecoder)
        {
            const PortDecoder::NetworkCapabilities caps = _context->pPortDecoder->DescribeNetwork();
            for (const auto& [slotKey, peer] : change->slotPeers)
            {
                const std::string slotId = slotKey.substr(0, 4);
                const int channel = slotKey.size() > 4 ? 1 : 0;
                for (const PortDecoder::NetworkCapabilities::Slot& slot : caps.expansionSlots)
                {
                    if (slot.id == slotId && slot.setPeer)
                        slot.setPeer(channel, peer);
                }
            }
        }
        if (change->kbcFirmware)
        {
            // A new controller chip in the socket: it boots afresh; its peer is plugged in again below
            _context->config.atm.kbc_firmware = *change->kbcFirmware;
            _context->config.atm.kbc_rom_path[0] = '\0';
            PortDecoder* decoder = _context->pPortDecoder;
            const PortDecoder::NetworkCapabilities caps = decoder ? decoder->DescribeNetwork() : PortDecoder::NetworkCapabilities();
            std::string why;
            _firmwareNote.clear();
            if (caps.reloadFirmware && !caps.reloadFirmware(why))
                _firmwareNote = "kbc_firmware: " + why;
        }
        if (change->OnlyRemoteAccess())
        {
            // A host-side setting: the listeners move to the new address, the devices and connections stay
            if (_network)
                _network->SetRemoteAccess(net.remoteAccess != 0);
        }
        else
            _forceRefit = true;
    }
    Plan plan = MakePlan();
    if (!_firmwareNote.empty())
        plan.notes.push_back(_firmwareNote);
    const bool same = plan == _plan && !_forceRefit &&
                      (_network || _com || _atm2IoEsp || !_slotCards.empty() ||
                       (!plan.zxNetUsb && plan.serial == Plan::Serial::None && plan.machinePeer.empty() && !plan.atm2IoEsp &&
                        plan.slotCards.empty()));
    _forceRefit = false;
    if (same)
    {
        _plan.notes = plan.notes;
        UpdateStatus();
        return;
    }

    // The machine's own serial port keeps its registers across a refit (the
    // cable changes, not the chip)
    std::optional<AvrKeep> keep;
    // (a new AVR firmware restarts the AVR: its UART starts afresh)
    if (_com && plan.serial == Plan::Serial::EvoAvr && _plan.serial == Plan::Serial::EvoAvr && plan.avr == _plan.avr)
    {
        keep.emplace();
        keep->com.emplace();
        _com->Uart().SaveState(*keep->com);
        if (_zifi && plan.zifi)
        {
            keep->zifiLine.emplace();
            _zifi->Line().Uart().SaveState(*keep->zifiLine);
            keep->zifi.emplace();
            _zifi->SaveState(*keep->zifi);
        }
    }
    // The Ethernet boards stay when their part of the plan is the same (hardware: only the cable changes); a UART
    // card is rebuilt around its kept 16550 registers, because its peer lives on the network that is rebuilt
    auto frameCards = [](const Plan& p) {
        std::vector<Plan::SlotCard> out;
        for (const Plan::SlotCard& c : p.slotCards)
        {
            if (IsFrameCardKind(c.kind))
                out.push_back(c);
        }
        return out;
    };
    const bool keepSlotCards = frameCards(plan) == frameCards(_plan) && !_slotCards.empty();
    const bool networkOnNow = !_context->pFeatureManager || _context->pFeatureManager->isEnabled(Features::kNetwork);
    Unplug(keepSlotCards);
    _plan = plan;

    bool slotPeers = false;
    for (const Plan::SlotCard& card : plan.slotCards)
        slotPeers = slotPeers || ((!card.peer.empty() || !card.peerB.empty()) && networkOnNow);
    if (plan.zxNetUsb || !plan.peer.empty() || !plan.machinePeer.empty() || !plan.atm2IoEspPeer.empty() ||
        !plan.zifiPeer.empty() || plan.ethernetLink || slotPeers)
    {
        std::unique_ptr<IHostNet> host;
        if (_context->config.network.hostAccess)
        {
            HostNetBridge::Options options;
            options.connectTimeoutMs = _context->config.network.connectTimeoutMs;
            host = std::make_unique<HostNetBridge>(options);
        }
        _network = std::make_unique<VirtualNetwork>(_context, std::move(host), BuildConfig(_context));
        _network->UseTap(_traffic.get());
        _context->pVirtualNetwork = _network.get();
    }
    if (plan.ethernetLink && _network)
    {
        // The frame cards' wire: the virtual network's gateway, NAT or bridged to a host adapter (network SN6)
        VirtualNetwork::FrameSettings frames;
        frames.bridge = _context->config.network.ethernetMode == 1;
        frames.bridgeAdapter = _context->config.network.bridgeAdapter;
        frames.hostFrames = _hostFramesOverride.get();
        // A modem that answers calls owns its guest port: the gateway leaves that Forward= rule to it
        std::vector<uint16_t> modemPorts;
        auto modemPort = [&modemPorts](const std::string& text) {
            ComPortSpec spec;
            std::string error;
            if (!text.empty() && ComPortSpec::Parse(text, spec, error) && spec.kind == ComPortSpec::Kind::Modem && spec.port)
                modemPorts.push_back(spec.port);
        };
        modemPort(plan.peer);
        modemPort(plan.machinePeer);
        modemPort(plan.atm2IoEspPeer);
        modemPort(plan.zifiPeer);
        for (const Plan::SlotCard& card : plan.slotCards)
        {
            modemPort(card.peer);
            modemPort(card.peerB);
        }
        frames.reservedGuestPorts = std::move(modemPorts);
        _network->EnableFrames(frames);
        _context->pEthernetGateway = _network->Gateway();
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
    if (plan.machineSerial)
        FitMachineSerial(plan);
    if (plan.atm2IoEsp)
        FitAtm2IoEsp(plan);
    FitSlotCards(plan);
    NameTrafficGuests();
    UpdateStatus();
}

void NetworkManager::NameTrafficGuests()
{
    if (!_network)
        return;
    // A port's peer: an ESP module's network stack, a modem's call, a TCP link
    auto name = [this](const ISerialPeer* peer, const std::string& port) {
        if (!peer)
            return;
        if (const auto* esp = dynamic_cast<const EspModule*>(peer))
            _network->NameGuest(&esp->Stack(), port + ".esp");
        else if (const auto* modem = dynamic_cast<const HayesModemPeer*>(peer))
            _network->NameGuest(modem, port + ".modem");
        else if (const auto* stream = dynamic_cast<const StreamPeer*>(peer))
            _network->NameGuest(stream, port + ".tcp");
    };
    if (_card)
        _network->NameGuest(&_card->Chip(), "zxnetusb");
    if (_com)
        name(_com->Peer(), "com");
    name(_machinePeer.get(), "machine");
    if (_atm2IoEsp)
        name(_atm2IoEsp->Com().Peer(), "atm2ioesp");
    if (_zifi)
        name(_zifi->Line().Peer(), "zifi");
    for (const SlotCard& card : _slotCards)
    {
        if (!card.serial)
            continue;
        for (int ch = 0; ch < card.serial->Channels(); ++ch)
            name(card.serial->Com(ch).Peer(), card.slotId + (ch ? ".b" : ""));
    }
}

PcSerialCard* NetworkManager::SerialCard(const std::string& slotId) const
{
    for (const SlotCard& c : _slotCards)
    {
        if (c.serial && c.slotId == slotId)
            return c.serial.get();
    }
    return nullptr;
}

IEthernetCard* NetworkManager::EthernetCard(const std::string& portKey) const
{
    for (const SlotCard& c : _slotCards)
    {
        if (c.ethernet && c.ethernet->PortKey() == portKey)
            return c.ethernet.get();
    }
    return nullptr;
}

void NetworkManager::FitSlotCards(const Plan& plan)
{
    // Kept across the refit (the Ethernet boards): only the cable is new
    for (SlotCard& card : _slotCards)
    {
        if (card.ethernet && Gateway())
        {
            card.ethernet->SetLink(_network.get());
            _network->AttachStation(card.ethernet.get());
        }
    }
    std::vector<std::pair<std::string, Uart16550::State>> keep;
    keep.swap(_serialKeep);
    if (plan.slotCards.empty() || !_context || !_context->pPortDecoder)
        return;
    PortDecoder::NetworkCapabilities caps = _context->pPortDecoder->DescribeNetwork();
    EmulatorContext* context = _context;
    // The cards' time: base T-states (3.5 MHz units) - the wire speed does not change with the CPU's turbo
    auto clock = [context]() -> uint64_t {
        if (!context->pCore || !context->pCore->GetZ80())
            return context->emulatorState.t_states;
        const uint32_t multiplier = context->emulatorState.current_z80_frequency_multiplier
                                        ? context->emulatorState.current_z80_frequency_multiplier
                                        : 1u;
        return context->emulatorState.t_states + context->pCore->GetZ80()->t / multiplier;
    };
    for (const Plan::SlotCard& want : plan.slotCards)
    {
        bool present = false;
        for (const SlotCard& have : _slotCards)
            present = present || have.slotId == want.slotId;
        if (present)
            continue;
        for (PortDecoder::NetworkCapabilities::Slot& slot : caps.expansionSlots)
        {
            if (slot.id != want.slotId)
                continue;
            if (want.kind == "modem" || want.kind == "dual16552")
            {
                PcSerialCard::Settings settings;
                settings.preset = want.kind == "modem" ? PcSerialCard::Preset::Modem : PcSerialCard::Preset::Dual16552;
                settings.base = want.base;
                settings.irq = want.irq;
                settings.irqB = want.irqB;
                settings.partialDecode = want.partialDecode;
                SlotCard card;
                card.slotId = want.slotId;
                card.serial = std::make_unique<PcSerialCard>(_context, settings, MakePeer(want.peer, kDefaultEspBaud),
                                                             settings.preset == PcSerialCard::Preset::Dual16552
                                                                 ? MakePeer(want.peerB, kDefaultEspBaud)
                                                                 : nullptr,
                                                             want.slotId);
                for (const auto& [key, state] : keep)
                {
                    for (int ch = 0; ch < card.serial->Channels(); ++ch)
                    {
                        if (key == card.serial->PortKey(ch))
                            card.serial->RestoreUart(ch, state);
                    }
                }
                std::string why;
                if (!slot.fit || !slot.fit(card.serial.get(), why))
                {
                    _plan.notes.push_back(slot.id + ": the slot refused the card (" + why + ")");
                    continue;
                }
                _slotCards.push_back(std::move(card));
                continue;
            }
            if (want.kind == "sprinteresp")
            {
                SlotCard card;
                card.slotId = want.slotId;
                // The ESP-12F on the card: AT firmware at 115200 (or what the line names: a real ESP on USB, ...)
                card.serial = std::make_unique<PcSerialCard>(
                    _context, PcSerialCard::Preset::SprinterEsp,
                    MakePeer(want.peer, kDefaultEspBaud, static_cast<EspModule::Firmware>(want.espFirmware), &want.mac),
                    want.slotId);
                for (const auto& [key, state] : keep)
                {
                    if (key == card.serial->PortKey(0))
                        card.serial->RestoreUart(state);
                }
                std::string why;
                if (!slot.fit || !slot.fit(card.serial.get(), why))
                {
                    _plan.notes.push_back(slot.id + ": the slot refused the card (" + why + ")");
                    continue;
                }
                _slotCards.push_back(std::move(card));
                continue;
            }
            SlotCard card;
            card.slotId = want.slotId;
            if (want.kind == "el3c509b")
            {
                // The 3Com EtherLink III: its EEPROM carries the slot's base, IRQ and MAC (network tdd §9)
                EtherLink3::Settings settings;
                settings.variant = want.chip == "3C509B-TP" ? EtherLink3::Variant::Tp : EtherLink3::Variant::Tpo;
                settings.base = want.base;
                settings.irq = want.irq;
                settings.mac = want.mac;
                settings.key = want.portKey;
                card.ethernet = std::make_unique<EtherLink3>(settings, clock);
            }
            else
            {
                Ne2000Board::Settings settings;
                settings.variant = want.chip == "UM9003"   ? Ne2000Board::Variant::Um9003
                                   : want.chip == "NE1000" ? Ne2000Board::Variant::Ne1000
                                                           : Ne2000Board::Variant::Rtl8019as;
                settings.base = want.base;
                settings.irq = want.irq;
                settings.mac = want.mac;
                settings.key = want.portKey;
                card.ethernet = std::make_unique<Ne2000Board>(settings, clock);
            }
            std::string why;
            if (!slot.fit || !slot.fit(card.ethernet.get(), why))
            {
                _plan.notes.push_back(slot.id + ": the slot refused the card (" + why + ")");
                continue;
            }
            if (Gateway())
            {
                card.ethernet->SetLink(_network.get());
                _network->AttachStation(card.ethernet.get());
            }
            _slotCards.push_back(std::move(card));
        }
    }
}

void NetworkManager::UnplugSlotCards()
{
    if (_slotCards.empty())
        return;
    if (_context && _context->pPortDecoder)
    {
        PortDecoder::NetworkCapabilities caps = _context->pPortDecoder->DescribeNetwork();
        for (const SlotCard& card : _slotCards)
        {
            for (PortDecoder::NetworkCapabilities::Slot& slot : caps.expansionSlots)
            {
                std::string why;
                if (slot.id == card.slotId && slot.fit)
                    slot.fit(nullptr, why);
            }
        }
    }
    _slotCards.clear();
}

void NetworkManager::Unplug(bool keepSlotCards)
{
    if (_context)
    {
        if ((_machinePeer || _atm2IoEsp) && _context->pPortDecoder)
        {
            const PortDecoder::NetworkCapabilities caps = _context->pPortDecoder->DescribeNetwork();
            if (_machinePeer && caps.attachSerialPeer)
                caps.attachSerialPeer(nullptr);
            if (_atm2IoEsp && caps.internalIo)
                caps.internalIo(_atm2IoEsp.get(), false);
        }
        _context->pAtm2IoEsp = nullptr;
        _context->pZiFi = nullptr;
        _context->pZxNetUsb = nullptr;
        _context->pComPort = nullptr;
        _context->pMachineSerialPeer = nullptr;
        _context->pVirtualNetwork = nullptr;
    }
    // The adapters first: their sockets close through the virtual network
    if (_context)
        _context->pEthernetGateway = nullptr;
    for (SlotCard& card : _slotCards)
    {
        if (card.ethernet)
            card.ethernet->SetLink(nullptr);   // the wire goes with the virtual network (its gateway and bridge too)
    }
    // UART cards: their 16550 registers are kept for the card fitted next; the peer goes with the network
    for (const SlotCard& card : _slotCards)
    {
        if (card.serial)
        {
            for (int ch = 0; ch < card.serial->Channels(); ++ch)
            {
                Uart16550::State state;
                card.serial->Com(ch).Uart().SaveState(state);
                _serialKeep.emplace_back(card.serial->PortKey(ch), state);
            }
        }
    }
    if (!keepSlotCards)
        UnplugSlotCards();
    else
    {
        std::vector<SlotCard> kept;
        std::vector<std::string> pulled;
        for (SlotCard& card : _slotCards)
        {
            if (card.serial)
                pulled.push_back(card.slotId);
            else
                kept.push_back(std::move(card));
        }
        if (!pulled.empty() && _context && _context->pPortDecoder)
        {
            PortDecoder::NetworkCapabilities caps = _context->pPortDecoder->DescribeNetwork();
            for (PortDecoder::NetworkCapabilities::Slot& slot : caps.expansionSlots)
            {
                std::string why;
                if (slot.fit && std::find(pulled.begin(), pulled.end(), slot.id) != pulled.end())
                    slot.fit(nullptr, why);
            }
        }
        _slotCards = std::move(kept);
    }
    _machinePeer.reset();
    _atm2IoEsp.reset();
    if (_com)
        _com->SetZiFi(nullptr);
    _zifi.reset();   // before the COM port: it holds the 16550's rings
    _com.reset();
    _card.reset();
    _network.reset();
    _plan = Plan();
}

bool NetworkManager::Change::Empty() const
{
    return !card && !hostAccess && !dnsPass && !hosts && !forwards && !remoteAccess && !connectTimeoutMs && !comPort &&
           !zxWifi && !comModemLines && !espChip && !avrFirmware && !kbcFirmware && !atm2IoEsp && !atm2IoEspAddress &&
           !zifi && !modemPhonebook && !ethernetMode && !bridgeAdapter && slotPeers.empty();
}

bool NetworkManager::Change::OnlyRemoteAccess() const
{
    if (!remoteAccess)
        return false;
    Change rest = *this;
    rest.remoteAccess.reset();
    return rest.Empty();
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
    if (change.zifi)
    {
        ComPortSpec spec;
        if (!ComPortSpec::Parse(*change.zifi, spec, error))
        {
            error = "zifi: " + error;
            return false;
        }
        if (change.zifi->size() >= sizeof(_context->config.network.zifi))
        {
            error = "zifi: too long";
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
    if (change.bridgeAdapter && change.bridgeAdapter->size() >= sizeof(_context->config.network.bridgeAdapter))
    {
        error = "bridge_adapter: too long";
        return false;
    }
    if (change.modemPhonebook)
    {
        std::map<std::string, std::string> book;
        if (!HayesModemPeer::ParsePhonebook(*change.modemPhonebook, book, error))
        {
            error = "modem_phonebook: " + error;
            return false;
        }
        if (change.modemPhonebook->size() >= sizeof(_context->config.network.modemPhonebook))
        {
            error = "modem_phonebook: too long";
            return false;
        }
    }
    for (const auto& [slotId, peer] : change.slotPeers)
    {
        if (peer.size() >= sizeof(sprinterisa::SlotConfig::peer))
        {
            error = slotId + "_peer: too long";
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
        _network->Reset(ComPort::SerialNetGuests(_context));
    UpdateStatus();
}

void NetworkManager::OnFrame()
{
    OnFrameDevices();
    OnFrameHost();
}

void NetworkManager::OnFrameDevices()
{
    // The devices' own frame work: a pending refit, the cards' and the gateway's timers, the serial peers' flushes,
    // retries, a dialed link's connect, a modem's escape guard time. It runs before the TTD checkpoint of the boundary
    // (MainLoop), so the checkpoint holds its result: a replay that starts from that checkpoint does not run it again
    // and a replay that runs through the boundary runs it in the same place. The host's answers come after it (Pump,
    // OnFrameHost; during a replay from the journal at the first instruction after the boundary). Work after Pump would
    // see an answer live that it sees a frame later in a replay (the modem's DNS answer and the connect it starts)
    if (_refitPending)
        Refit();
    for (SlotCard& card : _slotCards)
    {
        if (card.ethernet)
            card.ethernet->OnFrame();
    }
    // The ports' peers work with a virtual network (without one a line has nothing on it: a ZX-Evo's AVR UART
    // catches up at its next access); the cards in expansion slots always (their UARTs feed the slot's IRQ line)
    if (_network)
    {
        _network->OnFrameDevices();   // the frame cards' wire: the gateway's timers and queued frames
        if (_com)
            _com->OnFrame();
        if (_machinePeer)
            _machinePeer->OnFrame();
        if (_atm2IoEsp)
            _atm2IoEsp->OnFrame();
        if (_zifi)
            _zifi->OnFrame();
    }
    for (SlotCard& card : _slotCards)
    {
        if (card.serial)
            card.serial->OnFrame();
    }
}

void NetworkManager::OnFrameHost()
{
    if (_network)
    {
        // The host's answers (journaled inputs: after the boundary's checkpoint, like the keyboard's); the gateway
        // and the peers handle each at once (OnNetEvent)
        _network->Pump();
        UpdateStatus();
    }
    else if (!_slotCards.empty())
        UpdateStatus();   // a slot card's registers in the report follow every frame
    else if ((_com || _plan.machineSerial || _atm2IoEsp) && _context && _context->emulatorState.frame_counter % 25 == 0)
    {
        // A serial port with nothing on its line (a ZX-Evo's AVR UART): no
        // peer to pump, the status copy twice a second is enough
        UpdateStatus();
    }
}

void NetworkManager::FillPeerStatus(const ISerialPeer* peer, Status::Com& c) const
{
    if (!peer)
        return;
    c.peer = peer->Kind();
    c.target = peer->Target();
    c.connected = peer->Connected();
    c.pending = peer->Pending();
    if (const auto* esp = dynamic_cast<const EspModule*>(peer))
    {
        c.peerBaud = esp->Baud();
        c.requests = esp->RequestsServed();
        c.esp = StateNode::Object();
        espdescribe::Describe(*esp, c.esp);
        for (const EspModule::Exchange& e : esp->RecentExchanges())
            c.exchanges.emplace_back(e.request, e.reply);
    }
    if (const auto* stream = dynamic_cast<const StreamPeer*>(peer))
    {
        static const char* const kPhases[] = {"idle", "resolving", "connecting", "connected"};
        c.phase = kPhases[static_cast<int>(stream->GetPhase())];
        c.error = stream->LastError();
    }
    if (const auto* modem = dynamic_cast<const HayesModemPeer*>(peer))
    {
        c.modem = StateNode::Object();
        modem->Describe(c.modem);
        for (const HayesModemPeer::Exchange& e : modem->RecentExchanges())
            c.exchanges.emplace_back(e.command, e.result);
    }
}

void NetworkManager::UpdateStatus()
{
    Status st;
    st.fitted = _card != nullptr;
    st.notes = _plan.notes;
    if (_context)
    {
        const auto& net = _context->config.network;
        auto& s = st.settings;
        s.card = networkspec::CardsToString(net.card);
        s.comPort = net.comPort[0] ? std::string(net.comPort) : std::string("NONE");
        s.zxWifi = net.zxWifi[0] ? std::string(net.zxWifi) : std::string("AT");
        s.atm2IoEsp = net.atm2IoEsp[0] ? std::string(net.atm2IoEsp) : std::string("AT");
        s.atm2IoEspAddress = net.atm2IoEspAddress;
        s.zifi = net.zifi[0] ? std::string(net.zifi) : std::string("NONE");
        s.modemPhonebook = net.modemPhonebook;
        s.ethernetMode = net.ethernetMode == 1 ? "BRIDGE" : "NAT";
        s.bridgeAdapter = net.bridgeAdapter;
        s.espChip = EspModule::FirmwareName(static_cast<EspModule::Firmware>(net.espChip));
        s.avrFirmware = Uart16550::AvrFirmwareName(static_cast<Uart16550::AvrFirmware>(_context->config.atm.evo_avr));
        s.dnsMode = net.dnsPass ? "PASS" : "HOST";
        s.hosts = net.hosts;
        s.forwards = net.forwards;
        s.comModemLines = net.comModemLines != 0;
        s.hostAccess = net.hostAccess != 0;
        s.remoteAccess = net.remoteAccess != 0;
        s.connectTimeoutMs = net.connectTimeoutMs;
    }
    {
        uint8_t cards = _card ? networkspec::kCardZxNetUsb : 0;
        if (_com && _plan.serial == Plan::Serial::ZxWifi)
            cards |= networkspec::kCardZxWifi;
        if (_atm2IoEsp)
            cards |= networkspec::kCardAtm2IoEsp;
        st.cards = networkspec::CardsToString(cards);
    }
    if (_context && _context->pPortDecoder)
    {
        const PortDecoder::NetworkCapabilities caps = _context->pPortDecoder->DescribeNetwork();
        using SerialPort = PortDecoder::NetworkCapabilities::SerialPort;
        st.zxBus = caps.zxBus;
        st.internalIo = static_cast<bool>(caps.internalIo);
        if (caps.reloadFirmware)   // the board has the keyboard controller socket
            st.settings.kbcFirmware = Atm2Kbc::FirmwareName(static_cast<Atm2Kbc::Firmware>(_context->config.atm.kbc_firmware));
        st.zifiMachine = caps.zifi;
        st.serialPort = caps.serialPort == SerialPort::EvoAvr      ? "evo-avr"
                        : caps.serialPort == SerialPort::ZiFi      ? "zifi"
                        : caps.serialPort == SerialPort::Atm2Kbc   ? "atm2-kbc"
                        : caps.serialPort == SerialPort::Profi8251 ? "profi-8251"
                                                                   : "none";
        if (caps.serialPort == SerialPort::Atm2Kbc)
        {
            auto& m = st.machineSerial;
            m.fitted = true;
            m.flavor = "atm2kbc";
            m.firmware = caps.firmware;
            m.baud = caps.serialBaud ? caps.serialBaud() : 0;
            m.frameBits = 10;   // mode 1: start, 8 data, stop
            m.modemLines = _context->config.network.comModemLines != 0;
            if (const auto* atm = dynamic_cast<const PortDecoder_ATM710*>(_context->pPortDecoder))
            {
                if (const Atm2Kbc* kbc = atm->GetKeyboardController())
                {
                    const Atm2Kbc::SerialLineState& line = kbc->GetSerialLine();
                    m.rts = kbc->Rts();
                    m.dtr = kbc->Dtr();
                    m.bytesIn = line.bytesIn;
                    m.bytesOut = line.bytesOut;
                    m.lost = line.lost;
                }
            }
            FillPeerStatus(_machinePeer.get(), m);
        }
        if (caps.serialPort == SerialPort::Profi8251)
        {
            // The ZX Profi v5's 8251: the line as the program set it (mode word, the 8253's counter 0)
            auto& m = st.machineSerial;
            m.fitted = true;
            m.flavor = "usart8251";
            m.baud = caps.serialBaud ? caps.serialBaud() : 0;
            m.modemLines = _context->config.network.comModemLines != 0;
            if (auto* profi = dynamic_cast<PortDecoder_Profi*>(_context->pPortDecoder))
            {
                const Usart8251& usart = profi->GetUsart();
                const Usart8251::State& chip = usart.GetState();
                m.frameBits = usart.FrameBits();
                m.rts = usart.Rts();
                m.dtr = usart.Dtr();
                m.bytesIn = chip.bytesIn;
                m.bytesOut = chip.bytesOut;
                m.lost = chip.overruns;
            }
            FillPeerStatus(_machinePeer.get(), m);
        }
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
        FillPeerStatus(_com->Peer(), c);
        c.modemLines = _context && _context->config.network.comModemLines != 0;
        c.uart = uart.GetView();
        c.baud = uart.Baud();
        c.frameBits = uart.FrameBits();

    }
    if (_atm2IoEsp)
    {
        auto& c = st.atm2IoEsp;
        const Uart16550& uart = _atm2IoEsp->Com().Uart();
        c.fitted = true;
        c.flavor = "atm2ioesp";
        FillPeerStatus(_atm2IoEsp->Com().Peer(), c);
        c.modemLines = _context && _context->config.network.comModemLines != 0;
        c.uart = uart.GetView();
        c.baud = uart.Baud();
        c.frameBits = uart.FrameBits();
        st.atm2IoEspAddress = _atm2IoEsp->Address();
    }
    if (_zifi)
    {
        auto& c = st.zifi;
        const Uart16550& uart = _zifi->Line().Uart();
        c.fitted = true;
        c.flavor = "zifi";
        if (_com)
            c.firmware = Uart16550::AvrFirmwareName(_com->Uart().GetParams().avr);
        FillPeerStatus(_zifi->Line().Peer(), c);
        c.uart = uart.GetView();
        c.baud = uart.Baud();
        c.frameBits = uart.FrameBits();
        st.zifiRegisters = _zifi->GetView();
    }
    if (_context && _context->pPortDecoder)
    {
        const PortDecoder::NetworkCapabilities caps = _context->pPortDecoder->DescribeNetwork();
        for (const PortDecoder::NetworkCapabilities::Slot& slot : caps.expansionSlots)
        {
            Status::Slot s;
            s.id = slot.id;
            s.bus = slot.bus;
            s.label = slot.label;
            s.configured = slot.configured;
            for (const SlotCard& card : _slotCards)
            {
                if (card.slotId == slot.id && card.ethernet)
                {
                    s.card = card.ethernet->Kind();
                    s.details = StateNode::Object();
                    card.ethernet->Describe(s.details);
                }
                if (card.slotId == slot.id && card.serial)
                {
                    s.card = card.serial->Kind();
                    s.details = StateNode::Object();
                    card.serial->Describe(s.details);
                    // The lines as configured ([ISA] SlotNPeer / SlotNPeerB, runtime isaN_peer / isaN_peer_b), in
                    // ComPortSpec terms
                    for (const Plan::SlotCard& want : _plan.slotCards)
                    {
                        if (want.slotId != slot.id)
                            continue;
                        s.details["peer_spec"] = want.peer.empty() ? std::string("NONE") : want.peer;
                        if (s.details.find("channel_b"))
                            s.details["channel_b"]["peer_spec"] = want.peerB.empty() ? std::string("NONE") : want.peerB;
                    }
                }
            }
            for (const std::string& note : _plan.notes)
            {
                if (note.compare(0, slot.id.size() + 1, slot.id + ":") == 0)
                    s.note = note.substr(slot.id.size() + 2);
            }
            st.expansionSlots.push_back(std::move(s));
        }
    }
    if (EthernetGateway* gateway = Gateway())
    {
        st.ethernetGateway = gateway->Describe();
        if (gateway->GetMode() == EthernetGateway::Mode::Bridge)
            st.ethernetGateway["bridge"] = _network->DescribeBridge();
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
    config.remoteAccess = net.remoteAccess != 0;

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
        else if (key == "remote_access" || key == "remoteaccess")
        {
            if (!flag(value, "remote_access", out.remoteAccess))
                return false;
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
            EspModule::Firmware firmware = EspModule::Firmware::Esp32At220;
            if (!EspModule::ParseFirmware(value, firmware))
            {
                error = "esp_chip: esp32 | esp8266 | esp8266-at221 | esp8266-at222";
                return false;
            }
            out.espChip = static_cast<uint8_t>(firmware);
        }
        else if (key == "isa1_peer" || key == "isa2_peer" || key == "isa1_peer_b" || key == "isa2_peer_b")
        {
            ComPortSpec spec;
            std::string why;
            if (!ComPortSpec::Parse(value, spec, why))
            {
                error = key + ": " + why;
                return false;
            }
            // "isa1" = the first UART's line, "isa1.b" the second's (SprinterSerial's COM2)
            out.slotPeers.emplace_back(key.substr(0, 4) + (key.size() > 9 ? ".b" : ""), spec.ToString());
        }
        else if (key == "com_modem_lines" || key == "commodemlines")
        {
            if (!flag(value, "com_modem_lines", out.comModemLines))
                return false;
        }
        else if (key == "avr_firmware" || key == "avr")
        {
            Uart16550::AvrFirmware firmware = Uart16550::kLatestAvr;
            if (!Uart16550::ParseAvrFirmware(value.c_str(), firmware) || value.empty())
            {
                error = "avr_firmware: baseconf | base2010 | base2011-04 | base2011-05 | base2011-09 | base2013 | "
                        "base2023 | ts | ts2013 | ts2016-02 | ts2016-04";
                return false;
            }
            out.avrFirmware = static_cast<uint8_t>(firmware);
        }
        else if (key == "atm2ioesp" || key == "atm2_io_esp")
        {
            ComPortSpec spec;
            std::string why;
            if (!ComPortSpec::Parse(value, spec, why))
            {
                error = "atm2ioesp: " + why;
                return false;
            }
            out.atm2IoEsp = spec.ToString();
        }
        else if (key == "atm2ioesp_address")
        {
            char* end = nullptr;
            const unsigned long address = std::strtoul(value.c_str(), &end, 0);
            if (value.empty() || !end || *end || address > 0xF8 || (address & 0x07))
            {
                error = "atm2ioesp_address: a bus address 0x00..0xF8 in steps of 8 (0xF0 Rev 1.5 / 2.0, 0xF8 Rev 1.0)";
                return false;
            }
            out.atm2IoEspAddress = static_cast<uint8_t>(address);
        }
        else if (key == "kbc_firmware" || key == "kbc")
        {
            Atm2Kbc::Firmware firmware = Atm2Kbc::kDefaultFirmware;
            if (value.empty() || !Atm2Kbc::ParseFirmware(value.c_str(), firmware))
            {
                error = "kbc_firmware: none | v22-7 | v22-11 | v22-12 | v31-7 | v31-11 | v32-7 | v32-11 | v40 | v41";
                return false;
            }
            out.kbcFirmware = static_cast<uint8_t>(firmware);
        }
        else if (key == "zifi")
        {
            ComPortSpec spec;
            std::string why;
            if (!ComPortSpec::Parse(value, spec, why))
            {
                error = "zifi: " + why;
                return false;
            }
            out.zifi = spec.ToString();
        }
        else if (key == "ethernet_mode" || key == "ethernetmode")
        {
            const std::string v = lower(value);
            if (v == "nat")
                out.ethernetMode = 0;
            else if (v == "bridge")
                out.ethernetMode = 1;
            else
            {
                error = "ethernet_mode: nat | bridge";
                return false;
            }
        }
        else if (key == "bridge_adapter" || key == "bridgeadapter" || key == "adapter")
            out.bridgeAdapter = value;
        else if (key == "modem_phonebook" || key == "phonebook")
        {
            std::map<std::string, std::string> book;
            std::string why;
            if (!HayesModemPeer::ParsePhonebook(value, book, why))
            {
                error = "modem_phonebook: " + why;
                return false;
            }
            out.modemPhonebook = value;
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
                    "' (card, host_access, dns_mode, hosts, forwards, remote_access, connect_timeout_ms, com_port, zx_wifi, "
                    "com_modem_lines, esp_chip, avr_firmware, kbc_firmware, atm2ioesp, atm2ioesp_address, zifi, isa1_peer, "
                    "isa2_peer, isa1_peer_b, isa2_peer_b, modem_phonebook, ethernet_mode, bridge_adapter)";
            return false;
        }
    }
    return true;
}
