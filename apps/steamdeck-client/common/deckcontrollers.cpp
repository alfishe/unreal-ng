#include "deckcontrollers.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <thread>

#include "3rdparty/digestpp/digestpp.hpp"
#include "3rdparty/message-center/messagecenter.h"
#include "debugger/ttd/timetravelhooks.h"
#include "devicedatabase.h"
#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/controllers/controllercontrol.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/notifications.h"
#include "haptics/hapticprofile.h"
#include "hostinputcontrol.h"
#include "hostinputservice.h"
#include "profiles/autodecision.h"
#include "profiles/profilestore.h"
#include "router/inputrouter.h"

using namespace controllers;

namespace deckpoc
{

namespace
{
std::string UserProfileDirectory()
{
    const char* config = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    const std::string base = config != nullptr && *config != '\0' ? config
                             : home != nullptr                    ? std::string(home) + "/.config"
                                                                  : std::string(".config");
    return base + "/unreal-ng/controllers/profiles";
}

std::string FileSha256(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    const std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return digestpp::sha256().absorb(data.data(), data.size()).hexdigest();
}
}  // namespace

class DeckControllers::Impl : public IHostInputOwner
{
public:
    Impl(Emulator& emulator, Options options) : _emulator(emulator), _options(options)
    {
        _service = std::make_unique<HostInputService>();
        std::vector<std::string> errors;
        _profiles.LoadDirectory(UserProfileDir(), ProfileScope::User, errors);
        const char* base = SDL_GetBasePath();
        _profiles.OpenGames(std::string(base != nullptr ? base : "") + "controllers/profiles/games", errors);
        for (const std::string& error : errors)
            SDL_Log("controllers: %s", error.c_str());
        _service->Router().SetActionSink([this](const ActionEvent& event) {
            std::lock_guard lock(_actionsMutex);
            if (_actionSink)
                _actionSink(event.action, event.pressed);
        });
        _service->StartNative();
        _owner = std::this_thread::get_id();
        HostInputControl::SetOwner(this, [this](const std::function<void()>& task) { Run(task); });
        _fileObserver =
            MessageCenter::DefaultMessageCenter().AddObserver(NC_FILE_LOADED, [this](int, Message* message) {
                auto* payload = message != nullptr ? dynamic_cast<FileLoadedPayload*>(message->obj) : nullptr;
                if (payload == nullptr || !payload->ok || payload->emulatorId.toString() != _emulator.GetId())
                    return;
                std::lock_guard lock(_tasksMutex);
                _loadedFile = payload->path;
                _fileChanged = true;
            });
        EmulatorContext* context = _emulator.GetContext();
        if (context != nullptr && context->pControllerManager != nullptr)
            context->pControllerManager->SetHostInputSource(_service->SourceFor(_emulator.GetId()));
        _service->Router().SetFocus(_emulator.GetId());
        Reapply();
    }

    ~Impl() override
    {
        HostInputControl::SetOwner(nullptr);
        MessageCenter::DefaultMessageCenter().RemoveObserverById(NC_FILE_LOADED, _fileObserver);
        {
            // A surface waiting on a task: let it go
            std::lock_guard lock(_tasksMutex);
            _closing = true;
        }
        _tasksDone.notify_all();
        EmulatorContext* context = _emulator.GetContext();
        if (context != nullptr && context->pControllerManager != nullptr)
        {
            context->pControllerManager->SetHostInputSource(nullptr);
            context->pControllerManager->SetHapticFeed(nullptr);
        }
        _hapticFeed.reset();
        _service->Router().SetActionSink(nullptr);
        _service->Stop();
    }

    // region <IHostInputOwner>
    ProfileStore& Profiles() override
    {
        return _profiles;
    }
    ProfileContext Context() const override
    {
        return _context;
    }
    const ResolvedProfiles& Resolved() const override
    {
        return _resolved;
    }
    std::string UserProfileDir() const override
    {
        return UserProfileDirectory();
    }
    std::string ManualProfile() const override
    {
        return _manualProfile;
    }
    void SetManualProfile(const std::string& id) override
    {
        _manualProfile = id;
        Reapply();
    }
    HostInputService* Input() override
    {
        return _service.get();
    }

    void Reapply() override
    {
        ProfileContext context;
        context.devices = _service->Router().Devices();
        EmulatorContext* emulator = _emulator.GetContext();
        if (emulator != nullptr)
            if (const TMemModel* model = Config::FindModelByEnum(emulator->config.mem_model))
                context.machine = model->ShortName ? model->ShortName : "";
        std::transform(context.machine.begin(), context.machine.end(), context.machine.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!_fileSha256.empty())
            context.fileSha256 = {_fileSha256};
        if (!_manualProfile.empty())
            context.manual = {_manualProfile};
        AssignPlayers(context.devices);
        _resolved = _profiles.Apply(_service->Router(), context);
        _context = context;
        ApplyConnectorPlans();
        AttachHaptics();
        for (const std::string& warning : _resolved.warnings)
            SDL_Log("controllers: %s", warning.c_str());
    }
    // endregion </IHostInputOwner>

    void Poll(bool focused)
    {
        std::deque<std::function<void()>> tasks;
        std::string loaded;
        bool fileChanged = false;
        {
            std::lock_guard lock(_tasksMutex);
            tasks.swap(_tasks);
            fileChanged = _fileChanged;
            loaded = _loadedFile;
            _fileChanged = false;
        }
        for (const std::function<void()>& task : tasks)
            task();
        if (!tasks.empty())
        {
            {
                std::lock_guard lock(_tasksMutex);
                _tasksRun += tasks.size();
            }
            _tasksDone.notify_all();
        }
        if (fileChanged)
            OnFileLoaded(loaded);

        CheckAuto();
        UpdateGate(focused);
        uint64_t machine = 0;
        if (EmulatorContext* context = _emulator.GetContext())
            if (context->pControllerManager != nullptr)
                machine = context->pControllerManager->Revision();
        if (_service->Revision() == _revision && machine == _machineRevision)
            return;
        _revision = _service->Revision();
        _machineRevision = machine;
        for (const HostDeviceInfo& device : _service->Router().Devices())
            if (std::find(_known.begin(), _known.end(), device.id) == _known.end())
                SDL_Log("controllers: %s connected (%s, %s)", device.name.c_str(), device.family.c_str(),
                        device.backend.c_str());
        _known.clear();
        for (const HostDeviceInfo& device : _service->Router().Devices())
            _known.push_back(device.id);
        Reapply();
        for (const std::string& line : Describe())
            SDL_Log("controllers: %s", line.c_str());
    }

    void SetActionSink(std::function<void(const std::string&, bool)> sink)
    {
        std::lock_guard lock(_actionsMutex);
        _actionSink = std::move(sink);
    }

    HostInputService& Service()
    {
        return *_service;
    }

    std::vector<std::string> Describe() const
    {
        std::vector<std::string> lines;
        for (const HostDeviceInfo& device : _service->Router().Devices())
        {
            const auto it = _players.find(device.id);
            const std::string connector = it != _players.end() ? it->second : std::string{};
            lines.push_back(device.id + " " + device.name + " -> " + (connector.empty() ? "no connector" : connector) +
                            (_service->HasRumble(device.id) ? ", haptics" : ""));
        }
        for (const std::string& profile : _resolved.applied)
            lines.push_back("profile " + profile);
        return lines;
    }

private:
    /// A surface's request runs on the Poll thread; it waits until it ran (or the client closes)
    void Run(const std::function<void()>& task)
    {
        if (std::this_thread::get_id() == _owner)
        {
            task();
            return;
        }
        std::unique_lock lock(_tasksMutex);
        if (_closing)
            return;
        _tasks.push_back(task);
        const uint64_t ticket = ++_tasksQueued;  // the tasks run in order: this one ran when the count reaches it
        _tasksDone.wait(lock, [&] { return _closing || _tasksRun >= ticket; });
    }

    void OnFileLoaded(const std::string& path)
    {
        _fileSha256 = FileSha256(path);
        _autoDone = false;
        _autoConnector.clear();
        Reapply();
    }

    /// retro-devices.md §4, the machine default (HostControllers::assignPlayers): the n-th pad takes the n-th of
    /// next.left, next.right, kempston, sinclair-1, sinclair-2 the machine has with a device; none of them: a cursor
    /// joystick on a free keyboard socket
    void AssignPlayers(const std::vector<HostDeviceInfo>& devices)
    {
        EmulatorContext* context = _emulator.GetContext();
        ControllerManager* manager = context != nullptr ? context->pControllerManager : nullptr;
        if (manager == nullptr)
            return;
        std::vector<std::string> order;
        if (!_autoConnector.empty() && manager->Find(_autoConnector) >= 0)
            order.push_back(_autoConnector);
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
        _players.clear();
        for (const HostDeviceInfo& device : devices)
        {
            if (device.family != "keyboard" && device.family != "mouse" && !player(device) &&
                _service->Router().Role(device.id) == DeviceRole::Player)
                _service->Router().SetRole(device.id, DeviceRole::DebuggerRemote);
            if (!player(device))
                continue;
            _players[device.id] = n < order.size() ? order[n] : std::string{};
            _service->Router().SetPlayerConnector(device.id, _players[device.id]);
            ++n;
        }
    }

    void ApplyConnectorPlans()
    {
        EmulatorContext* context = _emulator.GetContext();
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
                    SDL_Log("controllers: %s: %s", id.c_str(), reply.result.message.c_str());
            }
        }
        if (!_autoDone)
        {
            _autoDone = true;
            manager->WatchPortReads(false);
        }
    }

    void AttachHaptics()
    {
        EmulatorContext* context = _emulator.GetContext();
        if (context == nullptr || context->pControllerManager == nullptr)
            return;
        HapticProfile profile;
        std::string error;
        const bool game = !_resolved.haptics.IsNull() && ParseHapticProfile(_resolved.haptics, profile, error);
        if (!error.empty())
            SDL_Log("controllers: %s: %s", _resolved.hapticsProfile.c_str(), error.c_str());
        const std::string key = game ? "profile:" + _resolved.hapticsProfile + ":" + _resolved.haptics.Dump()
                                     : (_options.soundHaptics ? std::string("sound") : std::string());
        if (key == _hapticsKey && (key.empty() || _hapticFeed))
            return;
        _hapticsKey = key;
        if (game)
            _hapticFeed = _service->HapticFeedFor(_emulator.GetId(), profile, _resolved.hapticsProfile);
        else if (_options.soundHaptics)
            _hapticFeed = _service->HapticFeedFor(_emulator.GetId());
        else
            _hapticFeed.reset();
        context->pControllerManager->SetHapticFeed(_hapticFeed);
        SDL_Log("controllers: haptics %s", key.empty() ? "off" : game ? _resolved.hapticsProfile.c_str() : "sound");
    }

    /// H-5: running, not paused, not turbo, the window in focus, no history or RZX replay
    void UpdateGate(bool focused)
    {
        EmulatorContext* context = _emulator.GetContext();
        bool open = focused && context != nullptr && _emulator.IsRunning() && !_emulator.IsPaused() &&
                    !_emulator.IsTurboMode();
        if (open && (context->rzxPlayer != nullptr ||
                     (context->pTimeTravelHooks != nullptr && context->pTimeTravelHooks->OwnsInput())))
            open = false;
        _service->SetHapticsGate(open);
    }

    void CheckAuto()
    {
        if (_autoDone)
            return;
        EmulatorContext* context = _emulator.GetContext();
        if (context == nullptr || context->pControllerManager == nullptr)
            return;
        const PortReadEvidence evidence = context->pControllerManager->PortReads();
        if (!evidence.active)
            return;
        const AutoDecision decision = DecideFromPortReads(evidence);
        if (decision.decisive || decision.keyboard)
            ApplyAuto(decision);
    }

    void ApplyAuto(const AutoDecision& decision)
    {
        EmulatorContext* context = _emulator.GetContext();
        ControllerManager* manager = context != nullptr ? context->pControllerManager : nullptr;
        if (manager == nullptr)
            return;
        _autoDone = true;
        manager->WatchPortReads(false);
        if (decision.keyboard || decision.device == "kempston-mouse")
            return;
        std::string connector = decision.connector;
        if (!connector.empty())
        {
            if (manager->Find(connector) < 0)
                return;
        }
        else
        {
            ControllerRequest request{"plug", "auto", decision.device, {{"async", ""}}};
            if (!decision.option.empty())
                request.options[decision.option] = decision.optionValue;
            const ControllerReply reply = ControllerControl(context).Execute(request);
            if (!reply.result.Ok())
            {
                SDL_Log("controllers: %s: %s", decision.label.c_str(), reply.result.message.c_str());
                return;
            }
            connector = reply.connector;
        }
        _autoConnector = connector;
        SDL_Log("controllers: %s on %s (port reads: %s)", decision.label.c_str(), connector.c_str(),
                decision.reason.c_str());
        Reapply();
    }

    Emulator& _emulator;
    Options _options;
    std::unique_ptr<HostInputService> _service;
    ProfileStore _profiles;
    ResolvedProfiles _resolved;
    ProfileContext _context;
    std::string _manualProfile;
    std::string _fileSha256;
    std::string _autoConnector;
    bool _autoDone = false;
    std::shared_ptr<IHapticFeed> _hapticFeed;
    std::string _hapticsKey;
    uint64_t _revision = 0;
    uint64_t _machineRevision = 0;
    std::vector<std::string> _known;
    std::map<std::string, std::string> _players;  ///< device -> the connector AssignPlayers gave it
    uint64_t _fileObserver = 0;

    std::thread::id _owner;
    std::mutex _tasksMutex;
    std::condition_variable _tasksDone;
    std::deque<std::function<void()>> _tasks;
    uint64_t _tasksQueued = 0;
    uint64_t _tasksRun = 0;
    bool _closing = false;
    std::string _loadedFile;
    bool _fileChanged = false;

    std::mutex _actionsMutex;
    std::function<void(const std::string&, bool)> _actionSink;
};

DeckControllers::DeckControllers(Emulator& emulator, Options options)
    : _impl(std::make_unique<Impl>(emulator, options))
{
}

DeckControllers::~DeckControllers() = default;

void DeckControllers::Poll(bool focused)
{
    _impl->Poll(focused);
}

void DeckControllers::SetActionSink(std::function<void(const std::string&, bool)> sink)
{
    _impl->SetActionSink(std::move(sink));
}

controllers::HostInputService& DeckControllers::Service()
{
    return _impl->Service();
}

std::vector<std::string> DeckControllers::Describe() const
{
    return _impl->Describe();
}

}  // namespace deckpoc
