#include "profiles/videoprofile.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace controllers
{

namespace
{
constexpr unsigned kMaxPicturePeriod = 16;
const char* const kFields[] = {"picture_period", "smooth", "window", "exclude"};

/// [x, y, width, height]: whole numbers, the size above 0
bool ParseRect(const JsonValue& value, VideoProfile::Rect& out, const std::string& where, std::string& error)
{
    if (!value.IsArray() || value.Items().size() != 4)
    {
        error = where + " is [x, y, width, height]";
        return false;
    }
    for (size_t i = 0; i < 4; i++)
    {
        const JsonValue& item = value.Items()[i];
        if (!item.IsNumber() || std::floor(item.AsNumber()) != item.AsNumber() || item.AsNumber() < 0 ||
            item.AsNumber() > 4096)
        {
            error = where + ": whole numbers 0..4096";
            return false;
        }
        out[i] = static_cast<int>(item.AsNumber());
    }
    if (out[2] == 0 || out[3] == 0)
    {
        error = where + ": the width and the height above 0";
        return false;
    }
    return true;
}

bool Inside(const VideoProfile::Rect& inner, const VideoProfile::Rect& outer)
{
    return inner[0] >= outer[0] && inner[1] >= outer[1] && inner[0] + inner[2] <= outer[0] + outer[2] &&
           inner[1] + inner[3] <= outer[1] + outer[3];
}

bool Whole(const VideoProfile::Rect& r)
{
    return r == VideoProfile::Rect{};
}
}  // namespace

bool ParseVideoProfile(const JsonValue& value, VideoProfile& out, std::string& error)
{
    if (!value.IsObject())
    {
        error = "video: an object expected";
        return false;
    }
    VideoProfile video;
    for (const auto& [field, member] : value.GetMembers())
    {
        if (std::find_if(std::begin(kFields), std::end(kFields), [&](const char* f) { return field == f; }) ==
            std::end(kFields))
        {
            error = "video: unknown field '" + field +
                    "' (known: picture_period, smooth, window, exclude)";
            return false;
        }
    }
    if (const JsonValue* period = value.Find("picture_period"))
    {
        if (!period->IsNumber() || std::floor(period->AsNumber()) != period->AsNumber() || period->AsNumber() < 1 ||
            period->AsNumber() > kMaxPicturePeriod)
        {
            error = "video.picture_period is a whole number 1..16";
            return false;
        }
        video.picturePeriod = static_cast<unsigned>(period->AsNumber());
    }
    if (const JsonValue* smooth = value.Find("smooth"))
    {
        const auto word = smooth->IsString() ? std::find(std::begin(kVideoSmoothWords), std::end(kVideoSmoothWords),
                                                         smooth->AsString())
                                             : std::end(kVideoSmoothWords);
        if (word == std::end(kVideoSmoothWords))
        {
            error = "video.smooth is off, cadence or carry";
            return false;
        }
        video.smooth = static_cast<smoothmotion::GameMode>(word - std::begin(kVideoSmoothWords));
    }
    if (const JsonValue* window = value.Find("window"))
        if (!ParseRect(*window, video.window, "video.window", error))
            return false;
    if (const JsonValue* exclude = value.Find("exclude"))
    {
        if (!exclude->IsArray())
        {
            error = "video.exclude is a list of [x, y, width, height]";
            return false;
        }
        for (size_t i = 0; i < exclude->Items().size(); i++)
        {
            VideoProfile::Rect r{};
            const std::string where = "video.exclude[" + std::to_string(i) + "]";
            if (!ParseRect(exclude->Items()[i], r, where, error))
                return false;
            if (!Whole(video.window) && !Inside(r, video.window))
            {
                error = where + " is outside video.window";
                return false;
            }
            video.exclude.push_back(r);
        }
    }
    out = std::move(video);
    return true;
}

bool VideoProfileFits(const VideoProfile& video, int width, int height, std::string& error)
{
    const VideoProfile::Rect frame{0, 0, width, height};
    if (!Whole(video.window) && !Inside(video.window, frame))
    {
        error = "video.window is outside the " + std::to_string(width) + "x" + std::to_string(height) + " frame";
        return false;
    }
    for (const VideoProfile::Rect& r : video.exclude)
        if (!Inside(r, frame))
        {
            error = "video.exclude is outside the " + std::to_string(width) + "x" + std::to_string(height) + " frame";
            return false;
        }
    return true;
}

}  // namespace controllers
