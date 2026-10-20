// Single-shot content commands. Everything is validated BEFORE the device is opened; on any problem nothing is
// written and the exit code is non-zero (see cli::Exit).
//   sdctl set-key (--key N | --row R --col C) (--image FILE | --color RRGGBB) [--text TEXT] [--fg RRGGBB] [--device S]
//   sdctl set-lcd (--text TEXT | --image FILE) [--dial N] [--fg RRGGBB] [--bg RRGGBB] [--scale N] [--align left|center]
//                 [--device S]
// --dial N limits the write to dial N's 200-px tile (region write, other tiles untouched). Without it the whole strip
// is replaced.
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>

#include "cli.h"
#include "font5x7.h"
#include "sdb/icons.h"
#include "sdb/svg.h"
#include "sdb/theme.h"

using namespace sdb;

namespace cli
{
namespace
{

std::optional<std::array<std::uint8_t, 3>> ParseColor(std::string s)
{
    if (!s.empty() && s[0] == '#')
        s.erase(0, 1);
    if (s.size() != 6 || s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return std::nullopt;
    const auto v = std::stoul(s, nullptr, 16);
    return std::array<std::uint8_t, 3>{static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8),
                                       static_cast<std::uint8_t>(v)};
}

// "auto" = colour of the image's top-left pixel (a ZX Spectrum frame's border), else RRGGBB.
std::optional<std::array<std::uint8_t, 3>> ParseBg(const std::string& s, const Rgb& src)
{
    if (s == "auto")
    {
        const auto* p = src.At(0, 0);
        return std::array<std::uint8_t, 3>{p[0], p[1], p[2]};
    }
    return ParseColor(s);
}

int Fail(Exit code, const char* msg)
{
    std::fprintf(stderr, "error: %s\n", msg);
    return code;
}

// Resolves which device a command targets without opening it (so validation never touches hardware).
std::optional<Found> Pick(const Args& a)
{
    return FindTarget(a);
}

// Draws one line of text fitted into w x h; scale 0 = largest that fits.
Rgb RenderText(int w, int h, const std::string& text, int scale, bool center, std::array<std::uint8_t, 3> fg,
               std::array<std::uint8_t, 3> bg)
{
    Rgb im(w, h, bg[0], bg[1], bg[2]);
    const int margin = 4;
    const int chars = static_cast<int>(text.size());
    int s = scale;
    if (s <= 0)
        s = std::max(1, std::min((w - 2 * margin) / (6 * chars), (h - 2 * margin) / 7));
    const int tw = chars * 6 * s - s, th = 7 * s;
    DrawText(im, center ? std::max(margin, (w - tw) / 2) : margin, std::max(0, (h - th) / 2), text, s, fg[0], fg[1],
             fg[2]);
    return im;
}
}  // namespace

int CmdSetKey(const Args& a)
{
    auto dev = Pick(a);
    if (!dev)
        return Fail(kNoDevice, "no matching Stream Deck found");
    const Model& m = *dev->model;
    if (!m.HasKeyScreens())
        return Fail(kBadCoordinates, "this model has no key screens");

    // 1) coordinates
    int key = -1;
    if (a.Has("key"))
    {
        key = a.Num("key", -1);
        if (a.Str("key").find_first_not_of("0123456789") != std::string::npos || key < 0 || key >= m.keys)
        {
            std::fprintf(stderr, "error: key %s out of range 0..%d\n", a.Str("key").c_str(), m.keys - 1);
            return kBadCoordinates;
        }
    }
    else if (a.Has("row") && a.Has("col"))
    {
        auto k = KeyIndex(m, a.Num("row", -1), a.Num("col", -1));
        if (!k)
        {
            std::fprintf(stderr, "error: row %s col %s outside %dx%d grid\n", a.Str("row").c_str(),
                         a.Str("col").c_str(), m.rows, m.cols);
            return kBadCoordinates;
        }
        key = *k;
    }
    else
    {
        return Fail(kBadUsage, "need --key N or --row R --col C");
    }

    // 2) content
    const bool haveImg = a.Has("image"), haveColor = a.Has("color"), haveIcon = a.Has("icon"), haveSvg = a.Has("svg");
    if (haveImg + haveColor + haveIcon + haveSvg != 1)
        return Fail(kBadUsage, "give exactly one of --image FILE, --color RRGGBB, --icon NAME or --svg FILE");
    Rgb img;
    if (haveImg)
    {
        auto loaded = LoadImage(a.Str("image"));
        if (!loaded)
            return Fail(kBadContent, "cannot read image (need PNG/JPEG/BMP/GIF)");
        auto fit = FitByName(a.Str("fit", "contain"));
        auto fbg = ParseBg(a.Str("fit-bg", "000000"), *loaded);
        if (!fit)
            return Fail(kBadContent, "--fit must be contain, cover or stretch");
        if (!fbg)
            return Fail(kBadContent, "bad --fit-bg, expected RRGGBB or auto");
        img = FitImage(*loaded, 144, 144, *fit, (*fbg)[0], (*fbg)[1],
                       (*fbg)[2]);  // keeps the aspect ratio unless --fit stretch
    }
    else if (haveColor)
    {
        auto c = ParseColor(a.Str("color"));
        if (!c)
            return Fail(kBadContent, "bad --color, expected RRGGBB");
        img = Rgb(m.key.w, m.key.h, (*c)[0], (*c)[1], (*c)[2]);
    }
    else if (haveSvg)
    {  // e.g. unreal-ng resources/icons/tape.svg, recoloured to --fg
        auto bg = ParseColor(a.Str("bg", "10101C")), fgc = ParseColor(a.Str("fg", "FFFFFF"));
        if (!bg || !fgc)
            return Fail(kBadContent, "bad --bg/--fg, expected RRGGBB");
        const bool label = a.Has("text");
        const int sz = label ? 96 : 120;
        img = Rgb(144, 144, (*bg)[0], (*bg)[1], (*bg)[2]);
        if (!DrawSvg(img, a.Str("svg"), (144 - sz) / 2, label ? 4 : 12, sz, {(*fgc)[0], (*fgc)[1], (*fgc)[2]}, 0.08f))
            return Fail(kBadContent, "cannot render --svg (missing file, parse error, or nothing drawable)");
    }
    else
    {
        auto ic = IconByName(a.Str("icon"));
        if (!ic)
        {
            std::string names;
            for (auto& n : IconNames())
                names += n + " ";
            std::fprintf(stderr, "error: unknown icon \"%s\"; available: %s\n", a.Str("icon").c_str(), names.c_str());
            return kBadContent;
        }
        auto bg = ParseColor(a.Str("bg", "10101C")), fgc = ParseColor(a.Str("fg", "FFFFFF")),
             ac = ParseColor(a.Str("accent", "FFD700"));
        if (!bg || !fgc || !ac)
            return Fail(kBadContent, "bad --bg/--fg/--accent, expected RRGGBB");
        img = Rgb(144, 144, (*bg)[0], (*bg)[1], (*bg)[2]);
        const int sz = a.Has("text") ? 92 : 112;
        DrawIcon(img, *ic, (144 - sz) / 2, a.Has("text") ? 8 : 16, sz, {(*fgc)[0], (*fgc)[1], (*fgc)[2]},
                 {(*ac)[0], (*ac)[1], (*ac)[2]}, a.Num("frame", 0));
    }
    if (a.Has("text"))
    {
        const auto text = a.Str("text");
        auto fg = ParseColor(a.Str("fg", "FFFFFF"));
        if (!fg)
            return Fail(kBadContent, "bad --fg, expected RRGGBB");
        if (!Renderable(text))
            return Fail(
                kBadContent,
                "text has characters the built-in font cannot draw (A-Z 0-9 space and - : . , > < / % + = ! ? ( ))");
        const int w = std::max(img.w, 1), h = std::max(img.h, 1);
        int s = std::max(1, std::min((w - 8) / (6 * static_cast<int>(text.size())), h / 7 / 2 + 1));
        if (haveIcon || haveSvg)
            s = std::min(s, 4);  // keep labels clear of the icon
        const int tw = static_cast<int>(text.size()) * 6 * s - s;
        DrawText(img, std::max(2, (w - tw) / 2), h - 7 * s - 6, text, s, (*fg)[0], (*fg)[1], (*fg)[2]);
    }

    if (!haveImg && m.keySafeInset > 0)
    {  // generated content: keep it out from under the key bezel
        const int pad = (m.keySafeInset * 144 + m.key.w / 2) / m.key.w;
        const auto* p0 = img.At(0, 0);
        img = InsetContent(img, pad, p0[0], p0[1], p0[2]);
    }
    if (a.Has("outline"))
    {  // selection frame
        auto oc = ParseColor(a.Str("outline"));
        if (!oc)
            return Fail(kBadContent, "bad --outline, expected RRGGBB");
        if (img.w != 144 || img.h != 144)
            img = FitImage(img, 144, 144, Fit::Contain);
        draw::Outline(img, 0, 0, 144, 144, 8, {(*oc)[0], (*oc)[1], (*oc)[2]});
    }
    if (a.Has("out"))
    {  // render only, no device access
        const auto bmp = EncodeBmp(img);
        std::ofstream(a.Str("out"), std::ios::binary)
            .write(reinterpret_cast<const char*>(bmp.data()), static_cast<std::streamsize>(bmp.size()));
        return kOk;
    }

    // 3) write
    auto opened = OpenDevice(*dev);
    if (!opened)
        return Fail(kDeviceError, "cannot open device (is other Stream Deck software, the Elgato app, running?)");
    const auto st = opened->SetKeyImage(key, img);
    if (st.reports == 0)
        return Fail(kDeviceError, "write failed");
    std::printf("key %d (row %d col %d) updated: %zu reports, %.1f ms\n", key, key / m.cols, key % m.cols, st.reports,
                st.elapsed.count() / 1e6);
    return kOk;
}

int CmdSetLcd(const Args& a)
{
    auto dev = Pick(a);
    if (!dev)
        return Fail(kNoDevice, "no matching Stream Deck found");
    const Model& m = *dev->model;
    if (!m.HasLcd())
        return Fail(kBadCoordinates, "this model has no horizontal display");

    // 1) target area
    int dial = -1;
    int areaW = m.lcdLogicalW;
    if (a.Has("dial"))
    {
        dial = a.Num("dial", -1);
        const int tiles = m.lcdKind == LcdKind::Region ? m.lcdLogicalW / m.dialTileW : 0;
        if (a.Str("dial").find_first_not_of("0123456789") != std::string::npos || dial < 0 || dial >= tiles)
        {
            std::fprintf(stderr, "error: dial %s out of range (this display has %d tiles)\n", a.Str("dial").c_str(),
                         tiles);
            return kBadCoordinates;
        }
        areaW = m.dialTileW;
    }
    const int areaH = m.lcdLogicalH;

    // 2) content
    const bool haveImg = a.Has("image"), haveText = a.Has("text");
    if (haveImg == haveText)
        return Fail(kBadUsage, "give exactly one of --text TEXT or --image FILE");
    Rgb content;
    if (haveImg)
    {
        auto loaded = LoadImage(a.Str("image"));
        if (!loaded)
            return Fail(kBadContent, "cannot read image (need PNG/JPEG/BMP/GIF)");
        auto fit = FitByName(a.Str("fit", "contain"));
        auto fbg = ParseColor(a.Str("fit-bg", "000000"));
        if (!fit)
            return Fail(kBadContent, "--fit must be contain, cover or stretch");
        if (!fbg)
            return Fail(kBadContent, "bad --fit-bg, expected RRGGBB");
        content = FitImage(*loaded, areaW, areaH, *fit, (*fbg)[0], (*fbg)[1], (*fbg)[2]);
    }
    else
    {
        const auto text = a.Str("text");
        auto fg = ParseColor(a.Str("fg", "FFFFFF"));
        auto bg = ParseColor(a.Str("bg", "000000"));
        if (!fg || !bg)
            return Fail(kBadContent, "bad --fg/--bg, expected RRGGBB");
        if (!Renderable(text))
            return Fail(
                kBadContent,
                "text has characters the built-in font cannot draw (A-Z 0-9 space and - : . , > < / % + = ! ? ( ))");
        const int scale = a.Num("scale", 0);
        if (scale < 0 || scale > 20)
            return Fail(kBadContent, "--scale must be 1..20 (or omitted for auto-fit)");
        const bool center = a.Str("align", "center") != "left";
        if (a.Str("align", "center") != "left" && a.Str("align", "center") != "center")
            return Fail(kBadUsage, "--align must be left or center");
        if (scale > 0 && static_cast<int>(text.size()) * 6 * scale - scale > areaW)
            return Fail(kBadContent, "text too wide for the target area at this --scale");
        if (scale == 0 && static_cast<int>(text.size()) * 6 > areaW)
            return Fail(kBadContent, "text too long for the target area even at scale 1");
        content = RenderText(areaW, areaH, text, scale, center, *fg, *bg);
    }

    // 3) write
    auto opened = OpenDevice(*dev);
    if (!opened)
        return Fail(kDeviceError, "cannot open device (is other Stream Deck software, the Elgato app, running?)");
    const auto st = dial >= 0 ? opened->SetLcdTile(dial, content) : opened->SetLcdFull(content);
    if (st.reports == 0)
        return Fail(kDeviceError, "write failed");
    std::printf("display %s updated: %zu reports, %.1f ms\n",
                dial >= 0 ? ("tile " + std::to_string(dial)).c_str() : "full strip", st.reports,
                st.elapsed.count() / 1e6);
    return kOk;
}

namespace
{
// "2" -> [2,2]; "1-3" -> [1,3]. Returns false on malformed text.
bool ParseRange(const std::string& s, int& lo, int& hi)
{
    if (s.empty())
        return false;
    const auto dash = s.find('-');
    const std::string a = s.substr(0, dash), b = dash == std::string::npos ? a : s.substr(dash + 1);
    auto digits = [](const std::string& t) {
        return !t.empty() && t.size() < 6 && t.find_first_not_of("0123456789") == std::string::npos;
    };
    if (!digits(a) || !digits(b))
        return false;
    lo = std::atoi(a.c_str());
    hi = std::atoi(b.c_str());
    return true;
}
}  // namespace

// Spreads ONE image over a block of keys, keeping its aspect ratio (default --fit contain = letterbox).
int CmdSetGrid(const Args& a)
{
    auto dev = Pick(a);
    if (!dev)
        return Fail(kNoDevice, "no matching Stream Deck found");
    const Model& m = *dev->model;
    if (!m.HasKeyScreens())
        return Fail(kBadCoordinates, "this model has no key screens");

    int r0 = 0, r1 = m.rows - 1, c0 = 0, c1 = m.cols - 1;
    if (a.Has("rows") && !ParseRange(a.Str("rows"), r0, r1))
        return Fail(kBadUsage, "--rows must look like 2 or 0-3");
    if (a.Has("cols") && !ParseRange(a.Str("cols"), c0, c1))
        return Fail(kBadUsage, "--cols must look like 4 or 0-8");
    if (r0 > r1 || c0 > c1 || r1 >= m.rows || c1 >= m.cols)
    {
        std::fprintf(stderr, "error: block rows %d-%d cols %d-%d outside the %dx%d grid\n", r0, r1, c0, c1, m.rows,
                     m.cols);
        return kBadCoordinates;
    }
    if (!a.Has("image"))
        return Fail(kBadUsage, "need --image FILE");
    auto src = LoadImage(a.Str("image"));
    if (!src)
        return Fail(kBadContent, "cannot read image (need PNG/JPEG/BMP/GIF)");
    auto fit = FitByName(a.Str("fit", "contain"));
    auto bg = ParseBg(a.Str("bg", "000000"), *src);
    if (!fit)
        return Fail(kBadContent, "--fit must be contain, cover or stretch");
    if (!bg)
        return Fail(kBadContent, "bad --bg, expected RRGGBB or auto");
    const int gap = a.Num("gap", 0);  // virtual bezel in source pixels between keys, 0 = seamless
    if (gap < 0 || gap > 40)
        return Fail(kBadContent, "--gap must be 0..40");

    GridSpec spec;
    spec.r0 = r0;
    spec.r1 = r1;
    spec.c0 = c0;
    spec.c1 = c1;
    spec.fit = *fit;
    spec.gap = gap;
    for (int i = 0; i < 3; ++i)
        spec.bg[i] = (*bg)[i];
    Rgb canvas;
    const auto tiles = SliceGrid(m, *src, spec, &canvas);  // aspect ratio preserved inside the block canvas
    const int nr = r1 - r0 + 1, nc = c1 - c0 + 1;
    std::vector<std::pair<int, std::vector<std::uint8_t>>> frames;
    for (const auto& [key, tile] : tiles)
        frames.emplace_back(key, EncodeForFormat(tile, m.key));

    if (a.Has("out"))
    {  // preview of the composed canvas, no device
        const auto bmp = EncodeBmp(canvas);
        std::ofstream(a.Str("out"), std::ios::binary)
            .write(reinterpret_cast<const char*>(bmp.data()), static_cast<std::streamsize>(bmp.size()));
        return kOk;
    }
    auto opened = OpenDevice(*dev);
    if (!opened)
        return Fail(kDeviceError, "cannot open device (is other Stream Deck software, the Elgato app, running?)");
    std::size_t reports = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (auto& [key, bytes] : frames)
    {
        const auto st = opened->SetKeyRaw(key, bytes);
        if (st.reports == 0)
            return Fail(kDeviceError, "write failed");
        reports += st.reports;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("image %dx%d spread over %d keys (rows %d-%d, cols %d-%d), fit=%s: %zu reports, %.1f ms\n", src->w,
                src->h, nr * nc, r0, r1, c0, c1, a.Str("fit", "contain").c_str(), reports, ms);
    return kOk;
}

}  // namespace cli
