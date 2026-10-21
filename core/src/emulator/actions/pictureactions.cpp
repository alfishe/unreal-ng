#include "emulator/actions/pictureactions.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "emulator/actions/actionparams.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/emulator.h"
#include "emulator/execution/automationgate.h"
#include "emulator/video/viewportcontrol.h"
#include "emulator/video/windowdisplay.h"

namespace actions
{
namespace
{
/// region <Rules and results>

/// The window's actions change the GUI only: the route they stand for (PUT /video/window) changes nothing
ActionRules WindowRules()
{
    ActionRules rules;
    const uint32_t scope = automation_gate::ChangesMachine("PUT", "/video/window")
                               ? automation_gate::CompanionScopeOf("PUT", "/video/window")
                               : 0;
    rules.scope = ActionRules::Fixed(scope);
    rules.unavailable = [](Emulator&, const ActionValue&) -> std::string {
        return WindowView::Provider() ? std::string() : std::string("no window: the display settings exist only in the GUI");
    };
    return rules;
}

/// A toggle of the window: its state from the published settings
ActionRules WindowToggle(std::function<bool(const WindowDisplaySettings&)> on)
{
    ActionRules rules = WindowRules();
    rules.state = [on = std::move(on)](Emulator&, const ActionValue&) -> std::optional<bool> {
        const std::optional<WindowDisplaySettings> settings = WindowView::PublishedSettings();
        if (!settings)
            return std::nullopt;
        return on(*settings);
    };
    return rules;
}

ActionResult BadRequest(std::string message)
{
    return ActionResult::Failure("Bad Request", std::move(message));
}

ActionResult FromWindow(const WindowDisplay::Result& r, const std::string& message)
{
    if (!r.Ok())
        return ActionResult::Failure(r.status == WindowDisplay::Status::BadParameter ? "Bad Request" : "Conflict",
                                     r.error);
    return ActionResult::Success(WindowDisplay::Report(r.settings), message);
}

/// The window's settings now (asked, not the published copy: a flip must start from the truth)
std::optional<WindowDisplaySettings> Current(ActionResult& failure)
{
    const WindowDisplay::Result now = WindowDisplay::Get();
    if (!now.Ok())
    {
        failure = FromWindow(now, {});
        return std::nullopt;
    }
    return now.settings;
}

std::string OnOff(bool on)
{
    return on ? "on" : "off";
}

/// `on` (a bool param) or the flip of `now`; nullopt with `failure` for a bad param
std::optional<bool> OnOrFlip(const ActionValue& value, bool now, ActionResult& failure)
{
    std::string error;
    const std::optional<bool> on = BoolParam(value, "on", error);
    if (!error.empty())
    {
        failure = BadRequest(error);
        return std::nullopt;
    }
    return on.value_or(!now);
}

/// The item of `list` a cycling action goes to: `to` itself, or "next" / "prev" (none: next) from `now`
std::string Cycle(const std::vector<std::string>& list, const std::string& now, const std::string& to)
{
    const std::string word = WindowDisplay::Lower(to);
    if (!word.empty() && word != "next" && word != "prev")
        return word;
    if (list.empty())
        return now;
    const auto at = std::find(list.begin(), list.end(), now);
    const size_t count = list.size();
    const size_t index = at == list.end() ? count - 1 : static_cast<size_t>(at - list.begin());
    return list[word == "prev" ? (index + count - 1) % count : (index + 1) % count];
}

/// A simple on / off switch of the window: `field` of the change, `read` of the settings
template <typename Field>
Executor Switch(const char* what, Field field, std::function<bool(const WindowDisplaySettings&)> read)
{
    return [what, field, read = std::move(read)](const ActionCall& call) {
        ActionResult failure;
        const std::optional<WindowDisplaySettings> now = Current(failure);
        if (!now)
            return failure;
        const std::optional<bool> on = OnOrFlip(call.value, read(*now), failure);
        if (!on)
            return failure;
        WindowDisplayChange change;
        change.*field = *on;
        return FromWindow(WindowDisplay::Set(change), std::string(what) + " " + OnOff(*on));
    };
}

/// endregion </Rules and results>

/// region <Readouts>

StateNode Unavailable(const char* why)
{
    StateNode node = StateNode::Object();
    node["available"] = false;
    node["reason"] = why;
    return node;
}

/// endregion </Readouts>
}  // namespace

std::vector<RegistrationHandle> RegisterPictureActions(ActionRegistry& registry)
{
    std::vector<RegistrationHandle> handles;
    auto add = [&](const char* id, const char* label, const char* group, ValueKind kind, const char* params,
                   const char* description, Executor run, ActionRules rules) {
        handles.push_back(registry.Register({id, label, group, kind, false, false, params, description, false},
                                            std::move(run), std::move(rules)));
    };
    using Change = WindowDisplayChange;

    // region <Video: GPU, CRT, blending, viewport>
    const char* video = "Video";
    add("video.gpu_toggle", "GPU acceleration", video, ValueKind::Press, "on? (else flips)",
        "Switch the window's GPU acceleration on or off (the CRT presets need it).",
        Switch("GPU acceleration", &Change::gpu, [](const WindowDisplaySettings& s) { return s.gpu; }),
        WindowToggle([](const WindowDisplaySettings& s) { return s.gpu; }));
    add("video.crt_toggle", "CRT effects", video, ValueKind::Press, "on? (else flips)",
        "Switch the window's CRT effects on or off (the chosen profile stays).",
        Switch("CRT effects", &Change::crt, [](const WindowDisplaySettings& s) { return s.crt; }),
        WindowToggle([](const WindowDisplaySettings& s) { return s.crt; }));
    add(
        "video.crt_profile", "CRT profile", video, ValueKind::Press,
        "profile? (a name of crt_profiles, next, prev; next)",
        "Choose the window's CRT profile by name, or the next / previous one (a shader only with the GPU on).",
        [](const ActionCall& call) {
            ActionResult failure;
            const std::optional<WindowDisplaySettings> now = Current(failure);
            if (!now)
                return failure;
            // Cycling skips what cannot show now: a shader without the GPU
            std::vector<std::string> offered;
            for (const std::string& name : now->crtProfiles)
                if (now->gpu || name.rfind("shader:", 0) != 0)
                    offered.push_back(name);
            Change change;
            change.crtProfile = Cycle(offered, now->crtProfile, TextParam(call.value, "profile").value_or(""));
            return FromWindow(WindowDisplay::Set(change), "CRT profile " + *change.crtProfile);
        },
        WindowRules());
    add(
        "video.crt_param_by", "CRT parameter by a dial", video, ValueKind::Delta,
        "name (a parameter of the active profile), delta = steps",
        "Move one parameter of the active CRT profile by a dial's steps (its own step size, clamped to its range).",
        [](const ActionCall& call) {
            const std::string name = TextParam(call.value, "name").value_or("");
            if (name.empty())
                return BadRequest("name: a parameter of the active CRT profile (video window crt_params lists them)");
            CrtParameterChange change;
            change.steps.emplace_back(name, call.value.delta);
            const WindowDisplay::ParametersResult r = WindowDisplay::SetCrtParameters(change);
            if (!r.Ok())
                return ActionResult::Failure(
                    r.status == WindowDisplay::Status::BadParameter ? "Bad Request" : "Conflict", r.error);
            StateNode body = WindowDisplay::CrtParametersReport(r.parameters);
            std::string message = name;
            for (const CrtParameter& p : r.parameters.parameters)
                if (p.name == name)
                {
                    body["value"] = p.value;
                    message += " = " + WindowDisplay::NumberWord(p.value);
                }
            return ActionResult::Success(std::move(body), message);
        },
        WindowRules());
    add(
        "video.crt_params_reset", "Reset CRT parameters", video, ValueKind::Press, "name? (one; else all)",
        "Put the active CRT profile's parameters (or one) back to their initial values.",
        [](const ActionCall& call) {
            CrtParameterChange change;
            const std::string name = TextParam(call.value, "name").value_or("");
            if (name.empty())
                change.resetAll = true;
            else
                change.reset.push_back(name);
            const WindowDisplay::ParametersResult r = WindowDisplay::SetCrtParameters(change);
            if (!r.Ok())
                return ActionResult::Failure(
                    r.status == WindowDisplay::Status::BadParameter ? "Bad Request" : "Conflict", r.error);
            return ActionResult::Success(WindowDisplay::CrtParametersReport(r.parameters),
                                         name.empty() ? "CRT parameters reset" : name + " reset");
        },
        WindowRules());
    add(
        "video.blending", "Frame blending", video, ValueKind::Press,
        "on? (else flips), frames? (2..5), weights? (equal|exponential), decay? (0.1..0.9)",
        "Switch the window's temporal blending (gigascreen smoothing) on or off, with its frames, weights and decay.",
        [](const ActionCall& call) {
            ActionResult failure;
            const std::optional<WindowDisplaySettings> now = Current(failure);
            if (!now)
                return failure;
            std::string error;
            const bool hasOn = call.value.params.find("on") != nullptr;
            const bool tunes = call.value.params.find("frames") != nullptr ||
                               call.value.params.find("weights") != nullptr ||
                               call.value.params.find("decay") != nullptr;
            Change change;
            for (const char* key : {"frames", "weights", "decay"})
            {
                // A number in words without the locale (TextParam's double text follows it)
                const StateNode* node = call.value.params.find(key);
                const std::optional<std::string> word = node != nullptr && node->kind == StateNode::Kind::Double
                                                            ? std::optional<std::string>(WindowDisplay::NumberWord(node->d))
                                                            : TextParam(call.value, key);
                if (word && !WindowDisplay::ApplyWord(change, std::string("blending_") + key, *word, error))
                    return BadRequest(error);
            }
            // Only tuning: the switch stays; else on, or the flip
            if (hasOn || !tunes)
            {
                const std::optional<bool> on = OnOrFlip(call.value, now->blending, failure);
                if (!on)
                    return failure;
                change.blending = *on;
            }
            const WindowDisplay::Result r = WindowDisplay::Set(change);
            return FromWindow(r, std::string("Frame blending ") + OnOff(r.Ok() ? r.settings.blending : false));
        },
        WindowToggle([](const WindowDisplaySettings& s) { return s.blending; }));
    add(
        "video.blending_decay_by", "Blending decay by a dial", video, ValueKind::Delta, "delta = tenths",
        "Move the exponential blending's decay by a dial's steps of 0.1 (0.1..0.9).",
        [](const ActionCall& call) {
            ActionResult failure;
            const std::optional<WindowDisplaySettings> now = Current(failure);
            if (!now)
                return failure;
            // In tenths, rounded: the decay stays on the dialog's 0.1 grid
            const double tenths = std::round(now->blendingDecay * 10.0) + std::round(call.value.delta);
            Change change;
            change.blendingDecay = std::clamp(tenths, 1.0, 9.0) / 10.0;
            change.blendingWeights = "exponential";
            return FromWindow(WindowDisplay::Set(change),
                              "Blending decay " + WindowDisplay::NumberWord(*change.blendingDecay));
        },
        WindowRules());
    {
        ActionRules rules;
        rules.scope = ActionRules::Fixed(automation_gate::ChangesMachine("PUT", "/video/viewport")
                                             ? automation_gate::CompanionScopeOf("PUT", "/video/viewport")
                                             : 0);
        add(
            "video.viewport", "Overscan viewport", video, ValueKind::Press,
            "viewport? (full|symmetric|standard|screen_only|next|prev; next)",
            "Choose how the overscan frame is cropped: full, symmetric, standard, screen only, or the next one.",
            [](const ActionCall& call) {
                const VideoViewport::Result r =
                    VideoViewport::SetViewport(*call.emulator, TextParam(call.value, "viewport").value_or("next"));
                if (!r.Ok())
                    return ActionResult::Failure(r.status == 400 ? "Bad Request" : "Conflict", r.error);
                return ActionResult::Success(VideoViewport::Report(*call.emulator),
                                             VideoViewport::Summary(*call.emulator));
            },
            std::move(rules));
    }
    // endregion </Video: GPU, CRT, blending, viewport>

    // region <View: full screen, scale, bars, Next layers>
    const char* view = "View";
    add("view.fullscreen", "Full screen", view, ValueKind::Press, "on? (else flips)",
        "Show the window full screen, or leave full screen.",
        Switch("Full screen", &Change::fullscreen, [](const WindowDisplaySettings& s) { return s.fullscreen; }),
        WindowToggle([](const WindowDisplaySettings& s) { return s.fullscreen; }));
    add(
        "view.scale", "Window scale", view, ValueKind::Press, "scale? (1..4, next, prev; next)",
        "Fit the window to the picture at 1x..4x (leaves full screen), or the next / previous scale.",
        [](const ActionCall& call) {
            ActionResult failure;
            const std::optional<WindowDisplaySettings> now = Current(failure);
            if (!now)
                return failure;
            const std::vector<std::string> scales = {"1", "2", "3", "4"};
            const std::string target =
                Cycle(scales, std::to_string(now->scale), TextParam(call.value, "scale").value_or(""));
            Change change;
            std::string error;
            if (!WindowDisplay::ApplyWord(change, "scale", target, error))
                return BadRequest(error);
            // The window drops to the largest whole zoom the display holds: the message says what it got
            const WindowDisplay::Result r = WindowDisplay::Set(change);
            const std::string got = r.Ok() && r.settings.scale > 0 && std::to_string(r.settings.scale) != target
                                        ? " (" + std::to_string(r.settings.scale) + "x fits the display)"
                                        : std::string();
            return FromWindow(r, "Scale " + target + "x" + got);
        },
        WindowRules());
    add("view.toolbar", "Toolbar", view, ValueKind::Press, "on? (else flips)", "Show or hide the window's toolbar.",
        Switch("Toolbar", &Change::toolbar, [](const WindowDisplaySettings& s) { return s.toolbar; }),
        WindowToggle([](const WindowDisplaySettings& s) { return s.toolbar; }));
    add("view.status_bar", "Status bar", view, ValueKind::Press, "on? (else flips)",
        "Show or hide the window's status bar.",
        Switch("Status bar", &Change::statusBar, [](const WindowDisplaySettings& s) { return s.statusBar; }),
        WindowToggle([](const WindowDisplaySettings& s) { return s.statusBar; }));
    add("view.next_layers", "Next layers window", view, ValueKind::Press, "on? (else flips)",
        "Open or close the ZX Next layers window.",
        Switch("Next layers", &Change::nextLayers, [](const WindowDisplaySettings& s) { return s.nextLayers; }),
        WindowToggle([](const WindowDisplaySettings& s) { return s.nextLayers; }));
    add(
        "view.next_layers_mode", "Next layers mode", view, ValueKind::Press,
        "mode? (layers|composed|winner|next|prev; next)",
        "Show the ZX Next layers window's layers, the composed picture or the winning layer per pixel.",
        [](const ActionCall& call) {
            ActionResult failure;
            const std::optional<WindowDisplaySettings> now = Current(failure);
            if (!now)
                return failure;
            Change change;
            std::string error;
            const std::string mode =
                Cycle(WindowDisplay::NextLayersModes(), now->nextLayersMode, TextParam(call.value, "mode").value_or(""));
            if (!WindowDisplay::ApplyWord(change, "next_layers_mode", mode, error))
                return BadRequest(error);
            return FromWindow(WindowDisplay::Set(change), "Next layers " + mode);
        },
        WindowRules());
    {
        ActionRules rules = WindowRules();
        rules.toggleStem = "view.next_layer";
        rules.toggleParam = "name";
        rules.state = [](Emulator&, const ActionValue& value) -> std::optional<bool> {
            const std::optional<WindowDisplaySettings> settings = WindowView::PublishedSettings();
            const std::optional<std::string> name = TextParam(value, "name");
            if (!settings || !name)
                return std::nullopt;
            const auto it = settings->nextLayersShown.find(*name);
            if (it == settings->nextLayersShown.end())
                return std::nullopt;
            return it->second;
        };
        add(
            "view.next_layer", "Next layer tick", view, ValueKind::Press, "name (ula|layer2|tilemap|sprites), on?",
            "Tick or untick one layer of the ZX Next layers window.",
            [](const ActionCall& call) {
                const std::string name = TextParam(call.value, "name").value_or("");
                ActionResult failure;
                const std::optional<WindowDisplaySettings> now = Current(failure);
                if (!now)
                    return failure;
                const auto it = now->nextLayersShown.find(name);
                if (it == now->nextLayersShown.end())
                    return BadRequest("name: one of " + WindowDisplay::Joined(WindowDisplay::NextLayerNames()));
                const std::optional<bool> on = OnOrFlip(call.value, it->second, failure);
                if (!on)
                    return failure;
                Change change;
                change.nextLayersShown[name] = *on;
                return FromWindow(WindowDisplay::Set(change), "Next layer " + name + " " + OnOff(*on));
            },
            std::move(rules));
    }
    // endregion </View: full screen, scale, bars, Next layers>

    // region <HUD and the on-screen keyboard>
    const char* hud = "HUD";
    add("hud.toggle", "HUD", hud, ValueKind::Press, "on? (else flips)", "Show or hide the HUD over the picture.",
        Switch("HUD", &Change::hud, [](const WindowDisplaySettings& s) { return s.hud; }),
        WindowToggle([](const WindowDisplaySettings& s) { return s.hud; }));
    add(
        "hud.theme", "HUD theme", hud, ValueKind::Press, "theme? (a name of hud_themes, next, prev; next)",
        "Choose the HUD's theme by name, or the next / previous one.",
        [](const ActionCall& call) {
            ActionResult failure;
            const std::optional<WindowDisplaySettings> now = Current(failure);
            if (!now)
                return failure;
            Change change;
            change.hudTheme = Cycle(now->hudThemes, now->hudTheme, TextParam(call.value, "theme").value_or(""));
            return FromWindow(WindowDisplay::Set(change), "HUD theme " + *change.hudTheme);
        },
        WindowRules());
    {
        ActionRules rules = WindowRules();
        rules.toggleStem = "hud.notice";
        rules.toggleParam = "name";
        rules.state = [](Emulator&, const ActionValue& value) -> std::optional<bool> {
            const std::optional<WindowDisplaySettings> settings = WindowView::PublishedSettings();
            const std::optional<std::string> name = TextParam(value, "name");
            if (!settings || !name)
                return std::nullopt;
            const auto it = settings->hudNotices.find(*name);
            if (it == settings->hudNotices.end())
                return std::nullopt;
            return it->second;
        };
        add(
            "hud.notice", "HUD notice", hud, ValueKind::Press, "name (a hud_notices id: ram-page, tape-events, ...), on?",
            "Show or hide one kind of HUD notice (RAM pages, tape events, breakpoints, ...).",
            [](const ActionCall& call) {
                const std::string name = TextParam(call.value, "name").value_or("");
                ActionResult failure;
                const std::optional<WindowDisplaySettings> now = Current(failure);
                if (!now)
                    return failure;
                const auto it = now->hudNotices.find(name);
                if (it == now->hudNotices.end())
                {
                    std::vector<std::string> ids;
                    for (const auto& notice : now->hudNotices)
                        ids.push_back(notice.first);
                    return BadRequest("name: one of " + WindowDisplay::Joined(ids));
                }
                const std::optional<bool> on = OnOrFlip(call.value, it->second, failure);
                if (!on)
                    return failure;
                Change change;
                change.hudNotices[name] = *on;
                return FromWindow(WindowDisplay::Set(change), "HUD notice " + name + " " + OnOff(*on));
            },
            std::move(rules));
    }
    add(
        "osd.keyboard_picture", "Keyboard picture", "Capture and machine", ValueKind::Press,
        "picture? (one of osd_keyboard_pictures, next, prev; next)",
        "Choose the on-screen keyboard's picture for the machine shown, or the next one (Pentagon, Profi, PC clones).",
        [](const ActionCall& call) {
            ActionResult failure;
            const std::optional<WindowDisplaySettings> now = Current(failure);
            if (!now)
                return failure;
            Change change;
            change.osdKeyboardPicture = Cycle(now->osdKeyboardPictures, now->osdKeyboardPicture,
                                              TextParam(call.value, "picture").value_or(""));
            return FromWindow(WindowDisplay::Set(change), "Keyboard picture " + *change.osdKeyboardPicture);
        },
        WindowRules());
    // endregion </HUD and the on-screen keyboard>
    return handles;
}

const std::vector<std::string>& PictureReadoutNames()
{
    static const std::vector<std::string> names = {"video.window", "video.crt_params", "video.viewport"};
    return names;
}

StateNode ReadPictureReadout(Emulator& emulator, const std::string& name)
{
    if (name == "video.window")
    {
        const std::optional<WindowDisplaySettings> settings = WindowView::PublishedSettings();
        if (!settings)
            return Unavailable("no window");
        StateNode node = WindowDisplay::Report(*settings);
        node["available"] = true;
        return node;
    }
    if (name == "video.crt_params")
    {
        const std::optional<CrtParameters> parameters = WindowView::PublishedCrtParameters();
        if (!parameters)
            return Unavailable("no window");
        // name -> value: what a key shows; the ranges are the crt_params verb's
        StateNode node = StateNode::Object();
        node["available"] = true;
        node["profile"] = parameters->profile;
        node["kind"] = parameters->kind;
        StateNode values = StateNode::Object();
        for (const CrtParameter& p : parameters->parameters)
            values[p.name] = p.value;
        node["values"] = values;
        return node;
    }
    if (name == "video.viewport")
        return VideoViewport::Report(emulator);
    return {};
}

}  // namespace actions
