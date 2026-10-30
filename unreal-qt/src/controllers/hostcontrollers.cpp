#include "hostcontrollers.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMetaObject>
#include <QSettings>
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QTextEdit>
#include <QThread>

#include <algorithm>

#include "3rdparty/message-center/messagecenter.h"
#include "common/filehelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/actions/contextstate.h"
#include "emulator/keyboardmanager.h"
#include "platform/windowlesssession.h"
#include "platform/windows/wheelmenu_windows.h"

#ifdef ENABLE_CONTROLLERS
#include "debugger/ttd/timetravelhooks.h"
#include "devicedatabase.h"
#include "emulator/config.h"
#include "emulator/io/controllers/controllercontrol.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "haptics/profilehaptics.h"
#include "hostcontrols.h"
#include "profiles/json.h"
#include "router/inputrouter.h"
#ifdef ENABLE_SWSIGNATURE
#include "swsignature.h"
#endif

using namespace controllers;

namespace
{
constexpr const char* kKeyboard = "kbd:system";

/// The signature hook (C6, D7): the program's signatures (exact, tape content / TR-DOS file set / memory) and its
/// TR-DOS file set into @p context; without the signature library the file's SHA-256 alone
bool ProgramSignatures(const uint8_t* data, size_t size, const std::string& name, ProfileContext& context,
                       std::string& error)
{
    context.signatures.clear();
    context.fileSha256.clear();
    context.fileSet.clear();
#ifdef ENABLE_SWSIGNATURE
    unrealng::swsignature::Signatures signatures;
    if (!unrealng::swsignature::Compute(data, size, name, signatures, error))
        return false;
    context.signatures = signatures.Keys();
    context.fileSha256 = {signatures.sha256};
    context.fileSet = signatures.FileSet();
    return true;
#else
    (void)name;
    (void)error;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size)));
    context.fileSha256 = {hash.result().toHex().toStdString()};
    return true;
#endif
}

bool ProgramSignatures(const std::string& path, ProfileContext& context, std::string& error)
{
#ifdef ENABLE_SWSIGNATURE
    context.signatures.clear();
    context.fileSha256.clear();
    context.fileSet.clear();
    unrealng::swsignature::Signatures signatures;
    if (!unrealng::swsignature::ComputeFile(path, signatures, error))
        return false;
    context.signatures = signatures.Keys();
    context.fileSha256 = {signatures.sha256};
    context.fileSet = signatures.FileSet();
    return true;
#else
    context.signatures.clear();
    context.fileSha256.clear();
    context.fileSet.clear();
    QFile file(QString::fromStdString(path));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!QFileInfo(file).isFile() || !file.open(QIODevice::ReadOnly) || !hash.addData(&file))
    {
        error = "cannot read '" + path + "'";
        return false;
    }
    context.fileSha256 = {hash.result().toHex().toStdString()};
    return true;
#endif
}

/// HostInputControl's view of the Qt client's host input
class ControlOwner : public IHostInputOwner
{
public:
    explicit ControlOwner(HostControllers& host) : _host(host)
    {
    }
    ProfileStore& Profiles() override
    {
        return _host.Profiles();
    }
    ProfileContext Context() const override
    {
        return _host.Context();
    }
    const ResolvedProfiles& Resolved() const override
    {
        return _host.Resolved();
    }
    std::string UserProfileDir() const override
    {
        return HostControllers::UserProfileDir().toStdString();
    }
    std::string UserControllersDir() const override
    {
        return HostControllers::UserControllersDir().toStdString();
    }
    std::string ManualProfile() const override
    {
        return _host.ManualProfile();
    }
    void SetManualProfile(const std::string& id) override
    {
        _host.SetManualProfile(id);
    }
    void Reapply() override
    {
        _host.Reapply();
    }
    HostInputService* Input() override
    {
        return &_host.Service();
    }
    std::string ChoosePlayerConnector(const std::string& device, const std::string& connector,
                                      std::string& message) override
    {
        return _host.ChoosePlayerConnector(device, connector, message);
    }
    std::string PlayerChoice(const std::string& device) const override
    {
        return _host.PlayerChoice(device);
    }
    bool SignaturesOf(const std::string& path, ProfileContext& context, std::string& error) override
    {
        return ProgramSignatures(path, context, error);
    }

private:
    HostControllers& _host;
};
}  // namespace
#endif

HostControllers::HostControllers(EmulatorBinding* binding, QObject* parent) : QObject(parent), _binding(binding)
{
    _debugLayer = QSettings().value("Controllers/DebugLayer", false).toBool();
    _soundHaptics = QSettings().value("Controllers/SoundHaptics", false).toBool();
    _soundHapticsDsp = QSettings().value("Controllers/SoundHapticsDsp", false).toBool();
    _globalShortcuts = QSettings().value("Controllers/GlobalShortcuts", false).toBool();
    _pauseOnDisconnect = QSettings().value("Controllers/PauseOnDisconnect", false).toBool();
#ifdef ENABLE_CONTROLLERS
    // The user's device entries over the compiled-in database (device-database.md §7), before a device is matched
    {
        QFile user(QDir(UserControllersDir()).filePath("devices.user.json"));
        std::string error;
        if (user.exists() && user.open(QIODevice::ReadOnly) &&
            !DeviceDatabase::SetUser(user.readAll().toStdString(), error))
            qWarning("controllers: %s: %s", qPrintable(user.fileName()), error.c_str());
    }
    _service = std::make_unique<HostInputService>();
    // The window keyboard: its debug bindings are the debugger's shortcuts; it never drives a connector
    _service->Router().SetRole(kKeyboard, DeviceRole::DebuggerRemote);
    _service->SetHapticsDspDefaults(_soundHapticsDsp);
    _service->OnDeviceAdded({kKeyboard, tr("Keyboard").toStdString(), "keyboard", "keyboard", "qt"});
    std::vector<std::string> errors;
    _profiles.LoadDirectory(UserProfileDir().toStdString(), ProfileScope::User, errors);
    // The promoted devices' old ids (architecture.md §5): profiles naming a `gc:` id follow its IOHID id
    std::string aliasError;
    if (!_profiles.LoadAliases(UserProfileDir().toStdString(), aliasError))
        errors.push_back(aliasError);
    // The shipped game profiles: the index now, a profile when a loaded file or a manual choice names it
    _profiles.OpenGames(FileHelper::GetResourcesPath() + "/controllers/profiles/games", errors);
    for (const std::string& error : errors)
        qWarning("controllers: %s", error.c_str());
    _service->Router().SetActionSink([this](const ActionEvent& event) {
        HostActionEvent out;
        out.action = QString::fromStdString(event.action);
        out.pressed = event.pressed;
        out.value = event.value;
        out.device = QString::fromStdString(event.device);
        out.turn = event.turn;
        out.rate = event.rate;
        if (!event.params.IsNull())
            out.params = QString::fromStdString(event.params.Dump());
        QMetaObject::invokeMethod(this, [this, out] { emit actionTriggered(out); }, Qt::QueuedConnection);
    });
    // A windowless session opens no gamepad, HID device or Surface Dial: they are the person's at the desk
    if (WindowlessSessionUses("controllers"))
        _service->StartNative();
    // The host verbs of the automation surfaces run here, on the GUI thread
    _controlOwner = std::make_unique<ControlOwner>(*this);
    HostInputControl::SetOwner(_controlOwner.get(), [](const std::function<void()>& task) {
        QCoreApplication* app = QCoreApplication::instance();
        if (app == nullptr || QThread::currentThread() == app->thread())
            task();
        else
            QMetaObject::invokeMethod(app, [&task] { task(); }, Qt::BlockingQueuedConnection);
    });
    // The program loaded: its file's hash keys the game profiles, and the auto decision starts again
    _fileObserver = MessageCenter::DefaultMessageCenter().AddObserver(NC_FILE_LOADED, [this](int, Message* message) {
        auto* payload = message != nullptr ? dynamic_cast<FileLoadedPayload*>(message->obj) : nullptr;
        if (payload == nullptr || !payload->ok)
            return;
        const QString id = QString::fromStdString(payload->emulatorId.toString());
        const QString path = QString::fromStdString(payload->path);
        QMetaObject::invokeMethod(this, [this, id, path] { onFileLoaded(id, path); }, Qt::QueuedConnection);
    });
    // A program the guest read from a block medium (a NEX started from NextZXOS's Browser): the machine loaded it,
    // not this client, so the bytes the core read give the same keys
    _guestObserver = MessageCenter::DefaultMessageCenter().AddObserver(NC_GUEST_PROGRAM, [this](int, Message* message) {
        auto* payload = message != nullptr ? dynamic_cast<GuestProgramPayload*>(message->obj) : nullptr;
        if (payload == nullptr || payload->data.empty())
            return;
        const QString id = QString::fromStdString(payload->emulatorId.toString());
        const QString path = QString::fromStdString(payload->path);
        const QByteArray bytes(reinterpret_cast<const char*>(payload->data.data()),
                               static_cast<qsizetype>(payload->data.size()));
        QMetaObject::invokeMethod(this, [this, id, path, bytes] { onGuestProgram(id, path, bytes); },
                                  Qt::QueuedConnection);
    });
    // The context of an instance changed (ctx.set on any plane, the GUI's own panes): the router follows it
    _contextObserver = MessageCenter::DefaultMessageCenter().AddObserver(NC_SURFACE_CONTEXT, [this](int, Message*) {
        QMetaObject::invokeMethod(this, [this] { followContext(); }, Qt::QueuedConnection);
    });
    _breakpointObserver =
        MessageCenter::DefaultMessageCenter().AddObserver(NC_EXECUTION_BREAKPOINT, [this](int, Message* message) {
            auto* payload = message != nullptr ? dynamic_cast<BreakpointTriggeredPayload*>(message->obj) : nullptr;
            if (payload == nullptr || payload->hidden)
                return;
            const QString id = QString::fromStdString(payload->emulatorId.toString());
            QMetaObject::invokeMethod(this, [this, id] { onBreakpointHit(id); }, Qt::QueuedConnection);
        });
#endif
    connect(_binding, &EmulatorBinding::bound, this, &HostControllers::onBound);
    connect(_binding, &EmulatorBinding::unbound, this, &HostControllers::onUnbound);
    // ui-ux.md §7: a 250 ms poll of the revisions, nothing else while nothing changes
    connect(&_timer, &QTimer::timeout, this, &HostControllers::poll);
    _timer.start(250);
    if (_binding->emulator() != nullptr)
        onBound();
}

HostControllers::~HostControllers()
{
    if (_mainWindow != nullptr || _debuggerWindow != nullptr)
        qApp->removeEventFilter(this);
    onUnbound();
#ifdef ENABLE_CONTROLLERS
    HostInputControl::SetOwner(nullptr);
    MessageCenter::DefaultMessageCenter().RemoveObserverById(NC_FILE_LOADED, _fileObserver);
    MessageCenter::DefaultMessageCenter().RemoveObserverById(NC_GUEST_PROGRAM, _guestObserver);
    MessageCenter::DefaultMessageCenter().RemoveObserverById(NC_SURFACE_CONTEXT, _contextObserver);
    MessageCenter::DefaultMessageCenter().RemoveObserverById(NC_EXECUTION_BREAKPOINT, _breakpointObserver);
    releaseWheelMenu();
    _service->Router().SetActionSink(nullptr);
    _service->Stop();
#endif
}

void HostControllers::SetSoundHapticsDsp(bool on)
{
    if (on == _soundHapticsDsp)
        return;
    _soundHapticsDsp = on;
    QSettings().setValue("Controllers/SoundHapticsDsp", on);
#ifdef ENABLE_CONTROLLERS
    _service->SetHapticsDspDefaults(on);
    if (_attached)
        attachHaptics();  // the key below names the filters: the sound feed is made again
#endif
}

void HostControllers::SetSoundHaptics(bool on)
{
    if (on == _soundHaptics)
        return;
    _soundHaptics = on;
    QSettings().setValue("Controllers/SoundHaptics", on);
#ifdef ENABLE_CONTROLLERS
    if (_attached)
        attachHaptics();
#endif
}

void HostControllers::SetPauseOnDisconnect(bool on)
{
    _pauseOnDisconnect = on;
    QSettings().setValue("Controllers/PauseOnDisconnect", on);
}

void HostControllers::SetGlobalShortcuts(bool on)
{
    if (on == _globalShortcuts)
        return;
    _globalShortcuts = on;
    QSettings().setValue("Controllers/GlobalShortcuts", on);
}

void HostControllers::SetShortcutWindows(QWidget* main, QWidget* debugger)
{
    if (_mainWindow == nullptr && _debuggerWindow == nullptr)
    {
        qApp->installEventFilter(this);
        connect(qApp, &QApplication::focusChanged, this, &HostControllers::updateDebugFocus);
        connect(qApp, &QGuiApplication::focusWindowChanged, this, &HostControllers::updateDebugFocus);
    }
    _mainWindow = main;
    _debuggerWindow = debugger;
}

void HostControllers::SetContextWidgets(std::vector<std::pair<QWidget*, std::string>> widgets, QWidget* ttdPanel)
{
    _contextWidgets = std::move(widgets);
    _ttdPanel = ttdPanel;
    connect(qApp, &QApplication::focusChanged, this, &HostControllers::updateGuiContext, Qt::UniqueConnection);
}

void HostControllers::OnDebuggerVisible(bool visible)
{
    if (visible)
        return;
    _guiPane.clear();
    releaseGuiContext("debug.");
}

void HostControllers::OnTtdPanelVisible(bool visible)
{
    if (visible)
        setGuiContext(actions::ContextState::kTtd);
    else
        releaseGuiContext(actions::ContextState::kTtd);
}

void HostControllers::updateGuiContext()
{
    QWidget* focus = QApplication::focusWidget();
    std::string pane;
    for (const auto& [widget, context] : _contextWidgets)
        if (focus != nullptr && widget != nullptr && (widget == focus || widget->isAncestorOf(focus)))
            pane = context;
    if (pane.empty() && focus != nullptr && _ttdPanel != nullptr && (_ttdPanel == focus || _ttdPanel->isAncestorOf(focus)))
        pane = actions::ContextState::kTtd;
    if (pane == _guiPane)
        return;
    _guiPane = pane;
    if (!pane.empty())
        setGuiContext(pane);  // entering a pane sets its context once; another setter's later one stays until then
}

void HostControllers::setGuiContext(const std::string& context)
{
    Emulator* emulator = _binding->emulator();
    if (emulator == nullptr)
        return;
    std::string error;
    if (!actions::ContextState::Instance().Set(emulator->GetId(), emulator, context, "gui", error))
        qWarning("controllers: context '%s': %s", context.c_str(), error.c_str());
    followContext();
}

void HostControllers::releaseGuiContext(const std::string& prefix)
{
    Emulator* emulator = _binding->emulator();
    if (emulator == nullptr)
        return;
    const actions::ContextInfo info = actions::ContextState::Instance().Get(emulator->GetId(), emulator);
    if (info.set && info.by == "gui" && info.context.compare(0, prefix.size(), prefix) == 0)
        setGuiContext({});
}

void HostControllers::followContext()
{
#ifdef ENABLE_CONTROLLERS
    Emulator* emulator = _binding->emulator();
    if (emulator == nullptr)
        return;
    // The default (Q-7: `ttd` while a history exists) changes without a notification: the poll reads it too
    const actions::ContextInfo info = actions::ContextState::Instance().Get(emulator->GetId(), emulator);
    _service->Router().SetContext(emulator->GetId(), info.context);
#endif
}

void HostControllers::PlayMarks(const std::string& device, const std::vector<std::string>& marks)
{
#ifdef ENABLE_CONTROLLERS
    _service->PlayMarks(device, marks);
#else
    (void)device;
    (void)marks;
#endif
}

void HostControllers::updateDebugFocus()
{
#ifdef ENABLE_CONTROLLERS
    QWidget* active = QApplication::activeWindow();
    if (active == nullptr)
        return;  // another app has the focus: the pads keep what they drove
    QWidget* focus = QApplication::focusWidget();
    const bool on = _debuggerWindow != nullptr && _debuggerWindow->isVisible() &&
                    (active == _debuggerWindow || (focus != nullptr && _debuggerWindow->isAncestorOf(focus)));
    if (on == _debugFocus)
        return;
    _debugFocus = on;
    Reapply();
    _service->Router().SetDebugFocus(on);
    const std::vector<HostDeviceInfo> devices = _service->Router().Devices();
    const bool pads = std::any_of(devices.begin(), devices.end(), [this](const HostDeviceInfo& device) {
        return device.family != "keyboard" && device.family != "mouse" &&
               _service->Router().Role(device.id) == DeviceRole::Player;
    });
    if (pads)
        emit notice(on ? tr("Game pads drive the debugger") : tr("Game pads drive the game"));
#endif
}

std::string HostControllers::KeyControlOf(const QKeyEvent* event)
{
#ifdef ENABLE_CONTROLLERS
    const PcKey key = KeyboardManager::mapQtEventToPcKey(event);
    switch (key)
    {
        case PcKey::None:
        case PcKey::LeftShift:
        case PcKey::RightShift:
        case PcKey::LeftCtrl:
        case PcKey::RightCtrl:
        case PcKey::LeftAlt:
        case PcKey::RightAlt:
        case PcKey::LeftGui:
        case PcKey::RightGui:
            return {};
        default:
            break;
    }
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    uint8_t held = 0;
    if (modifiers.testFlag(Qt::ControlModifier))
        held |= kKeyCtrl;
    if (modifiers.testFlag(Qt::AltModifier))
        held |= kKeyAlt;
    if (modifiers.testFlag(Qt::ShiftModifier))
        held |= kKeyShift;
    if (modifiers.testFlag(Qt::MetaModifier))
        held |= kKeyMeta;
    return KeyControl(pckey::Name(key), held);
#else
    (void)event;
    return {};
#endif
}

bool HostControllers::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();
    if (type != QEvent::KeyPress && type != QEvent::KeyRelease && type != QEvent::ShortcutOverride)
        return QObject::eventFilter(watched, event);
    // The debugger window always, the main window with global shortcuts on; a dialog never
    QWidget* window = QApplication::activeWindow();
    if (window == nullptr || !(window == _debuggerWindow || (_globalShortcuts && window == _mainWindow)))
        return QObject::eventFilter(watched, event);
    auto* key = static_cast<QKeyEvent*>(event);
    if (type == QEvent::ShortcutOverride)
    {
        // A bound key comes as a key press, not as a menu shortcut of the window
#ifdef ENABLE_CONTROLLERS
        const std::string control = KeyControlOf(key);
        if (!control.empty() && _service->Router().Consumes(kKeyboard, control))
            event->accept();
#endif
        return QObject::eventFilter(watched, event);
    }
    return shortcutKey(key, type == QEvent::KeyPress) || QObject::eventFilter(watched, event);
}

bool HostControllers::shortcutKey(const QKeyEvent* event, bool pressed)
{
#ifdef ENABLE_CONTROLLERS
    const int code = static_cast<int>(KeyboardManager::mapQtEventToPcKey(event));
    auto held = _heldKeys.find(code);
    if (!pressed)
    {
        // An event that propagates to the parent comes again: the first one released it
        if (held == _heldKeys.end() || event->isAutoRepeat())
            return held != _heldKeys.end() && held->second.taken;
        const HeldKey key = held->second;
        _heldKeys.erase(held);
        _service->OnControl({kKeyboard, key.control, 0, 0});
        return key.taken;
    }
    if (held != _heldKeys.end())
        return held->second.taken;  // the auto-repeat, or the same press seen by a parent
    const std::string control = KeyControlOf(event);
    if (control.empty())
        return false;
    // A text field keeps its keys: there only the function keys are shortcuts
    QWidget* focus = QApplication::focusWidget();
    const auto* line = qobject_cast<QLineEdit*>(focus);
    const auto* text = qobject_cast<QTextEdit*>(focus);
    const auto* plain = qobject_cast<QPlainTextEdit*>(focus);
    const bool editing = (line != nullptr && !line->isReadOnly()) || (text != nullptr && !text->isReadOnly()) ||
                         (plain != nullptr && !plain->isReadOnly()) || qobject_cast<QAbstractSpinBox*>(focus);
    const PcKey key = static_cast<PcKey>(code);
    if (editing && !(key >= PcKey::Function1 && key <= PcKey::Function12))
        return false;
    const bool taken = _service->Router().Consumes(kKeyboard, control);
    _heldKeys[code] = {control, taken};
    _service->OnControl({kKeyboard, control, 1, 0});
    return taken;
#else
    (void)event;
    (void)pressed;
    return false;
#endif
}

void HostControllers::SetManualProfile(const std::string& id)
{
    if (id == _manualProfile)
        return;
    _manualProfile = id;
    Reapply();
}

void HostControllers::SetDebugLayer(bool on)
{
    if (on == _debugLayer)
        return;
    _debugLayer = on;
    QSettings().setValue("Controllers/DebugLayer", on);
    Reapply();
}

Emulator* HostControllers::BoundEmulator() const
{
    return _binding->emulator();
}

EmulatorContext* HostControllers::BoundContext() const
{
    return _binding->context();
}

void HostControllers::onBound()
{
#ifdef ENABLE_CONTROLLERS
    onUnbound();
    std::shared_ptr<Emulator> emulator = _binding->emulatorShared();
    EmulatorContext* context = _binding->context();
    if (!emulator || context == nullptr || context->pControllerManager == nullptr)
        return;
    _attached = emulator;
    context->pControllerManager->SetHostInputSource(_service->SourceFor(emulator->GetId()));
    _service->Router().SetFocus(emulator->GetId());
    followContext();
    Reapply();
#endif
}

void HostControllers::onUnbound()
{
#ifdef ENABLE_CONTROLLERS
    if (!_attached)
        return;
    EmulatorContext* context = _attached->GetContext();  // lifetime-ok: _attached holds the emulator
    if (context != nullptr && context->pControllerManager != nullptr)
    {
        context->pControllerManager->SetHostInputSource(nullptr);
        context->pControllerManager->SetHapticFeed(nullptr);
    }
    _hapticFeed.reset();
    _hapticsKey.clear();
    _attached.reset();
#endif
}

void HostControllers::poll()
{
#ifdef ENABLE_CONTROLLERS
    uint64_t machine = 0;
    if (EmulatorContext* context = _binding->context())
        if (context->pControllerManager != nullptr)
            machine = context->pControllerManager->Revision();
    checkAuto();
    updateHapticsGate();
    followContext();
    updateWheelMenu();  // a window may have got its native handle
    if (_service->Revision() == _revision && machine == _machineRevision)
        return;
    _revision = _service->Revision();
    _machineRevision = machine;
    const auto promotions = _service->TakePromotions();
    for (const auto& [from, to] : promotions)
        _profiles.AddAlias(from, to);
    if (!promotions.empty())
    {
        std::string error;
        if (!_profiles.SaveAliases(UserProfileDir().toStdString(), error))
            qWarning("controllers: %s", error.c_str());
    }

    const std::vector<HostDeviceInfo> devices = _service->Router().Devices();
    std::vector<HostDeviceInfo> arrived;
    for (const HostDeviceInfo& device : devices)
        if (device.id != kKeyboard && std::find(_known.begin(), _known.end(), device.id) == _known.end())
            arrived.push_back(device);
    std::vector<std::pair<std::string, bool>> gone;  // name, a player
    for (const HostDeviceId& id : _known)
        if (std::none_of(devices.begin(), devices.end(), [&id](const HostDeviceInfo& device) { return device.id == id; }))
        {
            const auto it = _knownNames.find(id);
            if (it != _knownNames.end())
                gone.push_back(it->second);
        }
    _known.clear();
    _knownNames.clear();
    const auto player = [](const HostDeviceInfo& device) {
        return device.family != "keyboard" && device.family != "mouse" &&
               DeviceDatabase::Active()->ForFamily(device.family, device.layout).player;
    };
    for (const HostDeviceInfo& device : devices)
    {
        _known.push_back(device.id);
        _knownNames[device.id] = {device.name, player(device)};
    }
    _wheelConnected = WheelClaim::AnyWheel(devices, *DeviceDatabase::Active());
    updateWheelMenu();
    Reapply();  // the arrived pads have their player connectors now

    // ui-ux.md §8: the first connection of a pad on this computer gets the toast, a known one the status line
    QStringList seen = QSettings().value("Controllers/SeenDevices").toStringList();
    for (const HostDeviceInfo& device : arrived)
    {
        const QString name = QString::fromStdString(device.name);
        if (!player(device))
        {
            emit notice(tr("%1 connected").arg(name));
            continue;
        }
        // Seen by family and name, not by id: a Bluetooth pad gets a new id on another adapter
        const QString key = QString::fromStdString(device.family + "|" + device.name);
        const bool first = !seen.contains(key);
        if (first)
            seen << key;
        QString connector;
        const std::string plugged = _service->Router().PlayerConnector(device.id);
        if (EmulatorContext* context = _binding->context(); context != nullptr && !plugged.empty() &&
                                                           context->pControllerManager != nullptr)
        {
            const int index = context->pControllerManager->Find(plugged);
            if (index >= 0)
                connector = connectorview::DisplayName(context->pControllerManager->At(static_cast<size_t>(index)));
        }
        // A pad the database does not know and nobody taught yet: the toast offers the learn wizard
        const bool unknown = device.family == "generic" && _service->ControlMap(device.id).empty();
        if (first && unknown)
            emit unknownDeviceConnected(name, QString::fromStdString(device.id));
        emit deviceConnected(name, connector, first && !unknown);
        emit notice(connector.isEmpty() ? tr("%1 connected").arg(name) : tr("%1 connected → %2").arg(name, connector));
    }
    if (!arrived.empty())
        QSettings().setValue("Controllers/SeenDevices", seen);

    // A pad gone while the machine runs: said at once, and the machine paused when the option asks for it
    Emulator* emulator = _binding->emulator();
    for (const auto& [name, isPlayer] : gone)
    {
        if (!isPlayer)
            continue;
        const bool running = emulator != nullptr && !emulator->IsPaused();
        bool paused = false;
        if (running && _pauseOnDisconnect)
        {
            // Applied, or queued behind a running command (it applies when that ends)
            const execution::Outcome outcome =
                emulator->Pause(execution::Requester{execution::Origin::Gui, {}}, "controller disconnected").outcome;
            paused = outcome == execution::Outcome::Applied || outcome == execution::Outcome::Pending;
        }
        if (running)
            emit deviceDisconnected(QString::fromStdString(name), paused);
        emit notice(tr("%1 disconnected").arg(QString::fromStdString(name)));
    }
#endif
}

connectorview::Led HostControllers::ConnectorLed() const
{
#ifdef ENABLE_CONTROLLERS
    EmulatorContext* context = _binding->context();
    ControllerManager* manager = context != nullptr ? context->pControllerManager : nullptr;
    if (manager == nullptr)
        return {};
    const std::vector<Connector> connectors = manager->Connectors();
    std::vector<uint16_t> states;
    for (size_t i = 0; i < connectors.size(); ++i)
        states.push_back(manager->State(i));
    std::map<std::string, QStringList> drivers;
    for (const HostDeviceInfo& device : _service->Router().Devices())
        for (const std::string& connector : _service->Router().Connectors(device.id))
            drivers[connector] << QString::fromStdString(device.name);
    return connectorview::DescribeLed(connectors, states, drivers);
#else
    return {};
#endif
}

bool HostControllers::DebugLayerHeld() const
{
#ifdef ENABLE_CONTROLLERS
    if (_debugFocus)
        return false;  // the debugger window has the pads: no key is held for it
    for (const HostDeviceInfo& device : _service->Router().Devices())
    {
        if (device.id == kKeyboard || _service->Router().Role(device.id) != DeviceRole::Player)
            continue;
        const std::vector<std::string> layers = _service->Router().Layers(device.id);
        if (std::find(layers.begin(), layers.end(), "debug") != layers.end())
            return true;
    }
#endif
    return false;
}

void HostControllers::Reapply()
{
#ifdef ENABLE_CONTROLLERS
    ProfileContext context;
    context.devices = _service->Router().Devices();
    if (EmulatorContext* ctx = _binding->context())
        if (const TMemModel* model = Config::FindModelByEnum(ctx->config.mem_model))
            context.machine = model->ShortName ? model->ShortName : "";
    std::transform(context.machine.begin(), context.machine.end(), context.machine.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    context.signatures = _program.signatures;
    context.fileSha256 = _program.fileSha256;
    context.fileSet = _program.fileSet;
    if (!_manualProfile.empty())
        context.manual = {_manualProfile};
    context.debugLayer = _debugLayer || _debugFocus;
    assignPlayers(context.devices);
    _resolved = _profiles.Apply(_service->Router(), context);
    // The user's drag and drop (the player verb) wins over the machine default and a profile's pin
    for (const HostDeviceInfo& device : context.devices)
        if (const auto choice = _playerChoice.find(device.id); choice != _playerChoice.end())
            _service->Router().SetPlayerConnector(device.id, choice->second);
    _context = context;
    applyConnectorPlans();
    attachHaptics();
    for (const std::string& warning : _resolved.warnings)
        qWarning("controllers: %s", warning.c_str());
    emit changed();
#endif
}

#ifdef ENABLE_CONTROLLERS
void HostControllers::updateWheelMenu()
{
#ifdef Q_OS_WIN
    // Only the Windows platform's window ids are HWNDs (offscreen hands out its own); a window without a native
    // handle yet is skipped (internalWinId does not create one) and taken once it has one
    std::vector<WheelClaim::Window> windows;
    if (QGuiApplication::platformName() == QLatin1String("windows"))
        for (QWidget* widget : {_mainWindow, _debuggerWindow})
            if (widget != nullptr)
                windows.push_back(static_cast<WheelClaim::Window>(widget->window()->internalWinId()));
    const WheelClaim::Changes changes = _wheelClaim.Update(_wheelConnected, windows);
    for (WheelClaim::Window window : changes.release)
    {
        WheelMenuWindows::Release(_wheelHolds[window]);
        _wheelHolds.erase(window);
    }
    for (WheelClaim::Window window : changes.take)
        _wheelHolds[window] = WheelMenuWindows::Take(reinterpret_cast<void*>(window));
#endif
}

void HostControllers::releaseWheelMenu()
{
    for (WheelClaim::Window window : _wheelClaim.ReleaseAll().release)
        WheelMenuWindows::Release(_wheelHolds[window]);
    _wheelHolds.clear();
}

void HostControllers::attachHaptics()
{
    if (!_attached)
        return;
    EmulatorContext* context = _attached->GetContext();  // lifetime-ok: _attached holds the emulator
    if (context == nullptr || context->pControllerManager == nullptr)
        return;
    HapticProfile profile;
    std::string error;
    const bool game = !_resolved.haptics.IsNull() && ParseHapticProfile(_resolved.haptics, profile, error);
    if (!error.empty())
        qWarning("controllers: %s: %s", _resolved.hapticsProfile.c_str(), error.c_str());
    const std::string key = game ? "profile:" + _resolved.hapticsProfile + ":" + _resolved.haptics.Dump()
                                 : (_soundHaptics ? std::string(_soundHapticsDsp ? "sound+dsp" : "sound")
                                                  : std::string());
    if (key == _hapticsKey && (key.empty() || _hapticFeed))
        return;
    _hapticsKey = key;
    if (game)
        _hapticFeed = _service->HapticFeedFor(_attached->GetId(), profile, _resolved.hapticsProfile);
    else if (_soundHaptics)
        _hapticFeed = _service->HapticFeedFor(_attached->GetId());
    else
        _hapticFeed.reset();
    context->pControllerManager->SetHapticFeed(_hapticFeed);
}

QString HostControllers::HapticsStatus(const std::string& device) const
{
    QString text;
    if (auto* game = dynamic_cast<const ProfileHaptics*>(_hapticFeed.get()))
    {
        text = tr("Profile %1").arg(QString::fromStdString(game->Name()));
        QStringList sources;
        for (const auto& [name, level] : game->SourceLevels())
            sources << QString("%1 %2").arg(QString::fromStdString(name)).arg(level, 0, 'f', 2);
        if (!sources.isEmpty())
            text += "\n" + tr("Sources: %1").arg(sources.join(", "));
    }
    else if (_hapticFeed)
        text = tr("From the sound (AY noise / envelope, beeper)");
    else
        text = tr("Off");
    text += "\n" + (_service->Haptics().GateOpen() ? tr("Gate open") : tr("Gate closed (paused, turbo, replay or "
                                                                            "no focus)"));
    if (!device.empty() && _service->HasRumble(device))
    {
        const RumbleLevels levels = _service->Haptics().Written(device);
        text += "\n" + tr("Motors: low %1, high %2, triggers %3 / %4")
                           .arg(levels.low, 0, 'f', 2)
                           .arg(levels.high, 0, 'f', 2)
                           .arg(levels.leftTrigger, 0, 'f', 2)
                           .arg(levels.rightTrigger, 0, 'f', 2);
    }
    return text;
}

QString HostControllers::UserControllersDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath("unreal-ng/controllers");
}

QString HostControllers::UserProfileDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath("unreal-ng/controllers/profiles");
}

/// retro-devices.md §4, the machine default: the n-th pad takes the n-th of next.left, next.right, kempston,
/// sinclair-1, sinclair-2 the machine has with a device; none of them: a cursor joystick on a free keyboard socket
void HostControllers::assignPlayers(const std::vector<HostDeviceInfo>& devices)
{
    EmulatorContext* context = _binding->context();
    ControllerManager* manager = context != nullptr ? context->pControllerManager : nullptr;
    if (manager == nullptr)
        return;
    std::vector<std::string> order;
    if (!_autoConnector.empty() && manager->Find(_autoConnector) >= 0)
        order.push_back(_autoConnector);  // the program's choice (port reads) before the machine default
    const std::vector<Connector> connectors = manager->Connectors();
    for (const char* id : {"next.left", "next.right", "kempston", "sinclair-1", "sinclair-2"})
        for (const Connector& connector : connectors)
            if (connector.id == id && connector.device != RetroDeviceKind::None &&
                std::find(order.begin(), order.end(), connector.id) == order.end())
                order.push_back(connector.id);
    for (const Connector& connector : connectors)
        if (connector.kind == ConnectorKind::KeyboardJoystick && connector.device != RetroDeviceKind::None &&
            std::find(order.begin(), order.end(), connector.id) == order.end())
            order.push_back(connector.id);

    // A keyboard, a mouse and a control surface (the Surface Dial, `"player": false` in the device database) take
    // no player connector
    const auto player = [](const HostDeviceInfo& device) {
        return device.family != "keyboard" && device.family != "mouse" &&
               DeviceDatabase::Active()->ForFamily(device.family, device.layout).player;
    };
    const size_t pads = static_cast<size_t>(std::count_if(devices.begin(), devices.end(), player));
    if (order.empty() && pads > 0)
    {
        ControllerControl control(context);
        ControllerRequest request{"plug", "auto", "cursor-joystick", {{"async", ""}}};
        const ControllerReply reply = control.Execute(request);
        if (reply.result.Ok())
            order.push_back(reply.connector);
    }
    size_t n = 0;
    for (const HostDeviceInfo& device : devices)
    {
        // A control surface starts as a debugger remote by its family's role (HostInputService, the device
        // database's `role`): its debug profile acts with no layer key
        if (!player(device))
            continue;
        _service->Router().SetPlayerConnector(device.id, n < order.size() ? order[n] : std::string{});
        ++n;
    }
}

std::string HostControllers::ChoosePlayerConnector(const std::string& device, const std::string& connector,
                                                   std::string& message)
{
    const std::vector<HostDeviceInfo> devices = _service->Router().Devices();
    const auto info = std::find_if(devices.begin(), devices.end(),
                                   [&device](const HostDeviceInfo& known) { return known.id == device; });
    if (info == devices.end())
    {
        message = "no connected host device '" + device + "' (devices lists them)";
        return "unknown-device";
    }
    if (info->family == "keyboard" || info->family == "mouse" ||
        !DeviceDatabase::Active()->ForFamily(info->family, info->layout).player)
    {
        message = info->name + " takes no player connector: its bindings name their targets";
        return "bad-request";
    }
    if (connector.empty())
    {
        _playerChoice.erase(device);
        Reapply();
        return {};
    }
    if (connector == "none")
    {
        // Unplugged by hand (a cord dragged off its connector): the device drives no player connector
        _playerChoice[device].clear();
        Reapply();
        return {};
    }
    EmulatorContext* context = _binding->context();
    ControllerManager* manager = context != nullptr ? context->pControllerManager : nullptr;
    if (manager == nullptr)
    {
        message = "no machine is bound: start one first";
        return "unavailable";
    }
    const int index = manager->Find(connector);
    if (index < 0)
    {
        message = "no connector '" + connector + "' on this machine (list names them)";
        return "bad-request";
    }
    const Connector found = manager->At(static_cast<size_t>(index));
    if (found.device == RetroDeviceKind::None)
    {
        message = found.id + " holds no device: plug one first (plug " + found.id + " kempston-joystick)";
        return "bad-request";
    }
    _playerChoice[device] = found.id;
    Reapply();
    return {};
}

void HostControllers::ResetPlayerChoices()
{
    _playerChoice.clear();
    Reapply();
}

std::string HostControllers::PlayerChoice(const std::string& device) const
{
    const auto it = _playerChoice.find(device);
    if (it == _playerChoice.end())
        return {};
    return it->second.empty() ? std::string("none") : it->second;
}

HostReply HostControllers::Host(const HostRequest& request)
{
    return HostInputControl(*_controlOwner).Execute(request);
}

bool HostControllers::saveUserProfile(Profile profile, QString& error)
{
    // Validate through the same reader the files go through
    Profile checked;
    std::string why;
    const std::string text = ProfileStore::Dump(profile);
    if (!ParseProfile(text, checked, why) || !ProfileStore::Save(checked, UserProfileDir().toStdString(), why))
    {
        error = QString::fromStdString(why);
        return false;
    }
    checked.scope = ProfileScope::User;
    if (!_profiles.Add(checked, why))
    {
        error = QString::fromStdString(why);
        return false;
    }
    Reapply();
    return true;
}

bool HostControllers::KeepAutoDecision(QString& error)
{
    if (_program.fileSha256.empty() || _autoConnector.empty())
    {
        error = tr("Nothing to keep: no program loaded from a file, or no automatic choice");
        return false;
    }
    // Keyed by the signatures (D7): the content one finds the program re-packed or renamed, the file set after saves,
    // the file hash as before (and as the profiles of older versions did)
    std::string content;
    for (const std::string& signature : _program.signatures)
        if (signature.rfind("file:", 0) != 0)
            content = signature;
    const std::string& sha = _program.fileSha256.front();
    const std::string key = content.empty() ? sha : content.substr(content.rfind(':') + 1);
    Profile profile;
    profile.kind = ProfileKind::Play;
    profile.scope = ProfileScope::User;
    profile.id = "user/game-" + key.substr(0, 16);
    profile.label = _fileName.toStdString();
    profile.fileSha256 = {sha};
    if (!content.empty())
        profile.signatures = {content};
    profile.fileSet = _program.fileSet;
    profile.options = JsonValue::Object();
    ConnectorPlan plan;
    plan.device = _decision.device;
    plan.options = JsonValue::Object();
    if (!_decision.option.empty())
        plan.options.Set(_decision.option, JsonValue::String(_decision.optionValue));
    profile.connectors[_autoConnector] = plan;
    profile.players = {{"auto:1", _autoConnector}};
    return saveUserProfile(std::move(profile), error);
}

bool HostControllers::AddHapticProbe(const HapticProbe& probe, QString& name, QString& error)
{
    // The surfaces' haptics_probe verb, so the menu and an agent write the same profile
    static const char* const kKinds[] = {"exec", "read", "write", "port-in", "port-out"};
    HostRequest request;
    request.verb = "haptics_probe";
    request.options["kind"] = kKinds[static_cast<size_t>(probe.kind)];
    request.options["address"] = std::to_string(probe.address);
    if (probe.page != 0xFFFF)
        request.options["page"] = std::to_string(probe.page);
    if (probe.kind == HapticProbeKind::PortIn || probe.kind == HapticProbeKind::PortOut)
        request.options["port_mask"] = std::to_string(probe.portMask);
    const HostReply reply = Host(request);
    if (!reply.Ok())
    {
        error = QString::fromStdString(reply.message);
        return false;
    }
    const JsonValue* source = reply.body.Find("source");
    name = source != nullptr ? QString::fromStdString(source->AsString()) : QString();
    return true;
}

void HostControllers::updateHapticsGate()
{
    Emulator* emulator = _binding->emulator();
    EmulatorContext* context = _binding->context();
    bool open = emulator != nullptr && context != nullptr && emulator->IsRunning() && !emulator->IsPaused() &&
                !emulator->IsTurboMode() && QGuiApplication::applicationState() == Qt::ApplicationActive;
    if (open && (context->rzxPlayer != nullptr ||
                 (context->pTimeTravelHooks != nullptr && context->pTimeTravelHooks->OwnsInput())))
        open = false;  // history and RZX replay what was, the haptics stay quiet
    _service->SetHapticsGate(open);
}

void HostControllers::onBreakpointHit(const QString& emulatorId)
{
    Emulator* emulator = _binding->emulator();
    if (!_debugLayer || emulator == nullptr || QString::fromStdString(emulator->GetId()) != emulatorId)
        return;
    constexpr uint32_t kPulseMs = 80;
    for (const HostDeviceInfo& device : _service->Router().Devices())
        if (_service->HasRumble(device.id))
            _service->PlayEffect(device.id, {{0.5f, 0.5f, 0.0f, 0.0f}, kPulseMs, true});
}

void HostControllers::onFileLoaded(const QString& emulatorId, const QString& path)
{
    Emulator* emulator = _binding->emulator();
    if (emulator == nullptr || QString::fromStdString(emulator->GetId()) != emulatorId)
        return;
    _fileName = QFileInfo(path).fileName();
    std::string error;
    if (!ProgramSignatures(path.toStdString(), _program, error))
        qWarning("controllers: signatures of %s: %s", qUtf8Printable(path), error.c_str());
    _autoDone = false;
    _keyboardNoticed = false;
    _autoConnector.clear();
    _decision = {};
    Reapply();
}

void HostControllers::onGuestProgram(const QString& emulatorId, const QString& path, const QByteArray& data)
{
    Emulator* emulator = _binding->emulator();
    if (emulator == nullptr || QString::fromStdString(emulator->GetId()) != emulatorId)
        return;
    _fileName = QFileInfo(path).fileName();
    std::string error;
    if (!ProgramSignatures(reinterpret_cast<const uint8_t*>(data.constData()), static_cast<size_t>(data.size()),
                           _fileName.toStdString(), _program, error))
        qWarning("controllers: signatures of %s (read by the guest): %s", qUtf8Printable(path), error.c_str());
    _autoDone = false;
    _keyboardNoticed = false;
    _autoConnector.clear();
    _decision = {};
    Reapply();
}

void HostControllers::applyConnectorPlans()
{
    EmulatorContext* context = _binding->context();
    if (context == nullptr || context->pControllerManager == nullptr || _resolved.connectors.empty())
        return;
    ControllerManager* manager = context->pControllerManager;
    for (const auto& [id, plan] : _resolved.connectors)
    {
        const int index = manager->Find(id);
        if (index < 0)
            continue;
        if (RetroDeviceKindName(manager->At(static_cast<size_t>(index)).device) != plan.device)
        {
            ControllerRequest request{"plug", id, plan.device, {{"async", ""}}};
            for (const auto& [name, value] : plan.options.GetMembers())
                request.options[name] = value.IsString() ? value.AsString()
                                        : value.IsBool() ? std::string(value.AsBool() ? "true" : "false")
                                                         : std::to_string(static_cast<long long>(value.AsNumber()));
            const ControllerReply reply = ControllerControl(context).Execute(request);
            if (!reply.result.Ok())
                qWarning("controllers: %s: %s", id.c_str(), reply.result.message.c_str());
        }
    }
    // A kept game profile holds the connectors: the port reads decide nothing more for this program
    if (!_autoDone)
    {
        _autoDone = true;
        manager->WatchPortReads(false);
    }
}

void HostControllers::checkAuto()
{
    if (_autoDone)
        return;
    EmulatorContext* context = _binding->context();
    if (context == nullptr || context->pControllerManager == nullptr)
        return;
    const PortReadEvidence evidence = context->pControllerManager->PortReads();
    if (!evidence.active)
        return;
    const AutoDecision decision = DecideFromPortReads(evidence);
    if (decision.keyboard)
    {
        // A menu or a text prompt reads every key: say so once and keep watching, the joystick comes later
        if (!_keyboardNoticed)
        {
            _keyboardNoticed = true;
            emit notice(tr("This program reads the whole keyboard"));
        }
        return;
    }
    if (!decision.decisive)
        return;
    applyAuto(decision);
}

void HostControllers::applyAuto(const AutoDecision& chosen)
{
    EmulatorContext* context = _binding->context();
    ControllerManager* manager = context != nullptr ? context->pControllerManager : nullptr;
    if (manager == nullptr)
        return;
    AutoDecision decision = chosen;
    // The game's profile knows the keys of its QAOP-style joystick (its fire key may be M)
    if (decision.device == "keys-joystick" && !_resolved.joystickKeys.empty())
        decision.optionValue = _resolved.joystickKeys;
    _autoDone = true;
    manager->WatchPortReads(false);
    if (decision.device == "kempston-mouse")
    {
        emit notice(tr("This program reads the Kempston mouse (%1)").arg(QString::fromStdString(decision.reason)));
        return;
    }
    std::string connector = decision.connector;
    if (!connector.empty())
    {
        if (manager->Find(connector) < 0)
            return;  // the machine has no such connector: the default stays
    }
    else
    {
        ControllerRequest request{"plug", "auto", decision.device, {{"async", ""}}};
        if (!decision.option.empty())
            request.options[decision.option] = decision.optionValue;
        const ControllerReply reply = ControllerControl(context).Execute(request);
        if (!reply.result.Ok())
        {
            emit notice(tr("%1: %2").arg(QString::fromStdString(decision.label),
                                          QString::fromStdString(reply.result.message)));
            return;
        }
        connector = reply.connector;
    }
    _decision = decision;
    _autoConnector = connector;
    Reapply();
    emit autoDecided(tr("%1 on %2 (port reads: %3)")
                         .arg(QString::fromStdString(decision.label), QString::fromStdString(connector),
                              QString::fromStdString(decision.reason)));
}
#endif
