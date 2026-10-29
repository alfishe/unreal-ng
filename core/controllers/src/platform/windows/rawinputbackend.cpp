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
#include <winioctl.h>

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
#include "hid/drivers/surfacedial.h"
#include "hid/hiddescriptor.h"
#include "hid/hidpad.h"
#include "platform/windows/winthread.h"
#include "platform/windowsdevices.h"
#include "protocolpads.h"

// hidclass.h (the WDK's; MinGW ships it under ddk/): what HidD_GetFeature sends, but with the length that came back
#ifndef IOCTL_HID_GET_FEATURE
#define IOCTL_HID_GET_FEATURE CTL_CODE(FILE_DEVICE_KEYBOARD, 100, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)
#endif
// What HidD_SetFeature sends (hidapi writes it overlapped the same way)
#ifndef IOCTL_HID_SET_FEATURE
#define IOCTL_HID_SET_FEATURE CTL_CODE(FILE_DEVICE_KEYBOARD, 100, METHOD_IN_DIRECT, FILE_ANY_ACCESS)
#endif

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

/// The writes of a pad with a protocol (ProtocolPads), on its pump's thread: output reports through WriteFile (the
/// interrupt OUT pipe over USB, the HID channel over Bluetooth, as hidapi's hid_write) and feature reads through
/// IOCTL_HID_GET_FEATURE. Overlapped with a time limit, so a pad that stops answering never holds the pump (or its
/// final report at the device's removal) for long. One call at a time: the pump's thread, then its destructor after
/// the join
class PadIo
{
public:
    static constexpr DWORD kTimeoutMs = 1000;

    PadIo(HANDLE handle, size_t outputLength, size_t featureLength)
        : _handle(handle), _event(CreateEventW(nullptr, TRUE, FALSE, nullptr)), _outputLength(outputLength),
          _featureLength(featureLength)
    {
    }
    ~PadIo()
    {
        if (_event != nullptr)
            CloseHandle(_event);
    }
    PadIo(const PadIo&) = delete;
    PadIo& operator=(const PadIo&) = delete;

    bool Valid() const
    {
        return _handle.Valid() && _event != nullptr;
    }

    /// Windows takes an output report at the collection's longest output length (hidapi: "Windows expects the number
    /// of bytes which are in the _longest_ report"), zero-padded
    bool Write(const std::vector<uint8_t>& report)
    {
        std::vector<uint8_t> padded = PadHidReport(report, _outputLength);
        DWORD done = 0;
        return Transfer(
                   [&](OVERLAPPED* overlapped) {
                       return WriteFile(_handle.handle, padded.data(), static_cast<DWORD>(padded.size()), nullptr,
                                        overlapped) != FALSE;
                   },
                   done) &&
               done == padded.size();
    }

    /// Windows wants a buffer of the collection's longest feature report and may fill all of it; the protocol knows
    /// its report's own size (@p size), so a longer answer is cut to it
    bool GetFeature(uint8_t id, size_t size, std::vector<uint8_t>& data)
    {
        data.assign(std::max(size, _featureLength), 0);
        data[0] = id;
        const auto length = static_cast<DWORD>(data.size());
        DWORD done = 0;
        if (!Transfer(
                [&](OVERLAPPED* overlapped) {
                    return DeviceIoControl(_handle.handle, IOCTL_HID_GET_FEATURE, data.data(), length, data.data(),
                                           length, nullptr, overlapped) != FALSE;
                },
                done) ||
            done == 0)
            return false;
        data.resize(std::min<size_t>(done, size));
        return data[0] == id;
    }

    /// A feature report (bytes[0] the report id, 0 for a collection without ids), zero-padded to the collection's
    /// longest feature report as HidD_SetFeature wants it
    bool SetFeature(const std::vector<uint8_t>& report)
    {
        std::vector<uint8_t> padded = PadHidReport(report, _featureLength);
        DWORD done = 0;
        return Transfer(
            [&](OVERLAPPED* overlapped) {
                return DeviceIoControl(_handle.handle, IOCTL_HID_SET_FEATURE, padded.data(),
                                       static_cast<DWORD>(padded.size()), nullptr, 0, nullptr, overlapped) != FALSE;
            },
            done);
    }

private:
    template <typename Start>
    bool Transfer(Start start, DWORD& done)
    {
        OVERLAPPED overlapped{};
        overlapped.hEvent = _event;
        ResetEvent(_event);
        if (!start(&overlapped))
        {
            if (GetLastError() != ERROR_IO_PENDING)
                return false;
            if (WaitForSingleObject(_event, kTimeoutMs) != WAIT_OBJECT_0)
            {
                CancelIoEx(_handle.handle, &overlapped);
                GetOverlappedResult(_handle.handle, &overlapped, &done, TRUE);
                return false;
            }
        }
        return GetOverlappedResult(_handle.handle, &overlapped, &done, FALSE) != FALSE;
    }

    WinHandle _handle;
    HANDLE _event;
    size_t _outputLength;
    size_t _featureLength;
};

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
        // Made while the thread does not run (a restart's is empty: Run cleared it); kept until the backend goes, so a
        // late output call finds no device rather than a gone object
        if (!_protocol || _protocolSink != &sink)
        {
            _protocol = std::make_unique<ProtocolPads>(sink);
            _protocolSink = &sink;
        }
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
        return _pads.size() + _dials.size() + (_protocol ? _protocol->DeviceCount() : 0);
    }

    /// region <Haptics: the pads with a protocol (ProtocolPads), the Dial's clicks>
    bool HasRumble(const HostDeviceId& device) const override
    {
        return (RumbleActuators(device) & (kRumbleLow | kRumbleHigh)) != 0;
    }

    uint8_t RumbleActuators(const HostDeviceId& device) const override
    {
        if (ProtocolPads* protocol = ProtocolOf(device))
            return protocol->RumbleActuators(device);
        std::lock_guard lock(_mutex);
        return WriterLocked(device) != nullptr ? static_cast<uint8_t>(kHapticClicker) : 0;
    }

    std::string RumbleUnavailable(const HostDeviceId& device) const override
    {
        if (ProtocolPads* protocol = ProtocolOf(device))
            return protocol->RumbleUnavailable(device);
        std::lock_guard lock(_mutex);
        if (const Pad* pad = PadLocked(device))
            return pad->rumbleUnavailable;
        if (WriterLocked(device) != nullptr)
            return {};  // no motors, but its clicks play (PlayClicks)
        return "The device is not connected";
    }

    /// Queued for the pad's pump, never blocks: a DualSense's trigger levels become their resistance (haptics.md
    /// §6.4), a Switch pad plays the levels' HD frequencies, a Joy-Con pair splits them by side
    bool SetRumble(const HostDeviceId& device, const RumbleLevels& levels) override
    {
        ProtocolPads* protocol = ProtocolOf(device);
        return protocol != nullptr && protocol->SetRumble(device, levels);
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

    std::string DeviceNotice(const HostDeviceId& device) const override
    {
        std::lock_guard lock(_mutex);
        const Pad* pad = PadLocked(device);
        return pad != nullptr && !pad->heard ? pad->notice : std::string();
    }

    /// region <Battery and light: the pads with a protocol, from their own reports and through their pumps>
    /// A DualSense / DualShock 4 from its full report, a Switch pad from its 0x30, a Joy-Con pair the lower half's
    BatteryState Battery(const HostDeviceId& device) const override
    {
        ProtocolPads* protocol = ProtocolOf(device);
        return protocol != nullptr ? protocol->Battery(device) : BatteryState{};
    }

    bool HasLight(const HostDeviceId& device) const override
    {
        ProtocolPads* protocol = ProtocolOf(device);
        return protocol != nullptr && protocol->HasLight(device);
    }

    std::string LightUnavailable(const HostDeviceId& device) const override
    {
        if (ProtocolPads* protocol = ProtocolOf(device))
            return protocol->LightUnavailable(device);
        std::lock_guard lock(_mutex);
        if (PadLocked(device) != nullptr)
            return "This device has no light the emulator can set";
        return WriterLocked(device) != nullptr ? "The Surface Dial has no light" : "The device is not connected";
    }

    /// USB and Bluetooth (the Bluetooth report with its CRC); a DualSense's first colour releases the firmware's light
    bool SetLight(const HostDeviceId& device, const LightColor& color) override
    {
        ProtocolPads* protocol = ProtocolOf(device);
        return protocol != nullptr && protocol->SetLight(device, color);
    }
    /// endregion </Battery and light>

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
        std::string rumbleUnavailable;
        std::string notice;               ///< until its first report (a DualShock 3 without its driver)
        std::atomic<bool> heard{false};  ///< a report came (backend thread; read under _mutex)
    };
    static constexpr UINT kRegistrations = 5;
    /// A device read from its HID interface, not Raw Input: the Surface Dial, or a control surface of the database
    /// (DeviceDatabase::SurfaceProducts) whose collection is not a pad's, decoded by its HID map
    struct Dial
    {
        HostDeviceId id;
        std::wstring path;  ///< case-folded: the removal notice may spell it otherwise
        std::unique_ptr<SurfaceDial> dial;
        std::unique_ptr<HidPad> hid;  ///< a control surface (no dial)
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

        // Joystick, game pad, multi-axis controller: in the background too, with arrivals and removals. Then whole
        // pages for the pads with a protocol whose collection is not one of those: the vendor page (a Steam
        // Controller's pad interface) and Game Controls (the GameCube adapter); AddPad keeps only those of them
        RAWINPUTDEVICE devices[kRegistrations] = {};
        const USHORT usages[3] = {0x04, 0x05, 0x08};
        for (int i = 0; i < 3; ++i)
            devices[i] = {0x01, usages[i], RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, _window};
        devices[3] = {0xFF00, 0, RIDEV_PAGEONLY | RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, _window};
        devices[4] = {0x05, 0, RIDEV_PAGEONLY | RIDEV_INPUTSINK | RIDEV_DEVNOTIFY, _window};
        if (!RegisterRawInputDevices(devices, kRegistrations, sizeof(RAWINPUTDEVICE)))
        {
            const DWORD error = GetLastError();
            DestroyWindow(_window);
            _window = nullptr;
            started.set_value("RegisterRawInputDevices: " + std::to_string(error));
            return;
        }
        // The Dial's collection and the control surfaces' are not ones Raw Input is registered for: HID interface
        // arrivals tell about them
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
            device.dwFlags = RIDEV_REMOVE | (device.usUsage == 0 ? RIDEV_PAGEONLY : 0);
            device.hwndTarget = nullptr;
        }
        RegisterRawInputDevices(devices, kRegistrations, sizeof(RAWINPUTDEVICE));
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
        _protocolKeys.clear();
        _protocol->Clear();  // its pumps stop their motors, its devices are reported gone
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
        const auto vendor = static_cast<uint16_t>(info.hid.dwVendorId);
        const auto product = static_cast<uint16_t>(info.hid.dwProductId);
        // A pad's collection, or a protocol's on another page (the Steam vendor page, the GameCube adapter's); its
        // product string tells the NES pads from right Joy-Cons further down
        const bool pad = WinRawInputRole(info.hid.usUsagePage, info.hid.usUsage, path8) == WinHidRole::Pad;
        if (!pad && !ProtocolPads::TakesCollection(vendor, product, info.hid.usUsagePage))
            return;
        UINT preparsedSize = 0;
        if (GetRawInputDeviceInfoW(raw, RIDI_PREPARSEDDATA, nullptr, &preparsedSize) != 0 || preparsedSize == 0)
            return;
        std::vector<uint64_t> preparsed((preparsedSize + 7) / 8);  // aligned for the HidP_* reads
        if (GetRawInputDeviceInfoW(raw, RIDI_PREPARSEDDATA, preparsed.data(), &preparsedSize) == static_cast<UINT>(-1))
            return;
        HidDescriptor descriptor;
        HIDP_CAPS caps{};
        const bool described = DescriptorFromPreparsed(reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsed.data()),
                                                       info.hid.usUsage, descriptor, caps);
        if (!described && HidP_GetCaps(reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsed.data()), &caps) !=
                              HIDP_STATUS_SUCCESS)
            return;
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
        const DeviceMatch match = DeviceDatabase::Active()->Lookup(identity);
        if (match.ignore)
            return;
        // A pad with a protocol of its own reads its full report even when the collection declares vendor data only
        const bool protocol = ProtocolPads::TakesCollection(vendor, product, info.hid.usUsagePage, name);
        if (!described && !protocol)
            return;

        auto hid = described ? std::make_unique<HidPad>(descriptor, HidLayoutFor(match)) : nullptr;
        if (hid && hid->Empty())
            hid.reset();
        if (!hid && !protocol)
            return;

        const std::string id = WinDeviceId(where.bus, vendor, product, version, serial, path8);
        HostDeviceInfo host;
        // A Joy-Con names its side ("Joy-Con (L)"); the database's name is the family's
        const bool joyCon = SwitchProPad::IsJoyCon(SwitchProPad::LayoutFor(vendor, product, name).value_or(SwitchLayout::Pro));
        host.name = (joyCon && !name.empty()) || match.name.empty() ? name : match.name;
        host.family = match.family;
        host.layout = match.layout;
        host.backend = Name();
        if (protocol)
        {
            AddProtocolPad(raw, path, id, host, vendor, product, where.bus == "bt", name, std::move(hid), caps);
            return;
        }

        auto added = std::make_unique<Pad>();
        added->path = path;
        added->hid = std::move(hid);
        added->rumbleUnavailable = "On Windows rumble works for Xbox pads (GameInput, XInput), the DualSense, the "
                                   "DualShock 4, the Switch Pro controller and the Joy-Cons; this device has no motors "
                                   "the emulator can drive";
        if (vendor == kSony && product == kDualShock3)
            added->notice = EnableDualShock3(path, where.bus == "bt", caps);
        {
            std::lock_guard lock(_mutex);
            added->id = UniqueLocked(id);
            host.id = added->id;
            _pads[raw] = std::move(added);
        }
        _ignored.erase(raw);
        _sink->OnDeviceAdded(host);
    }

    static constexpr uint16_t kSony = 0x054C;
    static constexpr uint16_t kDualShock3 = 0x0268;

    /// A DualShock 3 on the cable sends no input report until feature report 0xF4 enables them (what a PS3 sends; the
    /// IOHID backend does the same). Windows has no driver of its own for it: without DsHidMini (BthPS3 over
    /// Bluetooth) the pad may stay silent. The notice says so until its first report
    static std::string EnableDualShock3(const std::wstring& path, bool bluetooth, const HIDP_CAPS& caps)
    {
        if (!bluetooth && caps.FeatureReportByteLength > 0)
        {
            WinHandle writer(OpenHid(path, GENERIC_READ | GENERIC_WRITE));
            if (writer.Valid())
            {
                std::vector<uint8_t> enable =
                    PadHidReport({0xF4, 0x42, 0x0C, 0x00, 0x00}, caps.FeatureReportByteLength);
                HidD_SetFeature(writer.handle, enable.data(), static_cast<ULONG>(enable.size()));
            }
        }
        return "No input from this DualShock 3 yet: Windows has no driver for it; install DsHidMini (and BthPS3 for "
               "Bluetooth), then connect it again";
    }

    /// A DualSense / DualShock 4 / Switch pad: ProtocolPads decodes it and drives its outputs through a handle opened
    /// for writing (shared: Windows and other programs keep theirs)
    void AddProtocolPad(HANDLE raw, const std::wstring& path, const std::string& id, HostDeviceInfo host,
                        uint16_t vendor, uint16_t product, bool bluetooth, const std::string& productName,
                        std::unique_ptr<HidPad> hid, const HIDP_CAPS& caps)
    {
        ProtocolPads::Endpoint endpoint;
        endpoint.key = std::to_string(reinterpret_cast<uintptr_t>(raw));
        endpoint.vendor = vendor;
        endpoint.product = product;
        endpoint.bluetooth = bluetooth;
        endpoint.productName = productName;
        endpoint.zeroReportIdFirst = true;  // RAWHID: a collection without ids gets report id 0 first
        endpoint.hid = std::move(hid);
        const HANDLE handle = OpenHid(path, GENERIC_READ | GENERIC_WRITE, FILE_FLAG_OVERLAPPED);
        const DWORD openError = handle == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        auto io = std::make_shared<PadIo>(handle, caps.OutputReportByteLength, caps.FeatureReportByteLength);
        if (io->Valid() && caps.OutputReportByteLength > 0)
            endpoint.write = [io](const std::vector<uint8_t>& report) { return io->Write(report); };
        if (io->Valid() && caps.FeatureReportByteLength > 0)
        {
            endpoint.getFeature = [io](uint8_t featureId, size_t size, std::vector<uint8_t>& data) {
                return io->GetFeature(featureId, size, data);
            };
            endpoint.setFeature = [io](const std::vector<uint8_t>& report) { return io->SetFeature(report); };
        }
        if (!io->Valid())
            endpoint.outputsUnavailable = "Windows did not open the pad for writing (" +
                                          std::to_string(openError) +
                                          "): another program may hold it alone (Steam Input, DS4Windows)";
        {
            std::lock_guard lock(_mutex);
            host.id = UniqueLocked(id);
        }
        endpoint.info = host;
        const std::string key = endpoint.key;
        if (!_protocol->Add(std::move(endpoint)))
            return;  // a grip half without writes: its side stays unknown
        _protocolKeys[raw] = key;
        _ignored.erase(raw);
    }

    void RemovePad(HANDLE raw)
    {
        _ignored.erase(raw);
        if (const auto key = _protocolKeys.find(raw); key != _protocolKeys.end())
        {
            _protocol->Remove(key->second);
            _protocolKeys.erase(key);
            return;
        }
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
        if (const auto key = _protocolKeys.find(raw->header.hDevice); key != _protocolKeys.end())
        {
            // Each report padded to the collection's longest input report: ProtocolPads cuts the full ones to size
            const RAWHID& hid = raw->data.hid;
            for (DWORD i = 0; i < hid.dwCount; ++i)
                _protocol->Input(key->second, hid.bRawData + static_cast<size_t>(i) * hid.dwSizeHid, hid.dwSizeHid);
            return;
        }
        Pad* pad = nullptr;
        {
            std::lock_guard lock(_mutex);
            auto it = _pads.find(raw->header.hDevice);
            if (it != _pads.end())
                pad = it->second.get();  // only this thread erases
        }
        if (pad == nullptr)
            return;
        pad->heard = true;
        const RAWHID& hid = raw->data.hid;
        const HostDeviceId& id = pad->id;
        const auto sink = [this, &id](const std::string& control, float value) {
            _sink->OnControl({id, control, value, 0});
        };
        for (DWORD i = 0; i < hid.dwCount; ++i)
            pad->hid->Decode(hid.bRawData + static_cast<size_t>(i) * hid.dwSizeHid, hid.dwSizeHid, sink);
    }

    /// ProtocolPads when it has @p device (it locks on its own)
    ProtocolPads* ProtocolOf(const HostDeviceId& device) const
    {
        return _protocol && _protocol->Has(device) ? _protocol.get() : nullptr;
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
        HIDD_ATTRIBUTES attributes{};
        attributes.Size = sizeof(attributes);
        if (!HidD_GetAttributes(query.handle, &attributes))
            return;
        const std::shared_ptr<const DeviceDatabase> database = DeviceDatabase::Active();
        const auto surfaces = database->SurfaceProducts();
        const bool surface = !SurfaceDial::Handles(attributes.VendorID, attributes.ProductID) &&
                             std::find(surfaces.begin(), surfaces.end(),
                                       std::pair{attributes.VendorID, attributes.ProductID}) != surfaces.end();
        PHIDP_PREPARSED_DATA preparsed = nullptr;
        if (!HidD_GetPreparsedData(query.handle, &preparsed))
            return;
        HIDP_CAPS caps{};
        HidDescriptor descriptor;
        const bool collection = HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS &&
                                (surface || WinRawInputRole(caps.UsagePage, caps.Usage, Utf8(path)) == WinHidRole::Dial);
        const bool described = collection && DescriptorFromPreparsed(preparsed, caps.Usage, descriptor, caps);
        HidD_FreePreparsedData(preparsed);
        if (!collection)
            return;

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
        const DeviceMatch match = database->Lookup(identity);

        auto dial = std::make_unique<Dial>();
        if (SurfaceDial::Handles(attributes.VendorID, attributes.ProductID))
            dial->dial = std::make_unique<SurfaceDial>();
        else if (surface && described)
        {
            // One interface may hold several collections: those with nothing of the map are not this surface
            dial->hid = std::make_unique<HidPad>(descriptor, HidLayoutFor(match));
            if (dial->hid->Empty())
                return;
        }
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
        auto writer = dial->dial ? std::make_shared<WinHandle>(OpenHid(path, GENERIC_READ | GENERIC_WRITE))
                                 : std::make_shared<WinHandle>(INVALID_HANDLE_VALUE);
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
        const auto sink = [this, &id](const std::string& control, float value) {
            _sink->OnControl({id, control, value, 0});
        };
        if (dial.dial)
            dial.dial->Decode(dial.buffer.data(), got, sink);
        else
            dial.hid->Decode(dial.buffer.data(), got, sink);
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
                if (dial->dial)
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
            return _protocol && _protocol->IdTaken(candidate);
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
    /// The pads with a protocol (DualSense, DualShock 4, Switch); made at Start, kept to the end
    std::unique_ptr<ProtocolPads> _protocol;
    IBackendSink* _protocolSink = nullptr;
    std::map<HANDLE, std::string> _protocolKeys;  ///< backend thread: their Raw Input handles -> ProtocolPads keys
    std::vector<uint64_t> _input;                  ///< backend thread: the WM_INPUT buffer, aligned
};
}  // namespace

std::unique_ptr<IBackend> CreateRawInputBackend()
{
    return std::make_unique<RawInputBackend>();
}

}  // namespace controllers
