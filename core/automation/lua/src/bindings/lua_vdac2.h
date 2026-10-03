#pragma once

/// @file lua_vdac2.h
/// @brief sol2 (Lua) bindings for the TS-Conf VDAC2 card (FT812): the bus
/// capture to an .evr replay stream. Same surface as the Python bindings,
/// through Vdac2Control like every interface.
/// Design: docs/inprogress/2026-10-01-tsconf-vdac2/ (vdac2-test-corpus.md §4)
///
///   ok, err = vdac2_capture_start(path)
///   ok, err = vdac2_capture_stop()
///   t = vdac2_capture_status()   -- {capturing, path, bytes, selects, exchanges, frames,
///                                --  start_clock, last_clock} or {error = "..."}
///   t = vdac2_metrics([lines], [in_flight])  -- FT812 line budget of the last finished frame:
///       {valid, frame, lines, hard_budget, soft_budget, worst_line, worst_clocks, total_clocks,
///        lines_over_soft, lines_over_hard, margin, measure_always, line_clocks = {...} (lines),
///        in_flight = {known, lines_passed, line_clocks} (in_flight; paused machine)} or {error}
///   ok, err = vdac2_metrics_set(margin, measure_always)  -- each may be nil

#include <sol/sol.hpp>

#include <functional>
#include <string>
#include <tuple>

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/platforms/tsconf/vdac2control.h>

namespace LuaVdac2
{

inline void registerBindings(sol::state& lua, std::function<Emulator*()> getEmulator)
{
    lua.set_function("vdac2_capture_start", [getEmulator](const std::string& path) -> std::tuple<bool, std::string> {
        Emulator* emulator = getEmulator();
        if (!emulator)
            return {false, "No emulator selected"};
        std::string error;
        const bool ok = Vdac2Control::StartCapture(emulator->GetContext(), path, &error);
        return {ok, error};
    });

    lua.set_function("vdac2_capture_stop", [getEmulator]() -> std::tuple<bool, std::string> {
        Emulator* emulator = getEmulator();
        if (!emulator)
            return {false, "No emulator selected"};
        std::string error;
        const bool ok = Vdac2Control::StopCapture(emulator->GetContext(), &error);
        return {ok, error};
    });

    lua.set_function("vdac2_capture_status", [getEmulator](sol::this_state s) -> sol::table {
        sol::state_view view(s);
        sol::table t = view.create_table();
        Emulator* emulator = getEmulator();
        Vdac2Control::CaptureStatus status;
        std::string error = "No emulator selected";
        if (!emulator || !Vdac2Control::GetCaptureStatus(emulator->GetContext(), status, &error))
        {
            t["error"] = error;
            return t;
        }
        t["capturing"] = status.capturing;
        t["path"] = status.path;
        t["bytes"] = status.bytesWritten;
        t["selects"] = status.selects;
        t["exchanges"] = status.exchanges;
        t["frames"] = status.frames;
        t["start_clock"] = status.startClock;
        t["last_clock"] = status.lastClock;
        return t;
    });

    lua.set_function("vdac2_metrics", [getEmulator](sol::optional<bool> lines, sol::optional<bool> inFlight,
                                                    sol::this_state s) -> sol::table {
        sol::state_view view(s);
        sol::table t = view.create_table();
        Emulator* emulator = getEmulator();
        Vdac2Control::FrameMetrics m;
        std::string error = "No emulator selected";
        const bool withLines = lines.value_or(false);
        const bool withFlight = inFlight.value_or(false);
        if (!emulator || !Vdac2Control::GetFrameMetrics(emulator->GetContext(), m, withLines, withFlight, &error))
        {
            t["error"] = error;
            return t;
        }
        t["valid"] = m.valid;
        t["frame"] = m.frame;
        t["lines"] = m.lines;
        t["hard_budget"] = m.hardBudget;
        t["soft_budget"] = m.softBudget;
        t["worst_line"] = m.worstLine;
        t["worst_clocks"] = m.worstClocks;
        t["total_clocks"] = m.totalClocks;
        t["lines_over_soft"] = m.linesOverSoft;
        t["lines_over_hard"] = m.linesOverHard;
        t["margin"] = m.margin;
        t["measure_always"] = m.measureAlways;
        if (withLines)
        {
            sol::table costs = view.create_table(static_cast<int>(m.lineClocks.size()), 0);
            for (size_t i = 0; i < m.lineClocks.size(); ++i)
                costs[i + 1] = m.lineClocks[i];
            t["line_clocks"] = costs;
        }
        if (withFlight)
        {
            sol::table flight = view.create_table();
            flight["known"] = m.inFlightKnown;
            flight["lines_passed"] = m.inFlightLinesPassed;
            if (withLines && m.inFlightKnown)
            {
                sol::table costs = view.create_table(static_cast<int>(m.inFlightLineClocks.size()), 0);
                for (size_t i = 0; i < m.inFlightLineClocks.size(); ++i)
                    costs[i + 1] = m.inFlightLineClocks[i];
                flight["line_clocks"] = costs;
            }
            t["in_flight"] = flight;
        }
        return t;
    });

    lua.set_function("vdac2_metrics_set", [getEmulator](sol::optional<uint32_t> margin,
                                                        sol::optional<bool> always) -> std::tuple<bool, std::string> {
        Emulator* emulator = getEmulator();
        if (!emulator)
            return {false, "No emulator selected"};
        std::string error;
        if (margin && !Vdac2Control::SetLineBudgetMargin(emulator->GetContext(), *margin, &error))
            return {false, error};
        if (always && !Vdac2Control::SetMeasureAlways(emulator->GetContext(), *always, &error))
            return {false, error};
        if (!margin && !always && !Vdac2Control::HasCard(emulator->GetContext(), &error))
            return {false, error};
        return {true, ""};
    });
}

} // namespace LuaVdac2
