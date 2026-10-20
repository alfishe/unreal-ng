// Monochrome SVG icons (e.g. unreal-ng's resources/icons/*.svg) rasterised with nanosvg and recoloured.
#pragma once
#include <optional>
#include <string>

#include "sdb/draw.h"

namespace sdb
{

// Renders `file` centred in a size x size square on `bg`, every painted stroke/fill recoloured to `fg`.
// `pad` = empty margin as a fraction of size. nullopt if the file can't be parsed or draws nothing
// (nanosvg has no <text> support).
[[nodiscard]] std::optional<Rgb> RenderSvg(const std::string& file, int size, draw::Color fg, draw::Color bg,
                                           float pad = 0.1f);

// Draws the icon into an existing image at (x, y) with a transparent background.
bool DrawSvg(Rgb& im, const std::string& file, int x, int y, int size, draw::Color fg, float pad = 0.f);

}  // namespace sdb
