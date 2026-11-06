#pragma once

/// @file videoprofile.h
/// @brief A game profile's `video:` section (mapping-and-profiles.md §7.7): the game's picture cadence, for the
/// presenter's smooth motion (frame presenter design, docs/inprogress/2026-10-26-frame-presenter, section 4.5).

#include <array>
#include <string>
#include <vector>

#include "emulator/video/smoothmotion.h"  // GameMode: header-only use (the library does not link the core)
#include "profiles/json.h"

namespace controllers
{

struct VideoProfile
{
    /// x, y, width, height in framebuffer pixels of the machine's video mode, border included
    using Rect = std::array<int, 4>;

    unsigned picturePeriod = 1;  ///< frames per picture, 1..16
    smoothmotion::GameMode smooth = smoothmotion::GameMode::Off;  ///< off | cadence | carry
    Rect window{};               ///< the play area; all zero = the whole frame
    std::vector<Rect> exclude;   ///< inside the window, left alone (score, status row)

    bool operator==(const VideoProfile& o) const = default;
};

/// `video` as written into @p out; false with the reason
bool ParseVideoProfile(const JsonValue& value, VideoProfile& out, std::string& error);

/// The words of `video.smooth`, in GameMode order: smoothmotion::GameModeName's (videoprofile_test holds them to it;
/// the library uses the core's headers only, so it cannot call smoothmotion::ParseGameMode)
inline constexpr const char* kVideoSmoothWords[] = {"off", "cadence", "carry"};

/// The window and every region inside a @p width x @p height frame (the presenter's check against the machine's
/// framebuffer); false with the reason
bool VideoProfileFits(const VideoProfile& video, int width, int height, std::string& error);

}  // namespace controllers
