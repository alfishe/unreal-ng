// sdctl lcd-scene <splash|balance|loading|status|volume|ttd|config|marquee|registers|disasm|memory|callstack> [params]
// [--animate SEC --fps N] [--out FILE.bmp]
//   balance  --value -100..100                      (animate: --from A --to B)
//   loading  --name S [--kind program|bytes] [--pilot] [--disk] --progress 0..100   (animate: --from/--to; first 12% =
//   pilot tone) status   --kind ok|error|warn|paused|resumed|muted|unmuted|info|saved|reset [--text T] [--sub T] volume
//   --adapters "SPEAKERS:80,HEADSET:45:m,..." [--select N] [--tile N]    (:m = muted; --tile N redraws only dial tile
//   N) ttd      --pos P --total T [--checkpoints a,b] [--breakpoints a,b] [--hits a,b] [--reverse] [--live]   (animate:
//   --from/--to) config   --names "48K,128K,PENTAGON" --select N marquee  --text T          splash registers --regs
//   "AF:1F44,BC:0120,..." [--prev "AF:1F00,..."] [--flags 0-255]   (changed registers glow, one tile per dial) disasm
//   --lines "8000:LD A,5;8002:LD B,A;..." --pc N [--bp 8003,8010] [--tstates N]   (4 lines, hex addresses) memory
//   --addr 8000 --bytes "3E,05,..." [--hl 2,3]        4 rows x 20 bytes, highlighted = just written callstack --frames
//   "8123:PLOT,8040:DRAW,0F00:MAIN" [--current N]
// Validation first, device later: exit 2 usage, 3 bad coordinates (tile), 4 bad content/values, 5 no device, 1 device
// error.
// --out renders a single frame to a BMP and never touches the device (frame = --frame N, default last).
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>

#include "cli.h"
#include "sdb/scenes.h"
#include "sdb/svg.h"

using namespace sdb;
using namespace sdb::scenes;

namespace cli
{
namespace
{

bool ParseInt(const std::string& s, int lo, int hi, int& out)
{
    if (s.empty())
        return false;
    size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (i >= s.size() || s.find_first_not_of("0123456789", i) != std::string::npos || s.size() - i > 9)
        return false;
    out = std::atoi(s.c_str());
    return out >= lo && out <= hi;
}
std::vector<std::string> Split(const std::string& s, char sep)
{
    std::vector<std::string> v;
    std::string cur;
    std::istringstream is(s);
    while (std::getline(is, cur, sep))
        v.push_back(cur);
    return v;
}
bool ParseIntList(const std::string& s, int lo, int hi, std::vector<int>& out)
{
    out.clear();
    if (s.empty())
        return true;
    for (auto& t : Split(s, ','))
    {
        int v;
        if (!ParseInt(t, lo, hi, v))
            return false;
        out.push_back(v);
    }
    return true;
}
bool ParseHex(const std::string& s, int lo, int hi, int& out)
{
    if (s.empty() || s.size() > 4 || s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return false;
    out = static_cast<int>(std::strtol(s.c_str(), nullptr, 16));
    return out >= lo && out <= hi;
}
bool ParseHexList(const std::string& s, int hi, std::vector<int>& out)
{
    out.clear();
    if (s.empty())
        return true;
    for (auto& t : Split(s, ','))
    {
        int v;
        if (!ParseHex(t, 0, hi, v))
            return false;
        out.push_back(v);
    }
    return true;
}
int Bad(Exit c, const std::string& m)
{
    std::fprintf(stderr, "error: %s\n", m.c_str());
    return c;
}
}  // namespace

int CmdLcdScene(const Args& a)
{
    if (a.pos.size() < 2)
        return Bad(
            kBadUsage,
            "usage: sdctl lcd-scene "
            "<splash|balance|loading|status|volume|ttd|config|marquee|registers|disasm|memory|callstack> [params]");
    const std::string scene = a.pos[1];

    // ---- device geometry (without opening it)
    Size size{1200, 100};
    std::optional<Found> found = FindTarget(a);
    const bool renderOnly = a.Has("out");
    if (!renderOnly)
    {
        if (!found)
            return Bad(kNoDevice, "no matching Stream Deck found");
        if (!found->model->HasLcd())
            return Bad(kBadCoordinates, "this model has no horizontal display");
    }
    if (found && found->model->HasLcd())
        size = {found->model->lcdLogicalW, found->model->lcdLogicalH};
    if (a.Has("width") && !ParseInt(a.Str("width"), 100, 4000, size.w))
        return Bad(kBadContent, "--width must be 100..4000");
    if (a.Has("height") && !ParseInt(a.Str("height"), 20, 400, size.h))
        return Bad(kBadContent, "--height must be 20..400");
    const int tileW = found ? found->model->dialTileW : 200;

    // ---- animation settings
    double seconds = a.Dbl("animate", 0);
    const int fps = a.Num("fps", 15);
    if (a.Has("animate") && (seconds <= 0 || seconds > 600))
        return Bad(kBadContent, "--animate must be 0..600 seconds");
    if (fps < 1 || fps > 60)
        return Bad(kBadContent, "--fps must be 1..60");
    const bool animate = seconds > 0 && !renderOnly;
    auto num = [&](const char* key, int def, int lo, int hi, int& out) -> bool {
        if (!a.Has(key))
        {
            out = def;
            return true;
        }
        return ParseInt(a.Str(key), lo, hi, out);
    };
    auto badNum = [&](const char* key, int lo, int hi) {
        return Bad(kBadContent,
                   std::string("--") + key + " must be an integer " + std::to_string(lo) + ".." + std::to_string(hi));
    };
    auto checkText = [&](const std::string& t) { return draw::Renderable(t); };

    // ---- scene -> render(t in 0..1, frame)
    std::function<Rgb(double, int)> render;
    int tile = -1;
    std::vector<Adapter> adapters;
    int selected = 0;

    if (scene == "splash")
    {
        render = [=](double, int f) { return Splash(size, f); };
    }
    else if (scene == "balance")
    {
        int v = 0, from = -100, to = 100;
        if (!num("value", 0, -100, 100, v))
            return badNum("value", -100, 100);
        if (!num("from", -100, -100, 100, from))
            return badNum("from", -100, 100);
        if (!num("to", 100, -100, 100, to))
            return badNum("to", -100, 100);
        render = [=](double t, int f) {
            return Balance(size, animate ? static_cast<int>(from + (to - from) * t) : v, f);
        };
    }
    else if (scene == "loading")
    {
        Loading loading;
        loading.name = a.Str("name", "UNREAL");
        if (!checkText(loading.name))
            return Bad(kBadContent, "bad --name: only " + draw::AllowedChars());
        const auto kind = a.Str("kind", "program");
        if (kind != "program" && kind != "bytes")
            return Bad(kBadContent, "--kind must be program or bytes");
        loading.bytes = kind == "bytes";
        loading.disk = a.Has("disk");
        loading.iconSvg = a.Str("icon-svg");
        if (!loading.iconSvg.empty() && !RenderSvg(loading.iconSvg, 32, draw::kBWhite, draw::kBlack))
            return Bad(kBadContent, "cannot render --icon-svg " + loading.iconSvg);
        int p = 0, from = 0, to = 100;
        if (!num("progress", 0, 0, 100, p))
            return badNum("progress", 0, 100);
        if (!num("from", 0, 0, 100, from))
            return badNum("from", 0, 100);
        if (!num("to", 100, 0, 100, to))
            return badNum("to", 0, 100);
        const bool pilot = a.Has("pilot");
        render = [=](double t, int f) mutable {
            Loading l = loading;
            l.pilot = pilot;
            l.progress = p;
            if (animate)
            {
                l.pilot = pilot || t < 0.12;
                l.progress = l.pilot ? 0 : from + static_cast<int>((to - from) * (t - 0.12) / 0.88);
            }
            return LoadingScene(size, l, f);
        };
    }
    else if (scene == "status")
    {
        Status st;
        if (!StatusByName(a.Str("kind", "ok"), st))
            return Bad(kBadContent, "unknown --kind (ok error warn paused resumed muted unmuted info saved reset)");
        const auto title = a.Str("text"), sub = a.Str("sub");
        if ((!title.empty() && !checkText(title)) || (!sub.empty() && !checkText(sub)))
            return Bad(kBadContent, "bad --text/--sub: only " + draw::AllowedChars());
        if (title.size() > 22 || sub.size() > 40)
            return Bad(kBadContent, "--text max 22 chars, --sub max 40");
        const auto svg = a.Str("icon-svg");
        if (!svg.empty() && !RenderSvg(svg, 32, draw::kBWhite, draw::kBlack))
            return Bad(kBadContent, "cannot render --icon-svg " + svg);
        render = [=](double, int f) { return StatusScene(size, st, title, sub, f, svg); };
    }
    else if (scene == "volume")
    {
        for (auto& item :
             Split(a.Str("adapters", "SPEAKERS:80,HEADSET:45,LINE OUT:100:m,HDMI:20,USB DAC:65,VIRTUAL:0"), ','))
        {
            auto parts = Split(item, ':');
            Adapter ad;
            if (parts.size() < 2 || parts.size() > 3 || !ParseInt(parts[1], 0, 100, ad.level) || !checkText(parts[0]))
                return Bad(kBadContent, "bad --adapters item \"" + item + "\" (NAME:0-100[:m])");
            if (parts.size() == 3)
            {
                if (parts[2] != "m")
                    return Bad(kBadContent, "adapter flag must be m (muted)");
                ad.muted = true;
            }
            ad.name = parts[0];
            adapters.push_back(ad);
        }
        const int tiles = std::max(1, size.w / tileW);
        if (static_cast<int>(adapters.size()) > tiles)
            return Bad(kBadCoordinates, "more adapters (" + std::to_string(adapters.size()) + ") than dial tiles (" +
                                            std::to_string(tiles) + ")");
        if (!num("select", 0, 0, tiles - 1, selected))
            return Bad(kBadCoordinates, "--select outside the dial tiles");
        if (a.Has("tile") && !num("tile", 0, 0, tiles - 1, tile))
            return Bad(kBadCoordinates, "--tile outside the dial tiles");
        const int w = tileW;
        render = [=, &adapters](double, int f) {
            if (tile >= 0)
                return VolumeTile(w, size.h, tile < static_cast<int>(adapters.size()) ? adapters[tile] : Adapter{},
                                  tile == selected, tile, f);
            return Volume(size, adapters, selected, w, f);
        };
    }
    else if (scene == "ttd")
    {
        Ttd ttd;
        if (!num("total", 20000, 1, 100000000, ttd.total))
            return badNum("total", 1, 100000000);
        int pos = 0, from = 0, to = 0;
        if (!num("pos", 0, 0, ttd.total, pos))
            return Bad(kBadContent, "--pos must be 0..total");
        if (!num("from", 0, 0, ttd.total, from))
            return Bad(kBadContent, "--from must be 0..total");
        if (!num("to", ttd.total, 0, ttd.total, to))
            return Bad(kBadContent, "--to must be 0..total");
        if (a.Has("abs"))
        {
            int abs0 = 0;
            if (!num("abs", 0, 0, 2000000000, abs0))
                return badNum("abs", 0, 2000000000);
            ttd.absStart = abs0;
            ttd.fps = 48.83;
        }
        if (!ParseIntList(a.Str("checkpoints"), 0, ttd.total, ttd.checkpoints) ||
            !ParseIntList(a.Str("breakpoints"), 0, ttd.total, ttd.breakpoints) ||
            !ParseIntList(a.Str("hits"), 0, ttd.total, ttd.hits))
            return Bad(kBadContent, "--checkpoints/--breakpoints/--hits must be comma separated frames 0..total");
        ttd.reverse = a.Has("reverse");
        ttd.live = a.Has("live");
        render = [=](double t, int f) mutable {
            Ttd x = ttd;
            x.pos = animate ? static_cast<int>(from + (to - from) * t) : pos;
            if (animate)
                x.reverse = to < from;
            return TtdScene(size, x, f);
        };
    }
    else if (scene == "registers")
    {
        auto parseRegs = [&](const std::string& spec, std::vector<Reg>& out) {
            out.clear();
            for (auto& item : Split(spec, ','))
            {
                auto p = Split(item, ':');
                Reg r;
                if (p.size() != 2 || !checkText(p[0]) || p[0].size() > 3 || !ParseHex(p[1], 0, 0xFFFF, r.value))
                    return false;
                r.name = p[0];
                out.push_back(r);
            }
            return !out.empty();
        };
        std::vector<Reg> regs, prev;
        if (!parseRegs(a.Str("regs", "AF:1F44,BC:0120,DE:5B00,HL:8000,IX:5C3A,SP:FF58"), regs))
            return Bad(kBadContent, "bad --regs (NAME:HEX4 list, NAME max 3 chars)");
        if (a.Has("prev"))
        {
            if (!parseRegs(a.Str("prev"), prev) || prev.size() != regs.size())
                return Bad(kBadContent, "bad --prev (same registers as --regs)");
            for (size_t i = 0; i < regs.size(); ++i)
                regs[i].prev = prev[i].value;
        }
        const int tiles = std::max(1, size.w / tileW);
        if (static_cast<int>(regs.size()) > tiles)
            return Bad(kBadCoordinates, "more registers than dial tiles");
        int flags = -1;
        if (!num("flags", -1, -1, 255, flags))
            return badNum("flags", 0, 255);
        if (a.Has("tile") && !num("tile", 0, 0, tiles - 1, tile))
            return Bad(kBadCoordinates, "--tile outside the dial tiles");
        const int w = tileW;
        render = [=](double, int f) {
            Rgb full = Registers(size, regs, flags, w, f);
            return tile >= 0 ? Crop(full, tile * w, 0, w, size.h) : full;
        };
    }
    else if (scene == "disasm")
    {
        std::vector<AsmLine> lines;
        for (auto& item : Split(a.Str("lines", "8000:LD A,5;8002:LD B,A;8003:ADD A,B;8004:JR NZ,8010"), ';'))
        {
            const auto colon = item.find(':');
            AsmLine l;
            if (colon == std::string::npos || !ParseHex(item.substr(0, colon), 0, 0xFFFF, l.addr) ||
                !checkText(item.substr(colon + 1)))
                return Bad(kBadContent, "bad --lines item \"" + item + "\" (HEXADDR:MNEMONIC)");
            l.text = item.substr(colon + 1);
            lines.push_back(l);
        }
        if (lines.empty() || lines.size() > 4)
            return Bad(kBadContent, "--lines needs 1..4 entries separated by ;");
        int pc = 0, ts = -1;
        if (!num("pc", 0, 0, static_cast<int>(lines.size()) - 1, pc))
            return Bad(kBadCoordinates, "--pc outside the lines");
        if (!num("tstates", -1, -1, 2000000000, ts))
            return badNum("tstates", 0, 2000000000);
        std::vector<int> bps;
        if (!ParseHexList(a.Str("bp"), 0xFFFF, bps))
            return Bad(kBadContent, "bad --bp (comma separated hex addresses)");
        render = [=](double, int f) { return Disasm(size, lines, pc, bps, ts, f); };
    }
    else if (scene == "memory")
    {
        int addr = 0x8000;
        if (a.Has("addr") && !ParseHex(a.Str("addr"), 0, 0xFFFF, addr))
            return Bad(kBadContent, "bad --addr (hex 0..FFFF)");
        std::vector<int> bytes, hl;
        if (!ParseHexList(a.Str("bytes", "3E,05,47,80,C3,00,80,00,00,00,41,42,43,44,45,46,47,48,49,4A"), 0xFF, bytes))
            return Bad(kBadContent, "bad --bytes (comma separated hex bytes 00..FF)");
        if (bytes.size() > 80)
            return Bad(kBadContent, "--bytes max 80");
        if (!ParseIntList(a.Str("hl"), 0, 79, hl))
            return Bad(kBadContent, "bad --hl (byte indexes 0..79)");
        render = [=](double, int f) { return Memory(size, addr, bytes, hl, f); };
    }
    else if (scene == "callstack")
    {
        std::vector<Frame> fr;
        for (auto& item : Split(a.Str("frames", "8123:PLOT,8040:DRAW,0F00:MAIN"), ','))
        {
            auto p = Split(item, ':');
            Frame f;
            if (p.size() != 2 || !ParseHex(p[0], 0, 0xFFFF, f.addr) || !checkText(p[1]) || p[1].size() > 10)
                return Bad(kBadContent, "bad --frames item \"" + item + "\" (HEXADDR:NAME, NAME max 10)");
            f.name = p[1];
            fr.push_back(f);
        }
        if (fr.empty() || fr.size() > 5)
            return Bad(kBadContent, "--frames needs 1..5 entries");
        int cur = 0;
        if (!num("current", 0, 0, static_cast<int>(fr.size()) - 1, cur))
            return Bad(kBadCoordinates, "--current outside the frames");
        render = [=](double, int f) { return CallStack(size, fr, cur, f); };
    }
    else if (scene == "ttd-live")
    {
        const bool paused = a.Has("paused");
        render = [=](double, int f) { return TtdLive(size, paused, f); };
    }
    else if (scene == "ttd-rec")
    {
        int from = 416155, len = 0;
        if (!num("from", 416155, 0, 2000000000, from))
            return badNum("from", 0, 2000000000);
        if (!num("frames", 5658, 0, 100000000, len))
            return badNum("frames", 0, 100000000);
        // animated: the end keeps counting at Pentagon rate, like a live recording
        render = [=](double t, int f) {
            return TtdRecording(size, from, from + len + static_cast<long long>(animate ? t * seconds * 48.83 : 0),
                                48.83, f);
        };
    }
    else if (scene == "config")
    {
        auto names = Split(a.Str("names", "48K,128K,PENTAGON,SCORPION,+3"), ',');
        for (auto& n : names)
            if (!checkText(n) || n.size() > 12)
                return Bad(kBadContent, "bad config name \"" + n + "\" (max 12 chars, " + draw::AllowedChars() + ")");
        if (!num("select", 0, 0, static_cast<int>(names.size()) - 1, selected))
            return Bad(kBadCoordinates, "--select outside the name list");
        render = [=](double, int f) { return Configs(size, names, selected, f); };
    }
    else if (scene == "marquee")
    {
        const auto t = a.Str("text", "UNREAL NG READY");
        if (!checkText(t) || t.size() > 80)
            return Bad(kBadContent, "bad --text: only " + draw::AllowedChars() + " (max 80)");
        render = [=](double, int f) { return Marquee(size, t, f); };
    }
    else
    {
        return Bad(kBadUsage, "unknown scene \"" + scene + "\"");
    }

    // ---- render only
    if (renderOnly)
    {
        const int frame = a.Num("frame", 20);
        const auto bmp = EncodeBmp(render(1.0, frame));
        std::ofstream(a.Str("out"), std::ios::binary)
            .write(reinterpret_cast<const char*>(bmp.data()), static_cast<std::streamsize>(bmp.size()));
        return kOk;
    }

    // ---- device
    auto dev = OpenDevice(*found);
    if (!dev)
        return Bad(kDeviceError, "cannot open device (is other Stream Deck software, the Elgato app, running?)");
    auto draw1 = [&](double t, int f) {
        const Rgb im = render(t, f);
        return tile >= 0 ? dev->SetLcdTile(tile, im) : dev->SetLcdFull(im);
    };
    if (!animate)
    {
        const auto st = draw1(1.0, a.Num("frame", 20));
        if (st.reports == 0)
            return Bad(kDeviceError, "write failed");
        std::printf("%s shown: %zu reports, %.1f ms\n", scene.c_str(), st.reports, st.elapsed.count() / 1e6);
        return kOk;
    }
    using clk = std::chrono::steady_clock;
    const int frames = std::max(2, static_cast<int>(seconds * fps));
    const auto period = std::chrono::duration_cast<clk::duration>(std::chrono::duration<double>(1.0 / fps));
    const auto t0 = clk::now();
    auto next = t0;
    int late = 0;
    for (int i = 0; i < frames; ++i)
    {
        if (draw1(static_cast<double>(i) / (frames - 1), i).reports == 0)
            return Bad(kDeviceError, "write failed");
        next += period;
        if (clk::now() > next)
            ++late;
        else
            std::this_thread::sleep_until(next);
    }
    const double el = std::chrono::duration<double>(clk::now() - t0).count();
    std::printf("%s: %d frames in %.2f s = %.1f fps (target %d, %d late)\n", scene.c_str(), frames, el, frames / el,
                fps, late);
    return kOk;
}

}  // namespace cli
