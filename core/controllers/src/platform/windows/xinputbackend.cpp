#include "xinputbackend.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace controllers
{

namespace
{
// The XInput ABI (xinput.h), declared here so the backend needs neither the header nor the import library
struct XGamepad
{
    WORD buttons;
    BYTE leftTrigger;
    BYTE rightTrigger;
    SHORT thumbLX;
    SHORT thumbLY;
    SHORT thumbRX;
    SHORT thumbRY;
};
struct XState
{
    DWORD packet;
    XGamepad gamepad;
};
struct XVibration
{
    WORD leftMotorSpeed;   ///< the low-frequency motor
    WORD rightMotorSpeed;  ///< the high-frequency motor
};
using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XState*);
using XInputSetStateFn = DWORD(WINAPI*)(DWORD, XVibration*);
constexpr DWORD kSlots = 4;

struct ButtonName
{
    WORD mask;
    const char* control;
};
constexpr ButtonName kButtons[] = {
    {0x0001, "dpad.up"}, {0x0002, "dpad.down"},    {0x0004, "dpad.left"},    {0x0008, "dpad.right"},
    {0x0010, "menu"},    {0x0020, "view"},         {0x0040, "lstick.click"}, {0x0080, "rstick.click"},
    {0x0100, "lb"},      {0x0200, "rb"},           {0x1000, "south"},        {0x2000, "east"},
    {0x4000, "west"},    {0x8000, "north"},
};

float Axis(SHORT value)
{
    return value >= 0 ? value / 32767.0f : value / 32768.0f;
}

class XInputBackend : public IBackend
{
public:
    explicit XInputBackend(std::shared_ptr<std::atomic<bool>> xboxServed) : _xboxServed(std::move(xboxServed))
    {
    }

    ~XInputBackend() override
    {
        Stop();
        if (_dll != nullptr)
            FreeLibrary(_dll);
    }

    std::string Name() const override
    {
        return "xinput";
    }

    bool Start(IBackendSink& sink, std::string& reason) override
    {
        if (_thread.joinable())
            return true;
        if (_xboxServed && *_xboxServed)
        {
            reason = "GameInput serves the Xbox pads";
            return false;
        }
        if (_getState == nullptr)
        {
            for (const char* name : {"xinput1_4.dll", "xinput9_1_0.dll", "xinput1_3.dll"})
                if ((_dll = LoadLibraryA(name)) != nullptr)
                    break;
            if (_dll != nullptr)
            {
                _getState = reinterpret_cast<XInputGetStateFn>(
                    reinterpret_cast<void*>(GetProcAddress(_dll, "XInputGetState")));
                _setState = reinterpret_cast<XInputSetStateFn>(
                    reinterpret_cast<void*>(GetProcAddress(_dll, "XInputSetState")));
            }
            if (_getState == nullptr)
            {
                reason = "no XInput runtime (xinput1_4.dll / xinput9_1_0.dll)";
                return false;
            }
        }
        _sink = &sink;
        _stopping = false;
        _thread = std::thread([this] { Run(); });
        return true;
    }

    void Stop() override
    {
        if (!_thread.joinable())
            return;
        {
            std::lock_guard lock(_mutex);
            _stopping = true;
        }
        _wake.notify_all();
        _thread.join();
        _sink = nullptr;
    }

    size_t DeviceCount() const override
    {
        return _count.load();
    }

    /// XInput drives the two handle motors; the impulse triggers need GameInput (C8)
    bool HasRumble(const HostDeviceId& device) const override
    {
        return _setState != nullptr && Index(device) < kSlots;
    }

    std::string RumbleUnavailable(const HostDeviceId& device) const override
    {
        if (_setState == nullptr)
            return "XInput is not available on this Windows";
        return Index(device) < kSlots ? std::string() : "The device is not connected";
    }

    bool SetRumble(const HostDeviceId& device, const RumbleLevels& levels) override
    {
        const DWORD index = Index(device);
        if (_setState == nullptr || index >= kSlots)
            return false;
        auto speed = [](float level) {
            return static_cast<WORD>(std::clamp(level, 0.0f, 1.0f) * 65535.0f + 0.5f);
        };
        XVibration vibration{speed(levels.low), speed(levels.high)};
        return _setState(index, &vibration) == ERROR_SUCCESS;
    }

private:
    struct Slot
    {
        bool connected = false;
        DWORD packet = ~0u;
        XGamepad last{};
        unsigned idleTicks = 0;  ///< an empty slot is asked again after 250 ticks (1 s)
    };

    void Run()
    {
        std::array<Slot, kSlots> slots{};
        std::unique_lock lock(_mutex);
        while (!_stopping)
        {
            lock.unlock();
            for (DWORD index = 0; index < kSlots; ++index)
                Poll(index, slots[index]);
            lock.lock();
            _wake.wait_for(lock, std::chrono::milliseconds(4), [this] { return _stopping; });
        }
        lock.unlock();
        for (DWORD index = 0; index < kSlots; ++index)
            if (slots[index].connected)
                _sink->OnDeviceRemoved(Id(index));
        _count = 0;
    }

    static std::string Id(DWORD index)
    {
        return "xinput:" + std::to_string(index);
    }
    /// The slot of an id this backend made; kSlots for another one
    static DWORD Index(const HostDeviceId& device)
    {
        const std::string prefix = "xinput:";
        if (device.size() != prefix.size() + 1 || device.compare(0, prefix.size(), prefix) != 0)
            return kSlots;
        const char digit = device.back();
        return digit >= '0' && digit < static_cast<char>('0' + kSlots) ? static_cast<DWORD>(digit - '0') : kSlots;
    }

    void Poll(DWORD index, Slot& slot)
    {
        if (!slot.connected && slot.idleTicks++ % 250 != 0)
            return;
        XState state{};
        const bool connected = _getState(index, &state) == ERROR_SUCCESS;
        const std::string id = Id(index);
        if (connected != slot.connected)
        {
            slot.connected = connected;
            slot.idleTicks = 1;
            if (connected)
            {
                slot.packet = ~0u;
                slot.last = {};
                ++_count;
                _sink->OnDeviceAdded({id, "XInput controller " + std::to_string(index + 1), "xbox", "xbox", "xinput"});
            }
            else
            {
                --_count;
                _sink->OnDeviceRemoved(id);
                return;
            }
        }
        if (!connected || state.packet == slot.packet)
            return;
        slot.packet = state.packet;
        const XGamepad& now = state.gamepad;
        const XGamepad& was = slot.last;
        for (const ButtonName& button : kButtons)
            if ((now.buttons & button.mask) != (was.buttons & button.mask))
                _sink->OnControl({id, button.control, (now.buttons & button.mask) != 0 ? 1.0f : 0.0f, 0});
        auto axis = [this, &id](const char* control, bool changed, float value) {
            if (changed)
                _sink->OnControl({id, control, value, 0});
        };
        axis("lt", now.leftTrigger != was.leftTrigger, now.leftTrigger / 255.0f);
        axis("rt", now.rightTrigger != was.rightTrigger, now.rightTrigger / 255.0f);
        axis("lstick.x", now.thumbLX != was.thumbLX, Axis(now.thumbLX));
        axis("lstick.y", now.thumbLY != was.thumbLY, -Axis(now.thumbLY));  // XInput: + up; canonical: + down
        axis("rstick.x", now.thumbRX != was.thumbRX, Axis(now.thumbRX));
        axis("rstick.y", now.thumbRY != was.thumbRY, -Axis(now.thumbRY));
        slot.last = now;
    }

    std::shared_ptr<std::atomic<bool>> _xboxServed;
    HMODULE _dll = nullptr;
    XInputGetStateFn _getState = nullptr;
    XInputSetStateFn _setState = nullptr;
    IBackendSink* _sink = nullptr;
    std::thread _thread;
    std::mutex _mutex;
    std::condition_variable _wake;
    bool _stopping = false;
    std::atomic<size_t> _count{0};
};
}  // namespace

std::unique_ptr<IBackend> CreateXInputBackend(std::shared_ptr<std::atomic<bool>> xboxServed)
{
    return std::make_unique<XInputBackend>(std::move(xboxServed));
}

}  // namespace controllers
