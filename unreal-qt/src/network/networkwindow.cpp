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
#include "emulator/io/network/networkmanager.h"
#include "emulator/state/devicestate.h"

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
        PeerAt
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
    _kind->addItem(tr("Loopback plug (every byte comes back)"));
    _kind->addItem(tr("TCP: a host endpoint (BBS, test harness)"));
    _kind->addItem(tr("Serial: a host serial device (real ESP, modem)"));
    _kind->addItem(tr("ESP module, ESPNET firmware (NedoOS)"));
    _kind->addItem(tr("ESP module, AT firmware (Espressif)"));
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

    connect(_kind, &QComboBox::currentIndexChanged, this, [this] {
        updateFields();
        emit edited();
    });
    connect(_host, &QLineEdit::textEdited, this, &SerialPeerEditor::edited);
    connect(_port, &QSpinBox::valueChanged, this, &SerialPeerEditor::edited);
    connect(_device, &QComboBox::currentTextChanged, this, &SerialPeerEditor::edited);
    connect(_baud, &QComboBox::currentTextChanged, this, &SerialPeerEditor::edited);
    updateFields();
}

void SerialPeerEditor::setSpec(const ComPortSpec& spec)
{
    const QSignalBlocker b1(_kind), b2(_host), b3(_port), b4(_device), b5(_baud);
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
    _timeout = new QSpinBox(net);
    _timeout->setRange(500, 120000);
    _timeout->setSingleStep(500);
    _timeout->setSuffix(tr(" ms"));
    netForm->addRow(tr("Connect timeout"), _timeout);
    page->addWidget(net);
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
    for (QCheckBox* box : {_zxNetUsb, _zxWifi, _modemLines, _hostAccess})
        connect(box, &QCheckBox::toggled, this, &NetworkWindow::onEdited);
    for (QComboBox* combo : {_avrFirmware, _kbcFirmware, _espChip, _dnsMode})
        connect(combo, &QComboBox::currentIndexChanged, this, &NetworkWindow::onEdited);
    for (QLineEdit* edit : {_hosts, _forwards})
        connect(edit, &QLineEdit::textEdited, this, &NetworkWindow::onEdited);
    connect(_timeout, &QSpinBox::valueChanged, this, &NetworkWindow::onEdited);
    connect(_comPort, &SerialPeerEditor::edited, this, &NetworkWindow::onEdited);
    connect(_zxWifiPeer, &SerialPeerEditor::edited, this, &NetworkWindow::onEdited);
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

    const QString serial = form.serialPort == "evo-avr"    ? tr("the ZX-Evo AVR's 16550 (#F8EF..#FFEF)")
                           : form.serialPort == "zifi"     ? tr("TS-Conf ZiFi (not emulated yet)")
                           : form.serialPort == "atm2-kbc" ? tr("the keyboard controller's RS-232 (IN #FE commands)")
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
    }
    updateAvailability();
    updateStatusTree(network);
}

void NetworkWindow::loadForm(const NetworkForm& form)
{
    _loading = true;
    _zxNetUsb->setChecked(form.zxNetUsb);
    _zxWifi->setChecked(form.zxWifi);
    _zxWifiPeer->setSpec(form.zxWifiPeer);
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
    _timeout->setValue(static_cast<int>(form.connectTimeoutMs));
    _loading = false;
}

NetworkForm NetworkWindow::readForm() const
{
    NetworkForm form = _applied;
    form.zxNetUsb = _zxNetUsb->isChecked();
    form.zxWifi = _zxWifi->isChecked();
    form.zxWifiPeer = _zxWifiPeer->spec();
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
    form.connectTimeoutMs = static_cast<unsigned>(_timeout->value());
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
    const bool esp = NetworkPeerIsEsp(form.comPort) || (form.zxWifi && NetworkPeerIsEsp(form.zxWifiPeer));
    _espChip->setEnabled(esp);
    const bool serial = form.comPort.kind == ComPortSpec::Kind::Serial ||
                        (form.zxWifi && form.zxWifiPeer.kind == ComPortSpec::Kind::Serial);
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

void NetworkWindow::onApply()
{
    Emulator* emulator = _binding && _binding->isBound() ? _binding->emulator() : nullptr;
    EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
    NetworkManager* manager = (context && context->pCore) ? context->pCore->GetNetworkManager() : nullptr;
    if (!manager)
        return;
    const auto settings = NetworkFormChanges(_applied, readForm());
    if (settings.empty())
        return;
    NetworkManager::Change change;
    std::string error;
    if (!NetworkManager::ParseChange(settings, change, error) || !manager->RequestChange(change, error))
    {
        _message->setText(tr("Not applied: %1").arg(Q(error)));
        return;
    }
    _message->setText(tr("Applied."));
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
