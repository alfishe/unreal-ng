#include <hidapi.h>
#ifdef __APPLE__
#include <hidapi_darwin.h>
#endif
#ifdef __linux__
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <filesystem>
#endif

#include <algorithm>
#include <codecvt>
#include <locale>
#include <set>
#include <vector>

#include "sdb/models.h"
#include "sdb/transport.h"

namespace sdb
{
namespace
{
std::string Narrow(const wchar_t* w)
{
    if (!w)
        return {};
    std::string s;
    for (; *w; ++w)
        s.push_back(*w < 0x80 ? static_cast<char>(*w) : '?');
    return s;
}

#ifdef __linux__
// hid-generic also makes the deck an evdev keyboard: every key press reaches the desktop as KEY_UNKNOWN (KWin and Qt
// apps act on it; on the Steam Deck unreal-qt's window maximised and restored with each press). While the deck is
// open, its event nodes are grabbed (EVIOCGRAB: their events reach no other reader) and released on close. The
// hidraw path is /dev/hidrawN; its input nodes are /sys/class/hidraw/hidrawN/device/input/input*/event*. Best
// effort: a node that does not open or is grabbed already (sdctl beside the daemon) is skipped.
std::vector<int> GrabEventNodes(const std::string& hidrawPath)
{
    namespace fs = std::filesystem;
    std::vector<int> fds;
    std::error_code ec;
    const fs::path inputs = fs::path("/sys/class/hidraw") / fs::path(hidrawPath).filename() / "device" / "input";
    for (const fs::directory_entry& input : fs::directory_iterator(inputs, ec))
        for (const fs::directory_entry& node : fs::directory_iterator(input.path(), ec))
        {
            const std::string name = node.path().filename().string();
            if (name.rfind("event", 0) != 0)
                continue;
            const std::string dev = "/dev/input/" + name;
            const int fd = open(dev.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0)
                continue;
            if (ioctl(fd, EVIOCGRAB, 1) == 0)
                fds.push_back(fd);
            else
                close(fd);
        }
    return fds;
}
#endif

class HidTransport final : public Transport
{
public:
    HidTransport(hid_device* d, [[maybe_unused]] const std::string& path) : _dev(d)
    {
#ifdef __linux__
        _grabbed = GrabEventNodes(path);
#endif
    }
    ~HidTransport() override
    {
#ifdef __linux__
        for (const int fd : _grabbed)
            close(fd);  // closing releases the grab
#endif
        if (_dev)
            hid_close(_dev);
    }
    int Write(std::span<const std::uint8_t> r) override
    {
        return hid_write(_dev, r.data(), r.size());
    }
    int SendFeature(std::span<const std::uint8_t> r) override
    {
        return hid_send_feature_report(_dev, r.data(), r.size());
    }
    int GetFeature(std::span<std::uint8_t> b) override
    {
        return hid_get_feature_report(_dev, b.data(), b.size());
    }
    int Read(std::span<std::uint8_t> b, int timeoutMs) override
    {
        return hid_read_timeout(_dev, b.data(), b.size(), timeoutMs);
    }
    std::string LastError() const override
    {
        return Narrow(hid_error(_dev));
    }

private:
    hid_device* _dev;
#ifdef __linux__
    std::vector<int> _grabbed;  ///< the deck's evdev nodes, grabbed while it is open
#endif
};

struct HidInit
{
    HidInit()
    {
        hid_init();
#ifdef __APPLE__
        // shared, not seized: Stream Deck Studio listens to the real deck while sdctl draws on it (input reports reach
        // every client that has it open; Linux hidraw and Windows share by default)
        hid_darwin_set_open_exclusive(0);
#endif
    }
    ~HidInit()
    {
        hid_exit();
    }
};
HidInit& Init()
{
    static HidInit i;
    return i;
}
}  // namespace

std::vector<HidInfo> EnumerateElgato()
{
    Init();
    std::vector<HidInfo> out;
    std::set<std::string> seen;  // one entry per serial, like the crate's HashSet
    hid_device_info* head = hid_enumerate(kElgatoVid, 0);
    for (auto* d = head; d; d = d->next)
    {
        HidInfo i;
        i.path = d->path ? d->path : "";
        i.serial = Narrow(d->serial_number);
        i.product = Narrow(d->product_string);
        i.manufacturer = Narrow(d->manufacturer_string);
        i.vid = d->vendor_id;
        i.pid = d->product_id;
        i.usagePage = d->usage_page;
        i.usage = d->usage;
        i.interfaceNumber = d->interface_number;
        if (!FindModel(i.vid, i.pid) || i.serial.empty())
            continue;
        if (seen.insert(i.serial).second)
            out.push_back(std::move(i));
    }
    hid_free_enumeration(head);
    return out;
}

std::unique_ptr<Transport> OpenHid(const std::string& path, std::string* error)
{
    Init();
    hid_device* d = hid_open_path(path.c_str());
    if (!d)
    {
        if (error)
            *error = Narrow(hid_error(nullptr));
        return nullptr;
    }
    return std::make_unique<HidTransport>(d, path);
}

}  // namespace sdb
