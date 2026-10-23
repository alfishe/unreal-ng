// Icon packs (docs/companions/streamdeck/icons/README.md): data/streamdeck/icons/packs/<pack>/pack.json + src/<glyph
// id>.png. Every pack draws the same glyph list in its own style; a face names a glyph (`face.glyph`) and the pack is
// a runtime choice (the deck's setting, else the scenario's settings.pack, else magicwb). A glyph is looked up in the
// pack, then along the pack's own `fallback` chain, then in magicwb, and drawn as tools/icons/render_streamdeck_icons.py
// draws it: png-pixel by the largest whole factor that fits (nearest neighbour, the pixel aspect kept), png-smooth
// scaled to fit, both centred on the pack's background.
#pragma once
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "sdb/draw.h"
#include "sdb/image.h"

namespace sdb::packs
{

inline constexpr const char* kDefaultPack = "magicwb";  // every pack's last fallback

struct Pack
{
    std::string name, title, style, source, fallback;  // source: png-pixel | png-smooth
    draw::Color background{0, 0, 0};
    int nativeW = 0, nativeH = 0, aspectX = 1, aspectY = 1;  // png-pixel
};

// data::Root()/streamdeck/icons/packs (empty when there is no data directory)
[[nodiscard]] std::filesystem::path Dir();
// Every pack of Dir(), by name
[[nodiscard]] std::vector<Pack> List();
[[nodiscard]] std::optional<Pack> Find(const std::string& name);
// {packs: [{name, title, style, source, fallback, background}], default}: GET /api/v1/packs
[[nodiscard]] nlohmann::json ListJson();
// The glyph ids of data/streamdeck/icons/glyphs.json, in its order (the studio's glyph picker)
[[nodiscard]] std::vector<std::string> GlyphIds();
// A pack name as a setting may hold it: [a-z0-9_-]+
[[nodiscard]] bool ValidName(const std::string& name);

// The pack that serves `glyph` for `pack` (the pack, its fallback chain, magicwb); "" when none has it.
[[nodiscard]] std::string Serving(const std::string& pack, const std::string& glyph);
// The glyph drawn size x size on its serving pack's background; null when no pack has it. Decoded and scaled once
// per (pack, glyph, size), then shared.
[[nodiscard]] std::shared_ptr<const Rgb> Glyph(const std::string& pack, const std::string& glyph, int size);
// The background a face with this glyph has (the serving pack's)
[[nodiscard]] draw::Color Background(const std::string& pack, const std::string& glyph);
// Forget every decoded glyph and pack (the packs changed on disk, tests)
void ClearCache();

}  // namespace sdb::packs
