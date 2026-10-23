#include "sdb/iconpacks.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <tuple>

#include "sdb/datadir.h"
#include "sdb/format.h"
#include "stb_image.h"

namespace sdb::packs
{

namespace
{
using json = nlohmann::json;

constexpr std::size_t kMaxGlyphs = 4096;  // decoded faces kept (8 packs x ~200 glyphs x a size or two)

std::optional<draw::Color> HexColor(const std::string& s)
{
    std::string h = !s.empty() && s[0] == '#' ? s.substr(1) : s;
    if (h.size() != 6 || h.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return std::nullopt;
    const auto v = std::stoul(h, nullptr, 16);
    return draw::Color{static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8),
                       static_cast<std::uint8_t>(v)};
}

std::optional<Pack> Read(const std::filesystem::path& dir)
{
    std::ifstream in(dir / "pack.json");
    if (!in)
        return std::nullopt;
    const json j = json::parse(in, nullptr, false);
    if (!j.is_object())
        return std::nullopt;
    Pack p;
    p.name = dir.filename().string();
    p.title = j.value("title", p.name);
    p.style = j.value("style", "");
    p.source = j.value("source", "png-pixel");
    if (j.contains("fallback") && j["fallback"].is_string())
        p.fallback = j["fallback"].get<std::string>();
    p.background = HexColor(j.value("background", "#000000")).value_or(draw::Color{0, 0, 0});
    if (const auto& n = j.value("native", json()); n.is_array() && n.size() == 2 && n[0].is_number_integer() &&
                                                   n[1].is_number_integer())
    {
        p.nativeW = n[0].get<int>();
        p.nativeH = n[1].get<int>();
    }
    if (const auto& a = j.value("pixel_aspect", json()); a.is_array() && a.size() == 2 && a[0].is_number_integer() &&
                                                         a[1].is_number_integer())
    {
        p.aspectX = std::max(1, a[0].get<int>());
        p.aspectY = std::max(1, a[1].get<int>());
    }
    return p;
}

// The source picture composited on the pack's background
std::optional<Rgb> LoadOn(const std::filesystem::path& file, draw::Color bg)
{
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load(file.string().c_str(), &w, &h, &n, 4);
    if (!data)
        return std::nullopt;
    Rgb o(w, h);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i)
    {
        const unsigned a = data[i * 4 + 3];
        const std::uint8_t b[3] = {bg.r, bg.g, bg.b};
        for (int c = 0; c < 3; ++c)
            o.px[i * 3 + c] = static_cast<std::uint8_t>((data[i * 4 + c] * a + b[c] * (255 - a) + 127) / 255);
    }
    stbi_image_free(data);
    return o;
}

// As render_streamdeck_icons.render_file: png-pixel by the largest whole factor (nearest, the aspect kept), else
// scaled to fit; centred on the background
Rgb Draw(const Pack& p, const Rgb& src, int size)
{
    Rgb out(size, size, p.background.r, p.background.g, p.background.b);
    if (p.source == "png-pixel")
    {
        const int k = std::max(1, std::min(size / std::max(1, src.w * p.aspectX), size / std::max(1, src.h * p.aspectY)));
        const int w = src.w * p.aspectX * k, h = src.h * p.aspectY * k;
        if (w <= size && h <= size)
        {
            Rgb big(w, h);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                {
                    const auto* s = src.At(x / (p.aspectX * k), y / (p.aspectY * k));
                    auto* d = big.At(x, y);
                    d[0] = s[0];
                    d[1] = s[1];
                    d[2] = s[2];
                }
            Blit(out, big, (size - w) / 2, (size - h) / 2);
            return out;
        }
    }
    const double s = std::min(static_cast<double>(size) / src.w, static_cast<double>(size) / src.h);
    const int w = std::max(1, static_cast<int>(std::lround(src.w * s))),
              h = std::max(1, static_cast<int>(std::lround(src.h * s)));
    Blit(out, Resize(src, w, h), (size - w) / 2, (size - h) / 2);
    return out;
}

struct Cache
{
    std::mutex m;
    std::filesystem::path dir;  // the packs' folder the entries below came from
    std::map<std::string, std::optional<Pack>> packs;
    std::map<std::pair<std::string, std::string>, std::string> serving;
    std::map<std::tuple<std::string, std::string, int>, std::shared_ptr<const Rgb>> glyphs;

    // under m: the entries belong to Dir() (a test may move the data directory)
    void Check()
    {
        const auto d = Dir();
        if (d != dir)
        {
            dir = d;
            packs.clear();
            serving.clear();
            glyphs.clear();
        }
    }
    const std::optional<Pack>& PackLocked(const std::string& name)
    {
        auto it = packs.find(name);
        if (it == packs.end())
            it = packs.emplace(name, ValidName(name) && !dir.empty() ? Read(dir / name) : std::nullopt).first;
        return it->second;
    }
    std::string ServingLocked(const std::string& pack, const std::string& glyph)
    {
        const auto key = std::make_pair(pack, glyph);
        if (auto it = serving.find(key); it != serving.end())
            return it->second;
        std::string found;
        std::set<std::string> seen;
        std::string at = pack.empty() ? std::string(kDefaultPack) : pack;
        while (!at.empty() && seen.insert(at).second && found.empty())
        {
            const auto& p = PackLocked(at);
            std::error_code ec;
            if (p && std::filesystem::exists(dir / at / "src" / (glyph + ".png"), ec))
                found = at;
            else
                at = p && !p->fallback.empty() ? p->fallback : std::string(kDefaultPack);
        }
        serving[key] = found;
        return found;
    }
};

Cache& TheCache()
{
    static Cache c;
    return c;
}

bool ValidGlyph(const std::string& g)
{
    return !g.empty() && g.front() != '.' && g.find("..") == std::string::npos &&
           g.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") == std::string::npos;
}
}  // namespace

std::filesystem::path Dir()
{
    const auto root = data::Root();
    return root.empty() ? std::filesystem::path() : root / "streamdeck" / "icons" / "packs";
}

bool ValidName(const std::string& name)
{
    return !name.empty() && name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos;
}

std::vector<Pack> List()
{
    std::vector<Pack> out;
    const auto dir = Dir();
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec))
        return out;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.is_directory(ec) && ValidName(e.path().filename().string()))
            if (auto p = Read(e.path()))
                out.push_back(std::move(*p));
    std::sort(out.begin(), out.end(), [](const Pack& a, const Pack& b) { return a.name < b.name; });
    return out;
}

std::optional<Pack> Find(const std::string& name)
{
    Cache& c = TheCache();
    std::lock_guard l(c.m);
    c.Check();
    return c.PackLocked(name);
}

json ListJson()
{
    json packs = json::array();
    for (const auto& p : List())
        packs.push_back({{"name", p.name},
                         {"title", p.title},
                         {"style", p.style},
                         {"source", p.source},
                         {"fallback", p.fallback.empty() ? json() : json(p.fallback)},
                         {"background", sdb::Format("#{:02X}{:02X}{:02X}", int{p.background.r}, int{p.background.g},
                                                    int{p.background.b})}});
    return {{"packs", packs}, {"default", kDefaultPack}};
}

std::vector<std::string> GlyphIds()
{
    std::vector<std::string> out;
    const auto root = data::Root();
    if (root.empty())
        return out;
    std::ifstream in(root / "streamdeck" / "icons" / "glyphs.json");
    const json j = in ? json::parse(in, nullptr, false) : json();
    if (j.is_object() && j.contains("glyphs") && j["glyphs"].is_array())
        for (const auto& g : j["glyphs"])
            if (g.is_object() && g.contains("id") && g["id"].is_string())
                out.push_back(g["id"].get<std::string>());
    return out;
}

std::string Serving(const std::string& pack, const std::string& glyph)
{
    if (!ValidGlyph(glyph))
        return {};
    Cache& c = TheCache();
    std::lock_guard l(c.m);
    c.Check();
    return c.dir.empty() ? std::string() : c.ServingLocked(pack, glyph);
}

draw::Color Background(const std::string& pack, const std::string& glyph)
{
    Cache& c = TheCache();
    std::lock_guard l(c.m);
    c.Check();
    const std::string at = ValidGlyph(glyph) && !c.dir.empty() ? c.ServingLocked(pack, glyph) : std::string();
    const auto& p = c.PackLocked(at.empty() ? (pack.empty() ? std::string(kDefaultPack) : pack) : at);
    return p ? p->background : draw::Color{0, 0, 0};
}

std::shared_ptr<const Rgb> Glyph(const std::string& pack, const std::string& glyph, int size)
{
    if (size <= 0 || !ValidGlyph(glyph))
        return nullptr;
    Cache& c = TheCache();
    std::lock_guard l(c.m);
    c.Check();
    if (c.dir.empty())
        return nullptr;
    const auto key = std::make_tuple(pack, glyph, size);
    if (auto it = c.glyphs.find(key); it != c.glyphs.end())
        return it->second;
    std::shared_ptr<const Rgb> img;
    if (const std::string at = c.ServingLocked(pack, glyph); !at.empty())
        if (const auto& p = c.PackLocked(at))
            if (auto src = LoadOn(c.dir / at / "src" / (glyph + ".png"), p->background))
                img = std::make_shared<const Rgb>(Draw(*p, *src, size));
    if (c.glyphs.size() >= kMaxGlyphs)
        c.glyphs.clear();
    c.glyphs[key] = img;
    return img;
}

void ClearCache()
{
    Cache& c = TheCache();
    std::lock_guard l(c.m);
    c.dir.clear();
    c.packs.clear();
    c.serving.clear();
    c.glyphs.clear();
}

}  // namespace sdb::packs
