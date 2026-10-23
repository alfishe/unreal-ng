#include "platform/windows/gameinputbackend.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <unknwn.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "devicedatabase.h"
#include "platform/windows/winthread.h"
#include "platform/windowsdevices.h"

namespace controllers
{

/// region <The GameInput v3 ABI (GameInput.h of the Microsoft.GameInput NuGet package, API version 3)>
/// Declared here, as far as this backend calls it, so the build needs neither the header nor GameInput.lib (the
/// runtime is loaded at run time, D17). The vtables are the header's order; nothing after the last method used.
/// Not in an anonymous namespace: gcc would see that no class of this file implements them and drop the calls
namespace gi
{
constexpr IID kIidGameInputV3 = {0x20efc1c7, 0x5d9a, 0x43ba, {0xb2, 0x6f, 0xb8, 0x07, 0xfa, 0x48, 0x60, 0x9c}};

constexpr uint32_t kKindGamepad = 0x00040000;
constexpr uint32_t kStatusConnected = 0x00000001;
constexpr uint32_t kAsyncEnumeration = 1;
constexpr uint32_t kEnableBackgroundInput = 0x00000040;

struct Version
{
    uint16_t major;
    uint16_t minor;
    uint16_t build;
    uint16_t revision;
};
struct Usage
{
    uint16_t page;
    uint16_t id;
};
struct AppLocalDeviceId
{
    uint8_t value[32];
};
/// GameInputDeviceInfo up to pnpPath; the runtime owns it, the rest is never read
struct DeviceInfo
{
    uint16_t vendorId;
    uint16_t productId;
    uint16_t revisionNumber;
    Usage usage;
    Version hardwareVersion;
    Version firmwareVersion;
    AppLocalDeviceId deviceId;
    AppLocalDeviceId deviceRootId;
    int32_t deviceFamily;
    uint32_t supportedInput;
    uint32_t supportedRumbleMotors;
    uint32_t supportedSystemButtons;
    GUID containerId;
    const char* displayName;
    const char* pnpPath;
};
struct GamepadState
{
    uint32_t buttons;
    float leftTrigger;
    float rightTrigger;
    float leftThumbstickX;
    float leftThumbstickY;
    float rightThumbstickX;
    float rightThumbstickY;
};
struct RumbleParams
{
    float lowFrequency;
    float highFrequency;
    float leftTrigger;
    float rightTrigger;
};

struct IDevice : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE GetDeviceInfo(const DeviceInfo** info) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetHapticInfo(void* info) = 0;
    virtual uint32_t STDMETHODCALLTYPE GetDeviceStatus() = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateForceFeedbackEffect(uint32_t motor, const void* params, void** effect) = 0;
    virtual bool STDMETHODCALLTYPE IsForceFeedbackMotorPoweredOn(uint32_t motor) = 0;
    virtual void STDMETHODCALLTYPE SetForceFeedbackMotorGain(uint32_t motor, float gain) = 0;
    virtual void STDMETHODCALLTYPE SetRumbleState(const RumbleParams* params) = 0;
};

struct IReading : public IUnknown
{
    virtual uint32_t STDMETHODCALLTYPE GetInputKind() = 0;
    virtual uint64_t STDMETHODCALLTYPE GetTimestamp() = 0;
    virtual void STDMETHODCALLTYPE GetDevice(IDevice** device) = 0;
    virtual uint32_t STDMETHODCALLTYPE GetControllerAxisCount() = 0;
    virtual uint32_t STDMETHODCALLTYPE GetControllerAxisState(uint32_t count, float* state) = 0;
    virtual uint32_t STDMETHODCALLTYPE GetControllerButtonCount() = 0;
    virtual uint32_t STDMETHODCALLTYPE GetControllerButtonState(uint32_t count, bool* state) = 0;
    virtual uint32_t STDMETHODCALLTYPE GetControllerSwitchCount() = 0;
    virtual uint32_t STDMETHODCALLTYPE GetControllerSwitchState(uint32_t count, void* state) = 0;
    virtual uint32_t STDMETHODCALLTYPE GetKeyCount() = 0;
    virtual uint32_t STDMETHODCALLTYPE GetKeyState(uint32_t count, void* state) = 0;
    virtual bool STDMETHODCALLTYPE GetMouseState(void* state) = 0;
    virtual bool STDMETHODCALLTYPE GetSensorsState(void* state) = 0;
    virtual bool STDMETHODCALLTYPE GetArcadeStickState(void* state) = 0;
    virtual bool STDMETHODCALLTYPE GetFlightStickState(void* state) = 0;
    virtual bool STDMETHODCALLTYPE GetGamepadState(GamepadState* state) = 0;
};

using DeviceCallback = void(CALLBACK*)(uint64_t token, void* context, IDevice* device, uint64_t timestamp,
                                       uint32_t currentStatus, uint32_t previousStatus);

struct IGameInput : public IUnknown
{
    virtual uint64_t STDMETHODCALLTYPE GetCurrentTimestamp() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentReading(uint32_t kind, IDevice* device, IReading** reading) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNextReading(IReading* reference, uint32_t kind, IDevice* device,
                                                     IReading** reading) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPreviousReading(IReading* reference, uint32_t kind, IDevice* device,
                                                         IReading** reading) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterReadingCallback(IDevice* device, uint32_t kind, void* context,
                                                              void* callback, uint64_t* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterDeviceCallback(IDevice* device, uint32_t kind, uint32_t statusFilter,
                                                             uint32_t enumeration, void* context,
                                                             DeviceCallback callback, uint64_t* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterSystemButtonCallback(IDevice* device, uint32_t filter, void* context,
                                                                   void* callback, uint64_t* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterKeyboardLayoutCallback(IDevice* device, void* context, void* callback,
                                                                     uint64_t* token) = 0;
    virtual void STDMETHODCALLTYPE StopCallback(uint64_t token) = 0;
    virtual bool STDMETHODCALLTYPE UnregisterCallback(uint64_t token) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateDispatcher(void** dispatcher) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindDeviceFromId(const AppLocalDeviceId* id, IDevice** device) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindDeviceFromPlatformString(const wchar_t* value, IDevice** device) = 0;
    virtual void STDMETHODCALLTYPE SetFocusPolicy(uint32_t policy) = 0;
};

using InitializeFn = HRESULT(WINAPI*)(REFIID riid, void** out);
}  // namespace gi
/// endregion </The GameInput v3 ABI>

namespace
{

std::wstring SystemPath(const wchar_t* file)
{
    wchar_t dir[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(dir, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return {};
    return std::wstring(dir) + L"\\" + file;
}

#ifndef RRF_SUBKEY_WOW6432KEY
#define RRF_SUBKEY_WOW6432KEY 0x00020000  // winreg.h; missing from MinGW's
#endif

/// The redistributable's folder (HKLM\SOFTWARE\Microsoft\GameInput RedistDir), where it is not in System32
std::wstring RedistPath()
{
    wchar_t dir[MAX_PATH] = {};
    DWORD size = sizeof(dir);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\GameInput", L"RedistDir",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, dir, &size) != ERROR_SUCCESS)
        return {};
    std::wstring path(dir);
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
        path += L'\\';
    return path + L"GameInputRedist.dll";
}

class GameInputBackend : public IBackend
{
public:
    explicit GameInputBackend(std::shared_ptr<std::atomic<bool>> xboxServed) : _xboxServed(std::move(xboxServed))
    {
    }

    ~GameInputBackend() override
    {
        Stop();
        if (_input != nullptr)
            _input->Release();
        if (_dll != nullptr)
            FreeLibrary(_dll);
    }

    std::string Name() const override
    {
        return "gameinput";
    }

    bool Start(IBackendSink& sink, std::string& reason) override
    {
        if (_thread.joinable())
            return true;
        if (_input == nullptr && !Load(reason))
            return false;
        _sink = &sink;
        _stopping = false;
        // The router gates what reaches a machine (focus, the debug layer); the pads read like the IOHID ones
        _input->SetFocusPolicy(gi::kEnableBackgroundInput);
        // Asynchronous: the pads present now come through the callback like later arrivals; Start does not wait
        const HRESULT registered =
            _input->RegisterDeviceCallback(nullptr, gi::kKindGamepad, gi::kStatusConnected, gi::kAsyncEnumeration,
                                           this, &GameInputBackend::OnDevice, &_token);
        if (FAILED(registered))
        {
            reason = "GameInput RegisterDeviceCallback: " + Hresult(registered);
            _sink = nullptr;
            return false;
        }
        if (_xboxServed)
            *_xboxServed = true;
        _thread = std::thread([this] { Run(); });
        return true;
    }

    void Stop() override
    {
        if (!_thread.joinable())
            return;
        // No callback after this: the runtime waits for one in flight
        _input->StopCallback(_token);
        _input->UnregisterCallback(_token);
        _token = 0;
        {
            std::lock_guard lock(_mutex);
            _stopping = true;
        }
        _wake.notify_all();
        _thread.join();
        for (const Change& change : _changes)
            change.device->Release();
        _changes.clear();
        if (_xboxServed)
            *_xboxServed = false;
        _sink = nullptr;
    }

    size_t DeviceCount() const override
    {
        std::lock_guard lock(_mutex);
        return _devices.size();
    }

    bool HasRumble(const HostDeviceId& device) const override
    {
        return (RumbleActuators(device) & (kRumbleLow | kRumbleHigh)) != 0;
    }

    /// GameInputRumbleMotors are RumbleActuator's bits: low 1, high 2, left trigger 4, right trigger 8
    uint8_t RumbleActuators(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        for (const Device& known : _devices)
            if (known.id == device)
                return static_cast<uint8_t>(known.motors & 0x0F);
        return 0;
    }

    std::string RumbleUnavailable(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        for (const Device& known : _devices)
            if (known.id == device)
                return (known.motors & 0x03) != 0 ? std::string() : "GameInput reports no motors for this pad";
        return "The device is not connected";
    }

    bool SetRumble(const HostDeviceId& device, const RumbleLevels& levels) override
    {
        std::lock_guard lock(_mutex);
        for (const Device& known : _devices)
        {
            if (known.id != device)
                continue;
            if ((known.motors & 0x0F) == 0)
                return false;
            auto level = [](float value) { return std::clamp(value, 0.0f, 1.0f); };
            const gi::RumbleParams params{level(levels.low), level(levels.high), level(levels.leftTrigger),
                                          level(levels.rightTrigger)};
            known.device->SetRumbleState(&params);
            return true;
        }
        return false;
    }

private:
    struct Device
    {
        gi::IDevice* device = nullptr;  ///< one reference held
        HostDeviceId id;
        uint32_t motors = 0;
        uint64_t timestamp = 0;  ///< of the last reading decoded
        GameInputPadState last;
        bool seen = false;
    };
    struct Change
    {
        gi::IDevice* device = nullptr;  ///< one reference held until the poll thread takes it
        bool connected = false;
    };

    static std::string Hresult(HRESULT value)
    {
        char text[16];
        snprintf(text, sizeof(text), "0x%08lx", static_cast<unsigned long>(value));
        return text;
    }

    bool Load(std::string& reason)
    {
        std::string tried;
        for (const std::wstring& path : {SystemPath(L"GameInputRedist.dll"), RedistPath(), SystemPath(L"GameInput.dll")})
        {
            if (path.empty())
                continue;
            HMODULE dll = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (dll == nullptr)
                continue;
            const auto initialize = reinterpret_cast<gi::InitializeFn>(
                reinterpret_cast<void*>(GetProcAddress(dll, "GameInputInitialize")));
            void* created = nullptr;
            const HRESULT result =
                initialize != nullptr ? initialize(gi::kIidGameInputV3, &created) : E_NOINTERFACE;
            if (SUCCEEDED(result) && created != nullptr)
            {
                _dll = dll;
                _input = static_cast<gi::IGameInput*>(created);
                return true;
            }
            // The inbox GameInput.dll of older Windows has the v0 API only (no GameInputInitialize)
            tried += tried.empty() ? "" : "; ";
            tried += initialize != nullptr ? "API v3 refused (" + Hresult(result) + ")" : "no API v3 export";
            FreeLibrary(dll);
        }
        reason = tried.empty() ? "no GameInput runtime (GameInputRedist.dll / GameInput.dll)"
                               : "the GameInput runtime is too old: " + tried;
        return false;
    }

    static void CALLBACK OnDevice(uint64_t, void* context, gi::IDevice* device, uint64_t, uint32_t current,
                                  uint32_t previous)
    {
        const bool now = (current & gi::kStatusConnected) != 0;
        const bool was = (previous & gi::kStatusConnected) != 0;
        if (now == was || device == nullptr)
            return;
        auto* self = static_cast<GameInputBackend*>(context);
        device->AddRef();
        {
            std::lock_guard lock(self->_mutex);
            self->_changes.push_back({device, now});
        }
        self->_wake.notify_all();
    }

    void Run()
    {
        NameThisThread(L"controllers-gameinput");
        std::unique_lock lock(_mutex);
        while (!_stopping)
        {
            std::vector<Change> changes;
            changes.swap(_changes);
            lock.unlock();
            for (const Change& change : changes)
                Apply(change);
            Poll();
            lock.lock();
            _wake.wait_for(lock, std::chrono::milliseconds(4), [this] { return _stopping || !_changes.empty(); });
        }
        std::vector<Device> gone;
        gone.swap(_devices);
        lock.unlock();
        for (Device& device : gone)
        {
            _sink->OnDeviceRemoved(device.id);
            device.device->Release();
        }
    }

    /// On the poll thread only: _devices changes here, so Poll reads it without the lock
    void Apply(const Change& change)
    {
        if (!change.connected)
        {
            Device removed;
            {
                std::lock_guard lock(_mutex);
                auto it = std::find_if(_devices.begin(), _devices.end(),
                                       [&change](const Device& known) { return known.device == change.device; });
                if (it != _devices.end())
                {
                    removed = *it;
                    _devices.erase(it);
                }
            }
            change.device->Release();
            if (removed.device != nullptr)
            {
                _sink->OnDeviceRemoved(removed.id);
                removed.device->Release();
            }
            return;
        }
        const gi::DeviceInfo* info = nullptr;
        if (FAILED(change.device->GetDeviceInfo(&info)) || info == nullptr ||
            !GameInputServes(info->deviceFamily) || (info->supportedInput & gi::kKindGamepad) == 0)
        {
            change.device->Release();  // a HID pad: the Raw Input backend's
            return;
        }
        const std::string pnpPath = info->pnpPath != nullptr ? info->pnpPath : "";
        const WinHidPath path = ParseWinHidPath(pnpPath);
        DeviceIdentity identity;
        identity.bus = path.bus;
        identity.vendor = info->vendorId;
        identity.product = info->productId;
        identity.version = info->revisionNumber;
        identity.name = info->displayName != nullptr ? info->displayName : "";
        identity.backend = Name();
        const DeviceMatch match = DeviceDatabase::Builtin().Lookup(identity);
        if (match.ignore)
        {
            change.device->Release();
            return;
        }
        // Without a PnP path the runtime's app-local device id stands for it (stable for this app and device)
        std::string where = pnpPath;
        if (where.empty())
            for (uint8_t byte : info->deviceId.value)
            {
                char hex[4];
                snprintf(hex, sizeof(hex), "%02x", byte);
                where += hex;
            }
        const std::string id =
            WinDeviceId(path.bus, info->vendorId, info->productId, info->revisionNumber, std::string(), where);

        Device device;
        device.device = change.device;  // the change's reference moves here
        device.motors = info->supportedRumbleMotors;
        HostDeviceInfo host;
        host.name = match.name.empty() ? identity.name : match.name;
        host.family = match.family;
        host.layout = match.layout;
        host.backend = Name();
        {
            std::lock_guard lock(_mutex);
            std::string unique = id;
            for (int n = 2; std::any_of(_devices.begin(), _devices.end(),
                                        [&unique](const Device& known) { return known.id == unique; });
                 ++n)
                unique = id + "#" + std::to_string(n);
            device.id = unique;
            host.id = unique;
            _devices.push_back(device);
        }
        _sink->OnDeviceAdded(host);
    }

    void Poll()
    {
        for (Device& device : _devices)
        {
            gi::IReading* reading = nullptr;
            if (FAILED(_input->GetCurrentReading(gi::kKindGamepad, device.device, &reading)) || reading == nullptr)
                continue;
            const uint64_t timestamp = reading->GetTimestamp();
            gi::GamepadState state{};
            const bool fresh = timestamp != device.timestamp && reading->GetGamepadState(&state);
            reading->Release();
            if (!fresh)
                continue;
            device.timestamp = timestamp;
            const GameInputPadState now{state.buttons,         state.leftTrigger,      state.rightTrigger,
                                        state.leftThumbstickX, state.leftThumbstickY,  state.rightThumbstickX,
                                        state.rightThumbstickY};
            const HostDeviceId& id = device.id;
            DiffGameInputPad(device.last, now, !device.seen, [this, &id](const std::string& control, float value) {
                _sink->OnControl({id, control, value, 0});
            });
            device.last = now;
            device.seen = true;
        }
    }

    std::shared_ptr<std::atomic<bool>> _xboxServed;
    HMODULE _dll = nullptr;
    gi::IGameInput* _input = nullptr;
    uint64_t _token = 0;
    IBackendSink* _sink = nullptr;
    std::thread _thread;
    mutable std::mutex _mutex;
    std::condition_variable _wake;
    bool _stopping = false;
    std::vector<Change> _changes;  ///< under _mutex: from the runtime's callback to the poll thread
    std::vector<Device> _devices;  ///< changed on the poll thread under _mutex; read there without it
};
}  // namespace

std::unique_ptr<IBackend> CreateGameInputBackend(std::shared_ptr<std::atomic<bool>> xboxServed)
{
    return std::make_unique<GameInputBackend>(std::move(xboxServed));
}

}  // namespace controllers
