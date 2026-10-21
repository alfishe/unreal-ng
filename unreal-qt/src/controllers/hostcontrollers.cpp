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
#include "emulator/keyboardmanager.h"

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

using namespace controllers;

namespace
{
constexpr const char* kKeyboard = "kbd:system";

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

private:
    HostControllers& _host;
};
}  // namespace
#endif

HostControllers::HostControllers(EmulatorBinding* binding, QObject* parent) : QObject(parent), _binding(binding)
{
    _debugLayer = QSettings().value("Controllers/DebugLayer", false).toBool();
    _soundHaptics = QSettings().value("Controllers/SoundHaptics", false).toBool();
    _globalShortcuts = QSettings().value("Controllers/GlobalShortcuts", false).toBool();
#ifdef ENABLE_CONTROLLERS
    _service = std::make_unique<HostInputService>();
    // The window keyboard: its debug bindings are the debugger's shortcuts; it never drives a connector
    _service->Router().SetRole(kKeyboard, DeviceRole::DebuggerRemote);
    _service->OnDeviceAdded({kKeyboard, tr("Keyboard").toStdString(), "keyboard", "keyboard", "qt"});
    std::vector<std::string> errors;
    _profiles.LoadDirectory(UserProfileDir().toStdString(), ProfileScope::User, errors);
    // The shipped game profiles: the index now, a profile when a loaded file or a manual choice names it
    _profiles.OpenGames(FileHelper::GetResourcesPath() + "/controllers/profiles/games", errors);
    for (const std::string& error : errors)
        qWarning("controllers: %s", error.c_str());
    _service->Router().SetActionSink([this](const ActionEvent& event) {
        const QString action = QString::fromStdString(event.action);
        const bool pressed = event.pressed;
        const float value = event.value;
        const QString device = QString::fromStdString(event.device);
        QMetaObject::invokeMethod(
            this, [this, action, pressed, value, device] { emit actionTriggered(action, pressed, value, device); },
            Qt::QueuedConnection);
    });
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
    MessageCenter::DefaultMessageCenter().RemoveObserverById(NC_EXECUTION_BREAKPOINT, _breakpointObserver);
    _service->Router().SetActionSink(nullptr);
    _service->Stop();
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
    if (_service->Revision() == _revision && machine == _machineRevision)
        return;
    _revision = _service->Revision();
    _machineRevision = machine;

    const std::vector<HostDeviceInfo> devices = _service->Router().Devices();
    for (const HostDeviceInfo& device : devices)
        if (device.id != kKeyboard && std::find(_known.begin(), _known.end(), device.id) == _known.end())
            emit notice(tr("%1 connected").arg(QString::fromStdString(device.name)));
    _known.clear();
    for (const HostDeviceInfo& device : devices)
        _known.push_back(device.id);
    Reapply();
#endif
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
    if (!_fileSha256.empty())
        context.fileSha256 = {_fileSha256};
    if (!_manualProfile.empty())
        context.manual = {_manualProfile};
    context.debugLayer = _debugLayer || _debugFocus;
    assignPlayers(context.devices);
    _resolved = _profiles.Apply(_service->Router(), context);
    _context = context;
    applyConnectorPlans();
    attachHaptics();
    for (const std::string& warning : _resolved.warnings)
        qWarning("controllers: %s", warning.c_str());
    emit changed();
#endif
}

#ifdef ENABLE_CONTROLLERS
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
                                 : (_soundHaptics ? std::string("sound") : std::string());
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
               DeviceDatabase::Builtin().ForFamily(device.family, device.layout).player;
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
        // A control surface is a debugger remote (debug-and-ttd.md §4.4): its debug profile acts with no layer key
        if (device.family != "keyboard" && device.family != "mouse" && !player(device) &&
            _service->Router().Role(device.id) == DeviceRole::Player)
            _service->Router().SetRole(device.id, DeviceRole::DebuggerRemote);
        if (!player(device))
            continue;
        _service->Router().SetPlayerConnector(device.id, n < order.size() ? order[n] : std::string{});
        ++n;
    }
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
    if (_fileSha256.empty() || _autoConnector.empty())
    {
        error = tr("Nothing to keep: no program loaded from a file, or no automatic choice");
        return false;
    }
    Profile profile;
    profile.kind = ProfileKind::Play;
    profile.scope = ProfileScope::User;
    profile.id = "user/game-" + _fileSha256.substr(0, 16);
    profile.label = _fileName.toStdString();
    profile.fileSha256 = {_fileSha256};
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
    _fileSha256.clear();
    _fileName = QFileInfo(path).fileName();
    QFile file(path);
    if (QFileInfo(path).isFile() && file.open(QIODevice::ReadOnly))
    {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (hash.addData(&file))
            _fileSha256 = hash.result().toHex().toStdString();
    }
    _autoDone = false;
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
    if (decision.decisive || decision.keyboard)
        applyAuto(decision);
}

void HostControllers::applyAuto(const AutoDecision& decision)
{
    EmulatorContext* context = _binding->context();
    ControllerManager* manager = context != nullptr ? context->pControllerManager : nullptr;
    if (manager == nullptr)
        return;
    _autoDone = true;
    manager->WatchPortReads(false);
    if (decision.keyboard)
    {
        emit notice(tr("This program reads the whole keyboard"));
        return;
    }
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
