// Opening decks on USB: the part of libsdb that needs hidapi (library sdb-hid). The studio links sdb alone.
#include "sdb/device.h"

namespace sdb
{

std::vector<Found> ListDevices()
{
    std::vector<Found> out;
    for (auto& i : EnumerateElgato())
        if (const Model* m = FindModel(i.vid, i.pid))
            out.push_back({m, i});
    return out;
}

std::unique_ptr<Device> OpenDevice(const Found& f, std::string* error)
{
    if (f.info.path.rfind("emu:", 0) == 0)
    {  // a Stream Deck Studio found by findTarget
        auto dev = OpenEmulator(f.info.path.substr(4), error);
        if (dev && f.physical && !dev->Emulate(*f.model, error))
            return nullptr;
        return dev;
    }
    auto t = OpenHid(f.info.path, error);
    if (!t)
        return nullptr;
    auto dev = std::make_unique<Device>(f.physical ? *f.physical : *f.model, std::move(t), f.info.serial);
    if (f.mirror)
    {  // before emulate: the mirror shows the real model
        std::string merr;
        if (!dev->AttachMirror(*f.mirror, &merr) && !f.mirrorOptional)
        {
            if (error)
                *error = merr;
            return nullptr;
        }
    }
    if (f.physical && !dev->Emulate(*f.model, error))
        return nullptr;
    return dev;
}

std::unique_ptr<Device> OpenFirst(const std::string& sub, std::string* error)
{
    for (auto& f : ListDevices())
        if (sub.empty() || f.info.serial.find(sub) != std::string::npos)
            return OpenDevice(f, error);
    if (error)
        *error = "no matching device";
    return nullptr;
}

}  // namespace sdb
