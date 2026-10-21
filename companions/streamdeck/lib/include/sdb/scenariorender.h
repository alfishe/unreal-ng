// Pictures for a scenario page: key faces, the back key, and the strip (breadcrumbs + the menu's content).
#pragma once
#include <string>
#include <vector>

#include "sdb/image.h"
#include "sdb/navigator.h"

namespace sdb::scn
{

inline constexpr int kFaceSize = 144;  // the layout grid of a face; drawn at any size (best: the device's key size)

// Upper-cases and replaces characters the 5x7 font cannot draw.
[[nodiscard]] std::string DisplayText(const std::string& s);

// `size`: the square drawn, in px; the device's own key size keeps it sharp (no resize after). `safePad`: inset (in
// `size` px) that keeps icons and labels clear of the bezel. `scenarioDir`: the scenario's folder, for $scenario and
// relative picture paths (sdb/datadir.h).
[[nodiscard]] Rgb RenderFace(const Face& f, const std::string& fallbackTitle, bool pressed, bool isMenu,
                             int safePad = 0, int size = kFaceSize, const std::string& scenarioDir = {});
[[nodiscard]] Rgb RenderBackKey(const std::string& parentTitle, int depth, bool pressed, int safePad = 0,
                                int size = kFaceSize);
[[nodiscard]] Rgb RenderEmptyKey(int size = kFaceSize);

// Breadcrumb chips on the strip, left to right. depth = stack index the chip pops to; -1 = collapsed "..".
struct Segment
{
    int x0 = 0, x1 = 0, depth = -1;
    std::string text;
};
[[nodiscard]] std::vector<Segment> CrumbSegments(const std::vector<std::string>& path, int stripW);
[[nodiscard]] int CrumbHit(const std::vector<std::string>& path, int stripW, int x);  // depth or -1

// The whole strip for a page (crumbs when page.crumbs == Strip, then the menu's own content, dial labels).
// `autoExitLeft` >= 0: seconds until the menu exits by itself (a shrinking bar + "BACK IN n" countdown).
// `status`: a live readout in a fixed slot at the right (reserved whenever a dial of the page scrubs time travel, so
// nothing shifts when it appears or changes).
[[nodiscard]] Rgb RenderStrip(const Page& page, int w, int h, int dialTileW, const std::string& banner = {},
                              double autoExitLeft = -1, const std::string& status = {},
                              const std::string& scenarioDir = {});

}  // namespace sdb::scn
