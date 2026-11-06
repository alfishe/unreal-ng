// offline-look.h - the waiting screen as the window draws it (PixelRenderer, 8x16 cells): every pixel outside the
// dialog is gray, the dialog frame keeps its colour, the panels under it stay readable. Shared by both test programs
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <set>
#include <string>

#include "gui/pixel-renderer.h"
#include "screen/palette.h"
#include "screen/textscreen.h"

namespace dbg::testing {

inline bool IsGray(uint32_t argb) {
    const uint32_t r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
    return r == g && g == b;
}

// Render the screen, check it, and with a folder write it as <folder>/<name>.ppm to look at
inline void CheckOfflineLook(const TextScreen& screen, const std::string& name, bool withPanels, const std::string& shots,
                             const std::function<void(bool, const std::string&)>& check) {
    PixelRenderer renderer(screen.Width(), screen.Height(), screen.Width() * 8, screen.Height() * 16);
    renderer.Render(screen, Palette());
    const std::vector<uint32_t>& px = renderer.Pixels();
    bool outsideGray = true;
    std::string firstColour;
    bool dialogColour = false;
    std::set<uint32_t> levels;
    for (int y = 0; y < renderer.PixelHeight(); ++y) {
        for (int x = 0; x < renderer.PixelWidth(); ++x) {
            const uint32_t c = px[static_cast<size_t>(y) * static_cast<size_t>(renderer.PixelWidth()) + static_cast<size_t>(x)];
            if (screen.Grayed(x / 8, y / 16)) {
                // the dialog's hairline frame runs one pixel outside its cells (its corners diagonally)
                bool nearDialog = false;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (x + dx >= 0 && y + dy >= 0 && !screen.Grayed((x + dx) / 8, (y + dy) / 16)) nearDialog = true;
                if (!IsGray(c)) {
                    if (nearDialog) dialogColour = true;
                    else if (outsideGray) {
                        outsideGray = false;
                        firstColour = " (first at cell " + std::to_string(x / 8) + "," + std::to_string(y / 16) + ")";
                    }
                }
                levels.insert(c);
            } else if (!IsGray(c)) {
                dialogColour = true;
            }
        }
    }
    check(outsideGray, name + ": everything but the dialog is gray" + firstColour);
    check(dialogColour, name + ": the dialog keeps its colour");
    if (withPanels) check(levels.size() >= 4, name + ": the panels under the dialog stay readable (several grays)");
    if (shots.empty()) return;
    const std::string path = shots + "/" + name + ".ppm";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return;
    std::fprintf(f, "P6 %d %d 255\n", renderer.PixelWidth(), renderer.PixelHeight());
    for (uint32_t c : px) {
        const unsigned char rgb[3] = {static_cast<unsigned char>(c >> 16), static_cast<unsigned char>(c >> 8),
                                      static_cast<unsigned char>(c)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

}  // namespace dbg::testing
