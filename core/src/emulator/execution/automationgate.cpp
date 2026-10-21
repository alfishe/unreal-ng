#include "automationgate.h"

#include <array>
#include <initializer_list>
#include <string_view>
#include <utility>

namespace automation_gate
{
namespace
{
bool StartsWith(std::string_view text, std::string_view prefix)
{
    return text.substr(0, prefix.size()) == prefix;
}

/// Requests that do not change the machine, by subpath prefix (a whole segment: "/profiler" covers
/// "/profiler/opcode/start", not "/profilers")
constexpr std::array<std::string_view, 29> kNoChange = {
    "/pause",                                   // always allowed (execution arbiter design §3.7)
    "/control/session",                         // the session itself
    "/control/take-back", "/control/allow-again",   // a companion's take-back: its handler checks the companion
    "/profiler", "/coverage", "/analyzer",      // observers
    "/capture", "/audio/capture", "/audio/mixer", "/video/record", "/video/temporal",
    "/video/window",                            // the GUI window's display settings (GPU, CRT, blending, HUD, ...)
    "/video/viewport",                          // the crop of the overscan frame (overscan itself: /state/screen/overscan)
    "/labels", "/symbols",                      // the debugger's names, not the machine
    "/debug/pchist", "/memory/find",
    "/snapshot/save", "/snapshot/inspect",
    "/ttd/start", "/ttd/stop", "/ttd/invalidate", "/ttd/export-clip",   // recording, not seeking
    "/next/layer-capture", "/vdac2", "/network/traffic",
    "/asm/sync/watch", "/asm/sync/push-watch",
};

bool IsJournal(std::string_view subpath)
{
    // The debugger's journals: /next/reg-journal, /next/sd-journal, /sprinter/pld-journal, ...
    const size_t slash = subpath.find('/', 1);
    return slash != std::string_view::npos && subpath.size() > 8 &&
           subpath.substr(subpath.size() - 8) == "-journal";
}
}  // namespace

bool ChangesMachine(const std::string& method, const std::string& subpath)
{
    if (method == "GET" || method == "HEAD" || method == "OPTIONS")
        return false;
    if (subpath.empty())                        // DELETE of the instance: the session ends with it
        return false;
    std::string_view path(subpath);
    if (const size_t query = path.find('?'); query != std::string_view::npos)
        path = path.substr(0, query);
    for (std::string_view prefix : kNoChange)
        if (StartsWith(path, prefix) && (path.size() == prefix.size() || path[prefix.size()] == '/'))
            return false;
    return !IsJournal(path);
}

bool CliCommandChangesMachine(const std::string& command, const std::vector<std::string>& args)
{
    static const std::array<std::string_view, 49> kAlways = {
        "reset", "nmi", "mni", "switch", "resume", "step", "stepin", "steps", "stepover", "stepout", "skip_until",
        "run_tstates", "run_to_scanline", "run_scanlines", "run_to_pixel", "run_to_interrupt", "run_frame",
        "run_frames", "run_ncycles", "out", "romflash", "open", "assemble",
        "bp", "break", "breakpoint", "wp", "bport", "bnextreg", "bbeam", "bmagic", "bcopper", "bphits", "bpclear",
        "bpgroup", "bpon", "bpoff", "bptoggle", "bkey", "bpnote",
        "key", "keyboard", "mouse", "joystick", "debugmode", "model", "rzx", "zxpoly", "start"};
    for (std::string_view always : kAlways)
        if (command == always)
            return !(command == "rzx" && (args.empty() || args[0] == "status" || args[0] == "help")) &&
                   !(command == "model" && (args.empty() || args[0] == "help")) &&
                   !(command == "debugmode" && args.empty()) && !(command == "zxpoly" && args.empty());
    if (args.empty())
        return false;
    const std::string& verb = args[0];
    auto any = [&](std::initializer_list<std::string_view> verbs) {
        for (std::string_view v : verbs)
            if (verb == v)
                return true;
        return false;
    };
    if (command == "memory" || command == "mem" || command == "m")
        return any({"write", "fill", "load"});
    if (command == "registers" || command == "regs" || command == "r")
        return args.size() >= 2;                            // "r pc 8000": a register set
    if (command == "pace")                                  // the paced run: `pace` / `pace show` only read
        return !any({"show", "help", "presets"});
    if (command == "tape")
        return any({"load", "eject", "play", "pause", "rewind", "seek", "stop"});
    if (command == "snapshot")
        return any({"load"});
    if (command == "ttd")
        return any({"back", "forward", "goto", "seek", "sb", "sf", "si-back", "si-forward", "step-back",
                    "step-forward", "step-instruction", "reverse-step", "rs", "reverse-continue", "rc", "resume",
                    "load", "open"});
    if (command == "disk")
        return any({"create", "eject", "insert", "write"});
    if (command == "basic")
        return any({"inject"});
    if (command == "media" || command == "slots")
        return !any({"compose", "formats", "help", "--help", "info", "list", "targets"}) && args.size() >= 2;
    if (command == "bus")                                   // an SD card's timing profile; tracing only observes
        return args.size() >= 3 && (args[1] == "sd-timing" || args[1] == "sdtiming") && args[2].find('=') == std::string::npos;
    if (command == "feature" || command == "setting" || command == "settings" || command == "set" ||
        command == "rtc" || command == "cmos")
        return args.size() >= 2;
    return false;
}

std::string ClientFromUserAgent(const std::string& userAgent)
{
    const size_t start = userAgent.find_first_not_of(' ');
    if (start == std::string::npos)
        return "HTTP client";
    const size_t end = userAgent.find(' ', start);
    return userAgent.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

namespace
{
bool OneOf(std::string_view value, std::initializer_list<std::string_view> values)
{
    for (std::string_view v : values)
        if (value == v)
            return true;
    return false;
}

/// The settings and features a companion may switch, by the scope they belong to (WebAPI and CLI names)
uint32_t SettingScope(std::string_view name)
{
    if (OneOf(name, {"speed", "turbo_mode", "turbo_audio"}))
        return ScopeRun;
    if (OneOf(name, {"fast_tape", "turbo_tape", "fast_disk"}))
        return ScopeMedia;
    return ScopeMachine;  // every other setting is the machine's configuration
}

uint32_t FeatureScope(std::string_view name)
{
    if (OneOf(name, {"turbomode", "turbo"}))
        return ScopeRun;
    if (OneOf(name, {"fasttape", "ftape", "turbotape", "ttape", "fastdisk", "fdisk"}))
        return ScopeMedia;
    if (OneOf(name, {"debugmode", "breakpoints", "memorycounters", "calltrace"}))
        return ScopeDebug;
    return ScopeMachine;  // every other feature is the machine's configuration
}

constexpr std::array<std::pair<std::string_view, uint32_t>, 7> kScopeNames = {{
    {"run", ScopeRun},
    {"input", ScopeInput},
    {"media", ScopeMedia},
    {"machine", ScopeMachine},
    {"rewind", ScopeRewind},
    {"takeback", ScopeTakeBack},
    {"debug", ScopeDebug},
}};
}  // namespace

uint32_t CompanionScopeOf(const std::string& method, const std::string& subpath)
{
    (void)method;  // the gate asks only for changing requests: POST, PUT, DELETE
    std::string_view path(subpath);
    if (const size_t query = path.find('?'); query != std::string_view::npos)
        path = path.substr(0, query);
    std::vector<std::string_view> segments;
    while (!path.empty())
    {
        if (path.front() == '/')
        {
            path.remove_prefix(1);
            continue;
        }
        const size_t slash = path.find('/');
        segments.push_back(path.substr(0, slash));
        path = slash == std::string_view::npos ? std::string_view() : path.substr(slash);
    }
    if (segments.empty())
        return ScopeDebug;
    const size_t n = segments.size();
    const std::string_view head = segments[0];
    const std::string_view second = n > 1 ? segments[1] : std::string_view();
    const std::string_view third = n > 2 ? segments[2] : std::string_view();
    if (n == 1 && OneOf(head, {"resume", "reset", "nmi", "run_pace"}))  // run_pace: the paced run (RunPacer)
        return ScopeRun;
    if (head == "settings" && n == 2)
        return SettingScope(second);
    if (head == "feature" && n == 2)
        return FeatureScope(second);
    if (OneOf(head, {"keyboard", "joystick", "mouse"}) && n == 2)
        return ScopeInput;
    // A connector's retro device: plugging one is a hardware change, pressing its controls is the user's input
    if (head == "controllers" && n == 3)
        return OneOf(third, {"plug", "unplug"}) ? ScopeMachine : ScopeInput;
    if (head == "tape" && n == 2 && OneOf(second, {"load", "eject", "play", "pause", "stop", "rewind", "seek"}))
        return ScopeMedia;
    if (head == "disk" && n == 3 && OneOf(third, {"insert", "eject", "create"}))
        return ScopeMedia;
    if (head == "snapshot" && n == 2 && second == "load")
        return ScopeMedia;
    if (head == "media" && n == 3)
        return ScopeMedia;
    if (head == "rzx" && n == 2 && OneOf(second, {"play", "stop", "seek"}))
        return ScopeMedia;
    if (head == "cdaudio" && n == 2)
        return ScopeMedia;
    if (head == "ttd" && n == 2 && OneOf(second, {"seek", "step-back", "step-forward", "resume"}))
        return ScopeRewind;
    if (n == 1 && OneOf(head, {"model", "switches", "start", "stop"}))
        return ScopeMachine;
    if (head == "slots" && n == 3)
        return ScopeMachine;
    if ((head == "network" && second == "config") || (head == "snapshot" && second == "transfer") ||
        (head == "state" && second == "screen" && third == "overscan") || (head == "sprinter" && second == "bios") ||
        (head == "bus" && third == "sd-timing") || (head == "memory" && second == "rom" && third == "protect"))
        return ScopeMachine;
    if (head == "control" && n == 2 && OneOf(second, {"take-back", "allow-again"}))
        return ScopeTakeBack;
    return ScopeDebug;  // memory, registers, ports, breakpoints, steps, runs, TTD analysis, BASIC, assembler, ...
}

uint32_t CompanionCliScopeOf(const std::string& command, const std::vector<std::string>& args)
{
    const std::string_view verb = args.empty() ? std::string_view() : std::string_view(args[0]);
    if (OneOf(command, {"resume", "reset", "nmi", "mni", "pace"}))
        return ScopeRun;
    if (OneOf(command, {"setting", "settings", "set"}))
        return SettingScope(verb);
    if (command == "feature")
        return FeatureScope(verb);
    if (OneOf(command, {"key", "keyboard", "joystick", "mouse"}))
        return ScopeInput;
    if (command == "controllers" && OneOf(verb, {"plug", "unplug"}))
        return ScopeMachine;
    if (command == "controllers" && OneOf(verb, {"press", "release", "set", "tap"}))
        return ScopeInput;
    if (command == "tape" && OneOf(verb, {"load", "eject", "play", "pause", "stop", "rewind", "seek"}))
        return ScopeMedia;
    if (command == "disk" && OneOf(verb, {"insert", "eject", "create"}))
        return ScopeMedia;
    if (command == "snapshot" && verb == "load")
        return ScopeMedia;
    if (OneOf(command, {"media", "open"}))
        return ScopeMedia;
    if (command == "rzx" && OneOf(verb, {"play", "stop", "seek"}))
        return ScopeMedia;
    if (command == "ttd" && OneOf(verb, {"back", "forward", "goto", "seek", "sb", "sf", "step-back", "step-forward",
                                         "resume"}))
        return ScopeRewind;
    if (OneOf(command, {"model", "switch", "slots", "start", "zxpoly"}))
        return ScopeMachine;
    if ((command == "bus" && args.size() >= 2 && (args[1] == "sd-timing" || args[1] == "sdtiming")) ||
        (OneOf(command, {"network", "net"}) && verb == "set"))
        return ScopeMachine;
    return ScopeDebug;  // memory, registers, steps, runs, breakpoints, ports, debug mode, ...
}

bool CompanionNeedsPause(const std::string& subpath)
{
    std::string_view path(subpath);
    if (const size_t query = path.find('?'); query != std::string_view::npos)
        path = path.substr(0, query);
    for (std::string_view stepping : {"/step", "/steps", "/stepover", "/stepout", "/skip_until", "/run_frame",
                                      "/run_frames", "/run_tstates", "/run_scanlines", "/run_to_scanline",
                                      "/run_to_pixel", "/run_to_interrupt", "/listing/step_line", "/listing/run_to_line",
                                      "/ttd/step-instruction", "/ttd/reverse-step", "/ttd/reverse-continue"})
        if (path == stepping)
            return true;
    return StartsWith(path, "/registers/");
}

bool CompanionCliNeedsPause(const std::string& command, const std::vector<std::string>& args)
{
    if (OneOf(command, {"step", "stepin", "steps", "stepover", "stepout", "skip_until", "run_tstates",
                        "run_to_scanline", "run_scanlines", "run_to_pixel", "run_to_interrupt", "run_frame",
                        "run_frames", "run_ncycles"}))
        return true;
    if (OneOf(command, {"registers", "regs", "r"}))
        return args.size() >= 2;
    const std::string_view verb = args.empty() ? std::string_view() : std::string_view(args[0]);
    return command == "ttd" && OneOf(verb, {"si-back", "si-forward", "step-instruction", "reverse-step", "rs",
                                            "reverse-continue", "rc"});
}

std::string CompanionScopeName(uint32_t scope)
{
    for (const auto& [name, bit] : kScopeNames)
        if (bit == scope)
            return std::string(name);
    return {};
}

std::vector<std::string> CompanionScopeNames(uint32_t scopes)
{
    std::vector<std::string> names;
    for (const auto& [name, bit] : kScopeNames)
        if (scopes & bit)
            names.emplace_back(name);
    return names;
}

uint32_t CompanionScopeFromName(const std::string& name)
{
    if (name == "all")
        return kCompanionAllScopes;
    for (const auto& [scopeName, bit] : kScopeNames)
        if (name == scopeName)
            return bit;
    return 0;
}
}  // namespace automation_gate
