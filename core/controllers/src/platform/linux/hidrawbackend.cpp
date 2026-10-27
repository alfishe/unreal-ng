#include "hidrawbackend.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "devicedatabase.h"
#include "hid/drivers/steamdeck.h"

namespace controllers
{

namespace
{
using Clock = std::chrono::steady_clock;

constexpr const char* kDevDir = "/dev";
constexpr const char* kSysDir = "/sys/class/hidraw";
/// The motors are written again at this period while on: the Deck stops a motor that hears nothing (hid-steam resends
/// at 20 Hz)
constexpr auto kRefresh = std::chrono::milliseconds(50);

std::string ReadLine(const std::string& path)
{
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    return line;
}

/// KEY=value of a sysfs uevent file
std::map<std::string, std::string> Uevent(const std::string& path)
{
    std::map<std::string, std::string> out;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line))
        if (const size_t eq = line.find('='); eq != std::string::npos)
            out[line.substr(0, eq)] = line.substr(eq + 1);
    return out;
}

std::string Hex4(unsigned value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "%04x", value & 0xFFFF);
    return text;
}

/// Steam is running: it owns the Deck's settings (the lizard mode among them), so we leave them alone
bool SteamRunning()
{
    DIR* proc = opendir("/proc");
    if (proc == nullptr)
        return false;
    bool found = false;
    while (dirent* entry = readdir(proc))
    {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
            continue;
        if (ReadLine(std::string("/proc/") + entry->d_name + "/comm") == "steam")
        {
            found = true;
            break;
        }
    }
    closedir(proc);
    return found;
}

struct Device
{
    int fd = -1;
    std::string node;
    HostDeviceInfo info;
    bool writable = false;
    bool lizardOff = false;  ///< we switched the lizard mode off: feed its watchdog
    SteamDeck deck;
    // The wanted actuators (SetRumble) and what was written
    RumbleLevels wanted;
    float motors[2] = {};
    Clock::time_point motorsAt;
};

class HidrawBackend : public IBackend
{
public:
    ~HidrawBackend() override
    {
        Stop();
    }

    std::string Name() const override
    {
        return "hidraw";
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
        fcntl(_wake[0], F_SETFL, O_NONBLOCK);
        _inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (_inotify >= 0)
            inotify_add_watch(_inotify, kDevDir, IN_CREATE | IN_ATTRIB | IN_DELETE);
        _sink = &sink;
        _stopping = false;
        Scan();
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
        Wake();
        _thread.join();
        for (auto& [node, device] : _devices)
        {
            if (device.writable)
                Feature(device, SteamDeck::RumbleReport(0, 0));
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

    size_t DeviceCount() const override
    {
        std::lock_guard lock(_mutex);
        return _devices.size();
    }

    bool HasRumble(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        const Device* known = Find(device);
        return known != nullptr && known->writable;
    }

    uint8_t RumbleActuators(const HostDeviceId& device) const override
    {
        return HasRumble(device) ? static_cast<uint8_t>(kRumbleLow | kRumbleHigh | kHapticClicker)
                                 : 0;
    }

    std::string RumbleUnavailable(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        const Device* known = Find(device);
        if (known == nullptr)
            return "The device is not connected";
        return known->writable ? std::string() : "No write access to " + known->node + ": a udev rule (uaccess) gives it";
    }

    /// The levels are kept and written by the backend's thread: motors on a change and every kRefresh while on
    bool SetRumble(const HostDeviceId& device, const RumbleLevels& levels) override
    {
        {
            std::lock_guard lock(_mutex);
            Device* known = Find(device);
            if (known == nullptr || !known->writable)
                return false;
            known->wanted = levels;
        }
        Wake();
        return true;
    }

    /// A clicker's effect on both trackpads: one click is the firmware's, a train of them 2 ms pulses, a buzz 100 ms
    /// of a 200 Hz train
    bool PlayClicks(const HostDeviceId& device, const HapticClicks& clicks) override
    {
        std::lock_guard lock(_mutex);
        Device* known = Find(device);
        if (known == nullptr || !known->writable)
            return false;
        const uint16_t count = std::max<uint16_t>(1, clicks.count);
        if (clicks.wave == HapticClicks::Wave::Click && count == 1)
        {
            SteamDeck::PadEffect click;
            click.command = SteamDeck::PadCommand::Click;
            return Feature(*known, SteamDeck::PadEffectReport(click));
        }
        if (clicks.wave == HapticClicks::Wave::Click)
        {
            const unsigned period = std::max<unsigned>(clicks.periodMs * 1000u, 4000u);
            return Feature(*known, SteamDeck::PulseReport(SteamDeck::Pad::Both, 2000,
                                                          static_cast<uint16_t>(std::min(period - 2000u, 65535u)),
                                                          count, 0));
        }
        return Feature(*known, SteamDeck::PulseReport(SteamDeck::Pad::Both, 2500, 2500,
                                                      static_cast<uint16_t>(20 * count), 0));
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

    void Wake()
    {
        const char byte = 1;
        [[maybe_unused]] const ssize_t written = write(_wake[1], &byte, 1);
    }

    static bool Feature(const Device& device, std::vector<uint8_t> report)
    {
        return ioctl(device.fd, HIDIOCSFEATURE(report.size()), report.data()) >= 0;
    }

    void Scan()
    {
        DIR* dir = opendir(kSysDir);
        if (dir == nullptr)
            return;
        while (dirent* entry = readdir(dir))
            if (std::strncmp(entry->d_name, "hidraw", 6) == 0)
                Open(entry->d_name);
        closedir(dir);
    }

    void Open(const std::string& name)
    {
        const std::string node = std::string(kDevDir) + "/" + name;
        {
            std::lock_guard lock(_mutex);
            if (_devices.count(node) != 0)
                return;
        }
        const std::string sys = std::string(kSysDir) + "/" + name + "/device";
        std::map<std::string, std::string> uevent = Uevent(sys + "/uevent");
        unsigned bus = 0;
        unsigned vendor = 0;
        unsigned product = 0;
        if (std::sscanf(uevent["HID_ID"].c_str(), "%x:%x:%x", &bus, &vendor, &product) != 3 ||
            !SteamDeck::Handles(static_cast<uint16_t>(vendor), static_cast<uint16_t>(product)))
            return;
        // The controller is interface 2; 0 and 1 are the lizard mode's keyboard and mouse
        if (ReadLine(sys + "/../bInterfaceNumber") != "0" + std::to_string(SteamDeck::kInterface))
            return;

        bool writable = true;
        int fd = open(node.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
        {
            writable = false;
            fd = open(node.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        }
        if (fd < 0)
            return;  // no access yet: udev sets the ACL a moment later (IN_ATTRIB)

        unsigned version = 0;
        std::sscanf(ReadLine(sys + "/../../bcdDevice").c_str(), "%x", &version);
        const std::string serial = ReadLine(sys + "/../../serial");
        const std::string hidName = uevent["HID_NAME"];
        const DeviceMatch match = DeviceDatabase::Builtin().Lookup(
            {"usb", static_cast<uint16_t>(vendor), static_cast<uint16_t>(product), static_cast<int>(version), hidName,
             Name()});
        if (match.ignore)
        {
            close(fd);
            return;
        }

        Device device;
        device.fd = fd;
        device.node = node;
        device.writable = writable;
        // architecture.md §5: usb:<vid>:<pid>:<version>:<serial>
        std::string id = "usb:" + Hex4(vendor) + ":" + Hex4(product) + ":" + Hex4(version);
        id += serial.empty() ? "@" + name : ":" + serial;
        device.info = {id, match.name.empty() ? hidName : match.name, match.family, match.layout, Name()};
        if (writable && !SteamRunning())
        {
            for (const std::vector<uint8_t>& report : SteamDeck::LizardOffReports())
                Feature(device, report);
            // A reply may linger after the settings: read it away (SDL does the same)
            std::vector<uint8_t> reply(SteamDeck::kFeatureSize, 0);
            ioctl(fd, HIDIOCGFEATURE(reply.size()), reply.data());
            device.lizardOff = true;
        }
        {
            std::lock_guard lock(_mutex);
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
            bool active = false;
            {
                std::lock_guard lock(_mutex);
                if (_stopping)
                    return;
                for (const auto& [node, device] : _devices)
                {
                    fds.push_back({device.fd, POLLIN, 0});
                    nodes.push_back(node);
                    active = active || Playing(device);
                }
            }
            const int timeout = active ? static_cast<int>(kRefresh.count() / 2) : -1;
            if (poll(fds.data(), fds.size(), timeout) < 0 && errno != EINTR)
                return;
            if (fds[0].revents & POLLIN)
            {
                char drain[64];
                while (read(_wake[0], drain, sizeof(drain)) > 0)
                {
                }
            }
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
                    Scan();
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
            WriteActuators();
        }
    }

    static bool Playing(const Device& device)
    {
        const RumbleLevels& w = device.wanted;
        return w.low > 0 || w.high > 0 || device.motors[0] > 0 || device.motors[1] > 0;
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
        uint8_t report[SteamDeck::kReportSize * 2];
        for (;;)
        {
            const ssize_t got = read(device->fd, report, sizeof(report));
            if (got <= 0)
            {
                if (got < 0 && errno == ENODEV)
                    Close(node);
                return;
            }
            const std::string& id = device->info.id;
            device->deck.Decode(report, static_cast<size_t>(got),
                                [this, &id](const std::string& control, float value) {
                                    _sink->OnControl({id, control, value, 0});
                                });
            if (device->deck.WatchdogDue() && device->lizardOff)
            {
                std::lock_guard lock(_mutex);
                for (const std::vector<uint8_t>& feed : SteamDeck::WatchdogReports())
                    Feature(*device, feed);
            }
        }
    }

    /// The actuators of every device, on this thread (the feature reports go out in order with the watchdog's)
    void WriteActuators()
    {
        std::lock_guard lock(_mutex);
        const Clock::time_point now = Clock::now();
        for (auto& [node, device] : _devices)
        {
            if (!device.writable)
                continue;
            const RumbleLevels& w = device.wanted;
            const bool changed = w.low != device.motors[0] || w.high != device.motors[1];
            const bool on = w.low > 0 || w.high > 0;
            if (changed || (on && now - device.motorsAt >= kRefresh))
            {
                Feature(device, SteamDeck::RumbleReport(w.low, w.high));
                device.motors[0] = w.low;
                device.motors[1] = w.high;
                device.motorsAt = now;
            }
        }
    }

    IBackendSink* _sink = nullptr;
    std::thread _thread;
    int _wake[2] = {-1, -1};
    int _inotify = -1;
    bool _stopping = false;
    mutable std::mutex _mutex;
    std::map<std::string, Device> _devices;
};
}  // namespace

std::unique_ptr<IBackend> CreateHidrawBackend()
{
    return std::make_unique<HidrawBackend>();
}

}  // namespace controllers
