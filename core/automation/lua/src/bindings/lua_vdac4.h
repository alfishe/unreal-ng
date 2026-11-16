#pragma once

/// @file lua_vdac4.h
/// @brief sol2 (Lua) bindings for the TS-Conf VDAC4 board (Tang FT812 core + ESP32-S3): its state, read through
/// Vdac4Control like every interface. Same surface as the Python bindings; the table has the keys of WebAPI
/// GET /vdac4/state.
/// Design: docs/inprogress/2026-11-15-tsconf-vdac4/design.md §10.2
///
///   t = vdac4_state([log])   -- log: recent ESP log lines (default 50, at most 256)
///       {tang = {personality, armed, esp_host, ram_g_3mb, ft_cfg},
///        esp = {phase, ms, boots, boot_nonce, last_step, last_error, resources, clock_restarts, clock_offset_us},
///        app = {running, module, name, version, command_first, command_last, error},
///        link = {pending_from_z80, pending_to_z80, send_blocked, bad_checksums, resyncs, dropped_frames},
///        packets = {count, recent = {{command, length, ms}, ...}},
///        audio = {input_rate, output_rate, queued, underruns, dropped, sound},
///        log = {"line", ...}}  or {error = "..."}

#include <sol/sol.hpp>

#include <algorithm>
#include <functional>
#include <string>

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/platforms/tsconf/vdac4/vdac4control.h>

namespace LuaVdac4
{

inline void registerBindings(sol::state& lua, std::function<Emulator*()> getEmulator)
{
    lua.set_function("vdac4_state", [getEmulator](sol::optional<uint32_t> log, sol::this_state ts) -> sol::table {
        sol::state_view view(ts);
        sol::table t = view.create_table();
        Emulator* emulator = getEmulator();
        Vdac4Control::BoardState s;
        std::string error = "No emulator selected";
        const size_t logLines = std::min<size_t>(log.value_or(50), 256);
        if (!emulator || !Vdac4Control::GetState(emulator->GetContext(), s, logLines, &error))
        {
            t["error"] = error;
            return t;
        }

        sol::table tang = view.create_table();
        tang["personality"] = s.personality;
        tang["armed"] = s.armed;
        tang["esp_host"] = s.espHost;
        tang["ram_g_3mb"] = s.ramG3M;
        tang["ft_cfg"] = s.ftCfg;
        t["tang"] = tang;

        sol::table esp = view.create_table();
        esp["phase"] = s.espPhase;
        esp["ms"] = s.espMs;
        esp["boots"] = s.boots;
        esp["boot_nonce"] = s.bootNonce;
        esp["last_step"] = s.lastStep;
        esp["last_error"] = s.lastError;
        esp["resources"] = s.resources;
        esp["clock_restarts"] = s.clockRestarts;
        esp["clock_offset_us"] = s.clockOffsetUs;
        t["esp"] = esp;

        sol::table app = view.create_table();
        app["running"] = s.appRunning;
        app["module"] = s.appModule;
        app["name"] = s.appName;
        app["version"] = s.appVersion;
        app["command_first"] = s.commandFirst;
        app["command_last"] = s.commandLast;
        app["error"] = s.appError;
        t["app"] = app;

        sol::table link = view.create_table();
        link["pending_from_z80"] = s.pendingFromZ80;
        link["pending_to_z80"] = s.pendingToZ80;
        link["send_blocked"] = s.sendBlocked;
        link["bad_checksums"] = s.badChecksums;
        link["resyncs"] = s.resyncs;
        link["dropped_frames"] = s.droppedFrames;
        t["link"] = link;

        sol::table packets = view.create_table();
        packets["count"] = s.packets;
        sol::table recent = view.create_table(static_cast<int>(s.recentPackets.size()), 0);
        for (size_t i = 0; i < s.recentPackets.size(); ++i)
        {
            sol::table item = view.create_table();
            item["command"] = s.recentPackets[i].command;
            item["length"] = s.recentPackets[i].length;
            item["ms"] = s.recentPackets[i].ms;
            recent[i + 1] = item;
        }
        packets["recent"] = recent;
        t["packets"] = packets;

        sol::table audio = view.create_table();
        audio["input_rate"] = s.audioInputRate;
        audio["output_rate"] = s.audioOutputRate;
        audio["queued"] = s.audioQueued;
        audio["underruns"] = s.audioUnderruns;
        audio["dropped"] = s.audioDropped;
        audio["sound"] = s.audioSound;
        t["audio"] = audio;

        sol::table lines = view.create_table(static_cast<int>(s.log.size()), 0);
        for (size_t i = 0; i < s.log.size(); ++i)
            lines[i + 1] = s.log[i];
        t["log"] = lines;
        return t;
    });
}

} // namespace LuaVdac4
