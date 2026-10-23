#include "evdevbackend.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/hidraw.h>
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
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "devicedatabase.h"
#include "hid/drivers/surfacedial.h"
#include "hid/hiddescriptor.h"
#include "hid/hidpad.h"
#include "hid/hidrawnode.h"

namespace controllers
{

// BTN_GRIP* came to linux/input-event-codes.h with the kernels whose xpad sends them; a build against older headers
// still meets them at run time, so their UAPI values (fixed once released) stand in
#ifndef BTN_GRIPL
#define BTN_GRIPL 0x224
#endif
#ifndef BTN_GRIPR
#define BTN_GRIPR 0x225
#endif
#ifndef BTN_GRIPL2
#define BTN_GRIPL2 0x226
#endif
#ifndef BTN_GRIPR2
#define BTN_GRIPR2 0x227
#endif

const char* EvdevPadButton(unsigned code)
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
        // xpad's Elite 2 paddles (drivers/input/joystick/xpad.c, xpad_btn_paddles[] "paddle upper right, lower right /
        // upper left, lower left", reported from data[18] bits 0-3 and zeroed while a custom profile slot is active):
        // current kernels send BTN_GRIPR, BTN_GRIPR2, BTN_GRIPL, BTN_GRIPL2, older ones (6.1+) BTN_TRIGGER_HAPPY5-8 in
        // the same bit order. That is the order of the Bluetooth report's paddle nibble HidPad reads on macOS (0x01 P1
        // upper right .. 0x08 P4 lower left, research-host-apis.md §2.3b), so the same paddle has the same name
        case BTN_GRIPR:
        case BTN_TRIGGER_HAPPY5: return "paddle.p1";
        case BTN_GRIPR2:
        case BTN_TRIGGER_HAPPY6: return "paddle.p2";
        case BTN_GRIPL:
        case BTN_TRIGGER_HAPPY7: return "paddle.p3";
        case BTN_GRIPL2:
        case BTN_TRIGGER_HAPPY8: return "paddle.p4";
        default: return nullptr;
    }
}


namespace
{
constexpr const char* kInputDir = "/dev/input";
constexpr const char* kDevDir = "/dev";
constexpr const char* kSysDir = "/sys";

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

/// A hidraw descriptor shared with the Dial's writer thread: closed when the last of them lets go, so a write in
/// flight never lands on a reused descriptor
struct HidrawHandle
{
    explicit HidrawHandle(int descriptor) : fd(descriptor)
    {
    }
    ~HidrawHandle()
    {
        close(fd);
    }
    HidrawHandle(const HidrawHandle&) = delete;
    HidrawHandle& operator=(const HidrawHandle&) = delete;
    const int fd;
};

/// The Dial's writer: output and feature reports, and feature report 1 read (the found settings, the read-back)
std::unique_ptr<SurfaceDialWriter> MakeDialWriter(std::shared_ptr<HidrawHandle> handle)
{
    return std::make_unique<SurfaceDialWriter>(
        [handle](SurfaceDialWriter::ReportType type, const std::vector<uint8_t>& report) {
            // The first byte is the report id, as hidraw takes it for numbered reports
            if (type == SurfaceDialWriter::ReportType::Feature)
            {
                std::vector<uint8_t> buffer = report;  // the ioctl's buffer is not const
                return ioctl(handle->fd, HIDIOCSFEATURE(buffer.size()), buffer.data()) >= 0;
            }
            return write(handle->fd, report.data(), report.size()) == static_cast<ssize_t>(report.size());
        },
        [handle](std::vector<uint8_t>& report) {
            report.assign(16, 0);
            report[0] = SurfaceDial::kFeatureReport;
            const int size = ioctl(handle->fd, HIDIOCGFEATURE(report.size()), report.data());
            if (size < 0)
                return false;
            report.resize(static_cast<size_t>(size));
            return true;
        });
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
    std::string notice;   ///< DeviceNotice: what is missing and how to fix it

    /// region <hidraw (hidrawnode.h): the same HID device's raw reports>
    std::string event;    ///< "event7": the sysfs name of the evdev node
    std::string layout;   ///< the device database layout, for the HidPad of the hidraw descriptor
    uint16_t vendor = 0;
    uint16_t product = 0;
    HidrawUse use = HidrawUse::None;
    std::string hidrawNode;
    int hidrawFd = -1;            ///< HidrawUse::Input: read on the backend's thread
    std::unique_ptr<HidPad> hid;  ///< HidrawUse::Input: its reports, paddles and profile included
    HidrawSourceMerge merge;          ///< one device from both: the hidraw reports while they are read, evdev otherwise
    std::shared_ptr<HidrawHandle> hidrawOut;    ///< HidrawUse::Output
    std::unique_ptr<SurfaceDialWriter> writer;  ///< HidrawUse::Output: the Dial's feature and output reports
    /// endregion </hidraw>
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
        {
            _inputWatch = inotify_add_watch(_inotify, kInputDir, IN_CREATE | IN_ATTRIB | IN_DELETE);
            // The hidraw nodes: udev may give the user access to one after its evdev node (RetryHidraw)
            _devWatch = inotify_add_watch(_inotify, kDevDir, IN_CREATE | IN_ATTRIB);
        }
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
        std::map<std::string, std::unique_ptr<Device>> devices;
        {
            std::lock_guard lock(_mutex);
            devices.swap(_devices);
        }
        for (auto& [node, device] : devices)
        {
            _sink->OnDeviceRemoved(device->info.id);
            CloseFiles(*device);
        }
        devices.clear();  // the Dials' writers finish a write in flight
        if (_inotify >= 0)
            close(_inotify);
        close(_wake[0]);
        close(_wake[1]);
        _inotify = -1;
        _inputWatch = -1;
        _devWatch = -1;
        _sink = nullptr;
    }

    std::string DeviceNotice(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        const Device* known = Find(device);
        return known != nullptr ? known->notice : std::string();
    }

    bool HasRumble(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        const Device* known = Find(device);
        return known != nullptr && known->rumble;
    }

    uint8_t RumbleActuators(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        const Device* known = Find(device);
        if (known == nullptr)
            return 0;
        if (known->writer)
            return kHapticClicker;
        return known->rumble ? static_cast<uint8_t>(kRumbleLow | kRumbleHigh) : 0;
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

    /// The Surface Dial's clicks: output report 1 through its hidraw node (SurfaceDial::HapticReport), queued for its
    /// writer thread
    bool PlayClicks(const HostDeviceId& device, const HapticClicks& clicks) override
    {
        std::lock_guard lock(_mutex);
        if (SurfaceDialWriter* writer = WriterLocked(device))
        {
            writer->Clicks(clicks);
            return true;
        }
        return false;
    }

    /// region <Surface outputs: the Dial's patterns and steps, through its hidraw writer>
    uint8_t Outputs(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        return WriterLocked(device) != nullptr ? static_cast<uint8_t>(kOutputHaptic | kOutputSteps) : 0;
    }

    bool Output(const HostDeviceId& device, const HapticOut& out) override
    {
        std::lock_guard lock(_mutex);
        if (SurfaceDialWriter* writer = WriterLocked(device))
        {
            writer->Pattern(out.pattern);
            return true;
        }
        return false;
    }

    /// Feature report 1 (HIDIOCSFEATURE). Kept by id: the Dial gets it again when it reconnects (it forgets its
    /// settings when it sleeps)
    bool Output(const HostDeviceId& device, const StepsOut& out) override
    {
        std::lock_guard lock(_mutex);
        SurfaceDialWriter* writer = WriterLocked(device);
        if (writer == nullptr)
            return false;
        _dialSteps[device] = out;
        writer->Steps(out);
        return true;
    }
    /// endregion </Surface outputs>

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
            if (known->info.id == device)
                return known.get();
        return nullptr;
    }
    const Device* Find(const HostDeviceId& device) const
    {
        for (const auto& [node, known] : _devices)
            if (known->info.id == device)
                return known.get();
        return nullptr;
    }
    SurfaceDialWriter* WriterLocked(const HostDeviceId& device) const
    {
        const Device* known = Find(device);
        return known != nullptr ? known->writer.get() : nullptr;
    }

    static void CloseFiles(Device& device)
    {
        close(device.fd);
        if (device.hidrawFd >= 0)
            close(device.hidrawFd);
        device.hidrawFd = -1;
    }

    void Scan()
    {
        DIR* dir = opendir(kInputDir);
        if (dir == nullptr)
            return;
        while (dirent* entry = readdir(dir))
            if (std::strncmp(entry->d_name, "event", 5) == 0)
                Open(entry->d_name);
        closedir(dir);
    }

    void Open(const std::string& event)
    {
        const std::string node = std::string(kInputDir) + "/" + event;
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

        auto device = std::make_unique<Device>();
        device->fd = fd;
        device->node = node;
        device->event = event;
        device->layout = match.layout;
        device->pad = pad;
        device->dial = dial;
        device->rumble = writable && TestBit(Bits(fd, EV_FF, FF_MAX), FF_RUMBLE);
        if (!device->rumble)
            device->rumbleUnavailable = writable ? "The device offers no rumble (FF_RUMBLE) to Linux"
                                                 : "No write access to " + node + ": a udev rule that gives the user "
                                                   "write access drives its motors";
        // D15: over USB the paddles are xpad's buttons on profile 0 only, and the profile is not readable
        if (!bluetooth && match.family == "xbox-elite-2")
            device->notice = XpadPaddlesNotice();
        const std::vector<unsigned long> axes = Bits(fd, EV_ABS, ABS_MAX);
        for (unsigned code = 0; code <= ABS_MAX; ++code)
            if (TestBit(axes, code))
            {
                input_absinfo info{};
                if (ioctl(fd, EVIOCGABS(code), &info) == 0)
                    device->abs[code] = info;
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
        device->info = {id, match.name.empty() ? name : match.name, match.family, match.layout, Name()};

        // The same HID device's hidraw node, when this device needs it: before the device is announced, so its
        // outputs and controls are right from the first moment
        device->vendor = ident.vendor;
        device->product = ident.product;
        device->use = HidrawUseFor(bluetooth, ident.vendor, ident.product, match.family);
        if (device->use != HidrawUse::None)
            AttachHidraw(*device);

        HostDeviceInfo announced;
        {
            std::lock_guard lock(_mutex);
            for (const auto& [other, known] : _devices)
                if (known->info.id == device->info.id)
                    device->info.id += "#" + std::to_string(_devices.size() + 1);
            // A Dial that reconnects gets the steps it had (architecture.md §9: the driver applies them again)
            if (device->writer)
                if (const auto steps = _dialSteps.find(device->info.id); steps != _dialSteps.end())
                    device->writer->Steps(steps->second);
            announced = device->info;
            _devices[node] = std::move(device);
        }
        _sink->OnDeviceAdded(announced);
    }

    /// Open the hidraw node of @p device's HID device for its use; on failure the notice (Input) or the Test haptics
    /// reason (Output) says why and what to install. On the backend's thread (or in Start), before or after the
    /// device is announced; the hidraw input side (hidrawFd, hid, merge) is only touched there
    void AttachHidraw(Device& device)
    {
        HidrawNode found;
        if (!FindHidrawNode(kSysDir, kDevDir, device.event, device.vendor, device.product, found))
        {
            Unavailable(device, HidrawUnavailable(device.use, {}, 0, {}));
            return;
        }
        device.hidrawNode = found.node;
        if (device.use == HidrawUse::Output)
        {
            int fd = open(found.node.c_str(), O_RDWR | O_CLOEXEC);
            if (fd < 0 && (errno == EACCES || errno == EPERM))
                fd = open(found.node.c_str(), O_WRONLY | O_CLOEXEC);
            if (fd < 0)
            {
                const int error = errno;
                Unavailable(device, HidrawUnavailable(device.use, found.node, error, std::strerror(error)));
                return;
            }
            std::lock_guard lock(_mutex);  // the writer is what Outputs and PlayClicks look for
            device.hidrawOut = std::make_shared<HidrawHandle>(fd);
            device.writer = MakeDialWriter(device.hidrawOut);
            device.rumbleUnavailable.clear();  // no motors, but its clicks play (PlayClicks)
            return;
        }
        const int fd = open(found.node.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
        {
            const int error = errno;
            Unavailable(device, HidrawUnavailable(device.use, found.node, error, std::strerror(error)));
            return;
        }
        int size = 0;
        bool described = ioctl(fd, HIDIOCGRDESCSIZE, &size) >= 0 && size > 0 && size <= HID_MAX_DESCRIPTOR_SIZE;
        hidraw_report_descriptor descriptor{};
        descriptor.size = described ? static_cast<uint32_t>(size) : 0;
        described = described && ioctl(fd, HIDIOCGRDESC, &descriptor) >= 0;
        HidDescriptor parsed;
        std::string error;
        if (!described || !ParseHidDescriptor(descriptor.value, descriptor.size, parsed, error))
        {
            close(fd);
            Unavailable(device, "The report descriptor of " + found.node + " could not be read" +
                                    (error.empty() ? std::string() : " (" + error + ")") +
                                    ": the paddles and the onboard profile are not seen");
            return;
        }
        auto hid = std::make_unique<HidPad>(parsed, HidLayoutFor(device.layout));
        if (hid->Empty())
        {
            close(fd);
            Unavailable(device, found.node + " describes no pad controls: the paddles and the onboard profile are not "
                                             "seen");
            return;
        }
        device.hid = std::move(hid);
        device.hidrawFd = fd;  // polled from the next round on
        {
            std::lock_guard lock(_mutex);
            device.notice.clear();
        }
        // From now on every control comes from the hidraw reports; what evdev held is released (not under _mutex:
        // the sink may ask the backend)
        device.merge.Switch(HidrawSourceMerge::Source::Hidraw, Emitter(device));
    }

    void Unavailable(Device& device, const std::string& why)
    {
        std::lock_guard lock(_mutex);
        if (device.use == HidrawUse::Output)
            device.rumbleUnavailable = why;
        else
            device.notice = why;
    }

    /// The devices whose hidraw node did not open: udev may have given access since (a hidraw node's ACL arrives
    /// after its evdev node's). One that opens now is announced again, so the router sees its outputs and the dialog
    /// its controls
    void RetryHidraw()
    {
        std::vector<Device*> waiting;
        {
            std::lock_guard lock(_mutex);
            for (auto& [node, device] : _devices)
                if ((device->use == HidrawUse::Input && device->hidrawFd < 0) ||
                    (device->use == HidrawUse::Output && !device->writer))
                    waiting.push_back(device.get());  // only this thread erases
        }
        for (Device* device : waiting)
        {
            const std::string id = device->info.id;
            AttachHidraw(*device);
            const bool attached = device->hidrawFd >= 0 || device->writer;
            if (!attached)
                continue;
            _sink->OnDeviceRemoved(id);
            {
                std::lock_guard lock(_mutex);
                if (device->writer)
                    if (const auto steps = _dialSteps.find(id); steps != _dialSteps.end())
                        device->writer->Steps(steps->second);
            }
            _sink->OnDeviceAdded(device->info);
        }
    }

    void Close(const std::string& node)
    {
        std::unique_ptr<Device> device;
        {
            std::lock_guard lock(_mutex);
            auto it = _devices.find(node);
            if (it == _devices.end())
                return;
            device = std::move(it->second);
            _devices.erase(it);
        }
        CloseFiles(*device);
        _sink->OnDeviceRemoved(device->info.id);
        // The Dial's writer ends with the device: a write in flight finishes first (a gone device refuses it at once)
    }

    /// hidraw stopped (the node went away before its evdev node, or failed): evdev is the source again
    void DetachHidraw(Device& device)
    {
        if (device.hidrawFd >= 0)
            close(device.hidrawFd);
        device.hidrawFd = -1;
        device.hid.reset();
        {
            std::lock_guard lock(_mutex);
            device.notice = "The hidraw node " + device.hidrawNode + " went away: the paddles and the onboard "
                            "profile are not seen until the controller reconnects";
        }
        device.merge.Switch(HidrawSourceMerge::Source::Evdev, Emitter(device));
    }

    void Run()
    {
        struct Polled
        {
            std::string node;
            bool hidraw = false;
        };
        std::vector<pollfd> fds;
        std::vector<Polled> polled;
        for (;;)
        {
            fds.clear();
            polled.clear();
            fds.push_back({_wake[0], POLLIN, 0});
            if (_inotify >= 0)
                fds.push_back({_inotify, POLLIN, 0});
            bool dials = false;
            {
                std::lock_guard lock(_mutex);
                for (const auto& [node, device] : _devices)
                {
                    fds.push_back({device->fd, POLLIN, 0});
                    polled.push_back({node, false});
                    if (device->hidrawFd >= 0)
                    {
                        fds.push_back({device->hidrawFd, POLLIN, 0});
                        polled.push_back({node, true});
                    }
                    dials = dials || device->dial;
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
                    bool scan = false;
                    bool hidraw = false;
                    alignas(inotify_event) char buffer[4096];
                    ssize_t got = 0;
                    while ((got = read(_inotify, buffer, sizeof(buffer))) > 0)
                        for (ssize_t at = 0; at + static_cast<ssize_t>(sizeof(inotify_event)) <= got;)
                        {
                            const auto* change = reinterpret_cast<const inotify_event*>(buffer + at);
                            if (change->wd == _devWatch)
                                hidraw = hidraw || (change->len > 0 && std::strncmp(change->name, "hidraw", 6) == 0);
                            else
                                scan = true;
                            at += static_cast<ssize_t>(sizeof(inotify_event) + change->len);
                        }
                    if (scan)
                        Scan();  // a new node: udev sets its permissions a moment later (IN_ATTRIB)
                    if (hidraw)
                        RetryHidraw();
                }
            }
            for (size_t i = first; i < fds.size(); ++i)
            {
                const Polled& entry = polled[i - first];
                if (entry.hidraw)
                {
                    if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                    {
                        if (Device* device = Lookup(entry.node))
                            DetachHidraw(*device);
                    }
                    else if (fds[i].revents & POLLIN)
                        ReadHidraw(entry.node);
                    continue;
                }
                if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                {
                    Close(entry.node);
                    continue;
                }
                if (fds[i].revents & POLLIN)
                    Read(entry.node);
            }
        }
    }

    /// The device of an evdev node, or nullptr (it closed earlier in this round). Only this thread erases
    Device* Lookup(const std::string& node)
    {
        std::lock_guard lock(_mutex);
        auto it = _devices.find(node);
        return it == _devices.end() ? nullptr : it->second.get();
    }

    void ExpireDials()
    {
        const uint64_t now = DialNowMs();
        std::vector<Device*> dials;
        {
            std::lock_guard lock(_mutex);
            for (auto& [node, device] : _devices)
                if (device->dial)
                    dials.push_back(device.get());  // only this thread erases
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
        Device* device = Lookup(node);
        if (device == nullptr)
            return;
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

    /// One read is one input report, its id first (numbered reports), as IOHID gives it on macOS
    void ReadHidraw(const std::string& node)
    {
        Device* device = Lookup(node);
        if (device == nullptr || device->hidrawFd < 0 || !device->hid)
            return;
        uint8_t report[256];
        const ControlSink sink = Emitter(*device);
        for (;;)
        {
            const ssize_t got = read(device->hidrawFd, report, sizeof(report));
            if (got <= 0)
            {
                if (got < 0 && errno != EAGAIN && errno != EINTR)
                    DetachHidraw(*device);
                return;
            }
            device->hid->Decode(report, static_cast<size_t>(got), [device, &sink](const std::string& control,
                                                                                  float value) {
                device->merge.Control(HidrawSourceMerge::Source::Hidraw, control, value, sink);
            });
        }
    }

    /// What the merge passes on goes to the sink as the device's control
    /// (two pointers: no allocation; the device outlives every call, only this thread erases it)
    ControlSink Emitter(const Device& device)
    {
        return [this, known = &device](const std::string& control, float value) {
            _sink->OnControl({known->info.id, control, value, 0});
        };
    }

    /// An evdev control of a pad or a joystick, through the merge: dropped while the hidraw reports are the source
    void Emit(Device& device, const std::string& control, float value)
    {
        if (device.merge.Active() == HidrawSourceMerge::Source::Evdev)
            device.merge.Control(HidrawSourceMerge::Source::Evdev, control, value, Emitter(device));
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
            if (const char* control = device.pad ? EvdevPadButton(event.code) : nullptr)
                Emit(device, control, value);
            else if (event.code >= BTN_JOYSTICK && event.code < BTN_JOYSTICK + 32)
                Emit(device, "button." + std::to_string(event.code - BTN_JOYSTICK), value);
            else if (event.code >= BTN_GAMEPAD && event.code < BTN_GAMEPAD + 16)
                Emit(device, "button." + std::to_string(event.code - BTN_GAMEPAD), value);
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
                Emit(device, x ? (was < 0 ? "dpad.left" : "dpad.right") : (was < 0 ? "dpad.up" : "dpad.down"), 0.0f);
            if (now != 0)
                Emit(device, x ? (now < 0 ? "dpad.left" : "dpad.right") : (now < 0 ? "dpad.up" : "dpad.down"), 1.0f);
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
            Emit(device, control, unit);
        else if (control != nullptr)
            Emit(device, control, unit * 2.0f - 1.0f);  // evdev y is + down, as the canonical one
        else
            Emit(device, "axis." + std::to_string(event.code), unit * 2.0f - 1.0f);
    }

    IBackendSink* _sink = nullptr;
    std::thread _thread;
    int _wake[2] = {-1, -1};
    int _inotify = -1;
    int _inputWatch = -1;
    int _devWatch = -1;
    mutable std::mutex _mutex;
    std::map<std::string, std::unique_ptr<Device>> _devices;
    std::map<HostDeviceId, StepsOut> _dialSteps;  ///< under _mutex: each Dial's last steps, kept across a reconnect
};
}  // namespace

std::unique_ptr<IBackend> CreateEvdevBackend()
{
    return std::make_unique<EvdevBackend>();
}

}  // namespace controllers
