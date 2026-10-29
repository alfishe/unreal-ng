#include "platform/macos/iohidbackend.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <IOKit/hid/IOHIDManager.h>

#include <mach/mach_time.h>
#include <pthread.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <future>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "devicedatabase.h"
#include "endpointmerge.h"
#include "gcpad.h"
#include "hid/drivers/dualsenseoutput.h"
#include "hid/drivers/gamecubeadapter.h"
#include "hid/drivers/padprotocol.h"
#include "hid/drivers/sonypad.h"
#include "hid/drivers/steampad.h"
#include "hid/drivers/surfacedial.h"
#include "hid/drivers/switchpro.h"
#include "hid/drivers/xboxbluetoothoutput.h"
#include "hid/drivers/xboxgip.h"
#include "hid/hiddescriptor.h"
#include "hid/hidpad.h"
#include "joyconpairing.h"
#include "hid/reporttap.h"
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

/// The writer thread of a pad that talks a protocol (SonyProtocol, SwitchProtocol): output reports and feature reads
std::unique_ptr<PadPump> MakePump(IOHIDDeviceRef ref, std::unique_ptr<IPadProtocol> protocol)
{
    auto device = std::make_shared<RetainedDevice>(ref);
    return std::make_unique<PadPump>(
        std::move(protocol),
        [device](const std::vector<uint8_t>& report) {
            return IOHIDDeviceSetReport(device->ref, kIOHIDReportTypeOutput, report[0], report.data(),
                                        static_cast<CFIndex>(report.size())) == kIOReturnSuccess;
        },
        [device](uint8_t id, size_t size, std::vector<uint8_t>& data) {
            data.assign(size, 0);
            data[0] = id;
            CFIndex length = static_cast<CFIndex>(size);
            if (IOHIDDeviceGetReport(device->ref, kIOHIDReportTypeFeature, id, data.data(), &length) != kIOReturnSuccess)
                return false;
            data.resize(static_cast<size_t>(length));
            return true;
        },
        PadPump::Clock{},
        [device](const std::vector<uint8_t>& report) {
            // hidapi's rule: a report id of 0 is no id, the data after it goes alone
            const bool noId = report[0] == 0;
            const uint8_t* data = report.data() + (noId ? 1 : 0);
            const CFIndex size = static_cast<CFIndex>(report.size() - (noId ? 1 : 0));
            return IOHIDDeviceSetReport(device->ref, kIOHIDReportTypeFeature, report[0], data, size) ==
                   kIOReturnSuccess;
        });
}

/// `gc:<vendor name>#<n>` (architecture.md §5): the name in lower case, every run of other characters one '-'
std::string GcSlug(const std::string& vendorName)
{
    std::string slug;
    for (unsigned char c : vendorName)
    {
        if (std::isalnum(c))
            slug.push_back(static_cast<char>(std::tolower(c)));
        else if (!slug.empty() && slug.back() != '-')
            slug.push_back('-');
    }
    while (!slug.empty() && slug.back() == '-')
        slug.pop_back();
    return slug.empty() ? "pad" : slug;
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

/// Every HID interface of one vendor (Valve's Steam Controllers: their pad is a vendor-page interface)
CFDictionaryRef VendorMatching(int vendor)
{
    CFNumberRef vendorRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor);
    const void* keys[] = {CFSTR(kIOHIDVendorIDKey)};
    const void* values[] = {vendorRef};
    CFDictionaryRef dict = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);
    CFRelease(vendorRef);
    return dict;
}

/// One product (the GameCube adapter, whatever usage its descriptor gives)
CFDictionaryRef ProductMatching(int vendor, int product)
{
    CFNumberRef vendorRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor);
    CFNumberRef productRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &product);
    const void* keys[] = {CFSTR(kIOHIDVendorIDKey), CFSTR(kIOHIDProductIDKey)};
    const void* values[] = {vendorRef, productRef};
    CFDictionaryRef dict = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);
    CFRelease(vendorRef);
    CFRelease(productRef);
    return dict;
}

/// A GameCube adapter's port as a device of its own: `<adapter id>:p1` .. `:p4`
HostDeviceId PortId(const HostDeviceId& adapter, int port)
{
    return adapter + ":p" + std::to_string(port + 1);
}

/// The adapter's id and the port (0-3) of a port's id; port -1 for any other id
std::pair<HostDeviceId, int> SplitPortId(const HostDeviceId& id)
{
    if (id.size() > 3 && id[id.size() - 3] == ':' && id[id.size() - 2] == 'p' && id.back() >= '1' && id.back() <= '4')
        return {id.substr(0, id.size() - 3), id.back() - '1'};
    return {id, -1};
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
        if (PairLocked(device).first != nullptr)
            return true;
        return NativeRumbleLocked(device) != nullptr || CompanionActuatorsLocked(device) != 0;
    }

    /// The motors of a `gc:` device: its GCController's haptics
    std::string GcUnavailableLocked(const std::string& companion, bool light) const
    {
        const auto found = _companionPads.find(companion);
        if (found == _companionPads.end())
            return "The device is not connected";
        if (light)
            return found->second.light ? std::string() : "GameController gives this pad no light";
        return found->second.actuators != 0 ? std::string() : "GameController gives this pad no haptics";
    }

    std::string RumbleUnavailable(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        if (const auto gc = _gcKeys.find(device); gc != _gcKeys.end())
            return GcUnavailableLocked(gc->second, false);
        if (PairLocked(device).first != nullptr)
            return {};
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
        if (PairLocked(device).first != nullptr)
            return static_cast<uint8_t>(kRumbleLow | kRumbleHigh);  // the left half's motor and the right one's
        if (const Device* native = NativeRumbleLocked(device))
            return native->nativeActuators;
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

    /// The Xbox Bluetooth rumble report 3 with the impulse triggers (XboxBluetoothOutput::RumbleReport); a
    /// DualSense / DualShock 4 (USB and Bluetooth) and a Switch Pro controller through their protocol's pump (queued,
    /// never blocks)
    bool SetRumble(const HostDeviceId& device, const RumbleLevels& levels) override
    {
        std::lock_guard lock(_mutex);
        if (const auto [left, right] = PairLocked(device); left != nullptr)
        {
            // A Joy-Con pair: the left half the low motor's level, the right one the high one's (SDL)
            left->pump->SetRumble(SwitchProPad::HalfRumble(levels, true));
            right->pump->SetRumble(SwitchProPad::HalfRumble(levels, false));
            return true;
        }
        if (Device* known = NativeRumbleLocked(device))
        {
            if (known->pump)
            {
                // A DualSense's trigger levels become their resistance (haptics.md §6.4); zero turns it off
                known->pump->SetRumble(levels);
                return true;
            }
            const std::vector<uint8_t> report = XboxBluetoothOutput::RumbleReport(levels);
            return IOHIDDeviceSetReport(known->ref, kIOHIDReportTypeOutput, report[0], report.data(),
                                        static_cast<CFIndex>(report.size())) == kIOReturnSuccess;
        }
        // GameController's haptics: queued on its own thread, never blocks
        return CompanionActuatorsLocked(device) != 0 && _companion &&
               _companion->SetRumble(CompanionKeyLocked(device), levels);
    }

    /// region <Battery and light>
    /// The pad's own report where its driver reads one (a DualSense / DualShock 4 full report, a Switch Pro
    /// controller's 0x30), else GameController's battery of the matched GCController; unknown before the match and
    /// for pads GameController reports none for (the Elite 2: state unknown, level 0)
    BatteryState Battery(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        if (const auto [left, right] = PairLocked(device); left != nullptr)
            return JoyConPairing::Battery(left->battery, right->battery);
        for (const Device* known : EndpointsLocked(device))
            if (known->battery.known)
                return known->battery;
        const std::string companion = CompanionKeyLocked(device);
        return companion.empty() || !_companion ? BatteryState{} : _companion->Battery(companion);
    }

    /// A DualSense over USB through its output report, else GCDeviceLight once the GCController is matched
    bool HasLight(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        return NativeLightLocked(device) != nullptr || CompanionLightLocked(device);
    }

    std::string LightUnavailable(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        if (const auto gc = _gcKeys.find(device); gc != _gcKeys.end())
            return GcUnavailableLocked(gc->second, true);
        if (PairLocked(device).first != nullptr)
            return "Joy-Cons have no light the emulator can set (their player lights show the player)";
        if (EndpointsLocked(device).empty())
            return "The device is not connected";
        if (NativeLightLocked(device) != nullptr || CompanionLightLocked(device))
            return {};
        const bool waitingWithLight =
            std::any_of(_companionPads.begin(), _companionPads.end(), [this](const auto& entry) {
                return entry.second.light && _merge.DeviceOf(entry.first).empty();
            });
        if (waitingWithLight && _merge.StateOf(device) == EndpointMerge::MatchState::Tentative)
            return "Press a button on the pad: macOS sets its light through GameController once the emulator has "
                   "matched the two";
        return "This device has no light the emulator can set";
    }

    bool SetLight(const HostDeviceId& device, const LightColor& color) override
    {
        std::lock_guard lock(_mutex);
        // The light bar report through the pad's pump (the first one releases the firmware's light bar)
        if (Device* known = NativeLightLocked(device))
            return known->pump->SetLight(color);
        // GCDeviceLight: queued on the companion's thread, never blocks
        return CompanionLightLocked(device) && _companion && _companion->SetLight(CompanionKeyLocked(device), color);
    }
    /// endregion </Battery and light>

    std::vector<EndpointInfo> Endpoints(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        std::vector<EndpointInfo> endpoints;
        for (const Device* known : EndpointsLocked(device))
        {
            EndpointInfo endpoint;
            endpoint.backend = Name();
            endpoint.name = known->name;
            endpoint.role = known->input ? "input" : "none";
            endpoint.controls = known->controls;
            endpoint.driver = known->gip         ? "xbox gip"
                              : known->dial      ? "surface dial"
                              : known->sony      ? "sony"
                              : known->switchPro ? "switch pro"
                                                 : "hid descriptor";
            endpoint.rumble = known->xboxBluetooth || (known->pump != nullptr) || (known->dial != nullptr);
            endpoint.light = known->pump != nullptr && known->pumpLight;
            endpoint.detail = known->hasReportIds ? "report ids" : "";
            endpoints.push_back(std::move(endpoint));
        }
        const std::string companion = _merge.CompanionOf(device);
        if (!companion.empty())
        {
            EndpointInfo endpoint;
            endpoint.backend = "gamecontroller";
            endpoint.role = "outputs";
            endpoint.driver = "gamecontroller";
            if (const auto found = _companionPads.find(companion); found != _companionPads.end())
            {
                endpoint.name = found->second.vendorName;
                endpoint.rumble = found->second.actuators != 0;
                endpoint.light = found->second.light;
            }
            endpoint.detail = _merge.StateOf(device) == EndpointMerge::MatchState::Tentative ? "tentative match"
                                                                                               : "confirmed match";
            endpoints.push_back(std::move(endpoint));
        }
        return endpoints;
    }

    bool HasRawReports(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        return !EndpointsLocked(device).empty();
    }

    std::vector<RawReport> RawReports(const HostDeviceId& device) override
    {
        std::lock_guard lock(_mutex);
        for (Device* known : EndpointsLocked(device))
            if (known->input)
                return known->tap.Read(SteadyNowMs());
        return {};
    }

    /// Logical devices: the endpoints of one physical device count once
    size_t DeviceCount() const override
    {
        std::lock_guard lock(_mutex);
        size_t count = _gcKeys.size();
        for (const auto& [ref, device] : _devices)
        {
            count += _merge.IsInput(device->key) && !device->gripPending && !device->slotPending ? 1 : 0;
            count += static_cast<size_t>(std::count(device->portShown.begin(), device->portShown.end(), true));
        }
        return count - _joyCons.PairCount();  // two Joy-Cons, one pad
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
        /// A DualSense / DualShock 4 full report (USB, Bluetooth 0x31 / 0x11); its short Bluetooth report stays hid's
        std::unique_ptr<SonyPad> sony;
        /// A Switch Pro controller's or a Joy-Con's full report 0x30 and subcommand replies; the simple 0x3F stays hid's
        std::unique_ptr<SwitchProPad> switchPro;
        int joyCon = 0;         ///< 1 a left Joy-Con, 2 a right one (JoyConPairing)
        /// The id its events go out under: its own, or its Joy-Con pair's (written on the io thread under _mutex, read
        /// there by OnReport)
        HostDeviceId reported;
        HostDeviceInfo single;  ///< a Joy-Con's own device, for when it is alone again
        bool shown = false;     ///< under _mutex: a Joy-Con's own device is reported (it is alone)
        /// A charging grip's half waiting for its device info: not reported, its input dropped (io thread; atomic for
        /// DeviceCount)
        std::atomic<bool> gripPending{false};
        /// A Steam Controller's state report and lizard-mode set-up (SteamPad, SteamProtocol)
        std::unique_ptr<SteamPad> steam;
        /// A slot of a wireless dongle (Steam) with no pad on it: not reported, until a pad's connect event or state
        /// (io thread; atomic for DeviceCount)
        std::atomic<bool> slotPending{false};
        /// A GameCube adapter: never reported itself, each port with a controller in it a device (PortId)
        std::unique_ptr<GameCubeAdapter> gameCube;
        std::array<bool, GameCubeAdapter::kPorts> portShown{};  ///< under _mutex: the ports reported
        /// The device info's type (SwitchProtocol::TypeSink, from the pump or the io thread); -1 not yet. Shared: the
        /// pump's thread may still write it while the device goes
        std::shared_ptr<std::atomic<int>> gripType = std::make_shared<std::atomic<int>>(-1);
        /// The pad's protocol (SonyProtocol, SwitchProtocol): rumble, light, player LEDs, set-up reports
        std::unique_ptr<PadPump> pump;
        bool pumpLight = false;       ///< the pump sets a light bar colour (the PlayStation pads)
        int player = 0;               ///< the number its player LEDs show (pump pads), 1..
        BatteryState battery;         ///< under _mutex: from the pad's own report (sony, switchPro)
        BatteryState lastBattery;     ///< the io thread's copy: battery is written under the lock only on a change
        uint8_t nativeActuators = 0;  ///< the actuators its native output drives
        IoHidBackend* owner = nullptr;
        IOHIDDeviceRef ref = nullptr;
        bool xboxBluetooth = false;  ///< rumble through output report 3 (an Xbox One / Series pad over Bluetooth)
        /// An Xbox pad over USB: GameController lists its haptics and drives no motor (haptics.md §9), so the companion
        /// does not take its rumble either
        bool xboxUsb = false;
        std::string rumbleUnavailable;  ///< why neither: the Test haptics button's tooltip
        std::string name;               ///< kIOHIDProductKey, for the endpoint table
        size_t controls = 0;
        bool hasReportIds = false;
        ReportTap tap;  ///< the test panel's raw reports (host-only, while it reads them)
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
        // Valve's devices all: the Steam Controllers' pad is a vendor-page interface (SteamPad), their keyboard and
        // mouse ("lizard mode") are dropped in OnAdded
        // The database's control surfaces by id: their collections are not a pad's (the ShuttlePRO is a Consumer
        // Control). A user family added later is matched at the next start
        std::vector<CFDictionaryRef> matches = {
            Matching(0x04),
            Matching(0x05),
            Matching(0x08),
            Matching(0x0E),
            VendorMatching(SteamPad::kValve),
            ProductMatching(GameCubeAdapter::kNintendo, GameCubeAdapter::kAdapter)};
        for (const auto& [vendor, product] : DeviceDatabase::Active()->SurfaceProducts())
            matches.push_back(ProductMatching(vendor, product));
        CFArrayRef matching = CFArrayCreate(kCFAllocatorDefault, reinterpret_cast<const void**>(matches.data()),
                                            static_cast<CFIndex>(matches.size()), &kCFTypeArrayCallBacks);
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
            ReportStandaloneCompanions();
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
            {
                if (_merge.IsInput(device->key) && !device->gripPending && !device->slotPending &&
                    std::find(gone.begin(), gone.end(), device->reported) == gone.end())
                    gone.push_back(device->reported);  // a Joy-Con pair once
                for (int port = 0; port < GameCubeAdapter::kPorts; port++)
                    if (device->portShown[static_cast<size_t>(port)])
                        gone.push_back(PortId(device->reported, port));
            }
            for (const auto& [id, key] : _gcKeys)
                gone.push_back(id);
            _gcKeys.clear();
            _gcIds.clear();
            _gcHeld.clear();
            _joyCons = JoyConPairing();
            devices.swap(_devices);
            _merge = EndpointMerge();
            _companion.reset();
        }
        // Outside the lock: a Dial's writer writes its found settings back as it ends (a Bluetooth round trip), a pad's
        // pump stops its motors
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
        // A GameCube adapter's port: the adapter's endpoint, while a controller is in that port
        if (const auto [adapter, port] = SplitPortId(device); port >= 0)
        {
            for (const std::string& key : _merge.EndpointsOf(adapter))
                for (const auto& [ref, known] : _devices)
                    if (known->key == key && known->gameCube && known->portShown[static_cast<size_t>(port)])
                        endpoints.push_back(known.get());
            if (!endpoints.empty())
                return endpoints;
        }
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
            if (known->xboxBluetooth || (known->pump && known->nativeActuators != 0))
                return known;
        return nullptr;
    }

    /// The endpoint of @p device whose pump sets a light bar; nullptr when none does
    Device* NativeLightLocked(const HostDeviceId& device) const
    {
        for (Device* known : EndpointsLocked(device))
            if (known->pump && known->pumpLight)
                return known;
        return nullptr;
    }

    /// The GCController that gives @p device its outputs: a `gc:` device's own, else the one matched to it
    std::string CompanionKeyLocked(const HostDeviceId& device) const
    {
        if (const auto gc = _gcKeys.find(device); gc != _gcKeys.end())
            return gc->second;
        return _merge.CompanionOf(device);
    }

    /// The matched GCController of @p device has a light
    bool CompanionLightLocked(const HostDeviceId& device) const
    {
        const std::string companion = CompanionKeyLocked(device);
        if (companion.empty())
            return false;
        const auto found = _companionPads.find(companion);
        return found != _companionPads.end() && found->second.light;
    }

    /// The actuators GameController drives for @p device: its matched GCController's, none for an Xbox pad over USB
    uint8_t CompanionActuatorsLocked(const HostDeviceId& device) const
    {
        const std::string companion = CompanionKeyLocked(device);
        if (companion.empty())
            return 0;
        for (const Device* known : EndpointsLocked(device))
            if (known->xboxUsb)
                return 0;
        const auto found = _companionPads.find(companion);
        return found == _companionPads.end() ? 0 : found->second.actuators;
    }

    /// A `gc:` device the match just promoted: it gives way to its IOHID device (architecture.md §5)
    using Promotion = std::pair<HostDeviceId, HostDeviceId>;
    void PromoteLocked(const std::optional<EndpointMerge::Confirmed>& confirmed, std::vector<Promotion>& out)
    {
        if (!confirmed || !confirmed->promoted)
            return;
        const auto id = _gcIds.find(confirmed->companion);
        if (id == _gcIds.end())
            return;
        out.emplace_back(id->second, confirmed->device);
        _gcKeys.erase(id->second);
        _gcIds.erase(id);
        if (_companion)
            _companion->SetInput(confirmed->companion, false);
    }
    /// D18: a `gc:` device's input stops while an IOHID device of its name waits for the match, and comes back when
    /// that device goes or is another pad (EndpointMerge::IsShadowed); the switches for the companion, outside the lock
    using InputSwitch = std::pair<std::string, bool>;
    void SyncHeldLocked(std::vector<InputSwitch>& out)
    {
        for (const auto& [key, id] : _gcIds)
        {
            const bool held = _merge.IsShadowed(key);
            if (held == (_gcHeld.count(key) != 0))
                continue;
            if (held)
                _gcHeld.insert(key);
            else
                _gcHeld.erase(key);
            out.emplace_back(key, !held);
        }
        for (auto it = _gcHeld.begin(); it != _gcHeld.end();)
            it = _gcIds.count(*it) == 0 ? _gcHeld.erase(it) : std::next(it);  // promoted or gone
    }
    void SwitchInputs(const std::vector<InputSwitch>& switches)
    {
        if (_companion)
            for (const auto& [key, on] : switches)
                _companion->SetInput(key, on);
    }
    void Promote(const std::vector<Promotion>& promotions)
    {
        for (const auto& [from, to] : promotions)
        {
            _sink->OnDeviceRemoved(from);
            _sink->OnDevicePromoted(from, to);
        }
    }

    /// The companions no IOHID device claims after the arrival window: `gc:` devices with GameController's input
    void ReportStandaloneCompanions()
    {
        std::vector<std::pair<std::string, HostDeviceInfo>> added;
        {
            std::lock_guard lock(_mutex);
            if (!_companion)
                return;
            for (const std::string& key : _merge.StandaloneDue(SteadyNowMs()))
            {
                const auto pad = _companionPads.find(key);
                if (pad == _companionPads.end())
                    continue;
                const std::string base = "gc:" + GcSlug(pad->second.vendorName) + "#";
                std::string id;
                for (int n = 1; id.empty() || _gcKeys.count(id) != 0; ++n)
                    id = base + std::to_string(n);
                DeviceIdentity identity;
                identity.name = pad->second.vendorName;
                identity.backend = Name();
                const DeviceMatch match = DeviceDatabase::Builtin().Lookup(identity);
                HostDeviceInfo info;
                info.id = id;
                info.name = pad->second.vendorName.empty() ? match.name : pad->second.vendorName;
                info.family = match.family;
                info.layout = match.layout;
                info.backend = Name();
                _gcKeys[id] = key;
                _gcIds[key] = id;
                added.emplace_back(key, info);
            }
        }
        for (const auto& [key, info] : added)
        {
            _sink->OnDeviceAdded(info);
            _companion->SetInput(key, true);  // its state first, then its changes
        }
    }

    /// GameController next to IOHID (gccompanion.h): its pads are matched to ours and add outputs; a pad IOHID does
    /// not see becomes a `gc:` device with GameController's input
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
            HostDeviceId gone;
            {
                std::lock_guard lock(_mutex);
                _companionPads.erase(key);
                _merge.RemoveCompanion(key);
                if (const auto id = _gcIds.find(key); id != _gcIds.end())
                {
                    gone = id->second;
                    _gcKeys.erase(id->second);
                    _gcIds.erase(id);
                }
                _gcHeld.erase(key);
            }
            if (!gone.empty())
                _sink->OnDeviceRemoved(gone);
        };
        callbacks.pressed = [this](const std::string& key, uint64_t timeMs) {
            std::vector<Promotion> promotions;
            std::vector<InputSwitch> switches;
            {
                std::lock_guard lock(_mutex);
                PromoteLocked(_merge.CompanionEdge(key, timeMs), promotions);
                SyncHeldLocked(switches);
            }
            SwitchInputs(switches);
            Promote(promotions);
        };
        callbacks.control = [this](const std::string& key, const std::string& control, float value) {
            HostDeviceId id;
            {
                std::lock_guard lock(_mutex);
                const auto found = _gcIds.find(key);
                if (found == _gcIds.end())
                    return;
                id = found->second;
            }
            _sink->OnControl({id, control, value, 0});
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

    /// region <Joy-Con pairs>
    /// What a pair change shows the sink, outside the lock: the devices gone first, then the new ones
    struct PairEvents
    {
        std::vector<HostDeviceId> removed;
        std::vector<HostDeviceInfo> added;
    };

    Device* JoyConLocked(const HostDeviceId& id) const
    {
        for (const auto& [ref, known] : _devices)
            if (known->joyCon != 0 && known->id == id)
                return known.get();
        return nullptr;
    }

    /// Both halves of a pair; nullptr, nullptr for any other id
    std::pair<Device*, Device*> PairLocked(const HostDeviceId& device) const
    {
        const std::optional<JoyConPairing::Pair> pair = _joyCons.Find(device);
        if (!pair)
            return {nullptr, nullptr};
        Device* left = JoyConLocked(pair->left);
        Device* right = JoyConLocked(pair->right);
        if (left == nullptr || right == nullptr)
            return {nullptr, nullptr};
        return {left, right};
    }

    /// A pair broke (@p gone left it) and / or one formed: the halves' layouts, ids, player LEDs, and what the sink
    /// sees (on the io thread: the decoders are its own)
    void ApplyJoyConChangeLocked(const JoyConPairing::Change& change, const HostDeviceId& gone, PairEvents& out)
    {
        if (change.broken)
        {
            out.removed.push_back(change.broken->id);
            const HostDeviceId& alone = change.broken->left == gone ? change.broken->right : change.broken->left;
            if (Device* device = JoyConLocked(alone))
            {
                device->reported = device->id;
                device->switchPro->SetLayout(SwitchProPad::JoyConLayout(device->joyCon == 1, false));
                if (device->pump)
                {
                    device->pump->SetPlayer(device->player);
                    device->pump->SetRumble({});
                }
                device->shown = true;
                out.added.push_back(device->single);
            }
        }
        if (!change.formed)
            return;
        const JoyConPairing::Pair& pair = *change.formed;
        Device* left = JoyConLocked(pair.left);
        Device* right = JoyConLocked(pair.right);
        if (left == nullptr || right == nullptr)
            return;
        for (Device* half : {left, right})
        {
            if (half->shown)
            {
                // Shown alone until now: gone, or never shown when it came back alone in this same change
                const auto again = std::find_if(out.added.begin(), out.added.end(),
                                                [half](const HostDeviceInfo& info) { return info.id == half->id; });
                if (again != out.added.end())
                    out.added.erase(again);
                else
                    out.removed.push_back(half->id);
            }
            half->shown = false;
            half->reported = pair.id;
            half->switchPro->SetLayout(SwitchProPad::JoyConLayout(half == left, true));
            if (half->pump)
            {
                half->pump->SetPlayer(left->player);  // both show the pair's number (SDL sets each half's lights)
                half->pump->SetRumble({});
            }
        }
        HostDeviceInfo info = left->single;
        info.id = pair.id;
        info.name = "Joy-Con (L/R)";
        out.added.push_back(info);
    }

    /// A charging grip's half once its device info came: a Joy-Con of that side, paired as any. Another type (no
    /// answer: SDL's CalculateControllerType calls that "a Joy-Con that's missing from a charging grip slot") keeps the
    /// half hidden, its input dropped
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
            device.shown = true;
            const JoyConPairing::Change change = _joyCons.Add(device.id, left);
            if (change.formed)
                device.shown = false;  // straight into the pair
            else
                events.added.push_back(device.single);
            ApplyJoyConChangeLocked(change, {}, events);
        }
        ReportPairEvents(events);
    }

    /// A GameCube adapter's report: a controller put in a port is a device, one taken out is gone
    void OnGameCubeReport(Device& device, const uint8_t* report, size_t size, uint64_t timeNs)
    {
        std::vector<GameCubeAdapter::PortChange> changes;
        std::vector<std::pair<int, std::pair<std::string, float>>> controls;
        if (!device.gameCube->Decode(
                report, size,
                [&controls](int port, const std::string& control, float value) {
                    controls.push_back({port, {control, value}});
                },
                changes))
            return;
        for (const GameCubeAdapter::PortChange& change : changes)
        {
            HostDeviceInfo info;
            {
                std::lock_guard lock(_mutex);
                device.portShown[static_cast<size_t>(change.port)] = change.connected;
                info = device.single;
            }
            info.id = PortId(info.id, change.port);
            info.name += " (port " + std::to_string(change.port + 1) + ")";
            if (change.connected)
                _sink->OnDeviceAdded(info);
            else
                _sink->OnDeviceRemoved(info.id);
        }
        for (const auto& [port, control] : controls)
            _sink->OnControl({PortId(device.reported, port), control.first, control.second, timeNs});
    }

    /// A Steam dongle's slot: reported when a pad comes to it, removed when it goes (the dongle stays)
    void SteamSlot(Device& device, SteamPad::Event event)
    {
        if (!SteamPad::IsDongle(device.steam->Model()))
            return;
        const bool there = event == SteamPad::Event::Connected || event == SteamPad::Event::State;
        const bool gone = event == SteamPad::Event::Disconnected;
        if ((there && !device.slotPending) || (gone && device.slotPending) || (!there && !gone))
            return;
        HostDeviceInfo info;
        {
            std::lock_guard lock(_mutex);
            device.slotPending = gone;
            info = device.single;
        }
        if (gone)
            _sink->OnDeviceRemoved(info.id);
        else
            _sink->OnDeviceAdded(info);
    }

    void ReportPairEvents(const PairEvents& events)
    {
        for (const HostDeviceId& id : events.removed)
            _sink->OnDeviceRemoved(id);
        for (const HostDeviceInfo& info : events.added)
            _sink->OnDeviceAdded(info);
    }
    /// endregion </Joy-Con pairs>

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
        const DeviceMatch match = DeviceDatabase::Active()->Lookup(identity);
        // Apple's synthetic legacy pad mirrors a GIP pad we read directly (research-host-apis.md §2.1)
        if (match.ignore)
            return;
        // A Valve device: a Steam Controller's pad is its vendor-page interface; a game pad interface (the Steam Deck,
        // the virtual pads) as any; the lizard-mode keyboard and mouse, and the rest, are not pads
        const std::optional<SteamModel> steamModel = SteamPad::ModelOf(vendor, product);
        bool steamVendorPage = false;
        if (vendor == SteamPad::kValve)
        {
            const int page = IntProperty(ref, CFSTR(kIOHIDPrimaryUsagePageKey));
            const int usage = IntProperty(ref, CFSTR(kIOHIDPrimaryUsageKey));
            steamVendorPage = steamModel.has_value() && page >= 0xFF00;
            const bool gamePad = page == 0x01 && (usage == 0x04 || usage == 0x05 || usage == 0x08);
            if (!steamVendorPage && !gamePad)
                return;
        }

        auto device = std::make_unique<Device>();
        device->owner = this;
        device->ref = ref;
        // A USB GIP pad gets no rumble on macOS: its dext takes no rumble report, and GameController accepts the
        // haptics of a pad over USB but drives no motor (haptics.md §9, SDL issue #14800)
        constexpr uint16_t kMicrosoft = 0x045E;
        device->xboxBluetooth = XboxBluetoothOutput::Handles(bluetooth, vendor, match.layout, match.family);
        device->xboxUsb = !bluetooth && vendor == kMicrosoft && match.layout == "xbox";
        if (device->xboxBluetooth)
            device->nativeActuators = kRumbleLow | kRumbleHigh | kRumbleLeftTrigger | kRumbleRightTrigger;
        // A PlayStation pad and the Switch Pro controller: their own full reports and protocols (haptics.md §9)
        const std::optional<SonyModel> sony = SonyPad::ModelOf(vendor, product);
        const std::optional<SwitchLayout> switchLayout = SwitchProPad::LayoutFor(vendor, product, identity.name);
        const bool grip = SwitchProPad::IsChargingGrip(vendor, product);
        const bool switchPro = switchLayout.has_value() || grip;
        auto rumbleUnavailable = [&]() -> std::string {
            if (steamVendorPage)
                return "The emulator drives no Steam Controller haptics yet";
            if (GameCubeAdapter::Handles(vendor, product))
                return "The emulator drives no GameCube adapter rumble yet";
            if (switchLayout && !SwitchProPad::HasRumble(*switchLayout))
                return "This Nintendo Switch Online pad has no motors";
            if (device->xboxBluetooth || sony || switchPro)
                return {};
            if (vendor == kMicrosoft && match.family == "xbox-360")
                return "Xbox 360 pads do not rumble on macOS yet";
            if (vendor == kMicrosoft && match.layout == "xbox")
                return "macOS drives no motors of an Xbox pad over USB: connect the pad over Bluetooth";
            return "On macOS rumble works for Xbox pads over Bluetooth, the DualSense, the DualShock 4, the Switch "
                   "Pro controller and the Joy-Cons; this device has no motors the emulator can drive";
        };
        device->rumbleUnavailable = rumbleUnavailable();
        device->name = identity.name;
        const int maxInput = IntProperty(ref, CFSTR(kIOHIDMaxInputReportSizeKey));
        const HidLayout layout = HidLayoutFor(match);
        size_t controls = 0;
        if (XboxGipPad::Handles(vendor, product) && maxInput == 18)
        {
            device->gip = std::make_unique<XboxGipPad>();
            controls = XboxGipPad::Controls().size();
        }
        else if (SurfaceDial::Handles(vendor, product))
            device->dial = std::make_unique<SurfaceDial>();
        else if (GameCubeAdapter::Handles(vendor, product))
        {
            // Four controllers on one device: their own ids once one is in a port (OnGameCubeReport)
            device->gameCube = std::make_unique<GameCubeAdapter>();
            controls = GameCubeAdapter::Controls().size();
            device->pump = MakePump(ref, std::make_unique<GameCubeProtocol>());
            device->slotPending = true;
        }
        else if (steamVendorPage)
        {
            // Its own reports (no usable descriptor): out of lizard mode first (SteamProtocol)
            device->steam = std::make_unique<SteamPad>(*steamModel);
            controls = SteamPad::Controls(*steamModel).size();
            device->pump = MakePump(ref, std::make_unique<SteamProtocol>(*steamModel));
            device->slotPending = SteamPad::IsDongle(*steamModel);
        }
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
                // A pad with its own driver reads its full report even when the descriptor declares it vendor data
                if (device->hid->Empty() && !sony && !switchPro)
                    return;
                device->hasReportIds = descriptor.hasReportIds;
                if (device->hid->Empty())
                    device->hid.reset();
                else
                    controls = device->hid->Controls().size();
                // A DualShock 3 on the cable sends no input report until feature report 0xF4 enables them (what a
                // PS3 sends; SDL's PS3 driver does the same on macOS). Over Bluetooth it reports at once
                constexpr uint16_t kSony = 0x054C;
                constexpr uint16_t kDualShock3 = 0x0268;
                if (vendor == kSony && product == kDualShock3 && !bluetooth)
                {
                    const uint8_t enable[] = {0xF4, 0x42, 0x0C, 0x00, 0x00};
                    IOHIDDeviceSetReport(ref, kIOHIDReportTypeFeature, enable[0], enable, sizeof(enable));
                }
            }
        }
        device->controls = controls;
        if (device->dial)
        {
            device->rumbleUnavailable.clear();  // no motors, but its clicks play (PlayClicks)
            device->writer = MakeDialWriter(ref);
        }
        if (sony && !device->dial && !device->gip)
        {
            device->sony = std::make_unique<SonyPad>(*sony);
            controls = std::max(controls, SonyPad::Controls(*sony).size());
            device->pump = MakePump(ref, std::make_unique<SonyProtocol>(*sony, bluetooth));
            device->pumpLight = true;
            device->nativeActuators = *sony == SonyModel::DualShock4
                                          ? static_cast<uint8_t>(kRumbleLow | kRumbleHigh)
                                          : static_cast<uint8_t>(kRumbleLow | kRumbleHigh | kRumbleLeftTrigger |
                                                                 kRumbleRightTrigger);
        }
        else if (switchPro && !device->dial && !device->gip)
        {
            const SwitchLayout layout = switchLayout.value_or(SwitchLayout::Pro);
            device->switchPro = std::make_unique<SwitchProPad>(layout);
            controls = std::max(controls, SwitchProPad::Controls(layout).size());
            if (SwitchProPad::IsJoyCon(layout))
                device->joyCon = layout == SwitchLayout::JoyConLeft ? 1 : 2;
            // A half of the charging grip: its side from the device info, then as a Joy-Con of that side
            SwitchProtocol::TypeSink onType;
            if (grip)
            {
                device->gripPending = true;
                std::shared_ptr<std::atomic<int>> type = device->gripType;
                onType = [type](uint8_t value) { type->store(value); };
            }
            // The calibration comes with a subcommand reply, on the io thread (PadPump::Input): the decoder is its
            SwitchProPad* decoder = device->switchPro.get();
            device->pump = MakePump(
                ref, std::make_unique<SwitchProtocol>(
                         bluetooth,
                         [decoder](const SwitchStickCalibration& calibration) { decoder->SetCalibration(calibration); },
                         SwitchProtocol::kStartDelayMs, std::move(onType)));
            if (SwitchProPad::HasRumble(layout))
                device->nativeActuators = kRumbleLow | kRumbleHigh;
        }

        // architecture.md §5: usb:<vid>:<pid>:<version>:<serial>, usb:...@<path hash>, bt:<vid>:<pid>:<hash>
        const std::string serial = StringProperty(ref, CFSTR(kIOHIDSerialNumberKey));
        const std::string location = std::to_string(IntProperty(ref, CFSTR(kIOHIDLocationIDKey)));
        // Two interfaces of one known pad are one device (EndpointMerge). A generic device never merges: a two-player
        // encoder shows two pads on one USB port. The Dial has one endpoint
        // The grip's halves share its serial: never one device
        // A dongle's pads share its serial too
        const bool mergeable =
            match.family != "generic" && match.player && !device->dial && !grip && !device->slotPending;
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
        info.name = device->joyCon != 0 && !identity.name.empty() ? identity.name : match.name;  // "Joy-Con (L)"
        info.family = match.family;
        info.layout = match.layout;
        info.backend = Name();
        Device* raw = device.get();
        bool newDevice = true;
        std::vector<InputSwitch> switches;
        PairEvents pairEvents;
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
            device->reported = added.device;
            device->buffer.resize(static_cast<size_t>(maxInput > 0 ? maxInput : 64));
            info.id = added.device;
            // The player LEDs: the lowest number no other pump pad shows
            if (device->pump && added.newDevice)
            {
                int player = 1;
                while (std::any_of(_devices.begin(), _devices.end(),
                                   [player](const auto& entry) { return entry.second->player == player; }))
                    ++player;
                device->player = player;
                device->pump->SetPlayer(player);
            }
            // A Dial that reconnects gets the steps it had (architecture.md §9: the driver applies them again)
            if (device->writer)
                if (const auto steps = _dialSteps.find(info.id); steps != _dialSteps.end())
                    device->writer->Steps(steps->second);
            const bool joyCon = device->joyCon != 0;
            device->single = info;
            device->shown = joyCon && newDevice;
            if (device->gripPending || device->slotPending)
                newDevice = false;  // shown once its side is known (ResolveGrip), once a pad is there (SteamSlot)
            _devices[ref] = std::move(device);
            UpdateInputsLocked(info.id);
            SyncHeldLocked(switches);
            if (joyCon && newDevice)
            {
                // With a Joy-Con of the other side that waits: one pad (its own device never shows)
                const JoyConPairing::Change change = _joyCons.Add(info.id, raw->joyCon == 1);
                if (change.formed)
                {
                    raw->shown = false;
                    newDevice = false;
                }
                ApplyJoyConChangeLocked(change, {}, pairEvents);
            }
        }
        // A second endpoint of a device already reported adds nothing the sink sees
        if (newDevice)
            _sink->OnDeviceAdded(info);
        ReportPairEvents(pairEvents);
        SwitchInputs(switches);  // a `gc:` device of its name stops giving input
        // With the report's time stamp: the encoder's speed counts from when the report came, not when it ran
        IOHIDDeviceRegisterInputReportWithTimeStampCallback(ref, raw->buffer.data(),
                                                            static_cast<CFIndex>(raw->buffer.size()),
                                                            &IoHidBackend::Report, raw);
    }

    void OnRemoved(IOHIDDeviceRef ref)
    {
        std::unique_ptr<Device> device;
        EndpointMerge::HidRemoved removed;
        std::vector<HostDeviceId> portsGone;  ///< a GameCube adapter's ports with a controller
        std::vector<InputSwitch> switches;
        PairEvents pairEvents;
        {
            std::lock_guard lock(_mutex);
            auto it = _devices.find(ref);
            if (it == _devices.end())
                return;
            device = std::move(it->second);
            _devices.erase(it);
            removed = _merge.RemoveHid(device->key, SteadyNowMs());
            if (!removed.deviceGone)
                UpdateInputsLocked(removed.device);  // another endpoint of the pad gives its input now
            SyncHeldLocked(switches);
            if (device->gripPending || device->slotPending)
                removed.deviceGone = false;  // never reported
            for (int port = 0; port < GameCubeAdapter::kPorts; port++)
                if (device->portShown[static_cast<size_t>(port)])
                    portsGone.push_back(PortId(device->reported, port));
            if (device->joyCon != 0 && removed.deviceGone)
            {
                const JoyConPairing::Change change = _joyCons.Remove(device->id);
                if (!device->shown)
                    removed.deviceGone = false;  // its own device was never shown: the pair's removal says it
                ApplyJoyConChangeLocked(change, device->id, pairEvents);
            }
        }
        SwitchInputs(switches);  // a `gc:` device it held back gives its input again
        IOHIDDeviceRegisterInputReportWithTimeStampCallback(ref, device->buffer.data(),
                                                            static_cast<CFIndex>(device->buffer.size()), nullptr,
                                                            nullptr);
        if (removed.deviceGone)
            _sink->OnDeviceRemoved(device->id);
        for (const HostDeviceId& port : portsGone)
            _sink->OnDeviceRemoved(port);
        ReportPairEvents(pairEvents);
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
        if (device.tap.Armed())  // one atomic load while the test panel does not read
            device.tap.Record(report, size, SteadyNowMs());
        const auto sink = [this, &device, timeNs](const std::string& control, float value) {
            // A press for the GameController match, in the time it reached us (as the companion's presses)
            if (value > 0.5f && IsMatchButton(control))
            {
                std::vector<Promotion> promotions;
                {
                    std::lock_guard lock(_mutex);
                    PromoteLocked(_merge.HidEdge(device.id, SteadyNowMs()), promotions);
                }
                Promote(promotions);
            }
            _sink->OnControl({device.reported, control, value, timeNs});
        };
        // The protocol sees every report first (subcommand replies, acknowledgements)
        if (device.pump)
            device.pump->Input(report, size);
        if (device.gameCube)
        {
            OnGameCubeReport(device, report, size, timeNs);
            return;
        }
        if (device.steam)
        {
            const SteamPad::Event event = device.steam->Decode(report, size, device.slotPending ? ControlSink{} : sink);
            if (event == SteamPad::Event::Disconnected)
                device.steam->Release(device.slotPending ? ControlSink{} : sink);
            SteamSlot(device, event);
            return;
        }
        if (device.gripPending)
        {
            ResolveGrip(device);
            if (device.gripPending)
                return;  // which half it is decides what its buttons are
        }
        if (device.sony || device.switchPro)
        {
            const bool full = device.sony ? device.sony->Decode(report, size, sink)
                                          : device.switchPro->Decode(report, size, sink);
            if (full)
            {
                const BatteryState battery = device.sony ? device.sony->Battery() : device.switchPro->Battery();
                if (!(battery == device.lastBattery))
                {
                    device.lastBattery = battery;
                    std::lock_guard lock(_mutex);
                    device.battery = battery;
                }
                return;
            }
        }
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
    std::map<HostDeviceId, std::string> _gcKeys;             ///< under _mutex: `gc:` devices -> their companion
    std::map<std::string, HostDeviceId> _gcIds;              ///< under _mutex: the other way
    std::set<std::string> _gcHeld;  ///< under _mutex: `gc:` companions whose input is off (EndpointMerge::IsShadowed)
    JoyConPairing _joyCons;         ///< under _mutex: a left and a right Joy-Con are one pad
};
}  // namespace

std::unique_ptr<IBackend> CreateIoHidBackend()
{
    return std::make_unique<IoHidBackend>();
}

}  // namespace controllers
