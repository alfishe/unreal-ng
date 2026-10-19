#include "evdevbackend.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "devicedatabase.h"
#include "hid/drivers/surfacedial.h"

namespace controllers
{

namespace
{
constexpr const char* kInputDir = "/dev/input";

bool TestBit(const std::vector<unsigned long>& bits, unsigned bit)
{
    const unsigned per = sizeof(unsigned long) * 8;
    return bit / per < bits.size() && (bits[bit / per] >> (bit % per)) & 1ul;
}

std::vector<unsigned long> Bits(int fd, unsigned type, unsigned max)
{
    std::vector<unsigned long> bits(max / (sizeof(unsigned long) * 8) + 1, 0);
    if (ioctl(fd, EVIOCGBIT(type, bits.size() * sizeof(unsigned long)), bits.data()) < 0)
        bits.assign(bits.size(), 0);
    return bits;
}

std::string Hex4(int value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "%04x", value & 0xFFFF);
    return text;
}

/// FNV-1a, for ids that must not carry a MAC address or a port path as such
std::string Hash(const std::string& text)
{
    uint32_t hash = 2166136261u;
    for (unsigned char c : text)
        hash = (hash ^ c) * 16777619u;
    char out[12];
    std::snprintf(out, sizeof(out), "%08x", hash);
    return out;
}

std::string StringIoctl(int fd, unsigned long request)
{
    char text[256] = {};
    if (ioctl(fd, request, text) < 0)
        return {};
    return text;
}

const char* PadButton(unsigned code)
{
    switch (code)
    {
        case BTN_SOUTH: return "south";
        case BTN_EAST: return "east";
        case BTN_NORTH: return "north";
        case BTN_WEST: return "west";
        case BTN_TL: return "lb";
        case BTN_TR: return "rb";
        case BTN_TL2: return "lt";
        case BTN_TR2: return "rt";
        case BTN_SELECT: return "view";
        case BTN_START: return "menu";
        case BTN_MODE: return "guide";
        case BTN_THUMBL: return "lstick.click";
        case BTN_THUMBR: return "rstick.click";
        case BTN_DPAD_UP: return "dpad.up";
        case BTN_DPAD_DOWN: return "dpad.down";
        case BTN_DPAD_LEFT: return "dpad.left";
        case BTN_DPAD_RIGHT: return "dpad.right";
        default: return nullptr;
    }
}

const char* PadAxis(unsigned code)
{
    switch (code)
    {
        case ABS_X: return "lstick.x";
        case ABS_Y: return "lstick.y";
        case ABS_RX: return "rstick.x";
        case ABS_RY: return "rstick.y";
        case ABS_Z: return "lt";
        case ABS_RZ: return "rt";
        default: return nullptr;
    }
}

struct Device
{
    int fd = -1;
    std::string node;
    HostDeviceInfo info;
    bool pad = false;  ///< BTN_GAMEPAD: positional names; else a joystick with button.<n> / axis.<n>
    bool dial = false;  ///< a Surface Dial (hid-generic): REL_DIAL is `knob`, BTN_0 `push`, BTN_TOUCH `touch`
    DialTouch hand;     ///< the Dial's `touch`, sooner than its sensor
    bool sensor = false;
    bool pushed = false;
    bool touched = false;
    std::map<unsigned, input_absinfo> abs;
    int hatX = 0;
    int hatY = 0;
    bool rumble = false;  ///< opened for writing and FF_RUMBLE is offered
    std::string rumbleUnavailable;  ///< why not: the Test haptics button's tooltip
    int effect = -1;      ///< the uploaded FF_RUMBLE effect, -1 before the first
};

class EvdevBackend : public IBackend
{
public:
    ~EvdevBackend() override
    {
        Stop();
    }

    std::string Name() const override
    {
        return "evdev";
    }

    bool Start(IBackendSink& sink, std::string& reason) override
    {
        if (_thread.joinable())
            return true;
        if (pipe(_wake) != 0)
        {
            reason = std::string("pipe: ") + std::strerror(errno);
            return false;
        }
        _inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (_inotify >= 0)
            inotify_add_watch(_inotify, kInputDir, IN_CREATE | IN_ATTRIB | IN_DELETE);
        _sink = &sink;
        Scan();
        _thread = std::thread([this] { Run(); });
        return true;
    }

    void Stop() override
    {
        if (!_thread.joinable())
            return;
        const char byte = 1;
        [[maybe_unused]] const ssize_t written = write(_wake[1], &byte, 1);
        _thread.join();
        for (auto& [node, device] : _devices)
        {
            _sink->OnDeviceRemoved(device.info.id);
            close(device.fd);
        }
        {
            std::lock_guard lock(_mutex);
            _devices.clear();
        }
        if (_inotify >= 0)
            close(_inotify);
        close(_wake[0]);
        close(_wake[1]);
        _inotify = -1;
        _sink = nullptr;
    }

    bool HasRumble(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        const Device* known = Find(device);
        return known != nullptr && known->rumble;
    }

    std::string RumbleUnavailable(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        const Device* known = Find(device);
        return known != nullptr ? known->rumbleUnavailable : "The device is not connected";
    }

    /// FF_RUMBLE: strong = the low-frequency motor, weak = the high one; one effect per device, updated in place and
    /// played without an end (length 0) until the levels go to zero
    bool SetRumble(const HostDeviceId& device, const RumbleLevels& levels) override
    {
        std::lock_guard lock(_mutex);
        Device* known = Find(device);
        if (known == nullptr || !known->rumble)
            return false;
        auto magnitude = [](float level) {
            return static_cast<uint16_t>((level < 0 ? 0 : level > 1 ? 1 : level) * 65535.0f + 0.5f);
        };
        const bool on = levels.low > 0 || levels.high > 0;
        if (on)
        {
            ff_effect effect{};
            effect.type = FF_RUMBLE;
            effect.id = static_cast<int16_t>(known->effect);
            effect.u.rumble.strong_magnitude = magnitude(levels.low);
            effect.u.rumble.weak_magnitude = magnitude(levels.high);
            if (ioctl(known->fd, EVIOCSFF, &effect) < 0)
                return false;
            known->effect = effect.id;
        }
        if (known->effect < 0)
            return true;
        input_event play{};
        play.type = EV_FF;
        play.code = static_cast<uint16_t>(known->effect);
        play.value = on ? 1 : 0;
        return write(known->fd, &play, sizeof(play)) == static_cast<ssize_t>(sizeof(play));
    }

    size_t DeviceCount() const override
    {
        std::lock_guard lock(_mutex);
        return _devices.size();
    }

private:
    /// Under _mutex
    Device* Find(const HostDeviceId& device)
    {
        for (auto& [node, known] : _devices)
            if (known.info.id == device)
                return &known;
        return nullptr;
    }
    const Device* Find(const HostDeviceId& device) const
    {
        for (const auto& [node, known] : _devices)
            if (known.info.id == device)
                return &known;
        return nullptr;
    }

    void Scan()
    {
        DIR* dir = opendir(kInputDir);
        if (dir == nullptr)
            return;
        while (dirent* entry = readdir(dir))
            if (std::strncmp(entry->d_name, "event", 5) == 0)
                Open(std::string(kInputDir) + "/" + entry->d_name);
        closedir(dir);
    }

    void Open(const std::string& node)
    {
        {
            std::lock_guard lock(_mutex);
            if (_devices.count(node) != 0)
                return;
        }
        // Read-write for the force feedback; a node the user may only read still gives the controls
        bool writable = true;
        int fd = open(node.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
        {
            writable = false;
            fd = open(node.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        }
        if (fd < 0)
            return;  // not ours to read (no `input` group): the probe counts what opened
        input_id ident{};
        ioctl(fd, EVIOCGID, &ident);
        const std::vector<unsigned long> keys = Bits(fd, EV_KEY, KEY_MAX);
        const bool pad = TestBit(keys, BTN_GAMEPAD);
        const bool joystick = TestBit(keys, BTN_JOYSTICK) || TestBit(keys, BTN_TRIGGER);
        // The Surface Dial by its id, or a device of its kind: a dial and a button
        const std::vector<unsigned long> relative = Bits(fd, EV_REL, REL_MAX);
        const bool dial = !pad && !joystick && TestBit(relative, REL_DIAL) &&
                          (SurfaceDial::Handles(ident.vendor, ident.product) || TestBit(keys, BTN_0));
        if (!pad && !joystick && !dial)
        {
            close(fd);
            return;
        }
        const std::string name = StringIoctl(fd, EVIOCGNAME(255));
        const bool bluetooth = ident.bustype == BUS_BLUETOOTH;
        const DeviceMatch match = DeviceDatabase::Builtin().Lookup(
            {bluetooth ? "bt" : "usb", ident.vendor, ident.product, ident.version, name, Name()});
        if (match.ignore)
        {
            close(fd);
            return;
        }

        Device device;
        device.fd = fd;
        device.node = node;
        device.pad = pad;
        device.dial = dial;
        device.rumble = writable && TestBit(Bits(fd, EV_FF, FF_MAX), FF_RUMBLE);
        if (dial)
            device.rumbleUnavailable = "The Surface Dial clicks through its HID output report: Linux reaches it "
                                       "through hidraw, not evdev, and that backend is not there yet";
        else if (!device.rumble)
            device.rumbleUnavailable = writable ? "The device offers no rumble (FF_RUMBLE) to Linux"
                                                : "No write access to " + node + ": a udev rule that gives the user "
                                                  "write access drives its motors";
        const std::vector<unsigned long> axes = Bits(fd, EV_ABS, ABS_MAX);
        for (unsigned code = 0; code <= ABS_MAX; ++code)
            if (TestBit(axes, code))
            {
                input_absinfo info{};
                if (ioctl(fd, EVIOCGABS(code), &info) == 0)
                    device.abs[code] = info;
            }
        // architecture.md §5: usb:<vid>:<pid>:<version>:<serial>, usb:...@<path hash>, bt:<vid>:<pid>:<hash>
        const std::string uniq = StringIoctl(fd, EVIOCGUNIQ(255));
        const std::string phys = StringIoctl(fd, EVIOCGPHYS(255));
        std::string id;
        if (bluetooth)
            id = "bt:" + Hex4(ident.vendor) + ":" + Hex4(ident.product) + ":" + Hash(uniq.empty() ? phys : uniq);
        else if (!uniq.empty())
            id = "usb:" + Hex4(ident.vendor) + ":" + Hex4(ident.product) + ":" + Hex4(ident.version) + ":" + uniq;
        else
            id = "usb:" + Hex4(ident.vendor) + ":" + Hex4(ident.product) + ":" + Hex4(ident.version) + "@" +
                 Hash(phys.empty() ? node : phys);
        device.info = {id, match.name.empty() ? name : match.name, match.family, match.layout, Name()};
        {
            std::lock_guard lock(_mutex);
            for (const auto& [other, known] : _devices)
                if (known.info.id == device.info.id)
                    device.info.id += "#" + std::to_string(_devices.size() + 1);
            _devices[node] = device;
        }
        _sink->OnDeviceAdded(device.info);
    }

    void Close(const std::string& node)
    {
        Device device;
        {
            std::lock_guard lock(_mutex);
            auto it = _devices.find(node);
            if (it == _devices.end())
                return;
            device = it->second;
            _devices.erase(it);
        }
        close(device.fd);
        _sink->OnDeviceRemoved(device.info.id);
    }

    void Run()
    {
        std::vector<pollfd> fds;
        std::vector<std::string> nodes;
        for (;;)
        {
            fds.clear();
            nodes.clear();
            fds.push_back({_wake[0], POLLIN, 0});
            if (_inotify >= 0)
                fds.push_back({_inotify, POLLIN, 0});
            bool dials = false;
            {
                std::lock_guard lock(_mutex);
                for (const auto& [node, device] : _devices)
                {
                    fds.push_back({device.fd, POLLIN, 0});
                    nodes.push_back(node);
                    dials = dials || device.dial;
                }
            }
            // A Surface Dial's inferred touch ends on time between its events (DialTouch): wake 10 times a second
            const int timeout = dials ? 100 : -1;
            if (poll(fds.data(), fds.size(), timeout) < 0 && errno != EINTR)
                return;
            ExpireDials();
            if (fds[0].revents != 0)
                return;  // Stop
            size_t first = 1;
            if (_inotify >= 0)
            {
                first = 2;
                if (fds[1].revents & POLLIN)
                {
                    alignas(inotify_event) char buffer[4096];
                    while (read(_inotify, buffer, sizeof(buffer)) > 0)
                    {
                    }
                    Scan();  // a new node: udev sets its permissions a moment later (IN_ATTRIB)
                }
            }
            for (size_t i = first; i < fds.size(); ++i)
            {
                const std::string& node = nodes[i - first];
                if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                {
                    Close(node);
                    continue;
                }
                if (fds[i].revents & POLLIN)
                    Read(node);
            }
        }
    }

    void ExpireDials()
    {
        const uint64_t now = DialNowMs();
        std::vector<Device*> dials;
        {
            std::lock_guard lock(_mutex);
            for (auto& [node, device] : _devices)
                if (device.dial)
                    dials.push_back(&device);  // only this thread erases
        }
        for (Device* device : dials)
        {
            const bool touched = device->hand.Expire(now);
            if (!touched && device->touched)
                _sink->OnControl({device->info.id, SurfaceDial::kTouch, 0.0f, 0});
            device->touched = touched;
        }
    }

    void Read(const std::string& node)
    {
        Device* device = nullptr;
        {
            std::lock_guard lock(_mutex);
            auto it = _devices.find(node);
            if (it == _devices.end())
                return;
            device = &it->second;  // only this thread erases
        }
        input_event events[64];
        for (;;)
        {
            const ssize_t got = read(device->fd, events, sizeof(events));
            if (got <= 0)
            {
                if (got < 0 && errno == ENODEV)
                    Close(node);
                return;
            }
            for (size_t i = 0; i < static_cast<size_t>(got) / sizeof(input_event); ++i)
                Event(*device, events[i]);
        }
    }

    void Event(Device& device, const input_event& event)
    {
        const std::string& id = device.info.id;
        if (device.dial)
        {
            const bool turned = event.type == EV_REL && event.code == REL_DIAL && event.value != 0;
            if (event.type == EV_KEY && event.code == BTN_0)
                device.pushed = event.value != 0;
            else if (event.type == EV_KEY && event.code == BTN_TOUCH)
                device.sensor = event.value != 0;
            else if (!turned)
                return;
            // The hand arrives before what it does and leaves after it (SurfaceDial::Decode)
            const bool touched = device.hand.Update(device.sensor, turned, device.pushed, DialNowMs());
            if (touched && !device.touched)
                _sink->OnControl({id, SurfaceDial::kTouch, 1.0f, 0});
            if (turned)
                _sink->OnControl({id, SurfaceDial::kKnob, static_cast<float>(event.value), 0});
            else if (event.code == BTN_0)
                _sink->OnControl({id, SurfaceDial::kPush, device.pushed ? 1.0f : 0.0f, 0});
            if (!touched && device.touched)
                _sink->OnControl({id, SurfaceDial::kTouch, 0.0f, 0});
            device.touched = touched;
            return;
        }
        if (event.type == EV_KEY)
        {
            const float value = event.value != 0 ? 1.0f : 0.0f;
            if (const char* control = device.pad ? PadButton(event.code) : nullptr)
                _sink->OnControl({id, control, value, 0});
            else if (event.code >= BTN_JOYSTICK && event.code < BTN_JOYSTICK + 32)
                _sink->OnControl({id, "button." + std::to_string(event.code - BTN_JOYSTICK), value, 0});
            else if (event.code >= BTN_GAMEPAD && event.code < BTN_GAMEPAD + 16)
                _sink->OnControl({id, "button." + std::to_string(event.code - BTN_GAMEPAD), value, 0});
            return;
        }
        if (event.type != EV_ABS)
            return;
        if (event.code == ABS_HAT0X || event.code == ABS_HAT0Y)
        {
            const bool x = event.code == ABS_HAT0X;
            int& was = x ? device.hatX : device.hatY;
            const int now = event.value < 0 ? -1 : event.value > 0 ? 1 : 0;
            if (now == was)
                return;
            if (was != 0)
                _sink->OnControl({id, x ? (was < 0 ? "dpad.left" : "dpad.right") : (was < 0 ? "dpad.up" : "dpad.down"),
                                  0.0f, 0});
            if (now != 0)
                _sink->OnControl({id, x ? (now < 0 ? "dpad.left" : "dpad.right") : (now < 0 ? "dpad.up" : "dpad.down"),
                                  1.0f, 0});
            was = now;
            return;
        }
        auto range = device.abs.find(event.code);
        if (range == device.abs.end() || range->second.maximum <= range->second.minimum)
            return;
        const input_absinfo& info = range->second;
        const float span = static_cast<float>(info.maximum - info.minimum);
        const float unit = static_cast<float>(event.value - info.minimum) / span;  // 0..1
        const char* control = device.pad ? PadAxis(event.code) : nullptr;
        if (control != nullptr && (event.code == ABS_Z || event.code == ABS_RZ))
            _sink->OnControl({id, control, unit, 0});
        else if (control != nullptr)
            _sink->OnControl({id, control, unit * 2.0f - 1.0f, 0});  // evdev y is + down, as the canonical one
        else
            _sink->OnControl({id, "axis." + std::to_string(event.code), unit * 2.0f - 1.0f, 0});
    }

    IBackendSink* _sink = nullptr;
    std::thread _thread;
    int _wake[2] = {-1, -1};
    int _inotify = -1;
    mutable std::mutex _mutex;
    std::map<std::string, Device> _devices;
};
}  // namespace

std::unique_ptr<IBackend> CreateEvdevBackend()
{
    return std::make_unique<EvdevBackend>();
}

}  // namespace controllers
