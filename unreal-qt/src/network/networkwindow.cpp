/**
 * @file networkwindow.cpp
 * @brief The Network window: settings through NetworkManager, state through
 *        DeviceState::Network.
 */

#include "networkwindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/network/vnet/ethernetaccess.h"
#include "emulator/state/devicestate.h"
#include "cardslots/slotchangecontroller.h"

namespace
{
    QString Q(const std::string& text) { return QString::fromStdString(text); }
    std::string S(const QString& text) { return text.trimmed().toStdString(); }

    enum PeerKind
    {
        PeerNone,
        PeerLoopback,
        PeerTcp,
        PeerSerial,
        PeerEspnet,
        PeerAt,
        PeerModem,
        PeerZiFiNative,
        PeerPlug
    };

    ComPortSpec::Kind KindOf(int index)
    {
        switch (index)
        {
            case PeerLoopback: return ComPortSpec::Kind::Loopback;
            case PeerTcp: return ComPortSpec::Kind::Tcp;
            case PeerSerial: return ComPortSpec::Kind::Serial;
            case PeerEspnet: return ComPortSpec::Kind::Espnet;
            case PeerAt: return ComPortSpec::Kind::At;
            case PeerModem: return ComPortSpec::Kind::Modem;
            case PeerZiFiNative: return ComPortSpec::Kind::ZiFiNative;
            case PeerPlug: return ComPortSpec::Kind::Plug;
            default: return ComPortSpec::Kind::None;
        }
    }

    int IndexOf(ComPortSpec::Kind kind)
    {
        switch (kind)
        {
            case ComPortSpec::Kind::Loopback: return PeerLoopback;
            case ComPortSpec::Kind::Tcp: return PeerTcp;
            case ComPortSpec::Kind::Serial: return PeerSerial;
            case ComPortSpec::Kind::Espnet: return PeerEspnet;
            case ComPortSpec::Kind::At: return PeerAt;
            case ComPortSpec::Kind::Modem: return PeerModem;
            case ComPortSpec::Kind::ZiFiNative: return PeerZiFiNative;
            case ComPortSpec::Kind::Plug: return PeerPlug;
            default: return PeerNone;
        }
    }

    QString Scalar(const StateNode& node)
    {
        switch (node.kind)
        {
            case StateNode::Kind::Bool: return node.b ? QStringLiteral("true") : QStringLiteral("false");
            case StateNode::Kind::Int: return QString::number(static_cast<qlonglong>(node.i));
            case StateNode::Kind::Double: return QString::number(node.d);
            case StateNode::Kind::String: return Q(node.s);
            case StateNode::Kind::Null: return QStringLiteral("-");
            case StateNode::Kind::Array: return QObject::tr("%n item(s)", nullptr, static_cast<int>(node.items.size()));
            default: return QString();
        }
    }
}  // namespace

/// region <SerialPeerEditor>

SerialPeerEditor::SerialPeerEditor(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    _kind = new QComboBox(this);
    _kind->addItem(tr("Nothing connected"));
    _kind->addItem(tr("Echo (every byte comes back, lines held active)"));
    _kind->addItem(tr("TCP: a host endpoint (BBS, test harness)"));
    _kind->addItem(tr("Serial: a host serial device (real ESP, modem)"));
    _kind->addItem(tr("ESP module, ESPNET firmware (NedoOS)"));
    _kind->addItem(tr("ESP module, AT firmware (Espressif)"));
    _kind->addItem(tr("Hayes modem (ATDT dials host:port or a phone book number)"));
    _kind->addItem(tr("ZiFi module, native firmware (2026 ZiFi: 5A CMD LEN DATA XOR)"));
    _kind->addItem(tr("Loopback test plug (bytes back; RTS -> CTS, DTR -> DSR / DCD)"));
    layout->addWidget(_kind);

    _tcpRow = new QWidget(this);
    auto* tcp = new QHBoxLayout(_tcpRow);
    tcp->setContentsMargins(0, 0, 0, 0);
    _host = new QLineEdit(_tcpRow);
    _host->setPlaceholderText(tr("host name or a.b.c.d"));
    _host->setToolTip(tr("A name is resolved through the virtual network's DNS (Hosts first, then the host resolver)"));
    _port = new QSpinBox(_tcpRow);
    _port->setRange(1, 65535);
    _port->setValue(23);
    tcp->addWidget(new QLabel(tr("Host"), _tcpRow));
    tcp->addWidget(_host, 1);
    tcp->addWidget(new QLabel(tr("Port"), _tcpRow));
    tcp->addWidget(_port);
    layout->addWidget(_tcpRow);

    _serialRow = new QWidget(this);
    auto* serial = new QHBoxLayout(_serialRow);
    serial->setContentsMargins(0, 0, 0, 0);
    _device = new QComboBox(_serialRow);
    _device->setEditable(true);
    _device->setToolTip(tr("/dev/cu.usbserial-0001, /dev/ttyUSB0, COM3"));
    _baud = new QComboBox(_serialRow);
    _baud->setEditable(true);
    for (uint32_t rate : NetworkSerialBaudChoices())
        _baud->addItem(QString::number(rate));
    _baud->setCurrentText(QStringLiteral("115200"));
    _baud->setToolTip(tr("The rate until the ZX programs its own; the line follows the ZX after that"));
    serial->addWidget(new QLabel(tr("Device"), _serialRow));
    serial->addWidget(_device, 1);
    serial->addWidget(new QLabel(tr("Baud"), _serialRow));
    serial->addWidget(_baud);
    layout->addWidget(_serialRow);

    // An ESP module ships at the rate its firmware was built for
    _espRow = new QWidget(this);
    auto* espLine = new QHBoxLayout(_espRow);
    espLine->setContentsMargins(0, 0, 0, 0);
    _espBaud = new QComboBox(_espRow);
    _espBaud->addItem(tr("The port's default (ATM Turbo 2+: 38400, others: 115200)"), 0u);
    for (uint32_t rate : NetworkSerialBaudChoices())
        _espBaud->addItem(QString::number(rate), rate);
    _espBaud->setToolTip(tr("The module's firmware rate; the ZX must program the same, else both sides read garbage"));
    _espFirmware = new QComboBox(_espRow);
    _espFirmware->setToolTip(tr("The module's firmware: AT builds (default: [NETWORK] EspChip, or the board's own chip) "
                                "or the native ZiFi firmwares"));
    espLine->addWidget(new QLabel(tr("Firmware"), _espRow));
    espLine->addWidget(_espFirmware, 1);
    espLine->addWidget(new QLabel(tr("Module baud"), _espRow));
    espLine->addWidget(_espBaud, 1);
    layout->addWidget(_espRow);

    // A modem may also answer calls: host clients of a guest port (Forward=) ring it
    _modemRow = new QWidget(this);
    auto* modemLine = new QHBoxLayout(_modemRow);
    modemLine->setContentsMargins(0, 0, 0, 0);
    _modemPort = new QSpinBox(_modemRow);
    _modemPort->setRange(0, 65535);
    _modemPort->setSpecialValueText(tr("no incoming calls"));
    _modemPort->setToolTip(tr("Guest TCP port whose host clients ring the modem (RING, RI; ATA or S0 answers); "
                              "reach it from the host with a Forward= rule"));
    modemLine->addWidget(new QLabel(tr("Answers calls on guest port"), _modemRow));
    modemLine->addWidget(_modemPort, 1);
    layout->addWidget(_modemRow);
    connect(_modemPort, &QSpinBox::valueChanged, this, &SerialPeerEditor::edited);

    connect(_kind, &QComboBox::currentIndexChanged, this, [this] {
        updateFields();
        emit edited();
    });
    connect(_host, &QLineEdit::textEdited, this, &SerialPeerEditor::edited);
    connect(_port, &QSpinBox::valueChanged, this, &SerialPeerEditor::edited);
    connect(_device, &QComboBox::currentTextChanged, this, &SerialPeerEditor::edited);
    connect(_baud, &QComboBox::currentTextChanged, this, &SerialPeerEditor::edited);
    connect(_espBaud, &QComboBox::currentIndexChanged, this, &SerialPeerEditor::edited);
    connect(_espFirmware, &QComboBox::currentIndexChanged, this, &SerialPeerEditor::edited);
    updateFields();
}

void SerialPeerEditor::setSpec(const ComPortSpec& spec)
{
    const QSignalBlocker b1(_kind), b2(_host), b3(_port), b4(_device), b5(_baud), b6(_espBaud), b7(_modemPort),
        b8(_espFirmware);
    if (spec.kind == ComPortSpec::Kind::Modem)
        _modemPort->setValue(spec.port);
    _kind->setCurrentIndex(IndexOf(spec.kind));
    if (spec.kind == ComPortSpec::Kind::Tcp)
    {
        _host->setText(Q(spec.host));
        _port->setValue(spec.port ? spec.port : 23);
    }
    if (spec.kind == ComPortSpec::Kind::Serial)
    {
        _device->setCurrentText(Q(spec.device));
        _baud->setCurrentText(QString::number(spec.baud));
    }
    if (spec.kind == ComPortSpec::Kind::Espnet || spec.kind == ComPortSpec::Kind::At ||
        spec.kind == ComPortSpec::Kind::ZiFiNative)
    {
        updateFields();   // the firmware list for this kind
        _espFirmware->setCurrentIndex(std::max(0, _espFirmware->findData(static_cast<uint>(spec.firmware))));
        int index = _espBaud->findData(spec.baud);
        if (index < 0)
        {
            _espBaud->addItem(QString::number(spec.baud), spec.baud);
            index = _espBaud->count() - 1;
        }
        _espBaud->setCurrentIndex(index);
    }
    updateFields();
}

ComPortSpec SerialPeerEditor::spec() const
{
    ComPortSpec spec;
    spec.kind = KindOf(_kind->currentIndex());
    if (spec.kind == ComPortSpec::Kind::Tcp)
    {
        spec.host = S(_host->text());
        spec.port = static_cast<uint16_t>(_port->value());
    }
    if (spec.kind == ComPortSpec::Kind::Serial)
    {
        spec.device = S(_device->currentText());
        bool ok = false;
        const uint32_t baud = _baud->currentText().trimmed().toUInt(&ok);
        spec.baud = ok && baud ? baud : 115200;
    }
    if (spec.kind == ComPortSpec::Kind::Espnet || spec.kind == ComPortSpec::Kind::At ||
        spec.kind == ComPortSpec::Kind::ZiFiNative)
        spec.baud = _espBaud->currentData().toUInt();
    if ((spec.kind == ComPortSpec::Kind::At || spec.kind == ComPortSpec::Kind::ZiFiNative) && _espFirmware->count())
        spec.firmware = static_cast<uint8_t>(_espFirmware->currentData().toUInt());
    if (spec.kind == ComPortSpec::Kind::Modem)
        spec.port = static_cast<uint16_t>(_modemPort->value());
    return spec;
}

void SerialPeerEditor::setDevices(const QStringList& devices)
{
    if (devices == _devices)
        return;
    _devices = devices;
    const QSignalBlocker block(_device);
    const QString typed = _device->currentText();
    _device->clear();
    _device->addItems(devices);
    _device->setCurrentText(typed);
}

void SerialPeerEditor::updateFields()
{
    _tcpRow->setVisible(_kind->currentIndex() == PeerTcp);
    _serialRow->setVisible(_kind->currentIndex() == PeerSerial);
    const int kind = _kind->currentIndex();
    _espRow->setVisible(kind == PeerEspnet || kind == PeerAt || kind == PeerZiFiNative);
    if (kind != _firmwareKind)
    {
        // The builds this kind of module can run (ComPortSpec::firmware values)
        const QSignalBlocker block(_espFirmware);
        _espFirmware->clear();
        if (kind == PeerAt)
        {
            _espFirmware->addItem(tr("Default (EspChip, or the board's chip)"), static_cast<uint>(ComPortSpec::kDefaultFirmware));
            _espFirmware->addItem(tr("ESP32, AT 2.2.0"), 0u);
            _espFirmware->addItem(tr("ESP8266, NonOS AT 1.7.4"), 1u);
            _espFirmware->addItem(tr("ESP8266, ESP-AT 2.2.1 (the Sprinter kit's 2.2.1)"), 2u);
            _espFirmware->addItem(tr("ESP8266, ESP-AT 2.2.2"), 3u);
        }
        else if (kind == PeerZiFiNative)
        {
            _espFirmware->addItem(tr("ESP32-S3-Zero, s3-native-0.6.94"), 0u);
            _espFirmware->addItem(tr("ESP-01S, native-0.2.2"), 1u);
        }
        _espFirmware->setVisible(_espFirmware->count() > 0);
        _firmwareKind = kind;
    }
    _modemRow->setVisible(_kind->currentIndex() == PeerModem);
}

/// endregion </SerialPeerEditor>

NetworkWindow::NetworkWindow(QWidget* parent) : QWidget(parent)
{
    setWindowTitle(tr("Network"));
    buildUi();

    // Twice a second: the state copy is made on the machine thread at frame boundaries
    _timer = new QTimer(this);
    _timer->setInterval(500);
    connect(_timer, &QTimer::timeout, this, &NetworkWindow::refresh);
}

void NetworkWindow::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    _machine = new QLabel(this);
    _machine->setWordWrap(true);
    layout->addWidget(_machine);

    auto* tabs = new QTabWidget(this);
    layout->addWidget(tabs, 1);

    // --- Settings ---
    auto* scroll = new QScrollArea(tabs);
    scroll->setWidgetResizable(true);
    _settingsPage = new QWidget(scroll);
    auto* page = new QVBoxLayout(_settingsPage);
    scroll->setWidget(_settingsPage);
    tabs->addTab(scroll, tr("Settings"));

    auto why = [this](QWidget* parentWidget) {
        auto* label = new QLabel(parentWidget);
        label->setWordWrap(true);
        label->setStyleSheet(QStringLiteral("color: palette(mid);"));
        return label;
    };

    // Expansion slots (the Sprinter's ISA slots): fitted when the machine is created, from [ISA] in its config
    _slotsBox = new QGroupBox(tr("Expansion slots"), _settingsPage);
    auto* slotsLayout = new QVBoxLayout(_slotsBox);
    _slots = new QLabel(_slotsBox);
    _slots->setWordWrap(true);
    _slots->setTextInteractionFlags(Qt::TextSelectableByMouse);
    _slots->setToolTip(tr("The population comes from [ISA] Slot1= / Slot2= of the machine config (or the create "
                          "options) and stays for the instance's lifetime; the Status tab shows each card's registers"));
    slotsLayout->addWidget(_slots);
    for (int n = 0; n < 2; ++n)
    {
        // A UART card's line (SprinterESP: AT = its own ESP-12F; LOOPBACK, TCP, a real ESP on a USB adapter)
        _slotPeerRow[n] = new QWidget(_slotsBox);
        auto* rowForm = new QFormLayout(_slotPeerRow[n]);
        rowForm->setContentsMargins(0, 0, 0, 0);
        _slotPeer[n] = new SerialPeerEditor(_slotPeerRow[n]);
        _slotPeerLabel[n] = new QLabel(tr("Slot %1: its UART is wired to").arg(n + 1), _slotPeerRow[n]);
        rowForm->addRow(_slotPeerLabel[n], _slotPeer[n]);
        _slotPeerRow[n]->setVisible(false);
        slotsLayout->addWidget(_slotPeerRow[n]);
        // SprinterSerial's COM2 (#2F8, the DB-9): an external modem (MODEM), a host endpoint, a real port
        _slotPeerRowB[n] = new QWidget(_slotsBox);
        auto* rowFormB = new QFormLayout(_slotPeerRowB[n]);
        rowFormB->setContentsMargins(0, 0, 0, 0);
        _slotPeerB[n] = new SerialPeerEditor(_slotPeerRowB[n]);
        rowFormB->addRow(tr("Slot %1 SprinterSerial COM2 (#2F8, DB-9) is wired to").arg(n + 1), _slotPeerB[n]);
        _slotPeerRowB[n]->setVisible(false);
        slotsLayout->addWidget(_slotPeerRowB[n]);
    }
    _slotsBox->setVisible(false);
    page->addWidget(_slotsBox);

    // ZX-Bus cards
    auto* cards = new QGroupBox(tr("ZX-Bus cards"), _settingsPage);
    auto* cardsLayout = new QVBoxLayout(cards);
    _cardsWhy = why(cards);
    cardsLayout->addWidget(_cardsWhy);
    _zxNetUsb = new QCheckBox(tr("Ethernet: WIZnet W5300 - ZXNETUSB card (NedoPC; ports #xxAB)"), cards);
    _zxNetUsb->setToolTip(tr("NedoOS's W5300 kernel (sd_boot.$C) talks to this card; the ESP kernel (sd_bootesp.$C) uses the serial port"));
    cardsLayout->addWidget(_zxNetUsb);
    _zxWifi = new QCheckBox(tr("Wi-Fi: ZX-WiFi card (16550 + ESP module; ports #F8EF..#FFEF)"), cards);
    cardsLayout->addWidget(_zxWifi);
    _zxWifiWhy = why(cards);
    cardsLayout->addWidget(_zxWifiWhy);
    auto* wifiPeer = new QFormLayout();
    _zxWifiPeer = new SerialPeerEditor(cards);
    wifiPeer->addRow(tr("Its 16550 is wired to"), _zxWifiPeer);
    cardsLayout->addLayout(wifiPeer);
    page->addWidget(cards);

    // ATM2IOESP: not a ZX-Bus card - the ATM Turbo 2+ INTERNAL I/O connector
    auto* ioCard = new QGroupBox(tr("ATM Turbo 2+ INTERNAL I/O connector"), _settingsPage);
    auto* ioLayout = new QVBoxLayout(ioCard);
    _atm2IoEsp = new QCheckBox(tr("Wi-Fi: ATM2IOESP card (TL16C550C + ESP32; bus address by #FB, data by #FA)"), ioCard);
    _atm2IoEsp->setToolTip(tr("NedoOS: espcom.ini comType = 3, the registers at 0xF0..0xF7 (0xF8..0xFF on Rev 1.0)"));
    ioLayout->addWidget(_atm2IoEsp);
    _atm2IoEspWhy = why(ioCard);
    ioLayout->addWidget(_atm2IoEspWhy);
    auto* ioForm = new QFormLayout();
    _atm2IoEspPeer = new SerialPeerEditor(ioCard);
    ioForm->addRow(tr("Its 16550 is wired to"), _atm2IoEspPeer);
    _atm2IoEspAddress = new QComboBox(ioCard);
    _atm2IoEspAddress->addItem(tr("#F0..#F7 (Rev 1.5 / 2.0, default)"), 0xF0u);
    _atm2IoEspAddress->addItem(tr("#F8..#FF (Rev 1.0)"), 0xF8u);
    ioForm->addRow(tr("Bus address"), _atm2IoEspAddress);
    ioLayout->addLayout(ioForm);
    page->addWidget(ioCard);

    // ZiFi: the TS AVR firmware's own UART to an ESP module (TS-Conf; a ZX-Evo with a TS-Labs firmware)
    auto* zifi = new QGroupBox(tr("ZiFi (TS-Labs AVR firmware: registers #C0EF..#C9EF, data #00EF..#BFEF)"), _settingsPage);
    auto* zifiLayout = new QVBoxLayout(zifi);
    _zifiWhy = why(zifi);
    zifiLayout->addWidget(_zifiWhy);
    auto* zifiForm = new QFormLayout();
    _zifiPeer = new SerialPeerEditor(zifi);
    _zifiPeer->setToolTip(tr("None: no ZiFi board. AT: the original board, an ESP-01 (ESP8266, 1 MB) with Espressif's "
                             "AT firmware - NonOS 1.7.4 unless chosen (HackerVBI zifi.spg); ESP-AT 2.2.2 for a "
                             "Sprinter ESP Network Kit port. ZiFi native: the 2026 firmwares (the new zifi.spg, the "
                             "Wild Commander plugins). 115200, no flow control: the AVR's rings hold 511 bytes in, 255 out"));
    zifiForm->addRow(tr("Its UART is wired to"), _zifiPeer);
    zifiLayout->addLayout(zifiForm);
    page->addWidget(zifi);

    // The machine's own serial port
    auto* com = new QGroupBox(tr("Machine serial port"), _settingsPage);
    auto* comLayout = new QVBoxLayout(com);
    _comPortWhy = why(com);
    comLayout->addWidget(_comPortWhy);
    auto* comForm = new QFormLayout();
    _comPort = new SerialPeerEditor(com);
    comForm->addRow(tr("Connected to"), _comPort);
    _avrFirmware = new QComboBox(com);
    for (const auto& [name, text] : NetworkAvrFirmwareChoices())
    {
        _avrFirmware->addItem(Q(name) + QStringLiteral(" - ") + Q(text), Q(name));
        _avrFirmware->setItemData(_avrFirmware->count() - 1, Q(text), Qt::ToolTipRole);
    }
    _avrFirmware->setToolTip(tr("The ZX-Evo AVR firmware the COM port behaves like ([EVO] Avr=); a change restarts its UART"));
    comForm->addRow(tr("AVR firmware"), _avrFirmware);
    _kbcFirmware = new QComboBox(com);
    for (const auto& [name, text] : NetworkKbcFirmwareChoices())
    {
        _kbcFirmware->addItem(Q(name) + QStringLiteral(" - ") + Q(text), Q(name));
        _kbcFirmware->setItemData(_kbcFirmware->count() - 1, Q(text), Qt::ToolTipRole);
    }
    _kbcFirmware->setToolTip(tr("The ATM Turbo 2+ keyboard controller firmware ([ATM] Kbc=); its RS-232 is the "
                                "machine's serial port from V31 on; a change fits a new controller, which boots afresh"));
    comForm->addRow(tr("Keyboard controller"), _kbcFirmware);
    comLayout->addLayout(comForm);
    _avrWhy = why(com);
    comLayout->addWidget(_avrWhy);
    _kbcWhy = why(com);
    comLayout->addWidget(_kbcWhy);
    page->addWidget(com);

    // ESP module and serial line options
    auto* esp = new QGroupBox(tr("ESP module and serial line"), _settingsPage);
    auto* espForm = new QFormLayout(esp);
    _espChip = new QComboBox(esp);
    _espChip->addItem(tr("ESP32 (8 sockets, AT 2.x)"), QStringLiteral("ESP32"));
    _espChip->addItem(tr("ESP8266 (4 sockets, AT NonOS 1.7)"), QStringLiteral("ESP8266"));
    _espChip->addItem(tr("ESP8266, ESP-AT 2.2.1"), QStringLiteral("ESP8266-AT221"));
    _espChip->addItem(tr("ESP8266, ESP-AT 2.2.2 (Sprinter ESP Network Kit)"), QStringLiteral("ESP8266-AT222"));
    _espChip->setToolTip(tr("The firmware of an emulated ESP module. The SprinterESP card's ESP-12F is an ESP8266: it "
                            "takes an ESP8266 entry from here, ESP-AT 2.2.2 otherwise"));
    espForm->addRow(tr("Emulated chip"), _espChip);
    _modemLines = new QCheckBox(tr("Pass RTS / DTR to a host serial device, report CTS / DSR / RI / DCD"), esp);
    _modemLines->setToolTip(tr("Off by default: USB ESP boards wire RTS / DTR to reset / boot"));
    espForm->addRow(QString(), _modemLines);
    page->addWidget(esp);

    // Virtual network
    auto* net = new QGroupBox(tr("Virtual network (10.0.2.0/24)"), _settingsPage);
    auto* netForm = new QFormLayout(net);
    _hostAccess = new QCheckBox(tr("Reach the host network (off: DHCP, DNS hosts table and guest servers only)"), net);
    netForm->addRow(QString(), _hostAccess);
    _dnsMode = new QComboBox(net);
    _dnsMode->addItem(tr("Answered by the host resolver"), QStringLiteral("HOST"));
    _dnsMode->addItem(tr("Passed as plain UDP"), QStringLiteral("PASS"));
    netForm->addRow(tr("DNS"), _dnsMode);
    _hosts = new QLineEdit(net);
    _hosts->setPlaceholderText(tr("name=a.b.c.d,name=a.b.c.d"));
    netForm->addRow(tr("Hosts"), _hosts);
    _forwards = new QLineEdit(net);
    _forwards->setPlaceholderText(tr("tcp:<host port>:<guest port>,..."));
    netForm->addRow(tr("Guest servers"), _forwards);
    _remoteAccess = new QCheckBox(tr("Allow remote access (listen on all interfaces)"), net);
    _remoteAccess->setToolTip(tr("On: the guest servers (Forward= rules, an FTP / SMB / web server a ZX program runs) listen "
                                 "on every network interface of this computer (0.0.0.0), so other computers on your LAN - "
                                 "and anyone who can reach this computer - can connect to them. The emulated servers have "
                                 "no real security: turn this off on an untrusted network.\n"
                                 "Off: they listen on 127.0.0.1 only, reachable from this computer alone.\n"
                                 "A change moves the listeners at once; open connections stay."));
    netForm->addRow(QString(), _remoteAccess);
    _modemPhonebook = new QLineEdit(net);
    _modemPhonebook->setPlaceholderText(tr("5551234=bbs.example.org:23,5550000=10.0.2.2:2323"));
    _modemPhonebook->setToolTip(tr("Numbers a Hayes modem (peer MODEM, any serial port) dials with ATDT: "
                                   "<number>=<host>[:<port>]; letters, '.' or ':' in a dialed number make it a host name"));
    netForm->addRow(tr("Modem phone book"), _modemPhonebook);
    _timeout = new QSpinBox(net);
    _timeout->setRange(500, 120000);
    _timeout->setSingleStep(500);
    _timeout->setSuffix(tr(" ms"));
    netForm->addRow(tr("Connect timeout"), _timeout);
    page->addWidget(net);

    // The frame-level cards (NE2000, 3C509B in ISA slots): the gateway's NAT, or their frames on a host adapter
    auto* bridge = new QGroupBox(tr("Ethernet cards (NE2000, 3C509B)"), _settingsPage);
    auto* bridgeForm = new QFormLayout(bridge);
    _ethernetMode = new QComboBox(bridge);
    _ethernetMode->addItem(tr("NAT through the emulator's router (10.0.2.2; no admin rights)"), QStringLiteral("NAT"));
    _ethernetMode->addItem(tr("Bridge to a host adapter (the card on the real LAN)"), QStringLiteral("BRIDGE"));
    bridgeForm->addRow(tr("Path"), _ethernetMode);
    _bridgeAdapter = new QComboBox(bridge);
    _bridgeAdapter->setEditable(true);
    _bridgeAdapter->setToolTip(tr("The host adapter for the bridge: a wired adapter (Wi-Fi is not bridgeable yet)"));
    bridgeForm->addRow(tr("Host adapter"), _bridgeAdapter);
    _bridgeNote = new QLabel(bridge);
    _bridgeNote->setWordWrap(true);
    bridgeForm->addRow(QString(), _bridgeNote);
    page->addWidget(bridge);
    page->addStretch(1);

    // --- Status ---
    auto* status = new QWidget(tabs);
    auto* statusLayout = new QVBoxLayout(status);
    _notFitted = new QLabel(status);
    _notFitted->setWordWrap(true);
    statusLayout->addWidget(_notFitted);
    _tree = new QTreeWidget(status);
    _tree->setColumnCount(2);
    _tree->setHeaderLabels({tr("Item"), tr("Value")});
    _tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    statusLayout->addWidget(_tree, 1);
    tabs->addTab(status, tr("Status"));
    connect(_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) { _expanded.insert(item->data(0, Qt::UserRole).toString()); });
    connect(_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) { _expanded.remove(item->data(0, Qt::UserRole).toString()); });
    _expanded = {QStringLiteral("/com_port"), QStringLiteral("/card"), QStringLiteral("/virtual_network")};

    // --- Buttons ---
    auto* buttons = new QHBoxLayout();
    _message = new QLabel(this);
    _message->setWordWrap(true);
    buttons->addWidget(_message, 1);
    _revert = new QPushButton(tr("Revert"), this);
    _revert->setToolTip(tr("Show the settings in force again"));
    _apply = new QPushButton(tr("Apply"), this);
    _apply->setToolTip(tr("Applied at the next frame: the devices are fitted again, every connection closes"));
    buttons->addWidget(_revert);
    buttons->addWidget(_apply);
    layout->addLayout(buttons);

    connect(_apply, &QPushButton::clicked, this, &NetworkWindow::onApply);
    connect(_revert, &QPushButton::clicked, this, &NetworkWindow::onRevert);
    for (QCheckBox* box : {_zxNetUsb, _zxWifi, _atm2IoEsp, _modemLines, _hostAccess, _remoteAccess})
        connect(box, &QCheckBox::toggled, this, &NetworkWindow::onEdited);
    for (QComboBox* combo : {_avrFirmware, _kbcFirmware, _atm2IoEspAddress, _espChip, _dnsMode, _ethernetMode})
        connect(combo, &QComboBox::currentIndexChanged, this, &NetworkWindow::onEdited);
    connect(_bridgeAdapter, &QComboBox::currentTextChanged, this, &NetworkWindow::onEdited);
    for (QLineEdit* edit : {_hosts, _forwards, _modemPhonebook})
        connect(edit, &QLineEdit::textEdited, this, &NetworkWindow::onEdited);
    connect(_timeout, &QSpinBox::valueChanged, this, &NetworkWindow::onEdited);
    connect(_comPort, &SerialPeerEditor::edited, this, &NetworkWindow::onEdited);
    connect(_zxWifiPeer, &SerialPeerEditor::edited, this, &NetworkWindow::onEdited);
    connect(_atm2IoEspPeer, &SerialPeerEditor::edited, this, &NetworkWindow::onEdited);
    for (SerialPeerEditor* editor : _slotPeer)
        connect(editor, &SerialPeerEditor::edited, this, &NetworkWindow::onEdited);
    for (SerialPeerEditor* editor : _slotPeerB)
        connect(editor, &SerialPeerEditor::edited, this, &NetworkWindow::onEdited);
    connect(_zifiPeer, &SerialPeerEditor::edited, this, &NetworkWindow::onEdited);
}

void NetworkWindow::setBinding(EmulatorBinding* binding)
{
    if (_binding)
        disconnect(_binding, nullptr, this, nullptr);
    _binding = binding;
    if (_binding)
    {
        connect(_binding, &EmulatorBinding::bound, this, [this] { _dirty = false; refresh(); });
        connect(_binding, &EmulatorBinding::unbound, this, [this] { _dirty = false; refresh(); });
    }
    refresh();
}

void NetworkWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    fillBridgeAdapters();
    refresh();
    _timer->start();
    emit visibilityChanged(true);
}

void NetworkWindow::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    _timer->stop();
    emit visibilityChanged(false);
}

void NetworkWindow::refresh()
{
    Emulator* emulator = _binding && _binding->isBound() ? _binding->emulator() : nullptr;
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    NetworkManager* manager = (context && context->pCore) ? context->pCore->GetNetworkManager() : nullptr;
    _settingsPage->setEnabled(manager != nullptr);
    _apply->setEnabled(manager != nullptr && _dirty);
    _revert->setEnabled(manager != nullptr && _dirty);
    if (!manager)
    {
        _machine->setText(tr("No machine."));
        _tree->clear();
        _notFitted->clear();
        return;
    }

    const StateNode network = DeviceState::Network(context);
    const NetworkForm form = NetworkFormFromState(network);

    const QString serial = form.serialPort == "evo-avr"      ? tr("the ZX-Evo AVR's 16550 (#F8EF..#FFEF)")
                           : form.serialPort == "zifi"       ? tr("the TS AVR's 16550 (#F8EF..#FFEF) and ZiFi")
                           : form.serialPort == "atm2-kbc"   ? tr("the keyboard controller's RS-232 (IN #FE commands)")
                           : form.serialPort == "profi-8251" ? tr("the 8251 COM port (#D3 / #F3, 8253 baud timer)")
                                                             : tr("none");
    _machine->setText(tr("This machine: ZX-Bus %1; its own serial port: %2.")
                          .arg(form.zxBus ? tr("yes") : tr("no"), serial));

    QStringList devices;
    if (const StateNode* list = network.find("host_serial_devices"))
    {
        for (const StateNode& d : list->items)
            devices << Q(d.s);
    }
    _comPort->setDevices(devices);
    _zxWifiPeer->setDevices(devices);
    _atm2IoEspPeer->setDevices(devices);
    for (SerialPeerEditor* editor : _slotPeer)
        editor->setDevices(devices);
    for (SerialPeerEditor* editor : _slotPeerB)
        editor->setDevices(devices);
    _zifiPeer->setDevices(devices);

    if (!_dirty)
    {
        _applied = form;
        loadForm(form);
    }
    else
    {
        _applied.zxBus = form.zxBus;
        _applied.serialPort = form.serialPort;
        _applied.kbcFirmware = form.kbcFirmware;
        _applied.internalIo = form.internalIo;
        _applied.zifiMachine = form.zifiMachine;
        for (int n = 0; n < 2; ++n)
        {
            _applied.slotUart[n] = form.slotUart[n];
            _applied.slotCard[n] = form.slotCard[n];
            _applied.slotUartB[n] = form.slotUartB[n];
            _slotPeerRow[n]->setVisible(form.slotUart[n]);
            _slotPeerRowB[n]->setVisible(form.slotUartB[n]);
        }
    }
    updateAvailability();
    updateStatusTree(network);

    const StateNode isa = DeviceState::Isa(context);
    const StateNode* isaAvailable = isa.find("available");
    const std::vector<NetworkSlotRow> rows =
        NetworkSlotRows(network, isaAvailable && isaAvailable->b ? &isa : nullptr);
    QStringList lines;
    for (const NetworkSlotRow& row : rows)
        lines << QStringLiteral("<b>%1</b>: %2").arg(Q(row.label).toHtmlEscaped(), Q(row.line).toHtmlEscaped());
    _slots->setText(lines.join(QStringLiteral("<br>")));
    _slotsBox->setVisible(!rows.empty());
}

void NetworkWindow::loadForm(const NetworkForm& form)
{
    _loading = true;
    _zxNetUsb->setChecked(form.zxNetUsb);
    _zxWifi->setChecked(form.zxWifi);
    _zxWifiPeer->setSpec(form.zxWifiPeer);
    _atm2IoEsp->setChecked(form.atm2IoEsp);
    _atm2IoEspPeer->setSpec(form.atm2IoEspPeer);
    for (int n = 0; n < 2; ++n)
    {
        _slotPeer[n]->setSpec(form.slotPeer[n]);
        _slotPeerRow[n]->setVisible(form.slotUart[n]);
        const QString card = form.slotCard[n] == "modem"       ? tr("Slot %1 ISA modem: its 16550A is wired to (MODEM = the card's own modem)")
                             : form.slotCard[n] == "dual16552" ? tr("Slot %1 SprinterSerial COM1 (#3F8, USB) is wired to")
                                                               : tr("Slot %1 SprinterESP: its 16550 is wired to");
        _slotPeerLabel[n]->setText(card.arg(n + 1));
        _slotPeerB[n]->setSpec(form.slotPeerB[n]);
        _slotPeerRowB[n]->setVisible(form.slotUartB[n]);
    }
    if (_modemPhonebook->text() != Q(form.modemPhonebook))
        _modemPhonebook->setText(Q(form.modemPhonebook));
    _atm2IoEspAddress->setCurrentIndex(std::max(0, _atm2IoEspAddress->findData(form.atm2IoEspAddress)));
    _zifiPeer->setSpec(form.zifiPeer);
    _comPort->setSpec(form.comPort);
    _avrFirmware->setCurrentIndex(std::max(0, _avrFirmware->findData(Q(form.avrFirmware))));
    _kbcFirmware->setCurrentIndex(std::max(0, _kbcFirmware->findData(Q(form.kbcFirmware.empty() ? "V41" : form.kbcFirmware))));
    _espChip->setCurrentIndex(std::max(0, _espChip->findData(Q(form.espChip))));
    _modemLines->setChecked(form.modemLines);
    _hostAccess->setChecked(form.hostAccess);
    _dnsMode->setCurrentIndex(std::max(0, _dnsMode->findData(Q(form.dnsMode))));
    if (_hosts->text() != Q(form.hosts))
        _hosts->setText(Q(form.hosts));
    if (_forwards->text() != Q(form.forwards))
        _forwards->setText(Q(form.forwards));
    _remoteAccess->setChecked(form.remoteAccess);
    _timeout->setValue(static_cast<int>(form.connectTimeoutMs));
    _ethernetMode->setCurrentIndex(std::max(0, _ethernetMode->findData(Q(form.ethernetMode))));
    if (_bridgeAdapter->currentText() != Q(form.bridgeAdapter))
        _bridgeAdapter->setCurrentText(Q(form.bridgeAdapter));
    _loading = false;
}

void NetworkWindow::fillBridgeAdapters()
{
    // The packet library lists the adapters (loaded at run time: missing on a host without libpcap / Npcap)
    const StateNode report = EthernetAccess::Adapters();
    const QString current = _bridgeAdapter->currentText();
    const bool loading = _loading;
    _loading = true;
    _bridgeAdapter->clear();
    if (const StateNode* adapters = report.find("adapters"))
    {
        for (const StateNode& a : adapters->items)
        {
            QString label = Q(a.find("name")->s);
            QStringList ips;
            for (const StateNode& ip : a.find("ipv4")->items)
                ips << Q(ip.s);
            if (!ips.isEmpty())
                label += QStringLiteral(" (") + ips.join(QStringLiteral(", ")) + QStringLiteral(")");
            if (a.find("wireless")->b)
                label += tr(" - Wi-Fi, not bridgeable yet");
            else if (a.find("loopback")->b)
                label += tr(" - loopback");
            _bridgeAdapter->addItem(label, Q(a.find("name")->s));
        }
    }
    const int index = _bridgeAdapter->findData(current);
    if (index >= 0)
        _bridgeAdapter->setCurrentIndex(index);
    else
        _bridgeAdapter->setCurrentText(current);
    _loading = loading;
    const std::string error = report.find("error") ? report.find("error")->s : std::string();
    const std::string library = report.find("library") ? report.find("library")->s : std::string();
    _bridgeNote->setText(error.empty() ? tr("Packet library: %1").arg(Q(library)) : Q(error));
}

NetworkForm NetworkWindow::readForm() const
{
    NetworkForm form = _applied;
    form.zxNetUsb = _zxNetUsb->isChecked();
    form.zxWifi = _zxWifi->isChecked();
    form.zxWifiPeer = _zxWifiPeer->spec();
    form.atm2IoEsp = _atm2IoEsp->isChecked();
    form.atm2IoEspPeer = _atm2IoEspPeer->spec();
    for (int n = 0; n < 2; ++n)
    {
        if (form.slotUart[n])
            form.slotPeer[n] = _slotPeer[n]->spec();
        if (form.slotUartB[n])
            form.slotPeerB[n] = _slotPeerB[n]->spec();
    }
    form.modemPhonebook = S(_modemPhonebook->text());
    form.atm2IoEspAddress = _atm2IoEspAddress->currentData().toUInt();
    form.zifiPeer = _zifiPeer->spec();
    form.comPort = _comPort->spec();
    form.avrFirmware = S(_avrFirmware->currentData().toString());
    if (!form.kbcFirmware.empty())   // only where the board has the socket
        form.kbcFirmware = S(_kbcFirmware->currentData().toString());
    form.espChip = S(_espChip->currentData().toString());
    form.modemLines = _modemLines->isChecked();
    form.hostAccess = _hostAccess->isChecked();
    form.dnsMode = S(_dnsMode->currentData().toString());
    form.hosts = S(_hosts->text());
    form.forwards = S(_forwards->text());
    form.remoteAccess = _remoteAccess->isChecked();
    form.connectTimeoutMs = static_cast<unsigned>(_timeout->value());
    form.ethernetMode = S(_ethernetMode->currentData().toString());
    // The list shows "en0 (192.168.1.5)": the adapter is the item's data; a typed name is taken as it is
    const int adapter = _bridgeAdapter->findText(_bridgeAdapter->currentText());
    form.bridgeAdapter = adapter >= 0 && _bridgeAdapter->itemData(adapter).isValid()
                             ? S(_bridgeAdapter->itemData(adapter).toString())
                             : S(_bridgeAdapter->currentText());
    return form;
}

void NetworkWindow::updateAvailability()
{
    const NetworkForm form = readForm();
    const NetworkAvailability a = NetworkFormAvailability(form);
    _zxNetUsb->setEnabled(a.cards);
    _cardsWhy->setText(Q(a.cardsWhy));
    _cardsWhy->setVisible(!a.cards);
    // A card the machine cannot take stays visible (and checked if the config
    // asks for it), with the reason: the status lists it under not_fitted
    _zxWifi->setEnabled(a.zxWifi || form.zxWifi);
    _zxWifiWhy->setText(Q(a.zxWifiWhy));
    _zxWifiWhy->setVisible(!a.zxWifi);
    _zxWifiPeer->setEnabled(a.zxWifi && form.zxWifi);
    _atm2IoEsp->setEnabled(a.atm2IoEsp || form.atm2IoEsp);
    _atm2IoEspWhy->setText(Q(a.atm2IoEspWhy));
    _atm2IoEspWhy->setVisible(!a.atm2IoEsp);
    _atm2IoEspPeer->setEnabled(a.atm2IoEsp && form.atm2IoEsp);
    _atm2IoEspAddress->setEnabled(a.atm2IoEsp && form.atm2IoEsp);
    _zifiPeer->setEnabled(a.zifi);
    _zifiWhy->setText(Q(a.zifiWhy));
    _zifiWhy->setVisible(!a.zifi);
    _comPort->setEnabled(a.comPort);
    _comPortWhy->setText(Q(a.comPortWhy));
    _comPortWhy->setVisible(!a.comPort);
    _avrFirmware->setEnabled(a.avrFirmware);
    _avrWhy->setText(Q(a.avrFirmwareWhy));
    _avrWhy->setVisible(!a.avrFirmware);
    _kbcFirmware->setEnabled(a.kbcFirmware);
    _kbcWhy->setText(Q(a.kbcFirmwareWhy));
    _kbcWhy->setVisible(!a.kbcFirmware && a.avrFirmware);   // one "only the X has" line is enough
    if (!a.kbcFirmware && !a.avrFirmware)
        _avrWhy->setText(Q(a.avrFirmwareWhy + " " + a.kbcFirmwareWhy));
    const bool esp = NetworkPeerIsEsp(form.comPort) || (form.zxWifi && NetworkPeerIsEsp(form.zxWifiPeer)) ||
                     (form.atm2IoEsp && NetworkPeerIsEsp(form.atm2IoEspPeer)) || NetworkPeerIsEsp(form.zifiPeer) ||
                     (form.slotUart[0] && NetworkPeerIsEsp(form.slotPeer[0])) ||
                     (form.slotUart[1] && NetworkPeerIsEsp(form.slotPeer[1]));
    _espChip->setEnabled(esp);
    const bool serial = form.comPort.kind == ComPortSpec::Kind::Serial ||
                        (form.zxWifi && form.zxWifiPeer.kind == ComPortSpec::Kind::Serial) ||
                        (form.atm2IoEsp && form.atm2IoEspPeer.kind == ComPortSpec::Kind::Serial) ||
                        form.zifiPeer.kind == ComPortSpec::Kind::Serial;
    _modemLines->setEnabled(serial);
}

void NetworkWindow::onEdited()
{
    if (_loading)
        return;
    _dirty = !NetworkFormChanges(_applied, readForm()).empty();
    _apply->setEnabled(_dirty);
    _revert->setEnabled(_dirty);
    _message->clear();
    updateAvailability();
}

void NetworkWindow::setController(SlotChangeController* controller)
{
    _controller = controller;
}

void NetworkWindow::onApply()
{
    Emulator* emulator = _binding && _binding->isBound() ? _binding->emulator() : nullptr;
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    NetworkManager* manager = (context && context->pCore) ? context->pCore->GetNetworkManager() : nullptr;
    if (!manager)
        return;
    const NetworkFormApply split = NetworkFormSplitCards(_applied, readForm());
    if (!split.cardChange && split.rest.empty())
        return;
    NetworkManager::Change change;
    std::string error;
    if (!NetworkManager::ParseChange(split.rest, change, error) || !NetworkManager::ValidateChange(change, error))
    {
        _message->setText(tr("Not applied: %1").arg(Q(error)));
        return;
    }
    if (split.cardChange)
    {
        // The ZX-bus cards are slots (owner decision Q11): the slot change's confirmation, a restart, the removed cards
        // named with Undo; the other settings then go to the restarted machine
        if (!_controller)
        {
            _message->setText(tr("Not applied: the network cards change through Machine > Slots"));
            return;
        }
        const std::string id = emulator->GetId();
        manager = nullptr;
        context = nullptr;
        emulator = nullptr;
        if (!_controller->ApplyNetworkCards(id, split.zxBusCards, this))
        {
            _message->setText(tr("Not applied: the network cards did not change"));
            return;
        }
        std::shared_ptr<Emulator> restarted = EmulatorManager::GetInstance()->GetEmulator(_controller->LastEmulatorId());
        context = restarted ? restarted->GetContext() : nullptr;
        manager = (context && context->pCore) ? context->pCore->GetNetworkManager() : nullptr;
        if (change.Empty())
        {
            _message->setText(tr("Applied: the machine restarted with the new network cards."));
            _dirty = false;
            QTimer::singleShot(100, this, &NetworkWindow::refresh);
            return;
        }
        if (!manager || !manager->RequestChange(change, error))
        {
            _message->setText(tr("The network cards changed (a restart); the other settings were not applied: %1")
                                  .arg(Q(manager ? error : std::string("no machine"))));
            _dirty = false;
            QTimer::singleShot(100, this, &NetworkWindow::refresh);
            return;
        }
        _message->setText(tr("Applied: the machine restarted with the new network cards."));
    }
    else
    {
        if (!manager->RequestChange(change, error))
        {
            _message->setText(tr("Not applied: %1").arg(Q(error)));
            return;
        }
        _message->setText(tr("Applied."));
    }
    _dirty = false;
    // The machine thread applies it at its next frame boundary: the next
    // refresh shows the settings in force
    QTimer::singleShot(100, this, &NetworkWindow::refresh);
}

void NetworkWindow::onRevert()
{
    _dirty = false;
    _message->clear();
    loadForm(_applied);
    refresh();
}

void NetworkWindow::updateStatusTree(const StateNode& network)
{
    QStringList notes;
    if (const StateNode* list = network.find("not_fitted"))
    {
        for (const StateNode& n : list->items)
            notes << Q(n.s);
    }
    _notFitted->setText(notes.isEmpty() ? QString() : tr("Not fitted: %1").arg(notes.join(QStringLiteral("; "))));
    _notFitted->setVisible(!notes.isEmpty());

    const int scroll = _tree->verticalScrollBar()->value();
    _tree->setUpdatesEnabled(false);
    _tree->clear();
    for (const auto& [key, child] : network.members)
    {
        if (key == "settings" || key == "available")
            continue;   // the settings are the other tab
        auto* item = new QTreeWidgetItem(_tree);
        fillTree(item, QStringLiteral("/") + Q(key), child);
        item->setText(0, Q(key));
    }
    _tree->setUpdatesEnabled(true);
    _tree->verticalScrollBar()->setValue(scroll);
}

void NetworkWindow::fillTree(QTreeWidgetItem* item, const QString& path, const StateNode& node)
{
    item->setData(0, Qt::UserRole, path);
    if (node.isObject())
    {
        for (const auto& [key, child] : node.members)
        {
            auto* sub = new QTreeWidgetItem(item);
            sub->setText(0, Q(key));
            fillTree(sub, path + QStringLiteral("/") + Q(key), child);
        }
    }
    else if (node.isArray() && !node.items.empty())
    {
        item->setText(1, Scalar(node));
        for (size_t i = 0; i < node.items.size(); ++i)
        {
            auto* sub = new QTreeWidgetItem(item);
            const StateNode& child = node.items[i];
            sub->setText(0, QStringLiteral("[%1]").arg(i));
            fillTree(sub, path + QStringLiteral("/") + QString::number(i), child);
        }
    }
    else
        item->setText(1, Scalar(node));
    if (_expanded.contains(path))
        item->setExpanded(true);
}
