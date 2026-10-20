#include "sdb/theme.h"

#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

#include "sdb/draw.h"

namespace sdb
{
using json = nlohmann::json;
namespace fs = std::filesystem;

bool GridInRange(const Model& m, const GridSpec& g)
{
    return g.r0 >= 0 && g.c0 >= 0 && g.r0 <= g.r1 && g.c0 <= g.c1 && g.r1 < m.rows && g.c1 < m.cols;
}

std::vector<std::pair<int, Rgb>> SliceGrid(const Model& m, const Rgb& src, const GridSpec& g, Rgb* canvasOut)
{
    constexpr int cell = 144;
    const int nr = g.r1 - g.r0 + 1, nc = g.c1 - g.c0 + 1;
    std::vector<std::pair<int, Rgb>> out;
    const PanelLayout l = GetPanelLayout(m);
    if (!l.colX.empty())
    {
        // Physical geometry (Elgato pixel layout): the picture covers the block's real rectangle, gaps included, and
        // every key shows exactly the part that lies under it - the gaps are hidden by the bezel, so it stays
        // continuous.
        const int x0 = l.colX[static_cast<size_t>(g.c0)], y0 = l.rowY[static_cast<size_t>(g.r0)];
        const int x1 = l.colX[static_cast<size_t>(g.c1)] + l.key, y1 = l.rowY[static_cast<size_t>(g.r1)] + l.key;
        const Rgb canvas = FitImage(src, x1 - x0, y1 - y0, g.fit, g.bg[0], g.bg[1], g.bg[2]);
        for (int r = g.r0; r <= g.r1; ++r)
            for (int c = g.c0; c <= g.c1; ++c)
            {
                Rgb tile = Crop(canvas, l.colX[static_cast<size_t>(c)] - x0, l.rowY[static_cast<size_t>(r)] - y0, l.key,
                                l.key);
                if (g.gap > 0)
                {
                    draw::FillRect(tile, 0, 0, l.key, g.gap, {});
                    draw::FillRect(tile, 0, l.key - g.gap, l.key, g.gap, {});
                    draw::FillRect(tile, 0, 0, g.gap, l.key, {});
                    draw::FillRect(tile, l.key - g.gap, 0, g.gap, l.key, {});
                }
                out.emplace_back(r * m.cols + c, std::move(tile));
            }
        if (canvasOut)
            *canvasOut = canvas;
        return out;
    }
    // No published layout (e.g. Original): equal cells.
    const Rgb canvas = FitImage(src, nc * cell, nr * cell, g.fit, g.bg[0], g.bg[1], g.bg[2]);
    for (int r = 0; r < nr; ++r)
        for (int c = 0; c < nc; ++c)
        {
            Rgb tile = Crop(canvas, c * cell, r * cell, cell, cell);
            if (g.gap > 0)
            {
                draw::FillRect(tile, 0, 0, cell, g.gap, {});
                draw::FillRect(tile, 0, cell - g.gap, cell, g.gap, {});
                draw::FillRect(tile, 0, 0, g.gap, cell, {});
                draw::FillRect(tile, cell - g.gap, 0, g.gap, cell, {});
            }
            out.emplace_back((g.r0 + r) * m.cols + g.c0 + c, std::move(tile));
        }
    if (canvasOut)
        *canvasOut = canvas;
    return out;
}

namespace
{
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

ThemeLoad LoadTheme(const fs::path& p)
{
    ThemeLoad out;
    const fs::path file = fs::is_directory(p) ? p / "theme.json" : p;
    const fs::path base = file.parent_path();
    std::ifstream in(file);
    if (!in)
    {
        out.error = "cannot open " + file.string();
        return out;
    }
    json j;
    try
    {
        j = json::parse(in);
    }
    catch (const std::exception& e)
    {
        out.error = std::string("theme.json: ") + e.what();
        return out;
    }

    Theme t;
    t.name = j.value("name", base.filename().string());
    t.brightness = j.value("brightness", -1);
    t.quality = j.value("quality", 90);
    t.clearUnlisted = j.value("clear_unlisted", true);
    auto fitOf = [&](const json& node, Fit def) {
        const std::string n = node.value("fit", std::string());
        if (n.empty())
            return def;
        if (auto f = FitByName(n))
            return *f;
        out.warnings.push_back("unknown fit \"" + n + "\", using default");
        return def;
    };
    t.fit = fitOf(j, Fit::Contain);

    auto load = [&](const std::string& rel, const char* what) -> std::optional<Rgb> {
        auto img = LoadImage(base / rel);
        if (!img)
            out.warnings.push_back(std::string("cannot load ") + what + " image: " + rel);
        return img;
    };
    try
    {
        for (auto& k : j.value("keys", json::array()))
            if (auto img = load(k.at("image").get<std::string>(), "key"))
                t.keys.push_back({k.at("key").get<int>(), std::move(*img), fitOf(k, t.fit)});
        for (auto& g : j.value("grids", json::array()))
        {
            Theme::Grid grid;
            if (!ParseRange(g.value("rows", std::string()), grid.spec.r0, grid.spec.r1) ||
                !ParseRange(g.value("cols", std::string()), grid.spec.c0, grid.spec.c1))
            {
                out.warnings.push_back("grid needs rows/cols like \"3\" or \"0-8\"");
                continue;
            }
            grid.spec.fit = fitOf(g, t.fit);
            grid.spec.gap = g.value("gap", 0);
            if (auto img = load(g.at("image").get<std::string>(), "grid"))
            {
                grid.img = std::move(*img);
                t.grids.push_back(std::move(grid));
            }
        }
        if (j.contains("lcd"))
        {
            t.lcd = load(j["lcd"].at("image").get<std::string>(), "lcd");
            t.lcdFit = fitOf(j["lcd"], t.fit);
        }
        for (auto& k : j.value("tiles", json::array()))
            if (auto img = load(k.at("image").get<std::string>(), "tile"))
                t.tiles.push_back({k.at("dial").get<int>(), std::move(*img), fitOf(k, t.fit)});
        for (auto& l : j.value("touch_leds", json::array()))
        {
            auto rgb = l.at("rgb").get<std::vector<int>>();
            if (rgb.size() == 3)
                t.leds.push_back({l.at("point").get<int>(), static_cast<std::uint8_t>(rgb[0]),
                                  static_cast<std::uint8_t>(rgb[1]), static_cast<std::uint8_t>(rgb[2])});
        }
    }
    catch (const std::exception& e)
    {
        out.error = std::string("theme.json: ") + e.what();
        return out;
    }
    out.theme = std::move(t);
    return out;
}

UploadReport ApplyTheme(Device& dev, const Theme& th, bool hide)
{
    using clk = std::chrono::steady_clock;
    UploadReport rep;
    const Model& m = dev.GetModel();
    const auto t0 = clk::now();

    // Phase 1: encode everything (no I/O).
    std::map<int, std::vector<std::uint8_t>> keyBytes;
    for (auto& k : th.keys)
    {
        if (k.key < 0 || k.key >= m.keys)
        {
            rep.warnings.push_back("key " + std::to_string(k.key) + " out of range");
            continue;
        }
        if (m.HasKeyScreens())
            keyBytes[k.key] = EncodeForFormat(FitImage(k.img, m.key.w, m.key.h, k.fit), m.key, th.quality);
    }
    for (auto& g : th.grids)
    {
        if (!m.HasKeyScreens() || !GridInRange(m, g.spec))
        {
            rep.warnings.push_back("grid outside this device's key grid");
            continue;
        }
        for (auto& [key, tile] : SliceGrid(m, g.img, g.spec))
            keyBytes[key] = EncodeForFormat(tile, m.key, th.quality);
    }
    std::vector<std::uint8_t> blank;
    if (th.clearUnlisted && m.HasKeyScreens())
        blank = EncodeForFormat(Rgb(m.key.w, m.key.h), m.key, th.quality);
    const auto t1 = clk::now();
    rep.encode = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0);

    // Phase 2: stream to the device.
    auto acc = [&](const WriteStats& s) {
        rep.reports += s.reports;
        rep.bytes += s.bytes;
    };
    if (hide && th.brightness >= 0)
        dev.SetBrightness(0);
    if (m.HasKeyScreens())
        for (int k = 0; k < m.keys; ++k)
        {
            if (auto it = keyBytes.find(k); it != keyBytes.end())
            {
                acc(dev.SetKeyRaw(k, it->second));
                ++rep.keys;
            }
            else if (!blank.empty())
                acc(dev.SetKeyRaw(k, blank));
        }
    if (m.HasLcd())
    {
        if (th.lcd)
            acc(dev.SetLcdFull(FitImage(*th.lcd, m.lcdLogicalW, m.lcdLogicalH, th.lcdFit), th.quality));
        else if (th.clearUnlisted && th.tiles.empty())
            acc(dev.SetLcdFull(Rgb(m.lcdLogicalW, m.lcdLogicalH), th.quality));
        for (auto& t : th.tiles)
            acc(dev.SetLcdTile(t.dial, FitImage(t.img, m.dialTileW, m.lcdLogicalH, t.fit), th.quality));
    }
    for (auto& l : th.leds)
        dev.SetTouchLed(l.point, l.r, l.g, l.b);
    if (th.brightness >= 0)
        dev.SetBrightness(th.brightness);
    const auto t2 = clk::now();
    rep.upload = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1);
    rep.total = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t0);
    return rep;
}

}  // namespace sdb
