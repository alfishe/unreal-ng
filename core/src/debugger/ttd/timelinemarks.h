#pragma once

/// @file timelinemarks.h
/// @brief The marks a seek crossed on the recorded timeline (control surfaces actions.md §3): whole seconds, ten
/// seconds, the history's edge, a replay barrier, a bookmark. A surface turns them into feedback (the Surface Dial
/// clicks at a second, clicks twice at ten, buzzes at the edge). Pure arithmetic over frame numbers and the bookmark
/// list: no history access, no lock.

#include <cstdint>
#include <string>
#include <vector>

namespace ttd
{

enum class TimelineMarkKind : uint8_t
{
    Second,      ///< a whole second of the history (from its first frame) began between the two positions
    TenSeconds,  ///< the same for ten seconds; replaces Second there
    Bookmark,    ///< a bookmark lies between them
    Edge,        ///< the target was clamped to the history's first or last frame
    Barrier      ///< the seek halted at an external-event marker (a replay barrier)
};

/// "second", "ten_seconds", "bookmark", "edge", "barrier"
const char* TimelineMarkKindName(TimelineMarkKind kind);
/// edge = barrier (3) > bookmark = ten_seconds (2) > second (1)
int TimelineMarkStrength(TimelineMarkKind kind);

struct TimelineMark
{
    TimelineMarkKind kind = TimelineMarkKind::Second;
    uint64_t frame = 0;  ///< where: the boundary or bookmark nearest the target, the clamped target, the barrier
};

namespace timeline_marks
{
/// The second index of `frame`: floor((frame - first) / fps); 0 before `first`. fps need not be an integer (48.83 on
/// a Pentagon: seconds start at frames 0, 49, 98, 147, ... and never drift)
uint64_t SecondOf(uint64_t frame, uint64_t first, double fps);

/// The marks of a move from `from` to `to` (either direction): at most one Second or TenSeconds (the boundary
/// nearest `to`; TenSeconds when a ten-second boundary was crossed) and one Bookmark (the one nearest `to`, in
/// (from, to] forward, [to, from) back). Edge and Barrier are the seek's (added by its caller). Strongest first
std::vector<TimelineMark> Crossed(uint64_t from, uint64_t to, uint64_t first, double fps,
                                  const std::vector<uint64_t>& bookmarks);

/// Sort strongest first (stable within a strength)
void SortStrongestFirst(std::vector<TimelineMark>& marks);
}  // namespace timeline_marks

}  // namespace ttd
