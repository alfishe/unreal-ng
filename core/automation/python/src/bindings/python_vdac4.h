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
            py::list sections;
            for (const auto& section : s.appSections)
            {
                py::dict item;
                item["elf_address"] = section.elfAddress;
                item["size"] = section.size;
                item["loaded_at"] = section.loadedAt;
                sections.append(item);
            }
            app["sections"] = sections;
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
            py::dict threads;
            threads["esp_thread"] = s.espThread;
            threads["ft_thread"] = s.ftThread;
            threads["ft_draw_thread"] = s.ftDrawThread;
            threads["esp_latency_ms"] = s.espLatencyMs;
            threads["esp_jobs"] = s.espJobs;
            threads["esp_threaded_jobs"] = s.espThreadedJobs;
            threads["esp_waits"] = s.espWaits;
            threads["esp_wait_ms"] = s.espWaitMs;
            threads["esp_job_ms"] = s.espJobMs;
            threads["ft_ops"] = s.ftOps;
            threads["ft_reads"] = s.ftReads;
            threads["ft_apply_ms"] = s.ftApplyMs;
            threads["ft_producer_wait_ms"] = s.ftProducerWaitMs;
            threads["ft_owner_waits"] = s.ftOwnerWaits;
            threads["ft_owner_wait_ms"] = s.ftOwnerWaitMs;
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
            d["threads"] = threads;
            d["log"] = logLines;
            return d;
        },
        "The TS-Conf VDAC4 board's state, the keys of WebAPI GET /vdac4/state: tang {personality, armed, esp_host, "
        "ram_g_3mb, ft_cfg}, esp {phase, ms, boots, boot_nonce, last_step, last_error, resources, clock_restarts, "
        "clock_offset_us}, app {running, module, name, version, command_first, command_last, error, sections [{elf_address, size, loaded_at}]}, link "
        "{pending_from_z80, pending_to_z80, send_blocked, bad_checksums, resyncs, dropped_frames}, packets {count, "
        "recent [{command, length, ms}]}, audio {input_rate, output_rate, queued, underruns, dropped, sound}, log "
        "(the last `log` ESP lines, at most 256). Raises RuntimeError without the board.",
        py::arg("log") = 50);
    emulatorClass.def(
        "vdac4_esp_memory",
        [](Emulator& self, uint32_t address, uint32_t size, bool app) {
            std::vector<uint8_t> bytes;
            std::string error;
            if (!Vdac4Control::ReadEspMemory(self.GetContext(), address, size, app, bytes, &error))
                throw std::runtime_error("vdac4_esp_memory: " + error);
            return py::bytes(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        },
        "Bytes of the VDAC4 board's ESP32 memory (WebAPI GET /vdac4/esp/memory): `size` (at most 4096) from `address`, "
        "a D-bus / I-bus / SRAM address, or with app=True an address of the running app's ELF image (the one a haptic "
        "probe `cpu: esp` takes). Raises RuntimeError without the board, the app or the memory.",
        py::arg("address"), py::arg("size") = 16, py::arg("app") = false);
}

}  // namespace PythonBindings
