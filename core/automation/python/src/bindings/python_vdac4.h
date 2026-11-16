#pragma once

/// @file python_vdac4.h
/// @brief pybind11 bindings for the TS-Conf VDAC4 board (Tang FT812 core + ESP32-S3): its state, read through
/// Vdac4Control like every interface. The dict has the keys of WebAPI GET /vdac4/state. Failures raise RuntimeError
/// with the reason.
/// Design: docs/inprogress/2026-11-15-tsconf-vdac4/design.md §10.2

#include <pybind11/pybind11.h>

#include <algorithm>
#include <stdexcept>
#include <string>

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/platforms/tsconf/vdac4/vdac4control.h>

namespace PythonBindings
{

/// Register vdac4_* methods on the Emulator Python class
template <typename EmulatorClass>
inline void registerVdac4Bindings(EmulatorClass& emulatorClass)
{
    namespace py = pybind11;
    emulatorClass.def(
        "vdac4_state",
        [](Emulator& self, size_t log) {
            Vdac4Control::BoardState s;
            std::string error;
            if (!Vdac4Control::GetState(self.GetContext(), s, std::min<size_t>(log, 256), &error))
                throw std::runtime_error("vdac4_state: " + error);

            py::dict tang;
            tang["personality"] = s.personality;
            tang["armed"] = s.armed;
            tang["esp_host"] = s.espHost;
            tang["ram_g_3mb"] = s.ramG3M;
            tang["ft_cfg"] = s.ftCfg;
            py::dict esp;
            esp["phase"] = s.espPhase;
            esp["ms"] = s.espMs;
            esp["boots"] = s.boots;
            esp["boot_nonce"] = s.bootNonce;
            esp["last_step"] = s.lastStep;
            esp["last_error"] = s.lastError;
            esp["resources"] = s.resources;
            esp["clock_restarts"] = s.clockRestarts;
            esp["clock_offset_us"] = s.clockOffsetUs;
            py::dict app;
            app["running"] = s.appRunning;
            app["module"] = s.appModule;
            app["name"] = s.appName;
            app["version"] = s.appVersion;
            app["command_first"] = s.commandFirst;
            app["command_last"] = s.commandLast;
            app["error"] = s.appError;
            py::dict link;
            link["pending_from_z80"] = s.pendingFromZ80;
            link["pending_to_z80"] = s.pendingToZ80;
            link["send_blocked"] = s.sendBlocked;
            link["bad_checksums"] = s.badChecksums;
            link["resyncs"] = s.resyncs;
            link["dropped_frames"] = s.droppedFrames;
            py::list recent;
            for (const auto& packet : s.recentPackets)
            {
                py::dict item;
                item["command"] = packet.command;
                item["length"] = packet.length;
                item["ms"] = packet.ms;
                recent.append(item);
            }
            py::dict packets;
            packets["count"] = s.packets;
            packets["recent"] = recent;
            py::dict audio;
            audio["input_rate"] = s.audioInputRate;
            audio["output_rate"] = s.audioOutputRate;
            audio["queued"] = s.audioQueued;
            audio["underruns"] = s.audioUnderruns;
            audio["dropped"] = s.audioDropped;
            audio["sound"] = s.audioSound;
            py::list logLines;
            for (const std::string& line : s.log)
                logLines.append(line);

            py::dict d;
            d["tang"] = tang;
            d["esp"] = esp;
            d["app"] = app;
            d["link"] = link;
            d["packets"] = packets;
            d["audio"] = audio;
            d["log"] = logLines;
            return d;
        },
        "The TS-Conf VDAC4 board's state, the keys of WebAPI GET /vdac4/state: tang {personality, armed, esp_host, "
        "ram_g_3mb, ft_cfg}, esp {phase, ms, boots, boot_nonce, last_step, last_error, resources, clock_restarts, "
        "clock_offset_us}, app {running, module, name, version, command_first, command_last, error}, link "
        "{pending_from_z80, pending_to_z80, send_blocked, bad_checksums, resyncs, dropped_frames}, packets {count, "
        "recent [{command, length, ms}]}, audio {input_rate, output_rate, queued, underruns, dropped, sound}, log "
        "(the last `log` ESP lines, at most 256). Raises RuntimeError without the board.",
        py::arg("log") = 50);
}

}  // namespace PythonBindings
