// Device selection shared by sdctl and unreal-streamdeck: --device <serial-substring | emu[:host:port]>, --mirror,
// --no-mirror, --emulate <model>.
#include "cli.h"

#include <cstdio>
#include <cstdlib>

using namespace sdb;

namespace cli
{
std::optional<Found> FindTarget(const Args& a)
{
    std::optional<Found> f;
    const std::string sel = a.Str("device");
    if (sel == "emu" || sel.rfind("emu:", 0) == 0)
    {  // ask the emulator which model it shows
        const std::string addr = sel.size() > 4 ? sel.substr(4) : std::string();
        std::string err;
        auto d = OpenEmulator(addr, &err);
        if (!d)
        {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return std::nullopt;
        }
        HidInfo info;
        info.path = "emu:" + addr;
        info.serial = d->Serial();
        f = Found{&d->GetModel(), info};
    }
    if (!f)
        for (auto& d : ListDevices())
            if (a.Str("device").empty() || d.info.serial.find(a.Str("device")) != std::string::npos)
            {
                f = d;
                break;
            }
    if (!f && a.Has("out"))
    {  // render-only: assume a + XL (or --pid)
        const int pid = a.Has("pid") ? static_cast<int>(std::strtol(a.Str("pid").c_str(), nullptr, 0)) : 0x00C6;
        if (const Model* m = FindModel(kElgatoVid, static_cast<std::uint16_t>(pid)))
            f = Found{m, {}};
    }
    if (f && f->info.path.rfind("emu:", 0) != 0 && !f->info.path.empty())
    {
        // the real deck and Stream Deck Studio at once by default (when the studio runs); --no-mirror = the deck alone
        if (a.Has("mirror"))
            f->mirror = a.Str("mirror") == "1" ? std::string() : a.Str("mirror");
        else if (!a.Has("no-mirror") && !std::getenv("SDCTL_NO_MIRROR"))
        {
            f->mirror = std::string();
            f->mirrorOptional = true;
        }
    }
    if (f && a.Has("emulate"))
    {
        const Model* v = FindModelByName(a.Str("emulate"));
        if (!v)
        {
            std::fprintf(stderr, "error: unknown model for --emulate: %s\n", a.Str("emulate").c_str());
            return std::nullopt;
        }
        f->physical = f->model;
        f->model = v;
    }
    return f;
}

std::unique_ptr<Device> OpenOrExplain(const Args& a)
{
    std::string err;
    std::unique_ptr<Device> d;
    const std::string sel = a.Str("device");
    if (auto f = FindTarget(a))
    {
        d = OpenDevice(*f, &err);
    }
    else
    {
        err = "no matching device";
    }

    if (!d)
    {
        std::fprintf(stderr, "error: cannot open device (%s)\n", err.c_str());
        std::fprintf(
            stderr,
            "hint: quit other Stream Deck software (the Elgato app) first; it holds the device. Run `sdctl info`.\n");
    }
    return d;
}
}  // namespace cli
