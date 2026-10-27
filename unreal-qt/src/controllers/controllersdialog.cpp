#include "controllersdialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "controllers/bindingtable.h"
#include "controllers/connectorcard.h"
#include "controllers/connectorview.h"
#include "controllers/controllerpicture.h"
#include "controllers/devicetestpanel.h"
#include "controllers/hotspoteditor.h"
#include "controllers/learnwizard.h"
#include "controllers/patchcordoverlay.h"
#include "controllers/hostcontrollers.h"
#include "controllers/profilesdialog.h"
#include "controllers/surfacestable.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"

#ifdef ENABLE_CONTROLLERS
#include "devicedatabase.h"
#include "emulator/io/controllers/controllercontrol.h"
#include "emulator/io/controllers/controllermanager.h"
#include "haptics/testeffects.h"
#include "hid/reporttap.h"
#include "router/inputrouter.h"

using namespace controllers;

namespace
{
/// A control away from rest: a button or trigger pressed, a stick pushed (the cord pulses)
bool Sends(const std::map<std::string, float>& live)
{
    for (const auto& [control, value] : live)
        if (control.rfind("mouse.", 0) != 0 && control.rfind("pointer.", 0) != 0 && std::abs(value) >= 0.3f)
            return true;
    return false;
}

/// gamecontrollerdb.txt holds one line per pad: the one whose GUID carries @p vid / @p pid (little-endian words at
/// 4 and 8), else the first mapping line; any other text (a RetroArch .cfg) as it is
QString PickMapping(const QString& text, uint16_t vid, uint16_t pid)
{
    if (text.contains(QStringLiteral("input_device")) || text.contains(QStringLiteral("input_vendor_id")))
        return text;
    const auto word = [](uint16_t value) {
        return QStringLiteral("%1%2").arg(value & 0xff, 2, 16, QChar('0')).arg(value >> 8, 2, 16, QChar('0'));
    };
    QString first;
    for (const QString& raw : text.split('\n'))
    {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.count(',') < 2)
            continue;
        if (first.isEmpty())
            first = line;
        const QString guid = line.section(',', 0, 0).toLower();
        if ((vid != 0 || pid != 0) && guid.size() >= 24 && guid.mid(8, 4) == word(vid) && guid.mid(16, 4) == word(pid))
            return line;
    }
    return first;
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
    _pauseOnDisconnect = new QCheckBox(tr("Pause on disconnect"), this);
    _pauseOnDisconnect->setToolTip(tr("A game pad that disconnects while the machine runs pauses it"));
    _pauseOnDisconnect->setChecked(_host->PauseOnDisconnect());
    top->addWidget(_pauseOnDisconnect);
    _profilesButton = new QPushButton(tr("Profiles…"), this);
    _profilesButton->setToolTip(tr("Every profile as YAML: edit, save, import, export, delete"));
    top->addWidget(_profilesButton);
    root->addLayout(top);

    auto* splitter = new QSplitter(this);
    // The gutters carry the patch cords from the devices to the picture and from it to the connectors
    splitter->setHandleWidth(26);
    splitter->setChildrenCollapsible(false);
    root->addWidget(splitter, 1);

    auto* left = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(new QLabel(tr("<b>Host devices</b>"), left));
    _devices = new DeviceDragList(left);
    _devices->setToolTip(tr("Drag a device onto a connector to plug it"));
    leftLayout->addWidget(_devices, 1);
    _importButton = new QPushButton(tr("Import mapping…"), left);
    _importButton->setToolTip(
        tr("A gamecontrollerdb.txt line (SDL) or a RetroArch joypad autoconfig .cfg for a pad the database does not "
           "know: its VID:PID, buttons and controls become a device entry of your own"));
    leftLayout->addWidget(_importButton);
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
    _testButton = new QPushButton(tr("Test"), centre);
    _testButton->setCheckable(true);
    _testButton->setToolTip(tr("What the device sends: sticks with the dead zone and thresholds, triggers, buttons"));
    header->addWidget(_testButton, 0, Qt::AlignTop);
    _view = new QComboBox(centre);
    _view->setToolTip(tr("The side of the device the picture shows"));
    _view->hide();
    header->addWidget(_view, 0, Qt::AlignTop);
    connect(_view, &QComboBox::activated, this, [this] {
        _picture->load(_pictureSet, _view->currentData().toString());
        _picture->setCaptions(_table->captions());
    });
    _pictureButton = new QPushButton(tr("Picture…"), centre);
    _pictureButton->setToolTip(tr("Use a photo of your own pad and place its controls on it"));
    header->addWidget(_pictureButton, 0, Qt::AlignTop);
    connect(_testHaptics, &QPushButton::clicked, this, [this] {
        const int index = _hapticsEffect->currentIndex();
        if (index >= 0 && index < static_cast<int>(_hapticsTests.size()))
            _hapticsTests[index]();
    });
    centreLayout->addLayout(header);
    // What the device is for and which instance it drives; Identify and Learn… (ui-ux.md §3.5, §3.7)
    auto* deviceRow = new QHBoxLayout();
    deviceRow->addWidget(new QLabel(tr("Role:"), centre));
    _role = new QComboBox(centre);
    _role->addItem(tr("Player"), QStringLiteral("player"));
    _role->addItem(tr("Debugger remote"), QStringLiteral("debugger-remote"));
    _role->addItem(tr("Disabled"), QStringLiteral("disabled"));
    _role->setToolTip(tr("Player: plays the machine through a connector. Debugger remote: only its debug bindings "
                         "(step, breakpoints), never a connector. Disabled: it reaches nothing"));
    deviceRow->addWidget(_role);
    deviceRow->addWidget(new QLabel(tr("Drives:"), centre));
    _pin = new QComboBox(centre);
    _pin->setToolTip(tr("Which emulator instance the device drives: the one whose window has the focus, every "
                        "instance (a demo on the video wall), or one"));
    deviceRow->addWidget(_pin);
    _identify = new QPushButton(tr("Identify"), centre);
    _identify->setToolTip(tr("Rumble the device for 300 ms and blink its light, to tell two pads apart"));
    deviceRow->addWidget(_identify);
    _learnButton = new QPushButton(tr("Learn…"), centre);
    _learnButton->setToolTip(tr("Teach the emulator a pad it does not know: press each control when it is asked for"));
    deviceRow->addWidget(_learnButton);
    deviceRow->addStretch(1);
    centreLayout->addLayout(deviceRow);
    _picture = new ControllerPicture(centre);
    centreLayout->addWidget(_picture, 2);
    _test = new DeviceTestPanel(centre);
    _test->hide();
    centreLayout->addWidget(_test);
    _table = new BindingTable(_host, centre);
    _table->setDebugTab(_host->DebugLayer());
    centreLayout->addWidget(_table, 3);

    auto* right = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    _machineLabel = new QLabel(right);
    rightLayout->addWidget(_machineLabel);
    auto* scroll = new QScrollArea(right);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    _cardsBox = new QWidget(scroll);
    _cardsLayout = new QVBoxLayout(_cardsBox);
    _cardsLayout->setContentsMargins(0, 0, 4, 0);
    _cardsLayout->setSpacing(6);
    _cardsLayout->addStretch(1);
    scroll->setWidget(_cardsBox);
    rightLayout->addWidget(scroll, 1);
    auto* statusRow = new QHBoxLayout();
    _status = new QLabel(right);
    _status->setWordWrap(true);
    statusRow->addWidget(_status, 1);
    _undoButton = new QPushButton(tr("Undo"), right);
    _undoButton->setToolTip(tr("Undo the last plug or unplug (Ctrl+Z)"));
    _undoButton->setEnabled(false);
    statusRow->addWidget(_undoButton, 0, Qt::AlignTop);
    rightLayout->addLayout(statusRow);
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

    // Over everything, last: the cords pass between the panes, mouse events go through
    _cords = new PatchCordOverlay(this);
    _cords->setGeometry(rect());
    _cords->raise();

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
    connect(_pauseOnDisconnect, &QCheckBox::toggled, _host, &HostControllers::SetPauseOnDisconnect);
    connect(_testButton, &QPushButton::toggled, this, [this](bool on) {
        _test->setVisible(on);
        refreshTestPanel(true);
        updateCords();
    });
    connect(_pictureButton, &QPushButton::clicked, this, [this] {
#ifdef ENABLE_CONTROLLERS
        const std::string device = selectedDevice();
        if (device.empty())
            return;
        const DeviceMatch match = DeviceDatabase::Active()->ForFamily(selectedFamily(), selectedLayout());
        QListWidgetItem* item = _devices->currentItem();
        HotspotEditor::Hooks hooks;
        hooks.live = [this, device] { return _host->Service().Live(device); };
        hooks.userDir = HostControllers::UserControllersDir();
        hooks.save = [this](const std::string& entry, QString& error) {
            HostRequest request;
            request.verb = "database_save";
            request.options["text"] = entry;
            const HostReply reply = _host->Host(request);
            error = QString::fromStdString(reply.message);
            return reply.Ok();
        };
        HotspotEditor editor({device, selectedFamily(), item->text().section('\n', 0, 0).toStdString(), match.images,
                              match.controls},
                             std::move(hooks), this);
        if (editor.exec() != QDialog::Accepted)
            return;
        _pictureSet.clear();  // the set may have the same name with a new photo: load it again
        rebuildControls();
        if (selectedFamily() == "generic")
            QMessageBox::information(this, tr("Controllers"),
                                     tr("The picture is saved as a device entry of its own. It is used from the next "
                                        "time the device connects."));
#endif
    });
    connect(_soundHaptics, &QCheckBox::toggled, _soundHapticsDsp, &QCheckBox::setEnabled);
    connect(_soundHapticsDsp, &QCheckBox::toggled, _host, &HostControllers::SetSoundHapticsDsp);
    connect(_globalShortcuts, &QCheckBox::toggled, _host, &HostControllers::SetGlobalShortcuts);
#ifdef ENABLE_CONTROLLERS
    connect(_role, &QComboBox::activated, this, [this] {
        const std::string device = selectedDevice();
        if (device.empty())
            return;
        const HostReply reply =
            _host->Host({"role", {{"device", device}, {"role", _role->currentData().toString().toStdString()}}});
        setStatus(QString::fromStdString(reply.message), !reply.Ok());
        refreshRolePin();
    });
    connect(_pin, &QComboBox::activated, this, [this] {
        const std::string device = selectedDevice();
        if (device.empty())
            return;
        const HostReply reply =
            _host->Host({"pin", {{"device", device}, {"pin", _pin->currentData().toString().toStdString()}}});
        setStatus(QString::fromStdString(reply.message), !reply.Ok());
        refreshRolePin();
    });
    connect(_identify, &QPushButton::clicked, this, [this] { identifySelected(); });
    connect(_learnButton, &QPushButton::clicked, this, [this] { openLearnWizard(selectedDevice()); });
#endif
#ifdef ENABLE_CONTROLLERS
    // ui-ux.md §2.3: Plug / Unplug on a device's context menu, P and U on the list, for those who do not drag
    _devices->setContextMenuPolicy(Qt::CustomContextMenu);
    const auto plugMenu = [this](const QPoint& at) {
        const std::string device = selectedDevice();
        if (device.empty())
            return;
        QMenu menu(this);
        QMenu* into = menu.addMenu(tr("&Plug into"));
        for (const ConnectorCard* card : _cards)
        {
            const std::string id = card->connector().id;
            QAction* action = into->addAction(connectorview::DisplayName(card->connector()));
            const QString why = refusal(device, id);
            action->setEnabled(why.isEmpty());
            action->setToolTip(why);
            connect(action, &QAction::triggered, this, [this, device, id] { plugDevice(device, id); });
        }
        QAction* unplug = menu.addAction(tr("&Unplug"));
        unplug->setEnabled(_host->PlayerChoice(device) != "none" &&
                           !_host->Service().Router().PlayerConnector(device).empty());
        connect(unplug, &QAction::triggered, this, [this, device] { unplugDevice(device); });
        menu.exec(at);
    };
    connect(_devices, &QWidget::customContextMenuRequested, this,
            [this, plugMenu](const QPoint& at) { plugMenu(_devices->viewport()->mapToGlobal(at)); });
    auto* plugKey = new QShortcut(QKeySequence(Qt::Key_P), _devices, nullptr, nullptr, Qt::WidgetShortcut);
    connect(plugKey, &QShortcut::activated, this, [this, plugMenu] {
        if (QListWidgetItem* item = _devices->currentItem())
            plugMenu(_devices->viewport()->mapToGlobal(_devices->visualItemRect(item).center()));
    });
    auto* unplugKey = new QShortcut(QKeySequence(Qt::Key_U), _devices, nullptr, nullptr, Qt::WidgetShortcut);
    connect(unplugKey, &QShortcut::activated, this, [this] {
        if (!selectedDevice().empty())
            unplugDevice(selectedDevice());
    });
#endif
    connect(_undoButton, &QPushButton::clicked, this, [this] { undo(); });
    auto* undoKey = new QShortcut(QKeySequence::Undo, this);
    connect(undoKey, &QShortcut::activated, this, [this] { undo(); });
    connect(_importButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Import a pad mapping"), QString(),
            tr("Pad mappings (gamecontrollerdb.txt *.txt *.cfg);;All files (*)"));
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            setStatus(tr("Cannot read %1").arg(path), true);
            return;
        }
        importMapping(QString::fromUtf8(file.readAll()));
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
    const DeviceMatch match = DeviceDatabase::Active()->ForFamily(family, selectedLayout());
    const QString set = device.empty() ? QString() : QString::fromStdString(match.images);
    if (set != _pictureSet)
    {
        _pictureSet = set;
        _picture->load(set);
        // Front | Back when the set maps more than one side (an Elite's paddles, a DualSense Edge's back buttons)
        const QStringList views = ControllerPicture::Views(set);
        _view->clear();
        for (const QString& view : views)
            _view->addItem(view == "front" ? tr("Front") : view == "back" ? tr("Back") : view == "top" ? tr("Top") : view,
                           view);
        _view->setVisible(views.size() > 1);
    }
    _table->setDevice({device, family, device.empty() ? std::string{} : match.glyphs, name});
    _picture->setCaptions(_table->captions());
    _pictureButton->setEnabled(!device.empty() && family != "keyboard" && family != "mouse");
    _learnButton->setEnabled(!device.empty() && family != "keyboard" && family != "mouse");
    _identify->setEnabled(!device.empty());
    refreshRolePin();
    QString detail;
    for (const HostDeviceInfo& info : _host->Service().Router().Devices())
        if (info.id == device)
            detail = QString("%1 · %2 · %3")
                         .arg(QString::fromStdString(info.backend), QString::fromStdString(info.family),
                              QString::fromStdString(info.id));
    if (!detail.isEmpty() && !_probeLabel->text().isEmpty())
        detail += "  —  " + _probeLabel->text();
    _test->setDevice(device, device.empty() ? std::vector<std::string>{} : match.controls, detail);
    refreshTestPanel(true);
#endif
}

void ControllersDialog::refreshTestPanel(bool full)
{
#ifdef ENABLE_CONTROLLERS
    const std::string device = selectedDevice();
    if (device.empty() || !_test->isVisible())
        return;
    if (full)
    {
        for (const char* control : {"lstick", "rstick", "lt", "rt"})
        {
            AnalogOptions options;
            DeviceTestPanel::Thresholds read;
            read.fromProfile = _host->Service().Router().Analog(device, control, options);
            read.deadzone = options.deadzone;
            read.saturation = options.saturation;
            read.on = options.on;
            read.off = options.off;
            read.triggerClick = options.triggerClick;
            read.triggerRelease = options.triggerRelease;
            _test->setThresholds(control, read);
        }
        std::vector<QStringList> rows;
        const auto yes = [this](bool on) { return on ? tr("yes") : QStringLiteral("—"); };
        for (const EndpointInfo& endpoint : _host->Service().Endpoints(device))
            rows.push_back({QString::fromStdString(endpoint.backend), QString::fromStdString(endpoint.name),
                            QString::fromStdString(endpoint.role), QString::number(endpoint.controls),
                            QString::fromStdString(endpoint.driver), yes(endpoint.rumble), yes(endpoint.light),
                            yes(endpoint.battery), QString::fromStdString(endpoint.detail)});
        _test->setEndpoints(rows);
    }
    // Reading them keeps the backend's tap armed; it disarms itself a few seconds after the panel closes
    QStringList lines;
    for (const RawReport& report : _host->Service().RawReports(device))
        lines << QStringLiteral("%1  %2").arg(report.timeMs % 100000, 5, 10, QChar('0')).arg(
                     QString::fromStdString(RawReportHex(report)));
    _test->setRawReports(lines, QString::fromStdString(_host->Service().RawReportsUnavailable(device)));
#endif
}

void ControllersDialog::rebuildConnectors()
{
#ifdef ENABLE_CONTROLLERS
    Emulator* emulator = _host->BoundEmulator();
    EmulatorContext* context = _host->BoundContext();
    ControllerManager* manager = context != nullptr ? context->pControllerManager : nullptr;
    _machineLabel->setText(emulator != nullptr ? QString("<b>%1</b>").arg(QString::fromStdString(emulator->GetId()))
                                               : tr("No machine"));
    const std::vector<Connector> connectors = manager != nullptr ? manager->Connectors() : std::vector<Connector>{};
    if (manager != nullptr)
        _machineRevision = manager->Revision();
    // A card per connector, kept across a rebuild: a drag in flight keeps its target
    while (_cards.size() > connectors.size())
    {
        delete _cards.back();
        _cards.pop_back();
    }
    while (_cards.size() < connectors.size())
    {
        auto* card = new ConnectorCard(_cardsBox);
        _cardsLayout->insertWidget(static_cast<int>(_cards.size()), card);
        connect(card, &ConnectorCard::retroDeviceChosen, this,
                [this](const QString& connector, const QString& device) { plug(connector.toStdString(), device); });
        connect(card, &ConnectorCard::deviceDropped, this, [this](const QString& device, const QString& connector) {
            plugDevice(device.toStdString(), connector.toStdString());
        });
        connect(card, &ConnectorCard::deviceDraggedOff, this,
                [this](const QString& device, const QString&) { unplugDevice(device.toStdString()); });
        _cards.push_back(card);
    }
    for (size_t i = 0; i < connectors.size(); ++i)
    {
        ConnectorCard* card = _cards[i];
        card->setConnector(connectors[i]);
        const std::string id = connectors[i].id;
        card->refusalFor = [this, id](const std::string& device) { return refusal(device, id); };
        card->setHeld(manager->State(i));
    }
#endif
}

QString ControllersDialog::refusal(const std::string& device, const std::string& connector) const
{
#ifdef ENABLE_CONTROLLERS
    const std::vector<HostDeviceInfo> devices = _host->Service().Router().Devices();
    const auto info = std::find_if(devices.begin(), devices.end(),
                                   [&device](const HostDeviceInfo& known) { return known.id == device; });
    if (info == devices.end())
        return tr("The device is gone");
    if (info->family == "keyboard" || info->family == "mouse" ||
        !DeviceDatabase::Active()->ForFamily(info->family, info->layout).player)
        return tr("%1 does not plug: its bindings name their targets").arg(QString::fromStdString(info->name));
    EmulatorContext* context = _host->BoundContext();
    ControllerManager* manager = context != nullptr ? context->pControllerManager : nullptr;
    if (manager == nullptr)
        return tr("No machine");
    const int index = manager->Find(connector);
    if (index < 0)
        return tr("The connector is gone");
    const Connector found = manager->At(static_cast<size_t>(index));
    if (found.device == RetroDeviceKind::None && (found.fixed || found.accepts.empty()))
        return QString::fromStdString(found.id + " is part of the board");
    return {};
#else
    (void)device;
    (void)connector;
    return tr("No controllers in this build");
#endif
}

bool ControllersDialog::plugDevice(const std::string& device, const std::string& connector)
{
#ifdef ENABLE_CONTROLLERS
    const QString refused = refusal(device, connector);
    if (!refused.isEmpty())
    {
        setStatus(refused, true);
        return false;
    }
    ControllerManager* manager = _host->BoundContext()->pControllerManager;
    const Connector target = manager->At(static_cast<size_t>(manager->Find(connector)));
    UndoStep step{device, _host->PlayerChoice(device), {}};
    // An empty connector gets the first retro device it takes: the drop is the whole gesture
    if (target.device == RetroDeviceKind::None)
    {
        if (!plug(target.id, QString::fromLatin1(RetroDeviceKindName(target.accepts.front()))))
            return false;
        step.pluggedConnector = target.id;
    }
    std::string message;
    const std::string error = _host->ChoosePlayerConnector(device, target.id, message);
    if (!error.empty())
    {
        if (!step.pluggedConnector.empty())
            plug(step.pluggedConnector, QString());
        setStatus(QString::fromStdString(message), true);
        return false;
    }
    _undo.push_back(step);
    _undoButton->setEnabled(true);
    setStatus(tr("%1 plugged into %2").arg(QString::fromStdString(device), connectorview::DisplayName(target)), false);
    rebuildConnectors();
    return true;
#else
    (void)device;
    (void)connector;
    return false;
#endif
}

void ControllersDialog::unplugDevice(const std::string& device)
{
#ifdef ENABLE_CONTROLLERS
    std::string message;
    const std::string before = _host->PlayerChoice(device);
    if (const std::string error = _host->ChoosePlayerConnector(device, "none", message); !error.empty())
    {
        setStatus(QString::fromStdString(message), true);
        return;
    }
    _undo.push_back({device, before, {}});
    _undoButton->setEnabled(true);
    setStatus(tr("%1 unplugged (Ctrl+Z puts it back)").arg(QString::fromStdString(device)), false);
    rebuildConnectors();
#else
    (void)device;
#endif
}

bool ControllersDialog::undo()
{
    if (_undo.empty())
        return false;
    const UndoStep step = _undo.back();
    _undo.pop_back();
    _undoButton->setEnabled(!_undo.empty());
#ifdef ENABLE_CONTROLLERS
    std::string message;
    // "" goes back to the automatic choice, "none" and a connector id as they were
    _host->ChoosePlayerConnector(step.device, step.choice, message);
#endif
    if (!step.pluggedConnector.empty())
        plug(step.pluggedConnector, QString());
    setStatus(tr("Undone"), false);
    rebuildConnectors();
    return true;
}

QString ControllersDialog::statusText() const
{
    return _status->text();
}

void ControllersDialog::setStatus(const QString& text, bool error)
{
    _status->setStyleSheet(error ? QStringLiteral("color: #c0504d;") : QString());
    _status->setText(text);
}

void ControllersDialog::refreshRolePin()
{
#ifdef ENABLE_CONTROLLERS
    const std::string device = selectedDevice();
    _role->setEnabled(!device.empty());
    _pin->setEnabled(!device.empty());
    if (device.empty() || _role->view()->isVisible() || _pin->view()->isVisible())
        return;
    const InputRouter& router = _host->Service().Router();
    const int role = _role->findData(QString::fromLatin1(DeviceRoleName(router.Role(device))));
    if (role >= 0 && role != _role->currentIndex())
        _role->setCurrentIndex(role);
    // Follow focus, all, then one entry per instance (this window's named so); a pin to an instance gone stays listed
    std::string pinned;
    const InstanceMode mode = router.GetInstanceMode(device, &pinned);
    const QString current = mode == InstanceMode::All      ? QStringLiteral("all")
                            : mode == InstanceMode::Pinned ? QString::fromStdString("pinned:" + pinned)
                                                           : QStringLiteral("follow-focus");
    std::vector<std::pair<QString, QString>> items = {{tr("Follow focus"), QStringLiteral("follow-focus")},
                                                      {tr("All instances"), QStringLiteral("all")}};
    Emulator* bound = _host->BoundEmulator();
    std::vector<std::string> ids = EmulatorManager::GetInstance()->GetEmulatorIds();
    if (mode == InstanceMode::Pinned && std::find(ids.begin(), ids.end(), pinned) == ids.end())
        ids.push_back(pinned);
    for (const std::string& id : ids)
    {
        const QString shortId = QString::fromStdString(id.substr(0, 8));
        items.emplace_back(bound != nullptr && bound->GetId() == id ? tr("This instance (%1)").arg(shortId)
                                                                     : tr("Instance %1").arg(shortId),
                           QString::fromStdString("pinned:" + id));
    }
    bool same = static_cast<int>(items.size()) == _pin->count();
    for (int i = 0; same && i < _pin->count(); i++)
        same = _pin->itemData(i).toString() == items[static_cast<size_t>(i)].second &&
               _pin->itemText(i) == items[static_cast<size_t>(i)].first;
    if (!same)
    {
        _pin->clear();
        for (const auto& [label, data] : items)
            _pin->addItem(label, data);
    }
    const int pin = _pin->findData(current);
    if (pin >= 0 && pin != _pin->currentIndex())
        _pin->setCurrentIndex(pin);
#endif
}

QString ControllersDialog::identifySelected()
{
#ifdef ENABLE_CONTROLLERS
    const std::string device = selectedDevice();
    if (device.empty())
        return tr("Select a device");
    const HostReply reply = _host->Host({"identify", {{"device", device}}});
    const QString message = QString::fromStdString(reply.message);
    setStatus(message, !reply.Ok());
    if (reply.Ok())
    {
        // The row pulses as long as the pad rumbles, so two identical pads are told apart on screen too
        if (QListWidgetItem* item = _devices->currentItem())
        {
            item->setBackground(QColor(255, 200, 60, 140));
            QPointer<QListWidget> list(_devices);
            const QString id = QString::fromStdString(device);
            QTimer::singleShot(600, this, [list, id] {
                if (list == nullptr)
                    return;
                for (int row = 0; row < list->count(); row++)
                    if (list->item(row)->data(Qt::UserRole).toString() == id)
                        list->item(row)->setBackground(QBrush());
            });
        }
    }
    return message;
#else
    return {};
#endif
}

QString ControllersDialog::openLearnWizard(const std::string& device)
{
#ifdef ENABLE_CONTROLLERS
    // The device's row, so the picture and the table show what is learned
    QString name;
    for (int row = 0; row < _devices->count(); row++)
        if (_devices->item(row)->data(Qt::UserRole).toString().toStdString() == device)
        {
            _devices->setCurrentRow(row);
            name = _devices->item(row)->text().section('\n', 0, 0);
        }
    if (name.isEmpty())
    {
        const QString why = tr("No connected host device '%1'").arg(QString::fromStdString(device));
        setStatus(why, true);
        return why;
    }
    if (_wizard != nullptr)
    {
        _wizard->refresh();  // a session cancelled elsewhere since its last poll: not one to follow
        if (_wizard->device() == device && _wizard->learning())
        {
            _wizard->raise();
            _wizard->activateWindow();
            return {};
        }
        delete _wizard.data();  // its session ends with it
    }
    // The host may go first at exit: the wizard's last cancel then finds nobody to ask
    const QPointer<HostControllers> host(_host);
    auto* wizard = new LearnWizard(
        device, name,
        [host](const HostRequest& request) {
            HostReply reply;
            if (host == nullptr)
            {
                reply.error = "unavailable";
                reply.message = "the controllers are gone";
                return reply;
            }
            return host->Host(request);
        },
        this);
    wizard->setAttribute(Qt::WA_DeleteOnClose);
    _wizard = wizard;
    connect(wizard, &LearnWizard::learned, this, [this](const QString& message) {
        setStatus(message, false);
        _pictureSet.clear();  // a saved family may bring a layout of its own
        rebuildControls();
    });
    if (!wizard->start())
    {
        const QString why = wizard->statusText();
        setStatus(why, true);
        delete wizard;
        return why;
    }
    wizard->show();
    wizard->raise();
    return {};
#else
    (void)device;
    return {};
#endif
}

QString ControllersDialog::importMapping(const QString& text)
{
#ifdef ENABLE_CONTROLLERS
    // A USB or Bluetooth id carries the VID:PID ("usb:2dc8:6101:..."); other ids pick the first line
    const QStringList parts = QString::fromStdString(selectedDevice()).split(':');
    const uint16_t vid = parts.size() >= 3 ? parts[1].toUShort(nullptr, 16) : 0;
    const uint16_t pid = parts.size() >= 3 ? parts[2].toUShort(nullptr, 16) : 0;
    HostRequest request;
    request.verb = "database_import";
    request.options["text"] = PickMapping(text, vid, pid).toStdString();
    const HostReply reply = _host->Host(request);
    const QString answer = QString::fromStdString(reply.message);
    setStatus(reply.Ok() ? tr("%1. The pad uses the map now.").arg(answer) : answer, !reply.Ok());
    return answer;
#else
    (void)text;
    return {};
#endif
}

bool ControllersDialog::plug(const std::string& connector, const QString& device)
{
#ifdef ENABLE_CONTROLLERS
    EmulatorContext* context = _host->BoundContext();
    if (context == nullptr)
        return false;
    ControllerControl control(context);
    ControllerRequest request;
    request.verb = device.isEmpty() ? "unplug" : "plug";
    request.selector = connector;
    request.device = device.toStdString();
    request.options["async"] = "";
    const ControllerReply reply = control.Execute(request);
    if (!reply.result.Ok())
        setStatus(QString::fromStdString(reply.result.message), true);
    _host->Reapply();
    return reply.result.Ok();
#else
    (void)connector;
    (void)device;
    return false;
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
    if (++_rolePinTicks % 10 == 0)
        refreshRolePin();  // a script's role / pin shows within a third of a second
    if (!device.empty())
    {
        const std::map<std::string, float> live = _host->Service().Live(device);
        _picture->setLive(live);
        _table->setLive(live);
        if (_test->isVisible())
        {
            _test->setLive(live);
            refreshTestPanel(++_ticks % 30 == 0);  // the thresholds and endpoints once a second
        }
    }
    EmulatorContext* context = _host->BoundContext();
    if (context != nullptr && context->pControllerManager != nullptr)
    {
        ControllerManager* manager = context->pControllerManager;
        if (manager->Revision() != _machineRevision)
            rebuildConnectors();
        // Per card the held controls and the host devices that drive it, in the device list's order
        std::map<std::string, std::vector<std::pair<std::string, QString>>> drivers;
        for (const HostDeviceInfo& info : _host->Service().Router().Devices())
            for (const std::string& target : _host->Service().Router().Connectors(info.id))
                drivers[target].emplace_back(info.id, QString::fromStdString(info.name));
        for (size_t i = 0; i < _cards.size(); ++i)
        {
            _cards[i]->setHeld(manager->State(i));
            _cards[i]->setDrivers(drivers[_cards[i]->connector().id]);
        }
    }
#endif
    updateCords();
}

void ControllersDialog::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    _cords->setGeometry(rect());
    updateCords();
}

void ControllersDialog::updateCords()
{
    std::vector<PatchCordOverlay::Cord> cords;
    QRectF passage;
#ifdef ENABLE_CONTROLLERS
    EmulatorContext* context = _host->BoundContext();
    ControllerManager* manager = context != nullptr ? context->pControllerManager : nullptr;
    if (manager != nullptr && !_cards.empty() && _devices->count() > 0)
    {
        std::map<std::string, Connector> byId;
        for (const Connector& connector : manager->Connectors())
            byId[connector.id] = connector;
        const auto anchor = [this](QAbstractItemView* view, const QRect& row, bool right) {
            return PatchCordOverlay::RowAnchor(this, view, row, right);
        };
        std::map<std::string, const ConnectorCard*> cards;
        for (const ConnectorCard* card : _cards)
            cards[card->connector().id] = card;
        const std::string selected = selectedDevice();
        for (int i = 0; i < _devices->count(); ++i)
        {
            QListWidgetItem* item = _devices->item(i);
            const std::string device = item->data(Qt::UserRole).toString().toStdString();
            if (device.empty())
                continue;
            const std::vector<std::string> targets = _host->Service().Router().Connectors(device);
            if (targets.empty())
                continue;
            const bool live = Sends(_host->Service().Live(device));
            const QPointF from = anchor(_devices, _devices->visualItemRect(item), true);
            for (const std::string& target : targets)
            {
                const auto card = cards.find(target);
                const auto connector = byId.find(target);
                if (card == cards.end() || connector == byId.end() || !card->second->isVisible())
                    continue;
                const connectorview::Style style = connectorview::StyleFor(connector->second);
                PatchCordOverlay::Cord cord;
                cord.from = from;
                // A card scrolled away: the cord pins to the visible edge of the cards' column
                const QWidget* port = _cardsBox->parentWidget();
                const QPoint top = port->mapTo(this, QPoint(0, 0));
                const QPoint at = card->second->mapTo(this, card->second->cordAnchor());
                cord.to = QPointF(at.x(), std::clamp(at.y(), top.y(), top.y() + std::max(0, port->height() - 1)));
                cord.colour = style.colour;
                cord.tag = style.tag;
                cord.selected = device == selected;
                cord.live = live;
                cords.push_back(cord);
            }
        }
        if (_picture->isVisible())
            passage = QRectF(_picture->mapTo(this, QPoint(0, 0)), QSizeF(_picture->size()));
    }
#endif
    _cords->setCords(cords, passage);
}

void ControllersDialog::setSurfaceTargets(SurfaceTargets* targets)
{
    if (_surfaces != nullptr || targets == nullptr || _root == nullptr)
        return;
    _surfaces = new SurfacesTable(targets, this);
    _root->addWidget(_surfaces);
    _cords->raise();
}
