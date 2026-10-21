#pragma once

/// @file windowdisplay.h
/// @brief The GUI window's display settings (GPU acceleration, CRT effects and profile, temporal blending, full screen,
/// scale, toolbar / status bar, HUD with its theme and notices, on-screen keyboard and its picture, the ZX Next layers
/// window) and the active CRT profile's parameters, shared by every automation surface: CLI `video window`, WebAPI
/// /video/window and /video/window/crt_params, Lua / Python window_display() / window_display_set() /
/// window_crt_params() / window_crt_params_set(), MCP capture_media window_display / window_display_set /
/// window_crt_params / window_crt_params_set (through the WebAPI), the catalogue's video / view / hud / osd actions.
///
/// The window belongs to the front end: the core asks the IWindowViewProvider the front end registered
/// (emulator/video/windowview.h) and never sees the UI. Headless there is none: every call answers "no-window".
/// Validation lives here, so every surface refuses the same words with the same message; the provider only applies.

#include <emulator/state/statenode.h>
#include <emulator/video/windowview.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace WindowDisplay
{
/// The settings a change can name, in the order every surface lists them
inline constexpr const char* kKeys =
    "gpu, crt, crt_profile, blending, blending_frames, blending_weights, blending_decay, fullscreen, scale, toolbar, "
    "status_bar, hud, hud_theme, hud_notice.<id>, osd_keyboard, osd_keyboard_picture, next_layers, next_layers_mode, "
    "next_layer.<ula|layer2|tilemap|sprites>";

inline constexpr int kMinBlendingFrames = 2;
inline constexpr int kMaxBlendingFrames = 5;
inline constexpr double kMinBlendingDecay = 0.1;
inline constexpr double kMaxBlendingDecay = 0.9;
inline constexpr int kMinScale = 1;
inline constexpr int kMaxScale = 4;
inline constexpr const char* kHudNoticePrefix = "hud_notice.";
inline constexpr const char* kNextLayerPrefix = "next_layer.";

/// The ZX Next layers window's modes and layer ticks, in its order
inline const std::vector<std::string>& NextLayersModes()
{
    static const std::vector<std::string> modes = {"layers", "composed", "winner"};
    return modes;
}
inline const std::vector<std::string>& NextLayerNames()
{
    static const std::vector<std::string> names = {"ula", "layer2", "tilemap", "sprites"};
    return names;
}

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

/// A decimal number word ("0.3", "-2", "1e-1" is not one): locale-free, so a GUI's locale never turns "0.3" into 0.
/// False when the word is not a plain decimal
inline bool ParseNumber(const std::string& word, double& out)
{
    size_t i = 0;
    const bool negative = !word.empty() && (word[0] == '-' || word[0] == '+');
    if (negative)
        i = 1;
    double value = 0;
    int digits = 0;
    while (i < word.size() && std::isdigit(static_cast<unsigned char>(word[i])))
    {
        value = value * 10 + (word[i++] - '0');
        ++digits;
    }
    if (i < word.size() && word[i] == '.')
    {
        ++i;
        double scale = 1;
        int64_t fraction = 0;
        while (i < word.size() && std::isdigit(static_cast<unsigned char>(word[i])) && scale < 1e15)
        {
            fraction = fraction * 10 + (word[i++] - '0');
            scale *= 10;
            ++digits;
        }
        while (i < word.size() && std::isdigit(static_cast<unsigned char>(word[i])))
            ++i;  // beyond 15 decimals: dropped
        value += static_cast<double>(fraction) / scale;
    }
    if (digits == 0 || i != word.size())
        return false;
    out = word[0] == '-' ? -value : value;
    return true;
}

/// A number as a locale-free word: an integer as it is, else up to 6 decimals ("0.3", "-1.25")
inline std::string NumberWord(double value)
{
    const bool negative = value < 0;
    const auto scaled = static_cast<int64_t>(std::llround((negative ? -value : value) * 1e6));
    std::string fraction = std::to_string(scaled % 1000000);
    fraction.insert(0, 6 - fraction.size(), '0');
    while (!fraction.empty() && fraction.back() == '0')
        fraction.pop_back();
    return std::string(negative && scaled != 0 ? "-" : "") + std::to_string(scaled / 1000000) +
           (fraction.empty() ? "" : "." + fraction);
}

inline bool OneOf(const std::vector<std::string>& list, const std::string& item)
{
    return std::find(list.begin(), list.end(), item) != list.end();
}

inline std::string Joined(const std::vector<std::string>& list)
{
    std::string text;
    for (const std::string& item : list)
        text += (text.empty() ? "" : ", ") + item;
    return text;
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
    if (k == "blending_decay")
    {
        double decay = 0;
        if (!ParseNumber(value, decay) || decay < kMinBlendingDecay - 1e-9 || decay > kMaxBlendingDecay + 1e-9)
        {
            error = "'blending_decay' takes 0.1..0.9 (each older frame's share of the newer one), not '" + value + "'";
            return false;
        }
        change.blendingDecay = decay;
        return true;
    }
    if (k == "fullscreen")
        return flag(change.fullscreen);
    if (k == "scale")
    {
        double scale = 0;
        if (!ParseNumber(value, scale) || scale != std::floor(scale) || scale < kMinScale || scale > kMaxScale)
        {
            error = "'scale' takes " + std::to_string(kMinScale) + ".." + std::to_string(kMaxScale) + ", not '" +
                    value + "'";
            return false;
        }
        change.scale = static_cast<int>(scale);
        return true;
    }
    if (k == "toolbar")
        return flag(change.toolbar);
    if (k == "status_bar" || k == "statusbar")
        return flag(change.statusBar);
    if (k == "hud")
        return flag(change.hud);
    if (k == "hud_theme")
    {
        change.hudTheme = Lower(value);
        return true;
    }
    const std::string noticePrefix = kHudNoticePrefix;
    if (k.size() > noticePrefix.size() && k.compare(0, noticePrefix.size(), noticePrefix) == 0)
    {
        std::optional<bool> on;
        if (!flag(on))
            return false;
        change.hudNotices[k.substr(noticePrefix.size())] = *on;
        return true;
    }
    if (k == "osd_keyboard")
        return flag(change.osdKeyboard);
    if (k == "osd_keyboard_picture")
    {
        change.osdKeyboardPicture = Lower(value);
        return true;
    }
    if (k == "next_layers")
        return flag(change.nextLayers);
    if (k == "next_layers_mode")
    {
        const std::string mode = Lower(value);
        if (!OneOf(NextLayersModes(), mode))
        {
            error = "'next_layers_mode' takes " + Joined(NextLayersModes()) + ", not '" + value + "'";
            return false;
        }
        change.nextLayersMode = mode;
        return true;
    }
    const std::string layerPrefix = kNextLayerPrefix;
    if (k.size() > layerPrefix.size() && k.compare(0, layerPrefix.size(), layerPrefix) == 0)
    {
        const std::string layer = k.substr(layerPrefix.size());
        if (!OneOf(NextLayerNames(), layer))
        {
            error = "Unknown ZX Next layer '" + layer + "': use " + Joined(NextLayerNames());
            return false;
        }
        std::optional<bool> on;
        if (!flag(on))
            return false;
        change.nextLayersShown[layer] = *on;
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
    if (change.hudTheme && !OneOf(now.settings.hudThemes, *change.hudTheme))
        return Fail(Status::BadParameter,
                    "Unknown HUD theme '" + *change.hudTheme + "': use " + Joined(now.settings.hudThemes));
    for (const auto& [id, on] : change.hudNotices)
    {
        (void)on;
        if (now.settings.hudNotices.find(id) == now.settings.hudNotices.end())
        {
            std::vector<std::string> ids;
            for (const auto& notice : now.settings.hudNotices)
                ids.push_back(notice.first);
            return Fail(Status::BadParameter, "Unknown HUD notice '" + id + "': use " + Joined(ids));
        }
    }
    if (change.osdKeyboardPicture && !OneOf(now.settings.osdKeyboardPictures, *change.osdKeyboardPicture))
    {
        if (now.settings.osdKeyboardPictures.empty())
            return Fail(Status::Unavailable, "The machine the window shows has no on-screen keyboard picture");
        return Fail(Status::BadParameter, "Unknown keyboard picture '" + *change.osdKeyboardPicture +
                                              "' for this machine: use " + Joined(now.settings.osdKeyboardPictures));
    }
    if (change.gpu && *change.gpu && !now.settings.gpuAvailable)
        return Fail(Status::Unavailable, "GPU acceleration is not available on this host (no OpenGL)");
    // A libretro preset or a custom shader ("shader:<name>", offered when the host has a GPU) runs on the GPU only: on
    // with this change or already on
    if (change.crtProfile && change.crtProfile->rfind("shader:", 0) == 0 && !change.gpu.value_or(now.settings.gpu))
        return Fail(Status::Unavailable, "A shader preset needs GPU acceleration: add gpu=on");

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
    node["blending_decay"] = s.blendingDecay;
    node["emulator"] = s.emulatorId;
    node["fullscreen"] = s.fullscreen;
    node["scale"] = s.scale;
    node["toolbar"] = s.toolbar;
    node["status_bar"] = s.statusBar;
    node["hud"] = s.hud;
    node["hud_theme"] = s.hudTheme;
    StateNode themes = StateNode::Array();
    for (const std::string& name : s.hudThemes)
        themes.items.emplace_back(name);
    node["hud_themes"] = themes;
    StateNode notices = StateNode::Object();
    for (const auto& [id, on] : s.hudNotices)
        notices[id] = on;
    node["hud_notices"] = notices;
    node["osd_keyboard"] = s.osdKeyboard;
    node["osd_keyboard_picture"] = s.osdKeyboardPicture;
    StateNode pictures = StateNode::Array();
    for (const std::string& name : s.osdKeyboardPictures)
        pictures.items.emplace_back(name);
    node["osd_keyboard_pictures"] = pictures;
    node["next_layers"] = s.nextLayers;
    node["next_layers_mode"] = s.nextLayersMode;
    StateNode layers = StateNode::Object();
    for (const auto& [name, on] : s.nextLayersShown)
        layers[name] = on;
    node["next_layers_shown"] = layers;
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

/// One line, e.g. "gpu on (available), crt basic, blending off (3 frames, exponential)"
inline std::string Summary(const WindowDisplaySettings& s)
{
    std::string line = std::string("gpu ") + (s.gpu ? "on" : "off") + (s.gpuAvailable ? " (available)" : " (unavailable)");
    line += std::string(", crt ") + (s.crt ? s.crtProfile : "off");
    line += std::string(", blending ") + (s.blending ? "on" : "off") + " (" + std::to_string(s.blendingFrames) +
            " frames, " + s.blendingWeights + (s.blendingWeights == "exponential" ? " " + NumberWord(s.blendingDecay) : "") +
            ")";
    line += s.fullscreen ? ", full screen" : (s.scale > 0 ? ", scale " + std::to_string(s.scale) + "x" : ", scale free");
    line += std::string(", hud ") + (s.hud ? "on" : "off") + (s.hudTheme.empty() ? "" : " (" + s.hudTheme + ")");
    if (s.osdKeyboard)
        line += ", keyboard " + (s.osdKeyboardPicture.empty() ? std::string("(no picture)") : s.osdKeyboardPicture);
    if (s.nextLayers)
        line += ", next layers " + s.nextLayersMode;
    return line;
}

/// region <CRT parameters>

/// Put one parameter word into `change`: `<name>=<value>` sets it, `<name>+=<steps>` / `<name>-=<steps>` moves it by
/// dial steps, `reset` resets all, `reset=<name>` one. Names are checked by SetCrtParameters
inline bool ApplyCrtParameterWord(CrtParameterChange& change, const std::string& word, std::string& error)
{
    if (Lower(word) == "reset")
    {
        change.resetAll = true;
        return true;
    }
    const size_t eq = word.find('=');
    if (eq == std::string::npos || eq == 0)
    {
        error = "Expected <name>=<value>, <name>+=<steps>, <name>-=<steps>, reset or reset=<name>, not '" + word + "'";
        return false;
    }
    std::string name = word.substr(0, eq);
    const std::string value = word.substr(eq + 1);
    if (Lower(name) == "reset")
    {
        change.reset.push_back(value);
        return true;
    }
    const char sign = name.back();
    const bool relative = sign == '+' || sign == '-';
    if (relative)
        name.pop_back();
    double number = 0;
    if (name.empty() || !ParseNumber(value, number))
    {
        error = "'" + word + "': a parameter takes a number";
        return false;
    }
    if (relative)
        change.steps.emplace_back(name, sign == '-' ? -number : number);
    else
        change.values.emplace_back(name, number);
    return true;
}

struct ParametersResult
{
    Status status = Status::Ok;
    std::string error;
    CrtParameters parameters;

    bool Ok() const { return status == Status::Ok; }
};

inline ParametersResult FailParameters(Status status, std::string error)
{
    ParametersResult r;
    r.status = status;
    r.error = std::move(error);
    return r;
}

/// The active CRT profile's parameters
inline ParametersResult GetCrtParameters()
{
    std::shared_ptr<IWindowViewProvider> provider = WindowView::Provider();
    if (!provider)
        return FailParameters(Status::NoWindow, "No window: the display settings exist only in the GUI (unreal-qt)");
    ParametersResult r;
    if (!provider->GetCrtParameters(r.parameters, r.error))
        return FailParameters(Status::NoWindow, r.error);
    return r;
}

/// Validate the names against the active profile's, apply, answer the parameters after it. An empty change reads
inline ParametersResult SetCrtParameters(const CrtParameterChange& change)
{
    ParametersResult now = GetCrtParameters();
    if (!now.Ok() || change.Empty())
        return now;
    auto known = [&](const std::string& name) {
        for (const CrtParameter& p : now.parameters.parameters)
            if (p.name == name)
                return true;
        return false;
    };
    std::vector<std::string> names;
    for (const auto& [name, value] : change.values)
        names.push_back(name);
    for (const auto& [name, steps] : change.steps)
        names.push_back(name);
    names.insert(names.end(), change.reset.begin(), change.reset.end());
    for (const std::string& name : names)
    {
        if (known(name))
            continue;
        if (now.parameters.parameters.empty())
            return FailParameters(Status::Unavailable, "The CRT profile '" + now.parameters.profile +
                                                           "' has no parameters (crt_profile basic or a shader:<name> has)");
        std::string list;
        for (const CrtParameter& p : now.parameters.parameters)
            list += (list.empty() ? "" : ", ") + p.name;
        return FailParameters(Status::BadParameter, "Unknown parameter '" + name + "' of the CRT profile '" +
                                                        now.parameters.profile + "': use " + list);
    }
    std::shared_ptr<IWindowViewProvider> provider = WindowView::Provider();
    if (!provider)
        return FailParameters(Status::NoWindow, "No window: the display settings exist only in the GUI (unreal-qt)");
    ParametersResult r;
    if (!provider->SetCrtParameters(change, r.parameters, r.error))
        return FailParameters(Status::NoWindow, r.error);
    return r;
}

/// {profile, kind, parameters: [{name, description, value, initial, minimum, maximum, step}]}
inline StateNode CrtParametersReport(const CrtParameters& p)
{
    StateNode node = StateNode::Object();
    node["profile"] = p.profile;
    node["kind"] = p.kind;
    StateNode list = StateNode::Array();
    for (const CrtParameter& parameter : p.parameters)
    {
        StateNode item = StateNode::Object();
        item["name"] = parameter.name;
        item["description"] = parameter.description;
        item["value"] = parameter.value;
        item["initial"] = parameter.initial;
        item["minimum"] = parameter.minimum;
        item["maximum"] = parameter.maximum;
        item["step"] = parameter.step;
        list.items.push_back(std::move(item));
    }
    node["parameters"] = list;
    return node;
}

inline StateNode ErrorNode(const ParametersResult& r)
{
    StateNode node = StateNode::Object();
    node["ok"] = false;
    node["error"] = r.error;
    node["kind"] = StatusName(r.status);
    return node;
}

/// One line per parameter after a header: "crt-geom (preset), 12 parameters" / "  CRTgamma = 2.4 (0.1..5, step 0.1)"
inline std::string CrtParametersSummary(const CrtParameters& p)
{
    std::string text = "CRT profile " + p.profile + " (" + p.kind + "), " + std::to_string(p.parameters.size()) +
                       " parameter" + (p.parameters.size() == 1 ? "" : "s");
    for (const CrtParameter& parameter : p.parameters)
        text += "\n  " + parameter.name + " = " + NumberWord(parameter.value) + " (" + NumberWord(parameter.minimum) +
                ".." + NumberWord(parameter.maximum) + ", step " + NumberWord(parameter.step) +
                (parameter.value != parameter.initial ? ", initial " + NumberWord(parameter.initial) : std::string()) +
                ")" + (parameter.description.empty() ? "" : " " + parameter.description);
    return text;
}

/// endregion </CRT parameters>
}  // namespace WindowDisplay
