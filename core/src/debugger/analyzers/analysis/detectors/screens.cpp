/// The screen detector: what block copies moved into the display set (media.h: the screen, and the back buffers
/// chained to it). A source the code also wrote is a back buffer; one only copied is a picture. Pixels are 256
/// wide and length / 32 rows high, in the display file's order; attributes are 32 cells wide and length / 32 rows.
/// The screen itself is not claimed, nor a source a level map claim already covers (a level's attributes copied
/// to the screen as they are). One claim per source address: the longest copy. The sources of one copier that are
/// contiguous, overlap or leave gaps no longer than a copy (of 256 bytes at most) are one back buffer copied a row
/// at a time: its rows are the most frequent step from source to source (at least the longest copy), in order.

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "debugger/analyzers/analysis/detectors/media.h"

namespace analysis
{

namespace
{

constexpr uint32_t kMaxRow = 256;  ///< a copy no longer is a row of a back buffer at most

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
    // A back buffer copied a row at a time: the sources of one copier a row or less apart are one buffer
    struct Span
    {
        uint16_t src = 0;
        uint32_t end = 0;
        uint32_t row = 0;  ///< the longest copy
        uint64_t runs = 0;
        uint32_t copies = 0;
        bool written = false;  ///< code drew into one of the rows
        const media::DisplayCopy* first = nullptr;
        uint16_t last = 0;                   ///< the latest source
        std::map<uint32_t, uint32_t> steps;  ///< distance from source to source: how often

        /// A buffer row: the most frequent step from source to source, at least the longest copy
        uint32_t Row() const
        {
            uint32_t best = 0, count = 0;
            for (const auto& [step, n] : steps)
                if (n > count)
                {
                    best = step;
                    count = n;
                }
            return std::max(best, row);
        }
    };
    std::vector<Span> spans;
    std::map<std::pair<uint16_t, uint8_t>, size_t> open;
    for (const auto& [src, c] : bySource)
    {
        const uint32_t end = static_cast<uint32_t>(src) + c->length - 1;
        if ((src >> 14) != (end >> 14))
            continue;
        const auto key = std::make_pair(c->copiers.empty() ? uint16_t{0} : c->copiers.front(), c->type);
        const auto it = open.find(key);
        if (it != open.end() && c->length <= kMaxRow && spans[it->second].row <= kMaxRow &&
            src <= spans[it->second].end + 1 + spans[it->second].row &&
            (spans[it->second].src >> 14) == (end >> 14))
        {
            Span& s = spans[it->second];
            ++s.steps[src - s.last];
            s.last = src;
            s.end = std::max(s.end, end);
            s.row = std::max(s.row, c->length);
            s.runs += c->runs;
            ++s.copies;
            s.written = s.written || c->written;
            continue;
        }
        open[key] = spans.size();
        spans.push_back(Span{src, end, c->length, c->runs, 1, c->written, c, src, {}});
    }
    for (const Span& s : spans)
    {
        const uint16_t src = s.src;
        const uint32_t end = s.end;
        const media::DisplayCopy* c = s.first;
        bool map = false;
        for (const Region* r : context.out.store.Overlapping(context.Key(src), context.Key(static_cast<uint16_t>(end))))
            map = map || r->kind == RegionKind::LevelMap;
        if (map)
            continue;
        const bool pixels = c->type == media::kPixels;
        const bool rows = s.copies > 1;
        const uint32_t size = end - src + 1;
        const uint32_t row = s.Row();
        Region r;
        r.kind = RegionKind::Screen;
        r.detector = DetectorId::Screens;
        r.status = RegionStatus::Observed;
        r.geometry.frames = 1;
        if (rows)
        {
            // Rows of the copy's length, in the buffer's order
            r.geometry.width = static_cast<uint16_t>(pixels ? row * 8 : row);
            r.geometry.height = static_cast<uint16_t>((size + row - 1) / row);
            r.geometry.stride = static_cast<uint16_t>(size);
            r.geometry.layout = pixels ? "rows" : "attributes";
        }
        else
        {
            r.geometry.width = static_cast<uint16_t>(pixels ? 256 : 32);
            r.geometry.height = static_cast<uint16_t>(c->length / 32);
            r.geometry.layout = pixels ? "screen" : "attributes";
        }
        r.confidence = s.written ? 850 : confidence::FromCount(700, 900, s.runs, 2);
        const std::string what = pixels ? "pixels" : "attributes";
        r.title = std::string(s.written ? "Back buffer" : "Picture") + " (" + what + ", " +
                  std::to_string(r.geometry.width) + " x " + std::to_string(r.geometry.height) + ")" +
                  (rows ? ", copied a row of " + std::to_string(s.row) + " bytes at a time" : "") + ", to " +
                  Hex(c->dst) + " by " + Hex(c->copiers.empty() ? 0 : c->copiers.front());
        for (const uint16_t copier : c->copiers)
        {
            const uint32_t routine = media::RoutineOfZ80(context, copier);
            r.evidence.push_back(Evidence{"copy", copier, s.runs,
                                          std::to_string(c->length) + " bytes to " + Hex(c->dst) +
                                              (c->toScreen ? " (the screen)" : " (a back buffer)") +
                                              (routine != 0xFFFFFFFFu ? ", in routine " +
                                                                            Hex(static_cast<uint16_t>(routine))
                                                                      : "")});
            if (r.evidence.size() >= 4)
                break;
        }
        if (rows)
            r.evidence.push_back(Evidence{"rows", src, s.copies, "copies by the same copier from sources a row or less apart"});
        if (s.written)
            r.evidence.push_back(Evidence{"written", src, 0, "the code draws into it before the copy"});
        r.users = context.UsersOf(src, static_cast<uint16_t>(end));
        context.Claim(std::move(r), src, static_cast<uint16_t>(end));
    }
}

}  // namespace analysis
