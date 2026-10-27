#pragma once

/// @file callbacktext.h
/// @brief The one text line of a script callback (docs/features/script-callbacks.md), for every plane that prints one:
/// the CLI from core's report (callbackcontrol.h, a StateNode), MCP from the WebAPI's JSON of the same report. Header
/// only and free of the core's types, so the MCP layer (drogon-free and core-free) shares it rather than copying it.
///
/// A Reader has `std::string Text(const char* key) const`, `int64_t Number(const char* key) const` and
/// `bool Has(const char* key) const` over one callback's fields (the names of Lua's callbacks() table).

#include <cstdint>
#include <sstream>
#include <string>

namespace callback_text
{
/// "#3 tap write #5C78 if MDT == 0 (lua "lives") active, policy suspend, hits 6, dropped 0, pending 0, calls 6,
/// errors 0, overruns 0, max 60 us, p99 63 us, total 300 us, budget 1000 us, ring 256[, last overrun ...][; reason:
/// ...][; last error: ...]". A frame or sync callback says its mode after the state and its aborted calls and
/// instruction budget after the ring: "... active, sync, policy unpaced, ..., ring 256, aborted 0, instructions 100000";
/// a logic one says so after its mode ("active, sync, logic, policy ...")
template <class Reader>
std::string FormatLine(const Reader& r)
{
    std::ostringstream out;
    out << "#" << r.Number("id") << " " << r.Text("target");
    const std::string owner = r.Text("owner");
    const std::string label = r.Text("label");
    if (!owner.empty() || !label.empty())
    {
        out << " (" << owner;
        if (!label.empty())
            out << (owner.empty() ? "" : " ") << "\"" << label << "\"";
        out << ")";
    }
    const std::string mode = r.Text("mode");
    const bool hook = !mode.empty() && mode != "deferred";
    out << " " << r.Text("state");
    if (hook)
        out << ", " << mode;
    if (hook && r.Text("role") == "logic")
        out << ", logic";
    out << ", policy " << r.Text("policy") << ", hits " << r.Number("hits") << ", dropped "
        << r.Number("dropped") << ", pending " << r.Number("pending") << ", calls " << r.Number("calls") << ", errors "
        << r.Number("errors") << ", overruns " << r.Number("overruns") << ", max " << r.Number("max_us") << " us, p99 "
        << r.Number("p99_us") << " us, total " << r.Number("total_us") << " us, budget " << r.Number("budget_us")
        << " us, ring " << r.Number("ring");
    if (hook)
        out << ", aborted " << r.Number("aborted") << ", instructions " << r.Number("instructions");
    if (r.Has("last_overrun_frame"))
        out << ", last overrun " << r.Number("last_overrun_us") << " us at frame " << r.Number("last_overrun_frame");
    if (r.Has("reason"))
        out << "; reason: " << r.Text("reason");
    if (r.Has("last_error"))
        out << "; last error: " << r.Text("last_error");
    return out.str();
}

/// "Script callbacks: N" (the list's header; a line per callback follows)
inline std::string ListHeader(size_t count)
{
    return "Script callbacks: " + std::to_string(count);
}

/// "Hooks: paced, last frame 120 us, max 900 us, frames over 10 % 0, unpaced spans 0", from the list's `hooks`
template <class Reader>
std::string FormatHooks(const Reader& r)
{
    std::ostringstream out;
    out << "Hooks: " << (r.Number("unpaced") != 0 ? "unpaced" : "paced") << ", last frame " << r.Number("last_frame_us")
        << " us, max " << r.Number("max_frame_us") << " us, frames over " << r.Number("frame_share_percent")
        << " % " << r.Number("frames_over_share") << ", unpaced spans " << r.Number("unpaced_spans");
    return out.str();
}
}  // namespace callback_text
