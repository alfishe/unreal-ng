#pragma once

#include <cctype>
#include <cstdint>
#include <string>

/// [VIDEO] LipSyncOffsetMs's range: the trim added to every frame's target time (positive = the picture later)
inline constexpr int kMinLipSyncOffsetMs = -200;
inline constexpr int kMaxLipSyncOffsetMs = 200;

/// How the native presenter puts frames on the display ([VIDEO] PresentPolicy; frame presenter design, section 4):
/// - auto: each frame at its audio time on a VRR display (c-vrr), the frame of each refresh on a fixed one (b-vsync);
///   Windows checks that VRR engaged and falls back to vsync when the refreshes stay on the fixed grid;
/// - vrr: each frame at its own time, always (smooth motion draws the frame due);
/// - vsync: the refresh grid, always (a VRR display is driven at its top rate, smooth motion draws per refresh).
/// UNREAL_PRESENT_POLICY overrides it
enum class PresentPolicy : uint8_t
{
    Auto,
    Vrr,
    Vsync
};

inline const char* PresentPolicyName(PresentPolicy policy)
{
    switch (policy)
    {
        case PresentPolicy::Auto: return "auto";
        case PresentPolicy::Vrr: return "vrr";
        case PresentPolicy::Vsync: return "vsync";
    }
    return "auto";
}

/// auto, vrr, vsync (any case). False for any other word
inline bool ParsePresentPolicy(const char* text, PresentPolicy& policy)
{
    std::string word = text ? text : "";
    for (char& c : word)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (PresentPolicy p : {PresentPolicy::Auto, PresentPolicy::Vrr, PresentPolicy::Vsync})
        if (word == PresentPolicyName(p))
        {
            policy = p;
            return true;
        }
    return false;
}
