#pragma once

/// @file ft812linebudgetview.h
/// @brief What the FT812 Debug window draws, Qt-free: one row per screen line
/// with its cost and class, composed from Vdac2Control::FrameMetrics by the
/// rule of line-budget-metrics.md §3.3 (the same moment as the main screen):
/// - at a frame boundary (running, or the frame in flight not started or
///   complete): the last finished frame's block;
/// - inside a frame (a paused machine part way through an FT812 frame): the
///   lines passed so far from the frame in flight, the rest from the last
///   finished frame, with the boundary marked, as the screen shows the frame
///   drawn so far over the previous one.

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/platforms/tsconf/vdac2control.h"

namespace Ft812LineBudget
{

enum class LineClass : uint8_t
{
    NotMeasured,  ///< the line was not drawn (frame not shown, or passed without drawing)
    Ok,           ///< within the soft budget
    OverSoft,     ///< soft < cost <= hard: risky on a real card
    OverHard      ///< cost > hard: broken on a real card
};

struct Line
{
    uint32_t clocks = 0;
    LineClass cls = LineClass::NotMeasured;
    bool previousFrame = false;  ///< from the last finished frame, below the in-flight boundary
};

struct View
{
    std::vector<Line> lines;
    uint32_t hardBudget = 0;
    uint32_t softBudget = 0;
    uint32_t axisClocks = 0;     ///< the chart's full width in clocks (covers the worst line and the hard budget)
    bool composite = false;      ///< in-flight lines over the previous frame
    uint32_t boundary = 0;       ///< first line from the previous frame (composite only)
    uint32_t worstLine = 0;      ///< of what is shown
    uint32_t worstClocks = 0;
    uint32_t overSoft = 0;       ///< of what is shown
    uint32_t overHard = 0;
};

LineClass Classify(uint32_t clocks, bool measured, uint32_t soft, uint32_t hard);

/// The rows to draw; `metrics` read with lines (and the frame in flight when paused)
View Build(const Vdac2Control::FrameMetrics& metrics);

/// One line of text for the window's header
std::string Summary(const Vdac2Control::FrameMetrics& metrics, const View& view);

}  // namespace Ft812LineBudget
