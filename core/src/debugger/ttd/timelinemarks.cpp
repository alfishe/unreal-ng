#include "debugger/ttd/timelinemarks.h"

#include <algorithm>
#include <cmath>

namespace ttd
{

const char* TimelineMarkKindName(TimelineMarkKind kind)
{
    switch (kind)
    {
        case TimelineMarkKind::Second:
            return "second";
        case TimelineMarkKind::TenSeconds:
            return "ten_seconds";
        case TimelineMarkKind::Bookmark:
            return "bookmark";
        case TimelineMarkKind::Edge:
            return "edge";
        case TimelineMarkKind::Barrier:
            return "barrier";
    }
    return "second";
}

int TimelineMarkStrength(TimelineMarkKind kind)
{
    switch (kind)
    {
        case TimelineMarkKind::Edge:
        case TimelineMarkKind::Barrier:
            return 3;
        case TimelineMarkKind::Bookmark:
        case TimelineMarkKind::TenSeconds:
            return 2;
        case TimelineMarkKind::Second:
            return 1;
    }
    return 1;
}

namespace timeline_marks
{
namespace
{
/// The first frame of second `k`: the smallest frame whose SecondOf is k
uint64_t SecondStart(uint64_t k, uint64_t first, double fps)
{
    uint64_t frame = first + static_cast<uint64_t>(std::ceil(static_cast<double>(k) * fps));
    // The rounding of k * fps may land one frame off the floor() rule SecondOf uses: settle on that rule
    while (frame > first && SecondOf(frame - 1, first, fps) >= k)
        frame--;
    while (SecondOf(frame, first, fps) < k)
        frame++;
    return frame;
}
}  // namespace

uint64_t SecondOf(uint64_t frame, uint64_t first, double fps)
{
    if (frame <= first || !(fps > 0))
        return 0;
    return static_cast<uint64_t>(std::floor(static_cast<double>(frame - first) / fps));
}

std::vector<TimelineMark> Crossed(uint64_t from, uint64_t to, uint64_t first, double fps,
                                  const std::vector<uint64_t>& bookmarks)
{
    std::vector<TimelineMark> marks;
    if (from == to)
        return marks;
    const bool forward = to > from;

    if (fps > 0)
    {
        const uint64_t secondFrom = SecondOf(from, first, fps);
        const uint64_t secondTo = SecondOf(to, first, fps);
        if (secondFrom != secondTo)
        {
            // The boundary nearest the target: forward the start of the target's second, back the start of the
            // second after it
            const uint64_t tenFrom = secondFrom / 10;
            const uint64_t tenTo = secondTo / 10;
            if (tenFrom != tenTo)
            {
                const uint64_t k = forward ? tenTo * 10 : (tenTo + 1) * 10;
                marks.push_back({TimelineMarkKind::TenSeconds, SecondStart(k, first, fps)});
            }
            else
            {
                const uint64_t k = forward ? secondTo : secondTo + 1;
                marks.push_back({TimelineMarkKind::Second, SecondStart(k, first, fps)});
            }
        }
    }

    // A bookmark in (from, to] forward, [to, from) back: the one nearest the target
    bool found = false;
    uint64_t nearest = 0;
    for (uint64_t bookmark : bookmarks)
    {
        const bool inside = forward ? (bookmark > from && bookmark <= to) : (bookmark >= to && bookmark < from);
        if (!inside)
            continue;
        const uint64_t distance = forward ? to - bookmark : bookmark - to;
        if (!found || distance < (forward ? to - nearest : nearest - to))
        {
            nearest = bookmark;
            found = true;
        }
    }
    if (found)
        marks.push_back({TimelineMarkKind::Bookmark, nearest});

    SortStrongestFirst(marks);
    return marks;
}

void SortStrongestFirst(std::vector<TimelineMark>& marks)
{
    std::stable_sort(marks.begin(), marks.end(), [](const TimelineMark& a, const TimelineMark& b) {
        return TimelineMarkStrength(a.kind) > TimelineMarkStrength(b.kind);
    });
}
}  // namespace timeline_marks

}  // namespace ttd
