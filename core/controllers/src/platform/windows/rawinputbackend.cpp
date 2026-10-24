#include "platform/windows/rawinputbackend.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dbt.h>
#include <hidsdi.h>
#include <setupapi.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "devicedatabase.h"
#include "hid/drivers/dualsenseoutput.h"
#include "hid/drivers/surfacedial.h"
#include "hid/hiddescriptor.h"
#include "hid/hidpad.h"
#include "platform/windows/winthread.h"
#include "platform/windowsdevices.h"

namespace controllers
{

namespace
{
constexpr wchar_t kWindowClass[] = L"unrealng-controllers-rawinput";
constexpr DWORD kDialTickMs = 100;  ///< the inferred touch's end (DialTouch), as the IOHID backend's run loop

std::string Utf8(const std::wstring& text)
{
    if (text.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    std::string out(static_cast<size_t>(size > 0 ? size : 0), '\0');
    if (size > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr,
                            nullptr);
    return out;
}

std::wstring Folded(const std::wstring& text)
{
    std::wstring out = text;
    std::transform(out.begin(), out.end(), out.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return out;
}

/// A HidD string (product, serial); empty when the device has none
template <typename Get>
std::string HidString(HANDLE device, Get get)
{
    wchar_t buffer[128] = {};
    if (!get(device, buffer, static_cast<ULONG>(sizeof(buffer) - sizeof(wchar_t))))
        return {};
    return Utf8(buffer);
}

/// A kernel handle closed with its last owner (a Dial's writer thread may outlive the backend's entry)
struct WinHandle
{
    explicit WinHandle(HANDLE value) : handle(value)
    {
    }
    ~WinHandle()
    {
        if (handle != INVALID_HANDLE_VALUE && handle != nullptr)
            CloseHandle(handle);
    }
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;
    bool Valid() const
    {
        return handle != INVALID_HANDLE_VALUE && handle != nullptr;
    }
    HANDLE handle;
};

HANDLE OpenHid(const std::wstring& path, DWORD access, DWORD flags = 0)
{
    return CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, flags,
                       nullptr);
}

/// The input fields of a top-level collection, from its preparsed data (Windows gives no report descriptor): each
/// variable usage is written all zeros and all ones into a report HidP_InitializeReportForID made, and the bits that
/// changed are its place. The reports Windows reads start with the report id (0 without ids), so hasReportIds holds
bool DescriptorFromPreparsed(PHIDP_PREPARSED_DATA preparsed, uint16_t application, HidDescriptor& out,
                             HIDP_CAPS& caps)
{
    if (HidP_GetCaps(preparsed, &caps) != HIDP_STATUS_SUCCESS || caps.InputReportByteLength < 2)
        return false;
    const ULONG length = caps.InputReportByteLength;
    out = {};
    out.hasReportIds = true;
    std::vector<char> cleared(length);
    std::vector<char> set(length);
    auto place = [&](HidField& field, UCHAR reportId) {
        field.reportId = reportId;
        field.application = application;
        uint32_t offset = 0;
        uint8_t size = 0;
        if (!LocateHidBits(reinterpret_cast<const uint8_t*>(cleared.data()),
                           reinterpret_cast<const uint8_t*>(set.data()), length, offset, size))
            return false;
        field.bitOffset = offset;
        field.bitSize = size;
        return true;
    };
    auto fresh = [&](UCHAR reportId) {
        if (HidP_InitializeReportForID(HidP_Input, reportId, preparsed, cleared.data(), length) != HIDP_STATUS_SUCCESS)
            return false;
        set = cleared;
        return true;
    };

    USHORT valueCount = caps.NumberInputValueCaps;
    std::vector<HIDP_VALUE_CAPS> values(valueCount);
    if (valueCount > 0 && HidP_GetValueCaps(HidP_Input, values.data(), &valueCount, preparsed) == HIDP_STATUS_SUCCESS)
    {
        for (USHORT i = 0; i < valueCount; ++i)
        {
            const HIDP_VALUE_CAPS& value = values[i];
            // Variable main items only (bit 1 of the item data); an array of values is not a pad control
            if ((value.BitField & 0x02) == 0 || value.BitSize == 0 || value.BitSize > 32)
                continue;
            const uint32_t first = value.IsRange ? value.Range.UsageMin : value.NotRange.Usage;
            const uint32_t last = value.IsRange ? value.Range.UsageMax : value.NotRange.Usage;
            const ULONG ones = value.BitSize == 32 ? 0xFFFFFFFFu : (1u << value.BitSize) - 1;
            for (uint32_t usage = first; usage <= last; ++usage)
            {
                if (!fresh(value.ReportID))
                    break;
                if (HidP_SetUsageValue(HidP_Input, value.UsagePage, value.LinkCollection, static_cast<USAGE>(usage), 0,
                                       preparsed, cleared.data(), length) != HIDP_STATUS_SUCCESS ||
                    HidP_SetUsageValue(HidP_Input, value.UsagePage, value.LinkCollection, static_cast<USAGE>(usage),
                                       ones, preparsed, set.data(), length) != HIDP_STATUS_SUCCESS)
                    continue;
                HidField field;
                if (!place(field, value.ReportID))
                    continue;
                field.usagePage = value.UsagePage;
                field.usage = static_cast<uint16_t>(usage);
                field.logicalMin = value.LogicalMin;
                field.logicalMax = WinLogicalMax(value.LogicalMin, value.LogicalMax, field.bitSize);
                field.nullState = value.HasNull != 0;
                out.fields.push_back(field);
            }
        }
    }

    USHORT buttonCount = caps.NumberInputButtonCaps;
    std::vector<HIDP_BUTTON_CAPS> buttons(buttonCount);
    if (buttonCount > 0 &&
        HidP_GetButtonCaps(HidP_Input, buttons.data(), &buttonCount, preparsed) == HIDP_STATUS_SUCCESS)
    {
        for (USHORT i = 0; i < buttonCount; ++i)
        {
            const HIDP_BUTTON_CAPS& button = buttons[i];
            if ((button.BitField & 0x02) == 0)
                continue;  // an array (a keyboard's keys): HidPad reads variable buttons only
            const uint32_t first = button.IsRange ? button.Range.UsageMin : button.NotRange.Usage;
            const uint32_t last = button.IsRange ? button.Range.UsageMax : button.NotRange.Usage;
            for (uint32_t usage = first; usage <= last; ++usage)
            {
                if (!fresh(button.ReportID))
                    break;
                USAGE pressed = static_cast<USAGE>(usage);
                ULONG count = 1;
                if (HidP_SetUsages(HidP_Input, button.UsagePage, button.LinkCollection, &pressed, &count, preparsed,
                                   set.data(), length) != HIDP_STATUS_SUCCESS)
                    continue;
                HidField field;
                if (!place(field, button.ReportID) || field.bitSize != 1)
                    continue;
                field.usagePage = button.UsagePage;
                field.usage = static_cast<uint16_t>(usage);
                field.logicalMin = 0;
                field.logicalMax = 1;
                out.fields.push_back(field);
            }
        }
    }
    return !out.fields.empty();
}

class RawInputBackend : public IBackend
{
public:
    ~RawInputBackend() override
    {
        Stop();
    }

    std::string Name() const override
    {
        return "rawinput";
    }

    bool Start(IBackendSink& sink, std::string& reason) override
    {
        if (_thread.joinable())
            return true;
        _sink = &sink;
        _stopping = false;
        _stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (_stopEvent == nullptr)
        {
            reason = "CreateEvent failed";
            _sink = nullptr;
            return false;
        }
        std::promise<std::string> started;
        std::future<std::string> result = started.get_future();
        _thread = std::thread([this, &started] { Run(started); });
        reason = result.get();
        if (!reason.empty())
        {
            _thread.join();
            CloseHandle(_stopEvent);
            _stopEvent = nullptr;
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
        SetEvent(_stopEvent);
        _thread.join();
        CloseHandle(_stopEvent);
        _stopEvent = nullptr;
        _sink = nullptr;
    }

    size_t DeviceCount() const override
    {
        std::lock_guard lock(_mutex);
        return _pads.size() + _dials.size();
    }

    /// region <Haptics: a DualSense over USB, the Dial's clicks>
    bool HasRumble(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        const Pad* pad = PadLocked(device);
        return pad != nullptr && pad->dualSenseUsb;
    }

    uint8_t RumbleActuators(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        if (const Pad* pad = PadLocked(device); pad != nullptr && pad->dualSenseUsb)
            return kRumbleLow | kRumbleHigh | kRumbleLeftTrigger | kRumbleRightTrigger;
        return WriterLocked(device) != nullptr ? static_cast<uint8_t>(kHapticClicker) : 0;
    }

    std::string RumbleUnavailable(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        if (const Pad* pad = PadLocked(device))
            return pad->rumbleUnavailable;
        if (WriterLocked(device) != nullptr)
            return {};  // no motors, but its clicks play (PlayClicks)
        return "The device is not connected";
    }

    bool SetRumble(const HostDeviceId& device, const RumbleLevels& levels) override
    {
        std::lock_guard lock(_mutex);
        Pad* pad = PadLocked(device);
        if (pad == nullptr || !pad->dualSenseUsb)
            return false;
        if (!pad->writer)
            pad->writer = std::make_shared<WinHandle>(OpenHid(pad->path, GENERIC_READ | GENERIC_WRITE));
        if (!pad->writer->Valid())
            return false;
        // The triggers' levels become their resistance (haptics.md §6.4); zero turns it off
        const std::vector<uint8_t> report = PadHidReport(DualSenseOutput::UsbReport(levels), pad->outputLength);
        DWORD written = 0;
        return WriteFile(pad->writer->handle, report.data(), static_cast<DWORD>(report.size()), &written, nullptr) &&
               written == report.size();
    }

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
    /// endregion </Haptics>

    /// region <Light: a DualSense over USB, through the output report its rumble writes>
    bool HasLight(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        const Pad* pad = PadLocked(device);
        return pad != nullptr && pad->dualSenseUsb;
    }

    std::string LightUnavailable(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        const Pad* pad = PadLocked(device);
        if (pad == nullptr)
            return WriterLocked(device) != nullptr ? "The Surface Dial has no light" : "The device is not connected";
        return pad->dualSenseUsb ? std::string()
                                 : "On Windows the emulator sets the light of a DualSense over the USB cable only";
    }

    bool SetLight(const HostDeviceId& device, const LightColor& color) override
    {
        std::lock_guard lock(_mutex);
        Pad* pad = PadLocked(device);
        if (pad == nullptr || !pad->dualSenseUsb)
            return false;
        if (!pad->writer)
            pad->writer = std::make_shared<WinHandle>(OpenHid(pad->path, GENERIC_READ | GENERIC_WRITE));
        if (!pad->writer->Valid())
            return false;
        const std::vector<uint8_t> report =
            PadHidReport(DualSenseOutput::UsbLightReport(color, !pad->lightReleased), pad->outputLength);
        DWORD written = 0;
        const bool ok =
            WriteFile(pad->writer->handle, report.data(), static_cast<DWORD>(report.size()), &written, nullptr) &&
            written == report.size();
        pad->lightReleased = pad->lightReleased || ok;
        return ok;
    }
    /// endregion </Light>

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

private:
    struct Pad
    {
        HostDeviceId id;
        std::wstring path;
        std::unique_ptr<HidPad> hid;
        bool dualSenseUsb = false;
        size_t outputLength = 0;
        std::shared_ptr<WinHandle> writer;  ///< opened at the first rumble or light
        bool lightReleased = false;         ///< dualSenseUsb: the firmware's light bar was released (the first light)
        std::string rumbleUnavailable;
    };
    struct Dial
    {
        HostDeviceId id;
        std::wstring path;  ///< case-folded: the removal notice may spell it otherwise
        std::unique_ptr<SurfaceDial> dial;
        std::unique_ptr<SurfaceDialWriter> writer;
        HANDLE read = INVALID_HANDLE_VALUE;  ///< overlapped
        HANDLE event = nullptr;
        OVERLAPPED overlapped{};
        std::vector<uint8_t> buffer;
        bool pending = false;
    };

    /// region <The backend thread>
    void Run(std::promise<std::string>& started)
    {
        NameThisThread(L"controllers-rawinput");
        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &RawInputBackend::WindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = kWindowClass;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            started.set_value("RegisterClassEx: " + std::to_string(GetLastError()));
            return;
        }
        _window = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
        if (_window == nullptr)
        {
            started.set_value("CreateWindowEx (message-only): " + std::to_string(GetLastError()));
            return;
        }
        SetWindowLongPtrW(_window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        // Joystick, game pad, multi-axis controller: in the background too, with arrivals and removals
        RAWINPUTDEVICE devices[3] = {};
        const USHORT usages[3] = {0x04, 0x05, 0x08};
        for (int i = 0; i < 3; ++i)
            devices[i] = {0x01, usages[i], RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, _window};
        if (!RegisterRawInputDevices(devices, 3, sizeof(RAWINPUTDEVICE)))
        {
            const DWORD error = GetLastError();
            DestroyWindow(_window);
            _window = nullptr;
            started.set_value("RegisterRawInputDevices: " + std::to_string(error));
            return;
        }
        // The Dial's collection is not one Raw Input is registered for: HID interface arrivals tell about it
        GUID hidGuid;
        HidD_GetHidGuid(&hidGuid);
        DEV_BROADCAST_DEVICEINTERFACE_W filter{};
        filter.dbcc_size = sizeof(filter);
        filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        filter.dbcc_classguid = hidGuid;
        HDEVNOTIFY notify = RegisterDeviceNotificationW(_window, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
        started.set_value({});

        EnumeratePads();
        EnumerateDials(hidGuid);
        Loop();

        if (notify != nullptr)
            UnregisterDeviceNotification(notify);
        for (RAWINPUTDEVICE& device : devices)
        {
            device.dwFlags = RIDEV_REMOVE;
            device.hwndTarget = nullptr;
        }
        RegisterRawInputDevices(devices, 3, sizeof(RAWINPUTDEVICE));
        DestroyWindow(_window);
        _window = nullptr;

        std::vector<HostDeviceId> gone;
        std::vector<std::unique_ptr<Dial>> dials;
        {
            std::lock_guard lock(_mutex);
            for (const auto& [raw, pad] : _pads)
                gone.push_back(pad->id);
            for (const auto& dial : _dials)
                gone.push_back(dial->id);
            _pads.clear();
            dials.swap(_dials);
        }
        _ignored.clear();
        for (auto& dial : dials)
            CloseDial(*dial);
        for (const HostDeviceId& id : gone)
            _sink->OnDeviceRemoved(id);
    }

    void Loop()
    {
        uint64_t lastTick = DialNowMs();
        while (!_stopping)
        {
            // The stop event, each Dial's read, then the window's messages (raw input, arrivals)
            std::vector<HANDLE> waits{_stopEvent};
            std::vector<Dial*> reading;
            {
                std::lock_guard lock(_mutex);
                for (const auto& dial : _dials)
                    if (dial->pending && waits.size() < MAXIMUM_WAIT_OBJECTS - 1)
                    {
                        waits.push_back(dial->event);
                        reading.push_back(dial.get());  // only this thread erases
                    }
            }
            const DWORD timeout = _dials.empty() ? INFINITE : kDialTickMs;
            const DWORD count = static_cast<DWORD>(waits.size());
            const DWORD woke = MsgWaitForMultipleObjectsEx(count, waits.data(), timeout, QS_ALLINPUT,
                                                           MWMO_INPUTAVAILABLE);
            if (woke == WAIT_OBJECT_0 || woke == WAIT_FAILED)
                break;
            if (woke > WAIT_OBJECT_0 && woke < WAIT_OBJECT_0 + count)
                OnDialRead(*reading[woke - WAIT_OBJECT_0 - 1]);
            else if (woke == WAIT_OBJECT_0 + count)
            {
                MSG message;
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                    DispatchMessageW(&message);
            }
            const uint64_t now = DialNowMs();
            if (now - lastTick >= kDialTickMs)
            {
                lastTick = now;
                ExpireDials(now);
            }
        }
    }

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* self = reinterpret_cast<RawInputBackend*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self != nullptr)
        {
            switch (message)
            {
                case WM_INPUT: self->OnInput(reinterpret_cast<HRAWINPUT>(lParam)); break;  // DefWindowProc cleans up
                case WM_INPUT_DEVICE_CHANGE:
                    if (wParam == GIDC_ARRIVAL)
                        self->AddPad(reinterpret_cast<HANDLE>(lParam));
                    else if (wParam == GIDC_REMOVAL)
                        self->RemovePad(reinterpret_cast<HANDLE>(lParam));
                    return 0;
                case WM_DEVICECHANGE: self->OnDeviceChange(wParam, lParam); return TRUE;
                default: break;
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    /// endregion </The backend thread>

    /// region <Pads (Raw Input)>
    void EnumeratePads()
    {
        UINT count = 0;
        if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1) || count == 0)
            return;
        std::vector<RAWINPUTDEVICELIST> list(count);
        const UINT got = GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST));
        if (got == static_cast<UINT>(-1))
            return;
        for (UINT i = 0; i < got; ++i)
            if (list[i].dwType == RIM_TYPEHID)
                AddPad(list[i].hDevice);
    }

    static std::wstring RawDeviceName(HANDLE raw)
    {
        UINT size = 0;
        if (GetRawInputDeviceInfoW(raw, RIDI_DEVICENAME, nullptr, &size) != 0 || size == 0)
            return {};
        std::wstring name(size, L'\0');
        if (GetRawInputDeviceInfoW(raw, RIDI_DEVICENAME, name.data(), &size) == static_cast<UINT>(-1))
            return {};
        name.resize(wcsnlen(name.c_str(), name.size()));
        return name;
    }

    /// Idempotent: the arrivals of the devices already present come again after the registration
    void AddPad(HANDLE raw)
    {
        {
            std::lock_guard lock(_mutex);
            if (_pads.count(raw) != 0)
                return;
        }
        if (_ignored.count(raw) != 0)
            return;
        _ignored.insert(raw);  // until it turns out to be a pad
        RID_DEVICE_INFO info{};
        info.cbSize = sizeof(info);
        UINT size = sizeof(info);
        if (GetRawInputDeviceInfoW(raw, RIDI_DEVICEINFO, &info, &size) == static_cast<UINT>(-1) ||
            info.dwType != RIM_TYPEHID)
            return;
        const std::wstring path = RawDeviceName(raw);
        const std::string path8 = Utf8(path);
        if (WinRawInputRole(info.hid.usUsagePage, info.hid.usUsage, path8) != WinHidRole::Pad)
            return;
        UINT preparsedSize = 0;
        if (GetRawInputDeviceInfoW(raw, RIDI_PREPARSEDDATA, nullptr, &preparsedSize) != 0 || preparsedSize == 0)
            return;
        std::vector<uint64_t> preparsed((preparsedSize + 7) / 8);  // aligned for the HidP_* reads
        if (GetRawInputDeviceInfoW(raw, RIDI_PREPARSEDDATA, preparsed.data(), &preparsedSize) == static_cast<UINT>(-1))
            return;
        HidDescriptor descriptor;
        HIDP_CAPS caps{};
        if (!DescriptorFromPreparsed(reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsed.data()), info.hid.usUsage,
                                     descriptor, caps))
            return;

        const auto vendor = static_cast<uint16_t>(info.hid.dwVendorId);
        const auto product = static_cast<uint16_t>(info.hid.dwProductId);
        const auto version = static_cast<uint16_t>(info.hid.dwVersionNumber);
        const WinHidPath where = ParseWinHidPath(path8);
        std::string name;
        std::string serial;
        {
            // No access needed for the strings; the pad is not opened for its reports (Raw Input brings them)
            WinHandle query(OpenHid(path, 0));
            if (query.Valid())
            {
                name = HidString(query.handle, HidD_GetProductString);
                serial = HidString(query.handle, HidD_GetSerialNumberString);
            }
        }
        DeviceIdentity identity;
        identity.bus = where.bus;
        identity.vendor = vendor;
        identity.product = product;
        identity.version = version;
        identity.name = name;
        identity.backend = Name();
        const DeviceMatch match = DeviceDatabase::Builtin().Lookup(identity);
        if (match.ignore)
            return;

        auto pad = std::make_unique<Pad>();
        pad->path = path;
        pad->hid = std::make_unique<HidPad>(descriptor, HidLayoutFor(match.layout));
        if (pad->hid->Empty())
            return;
        pad->dualSenseUsb = where.bus == "usb" && DualSenseOutput::Handles(vendor, product);
        pad->outputLength = caps.OutputReportByteLength;
        if (DualSenseOutput::Handles(vendor, product) && !pad->dualSenseUsb)
            pad->rumbleUnavailable = "A DualSense rumbles over the USB cable only for now";
        else if (!pad->dualSenseUsb)
            pad->rumbleUnavailable = "On Windows rumble works for Xbox pads (GameInput, XInput) and a DualSense over "
                                     "USB; this device has no motors the emulator can drive";

        const std::string id = WinDeviceId(where.bus, vendor, product, version, serial, path8);
        HostDeviceInfo host;
        host.name = match.name.empty() ? name : match.name;
        host.family = match.family;
        host.layout = match.layout;
        host.backend = Name();
        {
            std::lock_guard lock(_mutex);
            pad->id = UniqueLocked(id);
            host.id = pad->id;
            _pads[raw] = std::move(pad);
        }
        _ignored.erase(raw);
        _sink->OnDeviceAdded(host);
    }

    void RemovePad(HANDLE raw)
    {
        _ignored.erase(raw);
        std::unique_ptr<Pad> pad;
        {
            std::lock_guard lock(_mutex);
            auto it = _pads.find(raw);
            if (it == _pads.end())
                return;
            pad = std::move(it->second);
            _pads.erase(it);
        }
        _sink->OnDeviceRemoved(pad->id);
    }

    void OnInput(HRAWINPUT input)
    {
        UINT size = 0;
        if (GetRawInputData(input, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0)
            return;
        _input.resize((size + 7) / 8);
        if (GetRawInputData(input, RID_INPUT, _input.data(), &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
            return;
        const auto* raw = reinterpret_cast<const RAWINPUT*>(_input.data());
        if (raw->header.dwType != RIM_TYPEHID)
            return;
        Pad* pad = nullptr;
        {
            std::lock_guard lock(_mutex);
            auto it = _pads.find(raw->header.hDevice);
            if (it != _pads.end())
                pad = it->second.get();  // only this thread erases
        }
        if (pad == nullptr)
            return;
        const RAWHID& hid = raw->data.hid;
        const HostDeviceId& id = pad->id;
        const auto sink = [this, &id](const std::string& control, float value) {
            _sink->OnControl({id, control, value, 0});
        };
        for (DWORD i = 0; i < hid.dwCount; ++i)
            pad->hid->Decode(hid.bRawData + static_cast<size_t>(i) * hid.dwSizeHid, hid.dwSizeHid, sink);
    }

    Pad* PadLocked(const HostDeviceId& device) const
    {
        for (const auto& [raw, pad] : _pads)
            if (pad->id == device)
                return pad.get();
        return nullptr;
    }
    /// endregion </Pads>

    /// region <The Surface Dial (HidD_* and overlapped ReadFile)>
    void EnumerateDials(const GUID& hidGuid)
    {
        HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE)
            return;
        SP_DEVICE_INTERFACE_DATA data{};
        data.cbSize = sizeof(data);
        for (DWORD index = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, index, &data); ++index)
        {
            DWORD need = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &data, nullptr, 0, &need, nullptr);
            if (need < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
                continue;
            std::vector<uint64_t> buffer((need + 7) / 8);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (SetupDiGetDeviceInterfaceDetailW(set, &data, detail, need, nullptr, nullptr))
                AddDial(detail->DevicePath);
        }
        SetupDiDestroyDeviceInfoList(set);
    }

    void OnDeviceChange(WPARAM event, LPARAM data)
    {
        const auto* header = reinterpret_cast<const DEV_BROADCAST_HDR*>(data);
        if (header == nullptr || header->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE)
            return;
        const auto* change = reinterpret_cast<const DEV_BROADCAST_DEVICEINTERFACE_W*>(header);
        if (event == DBT_DEVICEARRIVAL)
            AddDial(change->dbcc_name);
        else if (event == DBT_DEVICEREMOVECOMPLETE)
        {
            const std::wstring path = Folded(change->dbcc_name);
            std::unique_ptr<Dial> gone;
            {
                std::lock_guard lock(_mutex);
                auto it = std::find_if(_dials.begin(), _dials.end(),
                                       [&path](const auto& dial) { return dial->path == path; });
                if (it == _dials.end())
                    return;
                gone = std::move(*it);
                _dials.erase(it);
            }
            CloseDial(*gone);
            _sink->OnDeviceRemoved(gone->id);
        }
    }

    void AddDial(const std::wstring& path)
    {
        const std::wstring folded = Folded(path);
        {
            std::lock_guard lock(_mutex);
            if (std::any_of(_dials.begin(), _dials.end(), [&folded](const auto& dial) { return dial->path == folded; }))
                return;
        }
        // Its collection first, without access (any HID interface comes here)
        WinHandle query(OpenHid(path, 0));
        if (!query.Valid())
            return;
        PHIDP_PREPARSED_DATA preparsed = nullptr;
        if (!HidD_GetPreparsedData(query.handle, &preparsed))
            return;
        HIDP_CAPS caps{};
        HidDescriptor descriptor;
        const bool collection = HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS &&
                                WinRawInputRole(caps.UsagePage, caps.Usage, Utf8(path)) == WinHidRole::Dial;
        const bool described = collection && DescriptorFromPreparsed(preparsed, caps.Usage, descriptor, caps);
        HidD_FreePreparsedData(preparsed);
        if (!collection)
            return;
        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (!HidD_GetAttributes(query.handle, &attributes))
            return;

        auto dial = std::make_unique<Dial>();
        if (SurfaceDial::Handles(attributes.VendorID, attributes.ProductID))
            dial->dial = std::make_unique<SurfaceDial>();
        else if (described && SurfaceDial::Matches(descriptor))
            dial->dial = std::make_unique<SurfaceDial>(descriptor);
        else
            return;
        // Shared, not exclusive: Windows keeps its own use of the Dial (RadialController, surface-dial.md §4)
        dial->read = OpenHid(path, GENERIC_READ | GENERIC_WRITE, FILE_FLAG_OVERLAPPED);
        if (dial->read == INVALID_HANDLE_VALUE)
            dial->read = OpenHid(path, GENERIC_READ, FILE_FLAG_OVERLAPPED);
        if (dial->read == INVALID_HANDLE_VALUE)
            return;
        dial->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (dial->event == nullptr)
        {
            CloseHandle(dial->read);
            return;
        }
        dial->path = folded;
        dial->buffer.resize(std::max<size_t>(caps.InputReportByteLength, 2));
        auto writer = std::make_shared<WinHandle>(OpenHid(path, GENERIC_READ | GENERIC_WRITE));
        if (writer->Valid())
        {
            const size_t featureLength = caps.FeatureReportByteLength;
            const size_t outputLength = caps.OutputReportByteLength;
            dial->writer = std::make_unique<SurfaceDialWriter>(
                [writer, featureLength, outputLength](SurfaceDialWriter::ReportType type,
                                                      const std::vector<uint8_t>& report) {
                    const bool feature = type == SurfaceDialWriter::ReportType::Feature;
                    std::vector<uint8_t> padded = PadHidReport(report, feature ? featureLength : outputLength);
                    const auto length = static_cast<ULONG>(padded.size());
                    return feature ? HidD_SetFeature(writer->handle, padded.data(), length) != FALSE
                                   : HidD_SetOutputReport(writer->handle, padded.data(), length) != FALSE;
                },
                [writer, featureLength](std::vector<uint8_t>& report) {
                    // HidD_GetFeature fills the collection's whole feature length, the id first
                    report.assign(std::max<size_t>(featureLength, 8), 0);
                    report[0] = SurfaceDial::kFeatureReport;
                    return HidD_GetFeature(writer->handle, report.data(), static_cast<ULONG>(report.size())) != FALSE;
                });
        }

        const std::string name = HidString(query.handle, HidD_GetProductString);
        const std::string serial = HidString(query.handle, HidD_GetSerialNumberString);
        const WinHidPath where = ParseWinHidPath(Utf8(path));
        DeviceIdentity identity;
        identity.bus = where.bus;
        identity.vendor = attributes.VendorID;
        identity.product = attributes.ProductID;
        identity.version = attributes.VersionNumber;
        identity.name = name;
        identity.backend = Name();
        const DeviceMatch match = DeviceDatabase::Builtin().Lookup(identity);
        const std::string id = WinDeviceId(where.bus, attributes.VendorID, attributes.ProductID,
                                           attributes.VersionNumber, serial, Utf8(path));
        HostDeviceInfo host;
        host.name = match.name.empty() ? name : match.name;
        host.family = match.family;
        host.layout = match.layout;
        host.backend = Name();
        Dial* raw = dial.get();
        {
            std::lock_guard lock(_mutex);
            dial->id = UniqueLocked(id);
            host.id = dial->id;
            // A Dial that reconnects gets the steps it had (control surfaces architecture.md §9)
            if (dial->writer)
                if (const auto steps = _dialSteps.find(dial->id); steps != _dialSteps.end())
                    dial->writer->Steps(steps->second);
            _dials.push_back(std::move(dial));
        }
        _sink->OnDeviceAdded(host);
        if (!IssueRead(*raw))
            DropDial(raw);
    }

    bool IssueRead(Dial& dial)
    {
        ResetEvent(dial.event);
        dial.overlapped = {};
        dial.overlapped.hEvent = dial.event;
        // A read that completes at once signals the event too: the loop decodes every report in one place
        if (ReadFile(dial.read, dial.buffer.data(), static_cast<DWORD>(dial.buffer.size()), nullptr,
                     &dial.overlapped) ||
            GetLastError() == ERROR_IO_PENDING)
        {
            dial.pending = true;
            return true;
        }
        dial.pending = false;
        return false;
    }

    void OnDialRead(Dial& dial)
    {
        DWORD got = 0;
        dial.pending = false;
        if (!GetOverlappedResult(dial.read, &dial.overlapped, &got, FALSE))
        {
            DropDial(&dial);  // gone (asleep for good, unpaired): its removal notice may follow
            return;
        }
        const HostDeviceId& id = dial.id;
        dial.dial->Decode(dial.buffer.data(), got, [this, &id](const std::string& control, float value) {
            _sink->OnControl({id, control, value, 0});
        });
        if (!IssueRead(dial))
            DropDial(&dial);
    }

    void DropDial(Dial* dial)
    {
        std::unique_ptr<Dial> gone;
        {
            std::lock_guard lock(_mutex);
            auto it = std::find_if(_dials.begin(), _dials.end(), [dial](const auto& known) { return known.get() == dial; });
            if (it == _dials.end())
                return;
            gone = std::move(*it);
            _dials.erase(it);
        }
        CloseDial(*gone);
        _sink->OnDeviceRemoved(gone->id);
    }

    /// The read is cancelled and waited for; the writer ends with the entry (a write in flight finishes first)
    static void CloseDial(Dial& dial)
    {
        if (dial.pending)
        {
            DWORD got = 0;
            CancelIoEx(dial.read, &dial.overlapped);
            GetOverlappedResult(dial.read, &dial.overlapped, &got, TRUE);
            dial.pending = false;
        }
        if (dial.read != INVALID_HANDLE_VALUE)
            CloseHandle(dial.read);
        dial.read = INVALID_HANDLE_VALUE;
        if (dial.event != nullptr)
            CloseHandle(dial.event);
        dial.event = nullptr;
        dial.writer.reset();
    }

    void ExpireDials(uint64_t now)
    {
        std::vector<Dial*> dials;
        {
            std::lock_guard lock(_mutex);
            for (const auto& dial : _dials)
                dials.push_back(dial.get());  // only this thread erases
        }
        for (Dial* dial : dials)
        {
            const HostDeviceId& id = dial->id;
            dial->dial->Expire(now, [this, &id](const std::string& control, float value) {
                _sink->OnControl({id, control, value, 0});
            });
        }
    }

    SurfaceDialWriter* WriterLocked(const HostDeviceId& device) const
    {
        for (const auto& dial : _dials)
            if (dial->id == device && dial->writer)
                return dial->writer.get();
        return nullptr;
    }
    /// endregion </The Surface Dial>

    /// @p id, or with `#2`, `#3`... while a device of this backend has it (two pads of one model without serials)
    std::string UniqueLocked(const std::string& id) const
    {
        auto taken = [this](const std::string& candidate) {
            for (const auto& [raw, pad] : _pads)
                if (pad->id == candidate)
                    return true;
            for (const auto& dial : _dials)
                if (dial->id == candidate)
                    return true;
            return false;
        };
        std::string unique = id;
        for (int n = 2; taken(unique); ++n)
            unique = id + "#" + std::to_string(n);
        return unique;
    }

    IBackendSink* _sink = nullptr;
    std::thread _thread;
    std::atomic<bool> _stopping{false};
    HANDLE _stopEvent = nullptr;
    HWND _window = nullptr;
    mutable std::mutex _mutex;
    std::map<HANDLE, std::unique_ptr<Pad>> _pads;  ///< under _mutex; changed on the backend thread only
    std::vector<std::unique_ptr<Dial>> _dials;     ///< the same
    std::map<HostDeviceId, StepsOut> _dialSteps;   ///< under _mutex: each Dial's last steps, kept across a reconnect
    std::set<HANDLE> _ignored;                     ///< backend thread: Raw Input devices that are not pads
    std::vector<uint64_t> _input;                  ///< backend thread: the WM_INPUT buffer, aligned
};
}  // namespace

std::unique_ptr<IBackend> CreateRawInputBackend()
{
    return std::make_unique<RawInputBackend>();
}

}  // namespace controllers
