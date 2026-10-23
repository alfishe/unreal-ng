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
#include "endpointmerge.h"
#include "hid/drivers/dualsenseoutput.h"
#include "hid/drivers/surfacedial.h"
#include "hid/drivers/xboxgip.h"
#include "hid/hiddescriptor.h"
#include "hid/hidpad.h"
#include "platform/macos/gccompanion.h"

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

/// The Dial's writer: output and feature reports, and feature report 1 read (the found settings, the read-back)
std::unique_ptr<SurfaceDialWriter> MakeDialWriter(IOHIDDeviceRef ref)
{
    auto device = std::make_shared<RetainedDevice>(ref);
    return std::make_unique<SurfaceDialWriter>(
        [device](SurfaceDialWriter::ReportType type, const std::vector<uint8_t>& report) {
            const IOHIDReportType kind =
                type == SurfaceDialWriter::ReportType::Feature ? kIOHIDReportTypeFeature : kIOHIDReportTypeOutput;
            return IOHIDDeviceSetReport(device->ref, kind, report[0], report.data(),
                                        static_cast<CFIndex>(report.size())) == kIOReturnSuccess;
        },
        [device](std::vector<uint8_t>& report) {
            report.assign(16, 0);
            report[0] = SurfaceDial::kFeatureReport;
            CFIndex size = static_cast<CFIndex>(report.size());
            if (IOHIDDeviceGetReport(device->ref, kIOHIDReportTypeFeature, SurfaceDial::kFeatureReport, report.data(),
                                     &size) != kIOReturnSuccess)
                return false;
            report.resize(static_cast<size_t>(size));
            return true;
        });
}

uint64_t SteadyNowMs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

/// The presses EndpointMerge correlates with GameController's: the buttons GcCompanion watches
bool IsMatchButton(const std::string& control)
{
    return control == "south" || control == "east" || control == "west" || control == "north" || control == "lb" ||
           control == "rb";
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

    /// The motors: an IOHID output report where the pad takes one, else GameController's haptics once its
    /// GCController is matched to the device (EndpointMerge, D18)
    bool HasRumble(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        return NativeRumbleLocked(device) != nullptr || CompanionActuatorsLocked(device) != 0;
    }

    std::string RumbleUnavailable(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        const std::vector<Device*> endpoints = EndpointsLocked(device);
        if (endpoints.empty())
            return "The device is not connected";
        if (NativeRumbleLocked(device) != nullptr || CompanionActuatorsLocked(device) != 0)
            return {};
        const Device& input = *endpoints.front();
        const bool waitingWithMotors =
            std::any_of(_companionPads.begin(), _companionPads.end(), [this](const auto& entry) {
                return entry.second.actuators != 0 && _merge.DeviceOf(entry.first).empty();
            });
        if (!input.xboxUsb && waitingWithMotors && _merge.StateOf(device) == EndpointMerge::MatchState::Tentative)
            return "Press a button on the pad: macOS drives its motors through GameController once the emulator has "
                   "matched the two";
        return input.rumbleUnavailable;
    }

    uint8_t RumbleActuators(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        if (NativeRumbleLocked(device) != nullptr)
            return kRumbleLow | kRumbleHigh | kRumbleLeftTrigger | kRumbleRightTrigger;
        for (const Device* known : EndpointsLocked(device))
            if (known->dial)
                return kHapticClicker;
        return CompanionActuatorsLocked(device);
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
        if (Device* known = NativeRumbleLocked(device))
        {
            if (known->dualSenseUsb)
            {
                // The triggers' levels become their resistance (haptics.md §6.4); zero turns it off
                const std::vector<uint8_t> report = DualSenseOutput::UsbReport(levels);
                return IOHIDDeviceSetReport(known->ref, kIOHIDReportTypeOutput, report[0], report.data(),
                                            static_cast<CFIndex>(report.size())) == kIOReturnSuccess;
            }
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
        // GameController's haptics: queued on its own thread, never blocks
        return CompanionActuatorsLocked(device) != 0 && _companion &&
               _companion->SetRumble(_merge.CompanionOf(device), levels);
    }

    /// Logical devices: the endpoints of one physical device count once
    size_t DeviceCount() const override
    {
        std::lock_guard lock(_mutex);
        size_t count = 0;
        for (const auto& [ref, device] : _devices)
            count += _merge.IsInput(device->key) ? 1 : 0;
        return count;
    }

private:
    struct Device
    {
        HostDeviceId id;  ///< the logical device's (EndpointMerge): shared by the endpoints of one physical device
        std::string key;  ///< this endpoint's, for EndpointMerge
        /// This endpoint gives the device's input (the one of its endpoints with the most controls)
        std::atomic<bool> input{true};
        std::vector<uint8_t> buffer;
        std::unique_ptr<HidPad> hid;
        std::unique_ptr<XboxGipPad> gip;
        std::unique_ptr<SurfaceDial> dial;
        std::unique_ptr<SurfaceDialWriter> writer;  ///< a Dial's feature and output reports
        IoHidBackend* owner = nullptr;
        IOHIDDeviceRef ref = nullptr;
        bool xboxBluetooth = false;  ///< rumble through output report 3 (an Xbox One / Series pad over Bluetooth)
        bool dualSenseUsb = false;   ///< rumble and trigger resistance through output report 2
        /// An Xbox pad over USB: GameController lists its haptics and drives no motor (haptics.md §9), so the companion
        /// does not take its rumble either
        bool xboxUsb = false;
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
        StartCompanion();

        // 0.1 s: a Surface Dial's inferred touch ends on time between its reports (DialTouch)
        while (!_stopping)
        {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
            ExpireDials();
        }

        _loop = nullptr;
        // First: its callbacks take the lock and look at the devices
        if (_companion)
            _companion->Stop();
        IOHIDManagerRegisterDeviceMatchingCallback(manager, nullptr, nullptr);
        IOHIDManagerRegisterDeviceRemovalCallback(manager, nullptr, nullptr);
        IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
        CFRelease(manager);
        std::vector<HostDeviceId> gone;
        std::map<IOHIDDeviceRef, std::unique_ptr<Device>> devices;
        {
            std::lock_guard lock(_mutex);
            for (auto& [ref, device] : _devices)
                if (_merge.IsInput(device->key))
                    gone.push_back(device->id);
            devices.swap(_devices);
            _merge = EndpointMerge();
            _companion.reset();
        }
        // Outside the lock: a Dial's writer writes its found settings back as it ends (a Bluetooth round trip)
        devices.clear();
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

    /// The endpoints of a logical device, the input one first
    std::vector<Device*> EndpointsLocked(const HostDeviceId& device) const
    {
        std::vector<Device*> endpoints;
        for (const std::string& key : _merge.EndpointsOf(device))
            for (const auto& [ref, known] : _devices)
                if (known->key == key)
                    endpoints.push_back(known.get());
        return endpoints;
    }

    /// The endpoint of @p device that takes a rumble output report; nullptr when none does
    Device* NativeRumbleLocked(const HostDeviceId& device) const
    {
        for (Device* known : EndpointsLocked(device))
            if (known->xboxBluetooth || known->dualSenseUsb)
                return known;
        return nullptr;
    }

    /// The actuators GameController drives for @p device: its matched GCController's, none for an Xbox pad over USB
    uint8_t CompanionActuatorsLocked(const HostDeviceId& device) const
    {
        const std::string companion = _merge.CompanionOf(device);
        if (companion.empty())
            return 0;
        for (const Device* known : EndpointsLocked(device))
            if (known->xboxUsb)
                return 0;
        const auto found = _companionPads.find(companion);
        return found == _companionPads.end() ? 0 : found->second.actuators;
    }

    /// GameController next to IOHID (gccompanion.h): its pads are matched to ours and add outputs, never input
    void StartCompanion()
    {
        auto companion = std::make_unique<GcCompanion>();
        GcCompanion::Callbacks callbacks;
        callbacks.added = [this](const GcCompanion::Pad& pad) {
            std::lock_guard lock(_mutex);
            _companionPads[pad.key] = pad;
            _merge.AddCompanion({pad.key, pad.vendorName, SteadyNowMs()});
        };
        callbacks.removed = [this](const std::string& key) {
            std::lock_guard lock(_mutex);
            _companionPads.erase(key);
            _merge.RemoveCompanion(key);
        };
        callbacks.pressed = [this](const std::string& key, uint64_t timeMs) {
            std::lock_guard lock(_mutex);
            _merge.CompanionEdge(key, timeMs);
        };
        std::string reason;
        if (!companion->Start(std::move(callbacks), reason))
            return;  // IOHID alone: no outputs beyond its own reports
        std::lock_guard lock(_mutex);
        _companion = std::move(companion);
    }

    /// Each endpoint's input flag from the merge: one endpoint per device decodes
    void UpdateInputsLocked(const HostDeviceId& device)
    {
        for (Device* known : EndpointsLocked(device))
            known->input = _merge.IsInput(known->key);
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
        device->xboxUsb = !bluetooth && vendor == kMicrosoft && match.layout == "xbox";
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
        size_t controls = 0;
        if (XboxGipPad::Handles(vendor, product) && maxInput == 18)
        {
            device->gip = std::make_unique<XboxGipPad>();
            controls = XboxGipPad::Controls().size();
        }
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
                controls = device->hid->Controls().size();
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
        // Two interfaces of one known pad are one device (EndpointMerge). A generic device never merges: a two-player
        // encoder shows two pads on one USB port. The Dial has one endpoint
        const bool mergeable = match.family != "generic" && match.player && !device->dial;
        const std::string physical = !mergeable ? std::string()
                                                : identity.bus + ":" + Hex4(vendor) + ":" + Hex4(product) + ":" +
                                                      (serial.empty() ? "@" + location : serial);
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
        Device* raw = device.get();
        bool newDevice = true;
        {
            std::lock_guard lock(_mutex);
            std::string unique = id;
            for (int n = 2; std::any_of(_devices.begin(), _devices.end(),
                                        [&unique](const auto& entry) { return entry.second->id == unique; });
                 ++n)
                unique = id + "#" + std::to_string(n);
            device->key = std::to_string(reinterpret_cast<uintptr_t>(ref));
            const EndpointMerge::HidAdded added =
                _merge.AddHid({device->key, unique, physical, identity.name, controls, SteadyNowMs()});
            newDevice = added.newDevice;
            device->id = added.device;
            device->buffer.resize(static_cast<size_t>(maxInput > 0 ? maxInput : 64));
            info.id = added.device;
            // A Dial that reconnects gets the steps it had (architecture.md §9: the driver applies them again)
            if (device->writer)
                if (const auto steps = _dialSteps.find(info.id); steps != _dialSteps.end())
                    device->writer->Steps(steps->second);
            _devices[ref] = std::move(device);
            UpdateInputsLocked(info.id);
        }
        // A second endpoint of a device already reported adds nothing the sink sees
        if (newDevice)
            _sink->OnDeviceAdded(info);
        // With the report's time stamp: the encoder's speed counts from when the report came, not when it ran
        IOHIDDeviceRegisterInputReportWithTimeStampCallback(ref, raw->buffer.data(),
                                                            static_cast<CFIndex>(raw->buffer.size()),
                                                            &IoHidBackend::Report, raw);
    }

    void OnRemoved(IOHIDDeviceRef ref)
    {
        std::unique_ptr<Device> device;
        EndpointMerge::HidRemoved removed;
        {
            std::lock_guard lock(_mutex);
            auto it = _devices.find(ref);
            if (it == _devices.end())
                return;
            device = std::move(it->second);
            _devices.erase(it);
            removed = _merge.RemoveHid(device->key);
            if (!removed.deviceGone)
                UpdateInputsLocked(removed.device);  // another endpoint of the pad gives its input now
        }
        IOHIDDeviceRegisterInputReportWithTimeStampCallback(ref, device->buffer.data(),
                                                            static_cast<CFIndex>(device->buffer.size()), nullptr,
                                                            nullptr);
        if (removed.deviceGone)
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
        if (!device.input)
            return;  // another endpoint of the same pad gives its input: one press, one event
        const auto sink = [this, &device, timeNs](const std::string& control, float value) {
            // A press for the GameController match, in the time it reached us (as the companion's presses)
            if (value > 0.5f && IsMatchButton(control))
            {
                std::lock_guard lock(_mutex);
                _merge.HidEdge(device.id, SteadyNowMs());
            }
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
    EndpointMerge _merge;                          ///< under _mutex: endpoints -> devices, GCController matches
    std::unique_ptr<GcCompanion> _companion;       ///< set once on the io thread; its calls need no lock
    std::map<std::string, GcCompanion::Pad> _companionPads;  ///< under _mutex
};
}  // namespace

std::unique_ptr<IBackend> CreateIoHidBackend()
{
    return std::make_unique<IoHidBackend>();
}

}  // namespace controllers
