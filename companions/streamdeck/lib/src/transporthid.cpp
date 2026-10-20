#include <hidapi.h>
#ifdef __APPLE__
#include <hidapi_darwin.h>
#endif

#include <algorithm>
#include <codecvt>
#include <locale>
#include <set>

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

class HidTransport final : public Transport
{
public:
    explicit HidTransport(hid_device* d) : _dev(d) {}
    ~HidTransport() override
    {
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
    return std::make_unique<HidTransport>(d);
}

}  // namespace sdb
