#include "automationgate.h"

#include <array>
#include <initializer_list>
#include <string_view>

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
constexpr std::array<std::string_view, 25> kNoChange = {
    "/pause",                                   // always allowed (execution arbiter design §3.7)
    "/control/session",                         // the session itself
    "/profiler", "/coverage", "/analyzer",      // observers
    "/capture", "/audio/capture", "/audio/mixer", "/video/record", "/video/temporal",
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
    static const std::array<std::string_view, 46> kAlways = {
        "reset", "nmi", "mni", "switch", "resume", "step", "stepin", "steps", "stepover", "stepout", "skip_until",
        "run_tstates", "run_to_scanline", "run_scanlines", "run_to_pixel", "run_to_interrupt", "run_frame",
        "run_frames", "run_ncycles", "out", "romflash", "open", "assemble",
        "bp", "break", "breakpoint", "wp", "bport", "bnextreg", "bbeam", "bmagic", "bcopper", "bphits", "bpclear",
        "bpgroup", "bpon", "bpoff",
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
}  // namespace automation_gate
