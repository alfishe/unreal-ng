#pragma once

/// @file windowdisplay.h
/// @brief The GUI window's display settings (GPU acceleration, CRT effects and profile, temporal blending), shared by
/// every automation surface: CLI `video window`, WebAPI /video/window, Lua / Python window_display() /
/// window_display_set(), MCP capture_media window_display / window_display_set (through the WebAPI).
///
/// The window belongs to the front end: the core asks the IWindowViewProvider the front end registered
/// (emulator/video/windowview.h) and never sees the UI. Headless there is none: every call answers "no-window".
/// Validation lives here, so every surface refuses the same words with the same message; the provider only applies.

#include <emulator/state/statenode.h>
#include <emulator/video/windowview.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace WindowDisplay
{
/// The settings a change can name, in the order every surface lists them
inline constexpr const char* kKeys = "gpu, crt, crt_profile, blending, blending_frames, blending_weights";

inline constexpr int kMinBlendingFrames = 2;
inline constexpr int kMaxBlendingFrames = 5;

enum class Status
{
    Ok,
    NoWindow,      ///< no front end window (headless), or it cannot answer now
    BadParameter,  ///< an unknown key or value
    Unavailable    ///< GPU acceleration asked on a host without it
};

/// "ok", "no-window", "bad-parameter", "unavailable": the `kind` of an error on every surface
inline const char* StatusName(Status status)
{
    switch (status)
    {
        case Status::Ok: return "ok";
        case Status::NoWindow: return "no-window";
        case Status::BadParameter: return "bad-parameter";
        case Status::Unavailable: return "unavailable";
    }
    return "ok";
}

struct Result
{
    Status status = Status::Ok;
    std::string error;
    WindowDisplaySettings settings;

    bool Ok() const { return status == Status::Ok; }
};

inline Result Fail(Status status, std::string error)
{
    Result r;
    r.status = status;
    r.error = std::move(error);
    return r;
}

inline std::string Lower(std::string word)
{
    std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return word;
}

/// on / off words: true, false, on, off, 1, 0, yes, no (any case)
inline bool ParseBool(const std::string& word, bool& out)
{
    const std::string w = Lower(word);
    if (w == "true" || w == "on" || w == "1" || w == "yes")
        out = true;
    else if (w == "false" || w == "off" || w == "0" || w == "no")
        out = false;
    else
        return false;
    return true;
}

/// Put one `key` = `value` (words, as a command line or a JSON scalar renders them) into `change`.
/// False with `error` for an unknown key or a value of the wrong kind; profile names are checked by Set()
inline bool ApplyWord(WindowDisplayChange& change, const std::string& key, const std::string& value, std::string& error)
{
    const std::string k = Lower(key);
    auto flag = [&](std::optional<bool>& field) {
        bool on = false;
        if (!ParseBool(value, on))
        {
            error = "'" + key + "' takes on or off (true / false), not '" + value + "'";
            return false;
        }
        field = on;
        return true;
    };
    if (k == "gpu")
        return flag(change.gpu);
    if (k == "crt")
        return flag(change.crt);
    if (k == "blending")
        return flag(change.blending);
    if (k == "crt_profile")
    {
        change.crtProfile = Lower(value);
        return true;
    }
    if (k == "blending_frames")
    {
        int frames = 0;
        size_t used = 0;
        try
        {
            frames = std::stoi(value, &used);
        }
        catch (...)
        {
            used = 0;
        }
        if (used == 0 || used != value.size() || frames < kMinBlendingFrames || frames > kMaxBlendingFrames)
        {
            error = "'blending_frames' takes " + std::to_string(kMinBlendingFrames) + ".." +
                    std::to_string(kMaxBlendingFrames) + ", not '" + value + "'";
            return false;
        }
        change.blendingFrames = frames;
        return true;
    }
    if (k == "blending_weights")
    {
        const std::string w = Lower(value);
        if (w != "equal" && w != "exponential")
        {
            error = "'blending_weights' takes equal or exponential, not '" + value + "'";
            return false;
        }
        change.blendingWeights = w;
        return true;
    }
    error = "Unknown display setting '" + key + "': use " + kKeys;
    return false;
}

/// The settings of the window the front end registered
inline Result Get()
{
    std::shared_ptr<IWindowViewProvider> provider = WindowView::Provider();
    if (!provider)
        return Fail(Status::NoWindow, "No window: the display settings exist only in the GUI (unreal-qt)");
    Result r;
    if (!provider->GetWindowDisplay(r.settings, r.error))
        return Fail(Status::NoWindow, r.error);
    return r;
}

/// Validate `change` against the window's offer, apply it the way the View menu does, answer the settings after it.
/// An empty change answers the settings as they are
inline Result Set(const WindowDisplayChange& change)
{
    Result now = Get();
    if (!now.Ok() || change.Empty())
        return now;

    if (change.crtProfile)
    {
        const std::vector<std::string>& offered = now.settings.crtProfiles;
        if (std::find(offered.begin(), offered.end(), *change.crtProfile) == offered.end())
        {
            std::string list;
            for (const std::string& name : offered)
                list += (list.empty() ? "" : ", ") + name;
            return Fail(Status::BadParameter, "Unknown CRT profile '" + *change.crtProfile + "': use " + list);
        }
    }
    if (change.gpu && *change.gpu && !now.settings.gpuAvailable)
        return Fail(Status::Unavailable, "GPU acceleration is not available on this host (no OpenGL)");
    // A custom shader ("shader:<name>", offered when the host has a GPU) runs on the GPU only: on with this change or
    // already on
    if (change.crtProfile && change.crtProfile->rfind("shader:", 0) == 0 && !change.gpu.value_or(now.settings.gpu))
        return Fail(Status::Unavailable, "A custom shader needs GPU acceleration: add gpu=on");

    std::shared_ptr<IWindowViewProvider> provider = WindowView::Provider();
    if (!provider)
        return Fail(Status::NoWindow, "No window: the display settings exist only in the GUI (unreal-qt)");
    Result r;
    if (!provider->SetWindowDisplay(change, r.settings, r.error))
        return Fail(Status::NoWindow, r.error);
    return r;
}

/// The report every surface returns: backend, gpu_available, gpu, crt, crt_profile, crt_profiles[], blending,
/// blending_frames, blending_weights, emulator (the instance the window shows, "" = none)
inline StateNode Report(const WindowDisplaySettings& s)
{
    StateNode node = StateNode::Object();
    node["backend"] = s.backend;
    node["gpu_available"] = s.gpuAvailable;
    node["gpu"] = s.gpu;
    node["crt"] = s.crt;
    node["crt_profile"] = s.crtProfile;
    StateNode profiles = StateNode::Array();
    for (const std::string& name : s.crtProfiles)
        profiles.items.emplace_back(name);
    node["crt_profiles"] = profiles;
    node["blending"] = s.blending;
    node["blending_frames"] = s.blendingFrames;
    node["blending_weights"] = s.blendingWeights;
    node["emulator"] = s.emulatorId;
    return node;
}

/// {ok: false, error, kind} for a failed call
inline StateNode ErrorNode(const Result& r)
{
    StateNode node = StateNode::Object();
    node["ok"] = false;
    node["error"] = r.error;
    node["kind"] = StatusName(r.status);
    return node;
}

/// One line, e.g. "gpu on (available), crt shadow-mask, blending off (3 frames, exponential)"
inline std::string Summary(const WindowDisplaySettings& s)
{
    std::string line = std::string("gpu ") + (s.gpu ? "on" : "off") + (s.gpuAvailable ? " (available)" : " (unavailable)");
    line += std::string(", crt ") + (s.crt ? s.crtProfile : "off");
    line += std::string(", blending ") + (s.blending ? "on" : "off") + " (" + std::to_string(s.blendingFrames) +
            " frames, " + s.blendingWeights + ")";
    return line;
}
}  // namespace WindowDisplay
