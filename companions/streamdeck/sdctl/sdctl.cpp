// sdctl - non-interactive Stream Deck bridge CLI.
//   sdctl info [--json]
//   sdctl theme <dir|theme.json> [--device S] [--no-hide]
//   sdctl events [--device S] [--seconds N] [--json] [--raw]
//   sdctl brightness <0-100> | clear | reset   [--device S]
//   sdctl lcd-test <orient|tiles|sweep> ...    (see lcdtest.cpp)
//   sdctl bench <keys|matrix|lcd|feature|mixed> ...   (see bench.cpp)
#include <atomic>
#include <csignal>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

#include "cli.h"
#include "sdb/draw.h"
#include "sdb/format.h"
#include "sdb/http.h"
#include "sdb/image.h"
#include "sdb/protocol.h"
#include "sdb/theme.h"

using json = nlohmann::json;
using namespace sdb;

namespace
{
std::atomic<bool> stopRequested{false};
void OnSignal(int)
{
    stopRequested = true;
}

const char* LcdKindName(LcdKind k)
{
    return k == LcdKind::None ? "none" : k == LcdKind::Fill ? "fill" : "region";
}
const char* ModeName(ImageMode m)
{
    return m == ImageMode::Jpeg ? "JPEG" : m == ImageMode::Bmp ? "BMP" : "none";
}

int CmdInfo(const cli::Args& a)
{
    const auto found = ListDevices();
    json out = json::array();
    for (const auto& f : found)
    {
        const Model& m = *f.model;
        json d = {
            {"model", std::string(m.name)},
            {"pid", sdb::Format("0x{:04X}", m.pid)},
            {"serial", f.info.serial},
            {"path", f.info.path},
            {"product_string", f.info.product},
            {"family", m.family == Family::V1 ? "V1" : "V2"},
            {"keys", m.keys},
            {"rows", m.rows},
            {"cols", m.cols},
            {"dials", m.dials},
            {"touch_points", m.touchPoints},
            {"key_image",
             {{"mode", ModeName(m.key.mode)}, {"w", m.key.w}, {"h", m.key.h}, {"rotation", Degrees(m.key.rotation)}}},
            {"lcd",
             {{"kind", LcdKindName(m.lcdKind)},
              {"logical_w", m.lcdLogicalW},
              {"logical_h", m.lcdLogicalH},
              {"device_w", m.lcd.w},
              {"device_h", m.lcd.h},
              {"device_rotation", Degrees(m.lcd.rotation)}}}};
        std::string err;
        if (auto dev = OpenDevice(f, &err))
        {
            d["openable"] = true;
            if (auto fw = dev->Firmware())
                d["firmware"] = *fw;
            if (auto sn = dev->SerialFromDevice())
                d["serial_feature_report"] = *sn;
            if (auto u = dev->ReadUnitInfo())
                d["unit_info"] = {{"rows", u->rows},  {"cols", u->cols},  {"key_w", u->keyW}, {"key_h", u->keyH},
                                  {"lcd_w", u->lcdW}, {"lcd_h", u->lcdH}, {"bpp", u->bpp}};
            if (auto sl = dev->SleepAfter())
                d["sleep_after_s"] = *sl;
        }
        else
        {
            d["openable"] = false;
            d["open_error"] = err;
        }
        out.push_back(std::move(d));
    }
    if (a.Has("json"))
    {
        std::cout << out.dump(2) << "\n";
        return found.empty() ? 1 : 0;
    }
    if (found.empty())
    {
        std::puts("no Elgato devices found");
        return 1;
    }
    for (const auto& d : out)
    {
        std::printf("%s  pid=%s  serial=%s\n", d["model"].get<std::string>().c_str(),
                    d["pid"].get<std::string>().c_str(), d["serial"].get<std::string>().c_str());
        std::printf("  layout     %dx%d keys=%d dials=%d touch=%d  protocol=%s\n", d["rows"].get<int>(),
                    d["cols"].get<int>(), d["keys"].get<int>(), d["dials"].get<int>(), d["touch_points"].get<int>(),
                    d["family"].get<std::string>().c_str());
        const auto& k = d["key_image"];
        std::printf("  key image  %s %dx%d rot%d\n", k["mode"].get<std::string>().c_str(), k["w"].get<int>(),
                    k["h"].get<int>(), k["rotation"].get<int>());
        const auto& l = d["lcd"];
        if (l["kind"] != "none")
            std::printf("  lcd        %s logical %dx%d, device %dx%d rot%d\n", l["kind"].get<std::string>().c_str(),
                        l["logical_w"].get<int>(), l["logical_h"].get<int>(), l["device_w"].get<int>(),
                        l["device_h"].get<int>(), l["device_rotation"].get<int>());
        if (d["openable"].get<bool>())
        {
            std::printf("  firmware   %s\n", d.value("firmware", std::string("?")).c_str());
            if (d.contains("unit_info"))
            {
                const auto& u = d["unit_info"];
                std::printf("  unit info  %dx%d keys %dx%d px, panel %dx%d px\n", u["rows"].get<int>(),
                            u["cols"].get<int>(), u["key_w"].get<int>(), u["key_h"].get<int>(), u["lcd_w"].get<int>(),
                            u["lcd_h"].get<int>());
            }
            if (d.contains("sleep_after_s"))
                std::printf("  sleep      %s\n",
                            d["sleep_after_s"].get<int>()
                                ? (std::to_string(d["sleep_after_s"].get<int>()) + " s idle").c_str()
                                : "off");
        }
        else
            std::printf("  NOT OPENABLE: %s\n", d.value("open_error", std::string("?")).c_str());
    }
    return 0;
}

int CmdTheme(const cli::Args& a)
{
    if (a.pos.size() < 2)
    {
        std::fprintf(stderr, "usage: sdctl theme <dir|theme.json> [--device S]\n");
        return 2;
    }
    auto loaded = LoadTheme(a.pos[1]);
    for (auto& w : loaded.warnings)
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    if (!loaded.theme)
    {
        std::fprintf(stderr, "error: %s\n", loaded.error.c_str());
        return 2;
    }
    auto dev = cli::OpenOrExplain(a);
    if (!dev)
        return 1;
    const auto r = ApplyTheme(*dev, *loaded.theme, !a.Has("no-hide"));
    for (auto& w : r.warnings)
        std::fprintf(stderr, "warning: %s\n", w.c_str());
    std::printf("theme \"%s\" on %s (%s)\n", loaded.theme->name.c_str(), std::string(dev->GetModel().name).c_str(),
                dev->Serial().c_str());
    std::printf("  keys uploaded : %zu\n  reports       : %zu (%.1f KiB)\n", r.keys, r.reports, r.bytes / 1024.0);
    std::printf("  encode        : %.1f ms\n  upload        : %.1f ms\n  total         : %.1f ms\n",
                r.encode.count() / 1000.0, r.upload.count() / 1000.0, r.total.count() / 1000.0);
    return 0;
}

int CmdEvents(const cli::Args& a)
{
    auto dev = cli::OpenOrExplain(a);
    if (!dev)
        return 1;
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
    const double seconds = a.Dbl("seconds", 0);
    const bool asJson = a.Has("json");
    const auto t0 = NowUs();
    std::size_t total = 0, reads = 0;
    std::map<std::string, std::size_t> counts;
    if (!asJson)
        std::fprintf(stderr, "listening on %s (%s)%s ... Ctrl-C to stop\n", std::string(dev->GetModel().name).c_str(),
                     dev->Serial().c_str(), seconds > 0 ? sdb::Format(" for {}s", seconds).c_str() : "");
    if (a.Has("raw"))
    {  // every input report as hex: shows what the firmware really sends (e.g. drag points)
        std::vector<std::uint8_t> buf(1024);
        std::size_t n = 0;
        while (!stopRequested && (seconds <= 0 || (NowUs() - t0) < seconds * 1e6))
        {
            const int r = dev->GetTransport().Read(buf, 50);
            if (r <= 0)
                continue;
            ++n;
            int last = r - 1;
            while (last > 0 && buf[last] == 0)
                --last;  // trim trailing zero padding
            std::printf("%9.1f ms  len=%3d  ", (NowUs() - t0) / 1000.0, r);
            for (int i = 0; i <= last; ++i)
                std::printf("%02X ", buf[i]);
            std::printf("\n");
            std::fflush(stdout);
        }
        std::fprintf(stderr, "%zu raw reports\n", n);
        return 0;
    }
    while (!stopRequested && (seconds <= 0 || (NowUs() - t0) < seconds * 1e6))
    {
        ++reads;
        for (const auto& e : dev->PollEvents(50))
        {
            ++total;
            ++counts[ToString(e.type)];
            const double ms = (e.tUs - t0) / 1000.0;
            if (asJson)
                std::cout << json{{"t_ms", ms},
                                  {"source", ToString(e.source)},
                                  {"type", ToString(e.type)},
                                  {"index", e.index},
                                  {"delta", e.delta},
                                  {"x", e.x},
                                  {"y", e.y},
                                  {"x2", e.x2},
                                  {"y2", e.y2}}
                                 .dump()
                          << std::endl;
            else
                std::printf("%9.1f ms  %s  %s\n", ms, ToString(e.source), Describe(e).c_str());
        }
    }
    std::fprintf(stderr, "\n%zu events, %zu read calls, %.1f s\n", total, reads, (NowUs() - t0) / 1e6);
    for (auto& [k, v] : counts)
        std::fprintf(stderr, "  %-16s %zu\n", k.c_str(), v);
    return 0;
}

// Reads every getter feature report the Elgato docs list and prints raw bytes + a decoding.
int CmdProbe(const cli::Args& a)
{
    auto dev = cli::OpenOrExplain(a);
    if (!dev)
        return 1;
    const Model& m = dev->GetModel();
    const bool legacy = m.family == Family::V1;
    struct Q
    {
        std::uint8_t id;
        const char* what;
    };
    const std::vector<Q> qs =
        legacy ? std::vector<Q>{{0xA0, "firmware LD"},
                                {0xA1, "firmware AP2"},
                                {0xA2, "firmware AP1"},
                                {0x03, "serial"},
                                {0xA3, "sleep duration"}}
               : std::vector<Q>{{0x04, "firmware LD"}, {0x05, "firmware AP2 (primary)"}, {0x07, "firmware AP1"},
                                {0x06, "serial"},      {0x08, "unit information"},       {0x0A, "sleep duration"}};
    for (const auto& q : qs)
    {
        std::vector<std::uint8_t> buf(33, 0);
        buf[0] = q.id;
        const int n = dev->GetTransport().GetFeature(buf);
        std::printf("0x%02X %-24s n=%-3d ", q.id, q.what, n);
        int last = std::max(0, n) - 1;
        while (last > 0 && buf[last] == 0)
            --last;
        for (int i = 0; i <= last; ++i)
            std::printf("%02X ", buf[i]);
        std::string ascii;
        for (int i = 1; i <= last; ++i)
            ascii += (buf[i] >= 0x20 && buf[i] < 0x7f) ? static_cast<char>(buf[i]) : '.';
        std::printf(" |%s|\n", ascii.c_str());
        if (!legacy && q.id == 0x08 && n >= 17)
        {
            auto u16 = [&](int o) { return buf[o] | (buf[o + 1] << 8); };
            std::printf(
                "     rows %d cols %d key %dx%d lcd %dx%d bpp %d scheme %d gallery keys %d lcd %d demo frames %d\n",
                buf[1], buf[2], u16(3), u16(5), u16(7), u16(9), buf[11], buf[12], buf[13], buf[14], buf[15]);
        }
        if (q.id == 0x0A || q.id == 0xA3)
        {
            const std::int32_t v = buf[2] | (buf[3] << 8) | (buf[4] << 16) | (static_cast<std::uint32_t>(buf[5]) << 24);
            std::printf("     sleep after %d s\n", v);
        }
    }
    return 0;
}

int Usage()
{
    std::puts(
        "sdctl info [--json]\nsdctl theme <dir|theme.json> [--device S]\nsdctl events [--device S] [--seconds N] "
        "[--json] [--raw]\n"
        "sdctl set-key (--key N | --row R --col C) (--image F | --color RRGGBB) [--text T]\nsdctl set-lcd (--text T | "
        "--image F) [--dial N] [--fg RRGGBB --bg RRGGBB --scale N --align left|center]\nsdctl set-grid --image F "
        "[--rows 0-3 --cols 0-8 --fit contain|cover|stretch --bg RRGGBB --gap N]\nsdctl lcd-scene "
        "<splash|balance|loading|status|volume|ttd|config|marquee|registers|disasm|memory|callstack> [params] "
        "[--animate S --fps N] [--out f.bmp]\nsdctl set-panel --image F [--fit cover|contain|stretch] [--area "
        "keys|all|panel] [--rows 0-2 --cols 0-3] [--no-trim]   (whole panel incl. strip, one image)\nsdctl sleep "
        "[seconds] | probe | hid-test [--only N]\nsdctl brightness <0-100> | clear | reset\nsdctl lcd-test "
        "<orient|tiles|sweep> [...]\nsdctl bench <keys|matrix|lcd|feature|mixed> [...]\n"
        "sdctl ws <ws://host:port/path> [message ...] [--token T] [--seconds N] [--count N]   (WebSocket check)\n"
        "sdctl viewer [--port N] [--deck SERIAL | --own MODEL] [--seconds N]   (join unreal-streamdeck as a viewer)\n"
        "sdctl decks [--api PORT] [--json]   (unreal-streamdeck's logical decks)\n"
        "sdctl packs [--api PORT] | sdctl pack [NAME|scenario] [--deck SERIAL] [--api PORT]   (the icon packs; a "
        "deck's pack)\n"
        "Stream Deck Studio (any command: --device emu[:host:port] = the studio only, --mirror [host:port] = real deck "
        "+ studio,\n"
        "  the default when the studio runs; --no-mirror = the real deck alone):\n"
        "sdctl emu-model <name|pid> | emu-snapshot --out f.png | emu-record (--out f.mp4|.mov|.mkv|.webm|.gif [--fps "
        "N] | --stop)   [--emu host:port]   (a studio with --listen: device wire v1)\n"
        "sdctl emu-snapshot --out f.png --api PORT [--deck SERIAL]   (the deck's viewers through unreal-streamdeck: "
        "wire v2)");
    return 2;
}
}  // namespace

namespace
{
std::string DaemonUrl(const cli::Args& a)
{
    return "http://127.0.0.1:" + std::to_string(a.Num("api", 37053));
}

// sdctl decks [--api PORT] [--json]: the daemon's logical decks (GET /api/v1/decks)
int CmdDecks(const cli::Args& a)
{
    const auto r = HttpRequest(DaemonUrl(a), "GET", "/api/v1/decks", {}, {}, 2000);
    if (!r.Ok())
    {
        std::fprintf(stderr, "error: unreal-streamdeck automation at %s: %s\n", DaemonUrl(a).c_str(),
                     r.status ? r.body.c_str() : r.error.c_str());
        return 1;
    }
    const json j = json::parse(r.body, nullptr, false);
    if (a.Has("json") || !j.is_object())
    {
        std::printf("%s\n", r.body.c_str());
        return 0;
    }
    for (const auto& d : j.value("decks", json::array()))
    {
        std::printf("%-16s %-22s %-5s %-12s brightness %3d  pack %-11s viewers %zu  %s\n",
                    d.value("serial", "").c_str(), d.value("model", "").c_str(), d.value("real", false) ? "real" : "own",
                    d.value("connected", false) ? "connected" : "unplugged", d.value("brightness", 0),
                    d.value("pack", "").c_str(), d.value("viewers", json::array()).size(),
                    d.value("scenario", "").empty() ? "(idle scenario)" : d.value("scenario", "").c_str());
    }
    if (j.value("waiting_mirrors", 0) > 0)
        std::printf("%d mirror(s) waiting for a deck\n", j.value("waiting_mirrors", 0));
    return 0;
}

// sdctl packs: the icon packs (GET /api/v1/packs); sdctl pack [NAME|scenario] [--deck S]: a deck's pack (GET / PUT
// /api/v1/decks/{serial}/pack)
int CmdPacks(const cli::Args& a)
{
    const bool one = a.pos.size() > 0 && a.pos[0] == "pack";
    const std::string deck = a.Str("deck", "first");
    const bool set = one && a.pos.size() > 1;
    const auto r = !one  ? HttpRequest(DaemonUrl(a), "GET", "/api/v1/packs", {}, {}, 2000)
                   : set ? HttpRequest(DaemonUrl(a), "PUT", "/api/v1/decks/" + deck + "/pack",
                                       json{{"pack", a.pos[1]}}.dump(), {}, 3000)
                         : HttpRequest(DaemonUrl(a), "GET", "/api/v1/decks/" + deck + "/pack", {}, {}, 2000);
    if (!r.Ok())
    {
        std::fprintf(stderr, "error: unreal-streamdeck automation at %s: %s\n", DaemonUrl(a).c_str(),
                     r.status ? r.body.c_str() : r.error.c_str());
        return 1;
    }
    const json j = json::parse(r.body, nullptr, false);
    if (a.Has("json") || !j.is_object())
        std::printf("%s\n", r.body.c_str());
    else if (!one)
        for (const auto& p : j.value("packs", json::array()))
            std::printf("%-12s %-20s %s\n", p.value("name", "").c_str(), p.value("title", "").c_str(),
                        p.value("style", "").c_str());
    else
        std::printf("deck %s draws %s (setting %s)\n", j.value("serial", "").c_str(), j.value("pack", "").c_str(),
                    j.value("setting", "").c_str());
    return 0;
}

// emu-snapshot through the daemon (device wire v2): the deck's viewers (Stream Deck Studio windows) get a Control
// frame and save the PNG; this waits for the file.
int DaemonSnapshot(const cli::Args& a, const std::string& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
    const std::string deck = a.Str("deck", "first");
    const auto r = HttpRequest(DaemonUrl(a), "POST", "/api/v1/decks/" + deck + "/snapshot",
                               json{{"path", path}}.dump(), {}, 2000);
    if (!r.Ok())
    {
        std::fprintf(stderr, "error: %s\n", r.status ? r.body.c_str() : r.error.c_str());
        return 1;
    }
    for (int i = 0; i < 60; ++i)
    {
        if (std::filesystem::file_size(path, ec) > 0 && !ec)
        {
            std::printf("saved %s\n", path.c_str());
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::fprintf(stderr, "error: no viewer saved %s within 3 s\n", path.c_str());
    return 1;
}
}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
        return Usage();
    const std::string cmd = argv[1];
    const auto a = cli::Parse(argc, argv, 1);
    try
    {
        if (cmd == "ws")
            return cli::CmdWs(a);
        if (cmd == "viewer")
            return cli::CmdViewer(a);
        if (cmd == "info")
            return CmdInfo(a);
        if (cmd == "probe")
            return CmdProbe(a);
        if (cmd == "hid-test")
            return cli::CmdHidTest(a);
        if (cmd == "theme")
            return CmdTheme(a);
        if (cmd == "events")
            return CmdEvents(a);
        if (cmd == "set-key")
            return cli::CmdSetKey(a);
        if (cmd == "set-lcd")
            return cli::CmdSetLcd(a);
        if (cmd == "set-grid")
            return cli::CmdSetGrid(a);
        if (cmd == "lcd-scene")
            return cli::CmdLcdScene(a);
        if (cmd == "lcd-test")
            return cli::CmdLcdTest(a);
        if (cmd == "bench")
            return cli::CmdBench(a);
        if (cmd == "emu-model")
        {  // sdctl emu-model <name|pid> [--emu host:port]
            const Model* m = a.pos.size() >= 2 ? FindModelByName(a.pos[1]) : nullptr;
            if (!m)
            {
                std::fprintf(stderr, "error: unknown model\n");
                return 4;
            }
            std::string err;
            if (!EmulatorSetModel(a.Str("emu"), m->pid, &err))
            {
                std::fprintf(stderr, "error: %s\n", err.c_str());
                return 1;
            }
            std::printf("emulator shows %s\n", std::string(m->name).c_str());
            return 0;
        }
        if (cmd == "decks")
            return CmdDecks(a);
        if (cmd == "packs" || cmd == "pack")
            return CmdPacks(a);
        if (cmd == "emu-snapshot")
        {  // sdctl emu-snapshot --out file.png [--emu host:port] | [--api PORT [--deck SERIAL]]
            std::string err;
            const std::string out = a.Str("out", "emulator.png");
            const std::string path =
                out.front() == '/' ? out : (std::string(std::getenv("PWD") ? std::getenv("PWD") : ".") + "/" + out);
            if (a.Has("api"))
                return DaemonSnapshot(a, path);
            if (!EmulatorSnapshot(a.Str("emu"), path, &err))
            {
                std::fprintf(stderr, "error: %s\n", err.c_str());
                return 1;
            }
            std::printf("saved %s\n", path.c_str());
            return 0;
        }
        if (cmd == "emu-record")
        {  // sdctl emu-record --out file.mp4|.mov|.mkv|.webm|.gif [--fps N] | --stop  [--emu host:port]
            std::string err, result;
            std::string path;
            if (!a.Has("stop"))
            {
                const std::string out = a.Str("out", "emulator.mp4");
                path =
                    out.front() == '/' ? out : (std::string(std::getenv("PWD") ? std::getenv("PWD") : ".") + "/" + out);
            }
            if (!EmulatorRecord(a.Str("emu"), path, a.Num("fps", 0), &result, &err))
            {
                std::fprintf(stderr, "error: %s\n", err.c_str());
                return 1;
            }
            std::printf("%s\n", result.c_str());
            return 0;
        }
        if (cmd == "lcd-rect")
        {  // sdctl lcd-rect --x X --y Y --w W --h H --color RRGGBB: raw partial-window probe (physical device)
            auto dev = cli::OpenOrExplain(a);
            if (!dev)
                return 1;
            const int x = a.Num("x", 0), y = a.Num("y", 0), w = a.Num("w", 100), h = a.Num("h", 100);
            const unsigned long c = std::stoul(a.Str("color", "FF0000"), nullptr, 16);
            Rgb tile(w, h, static_cast<std::uint8_t>(c >> 16), static_cast<std::uint8_t>(c >> 8),
                     static_cast<std::uint8_t>(c));
            draw::Outline(tile, 0, 0, w, h, 3, {255, 255, 255});
            const auto st = dev->SetLcdRegion(x, y, tile);
            std::printf("rect %d,%d %dx%d: %zu reports\n", x, y, w, h, st.reports);
            return st.reports ? 0 : 1;
        }
        if (cmd == "sleep")
        {  // sdctl sleep [seconds]: show or set the idle time before the device sleeps (0 = never)
            auto dev = cli::OpenOrExplain(a);
            if (!dev)
                return 1;
            if (a.pos.size() >= 2)
            {
                const std::string v = a.pos[1];
                if (v.empty() || v.find_first_not_of("0123456789") != std::string::npos)
                {
                    std::fprintf(stderr, "error: seconds must be a number\n");
                    return 4;
                }
                if (!dev->SetSleepAfter(std::atoi(v.c_str())))
                    return 1;
            }
            if (auto s2 = dev->SleepAfter())
                std::printf("sleep after %d s%s\n", *s2, *s2 == 0 ? " (off)" : "");
            return 0;
        }
        if (cmd == "set-panel")
        {  // sdctl set-panel --image F [--fit contain|cover|stretch]: one picture over the whole panel (0x08)
            if (!a.Has("image"))
            {
                std::fprintf(stderr, "error: need --image FILE\n");
                return 2;
            }
            auto img = LoadImage(a.Str("image"));
            if (!img)
            {
                std::fprintf(stderr, "error: cannot read image\n");
                return 4;
            }
            auto fit = FitByName(a.Str("fit", "cover"));
            if (!fit)
            {
                std::fprintf(stderr, "error: --fit must be contain, cover or stretch\n");
                return 4;
            }
            auto dev = cli::OpenOrExplain(a);
            if (!dev)
                return 1;
            const Model& m = dev->GetModel();
            if (!m.HasFullScreen())
            {
                std::fprintf(stderr, "error: %s has no full-panel image command\n", std::string(m.name).c_str());
                return 3;
            }
            // --area all (default): from the top key row to the bottom of the window strip (the biggest picture);
            // --area keys: the key block only; --area panel: the whole 0x08 frame. A uniform border around the source
            // (a ZX Spectrum border) is trimmed first (--no-trim keeps it); its colour fills gaps and margins.
            const PanelLayout l = GetPanelLayout(m);
            const auto* c0 = img->At(0, 0);
            const std::uint8_t br = c0[0], bgc = c0[1], bb = c0[2];
            const Rgb src = a.Has("no-trim") ? *img : TrimBorder(*img);
            const std::string area = a.Str("area", "keys");
            if (area != "all" && area != "keys" && area != "panel")
            {
                std::fprintf(stderr, "error: --area must be all, keys or panel\n");
                return 4;
            }
            Rgb panel(m.screenLogicalW, m.screenLogicalH, br, bgc, bb);
            int x0 = 0, y0 = 0, x1 = m.screenLogicalW, y1 = m.screenLogicalH;
            if (area != "panel" && !l.colX.empty())
            {
                // --rows A-B --cols C-D: fit into the physical rectangle of that key block (a 4x3 block is ~4:3)
                int r0 = 0, r1 = m.rows - 1, k0 = 0, k1 = m.cols - 1;
                auto range = [](const std::string& t, int& lo, int& hi) {
                    const auto d = t.find('-');
                    try
                    {
                        lo = std::stoi(t.substr(0, d));
                        hi = d == std::string::npos ? lo : std::stoi(t.substr(d + 1));
                    }
                    catch (...)
                    {
                        return false;
                    }
                    return true;
                };
                if ((a.Has("rows") && !range(a.Str("rows"), r0, r1)) ||
                    (a.Has("cols") && !range(a.Str("cols"), k0, k1)) || r0 < 0 || k0 < 0 || r0 > r1 || k0 > k1 ||
                    r1 >= m.rows || k1 >= m.cols)
                {
                    std::fprintf(stderr, "error: block outside the %dx%d keys\n", m.rows, m.cols);
                    return 3;
                }
                x0 = l.colX[static_cast<size_t>(k0)];
                y0 = l.rowY[static_cast<size_t>(r0)];
                x1 = l.colX[static_cast<size_t>(k1)] + l.key;
                y1 = l.rowY[static_cast<size_t>(r1)] + l.key;
                if (area == "all" && l.stripW > 0 && !a.Has("rows"))
                {
                    x0 = std::min(x0, l.stripX);
                    x1 = std::max(x1, l.stripX + l.stripW);
                    y1 = l.stripY + l.stripH;
                }
            }
            Blit(panel, FitImage(src, x1 - x0, y1 - y0, *fit, br, bgc, bb), x0, y0);
            const auto st = dev->SetFullScreen(panel);
            std::printf("panel %dx%d: %zu reports, %.1f ms\n", m.screenLogicalW, m.screenLogicalH, st.reports,
                        st.elapsed.count() / 1e6);
            return st.reports ? 0 : 1;
        }
        if (cmd == "brightness" || cmd == "clear" || cmd == "reset")
        {
            auto dev = cli::OpenOrExplain(a);
            if (!dev)
                return 1;
            if (cmd == "brightness")
            {
                if (a.pos.size() < 2)
                    return Usage();
                return dev->SetBrightness(std::atoi(a.pos[1].c_str())) ? 0 : 1;
            }
            if (cmd == "reset")
                return dev->Reset() ? 0 : 1;
            dev->ClearAllKeys();
            if (dev->GetModel().HasLcd())
                (void)dev->SetLcdFull(Rgb(dev->GetModel().lcdLogicalW, dev->GetModel().lcdLogicalH));
            return 0;
        }
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return Usage();
}
