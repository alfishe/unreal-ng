#pragma once

/// @file videoprofile.h
/// @brief A game profile's `video:` section (mapping-and-profiles.md §7.7): the game's picture cadence, for the
/// presenter's smooth motion (frame presenter design, docs/inprogress/2026-10-26-frame-presenter, section 4.5).

#include <array>
#include <string>
#include <vector>

#include "profiles/json.h"

namespace controllers
{

struct VideoProfile
{
    /// x, y, width, height in framebuffer pixels of the machine's video mode, border included
    using Rect = std::array<int, 4>;

    unsigned picturePeriod = 1;  ///< frames per picture, 1..16
    std::string smooth = "off";  ///< off | cadence | carry
    Rect window{};               ///< the play area; all zero = the whole frame
    std::vector<Rect> exclude;   ///< inside the window, left alone (score, status row)
    std::string pictureReady;    ///< picture_ready.on: an event of the profile (empty: none)

    bool operator==(const VideoProfile& o) const = default;
};

/// `video` as written into @p out; false with the reason. @p events: the event names of the same profile, which
/// `picture_ready.on` must be one of (nullptr: not checked, the section was checked with its profile)
bool ParseVideoProfile(const JsonValue& value, const std::vector<std::string>* events, VideoProfile& out,
                       std::string& error);

/// The window and every region inside a @p width x @p height frame (the presenter's check against the machine's
/// framebuffer); false with the reason
bool VideoProfileFits(const VideoProfile& video, int width, int height, std::string& error);

}  // namespace controllers
