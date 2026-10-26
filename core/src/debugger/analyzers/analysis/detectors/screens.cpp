/// The screen detector: what block copies moved into the display set (media.h: the screen, and the back buffers
/// chained to it). A source the code also wrote is a back buffer; one only copied is a picture. Pixels are 256
/// wide and length / 32 rows high, in the display file's order; attributes are 32 cells wide and length / 32 rows.
/// The screen itself is not claimed, nor a source a level map claim already covers (a level's attributes copied
/// to the screen as they are). One claim per source address: the longest copy.

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>

#include "debugger/analyzers/analysis/detectors/media.h"

namespace analysis
{

namespace
{

std::string Hex(uint16_t v)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", v);
    return text;
}

}  // namespace

void DetectScreens(DetectorContext& context)
{
    const media::DisplayInfo display = media::FindDisplay(context);
    std::map<uint16_t, const media::DisplayCopy*> bySource;
    for (const media::DisplayCopy& c : display.copies)
    {
        const auto it = bySource.find(c.src);
        if (it == bySource.end() || c.length > it->second->length)
            bySource[c.src] = &c;
    }
    for (const auto& [src, c] : bySource)
    {
        const uint32_t end = static_cast<uint32_t>(src) + c->length - 1;
        if ((src >> 14) != (end >> 14))
            continue;
        bool map = false;
        for (const Region* r : context.out.store.Overlapping(context.Key(src), context.Key(static_cast<uint16_t>(end))))
            map = map || r->kind == RegionKind::LevelMap;
        if (map)
            continue;
        const bool pixels = c->type == media::kPixels;
        Region r;
        r.kind = RegionKind::Screen;
        r.detector = DetectorId::Screens;
        r.status = RegionStatus::Observed;
        r.geometry.width = static_cast<uint16_t>(pixels ? 256 : 32);
        r.geometry.height = static_cast<uint16_t>(c->length / 32);
        r.geometry.frames = 1;
        r.geometry.layout = pixels ? "screen" : "attributes";
        r.confidence = c->written ? 850 : confidence::FromCount(700, 900, c->runs, 2);
        const std::string what = pixels ? "pixels" : "attributes";
        r.title = std::string(c->written ? "Back buffer" : "Picture") + " (" + what + ", " +
                  std::to_string(pixels ? 256 : 32) + " x " + std::to_string(c->length / 32) + "), copied to " +
                  Hex(c->dst) + " by " + Hex(c->copiers.empty() ? 0 : c->copiers.front());
        for (const uint16_t copier : c->copiers)
        {
            const uint32_t routine = media::RoutineOfZ80(context, copier);
            r.evidence.push_back(Evidence{"copy", copier, c->runs,
                                          std::to_string(c->length) + " bytes to " + Hex(c->dst) +
                                              (c->toScreen ? " (the screen)" : " (a back buffer)") +
                                              (routine != 0xFFFFFFFFu ? ", in routine " +
                                                                            Hex(static_cast<uint16_t>(routine))
                                                                      : "")});
            if (r.evidence.size() >= 4)
                break;
        }
        if (c->written)
            r.evidence.push_back(Evidence{"written", src, 0, "the code draws into it before the copy"});
        r.users = context.UsersOf(src, static_cast<uint16_t>(end));
        context.Claim(std::move(r), src, static_cast<uint16_t>(end));
    }
}

}  // namespace analysis
