#include "evdevbackend.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "devicedatabase.h"
#include "devicestatus.h"
#include "hid/drivers/dualsenseoutput.h"
#include "hid/drivers/gamecubeadapter.h"
#include "hid/drivers/padprotocol.h"
#include "hid/drivers/sonypad.h"
#include "hid/drivers/surfacedial.h"
#include "hid/drivers/switchpro.h"
#include "hid/drivers/xboxbluetoothoutput.h"
#include "hid/hiddescriptor.h"
#include "hid/hidpad.h"
#include "hid/hidrawnode.h"
#include "joyconpairing.h"
#include "platform/linuxpads.h"

namespace controllers
{

const char* EvdevPadButton(unsigned code)
{
    return LinuxPadButton(LinuxPadKind::Standard, code);
}

namespace
{
constexpr const char* kInputDir = "/dev/input";
constexpr const char* kDevDir = "/dev";
constexpr const char* kSysDir = "/sys";
constexpr const char* kProcDir = "/proc";

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

std::string StringIoctl(int fd, unsigned long request)
{
    char text[256] = {};
    if (ioctl(fd, request, text) < 0)
        return {};
    return text;
}

std::string ReadFile(const std::string& path)
{
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

/// A pad's keys: the gamepad range (BTN_SOUTH .. BTN_THUMBR) or the d-pad buttons. A left Joy-Con has no BTN_SOUTH
/// (BTN_GAMEPAD): its face buttons are BTN_DPAD_*
bool PadKeys(const std::vector<unsigned long>& keys)
{
    for (unsigned code = BTN_SOUTH; code <= BTN_THUMBR; ++code)
        if (TestBit(keys, code))
            return true;
    for (unsigned code = BTN_DPAD_UP; code <= BTN_DPAD_RIGHT; ++code)
        if (TestBit(keys, code))
            return true;
    return false;
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

/// The writer thread of a pad whose protocol the backend talks through hidraw (SonyProtocol, SwitchProtocol): output
/// reports written, feature reports read (HIDIOCGFEATURE, the report id first)
std::unique_ptr<PadPump> MakePump(std::shared_ptr<HidrawHandle> handle, std::unique_ptr<IPadProtocol> protocol)
{
    return std::make_unique<PadPump>(
        std::move(protocol),
        [handle](const std::vector<uint8_t>& report) {
            return write(handle->fd, report.data(), report.size()) == static_cast<ssize_t>(report.size());
        },
        [handle](uint8_t id, size_t size, std::vector<uint8_t>& data) {
            data.assign(size, 0);
            data[0] = id;
            const int got = ioctl(handle->fd, HIDIOCGFEATURE(data.size()), data.data());
            if (got < 0)
                return false;
            data.resize(static_cast<size_t>(got));
            return true;
        });
}

constexpr uint8_t kMotors = kRumbleLow | kRumbleHigh;
constexpr uint8_t kTriggers = kRumbleLeftTrigger | kRumbleRightTrigger;

struct Device
{
    int fd = -1;
    std::string node;
    HostDeviceInfo info;
    /// Where its controls go: its own id, or its pair's while it is a half of one (Joy-Cons)
    HostDeviceId reported;
    bool pad = false;  ///< pad keys (PadKeys): canonical names by kind; else a joystick with button.<n> / axis.<n>
    LinuxPadKind kind = LinuxPadKind::Standard;
    /// hid-playstation / hid-sony send BTN_TL2 / BTN_TR2 beside the analog ABS_Z / ABS_RZ: the keys are left out
    bool analogTriggers = false;
    /// hid-generic serves it: its keys and axes are the HID usages' numbers, named by the family's HID map
    /// (LinuxGenericButton / LinuxGenericAxis) as HidPad names them on the other OSes
    bool generic = false;
    bool dial = false;  ///< a Surface Dial (hid-generic): REL_DIAL is `knob`, BTN_0 `push`, BTN_TOUCH `touch`
    bool surface = false;  ///< a control surface of the database: every control from hidraw (LinuxPadFacts::surface)
    DialTouch hand;     ///< the Dial's `touch`, sooner than its sensor
    bool sensor = false;
    bool pushed = false;
    bool touched = false;
    std::map<unsigned, input_absinfo> abs;
    int hatX = 0;
    int hatY = 0;
    bool bluetooth = false;
    LinuxHidDriver driver = LinuxHidDriver::Unknown;
    bool rumble = false;  ///< LinuxRumble::Evdev: opened for writing and FF_RUMBLE is offered
    std::string rumbleUnavailable;  ///< why not: the Test haptics button's tooltip
    int effect = -1;      ///< the uploaded FF_RUMBLE effect, -1 before the first
    /// DeviceNotice: what is missing and how to fix it. The device's own (xpad's paddles), then one per hidraw use
    /// that did not open (Input, Triggers, SwitchRumble, Protocol)
    std::string baseNotice;
    std::map<uint8_t, std::string> missing;

    /// region <Battery and light (devicestatus.h): the HID device's power supply and LED classes>
    std::string powerSupply;  ///< its battery's sysfs directory; "" when the driver registers none
    BatteryState battery;     ///< under the backend's mutex: as last read on the backend's thread
    BatteryState decoded;     ///< under the backend's mutex: from its own reports (SonyPad, SwitchProPad)
    SysfsLight light;         ///< its colour light, found once at open
    /// endregion </Battery and light>

    /// region <Joy-Cons (joyconpairing.h)>
    int joyCon = 0;  ///< 1 left, 2 right, 0 another device
    HostDeviceInfo single;  ///< what it shows as alone: "Joy-Con (L)" / "Joy-Con (R)"
    bool shown = true;      ///< the sink knows it under its own id (not a pair's half, not a grip half pending)
    /// A charging grip's half under hid-generic: its side from the device info (SwitchProtocol::TypeSink, from the
    /// pump's thread); hidden until it is known
    bool gripPending = false;
    std::shared_ptr<std::atomic<int>> gripType = std::make_shared<std::atomic<int>>(-1);
    /// endregion </Joy-Cons>

    /// region <hidraw (hidrawnode.h): the same HID device's raw reports>
    std::string event;    ///< "event7": the sysfs name of the evdev node
    HidLayout hidLayout;  ///< the family's HID map (HidLayoutFor(DeviceMatch)): hidraw's HidPad, hid-generic's evdev
    uint16_t vendor = 0;
    uint16_t product = 0;
    std::string hidName;  ///< the evdev node's name: the HID device's (SwitchProPad::LayoutFor tells an NES pad by it)
    HidrawUse use = HidrawUse::None;
    std::string hidrawNode;
    int hidrawFd = -1;            ///< Input, Protocol: read on the backend's thread
    std::unique_ptr<HidPad> hid;  ///< Input (the Elite 2): its reports, paddles and profile included
    std::unique_ptr<SonyPad> sony;  ///< Input (the Edge before Linux 7.2), Protocol: the full report
    std::unique_ptr<SwitchProPad> switchPro;  ///< Protocol: 0x30 / 0x21
    HidrawSourceMerge merge;          ///< one device from both: the hidraw reports while they are read, evdev otherwise
    std::shared_ptr<HidrawHandle> hidrawOut;    ///< HidrawUse::Output
    std::unique_ptr<SurfaceDialWriter> writer;  ///< HidrawUse::Output: the Dial's feature and output reports
    /// HidrawUse::Rumble (LinuxRumble::Hidraw): report 3 written on its own thread to a write-only descriptor of the
    /// node; it stops the motors when it ends (device removal, Stop)
    std::unique_ptr<RumbleWriter> rumbleWriter;
    /// HidrawUse::Triggers: a DualSense's trigger effects (DualSenseOutput::TriggerReport) beside FF_RUMBLE's motors
    std::unique_ptr<RumbleWriter> triggerWriter;
    /// HidrawUse::Protocol: the pad's whole protocol; HidrawUse::SwitchRumble: the HD rumble alone
    /// (SwitchProtocol::RumbleOnly)
    std::unique_ptr<PadPump> pump;
    bool pumpLight = false;  ///< the pump sets a light bar colour (a PlayStation pad under Protocol)
    int player = 0;          ///< the number its player LEDs show (Protocol pads), 1..
    /// endregion </hidraw>
};

/// A GameCube adapter (GameCubeAdapter): a hidraw node with no input device. Never shown itself: each port with a
/// controller in it is a device, `<adapter id>:p1` .. `:p4` as on macOS; while its node does not open, the adapter
/// is shown with no controls and the notice
struct HidrawAdapter
{
    std::string node;  ///< /dev/hidrawN
    HostDeviceInfo info;
    std::shared_ptr<HidrawHandle> handle;  ///< null while the node does not open
    std::unique_ptr<PadPump> pump;         ///< the start report 0x13 (GameCubeProtocol)
    GameCubeAdapter decoder;               ///< the backend's thread only
    std::array<bool, GameCubeAdapter::kPorts> shown{};  ///< under the backend's mutex: the ports reported
    std::string notice;                                 ///< why the node did not open; the adapter is shown then
};

HostDeviceId AdapterPortId(const HostDeviceId& adapter, int port)
{
    return adapter + ":p" + std::to_string(port + 1);
}

/// The battery the sink sees: the power supply's, else the one its reports carry
BatteryState BatteryOf(const Device& device)
{
    return device.battery.known ? device.battery : device.decoded;
}

std::string NoticeOf(const Device& device)
{
    std::string notice = device.baseNotice;
    for (const auto& [use, text] : device.missing)
        notice += (notice.empty() ? "" : " ") + text;
    return notice;
}

uint8_t ActuatorsOf(const Device& device)
{
    if (device.writer)
        return kHapticClicker;
    if (device.rumbleWriter)
        return kMotors | kTriggers;
    if (device.pump)
    {
        if (device.sony && device.sony->Model() != SonyModel::DualShock4)
            return kMotors | kTriggers;
        return kMotors;
    }
    uint8_t actuators = device.rumble ? kMotors : 0;
    if (device.triggerWriter)
        actuators |= kTriggers;
    return actuators;
}

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
            _devWatch = inotify_add_watch(_inotify, kDevDir, IN_CREATE | IN_ATTRIB | IN_DELETE);
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
        std::map<std::string, std::unique_ptr<HidrawAdapter>> adapters;
        std::vector<HostDeviceId> gone;
        {
            std::lock_guard lock(_mutex);
            for (const auto& [node, device] : _devices)
            {
                if (device->shown)
                    gone.push_back(device->info.id);
                if (const auto pair = _joyCons.PairOf(device->info.id); pair && pair->left == device->info.id)
                    gone.push_back(pair->id);
            }
            devices.swap(_devices);
            _joyCons = JoyConPairing();
            for (const auto& [id, info] : _steamHeld)
                gone.push_back(id);
            _steamHeld.clear();
            for (const auto& [node, adapter] : _adapters)
                AdapterGoneLocked(*adapter, gone);
            adapters.swap(_adapters);
        }
        for (const HostDeviceId& id : gone)
            _sink->OnDeviceRemoved(id);
        for (auto& [node, device] : devices)
            CloseFiles(*device);
        devices.clear();  // the writers and pumps finish a write in flight, then stop the motors
        adapters.clear();
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
        if (_steamHeld.count(device) != 0)
            return SteamHeldNotice();
        for (const auto& [node, adapter] : _adapters)
            if (adapter->info.id == device)
                return adapter->notice;
        if (const auto [left, right] = PairLocked(device); left != nullptr)
        {
            const std::string a = NoticeOf(*left);
            const std::string b = NoticeOf(*right);
            return a.empty() ? b : (b.empty() || b == a ? a : a + " " + b);
        }
        const Device* known = Find(device);
        return known != nullptr ? NoticeOf(*known) : std::string();
    }

    bool HasRumble(const HostDeviceId& device) const override
    {
        return (RumbleActuators(device) & (kMotors | kTriggers)) != 0;
    }

    /// region <Battery and light>
    /// The power supply as the backend's thread last read it (at open, then every 30 s): hid-input asks the device
    /// for a feature report on a read, which must not hold the caller. Else the pad's own reports (a Protocol pad);
    /// a pair's the lower of its halves'
    BatteryState Battery(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        if (const auto [left, right] = PairLocked(device); left != nullptr)
            return JoyConPairing::Battery(BatteryOf(*left), BatteryOf(*right));
        const Device* known = Find(device);
        return known != nullptr ? BatteryOf(*known) : BatteryState{};
    }

    /// The LED class light, while this user may write its files (the udev rule; they can change at run time), or a
    /// Protocol pad's light bar through its pump
    bool HasLight(const HostDeviceId& device) const override
    {
        return LightUnavailable(device).empty();
    }

    std::string LightUnavailable(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        if (PairLocked(device).first != nullptr)
            return "Joy-Cons have no light the emulator can set (their player lights show the player)";
        const Device* known = Find(device);
        if (known == nullptr)
            return "The device is not connected";
        if (known->pump && known->pumpLight)
            return {};
        if (known->light.kind == SysfsLight::Kind::None)
            return SysfsLightUnavailable(known->light, {});
        for (const std::string& file : known->light.Files())
            if (access(file.c_str(), W_OK) != 0)
                return SysfsLightUnavailable(known->light, file);
        return {};
    }

    /// The LED class's files, written in order (the LED core applies them without waiting for the device); a
    /// Protocol pad's output report through its pump
    bool SetLight(const HostDeviceId& device, const LightColor& color) override
    {
        SysfsLight light;
        {
            std::lock_guard lock(_mutex);
            const Device* known = Find(device);
            if (known == nullptr)
                return false;
            if (known->pump && known->pumpLight)
                return known->pump->SetLight(color);
            light = known->light;
        }
        const auto writes = LightWrites(light, color);
        for (const auto& [file, text] : writes)
        {
            const int fd = open(file.c_str(), O_WRONLY | O_CLOEXEC);
            if (fd < 0)
                return false;
            const bool written = write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
            close(fd);
            if (!written)
                return false;
        }
        return !writes.empty();
    }
    /// endregion </Battery and light>

    uint8_t RumbleActuators(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        if (const auto [left, right] = PairLocked(device); left != nullptr)
            return static_cast<uint8_t>((ActuatorsOf(*left) | ActuatorsOf(*right)) & kMotors);
        const Device* known = Find(device);
        return known != nullptr ? ActuatorsOf(*known) : 0;
    }

    std::string RumbleUnavailable(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        if (const auto [left, right] = PairLocked(device); left != nullptr)
            return (ActuatorsOf(*left) | ActuatorsOf(*right)) & kMotors ? std::string() : left->rumbleUnavailable;
        const Device* known = Find(device);
        if (known == nullptr)
            return "The device is not connected";
        return ActuatorsOf(*known) & (kMotors | kTriggers) ? std::string() : known->rumbleUnavailable;
    }

    /// A Protocol pad or a Switch pad's HD rumble: its pump (queued, never blocks). LinuxRumble::Hidraw: report 3
    /// with the triggers, queued for the device's RumbleWriter. LinuxRumble::Evdev: FF_RUMBLE, strong = the
    /// low-frequency motor, weak = the high one; one effect per device, updated in place and played without an end
    /// (length 0) until the levels go to zero; a DualSense's trigger levels beside it through its trigger writer. A
    /// Joy-Con pair: the left half the low level, the right one the high one (SDL)
    bool SetRumble(const HostDeviceId& device, const RumbleLevels& levels) override
    {
        std::lock_guard lock(_mutex);
        if (const auto [left, right] = PairLocked(device); left != nullptr)
        {
            const bool a = RumbleLocked(*left, SwitchProPad::HalfRumble(levels, true));
            const bool b = RumbleLocked(*right, SwitchProPad::HalfRumble(levels, false));
            return a || b;
        }
        Device* known = Find(device);
        return known != nullptr && RumbleLocked(*known, levels);
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
        // Kept first: a Dial asleep (no writer) when the context changed gets them when it wakes; the router sends a
        // feel once
        _dialSteps[device] = out;
        SurfaceDialWriter* writer = WriterLocked(device);
        if (writer == nullptr)
            return false;
        writer->Steps(out);
        return true;
    }
    /// endregion </Surface outputs>

    size_t DeviceCount() const override
    {
        std::lock_guard lock(_mutex);
        return _devices.size() + _steamHeld.size() + _adapters.size();
    }

private:
    /// Under _mutex
    Device* Find(const HostDeviceId& device) const
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

    /// Both halves of a pair; nullptr, nullptr for any other id
    std::pair<Device*, Device*> PairLocked(const HostDeviceId& device) const
    {
        const std::optional<JoyConPairing::Pair> pair = _joyCons.Find(device);
        if (!pair)
            return {nullptr, nullptr};
        Device* left = Find(pair->left);
        Device* right = Find(pair->right);
        if (left == nullptr || right == nullptr)
            return {nullptr, nullptr};
        return {left, right};
    }

    /// Under _mutex: one device's motors (SetRumble)
    bool RumbleLocked(Device& device, const RumbleLevels& levels)
    {
        if (device.pump)
        {
            device.pump->SetRumble(levels);
            return true;
        }
        if (device.rumbleWriter)
        {
            device.rumbleWriter->Set(levels);
            return true;
        }
        bool done = false;
        if (device.triggerWriter)
        {
            // Only the triggers' levels: the writer skips levels equal to the written ones
            RumbleLevels triggers;
            triggers.leftTrigger = levels.leftTrigger;
            triggers.rightTrigger = levels.rightTrigger;
            device.triggerWriter->Set(triggers);
            done = true;
        }
        if (device.rumble)
            done = FfRumbleLocked(device, levels) || done;
        return done;
    }

    static bool FfRumbleLocked(Device& device, const RumbleLevels& levels)
    {
        auto magnitude = [](float level) {
            return static_cast<uint16_t>((level < 0 ? 0 : level > 1 ? 1 : level) * 65535.0f + 0.5f);
        };
        const bool on = levels.low > 0 || levels.high > 0;
        if (on)
        {
            ff_effect effect{};
            effect.type = FF_RUMBLE;
            effect.id = static_cast<int16_t>(device.effect);
            effect.u.rumble.strong_magnitude = magnitude(levels.low);
            effect.u.rumble.weak_magnitude = magnitude(levels.high);
            if (ioctl(device.fd, EVIOCSFF, &effect) < 0)
                return false;
            device.effect = effect.id;
        }
        if (device.effect < 0)
            return true;
        input_event play{};
        play.type = EV_FF;
        play.code = static_cast<uint16_t>(device.effect);
        play.value = on ? 1 : 0;
        return write(device.fd, &play, sizeof(play)) == static_cast<ssize_t>(sizeof(play));
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
        std::vector<std::string> events;
        while (dirent* entry = readdir(dir))
            if (std::strncmp(entry->d_name, "event", 5) == 0)
                events.emplace_back(entry->d_name);
        closedir(dir);
        // In node order: two Joy-Cons that were there before the start pair the same way each time
        std::sort(events.begin(), events.end(), [](const std::string& a, const std::string& b) {
            return a.size() != b.size() ? a.size() < b.size() : a < b;
        });
        for (const std::string& event : events)
            Open(event);
        RefreshSteamHeld();
        ScanAdapters();
    }

    /// region <GameCube adapters (HidrawAdapter)>
    /// The adapters' hidraw nodes now: a new one opened (or shown with the reason it did not), one that went closed,
    /// one that did not open tried again (udev gives the seat user access a moment after the node appears)
    void ScanAdapters()
    {
        const std::vector<LinuxHidrawDevice> found =
            FindHidrawDevices(kSysDir, kDevDir, GameCubeAdapter::kNintendo, GameCubeAdapter::kAdapter);
        std::vector<std::string> gone;
        std::vector<LinuxHidrawDevice> fresh;
        {
            std::lock_guard lock(_mutex);
            for (const auto& [node, adapter] : _adapters)
                if (std::none_of(found.begin(), found.end(),
                                 [&node](const LinuxHidrawDevice& device) { return device.node == node; }) ||
                    !adapter->handle)
                    gone.push_back(node);
            for (const LinuxHidrawDevice& device : found)
                if (const auto it = _adapters.find(device.node); it == _adapters.end() || !it->second->handle)
                    fresh.push_back(device);
        }
        for (const std::string& node : gone)
            CloseAdapter(node);
        for (const LinuxHidrawDevice& device : fresh)
            OpenAdapter(device);
    }

    void OpenAdapter(const LinuxHidrawDevice& device)
    {
        auto adapter = std::make_unique<HidrawAdapter>();
        adapter->node = device.node;
        const DeviceMatch match = DeviceDatabase::Active()->Lookup(
            {"usb", GameCubeAdapter::kNintendo, GameCubeAdapter::kAdapter, -1, device.name, Name()});
        adapter->info = {device.id, match.name.empty() ? device.name : match.name, match.family, match.layout, Name()};
        const int fd = open(device.node.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0)
        {
            adapter->handle = std::make_shared<HidrawHandle>(fd);
            // hidraw refuses a write under 2 bytes (hidraw_send_report: "passed too short report"): the one-byte
            // start report 0x13 goes with a zero byte after it (the descriptor's report 0x13 has one data byte)
            auto handle = adapter->handle;
            adapter->pump = std::make_unique<PadPump>(
                std::make_unique<GameCubeProtocol>(),
                [handle](const std::vector<uint8_t>& report) {
                    std::vector<uint8_t> out = report;
                    if (out.size() < 2)
                        out.resize(2, 0);
                    return write(handle->fd, out.data(), out.size()) == static_cast<ssize_t>(out.size());
                },
                [](uint8_t, size_t, std::vector<uint8_t>&) { return false; });
        }
        else
            adapter->notice = HidrawUnavailable(HidrawUse::Protocol, device.node, errno, std::strerror(errno), match.family);
        HostDeviceInfo shown;
        {
            std::lock_guard lock(_mutex);
            if (!adapter->handle)
                shown = adapter->info;
            _adapters[device.node] = std::move(adapter);
        }
        if (!shown.id.empty())
            _sink->OnDeviceAdded(shown);
    }

    /// Under _mutex: what the sink knows of @p adapter, into @p gone
    static void AdapterGoneLocked(HidrawAdapter& adapter, std::vector<HostDeviceId>& gone)
    {
        if (!adapter.handle)
            gone.push_back(adapter.info.id);
        for (int port = 0; port < GameCubeAdapter::kPorts; ++port)
            if (adapter.shown[static_cast<size_t>(port)])
                gone.push_back(AdapterPortId(adapter.info.id, port));
        adapter.shown = {};
    }

    void CloseAdapter(const std::string& node)
    {
        std::unique_ptr<HidrawAdapter> adapter;
        std::vector<HostDeviceId> gone;
        {
            std::lock_guard lock(_mutex);
            const auto it = _adapters.find(node);
            if (it == _adapters.end())
                return;
            AdapterGoneLocked(*it->second, gone);
            adapter = std::move(it->second);
            _adapters.erase(it);
        }
        for (const HostDeviceId& id : gone)
            _sink->OnDeviceRemoved(id);
        // The pump finishes a write in flight; the node closes with the last of its handles
    }

    /// Its reports: the pump's first (the start), then the ports, a controller put in one a device, one taken out gone
    void ReadAdapter(const std::string& node)
    {
        HidrawAdapter* adapter = nullptr;
        {
            std::lock_guard lock(_mutex);
            if (const auto it = _adapters.find(node); it != _adapters.end() && it->second->handle)
                adapter = it->second.get();  // only this thread erases
        }
        if (adapter == nullptr)
            return;
        uint8_t report[64];
        for (;;)
        {
            const ssize_t got = read(adapter->handle->fd, report, sizeof(report));
            if (got <= 0)
            {
                if (got < 0 && errno != EAGAIN && errno != EINTR)
                    CloseAdapter(node);
                return;
            }
            const size_t size = static_cast<size_t>(got);
            adapter->pump->Input(report, size);
            std::vector<GameCubeAdapter::PortChange> changes;
            std::vector<std::pair<int, std::pair<std::string, float>>> controls;
            if (!adapter->decoder.Decode(
                    report, size,
                    [&controls](int port, const std::string& control, float value) {
                        controls.push_back({port, {control, value}});
                    },
                    changes))
                continue;
            for (const GameCubeAdapter::PortChange& change : changes)
            {
                HostDeviceInfo info = adapter->info;
                info.id = AdapterPortId(info.id, change.port);
                info.name += " (port " + std::to_string(change.port + 1) + ")";
                {
                    std::lock_guard lock(_mutex);
                    adapter->shown[static_cast<size_t>(change.port)] = change.connected;
                }
                if (change.connected)
                    _sink->OnDeviceAdded(info);
                else
                    _sink->OnDeviceRemoved(info.id);
            }
            for (const auto& [port, control] : controls)
                _sink->OnControl({AdapterPortId(adapter->info.id, port), control.first, control.second, 0});
        }
    }
    /// endregion </GameCube adapters>

    /// The Steam pads hid-steam gave to the Steam client (FindSteamHeldPads): each a device with no controls whose
    /// notice says so, while the client runs; gone when the driver offers its input device again (a new event node
    /// makes a Scan) or Steam quits
    void RefreshSteamHeld()
    {
        std::vector<LinuxSteamHeld> held = FindSteamHeldPads(kSysDir);  // no hid-steam pads: no look at /proc
        if (!held.empty() && !SteamClientRunning(kProcDir))
            held.clear();  // a wireless receiver with no pad on it
        std::vector<HostDeviceId> removed;
        std::vector<HostDeviceInfo> added;
        {
            std::lock_guard lock(_mutex);
            for (auto it = _steamHeld.begin(); it != _steamHeld.end();)
            {
                const bool still = std::any_of(held.begin(), held.end(),
                                               [&it](const LinuxSteamHeld& pad) { return pad.id == it->first; });
                if (still)
                    ++it;
                else
                {
                    removed.push_back(it->first);
                    it = _steamHeld.erase(it);
                }
            }
            for (const LinuxSteamHeld& pad : held)
            {
                if (_steamHeld.count(pad.id) != 0)
                    continue;
                const DeviceMatch match =
                    DeviceDatabase::Active()->Lookup({"usb", 0x28DE, pad.product, -1, pad.name, Name()});
                HostDeviceInfo info{pad.id, match.name.empty() ? pad.name : match.name, match.family, match.layout,
                                    Name()};
                _steamHeld.emplace(pad.id, info);
                added.push_back(info);
            }
        }
        for (const HostDeviceId& id : removed)
            _sink->OnDeviceRemoved(id);
        for (const HostDeviceInfo& info : added)
            _sink->OnDeviceAdded(info);
    }

    /// joycond's combined Joy-Cons: the pair id of the two it took (FindJoyConHalves), so a profile of the pair the
    /// backend joins itself applies to it as well
    static std::string JoycondId()
    {
        return JoycondPairId(FindJoyConHalves(kSysDir, kInputDir, [](const std::string& node) {
            return access(node.c_str(), R_OK) == 0;
        }));
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
            return;  // not ours to read (no `input` group, joycond's Joy-Cons): the probe counts what opened
        input_id ident{};
        ioctl(fd, EVIOCGID, &ident);
        const std::vector<unsigned long> keys = Bits(fd, EV_KEY, KEY_MAX);
        const bool joystick = TestBit(keys, BTN_JOYSTICK) || TestBit(keys, BTN_TRIGGER);
        const bool pad = TestBit(keys, BTN_GAMEPAD) || (!joystick && PadKeys(keys));
        // The Surface Dial by its id, or a device of its kind: a dial and a button
        const std::vector<unsigned long> relative = Bits(fd, EV_REL, REL_MAX);
        // A control surface of the database by its id, whatever hid-input made of it (the ShuttlePRO: a dial and
        // buttons, so it would pass for a Dial): read through hidraw (LinuxPadFacts::surface)
        const auto surfaces = DeviceDatabase::Active()->SurfaceProducts();
        const bool surface = !SurfaceDial::Handles(ident.vendor, ident.product) &&
                             std::find(surfaces.begin(), surfaces.end(), std::pair{ident.vendor, ident.product}) !=
                                 surfaces.end();
        const bool dial = !surface && !pad && !joystick && TestBit(relative, REL_DIAL) &&
                          (SurfaceDial::Handles(ident.vendor, ident.product) || TestBit(keys, BTN_0));
        if (!pad && !joystick && !dial && !surface)
        {
            close(fd);
            return;
        }
        const std::string name = StringIoctl(fd, EVIOCGNAME(255));
        const bool bluetooth = ident.bustype == BUS_BLUETOOTH;
        const bool virtualBus = ident.bustype == BUS_VIRTUAL;
        const DeviceMatch match = DeviceDatabase::Active()->Lookup(
            {bluetooth ? "bt" : (virtualBus ? "virtual" : "usb"), ident.vendor, ident.product, ident.version, name,
             Name()});
        if (match.ignore)
        {
            close(fd);
            return;
        }

        auto device = std::make_unique<Device>();
        device->fd = fd;
        device->node = node;
        device->event = event;
        device->hidLayout = HidLayoutFor(match);
        device->pad = pad && !surface;
        device->dial = dial;
        device->surface = surface;
        device->bluetooth = bluetooth;
        // The HID device (none for xpad's USB interface and for uinput devices such as joycond's): its driver
        const std::string hidDir = HidDeviceDir(kSysDir, event);
        if (!hidDir.empty())
        {
            HidUevent uevent;
            ParseHidUevent(ReadFile(hidDir + "/uevent"), uevent);
            device->driver = LinuxHidDriverOf(uevent.driver);
        }
        const std::vector<unsigned long> axes = Bits(fd, EV_ABS, ABS_MAX);
        for (unsigned code = 0; code <= ABS_MAX; ++code)
            if (TestBit(axes, code))
            {
                input_absinfo info{};
                if (ioctl(fd, EVIOCGABS(code), &info) == 0)
                    device->abs[code] = info;
            }
        device->kind = LinuxPadKindFor(ident.vendor, ident.product, TestBit(axes, ABS_RX),
                                       device->driver == LinuxHidDriver::Nintendo, name);
        device->analogTriggers =
            (device->driver == LinuxHidDriver::PlayStation || device->driver == LinuxHidDriver::Sony) &&
            TestBit(axes, ABS_Z) && TestBit(axes, ABS_RZ);
        device->generic = !hidDir.empty() && LinuxHidUsageNamed(device->driver) && !dial && !surface;
        if (IsJoyConKind(device->kind))
            device->joyCon = IsLeftJoyConKind(device->kind) ? 1 : 2;
        device->rumble = writable && TestBit(Bits(fd, EV_FF, FF_MAX), FF_RUMBLE);
        // Without FF_RUMBLE an Xbox pad over Bluetooth gets report 3 through hidraw (AttachHidraw says why not)
        if (!device->rumble)
            device->rumbleUnavailable = writable ? "The device offers no rumble (FF_RUMBLE) to Linux"
                                                 : "No write access to " + node + ": a udev rule that gives the user "
                                                   "write access drives its motors";
        // D15: over USB the paddles are xpad's buttons on profile 0 only, and the profile is not readable
        if (!bluetooth && match.family == "xbox-elite-2")
            device->baseNotice = XpadPaddlesNotice();
        // architecture.md §5: usb:<vid>:<pid>:<version>:<serial>, usb:...@<path hash>, bt:<vid>:<pid>:<hash>
        const std::string uniq = StringIoctl(fd, EVIOCGUNIQ(255));
        const std::string phys = StringIoctl(fd, EVIOCGPHYS(255));
        std::string id = LinuxDeviceId(ident.bustype, ident.vendor, ident.product, ident.version, uniq, phys, node);
        std::string shownName = match.name.empty() ? name : match.name;
        if (device->kind == LinuxPadKind::JoycondCombined)
        {
            if (const std::string pair = JoycondId(); !pair.empty())
                id = pair;
            shownName = "Joy-Con (L/R)";
        }
        else if (device->joyCon != 0)
            shownName = device->joyCon == 1 ? "Joy-Con (L)" : "Joy-Con (R)";
        device->info = {id, shownName, match.family, match.layout, Name()};

        // The same HID device's hidraw node, when this device needs it: before the device is announced, so its
        // outputs and controls are right from the first moment
        device->vendor = ident.vendor;
        device->product = ident.product;
        device->hidName = name;
        LinuxPadFacts facts;
        facts.name = name;
        facts.bluetooth = bluetooth;
        facts.vendor = ident.vendor;
        facts.product = ident.product;
        facts.family = match.family;
        facts.layout = match.layout;
        facts.driver = hidDir.empty() ? LinuxHidDriver::Other : device->driver;  // no HID device: no hidraw node
        facts.evdevRumble = device->rumble;
        facts.edgeButtons = TestBit(keys, BTN_TRIGGER_HAPPY3);
        facts.hidExtras = !match.hid.bits.empty();  // consumer usages come as KEY_* (LinuxGenericButton)
        facts.surface = surface;
        device->use = hidDir.empty() && device->kind == LinuxPadKind::JoycondCombined ? HidrawUse::None
                                                                                      : HidrawUseFor(facts);
        if (device->use != HidrawUse::None)
            AttachHidraw(*device);
        // The HID device's battery and light (a USB xpad pad has no HID device: neither)
        if (!hidDir.empty())
        {
            device->powerSupply = FindPowerSupply(hidDir);
            device->battery = ReadPowerSupply(device->powerSupply);
            device->light = FindLight(hidDir);
        }

        PairEvents events;
        {
            std::lock_guard lock(_mutex);
            const std::string base = device->info.id;
            for (int n = 2; Find(device->info.id) != nullptr; ++n)
                device->info.id = base + "#" + std::to_string(n);
            device->reported = device->info.id;
            device->single = device->info;
            // A Protocol pad's player LEDs are the backend's; under SwitchRumble the kernel's stay
            if (device->pump && HasUse(device->use, HidrawUse::Protocol))
                AssignPlayerLocked(*device);
            // A Dial that reconnects gets the steps it had (architecture.md §9: the driver applies them again)
            if (device->writer)
                if (const auto steps = _dialSteps.find(device->info.id); steps != _dialSteps.end())
                    device->writer->Steps(steps->second);
            Device* raw = device.get();
            _devices[node] = std::move(device);
            AnnounceLocked(*raw, events);
        }
        ReportPairEvents(events);
    }

    /// The player LEDs of a Protocol pad: the lowest number no other one shows (the kernel numbers its own pads)
    void AssignPlayerLocked(Device& device)
    {
        int player = 1;
        while (std::any_of(_devices.begin(), _devices.end(),
                           [player](const auto& entry) { return entry.second->player == player; }))
            ++player;
        device.player = player;
        device.pump->SetPlayer(player);
    }

    /// region <Joy-Con pairs>
    /// What a change shows the sink, outside the lock: the devices gone first, then the new ones
    struct PairEvents
    {
        std::vector<HostDeviceId> removed;
        std::vector<HostDeviceInfo> added;
    };

    void ReportPairEvents(const PairEvents& events)
    {
        for (const HostDeviceId& id : events.removed)
            _sink->OnDeviceRemoved(id);
        for (const HostDeviceInfo& info : events.added)
            _sink->OnDeviceAdded(info);
    }

    /// A device now in _devices is shown: a Joy-Con alone, or straight into a pair with one of the other side that
    /// waits (its own device never shows), or any other device. A grip's half waits for its side (ResolveGrip)
    void AnnounceLocked(Device& device, PairEvents& out)
    {
        if (device.gripPending)
        {
            device.shown = false;
            return;
        }
        if (device.joyCon == 0)
        {
            device.shown = true;
            out.added.push_back(device.info);
            return;
        }
        device.shown = true;
        const JoyConPairing::Change change = _joyCons.Add(device.info.id, device.joyCon == 1);
        if (change.formed)
            device.shown = false;
        else
            out.added.push_back(device.single);
        ApplyJoyConChangeLocked(change, {}, out);
    }

    /// A device about to leave _devices (or to be announced again): what the sink knew of it goes, a pair it was in
    /// breaks (its other half alone again, or into a pair with another that waits)
    void WithdrawLocked(Device& device, PairEvents& out)
    {
        if (device.gripPending)
            return;
        if (device.shown)
            out.removed.push_back(device.info.id);
        device.shown = false;
        if (device.joyCon == 0)
            return;
        const JoyConPairing::Change change = _joyCons.Remove(device.info.id);
        ApplyJoyConChangeLocked(change, device.info.id, out);
        device.reported = device.info.id;
    }

    /// A half's layout: its evdev kind, or its decoder's (a Protocol Joy-Con)
    static void SetJoyConLayout(Device& device, bool paired)
    {
        const bool left = device.joyCon == 1;
        if (device.switchPro)
            device.switchPro->SetLayout(SwitchProPad::JoyConLayout(left, paired));
        else
            device.kind = JoyConKind(left, paired);
    }

    /// A pair broke (@p gone left it) and / or one formed: the halves' layouts, ids, player LEDs, and what the sink
    /// sees
    void ApplyJoyConChangeLocked(const JoyConPairing::Change& change, const HostDeviceId& gone, PairEvents& out)
    {
        if (change.broken)
        {
            out.removed.push_back(change.broken->id);
            const HostDeviceId& alone = change.broken->left == gone ? change.broken->right : change.broken->left;
            if (Device* device = Find(alone); device != nullptr && alone != gone)
            {
                device->reported = device->info.id;
                SetJoyConLayout(*device, false);
                if (device->pump)
                {
                    device->pump->SetPlayer(device->player);
                    device->pump->SetRumble({});
                }
                else if (device->rumble)
                    FfRumbleLocked(*device, {});
                device->shown = true;
                out.added.push_back(device->single);
            }
        }
        if (!change.formed)
            return;
        const JoyConPairing::Pair& pair = *change.formed;
        Device* left = Find(pair.left);
        Device* right = Find(pair.right);
        if (left == nullptr || right == nullptr)
            return;
        for (Device* half : {left, right})
        {
            if (half->shown)
            {
                // Shown alone until now: gone, or never shown when it came back alone in this same change
                const auto again = std::find_if(out.added.begin(), out.added.end(),
                                                [half](const HostDeviceInfo& info) { return info.id == half->info.id; });
                if (again != out.added.end())
                    out.added.erase(again);
                else
                    out.removed.push_back(half->info.id);
            }
            half->shown = false;
            half->reported = pair.id;
            SetJoyConLayout(*half, true);
            if (half->pump)
            {
                half->pump->SetPlayer(left->player);  // both show the pair's number (SDL sets each half's lights)
                half->pump->SetRumble({});
            }
            else if (half->rumble)
                FfRumbleLocked(*half, {});
        }
        HostDeviceInfo info = left->single;
        info.id = pair.id;
        info.name = "Joy-Con (L/R)";
        out.added.push_back(info);
    }

    /// A charging grip's half under hid-generic once its device info came: a Joy-Con of that side, paired as any.
    /// Another type (no answer) keeps the half hidden, its input dropped
    void ResolveGrip(Device& device)
    {
        const int type = device.gripType->load();
        const bool left = type == SwitchProtocol::kTypeJoyConLeft;
        if (!left && type != SwitchProtocol::kTypeJoyConRight)
            return;
        PairEvents events;
        {
            std::lock_guard lock(_mutex);
            device.gripPending = false;
            device.joyCon = left ? 1 : 2;
            device.switchPro->SetLayout(SwitchProPad::JoyConLayout(left, false));
            device.single.name = left ? "Joy-Con (L)" : "Joy-Con (R)";
            device.info.name = device.single.name;
            AnnounceLocked(device, events);
        }
        ReportPairEvents(events);
    }
    /// endregion </Joy-Con pairs>

    /// Open the hidraw node of @p device's HID device for each of its uses not open yet; on failure the notice
    /// (Input, Triggers, SwitchRumble, Protocol) or the Test haptics reason (Output, Rumble) says why and what to
    /// install. On the backend's thread (or in Start), before or after the device is announced; the hidraw input side
    /// (hidrawFd, the decoders, merge) is only touched there
    void AttachHidraw(Device& device)
    {
        static constexpr HidrawUse kUses[] = {HidrawUse::Output,       HidrawUse::Rumble,   HidrawUse::Triggers,
                                              HidrawUse::SwitchRumble, HidrawUse::Protocol, HidrawUse::Input};
        HidrawNode found;
        if (!FindHidrawNode(kSysDir, kDevDir, device.event, device.vendor, device.product, found))
        {
            for (const HidrawUse use : kUses)
                if (HasUse(device.use, use))
                    Unavailable(device, use, HidrawUnavailable(use, {}, 0, {}, device.info.family, device.surface));
            return;
        }
        device.hidrawNode = found.node;
        if (HasUse(device.use, HidrawUse::Output) && !device.writer)
            AttachDialWriter(device, found.node);
        if (HasUse(device.use, HidrawUse::Rumble) && !device.rumbleWriter)
            AttachRumble(device, found.node);
        if (HasUse(device.use, HidrawUse::Triggers) && !device.triggerWriter)
            AttachTriggers(device, found.node);
        if (HasUse(device.use, HidrawUse::SwitchRumble) && !device.pump)
            AttachSwitchRumble(device, found.node);
        if (HasUse(device.use, HidrawUse::Protocol) && !device.pump)
            AttachProtocol(device, found.node);
        if (HasUse(device.use, HidrawUse::Input) && device.hidrawFd < 0)
            AttachInput(device, found.node);
    }

    /// A descriptor of @p node for writing; -1 with errno set
    static int OpenForWriting(const std::string& node, bool readToo)
    {
        int fd = readToo ? open(node.c_str(), O_RDWR | O_CLOEXEC) : -1;
        if (fd < 0 && (!readToo || errno == EACCES || errno == EPERM))
            fd = open(node.c_str(), O_WRONLY | O_CLOEXEC);
        return fd;
    }

    /// The open() failure of @p use on @p node, as the notice or the rumble reason
    void OpenFailed(Device& device, HidrawUse use, const std::string& node)
    {
        const int error = errno;
        Unavailable(device, use,
                    HidrawUnavailable(use, node, error, std::strerror(error), device.info.family, device.surface));
    }

    /// HidrawUse::Output: the Dial's writer (it reads feature report 1 back, so read-write where it may)
    void AttachDialWriter(Device& device, const std::string& node)
    {
        const int fd = OpenForWriting(node, true);
        if (fd < 0)
        {
            OpenFailed(device, HidrawUse::Output, node);
            return;
        }
        std::lock_guard lock(_mutex);  // the writer is what Outputs and PlayClicks look for
        device.hidrawOut = std::make_shared<HidrawHandle>(fd);
        device.writer = MakeDialWriter(device.hidrawOut);
        device.rumbleUnavailable.clear();  // no motors, but its clicks play (PlayClicks)
    }

    /// HidrawUse::Rumble: report 3 (XboxBluetoothOutput::RumbleReport) through its own write-only descriptor, so
    /// nothing queues input reports on it; the Elite's input reads a descriptor of its own (AttachInput)
    void AttachRumble(Device& device, const std::string& node)
    {
        const int fd = OpenForWriting(node, false);
        if (fd < 0)
        {
            OpenFailed(device, HidrawUse::Rumble, node);
            return;
        }
        auto handle = std::make_shared<HidrawHandle>(fd);
        auto writer = std::make_unique<RumbleWriter>([handle](const RumbleLevels& levels) {
            // The first byte is the report id, as hidraw takes it for numbered reports
            const std::vector<uint8_t> report = XboxBluetoothOutput::RumbleReport(levels);
            return write(handle->fd, report.data(), report.size()) == static_cast<ssize_t>(report.size());
        });
        std::lock_guard lock(_mutex);  // the writer is what HasRumble and SetRumble look for
        device.rumbleWriter = std::move(writer);
        device.rumbleUnavailable.clear();
    }

    /// HidrawUse::Triggers: a DualSense's trigger effects (report 2 / 0x31 with the trigger flags alone) on their own
    /// writer thread, beside hid-playstation's own output reports (motors, light bar, player LEDs)
    void AttachTriggers(Device& device, const std::string& node)
    {
        const int fd = OpenForWriting(node, false);
        if (fd < 0)
        {
            OpenFailed(device, HidrawUse::Triggers, node);
            return;
        }
        auto handle = std::make_shared<HidrawHandle>(fd);
        const bool bluetooth = device.bluetooth;
        auto writer = std::make_unique<RumbleWriter>(
            [handle, bluetooth, sequence = uint8_t{0}](const RumbleLevels& levels) mutable {
                const std::vector<uint8_t> report = DualSenseOutput::TriggerReport(levels, bluetooth, sequence++);
                return write(handle->fd, report.data(), report.size()) == static_cast<ssize_t>(report.size());
            });
        std::lock_guard lock(_mutex);
        device.triggerWriter = std::move(writer);
        device.missing.erase(static_cast<uint8_t>(HidrawUse::Triggers));
        device.rumbleUnavailable.clear();
    }

    /// HidrawUse::SwitchRumble: the 0x10 rumble report at the effect's frequencies (SwitchProtocol::RumbleOnly; the
    /// kernel driver did the set-up) in place of hid-nintendo's FF_RUMBLE, which then stays unused
    void AttachSwitchRumble(Device& device, const std::string& node)
    {
        const int fd = OpenForWriting(node, false);
        if (fd < 0)
        {
            OpenFailed(device, HidrawUse::SwitchRumble, node);
            return;
        }
        auto pump = MakePump(std::make_shared<HidrawHandle>(fd), SwitchProtocol::RumbleOnly(device.bluetooth));
        std::lock_guard lock(_mutex);
        device.pump = std::move(pump);
        device.missing.erase(static_cast<uint8_t>(HidrawUse::SwitchRumble));
        device.rumbleUnavailable.clear();
    }

    /// HidrawUse::Protocol: a PlayStation or Switch pad hid-generic serves, as macOS reads it: its full reports
    /// (SonyPad, SwitchProPad) from a read descriptor polled on the backend's thread, its protocol (set-up, rumble,
    /// light, player LEDs) on a pump of its own through a read-write one
    void AttachProtocol(Device& device, const std::string& node)
    {
        const int out = OpenForWriting(node, true);
        if (out < 0)
        {
            OpenFailed(device, HidrawUse::Protocol, node);
            return;
        }
        const int in = open(node.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (in < 0)
        {
            OpenFailed(device, HidrawUse::Protocol, node);
            close(out);
            return;
        }
        auto handle = std::make_shared<HidrawHandle>(out);
        const std::optional<SonyModel> sony = SonyPad::ModelOf(device.vendor, device.product);
        std::unique_ptr<PadPump> pump;
        if (sony)
        {
            device.sony = std::make_unique<SonyPad>(*sony);
            pump = MakePump(handle, std::make_unique<SonyProtocol>(*sony, device.bluetooth));
        }
        else
        {
            const std::optional<SwitchLayout> layout =
                SwitchProPad::LayoutFor(device.vendor, device.product, device.hidName);
            device.switchPro = std::make_unique<SwitchProPad>(layout.value_or(SwitchLayout::Pro));
            SwitchProtocol::TypeSink onType;
            if (!layout)
            {
                // A half of the charging grip: its side from the device info, then a Joy-Con of that side
                device.gripPending = true;
                std::shared_ptr<std::atomic<int>> type = device.gripType;
                onType = [type](uint8_t value) { type->store(value); };
            }
            else if (SwitchProPad::IsJoyCon(*layout))
                device.joyCon = *layout == SwitchLayout::JoyConLeft ? 1 : 2;
            // The calibration comes with a subcommand reply, on the backend's thread (PadPump::Input): the decoder is
            // its. No start delay: hid-generic talks nothing to the pad
            SwitchProPad* decoder = device.switchPro.get();
            pump = MakePump(handle, std::make_unique<SwitchProtocol>(
                                        device.bluetooth,
                                        [decoder](const SwitchStickCalibration& calibration) {
                                            decoder->SetCalibration(calibration);
                                        },
                                        0, std::move(onType)));
        }
        device.hidrawFd = in;  // polled from the next round on
        {
            std::lock_guard lock(_mutex);
            device.pump = std::move(pump);
            device.pumpLight = sony.has_value();
            device.missing.erase(static_cast<uint8_t>(HidrawUse::Protocol));
            device.rumbleUnavailable.clear();
        }
        device.merge.Switch(HidrawSourceMerge::Source::Hidraw, Emitter(device));
    }

    /// HidrawUse::Input: the Elite's reports (HidPad: paddles and profile included), the Edge's full report
    /// (SonyPad: paddles and Fn buttons) where hid-playstation predates them, read on the backend's thread
    void AttachInput(Device& device, const std::string& node)
    {
        const int fd = open(node.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
        {
            OpenFailed(device, HidrawUse::Input, node);
            return;
        }
        if (const std::optional<SonyModel> sony = SonyPad::ModelOf(device.vendor, device.product))
            device.sony = std::make_unique<SonyPad>(*sony);
        else
        {
            int size = 0;
            bool described =
                ioctl(fd, HIDIOCGRDESCSIZE, &size) >= 0 && size > 0 && size <= HID_MAX_DESCRIPTOR_SIZE;
            hidraw_report_descriptor descriptor{};
            descriptor.size = described ? static_cast<uint32_t>(size) : 0;
            described = described && ioctl(fd, HIDIOCGRDESC, &descriptor) >= 0;
            HidDescriptor parsed;
            std::string error;
            if (!described || !ParseHidDescriptor(descriptor.value, descriptor.size, parsed, error))
            {
                close(fd);
                Unavailable(device, HidrawUse::Input, "The report descriptor of " + node + " could not be read" +
                                                          (error.empty() ? std::string() : " (" + error + ")") +
                                                          ": the paddles and the onboard profile are not seen");
                return;
            }
            auto hid = std::make_unique<HidPad>(parsed, device.hidLayout);
            if (hid->Empty())
            {
                close(fd);
                Unavailable(device, HidrawUse::Input,
                            node + " describes no pad controls: the paddles and the onboard profile are not seen");
                return;
            }
            device.hid = std::move(hid);
        }
        device.hidrawFd = fd;  // polled from the next round on
        {
            std::lock_guard lock(_mutex);
            device.missing.erase(static_cast<uint8_t>(HidrawUse::Input));
        }
        // From now on every control comes from the hidraw reports; what evdev held is released (not under _mutex:
        // the sink may ask the backend)
        device.merge.Switch(HidrawSourceMerge::Source::Hidraw, Emitter(device));
    }

    void Unavailable(Device& device, HidrawUse use, const std::string& why)
    {
        std::lock_guard lock(_mutex);
        if (use == HidrawUse::Output || use == HidrawUse::Rumble)
            device.rumbleUnavailable = why;
        else
            device.missing[static_cast<uint8_t>(use)] = why;
    }

    /// Under _mutex: the uses of @p device whose hidraw side is not open
    static HidrawUse MissingLocked(const Device& device)
    {
        HidrawUse missing = HidrawUse::None;
        if (HasUse(device.use, HidrawUse::Input) && device.hidrawFd < 0)
            missing = missing | HidrawUse::Input;
        if (HasUse(device.use, HidrawUse::Output) && !device.writer)
            missing = missing | HidrawUse::Output;
        if (HasUse(device.use, HidrawUse::Rumble) && !device.rumbleWriter)
            missing = missing | HidrawUse::Rumble;
        if (HasUse(device.use, HidrawUse::Triggers) && !device.triggerWriter)
            missing = missing | HidrawUse::Triggers;
        if (HasUse(device.use, HidrawUse::SwitchRumble) && !device.pump)
            missing = missing | HidrawUse::SwitchRumble;
        if (HasUse(device.use, HidrawUse::Protocol) && !device.pump)
            missing = missing | HidrawUse::Protocol;
        return missing;
    }

    /// The devices whose hidraw node did not open: udev may have given access since (a hidraw node's ACL arrives
    /// after its evdev node's). One that opens a use now is announced again, so the router sees its outputs and the
    /// dialog its controls
    void RetryHidraw()
    {
        std::vector<std::pair<Device*, HidrawUse>> waiting;
        {
            std::lock_guard lock(_mutex);
            for (auto& [node, device] : _devices)
                if (const HidrawUse missing = MissingLocked(*device); missing != HidrawUse::None)
                    waiting.emplace_back(device.get(), missing);  // only this thread erases
        }
        for (const auto& [device, before] : waiting)
        {
            PairEvents events;
            {
                std::lock_guard lock(_mutex);
                WithdrawLocked(*device, events);
            }
            AttachHidraw(*device);
            {
                std::lock_guard lock(_mutex);
                if (MissingLocked(*device) == before)
                {
                    // Nothing changed: shown again as it was, nothing reported
                    PairEvents unchanged;
                    AnnounceLocked(*device, unchanged);
                    continue;
                }
                if (device->pump && HasUse(device->use, HidrawUse::Protocol) && device->player == 0)
                    AssignPlayerLocked(*device);
                if (device->writer)
                    if (const auto steps = _dialSteps.find(device->info.id); steps != _dialSteps.end())
                        device->writer->Steps(steps->second);
                AnnounceLocked(*device, events);
            }
            ReportPairEvents(events);
        }
    }

    void Close(const std::string& node)
    {
        std::unique_ptr<Device> device;
        PairEvents events;
        {
            std::lock_guard lock(_mutex);
            auto it = _devices.find(node);
            if (it == _devices.end())
                return;
            WithdrawLocked(*it->second, events);
            device = std::move(it->second);
            _devices.erase(it);
        }
        CloseFiles(*device);
        ReportPairEvents(events);
        // The writers and the pump end with the device: a write in flight finishes first (a gone device refuses it at
        // once)
    }

    /// hidraw stopped (the node went away before its evdev node, or failed): evdev is the source again
    void DetachHidraw(Device& device)
    {
        if (device.hidrawFd >= 0)
            close(device.hidrawFd);
        device.hidrawFd = -1;
        const bool protocol = HasUse(device.use, HidrawUse::Protocol);
        device.hid.reset();
        {
            std::lock_guard lock(_mutex);
            const std::string what = protocol ? "the pad's own reports, rumble and light are"
                                     : device.sony ? "the paddles and the Fn buttons are"
                                                   : "the paddles and the onboard profile are";
            device.missing[static_cast<uint8_t>(HidrawUse::Input)] =
                "The hidraw node " + device.hidrawNode + " went away: " + what +
                " not seen until the controller reconnects";
            device.pump.reset();
            device.pumpLight = false;
        }
        device.sony.reset();
        device.switchPro.reset();
        device.merge.Switch(HidrawSourceMerge::Source::Evdev, Emitter(device));
    }

    /// An evdev node this user could read went to root alone (joycond takes the Joy-Cons it joins: EVIOCGRAB and
    /// mode 0600): the device is gone for us, joycond's combined device comes instead
    void Revalidate(const std::vector<std::string>& events)
    {
        for (const std::string& event : events)
        {
            const std::string node = std::string(kInputDir) + "/" + event;
            if (Lookup(node) != nullptr && access(node.c_str(), R_OK) != 0)
                Close(node);
        }
    }

    void Run()
    {
        struct Polled
        {
            std::string node;
            bool hidraw = false;
            bool adapter = false;  ///< a HidrawAdapter's node
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
                for (const auto& [node, adapter] : _adapters)
                    if (adapter->handle)
                    {
                        fds.push_back({adapter->handle->fd, POLLIN, 0});
                        polled.push_back({node, true, true});
                    }
            }
            // A Surface Dial's inferred touch ends on time between its events (DialTouch): wake 10 times a second.
            // The batteries are read again every 30 s
            const int timeout = dials ? 100 : static_cast<int>(kBatteryRefreshMs);
            if (poll(fds.data(), fds.size(), timeout) < 0 && errno != EINTR)
                return;
            ExpireDials();
            RefreshBatteries();
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
                    std::vector<std::string> changed;
                    alignas(inotify_event) char buffer[4096];
                    ssize_t got = 0;
                    while ((got = read(_inotify, buffer, sizeof(buffer))) > 0)
                        for (ssize_t at = 0; at + static_cast<ssize_t>(sizeof(inotify_event)) <= got;)
                        {
                            const auto* change = reinterpret_cast<const inotify_event*>(buffer + at);
                            if (change->wd == _devWatch)
                                hidraw = hidraw || (change->len > 0 && std::strncmp(change->name, "hidraw", 6) == 0);
                            else
                            {
                                scan = true;
                                if ((change->mask & IN_ATTRIB) && change->len > 0 &&
                                    std::strncmp(change->name, "event", 5) == 0)
                                    changed.emplace_back(change->name);
                            }
                            at += static_cast<ssize_t>(sizeof(inotify_event) + change->len);
                        }
                    if (!changed.empty())
                        Revalidate(changed);
                    if (scan)
                        Scan();  // a new node: udev sets its permissions a moment later (IN_ATTRIB)
                    if (hidraw)
                    {
                        RetryHidraw();
                        ScanAdapters();
                    }
                }
            }
            for (size_t i = first; i < fds.size(); ++i)
            {
                const Polled& entry = polled[i - first];
                if (entry.adapter)
                {
                    if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                        CloseAdapter(entry.node);
                    else if (fds[i].revents & POLLIN)
                        ReadAdapter(entry.node);
                    continue;
                }
                if (entry.hidraw)
                {
                    if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))
                    {
                        if (Device* device = Lookup(entry.node); device != nullptr && device->hidrawFd >= 0)
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

    /// Every kBatteryRefreshMs: each power supply read outside the lock (a read may wait for the device), then kept
    void RefreshBatteries()
    {
        const uint64_t now = DialNowMs();
        if (now - _batteryReadMs < kBatteryRefreshMs)
            return;
        _batteryReadMs = now;
        std::vector<std::pair<Device*, std::string>> supplies;
        {
            std::lock_guard lock(_mutex);
            for (auto& [node, device] : _devices)
                if (!device->powerSupply.empty())
                    supplies.emplace_back(device.get(), device->powerSupply);  // only this thread erases
        }
        for (const auto& [device, dir] : supplies)
        {
            const BatteryState battery = ReadPowerSupply(dir);
            std::lock_guard lock(_mutex);
            device->battery = battery;
        }
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

    /// One read is one input report, its id first (numbered reports), as IOHID gives it on macOS. The pump sees it
    /// first (subcommand replies, acknowledgements)
    void ReadHidraw(const std::string& node)
    {
        Device* device = Lookup(node);
        if (device == nullptr || device->hidrawFd < 0)
            return;
        uint8_t report[512];
        const ControlSink sink = Emitter(*device);
        const ControlSink merged = [device, &sink](const std::string& control, float value) {
            device->merge.Control(HidrawSourceMerge::Source::Hidraw, control, value, sink);
        };
        for (;;)
        {
            const ssize_t got = read(device->hidrawFd, report, sizeof(report));
            if (got <= 0)
            {
                if (got < 0 && errno != EAGAIN && errno != EINTR)
                    DetachHidraw(*device);
                return;
            }
            const size_t size = static_cast<size_t>(got);
            {
                std::lock_guard lock(_mutex);  // the pump is replaced under it (DetachHidraw)
                if (device->pump)
                    device->pump->Input(report, size);
            }
            if (device->gripPending)
            {
                ResolveGrip(*device);
                if (device->gripPending)
                    continue;  // which half it is decides what its buttons are
            }
            if (device->sony || device->switchPro)
            {
                const bool full = device->sony ? device->sony->Decode(report, size, merged)
                                               : device->switchPro->Decode(report, size, merged);
                if (full)
                {
                    const BatteryState battery = device->sony ? device->sony->Battery() : device->switchPro->Battery();
                    std::lock_guard lock(_mutex);
                    device->decoded = battery;
                }
                continue;
            }
            if (device->hid)
                device->hid->Decode(report, size, merged);
        }
    }

    /// What the merge passes on goes to the sink as the device's control, or its pair's
    /// (two pointers: no allocation; the device outlives every call, only this thread erases it)
    ControlSink Emitter(const Device& device)
    {
        return [this, known = &device](const std::string& control, float value) {
            if (known->shown || known->reported != known->info.id)
                _sink->OnControl({known->reported, control, value, 0});
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
        if (device.surface)
            return;  // its controls are the hidraw reports' (LinuxPadFacts::surface)
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
            if (device.generic)
            {
                if (const std::string control = LinuxGenericButton(device.hidLayout, event.code); !control.empty())
                    Emit(device, control, value);
                return;
            }
            if (device.pad)
            {
                if (const char* control = LinuxPadButton(device.kind, event.code, device.analogTriggers))
                {
                    Emit(device, control, value);
                    return;
                }
                if (device.analogTriggers && (event.code == BTN_TL2 || event.code == BTN_TR2))
                    return;  // the analog trigger's own
            }
            if (event.code >= BTN_JOYSTICK && event.code < BTN_JOYSTICK + 32)
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
        const LinuxPadAxis axis = device.generic ? LinuxGenericAxis(device.hidLayout, event.code)
                                  : device.pad   ? LinuxPadAxisOf(device.kind, event.code)
                                                 : LinuxPadAxis{};
        if (axis.dpad)
        {
            // The cross on X / Y (HidLayout::dpadAxes): past half way is pressed, as HidPad's AxisDpad
            const bool x = event.code == ABS_X;
            int& was = x ? device.hatX : device.hatY;
            const float v = unit * 2.0f - 1.0f;
            const int now = v <= -0.5f ? -1 : (v >= 0.5f ? 1 : 0);
            if (now == was)
                return;
            if (was != 0)
                Emit(device, x ? (was < 0 ? "dpad.left" : "dpad.right") : (was < 0 ? "dpad.up" : "dpad.down"), 0.0f);
            if (now != 0)
                Emit(device, x ? (now < 0 ? "dpad.left" : "dpad.right") : (now < 0 ? "dpad.up" : "dpad.down"), 1.0f);
            was = now;
            return;
        }
        if (axis.control == nullptr)
            Emit(device, "axis." + std::to_string(event.code), unit * 2.0f - 1.0f);
        else if (axis.unit)
            Emit(device, axis.control, unit);
        else
        {
            // evdev y is + down, as the canonical one
            const float value = unit * 2.0f - 1.0f;
            Emit(device, axis.control, axis.negate ? -value : value);
        }
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
    JoyConPairing _joyCons;                        ///< under _mutex: the Joy-Cons alone and in pairs
    std::map<HostDeviceId, HostDeviceInfo> _steamHeld;  ///< under _mutex: the pads the Steam client has (no node)
    std::map<std::string, std::unique_ptr<HidrawAdapter>> _adapters;  ///< under _mutex: by hidraw node
    static constexpr uint64_t kBatteryRefreshMs = 30000;
    uint64_t _batteryReadMs = 0;  ///< the backend's thread only: when RefreshBatteries last read them
};
}  // namespace

std::unique_ptr<IBackend> CreateEvdevBackend()
{
    return std::make_unique<EvdevBackend>();
}

}  // namespace controllers
