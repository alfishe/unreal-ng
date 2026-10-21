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

namespace
{
// One reopen attempt of a reconnecting deck: the deck with that serial and model, or another deck that is there.
ReconnectingTransport::Reopen ReopenHid(std::string serial, std::uint16_t pid, std::string filter)
{
    return [serial = std::move(serial), pid, filter = std::move(filter)](bool anyDeck) {
        ReconnectingTransport::Probe p;
        std::string other;
        for (const auto& i : EnumerateElgato())
        {
            if (i.serial == serial && i.pid == pid)
            {
                p.same = OpenHid(i.path);  // may fail (still settling, held elsewhere): the next attempt tries again
                return p;
            }
            // the same serial as another model (a deck in another mode) is another deck too
            if (anyDeck && other.empty() && (filter.empty() || i.serial.find(filter) != std::string::npos))
                other = i.serial;
        }
        p.other = other;
        return p;
    };
}
}  // namespace

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
    if (f.reconnect)
    {
        const Model& physical = f.physical ? *f.physical : *f.model;
        t = std::make_unique<ReconnectingTransport>(std::move(t), physical, f.info.serial,
                                                    ReopenHid(f.info.serial, physical.pid, f.reconnect->filter),
                                                    *f.reconnect);
    }
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
