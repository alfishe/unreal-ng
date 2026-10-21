#include "sdb/idlescenario.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sdb/image.h"
#include "sdb/scenario.h"
#include "sdb/theme.h"

namespace sdb::idle
{

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{
constexpr double kBannerMin = 0.6;  // the banner on a strip is kept when it is at least this tall a strip, else logo

// What the idle screen takes from theme.json: file names (relative to the theme, fragments kept), no pictures.
struct ThemeRefs
{
    int brightness = 60;
    std::vector<std::pair<int, std::string>> keys;  // by key index
    std::string lcd, logo;  // grids and lcd_ttd are not read: no picture spread over keys but the logo, and the
                            // time-travel timeline is a strip readout, never keys (idle has no emulator either)
};

std::optional<ThemeRefs> ReadTheme(const fs::path& dir)
{
    std::ifstream in(dir / "theme.json");
    if (!in)
        return std::nullopt;
    try
    {
        const json j = json::parse(in);
        ThemeRefs t;
        t.brightness = j.value("brightness", t.brightness);
        for (const auto& k : j.value("keys", json::array()))
            t.keys.emplace_back(k.at("key").get<int>(), k.at("image").get<std::string>());
        std::sort(t.keys.begin(), t.keys.end());
        if (j.contains("lcd"))
            t.lcd = j["lcd"].value("image", "");
        if (j.contains("logo"))
            t.logo = j["logo"].value("image", "");
        return t;
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

std::string DataPath(const std::string& rel)
{
    return std::string("$data/") + kThemeDir + "/" + rel;
}

json Picture(const std::string& id, const std::string& title, int key, const Model& m, const std::string& image)
{
    return {{"id", id},
            {"type", "action"},
            {"title", title},
            {"tags", json::array({"idle"})},
            {"at", json::array({key / m.cols, key % m.cols})},
            {"face", {{"image", image}, {"fit", "contain"}, {"bg", "000000"}}},
            {"do", {{"kind", "multi"}, {"steps", json::array()}}}};  // a picture: pressing it does nothing
}

std::string Stem(const std::string& file)
{
    return fs::path(file).stem().string();
}

// The keys' rectangles in one panel picture: the published layout (gaps included, as a grid theme is drawn), else
// equal cells.
struct Cells
{
    PanelLayout l;
    explicit Cells(const Model& m) : l(GetPanelLayout(m))
    {
        if (l.colX.empty())
        {
            l.key = 144;
            for (int c = 0; c < m.cols; ++c)
                l.colX.push_back(c * l.key);
            for (int r = 0; r < m.rows; ++r)
                l.rowY.push_back(r * l.key);
        }
    }
    [[nodiscard]] double X(int c) const
    {
        return l.colX[static_cast<size_t>(c)];
    }
    [[nodiscard]] double Y(int r) const
    {
        return l.rowY[static_cast<size_t>(r)];
    }
};

// One picture (a region of a file) spread over the block of keys `g` with its aspect kept (contain): per key the
// region of the file under it, as "$data/...file#xywh=..." (parts beyond the picture come out black). `ref`: relative
// to the theme in `dir`.
std::vector<std::pair<int, std::string>> Spread(const Model& m, const fs::path& dir, const std::string& ref,
                                                const GridSpec& g)
{
    std::vector<std::pair<int, std::string>> out;
    const auto r = ParseImageRef(ref);
    if (!r)
        return out;
    const auto img = LoadImage(dir / r->file);
    if (!img)
        return out;
    const double sx = r->region ? r->x : 0, sy = r->region ? r->y : 0;
    const double sw = r->region ? r->w : img->w, sh = r->region ? r->h : img->h;
    const Cells cells(m);
    const double bx = cells.X(g.c0), by = cells.Y(g.r0);
    const double bw = cells.X(g.c1) + cells.l.key - bx, bh = cells.Y(g.r1) + cells.l.key - by;
    const double s = std::min(bw / sw, bh / sh);
    const double ox = bx + (bw - sw * s) / 2, oy = by + (bh - sh * s) / 2;
    const long side = std::lround(cells.l.key / s);
    for (int row = g.r0; row <= g.r1; ++row)
        for (int col = g.c0; col <= g.c1; ++col)
        {
            const long fx = std::lround(sx + (cells.X(col) - ox) / s), fy = std::lround(sy + (cells.Y(row) - oy) / s);
            char frag[96];
            std::snprintf(frag, sizeof frag, "#xywh=%ld,%ld,%ld,%ld", fx, fy, side, side);
            out.emplace_back(row * m.cols + col, DataPath(r->file) + frag);
        }
    return out;
}

json Base(const Model& m, int brightness)
{
    char pid[16];
    std::snprintf(pid, sizeof pid, "0x%04X", m.pid);
    return {{"format", "streamdeck-scenario"},
            {"version", scn::kFormatVersion},
            {"name", "idle"},
            {"model", pid},
            {"settings", {{"brightness", brightness}, {"navigation", {{"breadcrumbs", "none"}}}}},
            {"root", {{"id", "home"}, {"type", "menu"}, {"title", kTitle}, {"items", json::array()}}}};
}

// No data directory: the name, drawn
json Drawn(const Model& m)
{
    json doc = Base(m, 60);
    json& root = doc["root"];
    if (m.HasLcd())
        root["strip"] = {{"content", "text"}, {"text", kTitle}};
    else if (m.HasKeyScreens())
        root["items"].push_back({{"id", "title"},
                                 {"type", "action"},
                                 {"title", kTitle},
                                 {"tags", json::array({"idle"})},
                                 {"at", json::array({(m.rows - 1) / 2, (m.cols - 1) / 2})},
                                 {"face", {{"label", kTitle}, {"bg", "000000"}}},
                                 {"do", {{"kind", "multi"}, {"steps", json::array()}}}});
    return doc;
}
}  // namespace

json Document(const Model& m, const fs::path& dataRoot)
{
    const fs::path dir = dataRoot / kThemeDir;
    const auto theme = dataRoot.empty() ? std::nullopt : ReadTheme(dir);
    if (!theme)
        return Drawn(m);
    const ThemeRefs& t = *theme;
    json doc = Base(m, t.brightness);
    json& root = doc["root"];
    if (m.HasLcd() && !t.lcd.empty())
    {
        // the banner, or the logo where the banner would come out a thin band (the Neo's 248 x 58 window)
        const auto r = ParseImageRef(t.lcd);
        const auto img = r ? LoadImage(dir / r->file) : std::nullopt;
        const bool thin = img && std::min(1.0, static_cast<double>(m.lcdLogicalW) * img->h / img->w /
                                                   m.lcdLogicalH) < kBannerMin;
        root["strip"] = {{"content", "image"}, {"image", DataPath(thin && !t.logo.empty() ? t.logo : t.lcd)}};
    }
    if (!m.HasKeyScreens())
        return doc;
    json& items = root["items"];
    std::vector<bool> used(m.keys, false);
    auto put = [&](const std::string& id, const std::string& title, int key, const std::string& image) {
        if (key < 0 || key >= m.keys || used[static_cast<size_t>(key)])
            return;
        used[static_cast<size_t>(key)] = true;
        items.push_back(Picture(id, title, key, m, image));
    };

    // the deck the theme was drawn for: as designed
    const bool designed =
        std::all_of(t.keys.begin(), t.keys.end(), [&](const auto& k) { return k.first >= 0 && k.first < m.keys; }) &&
        (t.lcd.empty() || m.HasLcd());
    if (designed)
    {
        for (const auto& [key, file] : t.keys)
            put("key" + std::to_string(key), Stem(file), key, DataPath(file));
        return doc;
    }

    // any other deck: the banner on the strip, or across the middle row; the icons in order on the other keys
    if (!m.HasLcd() && !t.lcd.empty())
    {
        GridSpec row;
        row.r0 = row.r1 = (m.rows - 1) / 2;
        row.c0 = 0;
        row.c1 = m.cols - 1;
        // the logo (the banner's middle): across keys the bezel gaps hide parts of it, the larger letters survive
        const std::string& mark = t.logo.empty() ? t.lcd : t.logo;
        for (const auto& [key, ref] : Spread(m, dir, mark, row))
            put("logo-" + std::to_string(key), kTitle, key, ref);
    }
    size_t next = 0;
    for (int key = 0; key < m.keys && next < t.keys.size(); ++key)
        if (!used[static_cast<size_t>(key)])
        {
            const std::string& file = t.keys[next++].second;
            put("key" + std::to_string(key), Stem(file), key, DataPath(file));
        }
    return doc;
}

}  // namespace sdb::idle
