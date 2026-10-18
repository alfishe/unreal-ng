#include "controllersdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <set>

#include "controllers/controllerpicture.h"
#include "controllers/hostcontrollers.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

#ifdef ENABLE_CONTROLLERS
#include "emulator/io/controllers/controllercontrol.h"
#include "emulator/io/controllers/controllermanager.h"
#include "devicedatabase.h"
#include "router/inputrouter.h"

using namespace controllers;

namespace
{
QString TargetText(const Binding& binding)
{
    const Target& target = binding.target;
    QString text;
    switch (target.kind)
    {
        case TargetKind::None: text = "—"; break;
        case TargetKind::Retro:
            text = QString::fromStdString(target.retro.connector) + ":" + QString::number(target.retro.mask, 16);
            break;
        case TargetKind::Keys: text = QObject::tr("ZX keys"); break;
        case TargetKind::Macro: text = QObject::tr("macro (%1 steps)").arg(target.steps.size()); break;
        case TargetKind::Mouse: text = QObject::tr("mouse"); break;
        case TargetKind::Action: text = QString::fromStdString(target.name); break;
        case TargetKind::Layer: text = QObject::tr("layer %1").arg(QString::fromStdString(target.name)); break;
    }
    if (!binding.options.label.empty())
        text += "  (" + QString::fromStdString(binding.options.label) + ")";
    if (binding.options.turbo != Turbo::Off)
        text += QObject::tr("  turbo %1").arg(TurboName(binding.options.turbo));
    return text;
}

QString MaskText(uint16_t mask)
{
    static const char* const names[] = {"R", "L", "D", "U", "F", "F2", "F3", "F4", "A", "X", "Y", "Z", "M"};
    QStringList held;
    for (int bit = 0; bit < 13; ++bit)
        if (mask & (1u << bit))
            held << names[bit];
    return held.isEmpty() ? QString("·") : held.join(' ');
}

const char* const kTargets[] = {"retro auto:fire",  "retro auto:fire2", "retro auto:fire3", "retro auto:up",
                                "retro auto:down",  "retro auto:left",  "retro auto:right", "retro auto:dpad",
                                "key SPACE",        "key ENTER",        "key CAPS",         "key SYM",
                                "action osd.keyboard", "none"};
}  // namespace
#endif

ControllersDialog::ControllersDialog(HostControllers* host, QWidget* parent) : QWidget(parent, Qt::Window), _host(host)
{
    setWindowTitle(tr("Controllers"));
    resize(1180, 760);

    auto* root = new QVBoxLayout(this);
    _profileLabel = new QLabel(this);
    _profileLabel->setWordWrap(true);
    auto* top = new QHBoxLayout();
    top->addWidget(_profileLabel, 1);
    top->addWidget(new QLabel(tr("Game:"), this));
    _gameProfile = new QComboBox(this);
    _gameProfile->setToolTip(tr("The game profile: the one of the loaded file, or one chosen here for a program "
                                "started from the machine itself (a NEX from NextZXOS's browser)"));
    _gameProfile->addItem(tr("By the loaded file"), QString());
#ifdef ENABLE_CONTROLLERS
    // The shipped games from the index (not parsed until chosen) and the user's own
    std::map<QString, QString> games;  // label -> id, sorted by label
    const auto addGame = [&games](const std::string& id, const std::string& label,
                                  const std::vector<std::string>& machines) {
        QString text = QString::fromStdString(label.empty() ? id : label);
        if (!machines.empty())
            text += " (" + QString::fromStdString(machines.front()) + ")";
        games.emplace(text, QString::fromStdString(id));
    };
    for (const GameEntry& entry : _host->Profiles().Games().Entries())
        addGame(entry.id, entry.label, entry.machines);
    for (const Profile* profile : _host->Profiles().Profiles())
        if (profile->kind == ProfileKind::Play && profile->IsGame() &&
            _host->Profiles().Games().FindId(profile->id) == nullptr)
            addGame(profile->id, profile->label, profile->machines);
    for (const auto& [text, id] : games)
        _gameProfile->addItem(text, id);
    _gameProfile->setCurrentIndex(std::max(0, _gameProfile->findData(QString::fromStdString(_host->ManualProfile()))));
    connect(_gameProfile, &QComboBox::currentIndexChanged, this,
            [this] { _host->SetManualProfile(_gameProfile->currentData().toString().toStdString()); });
#endif
    top->addWidget(_gameProfile);
    _advanced = new QCheckBox(tr("Advanced: debug layer"), this);
    _advanced->setToolTip(tr("The debug profiles take part: a layer key (a paddle, or View + Menu) turns the pad into "
                             "a debugger remote"));
    _advanced->setChecked(_host->DebugLayer());
    top->addWidget(_advanced);
    _soundHaptics = new QCheckBox(tr("Haptics from the sound"), this);
    _soundHaptics->setToolTip(tr("Pads with motors rumble with the AY noise and envelope booms and the beeper's noise "
                                 "bursts, until a game profile brings its own haptics"));
    _soundHaptics->setChecked(_host->SoundHaptics());
    top->addWidget(_soundHaptics);
    root->addLayout(top);

    auto* splitter = new QSplitter(this);
    root->addWidget(splitter, 1);

    auto* left = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(new QLabel(tr("<b>Host devices</b>"), left));
    _devices = new QListWidget(left);
    leftLayout->addWidget(_devices, 1);
    _identifyButton = new QPushButton(tr("Identify"), left);
    _identifyButton->setToolTip(tr("Rumble the selected device"));
    _identifyButton->setEnabled(false);
    leftLayout->addWidget(_identifyButton);
    _probeLabel = new QLabel(left);
    _probeLabel->setWordWrap(true);
    leftLayout->addWidget(_probeLabel);

    auto* centre = new QWidget(splitter);
    auto* centreLayout = new QVBoxLayout(centre);
    centreLayout->setContentsMargins(0, 0, 0, 0);
    _deviceTitle = new QLabel(centre);
    _deviceTitle->setWordWrap(true);
    centreLayout->addWidget(_deviceTitle);
    _picture = new ControllerPicture(centre);
    centreLayout->addWidget(_picture, 2);
    _controls = new QTableWidget(0, 4, centre);
    _controls->setHorizontalHeaderLabels({tr("Control"), tr("Value"), tr("Binding (play)"), tr("Binding (debug)")});
    _controls->setColumnHidden(3, !_host->DebugLayer());
    _controls->horizontalHeader()->setStretchLastSection(true);
    _controls->setSelectionBehavior(QAbstractItemView::SelectRows);
    _controls->setSelectionMode(QAbstractItemView::SingleSelection);
    _controls->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _controls->verticalHeader()->hide();
    centreLayout->addWidget(_controls, 1);
    auto* hint = new QLabel(tr("Press a control on the device to select it, then bind it."), centre);
    centreLayout->addWidget(hint);
    _bindButton = new QPushButton(tr("Bind…"), centre);
    centreLayout->addWidget(_bindButton, 0, Qt::AlignLeft);

    auto* right = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    _machineLabel = new QLabel(right);
    rightLayout->addWidget(_machineLabel);
    _connectors = new QTableWidget(0, 3, right);
    _connectors->setHorizontalHeaderLabels({tr("Connector"), tr("Device"), tr("Held")});
    _connectors->horizontalHeader()->setStretchLastSection(true);
    _connectors->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _connectors->verticalHeader()->hide();
    rightLayout->addWidget(_connectors, 1);
    rightLayout->addWidget(new QLabel(tr("<b>Haptics</b>"), right));
    _hapticsLabel = new QLabel(right);
    _hapticsLabel->setWordWrap(true);
    _hapticsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rightLayout->addWidget(_hapticsLabel);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    splitter->setStretchFactor(2, 1);

    _live = new QTimer(this);
    _live->setInterval(33);  // ui-ux.md §7: the 30 Hz live read, only while visible
    connect(_live, &QTimer::timeout, this, &ControllersDialog::tick);
    connect(_host, &HostControllers::changed, this, &ControllersDialog::rebuild);
    connect(_devices, &QListWidget::currentRowChanged, this, &ControllersDialog::rebuildControls);
    connect(_bindButton, &QPushButton::clicked, this, &ControllersDialog::bindSelected);
#ifdef ENABLE_CONTROLLERS
    connect(_identifyButton, &QPushButton::clicked, this, [this] {
        const std::string device = selectedDevice();
        if (!device.empty())
            _host->Service().Identify(device);
    });
#endif
    connect(_picture, &ControllerPicture::controlClicked, this, &ControllersDialog::selectControl);
    connect(_advanced, &QCheckBox::toggled, this, [this](bool on) {
        _controls->setColumnHidden(3, !on);
        _host->SetDebugLayer(on);
    });
    connect(_soundHaptics, &QCheckBox::toggled, _host, &HostControllers::SetSoundHaptics);
    connect(_controls, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = _controls->currentRow();
        QTableWidgetItem* item = row >= 0 ? _controls->item(row, 0) : nullptr;
        _picture->setSelected(item != nullptr ? item->data(Qt::UserRole).toString().toStdString() : std::string{});
    });
    rebuild();
}

void ControllersDialog::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    rebuild();
    _live->start();
    emit visibilityChanged(true);
}

void ControllersDialog::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    _live->stop();
    emit visibilityChanged(false);
}

std::string ControllersDialog::selectedDevice() const
{
    QListWidgetItem* item = _devices->currentItem();
    return item != nullptr ? item->data(Qt::UserRole).toString().toStdString() : std::string{};
}

std::string ControllersDialog::selectedFamily() const
{
    QListWidgetItem* item = _devices->currentItem();
    return item != nullptr ? item->data(Qt::UserRole + 1).toString().toStdString() : std::string{};
}

std::string ControllersDialog::selectedLayout() const
{
    QListWidgetItem* item = _devices->currentItem();
    return item != nullptr ? item->data(Qt::UserRole + 2).toString().toStdString() : std::string{};
}

void ControllersDialog::selectControl(const QString& control)
{
    for (int row = 0; row < _controls->rowCount(); ++row)
        if (_controls->item(row, 0)->data(Qt::UserRole).toString() == control)
        {
            _controls->selectRow(row);
            return;
        }
}

void ControllersDialog::rebuild()
{
#ifdef ENABLE_CONTROLLERS
    const std::string current = selectedDevice();
    _devices->blockSignals(true);
    _devices->clear();
    int select = -1;
    for (const HostDeviceInfo& device : _host->Service().Router().Devices())
    {
        auto* item = new QListWidgetItem(QString("%1\n%2 · %3")
                                             .arg(QString::fromStdString(device.name),
                                                  QString::fromStdString(device.family),
                                                  QString::fromStdString(device.backend)),
                                         _devices);
        item->setData(Qt::UserRole, QString::fromStdString(device.id));
        item->setData(Qt::UserRole + 1, QString::fromStdString(device.family));
        item->setData(Qt::UserRole + 2, QString::fromStdString(device.layout));
        item->setToolTip(QString::fromStdString(device.id));
        if (device.id == current)
            select = _devices->count() - 1;
    }
    if (select < 0 && _devices->count() > 0)
        select = 0;
    _devices->setCurrentRow(select);
    _devices->blockSignals(false);
    if (_devices->count() == 0)
        new QListWidgetItem(tr("No game controller connected"), _devices);

    QStringList probes;
    for (const BackendProbe& probe : _host->Service().Probe())
        probes << (probe.running ? tr("%1 ok (%2)").arg(QString::fromStdString(probe.name)).arg(probe.devices)
                                 : tr("%1: %2").arg(QString::fromStdString(probe.name),
                                                    QString::fromStdString(probe.reason)));
    _probeLabel->setText(probes.isEmpty() ? tr("No native backend on this OS yet") : probes.join(" · "));

    const ResolvedProfiles& resolved = _host->Resolved();
    QStringList applied;
    for (const std::string& id : resolved.applied)
        applied << QString::fromStdString(id);
    _profileLabel->setText(tr("<b>Profiles</b>  %1%2")
                               .arg(applied.join(" → "),
                                    resolved.warnings.empty()
                                        ? QString()
                                        : tr("  <span style='color:#c60'>%n warning(s)</span>", nullptr,
                                             static_cast<int>(resolved.warnings.size()))));
    rebuildControls();
    rebuildConnectors();
#endif
}

void ControllersDialog::rebuildControls()
{
#ifdef ENABLE_CONTROLLERS
    const std::string device = selectedDevice();
    const std::string family = selectedFamily();
    _controls->setRowCount(0);
    _bindButton->setEnabled(!device.empty());
    _identifyButton->setEnabled(!device.empty() && _host->Service().HasRumble(device));
    QString title = device.empty() ? QString() : QString("<b>%1</b>").arg(QString::fromStdString(device));
#ifdef Q_OS_MACOS
    // research-host-apis.md §2.3a: XboxGamepad.dext cuts the USB report before the paddle byte
    if (family == "xbox-elite-2" && device.rfind("usb:", 0) == 0)
        title += "<br>" + tr("Paddles over USB on macOS arrive as the buttons the controller's profile assigns. "
                             "Connect by Bluetooth to use them separately.");
#endif
    _deviceTitle->setText(title);
    const DeviceDatabase& database = DeviceDatabase::Builtin();
    const DeviceMatch match = database.ForFamily(family, selectedLayout());
    const QString set = device.empty() ? QString() : QString::fromStdString(match.images);
    if (set != _pictureSet)
    {
        _pictureSet = set;
        _picture->load(set);
    }
    if (device.empty())
    {
        _picture->setCaptions({});
        return;
    }

    // The scope the router uses for this device: its own, else its family's, else every device's
    const ResolvedProfiles& resolved = _host->Resolved();
    const std::vector<Binding>* bindings = nullptr;
    for (const std::string& scope : {device, "family:" + family, std::string{}})
    {
        for (const auto& [name, list] : resolved.scopes)
            if (name == scope && bindings == nullptr)
                bindings = &list;
    }
    std::map<std::string, QString> bound;
    std::map<std::string, QString> debugBound;
    if (bindings != nullptr)
        for (const Binding& binding : *bindings)
        {
            if (!binding.layer.empty() && binding.layer != "debug")
                continue;
            std::string source;
            for (const std::string& s : binding.sources)
                source += (source.empty() ? "" : "+") + s;
            QString& text = binding.layer.empty() ? bound[source] : debugBound[source];
            text += (text.isEmpty() ? "" : "; ") + TargetText(binding);
        }
    std::set<std::string> rows;
    for (const auto& [control, value] : _host->Service().Live(device))
        rows.insert(control);
    for (const auto& [control, text] : bound)
        rows.insert(control);
    for (const auto& [control, text] : debugBound)
        rows.insert(control);
    std::map<std::string, QString> captions;
    for (const std::string& control : rows)
    {
        const int row = _controls->rowCount();
        _controls->insertRow(row);
        const std::string glyph = match.glyphs.empty() ? control : database.Glyph(match.glyphs, control);
        auto* name = new QTableWidgetItem(glyph == control ? QString::fromStdString(control)
                                                           : QString("%1 (%2)").arg(QString::fromStdString(control),
                                                                                    QString::fromStdString(glyph)));
        name->setData(Qt::UserRole, QString::fromStdString(control));
        _controls->setItem(row, 0, name);
        _controls->setItem(row, 1, new QTableWidgetItem(""));
        auto it = bound.find(control);
        _controls->setItem(row, 2, new QTableWidgetItem(it != bound.end() ? it->second : QString("—")));
        auto debug = debugBound.find(control);
        _controls->setItem(row, 3, new QTableWidgetItem(debug != debugBound.end() ? debug->second : QString("—")));
    }
    // A stick's hotspot is its x axis: the callout shows the whole stick's binding when the axis has none
    for (const auto& [source, text] : bound)
    {
        captions[source] = text;
        if (source.find('.') == std::string::npos && bound.count(source + ".x") == 0)
            captions[source + ".x"] = text;
    }
    _picture->setCaptions(captions);
#endif
}

void ControllersDialog::rebuildConnectors()
{
#ifdef ENABLE_CONTROLLERS
    Emulator* emulator = _host->BoundEmulator();
    EmulatorContext* context = _host->BoundContext();
    ControllerManager* manager = context != nullptr ? context->pControllerManager : nullptr;
    _rebuildingConnectors = true;
    _connectors->setRowCount(0);
    _machineLabel->setText(emulator != nullptr ? QString("<b>%1</b>").arg(QString::fromStdString(emulator->GetId()))
                                               : tr("No machine"));
    if (manager == nullptr)
    {
        _rebuildingConnectors = false;
        return;
    }
    _machineRevision = manager->Revision();
    for (const Connector& connector : manager->Connectors())
    {
        const int row = _connectors->rowCount();
        _connectors->insertRow(row);
        _connectors->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(connector.id)));
        auto* combo = new QComboBox(_connectors);
        if (!connector.fixed)
            combo->addItem(tr("(empty)"), QString());
        for (RetroDeviceKind kind : connector.accepts)
            combo->addItem(RetroDeviceKindName(kind), QString(RetroDeviceKindName(kind)));
        combo->setCurrentIndex(std::max(0, combo->findData(connector.device == RetroDeviceKind::None
                                                               ? QString()
                                                               : QString(RetroDeviceKindName(connector.device)))));
        combo->setEnabled(!connector.fixed);
        const std::string id = connector.id;
        connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, id](int) {
            if (!_rebuildingConnectors)
                plug(id, combo->currentData().toString());
        });
        _connectors->setCellWidget(row, 1, combo);
        _connectors->setItem(row, 2, new QTableWidgetItem(MaskText(manager->State(static_cast<size_t>(row)))));
    }
    _rebuildingConnectors = false;
#endif
}

void ControllersDialog::plug(const std::string& connector, const QString& device)
{
#ifdef ENABLE_CONTROLLERS
    EmulatorContext* context = _host->BoundContext();
    if (context == nullptr)
        return;
    ControllerControl control(context);
    ControllerRequest request;
    request.verb = device.isEmpty() ? "unplug" : "plug";
    request.selector = connector;
    request.device = device.toStdString();
    request.options["async"] = "";
    const ControllerReply reply = control.Execute(request);
    if (!reply.result.Ok())
        QMessageBox::warning(this, tr("Controllers"), QString::fromStdString(reply.result.message));
    _host->Reapply();
#else
    (void)connector;
    (void)device;
#endif
}

void ControllersDialog::tick()
{
#ifdef ENABLE_CONTROLLERS
    const std::string device = selectedDevice();
    const QString haptics = _host->HapticsStatus(device);
    if (_hapticsLabel->text() != haptics)
        _hapticsLabel->setText(haptics);
    if (!device.empty())
    {
        const std::map<std::string, float> live = _host->Service().Live(device);
        _picture->setLive(live);
        if (static_cast<int>(live.size()) > _controls->rowCount())
            rebuildControls();
        for (int row = 0; row < _controls->rowCount(); ++row)
        {
            const std::string control = _controls->item(row, 0)->data(Qt::UserRole).toString().toStdString();
            auto it = live.find(control);
            const float value = it != live.end() ? it->second : 0.0f;
            QTableWidgetItem* cell = _controls->item(row, 1);
            const QString text = it != live.end() ? QString::number(value, 'f', 2) : QString();
            if (cell->text() != text)
            {
                // press-to-bind: a control that goes from rest to pressed is selected
                const bool was = cell->text().toFloat() >= 0.5f || cell->text().toFloat() <= -0.5f;
                const bool now = value >= 0.5f || value <= -0.5f;
                cell->setText(text);
                if (now && !was)
                    _controls->selectRow(row);
            }
            QFont font = _controls->item(row, 0)->font();
            if (font.bold() != (value >= 0.5f || value <= -0.5f))
            {
                font.setBold(!font.bold());
                _controls->item(row, 0)->setFont(font);
            }
        }
    }
    EmulatorContext* context = _host->BoundContext();
    if (context != nullptr && context->pControllerManager != nullptr)
    {
        ControllerManager* manager = context->pControllerManager;
        if (manager->Revision() != _machineRevision)
            rebuildConnectors();
        for (int row = 0; row < _connectors->rowCount() && static_cast<size_t>(row) < manager->Count(); ++row)
            if (QTableWidgetItem* cell = _connectors->item(row, 2))
            {
                const QString text = MaskText(manager->State(static_cast<size_t>(row)));
                if (cell->text() != text)
                    cell->setText(text);
            }
    }
#endif
}

void ControllersDialog::bindSelected()
{
#ifdef ENABLE_CONTROLLERS
    const int row = _controls->currentRow();
    const std::string family = selectedFamily();
    if (row < 0 || family.empty())
        return;
    const QString control = _controls->item(row, 0)->data(Qt::UserRole).toString();
    QStringList items;
    for (const char* target : kTargets)
        items << target;
    bool ok = false;
    const QString choice = QInputDialog::getItem(this, tr("Bind %1").arg(control),
                                                 tr("Target for every %1 pad (retro <connector>:<control>, key "
                                                    "<ZX key>, action <id>, none):")
                                                     .arg(QString::fromStdString(family)),
                                                 items, 0, true, &ok);
    if (!ok || choice.trimmed().isEmpty())
        return;
    const QString field = choice.section(' ', 0, 0).trimmed();
    const QString value = choice.section(' ', 1).trimmed();
    QString spec;
    if (field == "none")
        spec = R"({"none": true})";
    else
        spec = QString(R"({"%1": "%2"})").arg(field, QString(value).replace('"', "\\\""));
    QString error;
    if (!_host->BindForFamily(family, control.toStdString(), spec.toStdString(), error))
        QMessageBox::warning(this, tr("Bind %1").arg(control), error);
#endif
}
