#include "controllersdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
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

#include "controllers/bindingtable.h"
#include "controllers/controllerpicture.h"
#include "controllers/hostcontrollers.h"
#include "controllers/profilesdialog.h"
#include "controllers/surfacestable.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

#ifdef ENABLE_CONTROLLERS
#include "devicedatabase.h"
#include "emulator/io/controllers/controllercontrol.h"
#include "emulator/io/controllers/controllermanager.h"
#include "haptics/testeffects.h"
#include "router/inputrouter.h"

using namespace controllers;

namespace
{
QString MaskText(uint16_t mask)
{
    static const char* const names[] = {"R", "L", "D", "U", "F", "F2", "F3", "F4", "A", "X", "Y", "Z", "M"};
    QStringList held;
    for (int bit = 0; bit < 13; ++bit)
        if (mask & (1u << bit))
            held << names[bit];
    return held.isEmpty() ? QString("·") : held.join(' ');
}
}  // namespace
#endif

ControllersDialog::ControllersDialog(HostControllers* host, QWidget* parent) : QWidget(parent, Qt::Window), _host(host)
{
    setWindowTitle(tr("Controllers"));
    resize(1180, 760);

    auto* root = new QVBoxLayout(this);
    _root = root;
    _profileLabel = new QLabel(this);
    _profileLabel->setWordWrap(true);
    auto* top = new QHBoxLayout();
    top->addWidget(_profileLabel, 1);
    top->addWidget(new QLabel(tr("Game:"), this));
    _gameProfile = new QComboBox(this);
    _gameProfile->setToolTip(
        tr("The game profile: the one of the loaded file, or one chosen here for a program "
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
    _advanced = new QCheckBox(tr("Debug layer key"), this);
    _advanced->setToolTip(
        tr("In the debugger window a game pad is always a debugger remote.\n"
           "With this on, it is one in the emulator window too while you hold its layer key:\n"
           "the P4 paddle on an Xbox Elite, View + Menu together on other pads."));
    _advanced->setChecked(_host->DebugLayer());
    top->addWidget(_advanced);
    _globalShortcuts = new QCheckBox(tr("Global shortcuts"), this);
    _globalShortcuts->setToolTip(
        tr("The debugger's shortcuts (F5, F10, F11...) work in the main window too; a key they take does not "
           "reach the machine. In the debugger window they always work"));
    _globalShortcuts->setChecked(_host->GlobalShortcuts());
    top->addWidget(_globalShortcuts);
    _soundHaptics = new QCheckBox(tr("Haptics from the sound"), this);
    _soundHaptics->setToolTip(
        tr("Pads with motors rumble with the AY noise and envelope booms and the beeper's noise "
           "bursts, until a game profile brings its own haptics"));
    _soundHaptics->setChecked(_host->SoundHaptics());
    top->addWidget(_soundHaptics);
    _profilesButton = new QPushButton(tr("Profiles…"), this);
    _profilesButton->setToolTip(tr("Every profile as YAML: edit, save, import, export, delete"));
    top->addWidget(_profilesButton);
    root->addLayout(top);

    auto* splitter = new QSplitter(this);
    root->addWidget(splitter, 1);

    auto* left = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(new QLabel(tr("<b>Host devices</b>"), left));
    _devices = new QListWidget(left);
    leftLayout->addWidget(_devices, 1);
    _probeLabel = new QLabel(left);
    _probeLabel->setWordWrap(true);
    leftLayout->addWidget(_probeLabel);

    auto* centre = new QWidget(splitter);
    auto* centreLayout = new QVBoxLayout(centre);
    centreLayout->setContentsMargins(0, 0, 0, 0);
    // The selected device's header: its name, and what can be tried on it
    auto* header = new QHBoxLayout();
    _deviceTitle = new QLabel(centre);
    _deviceTitle->setWordWrap(true);
    _deviceTitle->setTextInteractionFlags(Qt::TextSelectableByMouse);  // a notice's udev command can be copied
    header->addWidget(_deviceTitle, 1);
    // The battery as the OS reports it, small and honest: "unknown" when it says nothing (tick refreshes it)
    _battery = new QLabel(centre);
    _battery->setToolTip(tr("The battery as the operating system reports it"));
    header->addWidget(_battery, 0, Qt::AlignTop);
    _hapticsEffect = new QComboBox(centre);
    _hapticsEffect->setEnabled(false);
    header->addWidget(_hapticsEffect, 0, Qt::AlignTop);
    _testHaptics = new QPushButton(tr("Test haptics"), centre);
    _testHaptics->setEnabled(false);
    _testHaptics->setToolTip(tr("Select a device"));
    header->addWidget(_testHaptics, 0, Qt::AlignTop);
    connect(_testHaptics, &QPushButton::clicked, this, [this] {
        const int index = _hapticsEffect->currentIndex();
        if (index >= 0 && index < static_cast<int>(_hapticsTests.size()))
            _hapticsTests[index]();
    });
    centreLayout->addLayout(header);
    _picture = new ControllerPicture(centre);
    centreLayout->addWidget(_picture, 2);
    _table = new BindingTable(_host, centre);
    _table->setDebugTab(_host->DebugLayer());
    centreLayout->addWidget(_table, 3);

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
    _soundHapticsDsp = new QCheckBox(tr("DSP filters on the sound"), right);
    _soundHapticsDsp->setToolTip(
        tr("With \"Haptics from the sound\": the bass of the whole mix on the low motor and a short tick on the "
           "high motor at each sharp onset, besides the AY and beeper filters"));
    _soundHapticsDsp->setChecked(_host->SoundHapticsDsp());
    _soundHapticsDsp->setEnabled(_host->SoundHaptics());
    rightLayout->addWidget(_soundHapticsDsp);
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
    connect(_table, &BindingTable::captionsChanged, this, [this] { _picture->setCaptions(_table->captions()); });
    connect(_table, &BindingTable::controlHovered, this,
            [this](const std::string& control) { _picture->setSelected(control); });
    connect(_profilesButton, &QPushButton::clicked, this, [this] {
        ProfilesDialog dialog(_host, this);
        dialog.exec();
    });
#ifdef ENABLE_CONTROLLERS
#endif
    connect(_picture, &ControllerPicture::controlClicked, this, [this](const QString& control) {
        _picture->setSelected(control.toStdString());
        _table->showControl(control.toStdString());
    });
    connect(_advanced, &QCheckBox::toggled, this, [this](bool on) {
        _table->setDebugTab(on);
        _host->SetDebugLayer(on);
    });
    connect(_soundHaptics, &QCheckBox::toggled, _host, &HostControllers::SetSoundHaptics);
    connect(_soundHaptics, &QCheckBox::toggled, _soundHapticsDsp, &QCheckBox::setEnabled);
    connect(_soundHapticsDsp, &QCheckBox::toggled, _host, &HostControllers::SetSoundHapticsDsp);
    connect(_globalShortcuts, &QCheckBox::toggled, _host, &HostControllers::SetGlobalShortcuts);
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

void ControllersDialog::setActions(const actions::ActionRegistry* actions)
{
    _table->setActions(actions);
    _table->refresh();
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
        auto* item =
            new QListWidgetItem(QString("%1\n%2 · %3")
                                    .arg(QString::fromStdString(device.name), QString::fromStdString(device.family),
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
        probes << (probe.running
                       ? tr("%1 ok (%2)").arg(QString::fromStdString(probe.name)).arg(probe.devices)
                       : tr("%1: %2").arg(QString::fromStdString(probe.name), QString::fromStdString(probe.reason)));
    _probeLabel->setText(probes.isEmpty() ? tr("No native backend on this OS yet") : probes.join(" · "));

    const ResolvedProfiles& resolved = _host->Resolved();
    QStringList applied;
    for (const std::string& id : resolved.applied)
        applied << QString::fromStdString(id);
    _profileLabel->setText(
        tr("<b>Profiles</b>  %1%2")
            .arg(applied.join(" → "), resolved.warnings.empty()
                                          ? QString()
                                          : tr("  <span style='color:#c60'>%n warning(s)</span>", nullptr,
                                               static_cast<int>(resolved.warnings.size()))));
    rebuildControls();
    rebuildConnectors();
#endif
}

void ControllersDialog::rebuildHapticsTest(const std::string& device)
{
#ifdef ENABLE_CONTROLLERS
    const QString kept = _hapticsEffect->currentText();
    _hapticsEffect->clear();
    _hapticsTests.clear();
    const std::string why =
        device.empty() ? std::string("Select a device") : _host->Service().HapticsUnavailable(device);
    const QString tip = why.empty() ? tr("Play the chosen effect on the selected device") : QString::fromStdString(why);
    _hapticsEffect->setEnabled(why.empty());
    _hapticsEffect->setToolTip(tip);
    _testHaptics->setEnabled(why.empty());
    _testHaptics->setToolTip(tip);
    if (!why.empty())
        return;

    // Test effects are debug feedback (haptics.md §10): they play while the game's haptics are held back
    for (const controllers::TestEffect& test : controllers::TestEffects(_host->Service().RumbleActuators(device)))
    {
        _hapticsEffect->addItem(QString::fromStdString(test.label));
        _hapticsTests.push_back(
            [this, device, test] { _host->Service().PlayTest(device, test); });
    }
    const int again = _hapticsEffect->findText(kept);  // the same effect stays chosen across a refresh
    if (again >= 0)
        _hapticsEffect->setCurrentIndex(again);
#else
    (void)device;
#endif
}

void ControllersDialog::rebuildControls()
{
#ifdef ENABLE_CONTROLLERS
    const std::string device = selectedDevice();
    const std::string family = selectedFamily();
    rebuildHapticsTest(device);
    QListWidgetItem* item = _devices->currentItem();
    const QString name = item != nullptr && !device.empty() ? item->text().section('\n', 0, 0) : QString();
    QString title = device.empty() ? QString() : QString("<b>%1</b>").arg(name.toHtmlEscaped());
#ifdef Q_OS_MACOS
    // research-host-apis.md §2.3a: XboxGamepad.dext cuts the USB report before the paddle byte
    if (family == "xbox-elite-2" && device.rfind("usb:", 0) == 0)
        title += "<br>" + tr("Paddles over USB on macOS arrive as the buttons the controller's profile assigns. "
                             "Connect by Bluetooth to use them separately.");
#endif
    // What the backend says is missing and how to fix it (Linux: no hidraw access, paddles on profile 0 only)
    const std::string notice = device.empty() ? std::string() : _host->Service().DeviceNotice(device);
    if (!notice.empty())
        title += "<br>" + QString::fromStdString(notice).toHtmlEscaped();
    _deviceTitle->setText(title);
    const DeviceMatch match = DeviceDatabase::Builtin().ForFamily(family, selectedLayout());
    const QString set = device.empty() ? QString() : QString::fromStdString(match.images);
    if (set != _pictureSet)
    {
        _pictureSet = set;
        _picture->load(set);
    }
    _table->setDevice({device, family, device.empty() ? std::string{} : match.glyphs, name});
    _picture->setCaptions(_table->captions());
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
    // The backends keep the battery and refresh it themselves: reading it here does not wait for the device
    const QString battery =
        device.empty() ? QString()
                       : QString("<small>%1</small>")
                             .arg(tr("Battery: %1")
                                      .arg(QString::fromStdString(
                                          controllers::BatteryText(_host->Service().Battery(device)))));
    if (_battery->text() != battery)
        _battery->setText(battery);
    if (!device.empty())
    {
        const std::map<std::string, float> live = _host->Service().Live(device);
        _picture->setLive(live);
        _table->setLive(live);
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

void ControllersDialog::setSurfaceTargets(SurfaceTargets* targets)
{
    if (_surfaces != nullptr || targets == nullptr || _root == nullptr)
        return;
    _surfaces = new SurfacesTable(targets, this);
    _root->addWidget(_surfaces);
}
