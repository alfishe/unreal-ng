#include "emulator/video/viewportcontrol.h"

#include <algorithm>
#include <cctype>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

namespace VideoViewport
{
namespace
{
const DisplayViewport kPresets[] = {ViewportPresets::FULL_OVERSCAN, ViewportPresets::SYMMETRIC_HORIZONTAL,
                                    ViewportPresets::STANDARD, ViewportPresets::SCREEN_ONLY};

bool Same(const DisplayViewport& a, const DisplayViewport& b)
{
    return a.cropLeft == b.cropLeft && a.cropRight == b.cropRight && a.cropTop == b.cropTop &&
           a.cropBottom == b.cropBottom;
}

std::string Lower(std::string word)
{
    std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::replace(word.begin(), word.end(), '-', '_');
    return word;
}

Screen* ScreenOf(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    return context != nullptr ? context->pScreen : nullptr;
}

bool HasOverscanFrame(Screen& screen)
{
    const VideoModeEnum mode = screen.GetVideoMode();
    return mode == M_PENTAGON128K || mode == M_P384;
}

Result Fail(int status, std::string error)
{
    Result r;
    r.status = status;
    r.error = std::move(error);
    return r;
}
}  // namespace

const std::vector<std::string>& Names()
{
    static const std::vector<std::string> names = {"full", "symmetric", "standard", "screen_only"};
    return names;
}

std::string NameList()
{
    std::string list;
    for (const std::string& name : Names())
        list += (list.empty() ? "" : ", ") + name;
    return list;
}

bool Parse(const std::string& name, DisplayViewport& out)
{
    const std::string word = Lower(name);
    const std::vector<std::string>& names = Names();
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (names[i] == word)
        {
            out = kPresets[i];
            return true;
        }
    }
    return false;
}

std::string NameOf(const DisplayViewport& viewport)
{
    const std::vector<std::string>& names = Names();
    for (size_t i = 0; i < names.size(); ++i)
        if (Same(viewport, kPresets[i]))
            return names[i];
    return "custom";
}

StateNode Report(Emulator& emulator)
{
    StateNode node = StateNode::Object();
    Screen* screen = ScreenOf(emulator);
    const bool overscan = emulator.IsOverscanMode();
    node["overscan"] = overscan;
    node["overscan_available"] = screen != nullptr && HasOverscanFrame(*screen);
    node["viewport"] = overscan ? NameOf(emulator.GetDisplayViewport()) : std::string("full");
    StateNode names = StateNode::Array();
    for (const std::string& name : Names())
        names.items.emplace_back(name);
    node["viewports"] = names;
    if (screen != nullptr)
    {
        const FramebufferDescriptor& fb = screen->GetFramebufferDescriptor();
        node["framebuffer"] = std::to_string(fb.width) + "x" + std::to_string(fb.height);
        node["display"] = std::to_string(screen->GetDisplayWidth()) + "x" + std::to_string(screen->GetDisplayHeight());
        node["video_mode"] = Screen::GetVideoModeName(screen->GetVideoMode());
    }
    return node;
}

std::string Summary(Emulator& emulator)
{
    const StateNode report = Report(emulator);
    auto text = [&](const char* key) {
        const StateNode* value = report.find(key);
        return value != nullptr && value->kind == StateNode::Kind::String ? value->s : std::string();
    };
    std::string line = std::string("overscan ") + (emulator.IsOverscanMode() ? "on" : "off");
    const StateNode* available = report.find("overscan_available");
    if (available != nullptr && available->kind == StateNode::Kind::Bool && !available->b)
        line += " (no overscan frame on this machine)";
    line += ", viewport " + text("viewport") + " (" + text("display") + " of " + text("framebuffer") + ")";
    return line;
}

Result SetOverscan(Emulator& emulator, bool enable, const std::string& viewport)
{
    DisplayViewport crop = enable ? ViewportPresets::SYMMETRIC_HORIZONTAL : ViewportPresets::FULL_OVERSCAN;
    if (!viewport.empty() && !Parse(viewport, crop))
        return Fail(400, "Unknown viewport '" + viewport + "': use " + NameList());
    if (!enable && !viewport.empty() && Lower(viewport) != "full")
        return Fail(400, "A viewport crops the overscan frame: without overscan it is full");
    Screen* screen = ScreenOf(emulator);
    if (screen == nullptr)
        return Fail(409, "The machine has no screen");

    // Only the Pentagon has the larger frame; asking for the mode it is already in is not an error
    Result r;
    r.changed = emulator.SetOverscanMode(enable);
    if (!r.changed && emulator.IsOverscanMode() != enable)
        return Fail(409, "Only a Pentagon has an overscan frame; this machine's video mode is " +
                             Screen::GetVideoModeName(screen->GetVideoMode()));
    emulator.SetDisplayViewport(crop);
    return r;
}

Result SetViewport(Emulator& emulator, const std::string& name)
{
    const std::string word = Lower(name);
    std::string target = word;
    if (word == "next" || word == "prev")
    {
        const std::vector<std::string>& names = Names();
        const std::string now = emulator.IsOverscanMode() ? NameOf(emulator.GetDisplayViewport()) : "full";
        const auto at = std::find(names.begin(), names.end(), now);
        const size_t index = at == names.end() ? 0 : static_cast<size_t>(at - names.begin());
        const size_t count = names.size();
        target = names[word == "next" ? (index + 1) % count : (index + count - 1) % count];
    }
    DisplayViewport crop;
    if (!Parse(target, crop))
        return Fail(400, "Unknown viewport '" + name + "': use " + NameList() + ", next, prev");
    if (!emulator.IsOverscanMode())
    {
        if (target == "full")
            return Result();
        return Fail(409, "A viewport crops the overscan frame: switch overscan on first (a Pentagon only)");
    }
    Result r;
    r.changed = !Same(crop, emulator.GetDisplayViewport());
    emulator.SetDisplayViewport(crop);
    return r;
}

}  // namespace VideoViewport
