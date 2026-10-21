#include "platform/macos/iohidbackend.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDManager.h>

#include <mach/mach_time.h>
#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "devicedatabase.h"
#include "hid/drivers/dualsenseoutput.h"
#include "hid/drivers/surfacedial.h"
#include "hid/drivers/xboxgip.h"
#include "hid/hiddescriptor.h"
#include "hid/hidpad.h"

namespace controllers
{

namespace
{
int IntProperty(IOHIDDeviceRef device, CFStringRef key)
{
    int value = 0;
    CFTypeRef ref = IOHIDDeviceGetProperty(device, key);
    if (ref != nullptr && CFGetTypeID(ref) == CFNumberGetTypeID())
        CFNumberGetValue(static_cast<CFNumberRef>(ref), kCFNumberIntType, &value);
    return value;
}

std::string StringProperty(IOHIDDeviceRef device, CFStringRef key)
{
    CFTypeRef ref = IOHIDDeviceGetProperty(device, key);
    if (ref == nullptr || CFGetTypeID(ref) != CFStringGetTypeID())
        return {};
    char buffer[256] = {};
    if (!CFStringGetCString(static_cast<CFStringRef>(ref), buffer, sizeof(buffer), kCFStringEncodingUTF8))
        return {};
    return buffer;
}

std::string Hex4(int value)
{
    char text[8];
    snprintf(text, sizeof(text), "%04x", value & 0xFFFF);
    return text;
}

/// FNV-1a, for ids that must not carry a MAC address or a port path as such
std::string Hash(const std::string& text)
{
    uint32_t hash = 2166136261u;
    for (unsigned char c : text)
        hash = (hash ^ c) * 16777619u;
    char out[12];
    snprintf(out, sizeof(out), "%08x", hash);
    return out;
}

/// An IOHID report's time stamp (mach absolute time) in the steady clock's nanoseconds, the router's clock (libc++
/// counts it from the same CLOCK_UPTIME_RAW); 0 (the router takes its own time) when it is not plausibly now
uint64_t ReportTimeNs(uint64_t stamp)
{
    static const mach_timebase_info_data_t timebase = [] {
        mach_timebase_info_data_t info{};
        mach_timebase_info(&info);
        return info;
    }();
    if (stamp == 0 || timebase.denom == 0)
        return 0;
    const uint64_t ns = static_cast<uint64_t>(static_cast<__uint128_t>(stamp) * timebase.numer / timebase.denom);
    const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                               std::chrono::steady_clock::now().time_since_epoch())
                                               .count());
    constexpr uint64_t kPlausibleNs = 1'000'000'000;
    return ns <= now && now - ns < kPlausibleNs ? ns : 0;
}

/// An IOHIDDeviceRef kept alive for a writer thread that may outlive the backend's entry
struct RetainedDevice
{
    explicit RetainedDevice(IOHIDDeviceRef device) : ref(device)
    {
        CFRetain(ref);
    }
    ~RetainedDevice()
    {
        CFRelease(ref);
    }
    RetainedDevice(const RetainedDevice&) = delete;
    RetainedDevice& operator=(const RetainedDevice&) = delete;
    IOHIDDeviceRef ref;
};

std::unique_ptr<SurfaceDialWriter> MakeDialWriter(IOHIDDeviceRef ref)
{
    auto device = std::make_shared<RetainedDevice>(ref);
    return std::make_unique<SurfaceDialWriter>(
        [device](SurfaceDialWriter::ReportType type, const std::vector<uint8_t>& report) {
            const IOHIDReportType kind =
                type == SurfaceDialWriter::ReportType::Feature ? kIOHIDReportTypeFeature : kIOHIDReportTypeOutput;
            return IOHIDDeviceSetReport(device->ref, kind, report[0], report.data(),
                                        static_cast<CFIndex>(report.size())) == kIOReturnSuccess;
        });
}

CFDictionaryRef Matching(int usage)
{
    int page = 0x01;
    CFNumberRef pageRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &page);
    CFNumberRef usageRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage);
    const void* keys[] = {CFSTR(kIOHIDDeviceUsagePageKey), CFSTR(kIOHIDDeviceUsageKey)};
    const void* values[] = {pageRef, usageRef};
    CFDictionaryRef dict = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);
    CFRelease(pageRef);
    CFRelease(usageRef);
    return dict;
}

class IoHidBackend : public IBackend
{
public:
    ~IoHidBackend() override
    {
        Stop();
    }

    std::string Name() const override
    {
        return "iohid";
    }

    bool Start(IBackendSink& sink, std::string& reason) override
    {
        if (_thread.joinable())
            return true;
        _sink = &sink;
        std::promise<std::string> started;
        std::future<std::string> result = started.get_future();
        _thread = std::thread([this, &started] { Run(started); });
        reason = result.get();
        if (!reason.empty())
        {
            _thread.join();
            _sink = nullptr;
            return false;
        }
        return true;
    }

    void Stop() override
    {
        if (!_thread.joinable())
            return;
        _stopping = true;
        if (CFRunLoopRef loop = _loop.load())
            CFRunLoopStop(loop);  // thread-safe; a stop before the loop runs is caught by the flag
        _thread.join();
        _stopping = false;
        _sink = nullptr;
    }

    bool HasRumble(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        for (const auto& [ref, known] : _devices)
            if (known->id == device)
                return known->xboxBluetooth || known->dualSenseUsb;
        return false;
    }

    std::string RumbleUnavailable(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        for (const auto& [ref, known] : _devices)
            if (known->id == device)
                return known->rumbleUnavailable;
        return "The device is not connected";
    }

    uint8_t RumbleActuators(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        for (const auto& [ref, known] : _devices)
        {
            if (known->id == device && (known->xboxBluetooth || known->dualSenseUsb))
                return kRumbleLow | kRumbleHigh | kRumbleLeftTrigger | kRumbleRightTrigger;
            if (known->id == device && known->dial)
                return kHapticClicker;
        }
        return 0;
    }

    /// The Surface Dial's clicks: output report 1 of its Simple Haptic Controller (SurfaceDial::HapticReport), queued
    /// for its writer thread
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

    /// region <Surface outputs: the Dial's patterns and steps, through its writer thread>
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

    /// Kept by id: the Dial gets it again when it reconnects (it forgets its settings when it sleeps)
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

    /// The Xbox Bluetooth rumble report (as SDL's HIDAPI Xbox One driver sends it): 03, enable 0F, left trigger,
    /// right trigger, low motor, high motor (0..100), duration FF, delay 00, repeat EB
    bool SetRumble(const HostDeviceId& device, const RumbleLevels& levels) override
    {
        std::lock_guard lock(_mutex);
        for (const auto& [ref, known] : _devices)
        {
            if (known->id != device)
                continue;
            if (known->dualSenseUsb)
            {
                // The triggers' levels become their resistance (haptics.md §6.4); zero turns it off
                const std::vector<uint8_t> report = DualSenseOutput::UsbReport(levels);
                return IOHIDDeviceSetReport(known->ref, kIOHIDReportTypeOutput, report[0], report.data(),
                                            static_cast<CFIndex>(report.size())) == kIOReturnSuccess;
            }
            if (!known->xboxBluetooth)
                return false;
            auto percent = [](float level) {
                return static_cast<uint8_t>((level < 0 ? 0 : level > 1 ? 1 : level) * 100.0f + 0.5f);
            };
            const uint8_t report[] = {0x03,
                                      0x0F,
                                      percent(levels.leftTrigger),
                                      percent(levels.rightTrigger),
                                      percent(levels.low),
                                      percent(levels.high),
                                      0xFF,
                                      0x00,
                                      0xEB};
            return IOHIDDeviceSetReport(known->ref, kIOHIDReportTypeOutput, report[0], report, sizeof(report)) ==
                   kIOReturnSuccess;
        }
        return false;
    }

    size_t DeviceCount() const override
    {
        std::lock_guard lock(_mutex);
        return _devices.size();
    }

private:
    struct Device
    {
        HostDeviceId id;
        std::vector<uint8_t> buffer;
        std::unique_ptr<HidPad> hid;
        std::unique_ptr<XboxGipPad> gip;
        std::unique_ptr<SurfaceDial> dial;
        std::unique_ptr<SurfaceDialWriter> writer;  ///< a Dial's feature and output reports
        IoHidBackend* owner = nullptr;
        IOHIDDeviceRef ref = nullptr;
        bool xboxBluetooth = false;  ///< rumble through output report 3 (an Xbox One / Series pad over Bluetooth)
        bool dualSenseUsb = false;   ///< rumble and trigger resistance through output report 2
        std::string rumbleUnavailable;  ///< why neither: the Test haptics button's tooltip
    };

    void Run(std::promise<std::string>& started)
    {
        pthread_setname_np("controllers-io");
        IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
        if (manager == nullptr)
        {
            started.set_value("IOHIDManagerCreate failed");
            return;
        }
        // Joystick, game pad, multi-axis controller; System Multi-Axis Controller for the Surface Dial and devices of
        // its pattern. Opened without seizing (kIOHIDOptionsTypeNone): the system keeps its own use of them
        CFDictionaryRef matches[] = {Matching(0x04), Matching(0x05), Matching(0x08), Matching(0x0E)};
        CFArrayRef matching = CFArrayCreate(kCFAllocatorDefault, reinterpret_cast<const void**>(matches),
                                            static_cast<CFIndex>(std::size(matches)), &kCFTypeArrayCallBacks);
        for (CFDictionaryRef match : matches)
            CFRelease(match);
        IOHIDManagerSetDeviceMatchingMultiple(manager, matching);
        CFRelease(matching);
        IOHIDManagerRegisterDeviceMatchingCallback(manager, &IoHidBackend::Added, this);
        IOHIDManagerRegisterDeviceRemovalCallback(manager, &IoHidBackend::Removed, this);
        IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        const IOReturn opened = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
        if (opened != kIOReturnSuccess)
        {
            IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
            CFRelease(manager);
            char text[64];
            snprintf(text, sizeof(text), "IOHIDManagerOpen: 0x%08x", opened);
            started.set_value(text);
            return;
        }
        _loop = CFRunLoopGetCurrent();
        started.set_value({});

        // 0.1 s: a Surface Dial's inferred touch ends on time between its reports (DialTouch)
        while (!_stopping)
        {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
            ExpireDials();
        }

        _loop = nullptr;
        IOHIDManagerRegisterDeviceMatchingCallback(manager, nullptr, nullptr);
        IOHIDManagerRegisterDeviceRemovalCallback(manager, nullptr, nullptr);
        IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
        CFRelease(manager);
        std::vector<HostDeviceId> gone;
        {
            std::lock_guard lock(_mutex);
            for (auto& [ref, device] : _devices)
                gone.push_back(device->id);
            _devices.clear();
        }
        for (const HostDeviceId& id : gone)
            _sink->OnDeviceRemoved(id);
    }

    static void Added(void* context, IOReturn, void*, IOHIDDeviceRef ref)
    {
        static_cast<IoHidBackend*>(context)->OnAdded(ref);
    }
    static void Removed(void* context, IOReturn, void*, IOHIDDeviceRef ref)
    {
        static_cast<IoHidBackend*>(context)->OnRemoved(ref);
    }
    static void Report(void* context, IOReturn, void*, IOHIDReportType, uint32_t, uint8_t* report, CFIndex size,
                       uint64_t stamp)
    {
        Device* device = static_cast<Device*>(context);
        device->owner->OnReport(*device, report, static_cast<size_t>(size), ReportTimeNs(stamp));
    }

    SurfaceDialWriter* WriterLocked(const HostDeviceId& device) const
    {
        for (const auto& [ref, known] : _devices)
            if (known->id == device && known->writer)
                return known->writer.get();
        return nullptr;
    }

    void OnAdded(IOHIDDeviceRef ref)
    {
        const uint16_t vendor = static_cast<uint16_t>(IntProperty(ref, CFSTR(kIOHIDVendorIDKey)));
        const uint16_t product = static_cast<uint16_t>(IntProperty(ref, CFSTR(kIOHIDProductIDKey)));
        const int version = IntProperty(ref, CFSTR(kIOHIDVersionNumberKey));
        const std::string transport = StringProperty(ref, CFSTR(kIOHIDTransportKey));
        const bool bluetooth = transport.find("Bluetooth") != std::string::npos;
        DeviceIdentity identity;
        identity.bus = bluetooth ? "bt" : (transport == "USB" ? "usb" : "");
        identity.vendor = vendor;
        identity.product = product;
        identity.version = version;
        identity.name = StringProperty(ref, CFSTR(kIOHIDProductKey));
        identity.backend = Name();
        const DeviceMatch match = DeviceDatabase::Builtin().Lookup(identity);
        // Apple's synthetic legacy pad mirrors a GIP pad we read directly (research-host-apis.md §2.1)
        if (match.ignore)
            return;

        auto device = std::make_unique<Device>();
        device->owner = this;
        device->ref = ref;
        // A USB GIP pad gets no rumble on macOS: its dext takes no rumble report, and GameController accepts the
        // haptics of a pad over USB but drives no motor (haptics.md §9, SDL issue #14800)
        constexpr uint16_t kMicrosoft = 0x045E;
        device->xboxBluetooth = bluetooth && vendor == kMicrosoft && match.layout == "xbox" &&
                                match.family != "xbox-360";
        device->dualSenseUsb = !bluetooth && DualSenseOutput::Handles(vendor, product);
        auto rumbleUnavailable = [&]() -> std::string {
            if (device->dualSenseUsb || device->xboxBluetooth)
                return {};
            if (DualSenseOutput::Handles(vendor, product))
                return "A DualSense rumbles over the USB cable only for now";
            if (vendor == kMicrosoft && match.family == "xbox-360")
                return "Xbox 360 pads do not rumble on macOS yet";
            if (vendor == kMicrosoft && match.layout == "xbox")
                return "macOS drives no motors of an Xbox pad over USB: connect the pad over Bluetooth";
            return "On macOS rumble works for Xbox pads over Bluetooth and a DualSense over USB; this device has no "
                   "motors the emulator can drive";
        };
        device->rumbleUnavailable = rumbleUnavailable();
        const int maxInput = IntProperty(ref, CFSTR(kIOHIDMaxInputReportSizeKey));
        const HidLayout layout = HidLayoutFor(match.layout);
        if (XboxGipPad::Handles(vendor, product) && maxInput == 18)
            device->gip = std::make_unique<XboxGipPad>();
        else if (SurfaceDial::Handles(vendor, product))
            device->dial = std::make_unique<SurfaceDial>();
        else
        {
            CFTypeRef data = IOHIDDeviceGetProperty(ref, CFSTR(kIOHIDReportDescriptorKey));
            if (data == nullptr || CFGetTypeID(data) != CFDataGetTypeID())
                return;
            HidDescriptor descriptor;
            std::string error;
            if (!ParseHidDescriptor(CFDataGetBytePtr(static_cast<CFDataRef>(data)),
                                    static_cast<size_t>(CFDataGetLength(static_cast<CFDataRef>(data))), descriptor,
                                    error))
                return;
            if (SurfaceDial::Matches(descriptor))
                device->dial = std::make_unique<SurfaceDial>(descriptor);
            else
            {
                device->hid = std::make_unique<HidPad>(descriptor, layout);
                if (device->hid->Empty())
                    return;
            }
        }
        if (device->dial)
        {
            device->rumbleUnavailable.clear();  // no motors, but its clicks play (PlayClicks)
            device->writer = MakeDialWriter(ref);
        }

        // architecture.md §5: usb:<vid>:<pid>:<version>:<serial>, usb:...@<path hash>, bt:<vid>:<pid>:<hash>
        const std::string serial = StringProperty(ref, CFSTR(kIOHIDSerialNumberKey));
        const std::string location = std::to_string(IntProperty(ref, CFSTR(kIOHIDLocationIDKey)));
        std::string id;
        if (bluetooth)
            id = "bt:" + Hex4(vendor) + ":" + Hex4(product) + ":" + Hash(serial.empty() ? location : serial);
        else if (!serial.empty())
            id = "usb:" + Hex4(vendor) + ":" + Hex4(product) + ":" + Hex4(version) + ":" + serial;
        else
            id = "usb:" + Hex4(vendor) + ":" + Hex4(product) + ":" + Hex4(version) + "@" + Hash(location);

        HostDeviceInfo info;
        info.name = match.name;
        info.family = match.family;
        info.layout = match.layout;
        info.backend = Name();
        {
            std::lock_guard lock(_mutex);
            std::string unique = id;
            for (int n = 2; std::any_of(_devices.begin(), _devices.end(),
                                        [&unique](const auto& entry) { return entry.second->id == unique; });
                 ++n)
                unique = id + "#" + std::to_string(n);
            device->id = unique;
            device->buffer.resize(static_cast<size_t>(maxInput > 0 ? maxInput : 64));
            info.id = unique;
            // A Dial that reconnects gets the steps it had (architecture.md §9: the driver applies them again)
            if (device->writer)
                if (const auto steps = _dialSteps.find(unique); steps != _dialSteps.end())
                    device->writer->Steps(steps->second);
        }
        Device* raw = device.get();
        {
            std::lock_guard lock(_mutex);
            _devices[ref] = std::move(device);
        }
        _sink->OnDeviceAdded(info);
        // With the report's time stamp: the encoder's speed counts from when the report came, not when it ran
        IOHIDDeviceRegisterInputReportWithTimeStampCallback(ref, raw->buffer.data(),
                                                            static_cast<CFIndex>(raw->buffer.size()),
                                                            &IoHidBackend::Report, raw);
    }

    void OnRemoved(IOHIDDeviceRef ref)
    {
        std::unique_ptr<Device> device;
        {
            std::lock_guard lock(_mutex);
            auto it = _devices.find(ref);
            if (it == _devices.end())
                return;
            device = std::move(it->second);
            _devices.erase(it);
        }
        IOHIDDeviceRegisterInputReportWithTimeStampCallback(ref, device->buffer.data(),
                                                            static_cast<CFIndex>(device->buffer.size()), nullptr,
                                                            nullptr);
        _sink->OnDeviceRemoved(device->id);
        // The writer ends with the device: a write in flight finishes first (a gone device refuses it at once)
    }

    void ExpireDials()
    {
        std::vector<Device*> dials;
        {
            std::lock_guard lock(_mutex);
            for (const auto& [ref, device] : _devices)
                if (device->dial)
                    dials.push_back(device.get());  // only this thread erases
        }
        const uint64_t now = DialNowMs();
        for (Device* device : dials)
            device->dial->Expire(now, [this, device](const std::string& control, float value) {
                _sink->OnControl({device->id, control, value, 0});
            });
    }

    void OnReport(Device& device, const uint8_t* report, size_t size, uint64_t timeNs)
    {
        const auto sink = [this, &device, timeNs](const std::string& control, float value) {
            _sink->OnControl({device.id, control, value, timeNs});
        };
        if (device.gip)
            device.gip->Decode(report, size, sink);
        else if (device.dial)
            device.dial->Decode(report, size, sink);
        else if (device.hid)
            device.hid->Decode(report, size, sink);
    }

    IBackendSink* _sink = nullptr;
    std::thread _thread;
    std::atomic<CFRunLoopRef> _loop{nullptr};
    std::atomic<bool> _stopping{false};
    mutable std::mutex _mutex;
    std::map<IOHIDDeviceRef, std::unique_ptr<Device>> _devices;
    std::map<HostDeviceId, StepsOut> _dialSteps;  ///< under _mutex: each Dial's last steps, kept across a reconnect
};
}  // namespace

std::unique_ptr<IBackend> CreateIoHidBackend()
{
    return std::make_unique<IoHidBackend>();
}

}  // namespace controllers
