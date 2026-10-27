/// @file evdevuhid_functional_test.cpp
/// @brief The evdev backend against the real kernel drivers, with the pad emulated through uhid (Linux only, functional: `UNREAL_FUNCTIONAL_TESTS=1` and
/// write access to /dev/uhid, normally root; filter *_Functional.*). The kernel's own hid-playstation binds the
/// emulated DualSense / DualSense Edge (054C:0CE6 / 0DF2 over USB): the test answers its probe (feature reports 0x09
/// pairing, 0x20 firmware, 0x05 calibration), sends input report 0x01 and records every output report the kernel and
/// the backend write. Checked: one device with the MAC id hid-playstation sets, its controls from evdev (no `lt` jump
/// from BTN_TL2), the motors through FF_RUMBLE as hid-playstation's report 2, the trigger effects through the
/// backend's own trigger-only report 2 on hidraw, the light through the LED class, and the Edge's paddles (through
/// the full report on hidraw where the kernel predates its BTN_TRIGGER_HAPPY keys). hid-nintendo binds an emulated Switch
/// Pro controller and two Joy-Cons over Bluetooth: the test acknowledges its subcommands (device info with the type
/// and MAC, the SPI reads with a sane stick calibration) and sends the full report 0x30 every 15 ms. Checked: the Pro
/// controller's Capture, its HD rumble as the backend's own 0x10 report on hidraw (the effect's frequencies), and the
/// two Joy-Cons joined as one pad by the backend (one device, the pair layout, each half rumbling its side). Without
/// the switch or the access the tests are skipped: they make pads appear on the machine for a few seconds.

#ifdef __linux__

#include <gtest/gtest.h>

#include <fcntl.h>
#include <linux/uhid.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "_helpers/testtier.h"
#include "_helpers/testwaithelper.h"
#include "hostbackend.h"
#include "platform/linux/evdevbackend.h"

using namespace controllers;

namespace
{
/// A DualSense's reports as hid-playstation needs them over USB: input 1 (64 bytes), output 2, features 5, 9, 0x20.
/// Report 1 is laid out as the pad's own: the sticks and triggers, the hat and 15 buttons, so that hid-generic too
/// makes an evdev node of it (hid-playstation reads the raw bytes and ignores the usages)
const uint8_t kDescriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,                          // Generic Desktop, Game Pad, Application
    0x85, 0x01,                                                  // Report 1
    0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x09, 0x33,  // X, Y, Z, Rz, Rx,
    0x09, 0x34, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08,        // Ry: 0..255,
    0x95, 0x06, 0x81, 0x02,                                      // 6 bytes (sticks, triggers)
    0x06, 0x00, 0xFF, 0x09, 0x20, 0x95, 0x01, 0x81, 0x02,        // 1 vendor byte (the sequence)
    0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x35, 0x00,  // Hat switch 0..7,
    0x46, 0x3B, 0x01, 0x65, 0x14, 0x75, 0x04, 0x95, 0x01,        // 4 bits,
    0x81, 0x42,                                                  // null state
    0x65, 0x00, 0x45, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0F,  // Buttons 1..15,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0F, 0x81, 0x02,  // 15 bits
    0x06, 0x00, 0xFF, 0x09, 0x21, 0x75, 0x05, 0x95, 0x01,        // 5 vendor bits,
    0x81, 0x02, 0x09, 0x22, 0x26, 0xFF, 0x00, 0x75, 0x08,        //
    0x95, 0x35, 0x81, 0x02,                                      // 53 vendor bytes: 63 in all
    0x85, 0x02, 0x09, 0x23, 0x95, 0x3E, 0x91, 0x02,              // Report 2: 62 bytes output
    0x85, 0x05, 0x09, 0x24, 0x95, 0x28, 0xB1, 0x02,              // Feature 5: 40 bytes (calibration)
    0x85, 0x09, 0x09, 0x25, 0x95, 0x13, 0xB1, 0x02,              // Feature 9: 19 bytes (pairing: the MAC)
    0x85, 0x20, 0x09, 0x26, 0x95, 0x3F, 0xB1, 0x02,              // Feature 0x20: 63 bytes (firmware)
    0xC0,
};

/// A Switch pad's reports as hid-nintendo needs them over Bluetooth: input 0x21 (subcommand replies) and 0x30 (the
/// full report), output 0x01 (rumble and subcommand) and 0x10 (rumble only)
const uint8_t kSwitchDescriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,                    // Generic Desktop, Game Pad, Application
    0x85, 0x21, 0x06, 0x00, 0xFF, 0x09, 0x21, 0x15, 0x00,  // Report 0x21, vendor
    0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x3F, 0x81, 0x02,  // 63 bytes input
    0x85, 0x30, 0x09, 0x30, 0x95, 0x3F, 0x81, 0x02,        // Report 0x30: 63 bytes input
    0x85, 0x01, 0x09, 0x01, 0x95, 0x30, 0x91, 0x02,        // Report 0x01: 48 bytes output
    0x85, 0x10, 0x09, 0x10, 0x95, 0x30, 0x91, 0x02,        // Report 0x10: 48 bytes output
    0xC0,
};

/// One emulated pad: /dev/uhid, its probe answered and its output reports kept on a thread of its own. A Sony pad
/// answers hid-playstation's feature reads; a Switch pad acknowledges hid-nintendo's subcommands and sends its full
/// report every 15 ms
class UhidPad
{
public:
    /// @p switchType: 0 a DualSense (USB), else the Switch device type of the info reply (1 left Joy-Con, 2 right,
    /// 3 Pro controller; Bluetooth)
    explicit UhidPad(uint16_t product, uint8_t switchType = 0) : _product(product), _switchType(switchType)
    {
    }
    /// Any other HID device: its ids, bus, name and descriptor (no probe to answer; Send gives its reports)
    struct Plain
    {
        uint16_t vendor = 0;
        uint16_t product = 0;
        uint16_t bus = BUS_USB;
        std::string name;
        std::vector<uint8_t> descriptor;
    };
    explicit UhidPad(Plain plain) : _product(plain.product), _switchType(0), _plain(std::move(plain))
    {
    }
    ~UhidPad()
    {
        Destroy();
    }

    bool Create(std::string& error)
    {
        _fd = open("/dev/uhid", O_RDWR | O_CLOEXEC);
        if (_fd < 0)
        {
            error = std::string("/dev/uhid: ") + std::strerror(errno);
            return false;
        }
        uhid_event event{};
        event.type = UHID_CREATE2;
        const char* name = _plain   ? _plain->name.c_str()
                           : _switchType == 1 ? "Joy-Con (L)"
                           : _switchType == 2 ? "Joy-Con (R)"
                           : _switchType == 3 ? "Pro Controller"
                           : _product == 0x0DF2
                               ? "Sony Interactive Entertainment DualSense Edge Wireless Controller"
                               : "Sony Interactive Entertainment DualSense Wireless Controller";
        std::snprintf(reinterpret_cast<char*>(event.u.create2.name), sizeof(event.u.create2.name), "%s", name);
        std::snprintf(reinterpret_cast<char*>(event.u.create2.phys), sizeof(event.u.create2.phys), "unreal-uhid-%04x",
                      _product);
        if (_switchType != 0)
            std::snprintf(reinterpret_cast<char*>(event.u.create2.uniq), sizeof(event.u.create2.uniq),
                          "98:b6:e9:00:00:%02x", _switchType);
        const uint8_t* descriptor = _plain             ? _plain->descriptor.data()
                                    : _switchType != 0 ? kSwitchDescriptor
                                                       : kDescriptor;
        const size_t size = _plain             ? _plain->descriptor.size()
                            : _switchType != 0 ? sizeof(kSwitchDescriptor)
                                               : sizeof(kDescriptor);
        event.u.create2.rd_size = static_cast<uint16_t>(size);
        event.u.create2.bus = _plain ? _plain->bus : (_switchType != 0 ? BUS_BLUETOOTH : BUS_USB);
        event.u.create2.vendor = _plain ? _plain->vendor : (_switchType != 0 ? 0x057E : 0x054C);
        event.u.create2.product = _product;
        event.u.create2.version = 0x0100;
        std::memcpy(event.u.create2.rd_data, descriptor, size);
        if (write(_fd, &event, sizeof(event)) != static_cast<ssize_t>(sizeof(event)))
        {
            error = std::string("UHID_CREATE2: ") + std::strerror(errno);
            return false;
        }
        _thread = std::thread([this] { Run(); });
        return true;
    }

    void Destroy()
    {
        if (_fd < 0)
            return;
        _stop = true;
        if (_thread.joinable())
            _thread.join();
        uhid_event event{};
        event.type = UHID_DESTROY;
        [[maybe_unused]] const ssize_t written = write(_fd, &event, sizeof(event));
        close(_fd);
        _fd = -1;
    }

    /// Input report 0x01: sticks centred, @p buttons0..2 the three button bytes (the hat in buttons0's low nibble),
    /// @p z / @p rz the triggers
    bool Input(uint8_t buttons0, uint8_t buttons1, uint8_t buttons2, uint8_t z, uint8_t rz)
    {
        uhid_event event{};
        event.type = UHID_INPUT2;
        uint8_t* report = event.u.input2.data;
        report[0] = 0x01;
        report[1] = report[2] = report[3] = report[4] = 0x80;
        report[5] = z;
        report[6] = rz;
        report[7] = _sequence++;
        report[8] = buttons0;
        report[9] = buttons1;
        report[10] = buttons2;
        report[33] = 0x80;  // touch point 1: no finger
        report[37] = 0x80;  // touch point 2
        report[53] = 0x05;  // the battery: 50-59 %, discharging
        event.u.input2.size = 64;
        std::lock_guard lock(_writeMutex);
        return write(_fd, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event));
    }

    /// One input report as it is (the report id first)
    bool Send(const std::vector<uint8_t>& report)
    {
        uhid_event event{};
        event.type = UHID_INPUT2;
        std::memcpy(event.u.input2.data, report.data(), report.size());
        event.u.input2.size = static_cast<uint16_t>(report.size());
        std::lock_guard lock(_writeMutex);
        return write(_fd, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event));
    }

    /// A Switch pad's buttons from now on (hid-nintendo's 24 bits: byte 0 the right side, 1 the shared, 2 the left)
    void SetButtons(uint8_t right, uint8_t shared, uint8_t left)
    {
        _buttons = static_cast<uint32_t>(right | (shared << 8) | (left << 16));
    }

    std::vector<std::vector<uint8_t>> Outputs() const
    {
        std::lock_guard lock(_mutex);
        return _outputs;
    }
    void ClearOutputs()
    {
        std::lock_guard lock(_mutex);
        _outputs.clear();
    }
    int FeatureReads() const
    {
        return _features;
    }

private:
    void Run()
    {
        auto next = std::chrono::steady_clock::now();
        while (!_stop)
        {
            // A Switch pad's full report every 15 ms however busy the output side is (hid-nintendo paces its
            // subcommands on the input reports)
            if (_switchType != 0 && std::chrono::steady_clock::now() >= next)
            {
                Send30();
                next += std::chrono::milliseconds(15);
            }
            pollfd fds{_fd, POLLIN, 0};
            if (poll(&fds, 1, 5) <= 0)
                continue;
            uhid_event event{};
            if (read(_fd, &event, sizeof(event)) <= 0)
                continue;
            if (event.type == UHID_OUTPUT)
            {
                // uhid_event is packed: its bytes copied out, not bound by reference
                std::vector<uint8_t> bytes(std::min<size_t>(event.u.output.size, sizeof(event.u.output.data)));
                std::memcpy(bytes.data(), event.u.output.data, bytes.size());
                if (_switchType != 0 && bytes.size() > 10 && bytes[0] == 0x01)
                    Acknowledge(bytes.data());
                std::lock_guard lock(_mutex);
                _outputs.push_back(std::move(bytes));
            }
            else if (event.type == UHID_GET_REPORT)
                Answer(event.u.get_report.id, event.u.get_report.rnum);
            else if (event.type == UHID_SET_REPORT)
            {
                uhid_event reply{};
                reply.type = UHID_SET_REPORT_REPLY;
                reply.u.set_report_reply.id = event.u.set_report.id;
                std::lock_guard lock(_writeMutex);
                [[maybe_unused]] const ssize_t written = write(_fd, &reply, sizeof(reply));
            }
        }
    }

    /// The common head of 0x21 / 0x30: timer, battery full on battery, the buttons, both sticks centred
    void Head(uint8_t id, uint8_t* report)
    {
        report[0] = id;
        report[1] = _timer++;
        report[2] = 0x80;
        report[3] = static_cast<uint8_t>(_buttons & 0xFF);
        report[4] = static_cast<uint8_t>((_buttons >> 8) & 0xFF);
        report[5] = static_cast<uint8_t>((_buttons >> 16) & 0xFF);
        for (const int stick : {6, 9})
        {
            report[stick] = 0x00;  // x = 0x800, y = 0x800 (12 bits each)
            report[stick + 1] = 0x08;
            report[stick + 2] = 0x80;
        }
    }

    void Send30()
    {
        uhid_event event{};
        event.type = UHID_INPUT2;
        Head(0x30, event.u.input2.data);
        event.u.input2.size = 64;
        std::lock_guard lock(_writeMutex);
        [[maybe_unused]] const ssize_t written = write(_fd, &event, sizeof(event));
    }

    /// hid-nintendo's subcommand (output 0x01: id at 10, data from 11): reply 0x21 with the ack and the id echoed
    void Acknowledge(const uint8_t* output)
    {
        const uint8_t subcommand = output[10];
        uhid_event event{};
        event.type = UHID_INPUT2;
        uint8_t* report = event.u.input2.data;
        Head(0x21, report);
        report[13] = 0x80;
        report[14] = subcommand;
        uint8_t* data = report + 15;
        if (subcommand == 0x02)
        {
            // Device info: firmware, the type at 2, the MAC at 4-9
            report[13] = 0x82;
            data[0] = 0x04;
            data[1] = 0x21;
            data[2] = _switchType;
            data[3] = 0x02;
            const uint8_t mac[6] = {0x98, 0xB6, 0xE9, 0x00, 0x00, _switchType};
            std::memcpy(data + 4, mac, 6);
        }
        else if (subcommand == 0x10)
        {
            // SPI read: the address and size echoed, then the bytes. The user calibration has no magic; the factory
            // stick calibration is 0x600 each way around 0x800 (zeros would divide by zero in joycon_map_stick_val)
            report[13] = 0x90;
            std::memcpy(data, output + 11, 5);
            const uint32_t address = output[11] | (output[12] << 8) | (output[13] << 16) | (output[14] << 24);
            static const uint8_t kLeft[9] = {0x00, 0x06, 0x60, 0x00, 0x08, 0x80, 0x00, 0x06, 0x60};
            static const uint8_t kRight[9] = {0x00, 0x08, 0x80, 0x00, 0x06, 0x60, 0x00, 0x06, 0x60};
            if (address == 0x8010 || address == 0x801B || address == 0x8026)
                data[5] = data[6] = 0xFF;
            else if (address == 0x603D)
                std::memcpy(data + 5, kLeft, sizeof(kLeft));
            else if (address == 0x6046)
                std::memcpy(data + 5, kRight, sizeof(kRight));
        }
        event.u.input2.size = 64;
        std::lock_guard lock(_writeMutex);
        [[maybe_unused]] const ssize_t written = write(_fd, &event, sizeof(event));
    }

    void Answer(uint32_t id, uint8_t report)
    {
        uhid_event reply{};
        reply.type = UHID_GET_REPORT_REPLY;
        reply.u.get_report_reply.id = id;
        uint8_t* data = reply.u.get_report_reply.data;
        data[0] = report;
        switch (report)
        {
            case 0x09:  // pairing info: the MAC at 1-6 (hid-playstation prints it reversed, %pMR)
                for (uint8_t i = 0; i < 6; ++i)
                    data[1 + i] = static_cast<uint8_t>(0x01 + i);
                reply.u.get_report_reply.size = 20;
                break;
            case 0x20:
                reply.u.get_report_reply.size = 64;
                break;
            case 0x05:
                reply.u.get_report_reply.size = 41;
                break;
            default:
                reply.u.get_report_reply.err = EIO;
                break;
        }
        ++_features;
        std::lock_guard lock(_writeMutex);
        [[maybe_unused]] const ssize_t written = write(_fd, &reply, sizeof(reply));
    }

    uint16_t _product;
    uint8_t _switchType;
    std::optional<Plain> _plain;
    int _fd = -1;
    std::thread _thread;
    std::atomic<bool> _stop{false};
    std::atomic<int> _features{0};
    std::atomic<uint32_t> _buttons{0};
    uint8_t _sequence = 0;
    uint8_t _timer = 0;
    mutable std::mutex _mutex;
    std::mutex _writeMutex;
    std::vector<std::vector<uint8_t>> _outputs;
};

class RecordingSink : public IBackendSink
{
public:
    void OnDeviceAdded(const HostDeviceInfo& device) override
    {
        std::lock_guard lock(_mutex);
        _added.push_back(device);
    }
    void OnDeviceRemoved(const HostDeviceId& device) override
    {
        std::lock_guard lock(_mutex);
        _removed.push_back(device);
    }
    void OnControl(const ControlEvent& event) override
    {
        std::lock_guard lock(_mutex);
        _controls.push_back(event);
    }

    /// The device of this vendor and product (`usb:054c:0ce6:`), "" before it came
    HostDeviceId Find(const std::string& prefix) const
    {
        std::lock_guard lock(_mutex);
        for (const HostDeviceInfo& info : _added)
            if (info.id.rfind(prefix, 0) == 0)
                return info.id;
        return {};
    }
    bool Removed(const HostDeviceId& id) const
    {
        std::lock_guard lock(_mutex);
        return std::find(_removed.begin(), _removed.end(), id) != _removed.end();
    }
    /// The last value of @p control of @p device; -2 when none came
    float Last(const HostDeviceId& device, const std::string& control) const
    {
        std::lock_guard lock(_mutex);
        for (auto it = _controls.rbegin(); it != _controls.rend(); ++it)
            if (it->device == device && it->control == control)
                return it->value;
        return -2.0f;
    }
    bool Ever(const HostDeviceId& device, const std::string& control, float value) const
    {
        std::lock_guard lock(_mutex);
        for (const ControlEvent& event : _controls)
            if (event.device == device && event.control == control && event.value == value)
                return true;
        return false;
    }
    std::string Name(const HostDeviceId& device) const
    {
        std::lock_guard lock(_mutex);
        for (const HostDeviceInfo& info : _added)
            if (info.id == device)
                return info.name;
        return {};
    }

private:
    mutable std::mutex _mutex;
    std::vector<HostDeviceInfo> _added;
    std::vector<HostDeviceId> _removed;
    std::vector<ControlEvent> _controls;
};

bool UhidAllowed(std::string& why)
{
    if (!TestTier::Functional())
    {
        why = "functional test: UNREAL_FUNCTIONAL_TESTS=1 not set (the test makes a pad appear on this machine)";
        return false;
    }
    if (access("/dev/uhid", R_OK | W_OK) != 0)
    {
        why = "no read-write access to /dev/uhid (run as root)";
        return false;
    }
    return true;
}

/// A report the kernel or the backend wrote: report 2 with these valid flags
bool HasOutput(const UhidPad& pad, const std::function<bool(const std::vector<uint8_t>&)>& match)
{
    for (const std::vector<uint8_t>& report : pad.Outputs())
        if (!report.empty() && report[0] == 0x02 && report.size() >= 48 && match(report))
            return true;
    return false;
}

/// The emulated pad as the backend sees it, and its first input
struct Session
{
    std::unique_ptr<IBackend> backend = CreateEvdevBackend();
    RecordingSink sink;
    UhidPad pad;
    HostDeviceId id;

    explicit Session(uint16_t product, uint8_t switchType = 0) : pad(product, switchType)
    {
    }
    explicit Session(UhidPad::Plain plain) : pad(std::move(plain))
    {
    }
    ~Session()
    {
        pad.Destroy();
        backend->Stop();
    }

    /// Starts the backend, creates the pad and waits for the device whose id starts with @p prefix
    void Open(const std::string& prefix)
    {
        std::string reason;
        ASSERT_TRUE(backend->Start(sink, reason)) << reason;
        std::string error;
        ASSERT_TRUE(pad.Create(error)) << error;
        ASSERT_TRUE(TestWait::For([&] { return !sink.Find(prefix).empty(); }, std::chrono::milliseconds(8000)))
            << "the kernel driver did not bind (feature reads answered: " << pad.FeatureReads()
            << "); is the module available? modprobe hid-playstation / hid-nintendo";
        id = sink.Find(prefix);
    }
};

/// A 0x10 report a Switch pad got whose rumble is not the neutral one
bool HasSwitchRumble(const UhidPad& pad)
{
    static const uint8_t kNeutral[8] = {0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};
    for (const std::vector<uint8_t>& report : pad.Outputs())
        if (report.size() >= 10 && report[0] == 0x10 && std::memcmp(report.data() + 2, kNeutral, 8) != 0)
            return true;
    return false;
}
/// hid.ignore_special_drivers for the guard's life: hid-generic binds a new device in place of its own driver (the
/// macOS-like path where the backend speaks the pad's protocol on hidraw). Only devices created meanwhile are affected
class IgnoreSpecialDrivers
{
public:
    IgnoreSpecialDrivers()
    {
        std::FILE* file = std::fopen(kPath, "r");
        if (file == nullptr)
            return;
        _previous = std::fgetc(file);
        std::fclose(file);
        _set = Write('1');
    }
    ~IgnoreSpecialDrivers()
    {
        Restore();
    }
    void Restore()
    {
        if (_set)
            Write(_previous == '1' ? '1' : '0');
        _set = false;
    }
    bool Set() const
    {
        return _set;
    }

private:
    static constexpr const char* kPath = "/sys/module/hid/parameters/ignore_special_drivers";
    static bool Write(char value)
    {
        std::FILE* file = std::fopen(kPath, "w");
        if (file == nullptr)
            return false;
        const bool ok = std::fputc(value, file) != EOF;
        return std::fclose(file) == 0 && ok;
    }
    int _previous = '0';
    bool _set = false;
};
}  // namespace

TEST(LinuxUhid_Functional, DualSense_KernelMotorsBackendTriggersLightAndControls)
{
    std::string why;
    if (!UhidAllowed(why))
        GTEST_SKIP() << why;
    Session session(0x0CE6);
    session.Open("usb:054c:0ce6:");
    if (HasFatalFailure())
        return;
    const HostDeviceId& id = session.id;
    // hid-playstation sets uniq to the MAC (feature 0x09, reversed): the USB id carries it
    // The version with bit 15 set: hid-playstation marks its own mappings for SDL (HID_PLAYSTATION_VERSION_PATCH)
    EXPECT_EQ(id, "usb:054c:0ce6:8100:06:05:04:03:02:01");
    EXPECT_EQ(session.sink.Name(id), "DualSense");
    EXPECT_EQ(session.backend->DeviceNotice(id), "") << "root opens the hidraw node";
    EXPECT_EQ(session.backend->RumbleActuators(id), kRumbleLow | kRumbleHigh | kRumbleLeftTrigger | kRumbleRightTrigger)
        << "FF_RUMBLE's motors and the backend's triggers";
    EXPECT_TRUE(session.backend->HasLight(id)) << session.backend->LightUnavailable(id);
    EXPECT_EQ(session.backend->DeviceCount(), 1u) << "the Touchpad and Motion Sensors nodes are no devices";

    // The controls through evdev: Cross, L2 half way (its BTN_TL2 beside ABS_Z must not make `lt` jump to 1)
    ASSERT_TRUE(session.pad.Input(0x08 | 0x20, 0x04, 0x00, 128, 0));
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "south") == 1.0f; })) << "Cross";
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "lt") > 0.4f; }));
    EXPECT_NEAR(session.sink.Last(id, "lt"), 128.0f / 255.0f, 0.01f);
    EXPECT_FALSE(session.sink.Ever(id, "lt", 1.0f)) << "BTN_TL2 left out";
    ASSERT_TRUE(session.pad.Input(0x08, 0x00, 0x00, 0, 0));
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "south") == 0.0f; }));

    // The motors through FF_RUMBLE (hid-playstation's report 2: compatible vibration, right = weak, left = strong),
    // the triggers through the backend's own report 2 with only the trigger flags
    session.pad.ClearOutputs();
    RumbleLevels levels;
    levels.low = 1.0f;
    levels.high = 0.5f;
    levels.leftTrigger = 0.5f;
    levels.rightTrigger = 1.0f;
    ASSERT_TRUE(session.backend->SetRumble(id, levels));
    EXPECT_TRUE(TestWait::For([&] {
        return HasOutput(session.pad, [](const std::vector<uint8_t>& r) {
            return (r[1] & 0x03) == 0x03 && r[3] == 128 && r[4] == 255;
        });
    })) << "the kernel's motors";
    EXPECT_TRUE(TestWait::For([&] {
        return HasOutput(session.pad, [](const std::vector<uint8_t>& r) {
            return r[1] == 0x0C && r[2] == 0 && r[3] == 0 && r[4] == 0 && r[11] == 0x01 && r[22] == 0x01 &&
                   r[13] == 255 && r[24] == 128;
        });
    })) << "the backend's trigger effects, nothing else";
    ASSERT_TRUE(session.backend->SetRumble(id, {}));
    EXPECT_TRUE(TestWait::For([&] {
        return HasOutput(session.pad, [](const std::vector<uint8_t>& r) { return r[1] == 0x0C && r[11] == 0x00; });
    })) << "zero turns the resistance off";

    // The light through the LED class: hid-playstation's report 2 with the light bar flag
    session.pad.ClearOutputs();
    ASSERT_TRUE(session.backend->SetLight(id, {10, 20, 200}));
    EXPECT_TRUE(TestWait::For([&] {
        return HasOutput(session.pad, [](const std::vector<uint8_t>& r) {
            return (r[2] & 0x04) != 0 && r[45] < r[46] && r[46] < r[47] && r[47] > 100;
        });
    })) << "the light bar";

    session.pad.Destroy();
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Removed(id); }));
}

TEST(LinuxUhid_Functional, DualSenseEdge_PaddlesAndFnButtonsArriveOnEveryKernel)
{
    std::string why;
    if (!UhidAllowed(why))
        GTEST_SKIP() << why;
    Session session(0x0DF2);
    session.Open("usb:054c:0df2:");
    if (HasFatalFailure())
        return;
    const HostDeviceId& id = session.id;
    EXPECT_EQ(session.sink.Name(id), "DualSense Edge");
    EXPECT_EQ(session.backend->DeviceNotice(id), "");
    // The left paddle (buttons[2] bit 6) and Fn1 (bit 4): through evdev from 7.2, else through the full report the
    // backend reads on hidraw (SonyPad), with the other controls from the same source
    ASSERT_TRUE(session.pad.Input(0x08 | 0x20, 0x00, 0x40 | 0x10, 0, 0));
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "paddle.p3") == 1.0f; })) << "the left paddle";
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "misc.2") == 1.0f; })) << "Fn1";
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "south") == 1.0f; }));
    ASSERT_TRUE(session.pad.Input(0x08, 0x00, 0x00, 0, 0));
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "paddle.p3") == 0.0f; }));
    EXPECT_FALSE(session.sink.Ever(id, "paddle.p1", 1.0f)) << "the right paddle never pressed";
}

TEST(LinuxUhid_Functional, DualSenseUnderHidGeneric_TheBackendSpeaksItsProtocol)
{
    std::string why;
    if (!UhidAllowed(why))
        GTEST_SKIP() << why;
    IgnoreSpecialDrivers generic;
    if (!generic.Set())
        GTEST_SKIP() << "hid.ignore_special_drivers is not writable";
    Session session(0x0CE6);
    session.Open("usb:054c:0ce6:");
    generic.Restore();
    if (HasFatalFailure())
        return;
    const HostDeviceId& id = session.id;
    EXPECT_EQ(session.pad.FeatureReads(), 0) << "no hid-playstation probe: hid-generic serves it";
    EXPECT_EQ(session.sink.Name(id), "DualSense");
    EXPECT_EQ(session.backend->DeviceNotice(id), "") << "root opens the hidraw node";
    EXPECT_EQ(session.backend->RumbleActuators(id), kRumbleLow | kRumbleHigh | kRumbleLeftTrigger | kRumbleRightTrigger);
    EXPECT_TRUE(session.backend->HasLight(id)) << session.backend->LightUnavailable(id);

    // The controls from the full report: Cross is `south` (hid-generic's evdev calls it BTN_EAST, button 2)
    ASSERT_TRUE(session.pad.Input(0x08 | 0x20, 0x00, 0x00, 0, 0));
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "south") == 1.0f; })) << "Cross";
    EXPECT_FALSE(session.sink.Ever(id, "east", 1.0f)) << "hid-generic's button names dropped";
    ASSERT_TRUE(session.pad.Input(0x08, 0x00, 0x00, 0, 0));
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "south") == 0.0f; }));

    // Motors, triggers and the light: the backend's own report 2
    session.pad.ClearOutputs();
    RumbleLevels levels;
    levels.low = 1.0f;
    levels.high = 0.5f;
    levels.rightTrigger = 1.0f;
    ASSERT_TRUE(session.backend->SetRumble(id, levels));
    EXPECT_TRUE(TestWait::For([&] {
        return HasOutput(session.pad, [](const std::vector<uint8_t>& r) {
            return (r[1] & 0x03) == 0x03 && r[4] == 255 && r[3] >= 126 && r[3] <= 128 && r[11] == 0x01;
        });
    })) << "the motors and the right trigger in one report";
    ASSERT_TRUE(session.backend->SetLight(id, {10, 20, 200}));
    EXPECT_TRUE(TestWait::For([&] {
        return HasOutput(session.pad, [](const std::vector<uint8_t>& r) {
            return (r[2] & 0x04) != 0 && r[45] == 10 && r[46] == 20 && r[47] == 200;
        });
    })) << "the light bar";
    ASSERT_TRUE(session.backend->SetRumble(id, {}));
}


/// The GameCube controller adapter (057E:0337) under hid-generic: no input device (its reports are on the Game
/// Controls page), so the backend finds its hidraw node by the ids, writes the start report 0x13 and shows each port
/// with a controller in it as a device of its own, as on macOS
TEST(LinuxUhid_Functional, GameCubeAdapter_PortsThroughHidraw)
{
    std::string why;
    if (!UhidAllowed(why))
        GTEST_SKIP() << why;
    UhidPad::Plain plain;
    plain.vendor = 0x057E;
    plain.product = 0x0337;
    plain.name = "Nintendo Co., Ltd. WUP-028";
    plain.descriptor = {
        0x05, 0x05, 0x09, 0x00, 0xA1, 0x01, 0x85, 0x11, 0x19, 0x00, 0x2A, 0xFF, 0x00, 0x15, 0x00,  // rumble 0x11
        0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x04, 0x91, 0x00, 0xC0,                                //
        0xA1, 0x01, 0x85, 0x21, 0x05, 0x00, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x24,  // input 0x21
        0x81, 0x00, 0xC0,                                                                          //
        0xA1, 0x01, 0x85, 0x13, 0x19, 0x00, 0x2A, 0xFF, 0x00, 0x15, 0x00, 0x26, 0xFF, 0x00,        // start 0x13
        0x75, 0x08, 0x95, 0x01, 0x91, 0x00, 0xC0,
    };
    Session session(std::move(plain));
    std::string reason;
    ASSERT_TRUE(session.backend->Start(session.sink, reason)) << reason;
    std::string error;
    ASSERT_TRUE(session.pad.Create(error)) << error;
    const auto started = [&] {
        for (const std::vector<uint8_t>& report : session.pad.Outputs())
            if (report.size() == 2 && report[0] == 0x13 && report[1] == 0x00)
                return true;  // hidraw takes no 1-byte write: the start report padded
        return false;
    };
    ASSERT_TRUE(TestWait::For(started, std::chrono::milliseconds(8000))) << "no start report 0x13 written";
    EXPECT_TRUE(session.sink.Find("usb:057e:0337").empty()) << "the adapter itself is not shown";

    std::vector<uint8_t> report(37, 0);
    report[0] = 0x21;
    uint8_t* port2 = report.data() + 1 + 9;
    port2[0] = 0x10;  // a wired controller
    port2[1] = 0x01;  // A
    for (size_t axis = 3; axis < 7; ++axis)
        port2[axis] = 0x80;
    ASSERT_TRUE(session.pad.Send(report));
    ASSERT_TRUE(TestWait::For([&] { return !session.sink.Find("usb:057e:0337").empty(); }));
    const HostDeviceId id = session.sink.Find("usb:057e:0337");
    EXPECT_EQ(id.substr(id.size() - 3), ":p2");
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "south") == 1.0f; })) << "A";

    port2[0] = 0x00;  // taken out
    ASSERT_TRUE(session.pad.Send(report));
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Removed(id); }));
}

/// An Xbox Wireless Controller over Bluetooth (045E:0B13) under hid-generic: hid-input numbers its keys (Button 4, X,
/// arrives as BTN_NORTH by its code); the family's HID map names them as HidPad does on macOS, X is `west`
TEST(LinuxUhid_Functional, XboxUnderHidGeneric_KeysNamedByTheFamilysHidMap)
{
    std::string why;
    if (!UhidAllowed(why))
        GTEST_SKIP() << why;
    IgnoreSpecialDrivers generic;  // hid-microsoft would bind it otherwise (its keys are hid-input's all the same)
    if (!generic.Set())
        GTEST_SKIP() << "hid.ignore_special_drivers is not writable";
    UhidPad::Plain plain;
    plain.vendor = 0x045E;
    plain.product = 0x0B13;
    plain.bus = BUS_BLUETOOTH;
    plain.name = "Xbox Wireless Controller";
    plain.descriptor = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01,              // Game Pad, report 1
        0x05, 0x09, 0x19, 0x01, 0x29, 0x0F, 0x15, 0x00, 0x25, 0x01,  // Buttons 1..15
        0x75, 0x01, 0x95, 0x0F, 0x81, 0x02, 0x95, 0x01, 0x81, 0x03,  // 15 bits, 1 padding bit
        0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF,  // X, Y
        0x00, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,                    // 2 bytes
        0x85, 0x03, 0x06, 0x00, 0xFF, 0x09, 0x01, 0x95, 0x08, 0x91,  // Report 3: 8 bytes output (rumble)
        0x02, 0xC0,
    };
    Session session(std::move(plain));
    session.Open("bt:045e:0b13:");
    generic.Restore();
    if (HasFatalFailure())
        return;
    const HostDeviceId& id = session.id;
    ASSERT_TRUE(session.pad.Send({0x01, 0x08, 0x00, 0x80, 0x80}));  // Button 4
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "west") == 1.0f; })) << "X";
    EXPECT_FALSE(session.sink.Ever(id, "north", 1.0f)) << "not the code's positional name";
    ASSERT_TRUE(session.pad.Send({0x01, 0x00, 0x10, 0x80, 0x80}));  // Button 13
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "guide") == 1.0f; }));
    EXPECT_EQ(session.sink.Last(id, "west"), 0.0f);
    ASSERT_TRUE(session.pad.Send({0x01, 0x00, 0x00, 0xFF, 0x80}));
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "lstick.x") > 0.9f; }));
}

/// An Amazon Luna Controller over Bluetooth (0171:0419) under hid-generic: its guide, view and record are consumer
/// usages inside the Game Pad application, so hid-input gives them the pad's node as KEY_HOMEPAGE / KEY_BACK /
/// KEY_RECORD; the family's `hid.consumer` map names them as HidPad does on macOS, no hidraw node needed
TEST(LinuxUhid_Functional, LunaConsumerKeysOnThePadsNode)
{
    std::string why;
    if (!UhidAllowed(why))
        GTEST_SKIP() << why;
    UhidPad::Plain plain;
    plain.vendor = 0x0171;
    plain.product = 0x0419;
    plain.bus = BUS_BLUETOOTH;
    plain.name = "Luna Controller";
    plain.descriptor = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01,              // Game Pad, report 1
        0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01,  // Buttons 1..16
        0x75, 0x01, 0x95, 0x10, 0x81, 0x02,                          //
        0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x26, 0xFF, 0x00,        // X, Y
        0x75, 0x08, 0x95, 0x02, 0x81, 0x02,                          // 2 bytes
        0x05, 0x0C, 0x0A, 0x23, 0x02, 0x0A, 0x24, 0x02, 0x09, 0xB2,  // AC Home, AC Back, Record
        0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02,  // 3 bits
        0x95, 0x05, 0x81, 0x03, 0xC0,                                // 5 padding bits
    };
    Session session(std::move(plain));
    session.Open("bt:0171:0419:");
    if (HasFatalFailure())
        return;
    const HostDeviceId& id = session.id;
    EXPECT_EQ(session.backend->DeviceNotice(id), "");
    ASSERT_TRUE(session.pad.Send({0x01, 0x00, 0x00, 0x80, 0x80, 0x01}));  // AC Home
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "guide") == 1.0f; })) << "AC Home";
    ASSERT_TRUE(session.pad.Send({0x01, 0x00, 0x00, 0x80, 0x80, 0x02}));  // AC Back
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "view") == 1.0f; }));
    EXPECT_EQ(session.sink.Last(id, "guide"), 0.0f);
    ASSERT_TRUE(session.pad.Send({0x01, 0x01, 0x00, 0x80, 0x80, 0x00}));  // Button 1
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "south") == 1.0f; }));
}

TEST(LinuxUhid_Functional, SwitchPro_CaptureAndHdRumbleThroughHidraw)
{
    std::string why;
    if (!UhidAllowed(why))
        GTEST_SKIP() << why;
    Session session(0x2009, 3);
    session.Open("bt:057e:2009:");
    if (HasFatalFailure())
        return;
    const HostDeviceId& id = session.id;
    EXPECT_EQ(session.backend->DeviceNotice(id), "");
    EXPECT_EQ(session.backend->RumbleActuators(id), kRumbleLow | kRumbleHigh);
    EXPECT_EQ(session.backend->DeviceCount(), 1u) << "the IMU node is no device";

    session.pad.SetButtons(0x08, 0x20, 0x00);  // A, Capture
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "share") == 1.0f; })) << "Capture (BTN_Z)";
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "east") == 1.0f; })) << "A is east";
    session.pad.SetButtons(0x00, 0x00, 0x00);
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(id, "share") == 0.0f; }));

    // The HD rumble: the backend's own 0x10 report on hidraw at the effect's frequencies, neutral at the end
    session.pad.ClearOutputs();
    RumbleLevels levels;
    levels.low = 0.8f;
    levels.high = 0.4f;
    levels.lowHz = 100;
    levels.highHz = 600;
    ASSERT_TRUE(session.backend->SetRumble(id, levels));
    EXPECT_TRUE(TestWait::For([&] { return HasSwitchRumble(session.pad); })) << "a 0x10 report with the effect";
    ASSERT_TRUE(session.backend->SetRumble(id, {}));
}

TEST(LinuxUhid_Functional, JoyCons_JoinedAsOnePadEachHalfItsSide)
{
    std::string why;
    if (!UhidAllowed(why))
        GTEST_SKIP() << why;
    Session session(0x2006, 1);
    session.Open("bt:057e:2006:");
    if (HasFatalFailure())
        return;
    const HostDeviceId left = session.id;
    EXPECT_EQ(session.sink.Name(left), "Joy-Con (L)");
    // Alone, held sideways: its left arrow is the face button south
    session.pad.SetButtons(0x00, 0x00, 0x08);
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(left, "south") == 1.0f; })) << "sideways";
    session.pad.SetButtons(0x00, 0x00, 0x00);
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(left, "south") == 0.0f; }));

    UhidPad right(0x2007, 2);
    std::string error;
    ASSERT_TRUE(right.Create(error)) << error;
    ASSERT_TRUE(TestWait::For([&] { return !session.sink.Find(left + "+").empty(); }, std::chrono::milliseconds(8000)))
        << "the pair";
    const HostDeviceId pair = session.sink.Find(left + "+");
    EXPECT_NE(pair.find("+bt:057e:2007:"), std::string::npos) << pair;
    EXPECT_EQ(session.sink.Name(pair), "Joy-Con (L/R)");
    EXPECT_TRUE(session.sink.Removed(left)) << "the left one alone is gone into the pair";

    // The pair layout: the left arrow is the d-pad's, A on the right one is east
    session.pad.SetButtons(0x00, 0x00, 0x08);
    right.SetButtons(0x08, 0x00, 0x00);
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(pair, "dpad.left") == 1.0f; }));
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Last(pair, "east") == 1.0f; }));
    session.pad.SetButtons(0x00, 0x00, 0x00);
    right.SetButtons(0x00, 0x00, 0x00);

    // Each half its side: the left the low level, the right the high one
    session.pad.ClearOutputs();
    right.ClearOutputs();
    RumbleLevels levels;
    levels.low = 0.8f;
    levels.high = 0.6f;
    ASSERT_TRUE(session.backend->SetRumble(pair, levels));
    EXPECT_TRUE(TestWait::For([&] { return HasSwitchRumble(session.pad); })) << "the left half";
    EXPECT_TRUE(TestWait::For([&] { return HasSwitchRumble(right); })) << "the right half";
    ASSERT_TRUE(session.backend->SetRumble(pair, {}));

    // The right one goes: the pair breaks, the left one is alone again
    right.Destroy();
    EXPECT_TRUE(TestWait::For([&] { return session.sink.Removed(pair); }, std::chrono::milliseconds(4000)));
}

#endif  // __linux__
