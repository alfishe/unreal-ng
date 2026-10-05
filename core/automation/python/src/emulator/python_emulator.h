#pragma once

#include "emulator/io/network/networkmanager.h"
#include "emulator/zxpoly/zxpolygroup.h"
#include "debugger/memory/memoryread.h"
#include "debugger/media/sectorwrite.h"
#include "debugger/ports/portwrite.h"
#include "debugger/snapshot/debugsnapshot.h"
#include "debugger/search/memorysearch.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/ports/models/profiboard.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <emulator/emulator.h>
#include <emulator/io/ide/cdaudiocontrol.h>
#include <emulator/media/mediacontrol.h>
#include <emulator/emulatormanager.h>
#include <emulator/rzx/rzxlauncher.h>
#include <loaders/snapshot/snapshotlauncher.h>
#include <emulator/memory/memory.h>
#include <emulator/memory/memoryaccesstracker.h>
#include <emulator/memory/memorymap.h>  // TD-3 sparse map + hexdump
#include <emulator/cpu/z80.h>
#include <emulator/io/fdc/fdd.h>
#include <emulator/io/fdc/diskimage.h>
#include <emulator/io/tape/tape.h>
#include <tapeaudio/tapeaudioimporter.h>
#include <tapeaudio/tapeaudiorenderer.h>
#include <emulator/video/screen.h>
#include <emulator/sound/soundcharactersettings.h>
#include <emulator/sound/chips/neogs/neogsmedia.h>
#include <emulator/sound/soundmanager.h>
#include <emulator/sound/chips/soundchip_ay8910.h>
#include <emulator/sound/chips/gs/soundchip_gs.h>
#include <base/featuremanager.h>
#include <debugger/disassembler/z80disasm.h>
#include <debugger/debugmanager.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/labels/labelmanager.h>
#include <debugger/analyzers/analyzermanager.h>
#include <debugger/analyzers/trdos/trdosanalyzer.h>
#include <debugger/analyzers/rom-print/screenocr.h>
#include <emulator/video/screenshotter.h>
#include <emulator/cpu/opcode_profiler.h>
#include <debugger/keyboard/debugkeyboardmanager.h>
#include <debugger/mouse/debugmousemanager.h>
#include <debugger/joystick/debugjoystickmanager.h>
#include <debugger/ttd/timetravelmanager.h>
#include <debugger/ttd/machinestatehash.h>
#include <debugger/ttd/ttdfileinfo.h>
#include <debugger/ttd/ttdprobe.h>
#include <debugger/analyzers/audiocapture/audiocaptureanalyzer.h>
#include <debugger/analyzers/aylog/ayloganalyzer.h>
#include <debugger/analyzers/coverage/coverageanalyzer.h>
#include <debugger/assembler/z80textassembler.h>
#include <debugger/listing/listingparser.h>
#include <emulator/platform.h>
#include <emulator/ports/portdecoder.h>
#include <emulator/config.h>
#include <emulator/memory/devicememory.h>
#include <emulator/sound/audiomixer.h>
#include <emulator/video/framebufferexport.h>
#include <emulator/ports/models/sprinter/sprinterbios.h>
#include <emulator/video/screendigest.h>
#ifdef ENABLE_RECORDING
#include "recordingmanager.h"
#include "recordingrequest.h"
#include <atomic>
#include <ctime>
#include <filesystem>
#endif
#include <3rdparty/tinywav/tinywav.h>
#include <cctype>
#include <cmath>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <thread>
#include <debugger/ttd/ttdexternalevents.h>
#include "../../../automation.h"
#include "../../../temporalstatus.h"
#include <emulator/io/rtc/rtcaccess.h>
#include <emulator/io/sprinter/isa/isaaccess.h>
#include <emulator/io/network/vnet/ethernetaccess.h>
#include <emulator/state/devicestate.h>
#include "../bindings/python_porttrace.h"
#include "../bindings/python_vdac2.h"

namespace py = pybind11;


/// Shared page-index validation for the page_* bindings: an invalid index
/// must raise instead of silently returning zeros or touching memory outside
/// the page (a negative cache/misc page reads before the buffer)

/// How a direct run ended, for emu.step / emu.steps
inline py::dict StepOutcome(const Emulator& emulator, unsigned executed)
{
    const Emulator::BreakpointStop& stop = emulator.LastDirectStop();
    py::dict result;
    result["executed"] = executed;
    result["stopped"] = stop.hit;
    if (stop.hit)
    {
        result["breakpoint_id"] = stop.breakpointId;
        result["address"] = stop.address;
        result["access"] = BreakpointHitKindName(stop.kind);
    }
    return result;
}

/// bp / bp_read / bp_write / bp_port_in / bp_port_out: the options as keyword arguments (BreakpointManager::
/// ApplyScriptOptions). The breakpoint id, or -1 when refused
inline int PythonScriptBreakpoint(Emulator& self, BreakpointTypeEnum type, uint8_t access, uint16_t address,
                                  const std::string& page, int32_t to, bool slotOnly, int32_t mask, const std::string& hits)
{
    BreakpointManager* bpm = self.GetBreakpointManager();
    if (!bpm)
        return -1;
    BreakpointSpec spec;
    spec.type = type;
    spec.access = access;
    spec.address = address;
    std::string error;
    if (!BreakpointManager::ApplyScriptOptions(spec, page, to, slotOnly, mask, hits, error))
        return -1;
    const uint16_t id = bpm->AddBreakpoint(spec, error);
    return id == BRK_INVALID ? -1 : static_cast<int>(id);
}

inline void ValidatePageIndex(const char* api, const std::string& type, int page, int offset)
{
    int maxPage;
    if (type == "ram") maxPage = MAX_RAM_PAGES - 1;
    else if (type == "rom") maxPage = MAX_ROM_PAGES - 1;
    else if (type == "cache") maxPage = MAX_CACHE_PAGES - 1;
    else if (type == "misc") maxPage = MAX_MISC_PAGES - 1;
    else
        throw std::invalid_argument(std::string(api) + ": unknown page type '" + type + "' (use ram, rom, cache, misc)");

    if (page < 0 || page > maxPage)
        throw std::invalid_argument(std::string(api) + ": page " + std::to_string(page) +
                                    " out of range for '" + type + "' (0-" + std::to_string(maxPage) + ")");
    if (offset < 0 || offset >= PAGE_SIZE)
        throw std::invalid_argument(std::string(api) + ": offset " + std::to_string(offset) +
                                    " out of range (0-" + std::to_string(PAGE_SIZE - 1) + ")");
}

/// The recorded machine of a TTD session / file as a dict (the same keys as the
/// WebAPI: model_id, model, ram_page_bound, rom_signature, peripheral_mask,
/// peripherals, general_sound, turbo_sound)
inline py::dict TtdRecordedMachineDict(const ttd::TTDRecordedMachine& m)
{
    py::dict d;
    d["model_id"] = py::cast(static_cast<unsigned>(m.modelId));
    d["model"] = m.model.empty() ? py::object(py::none()) : py::object(py::cast(m.model));
    d["ram_page_bound"] = py::cast(static_cast<unsigned>(m.ramPageBound));
    d["rom_signature"] = m.romSignature == 0 ? py::object(py::none())
                                             : py::object(py::cast("0x" + ttd::HashToString(m.romSignature)));
    d["peripheral_mask"] = py::cast(m.peripheralMask);
    py::list list;
    for (const std::string& name : m.peripherals)
        list.append(name);
    d["peripherals"] = list;
    py::list notRecorded;
    for (const std::string& name : m.notRecorded)
        notRecorded.append(name);
    d["not_recorded"] = notRecorded;
    d["general_sound"] = py::cast(std::string(ttd::GeneralSoundName(m.generalSound)));
    d["turbo_sound"] = py::cast(m.turboSound);
    return d;
}

/// @brief Python bindings for Emulator class and related functionality
/// Provides comprehensive emulator control matching CLI and WebAPI interfaces
/// StateNode -> Python object (dict / list / scalars). The one converter
/// Python needs for every DeviceState report.
inline pybind11::object StateNodeToPy(const StateNode& node)
{
    namespace py = pybind11;
    switch (node.kind)
    {
        case StateNode::Kind::Bool: return py::bool_(node.b);
        case StateNode::Kind::Int: return py::int_(node.i);
        case StateNode::Kind::Double: return py::float_(node.d);
        case StateNode::Kind::String: return py::str(node.s);
        case StateNode::Kind::Object:
        {
            py::dict d;
            for (const auto& m : node.members)
                d[py::str(m.first)] = StateNodeToPy(m.second);
            return d;
        }
        case StateNode::Kind::Array:
        {
            py::list l;
            for (const auto& item : node.items)
                l.append(StateNodeToPy(item));
            return l;
        }
        default: return py::none();
    }
}

/// One media verb through MediaControl (media-control-design.md): the options
/// come as keyword arguments, the reply is the dict every surface returns
/// (ok, error, message, slot, pending, revision, report and the verb's fields)
/// One ISA action through IsaAccess (every interface's call): reads return the byte, the rest True
inline pybind11::object PyIsaCycle(Emulator& self, const std::string& action, int slot, pybind11::object address, int value)
{
    uint32_t addr = 0;
    if (pybind11::isinstance<pybind11::int_>(address))
        addr = address.cast<uint32_t>();
    else if (!pybind11::isinstance<pybind11::str>(address) || !IsaAccess::ParseAddress(address.cast<std::string>(), addr))
        throw pybind11::value_error("address: an int or '#..' / '0x..' text");
    StateNode result;
    std::string error;
    if (!IsaAccess::Execute(self.GetContext(), action, slot, addr, value, "Python isa", result, error))
        throw pybind11::value_error(error);
    const StateNode* v = result.find("value");
    if (v && (action == "io_read" || action == "mem_read" || action == "io_peek" || action == "mem_peek"))
        return pybind11::int_(std::strtol(v->s.c_str() + 1, nullptr, 16));
    return pybind11::bool_(true);
}

inline pybind11::object MediaCallPy(Emulator& self, const std::string& verb, const std::string& slot,
                                    const std::string& path, const pybind11::kwargs& options)
{
    namespace py = pybind11;
    MediaRequest request;
    request.verb = verb;
    request.selector = slot;
    request.path = path;
    for (const auto& item : options)
    {
        std::string name = py::str(item.first);
        if (name == "async_")
            name = "async";  // "async" is a Python keyword: media_eject("A", async_=True)
        const py::handle value = item.second;
        if (py::isinstance<py::bool_>(value))
            request.options[name] = value.cast<bool>() ? "true" : "false";
        else if (value.is_none())
            request.options[name] = "";
        else
            request.options[name] = py::str(value);
    }
    return StateNodeToPy(MediaControl(self.GetContext()).Execute(request).ToValue());
}

/// RZX playback helpers for the bindings below
namespace python_rzx
{
    /// The RZX playback status as a dict (the WebAPI rzx/status fields)
    inline pybind11::dict StatusDict(const rzx::SessionStatus& status)
    {
        pybind11::dict d;
        d["loaded"] = status.loaded;
        d["active"] = status.active;
        d["summary"] = rzx::RzxLauncher::StatusLine(status);
        if (!status.loaded)
            return d;
        const rzx::PlayerStatus& player = status.player;
        d["path"] = status.path;
        d["creator"] = status.creator;
        d["version"] = status.version;
        d["snapshot"] = status.snapshot;
        d["state"] = std::string(rzx::StateName(player.state));
        d["frame"] = player.frame;
        d["total_frames"] = player.totalFrames;
        d["block"] = player.block + 1;
        d["blocks"] = player.blocks;
        d["interrupts"] = player.interrupts;
        d["desyncs"] = player.desyncs;
        d["drift"] = player.drift;
        d["max_drift"] = player.maxDrift;
        d["snapshots_applied"] = player.snapshotsApplied;
        d["keyframes"] = player.keyframes;
        d["keyframe_bytes"] = player.keyframeBytes;
        d["keyframe_interval"] = player.keyframeInterval;
        d["reason"] = player.stopReason;
        if (player.desyncs > 0)
        {
            pybind11::dict first;
            first["kind"] = std::string(rzx::DesyncName(player.firstDesync.kind));
            first["frame"] = player.firstDesync.frame;
            first["expected"] = player.firstDesync.expected;
            first["actual"] = player.firstDesync.actual;
            first["pc"] = player.firstDesync.pc;
            d["first_desync"] = first;
        }
        return d;
    }

    inline std::string ResolveId(const std::string& id)
    {
        return id.empty() ? EmulatorManager::GetInstance()->GetSelectedEmulatorId() : id;
    }
}  // namespace python_rzx

namespace PythonBindings
{
    /// Pause() -> op -> Resume() bracket shared by the mutating tape
    /// bindings (same contract as the CLI/WebAPI handlers, design §7.1):
    /// pause only when actually running, resume exactly then. RAII so an
    /// early return can never leave the emulator parked.
    class EmulatorPauseBracket
    {
    public:
        explicit EmulatorPauseBracket(Emulator* emulator)
            : _emulator(emulator), _wasRunning(emulator && emulator->IsRunning() && !emulator->IsPaused())
        {
            if (_wasRunning)
            {
                _emulator->Pause();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Give emulator time to pause
            }
        }

        ~EmulatorPauseBracket()
        {
            if (_wasRunning)
            {
                _emulator->Resume();
            }
        }

    private:
        Emulator* _emulator;
        bool _wasRunning;
    };

    /// General Sound host-port stimulus. These step the card's Z80, so they go
    /// through the live-input path: applied on the machine's thread at an
    /// instruction boundary and journaled for TTD replay. True when submitted;
    /// false when no card is fitted, the byte is out of range or TTD replay
    /// owns input
    inline bool SubmitGSInput(Emulator& self, ttd::TTDInputKind kind, int value = 0)
    {
        auto* ctx = self.GetContext();
        if (!ctx || !ctx->pTimeTravelManager || value < 0 || value > 255)
            return false;
        if (!ctx->pSoundManager || !ctx->pSoundManager->getGeneralSound())
            return false;
        ttd::TTDInputEvent ev;
        ev.kind = kind;
        ev.value = static_cast<uint8_t>(value);
        return ctx->pTimeTravelManager->SubmitLiveInput(ev);
    }

    /// region <Kempston Mouse helpers (automation-interfaces §4.6)>

    /// Manager of the emulator, or RuntimeError when there is none
    inline DebugMouseManager& MouseManagerOrThrow(Emulator& self)
    {
        auto* ctx = self.GetContext();
        DebugMouseManager* mgr = (ctx && ctx->pDebugManager) ? ctx->pDebugManager->GetMouseManager() : nullptr;
        if (!mgr)
            throw std::runtime_error("mouse manager not available");
        return *mgr;
    }

    /// Resolve a button name or raise ValueError
    inline MouseButton MouseButtonOrThrow(const std::string& name)
    {
        const auto button = DebugMouseManager::ResolveButtonName(name);
        if (!button)
            throw py::value_error("Unknown mouse button '" + name + "'. Valid: left, right, middle (or l, r, m)");
        return *button;
    }

    /// One mouse device of the machine: same keys as the WebAPI `device` object
    inline py::dict MouseDeviceDict(const MouseDeviceStatus& device)
    {
        auto hexRow = [](const uint8_t* bytes, size_t count) {
            py::list row;
            for (size_t i = 0; i < count; i++)
            {
                char text[4];
                std::snprintf(text, sizeof(text), "%02X", bytes[i]);
                row.append(std::string(text));
            }
            return row;
        };
        py::dict d;
        d["id"] = device.id;
        d["name"] = device.name;
        d["kind"] = MouseDeviceStatus::KindName(device.kind);
        d["fitted"] = device.fitted;
        d["in_use"] = device.inUse;
        d["wheel"] = device.wheel;
        d["buttons"] = static_cast<int>(device.buttons);
        d["x"] = static_cast<int>(device.x);
        d["y"] = static_cast<int>(device.y);
        d["button_mask"] = static_cast<int>(device.buttonMask);
        if (device.hasPorts)
        {
            py::dict ports;
            ports["FADF"] = static_cast<int>(device.portButtons);
            ports["FBDF"] = static_cast<int>(device.portX);
            ports["FFDF"] = static_cast<int>(device.portY);
            d["ports"] = ports;
        }
        if (device.hasSerial)
        {
            const MouseDeviceStatus::Serial& serial = device.serial;
            py::dict line;
            line["baud"] = serial.baud;
            line["receiver_baud"] = serial.receiverBaud;
            line["receiver_in_tune"] = serial.receiverInTune;
            line["receiver_enabled"] = serial.receiverEnabled;
            line["packet_in_flight"] = serial.packetInFlight;
            line["packet"] = hexRow(serial.packet, 3);
            line["packet_bytes_sent"] = static_cast<int>(serial.packetBytesSent);
            py::dict pending;
            pending["dx"] = serial.pendingDx;
            pending["dy"] = serial.pendingDy;
            line["pending"] = pending;
            line["packets_sent"] = serial.packetsSent;
            line["bytes_received"] = serial.bytesReceived;
            line["framing_errors"] = serial.framingErrors;
            line["receiver_fifo"] = hexRow(serial.fifo, serial.fifoCount);
            line["receiver_overrun"] = serial.overrun;
            d["serial"] = line;
        }
        if (device.hasPs2)
        {
            py::dict ps2;
            ps2["connected"] = device.ps2.connected;
            ps2["resolution"] = static_cast<int>(device.ps2.resolution);
            ps2["counts_per_mm"] = 1 << device.ps2.resolution;
            d["ps2"] = ps2;
        }
        return d;
    }

    /// State dict: same keys as the WebAPI state object
    inline py::dict MouseStateDict(const MouseStateSnapshot& state, const std::string& warning = "")
    {
        py::dict buttons;
        buttons["left"] = state.IsPressed(MouseButton::Left);
        buttons["right"] = state.IsPressed(MouseButton::Right);
        buttons["middle"] = state.IsPressed(MouseButton::Middle);

        auto hex = [](uint8_t value) {
            char text[8];
            std::snprintf(text, sizeof(text), "0x%02X", value);
            return std::string(text);
        };
        py::dict ports;
        ports["FADF"] = static_cast<int>(state.portButtons);  // integers, same as the WebAPI
        ports["FBDF"] = static_cast<int>(state.portX);  // integers, same as the WebAPI
        ports["FFDF"] = static_cast<int>(state.portY);  // integers, same as the WebAPI

        py::dict d;
        d["x"] = state.x;
        d["y"] = state.y;
        d["buttons"] = buttons;
        d["button_mask"] = state.buttonMask;
        d["wheel"] = state.wheel;
        d["wheel_enabled"] = state.wheelEnabled;
        d["present"] = state.present;
        d["ports"] = ports;
        if (state.pendingClickButton.has_value())
        {
            py::dict pending;
            pending["button"] = DebugMouseManager::GetButtonName(*state.pendingClickButton);
            pending["frames_left"] = state.pendingClickFramesLeft;
            d["pending_click"] = pending;
        }
        else
        {
            d["pending_click"] = py::none();
        }
        d["ttd_journal"] = state.journalSupported ? "supported" : "unsupported";
        // The machine's mouse (design 2026-10-03)
        d["mouse_fitted"] = state.mouseFitted;
        d["device"] = state.device ? py::object(MouseDeviceDict(*state.device)) : py::object(py::none());
        py::list devices;
        for (const MouseDeviceStatus& device : state.devices)
            devices.append(MouseDeviceDict(device));
        d["devices"] = devices;
        py::dict queue;
        queue["ops"] = state.queuedOps;
        py::dict remaining;
        remaining["dx"] = state.glideRemainingDx;
        remaining["dy"] = state.glideRemainingDy;
        queue["glide_remaining"] = remaining;
        d["queue"] = queue;
        if (!warning.empty())
            d["warning"] = warning;
        return d;
    }

    /// Raise for a failed result (ValueError / RuntimeError), else return the state dict
    inline py::dict MouseResultOrThrow(DebugMouseManager& mgr, const MouseInjectResult& result)
    {
        switch (result.status)
        {
            case MouseInjectStatus::Ok:
                return MouseStateDict(mgr.GetState(), result.warning);
            case MouseInjectStatus::InvalidArgument:
                throw py::value_error(result.message);
            case MouseInjectStatus::NoDevice:
            case MouseInjectStatus::ReplayActive:
            case MouseInjectStatus::NoMouseFitted:
            default:
                throw std::runtime_error(result.message.empty() ? "mouse manager not available" : result.message);
        }
    }

    /// endregion </Kempston Mouse helpers>

    /// region <Kempston joystick helpers (joystick TDD §5)>

    inline DebugJoystickManager& JoystickManagerOrThrow(Emulator& self)
    {
        auto* ctx = self.GetContext();
        DebugJoystickManager* mgr = (ctx && ctx->pDebugManager) ? ctx->pDebugManager->GetJoystickManager() : nullptr;
        if (!mgr)
            throw std::runtime_error("joystick manager not available");
        return *mgr;
    }

    /// State dict: same keys as the WebAPI state object
    inline py::dict JoystickStateDict(const JoystickStateSnapshot& state, const std::string& warning = "")
    {
        py::dict buttons;
        for (const std::string& name : DebugJoystickManager::GetAllButtonNames())
            buttons[py::str(name)] = false;
        py::list pressed;
        for (const std::string& name : state.buttons)
        {
            buttons[py::str(name)] = true;
            pressed.append(name);
        }

        py::dict d;
        d["available"] = state.available;
        d["present"] = state.present;
        d["wired"] = state.wired;
        d["state"] = static_cast<int>(state.state);
        d["port_value"] = static_cast<int>(state.portValue);
        d["buttons"] = buttons;
        d["pressed"] = pressed;
        d["button_names"] = DebugJoystickManager::GetAllButtonNames();
        d["keys"] = state.keys;
        if (state.pendingTapMask != 0)
        {
            py::dict pending;
            pending["mask"] = static_cast<int>(state.pendingTapMask);
            pending["frames_left"] = static_cast<int>(state.pendingTapFramesLeft);
            d["pending_tap"] = pending;
        }
        else
        {
            d["pending_tap"] = py::none();
        }
        if (!warning.empty())
            d["warning"] = warning;
        return d;
    }

    /// Raise for a failed result (ValueError / RuntimeError), else return the state dict
    inline py::dict JoystickResultOrThrow(DebugJoystickManager& mgr, const JoystickInjectResult& result)
    {
        switch (result.status)
        {
            case JoystickInjectStatus::Ok:
                return JoystickStateDict(mgr.GetState(), result.warning);
            case JoystickInjectStatus::InvalidArgument:
                throw py::value_error(result.message);
            case JoystickInjectStatus::NoDevice:
            case JoystickInjectStatus::ReplayActive:
            default:
                throw std::runtime_error(result.message.empty() ? "joystick manager not available" : result.message);
        }
    }

    /// Button list: a string ("up+fire", "up,fire") or a list / tuple of names -> one list string
    inline std::string JoystickNamesOrThrow(const py::object& buttons)
    {
        if (py::isinstance<py::str>(buttons))
            return buttons.cast<std::string>();
        if (py::isinstance<py::list>(buttons) || py::isinstance<py::tuple>(buttons))
        {
            std::string names;
            for (const py::handle& item : buttons)
            {
                if (!py::isinstance<py::str>(item))
                    throw py::value_error("buttons must be a string or a list of button names");
                names += (names.empty() ? "" : ",") + item.cast<std::string>();
            }
            return names;
        }
        throw py::value_error("buttons must be a string or a list of button names");
    }

    /// Python int -> long long; beyond 64 bits saturates so the manager reports it out of range
    inline long long JoystickIntOrThrow(const py::object& value, const char* name)
    {
        if (!py::isinstance<py::int_>(value))
            throw py::value_error(std::string(name) + " must be an integer");
        try
        {
            return value.cast<long long>();
        }
        catch (const py::cast_error&)
        {
            return value.cast<py::int_>() > py::int_(0) ? std::numeric_limits<long long>::max()
                                                        : std::numeric_limits<long long>::min();
        }
    }
    /// endregion </Kempston joystick helpers>

    /// @brief Register all emulator bindings with the Python module
    /// @param m The pybind11 module to register bindings with
    inline void registerEmulatorBindings(py::module_& m)
    {
        // EmulatorManager singleton access
        m.def("emu_list", []() -> std::vector<std::string> {
            auto* mgr = EmulatorManager::GetInstance();
            return mgr->GetEmulatorIds();
        }, "List all emulator instance IDs");

        m.def("emu_count", []() -> int {
            auto* mgr = EmulatorManager::GetInstance();
            return static_cast<int>(mgr->GetEmulatorIds().size());
        }, "Get count of emulator instances");

        // ZX-Poly machines (EmulatorManager::CreateZXPolyMachine - the entry point
        // every surface uses): four synchronized instances of one model
        m.def("zxpoly_start", [](const std::string& model, const std::string& file,
                                 const std::string& ramPowerOn) -> std::string {
            auto* mgr = EmulatorManager::GetInstance();
            std::function<void(CONFIG&)> configOverride;
            if (!ramPowerOn.empty())
            {
                RamPowerOn mode = RamPowerOn::Random;
                if (!Config::ParseRamPowerOn(ramPowerOn, mode))
                    throw std::invalid_argument("ram_power_on must be 'random' or 'zero'");
                configOverride = Config::RamPowerOnOverride(mode);
            }
            std::string error;
            auto master = mgr->CreateZXPolyMachine("", model, file, &error, configOverride);
            if (!master)
                throw std::runtime_error("cannot start ZX-Poly: " + error);
            mgr->StartEmulatorAsync(master->GetId());
            mgr->SetSelectedEmulatorId(master->GetId());
            return master->GetId();
        }, py::arg("model") = "PENTAGON", py::arg("file") = "", py::arg("ram_power_on") = "",
           "Start a ZX-Poly machine (file: .zxp, .prom or multiloader disk); returns the master's id. "
           "ram_power_on: 'random' or 'zero' (RAM contents of all four modules; default: the model's unreal.ini)");

        m.def("zxpoly_status", [](const std::string& id) -> py::object {
            ZXPolyGroup* group = EmulatorManager::GetInstance()->GetZXPolyGroup(id);
            if (!group)
                return py::none();
            const ZXPolyGroup::Status status = group->GetStatus();
            py::dict out;
            out["master_id"] = status.memberIds[0];
            out["locked"] = status.locked;
            out["slaves_running"] = status.slavesRunning;
            out["parallel_slaves"] = status.parallelSlaves;
            out["pipelined_slaves"] = status.pipelinedSlaves;
            out["port_3d00"] = status.port3D00;
            out["video_mode"] = status.videoMode;
            py::list modules;
            for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
            {
                py::dict module;
                module["module"] = m;
                module["id"] = status.memberIds[m];
                module["registers"] = std::vector<int>(status.registers[m].begin(), status.registers[m].end());
                modules.append(module);
            }
            out["modules"] = modules;
            out["diverged"] = status.divergence.diverged;
            out["divergence"] = status.divergence.what;
            return std::move(out);
        }, py::arg("id"), "ZX-Poly group status of any member id (None if not a ZX-Poly machine)");

        m.def("emu_get", [](const std::string& id) -> Emulator* {
            auto* mgr = EmulatorManager::GetInstance();
            auto emu = mgr->GetEmulator(id);
            return emu.get();
        }, py::return_value_policy::reference, "Get emulator by ID");

        m.def("emu_get_selected", []() -> Emulator* {
            auto* mgr = EmulatorManager::GetInstance();
            std::string selectedId = mgr->GetSelectedEmulatorId();
            if (selectedId.empty()) return nullptr;
            auto emu = mgr->GetEmulator(selectedId);
            return emu.get();
        }, py::return_value_policy::reference, "Get currently selected emulator");

        // Snapshots by emulator id (default: the selected one). A file that
        // needs another model (an SPG: TS-Conf) switches it: emulator_id is the new one
        m.def("snapshot_load", [](const std::string& path, const std::string& emulatorId, bool switchModel) -> py::dict {
            SnapshotLoadRequest request;
            request.emulatorId = python_rzx::ResolveId(emulatorId);
            request.path = path;
            request.switchModel = switchModel;
            if (auto emulator = EmulatorManager::GetInstance()->GetEmulator(request.emulatorId))
            {
                if (std::string refusal = emulator->RecordingGuard(ttd::TTDGuardedAction::LoadSnapshot); !refusal.empty())
                    throw std::runtime_error(refusal);
            }
            const SnapshotLoadResult result = SnapshotLauncher::Load(request);
            py::dict d;
            d["ok"] = result.ok;
            d["message"] = result.message;
            d["emulator_id"] = result.emulator ? result.emulator->GetId() : std::string();
            d["model_switched"] = result.modelSwitched;
            d["previous_emulator_id"] = result.previousEmulatorId;
            d["required_model"] = result.requiredModel;
            return d;
        }, "Load a snapshot; a file for another model (.spg: TSL) switches the model unless switch_model is False",
           py::arg("path"), py::arg("emulator_id") = "", py::arg("switch_model") = true);

        // RZX input recordings, by emulator id (default: the selected one). A
        // model switch replaces the machine: the answer's emulator_id is the new one
        m.def("rzx_play", [](const std::string& path, const std::string& emulatorId, const std::string& desyncMode,
                             bool eiShortFrame, bool ldAirQuirk, bool ignoreLaterSnapshots, bool switchModel) -> py::dict {
            rzx::LaunchRequest request;
            request.emulatorId = python_rzx::ResolveId(emulatorId);
            request.path = path;
            if (!rzx::RzxLauncher::ParseDesyncMode(desyncMode, request.options.desyncMode))
                throw std::invalid_argument("desync_mode '" + desyncMode + "': expected strict or tolerant");
            request.options.eiShortFrameBlocksInt = eiShortFrame;
            request.options.ldAirParityQuirk = ldAirQuirk;
            request.options.ignoreLaterSnapshots = ignoreLaterSnapshots;
            request.switchModel = switchModel;
            const rzx::LaunchResult result = rzx::RzxLauncher::Play(request);
            py::dict d;
            d["ok"] = result.play.Ok();
            d["error"] = std::string(rzx::PlayErrorName(result.play.error));
            d["message"] = result.play.message;
            d["emulator_id"] = result.emulator ? result.emulator->GetId() : std::string();
            d["model_switched"] = result.modelSwitched;
            d["previous_emulator_id"] = result.previousEmulatorId;
            d["required_model"] = result.play.requiredModel;
            d["model"] = result.switchedToModel;
            if (result.emulator)
                d["status"] = python_rzx::StatusDict(result.emulator->GetRzxStatus());
            return d;
        }, "Play an RZX recording; switches to the recording's model unless switch_model is False",
           py::arg("path"), py::arg("emulator_id") = "", py::arg("desync_mode") = "strict",
           py::arg("ei_short_frame_blocks_int") = false, py::arg("ld_air_parity_quirk") = false,
           py::arg("ignore_later_snapshots") = false, py::arg("switch_model") = true);

        m.def("rzx_stop", [](const std::string& emulatorId) -> bool {
            auto emulator = EmulatorManager::GetInstance()->GetEmulator(python_rzx::ResolveId(emulatorId));
            return emulator && emulator->StopRzx();
        }, "Stop RZX playback; the machine runs live", py::arg("emulator_id") = "");

        m.def("rzx_seek", [](uint64_t frame, const std::string& emulatorId) -> bool {
            auto emulator = EmulatorManager::GetInstance()->GetEmulator(python_rzx::ResolveId(emulatorId));
            if (!emulator)
                throw std::invalid_argument("no emulator '" + emulatorId + "'");
            std::string error;
            if (!emulator->SeekRzx(frame, &error))
                throw std::runtime_error(error);
            return true;
        }, "Move the RZX playback to the boundary after `frame` frames (RuntimeError with the reason)",
           py::arg("frame"), py::arg("emulator_id") = "");

        m.def("rzx_status", [](const std::string& emulatorId) -> py::dict {
            auto emulator = EmulatorManager::GetInstance()->GetEmulator(python_rzx::ResolveId(emulatorId));
            if (!emulator)
                throw std::invalid_argument("no emulator '" + emulatorId + "'");
            return python_rzx::StatusDict(emulator->GetRzxStatus());
        }, "RZX playback status (frame, progress, desyncs, drift)", py::arg("emulator_id") = "");

        m.def("emu_select", [](const std::string& id) -> bool {
            auto* mgr = EmulatorManager::GetInstance();
            return mgr && mgr->SetSelectedEmulatorId(id);
        }, "Set emulator as currently selected by ID", py::arg("id"));

        m.def("videowall_singlesync", [](bool enable, const std::string& emulatorId) -> bool {
            return Automation::GetInstance().SetVideowallSingleSyncMode(enable, emulatorId);
        }, "Enable or disable Single Emulator Sync Mode for the videowall", py::arg("enable"), py::arg("emulator_id") = "");

        // Emulator class bindings
        auto emulatorClass = py::class_<Emulator>(m, "Emulator")
            // Lifecycle control
            .def("start", &Emulator::Start, "Start emulator execution")
            .def("start_async", &Emulator::StartAsync, "Start emulator asynchronously")
            .def("stop", &Emulator::Stop, "Stop emulator")
            .def("pause", [](Emulator& self) { self.Pause(true); }, "Pause emulator")
            .def("resume", [](Emulator& self) { self.Resume(true); }, "Resume emulator")
            .def("reset", &Emulator::Reset, "Reset emulator")
            .def("request_nmi", &Emulator::RequestNMI, "Pulse the Z80 NMI line (vector #0066)")
            .def("request_mni", &Emulator::RequestMNI,
                 "Scorpion magic button: page the Shadow Monitor, then NMI (plain NMI on other models)")
            .def("get_switch", [](Emulator& self, const std::string& name) -> py::object {
                FrontPanelSwitch sw;
                if (!ParseFrontPanelSwitch(name, sw))
                    return py::none();
                const int value = self.GetFrontPanelSwitch(sw);
                if (value < 0)
                    return py::none();
                return py::bool_(value != 0);
            }, py::arg("name"), "Front-panel switch position (Profi 'turbo'); None when the machine has no such switch")
            .def("set_switch", [](Emulator& self, const std::string& name, bool on) {
                FrontPanelSwitch sw;
                return ParseFrontPanelSwitch(name, sw) && self.SetFrontPanelSwitch(sw, on);
            }, py::arg("name"), py::arg("on"), "Flip a front-panel switch (recorded by TTD like a key); True when done")

            // Legacy __main__-compatible aliases: the startup registration in
            // automation-python.cpp aliases this class into __main__ (instead
            // of registering a second pybind11 type for the same C++ class),
            // so every legacy method must live on this binding
            .def("init", &Emulator::Init, "Initialize emulator")
            .def("get_uuid", &Emulator::GetUUID, "Get emulator UUID (legacy alias of get_id)")
            .def("read_memory", [](Emulator& self, uint16_t address) {
                return self.GetMemory()->DirectReadFromZ80Memory(address);
            }, "Read a byte from Z80 memory (legacy alias of mem_read)", py::arg("address"))
            .def("get_breakpoint_manager", &Emulator::GetBreakpointManager, "Get the breakpoint manager",
                 py::return_value_policy::reference)
            .def("run_until_condition", [](Emulator& self, py::function predicate, unsigned maxTStates) {
                self.RunUntilCondition([&predicate](const Z80State& state) -> bool {
                    return predicate(state.pc, state.af, state.bc, state.de, state.hl).cast<bool>();
                }, maxTStates);
            }, "Run until the Python predicate returns True", py::arg("predicate"), py::arg("max_tstates") = 0)
            
            // State queries
            .def("is_running", &Emulator::IsRunning, "Check if emulator is running")
            .def("is_paused", &Emulator::IsPaused, "Check if emulator is paused")
            .def("get_id", &Emulator::GetId, "Get emulator UUID")
            .def("get_symbolic_id", &Emulator::GetSymbolicId, "Get symbolic ID")
            .def("set_symbolic_id", &Emulator::SetSymbolicId, "Set symbolic ID")
            .def("get_state", [](Emulator& self) -> std::string {
                switch (self.GetState()) {
                    case StateRun: return "running";
                    case StatePaused: return "paused";
                    case StateStopped: return "stopped";
                    case StateInitialized: return "initialized";
                    case StateResumed: return "resumed";
                    default: return "unknown";
                }
            }, "Get emulator state as string")
            .def("ram_power_on", [](Emulator& self) -> std::string {
                EmulatorContext* context = self.GetContext();
                return context ? Config::RamPowerOnName(context->config.ramPowerOn) : "";
            }, "RAM contents the machine was created with: 'random' or 'zero'")
            
            // Register access
            .def("get_pc", [](Emulator& self) -> uint16_t {
                Z80State* z80 = self.GetZ80State();
                return z80 ? z80->pc : 0;
            }, "Get program counter")
            .def("get_sp", [](Emulator& self) -> uint16_t {
                Z80State* z80 = self.GetZ80State();
                return z80 ? z80->sp : 0;
            }, "Get stack pointer")
            .def("get_af", [](Emulator& self) -> uint16_t {
                Z80State* z80 = self.GetZ80State();
                return z80 ? z80->af : 0;
            }, "Get AF register")
            .def("get_bc", [](Emulator& self) -> uint16_t {
                Z80State* z80 = self.GetZ80State();
                return z80 ? z80->bc : 0;
            }, "Get BC register")
            .def("get_de", [](Emulator& self) -> uint16_t {
                Z80State* z80 = self.GetZ80State();
                return z80 ? z80->de : 0;
            }, "Get DE register")
            .def("get_hl", [](Emulator& self) -> uint16_t {
                Z80State* z80 = self.GetZ80State();
                return z80 ? z80->hl : 0;
            }, "Get HL register")
            .def("get_ix", [](Emulator& self) -> uint16_t {
                Z80State* z80 = self.GetZ80State();
                return z80 ? z80->ix : 0;
            }, "Get IX register")
            .def("get_iy", [](Emulator& self) -> uint16_t {
                Z80State* z80 = self.GetZ80State();
                return z80 ? z80->iy : 0;
            }, "Get IY register")
            .def("get_registers", [](Emulator& self) -> py::dict {
                py::dict regs;
                Z80State* z80 = self.GetZ80State();
                if (z80) {
                    regs["pc"] = z80->pc;
                    regs["sp"] = z80->sp;
                    regs["af"] = z80->af;
                    regs["bc"] = z80->bc;
                    regs["de"] = z80->de;
                    regs["hl"] = z80->hl;
                    regs["ix"] = z80->ix;
                    regs["iy"] = z80->iy;
                    regs["af_"] = z80->alt.af;
                    regs["bc_"] = z80->alt.bc;
                    regs["de_"] = z80->alt.de;
                    regs["hl_"] = z80->alt.hl;
                    regs["i"] = z80->i;
                    regs["r"] = Z80::RegisterR(z80);
                    regs["memptr"] = z80->memptr;
                    regs["im"] = z80->im;
                    regs["iff1"] = z80->iff1 != 0;
                    regs["iff2"] = z80->iff2 != 0;
                    regs["halted"] = z80->halted != 0;
                    regs["q"] = z80->q;
                    regs["boundary"] = Z80::BoundaryName(z80->boundary);
                    regs["t"] = static_cast<uint32_t>(z80->t);  // CPU T-states since the frame's start
                }
                return regs;
            }, "Get all registers as dictionary")
            .def("get_register", [](Emulator& self, const std::string& name) -> py::object {
                Z80State* z80 = self.GetZ80State();
                if (!z80)
                    return py::none();
                uint16_t value = 0;
                bool is16bit = false;
                if (!Z80::GetRegisterValue(z80, name, value, is16bit))
                    return py::none();
                return py::cast(value);
            }, "Get register value by name", py::arg("name"))
            .def("set_register", [](Emulator& self, const std::string& name, uint16_t value) -> bool {
                Z80State* z80 = self.GetZ80State();
                if (!z80)
                    return false;
                const bool set = Z80::SetRegisterValue(z80, name, value);
                if (set)
                    self.NoteDebugChange();   // the debugger snapshot's seq
                return set;
            }, "Set register value by name", py::arg("name"), py::arg("value"))

            // Memory access: direct (non-mutating) reads so inspecting
            // memory never drives the ProfROM quadrant machine
            .def("mem_read", [](Emulator& self, uint16_t addr) -> uint8_t {
                Memory* mem = self.GetMemory();
                return mem ? mem->DirectReadFromZ80Memory(addr) : 0;
            }, "Read byte from memory")
            .def("mem_write", [](Emulator& self, uint16_t addr, uint8_t value) {
                Memory* mem = self.GetMemory();
                if (!mem) return;
                self.EditMemoryFromTool("Python memory write", [&] { mem->ToolWriteToZ80Memory(addr, value); });
            }, "Write byte to memory")
            .def("mem_read_word", [](Emulator& self, uint16_t addr) -> uint16_t {
                Memory* mem = self.GetMemory();
                if (!mem) return 0;
                return mem->DirectReadFromZ80Memory(addr) | (mem->DirectReadFromZ80Memory(static_cast<uint16_t>(addr + 1)) << 8);
            }, "Read 16-bit word from memory")
            .def("mem_write_word", [](Emulator& self, uint16_t addr, uint16_t value) {
                Memory* mem = self.GetMemory();
                if (!mem) return;
                self.EditMemoryFromTool("Python memory write", [&] {
                    mem->ToolWriteToZ80Memory(addr, value & 0xFF);
                    mem->ToolWriteToZ80Memory(static_cast<uint16_t>(addr + 1), (value >> 8) & 0xFF);
                });
            }, "Write 16-bit word to memory")
            .def("debug_snapshot", [](Emulator& self, unsigned disasm, unsigned stack, const std::vector<std::string>& memory) -> py::object {
                DebugSnapshot::Options options;
                options.disasm = std::min(disasm, 100u);
                options.stack = stack;
                options.memory = memory;
                options.rawBytes = true;
                DebugSnapshot::Result result = DebugSnapshot::Build(&self, options);
                if (!result.error.empty())
                    throw py::value_error(result.error);
                // The raw bytes are no text: take them out before the dict conversion, put them back as bytes
                std::vector<std::string> raw;
                if (StateNode* windows = const_cast<StateNode*>(result.snapshot.find("memory")))
                    for (StateNode& window : windows->items)
                        for (auto it = window.members.begin(); it != window.members.end(); ++it)
                            if (it->first == "bytes")
                            {
                                raw.push_back(std::move(it->second.s));
                                window.members.erase(it);
                                break;
                            }
                py::object dict = StateNodeToPy(result.snapshot);
                if (!raw.empty())
                {
                    py::list windows = dict["memory"];
                    size_t next = 0;
                    for (auto item : windows)
                    {
                        py::dict window = item.cast<py::dict>();
                        if (!window.contains("error") && next < raw.size())
                            window["bytes"] = py::bytes(raw[next++]);
                    }
                }
                return dict;
            }, "One coherent debugger snapshot (core DebugSnapshot, GET /debug/snapshot): seq, state, pause, consistency, "
               "regs, prev_regs, pages, stack, time, disasm, memory windows ('cpu:0x8000:256', 'ram5:0:6912') with their "
               "bytes; ValueError when refused",
               py::arg("disasm") = 0, py::arg("stack") = 8, py::arg("memory") = std::vector<std::string>())
            .def("mem_read_bytes", [](Emulator& self, uint32_t addr, uint32_t len, const std::string& space) -> py::bytes {
                const MemoryRead::Result read = MemoryRead::Bytes(self.GetContext(), space, addr, len);
                if (!read.error.empty())
                    throw py::value_error(read.error);
                return py::bytes(reinterpret_cast<const char*>(read.bytes.data()), read.bytes.size());
            }, "Read raw bytes (MemoryRead): the CPU view (wraps at 0xFFFF, up to 65536), a page 'ram5' / 'rom2' / "
               "'cache0' (stops at the page's end) or 'ram' (every RAM page back to back); ValueError with the reason",
               py::arg("addr"), py::arg("len"), py::arg("space") = "cpu")
            .def("port_out", [](Emulator& self, int64_t port, int64_t value) {
                uint16_t p = 0;
                uint8_t v = 0;
                std::string error;
                if (port < 0 || value < 0)
                    throw py::value_error("port and value must not be negative");
                if (!PortWrite::Parse(std::to_string(port), std::to_string(value), p, v, error))
                    throw py::value_error(error);
                const PortWrite::Result result = PortWrite::Write(&self, p, v, "python");
                if (!result.ok)
                    throw std::runtime_error(result.error);
            }, "Write a port through the machine's decoder like a CPU OUT (PortWrite: paging, TS-Conf registers, AY, "
               "border), without breakpoints or device waits, as a TTD tool edit; paused, stopped or running. "
               "ValueError for a bad port (0..0xFFFF) / value (0..0xFF), RuntimeError when no coherent moment came",
               py::arg("port"), py::arg("value"))
            .def("mem_read_block", [](Emulator& self, uint16_t addr, uint16_t len) -> py::bytes {
                Memory* mem = self.GetMemory();
                if (!mem) return py::bytes("");
                std::string data;
                data.reserve(len);
                for (uint16_t i = 0; i < len; i++) {
                    data.push_back(static_cast<char>(mem->DirectReadFromZ80Memory(static_cast<uint16_t>(addr + i))));
                }
                return py::bytes(data);
            }, "Read block of bytes from memory", py::arg("addr"), py::arg("len"))
            // TD-3 Phase 1: sparse non-zero block overview (same core source
            // as GET /memory/map and MCP inspect_state 'memory_map')
            .def("memory_map", [](Emulator& self, const std::string& viewName, int minRun, int maxBlocks) -> py::dict {
                Memory* mem = self.GetMemory();
                EmulatorContext* ctx = self.GetContext();
                py::dict result;
                if (!mem || !ctx) return result;

                const MemoryMapView view = (viewName == "ram" || viewName == "pages")
                                               ? MemoryMapView::RamPages
                                               : MemoryMapView::AddressSpace;
                const uint32_t minRunClamped = minRun > 0 ? static_cast<uint32_t>(minRun) : kMemoryMapDefaultMinRun;
                const uint32_t maxBlocksClamped = maxBlocks > 0 ? static_cast<uint32_t>(maxBlocks) : kMemoryMapDefaultMaxBlocks;

                const MemoryMapReport report = BuildMemoryMap(*mem, ctx->config, view, minRunClamped, maxBlocksClamped);
                result["model"] = report.model;
                result["view"] = report.ramView ? "ram" : "address";
                result["total_size"] = report.totalSize;
                result["non_zero_bytes"] = report.nonZeroBytes;
                result["min_run"] = report.minRun;
                result["truncated"] = report.truncated;

                py::list blocks;
                for (const MemoryMapBlock& block : report.blocks) {
                    py::dict item;
                    item["address"] = block.address;
                    item["size"] = block.size;
                    item["type"] = block.typeName;
                    item["bank"] = block.bank == 0xFF ? -1 : static_cast<int>(block.bank);
                    item["page"] = block.page;
                    item["rom"] = block.isRom;
                    item["status"] = block.IsZeroFill() ? "zeros" : "data";
                    item["non_zero"] = block.nonZero;
                    if (!block.IsZeroFill())
                        item["hash"] = block.hash;  // FNV-1a 64 (format with f"{h:016x}")
                    blocks.append(item);
                }
                result["blocks"] = blocks;
                return result;
            }, "Sparse non-zero memory block overview (view: 'address'|'ram')",
               py::arg("view") = "address", py::arg("min_run") = 64, py::arg("max_blocks") = 48)
            .def("mem_hexdump", [](Emulator& self, uint16_t addr, int len) -> std::string {
                Memory* mem = self.GetMemory();
                if (!mem) return "";
                if (len < 1) len = 64;
                if (len > 4096) len = 4096;
                std::vector<uint8_t> buffer(static_cast<size_t>(len));
                for (size_t i = 0; i < buffer.size(); i++)
                    buffer[i] = mem->DirectReadFromZ80Memory(static_cast<uint16_t>(addr + i));
                return FormatHexDump(buffer.data(), buffer.size(), addr);
            }, "Classic 16B/line hexdump with ASCII sidebar", py::arg("addr"), py::arg("len") = 64)
            .def("mem_write_block", [](Emulator& self, uint16_t addr, py::bytes data) {
                Memory* mem = self.GetMemory();
                if (!mem) return;
                std::string bytes = data;
                self.EditMemoryFromTool("Python memory write", [&] {
                    for (size_t i = 0; i < bytes.size(); i++)
                        mem->ToolWriteToZ80Memory(static_cast<uint16_t>((addr + i) & 0xFFFF), static_cast<uint8_t>(bytes[i]));
                });
            }, "Write block of bytes to memory", py::arg("addr"), py::arg("data"))
            
            // Physical page access (ram/rom/cache/misc)
            .def("page_read", [](Emulator& self, const std::string& type, int page, int offset) -> int {
                Memory* mem = self.GetMemory();
                if (!mem) return 0;
                ValidatePageIndex("page_read", type, page, offset);
                uint8_t* pagePtr = nullptr;
                if (type == "ram")
                    pagePtr = mem->RAMPageAddress(static_cast<uint16_t>(page));
                else if (type == "rom")
                    pagePtr = mem->ROMPageHostAddress(static_cast<uint8_t>(page));
                else if (type == "cache")
                    pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
                else
                    pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
                return pagePtr[offset];
            }, "Read byte from physical page", py::arg("type"), py::arg("page"), py::arg("offset"))
            .def("page_write", [](Emulator& self, const std::string& type, int page, int offset, uint8_t value) {
                Memory* mem = self.GetMemory();
                if (!mem) return;
                ValidatePageIndex("page_write", type, page, offset);
                uint8_t* pagePtr = nullptr;
                if (type == "ram")
                    pagePtr = mem->RAMPageAddress(static_cast<uint16_t>(page));
                else if (type == "rom")
                    pagePtr = mem->ROMPageHostAddress(static_cast<uint8_t>(page));  // Allows ROM patching
                else if (type == "cache")
                    pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
                else
                    pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
                self.EditMemoryFromTool("Python page write", [&] {
                    pagePtr[offset] = value;
                    if (type == "ram")
                        mem->MarkRamPageEdited(static_cast<uint16_t>(page));
                });
            }, "Write byte to physical page", py::arg("type"), py::arg("page"), py::arg("offset"), py::arg("value"))
            .def("page_read_block", [](Emulator& self, const std::string& type, int page, int offset, int len) -> py::bytes {
                Memory* mem = self.GetMemory();
                if (!mem) return py::bytes("");
                ValidatePageIndex("page_read_block", type, page, offset);
                if (len < 0)
                    throw std::invalid_argument("page_read_block: len must be >= 0");
                uint8_t* pagePtr = nullptr;
                if (type == "ram")
                    pagePtr = mem->RAMPageAddress(static_cast<uint16_t>(page));
                else if (type == "rom")
                    pagePtr = mem->ROMPageHostAddress(static_cast<uint8_t>(page));
                else if (type == "cache")
                    pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
                else
                    pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
                // Clamp to page boundary
                if (len > PAGE_SIZE - offset) len = PAGE_SIZE - offset;
                return py::bytes(reinterpret_cast<char*>(pagePtr + offset), len);
            }, "Read block from physical page", py::arg("type"), py::arg("page"), py::arg("offset"), py::arg("len"))
            .def("page_write_block", [](Emulator& self, const std::string& type, int page, int offset, py::bytes data) {
                Memory* mem = self.GetMemory();
                if (!mem) return;
                ValidatePageIndex("page_write_block", type, page, offset);
                uint8_t* pagePtr = nullptr;
                if (type == "ram")
                    pagePtr = mem->RAMPageAddress(static_cast<uint16_t>(page));
                else if (type == "rom")
                    pagePtr = mem->ROMPageHostAddress(static_cast<uint8_t>(page));
                else if (type == "cache")
                    pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
                else
                    pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
                std::string bytes = data;
                // Clamp to page boundary
                size_t maxLen = PAGE_SIZE - offset;
                size_t writeLen = std::min(bytes.size(), maxLen);
                self.EditMemoryFromTool("Python page write", [&] {
                    std::memcpy(pagePtr + offset, bytes.data(), writeLen);
                    if (type == "ram")
                        mem->MarkRamPageEdited(static_cast<uint16_t>(page));
                });
            }, "Write block to physical page", py::arg("type"), py::arg("page"), py::arg("offset"), py::arg("data"))
            .def("memory_info", [](Emulator& self) -> py::dict {
                py::dict info;
                Memory* mem = self.GetMemory();
                if (!mem) return info;
                
                py::dict pages;
                pages["ram_count"] = MAX_RAM_PAGES;
                pages["rom_count"] = MAX_ROM_PAGES;
                pages["cache_count"] = MAX_CACHE_PAGES;
                pages["misc_count"] = MAX_MISC_PAGES;
                info["pages"] = pages;
                
                py::list banks;
                for (int bank = 0; bank < 4; bank++) {
                    py::dict bankInfo;
                    bankInfo["bank"] = bank;
                    bankInfo["start"] = bank * 0x4000;
                    bankInfo["end"] = (bank + 1) * 0x4000 - 1;
                    bankInfo["mapping"] = mem->GetCurrentBankName(bank);
                    banks.append(bankInfo);
                }
                info["z80_banks"] = banks;
                return info;
            }, "Get memory configuration info")
            
            // Feature management (using correct FeatureManager API)
            .def("feature_get", [](Emulator& self, const std::string& name) -> bool {
                FeatureManager* fm = self.GetFeatureManager();
                return fm ? fm->isEnabled(name) : false;
            }, "Get feature state")
            .def("feature_set", [](Emulator& self, const std::string& name, bool enabled) -> bool {
                FeatureManager* fm = self.GetFeatureManager();
                if (!fm) return false;
                if (fm->setFeature(name, enabled)) return true;
                if (fm->hasFeature(name))
                    throw std::runtime_error(fm->refusalReason(name, enabled));  // TTD holds it: why
                return false;  // unknown feature
            }, "Set feature state (RuntimeError when TTD holds the feature; False for an unknown one)")
            .def("feature_list", [](Emulator& self) -> py::dict {
                py::dict features;
                FeatureManager* fm = self.GetFeatureManager();
                if (fm) {
                    // Same listFeatures() enumeration the CLI `feature` table
                    // and the WebAPI /features endpoint use — keyed by feature
                    // id, so scripts keep working as new features register
                    for (const FeatureManager::FeatureInfo& feature : fm->listFeatures())
                        features[feature.id.c_str()] = feature.enabled;
                }
                return features;
            }, "List all features and states")
            
            // Disk operations
            .def("disk_is_inserted", [](Emulator& self, int drive) -> bool {
                if (drive < 0 || drive > 3) return false;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->coreState.diskDrives[drive]) return false;
                return ctx->coreState.diskDrives[drive]->isDiskInserted();
            }, "Check if disk is inserted")
            .def("disk_get_path", [](Emulator& self, int drive) -> std::string {
                if (drive < 0 || drive > 3) return "";
                auto* ctx = self.GetContext();
                if (!ctx) return "";
                return ctx->coreState.diskFilePaths[drive];
            }, "Get disk image path")
            .def("disk_eject", [](Emulator& self, int drive) -> bool {
                if (drive < 0 || drive > 3) return false;
                return self.EjectDisk(static_cast<uint8_t>(drive), /*force*/ true);
            }, "Eject disk from drive (the disk is freed; unsaved writes are lost)")
            // Media: every slot through MediaControl. slot: fdd.b, "B", "b:", "sd", "floppy:1",
            // "tag:sd+neogs" ("auto" for insert); options as keywords (access="readonly", save=True,
            // export="x.trd", discard=True, async_=True - "async" is a Python keyword)
            .def("media", [](Emulator& self, const std::string& verb, const std::string& slot, const std::string& path,
                             const py::kwargs& options) { return MediaCallPy(self, verb, slot, path, options); },
                 py::arg("verb"), py::arg("slot") = "", py::arg("path") = "",
                 "Any media verb: list, info, formats, targets, insert, swap, eject, save, export, discard, rescan, create, protect")
            .def("media_list", [](Emulator& self) { return MediaCallPy(self, "list", "", "", py::kwargs()); },
                 "Every media slot and the detached media")
            .def("media_info", [](Emulator& self, const std::string& slot) { return MediaCallPy(self, "info", slot, "", py::kwargs()); },
                 py::arg("slot"), "One slot and its medium")
            .def("media_formats", [](Emulator& self, const py::kwargs& options) { return MediaCallPy(self, "formats", "", "", options); },
                 "Accepted formats per kind (kind='floppy' to filter)")
            .def("media_targets", [](Emulator& self, const std::string& path) { return MediaCallPy(self, "targets", "", path, py::kwargs()); },
                 py::arg("path"), "Where a file can go: what it is, the slots that take it in order, the default, or why nothing does")
            .def("media_insert", [](Emulator& self, const std::string& slot, const std::string& path, const py::kwargs& options) {
                     return MediaCallPy(self, "insert", slot, path, options);
                 }, py::arg("slot"), py::arg("path"), "Insert a file or a folder ('auto' picks the slot)")
            .def("media_swap", [](Emulator& self, const std::string& slot, const std::string& path, const py::kwargs& options) {
                     return MediaCallPy(self, "swap", slot, path, options);
                 }, py::arg("slot"), py::arg("path"), "Eject + insert in one step (save=True / export=path / discard=True for a dirty medium)")
            .def("media_eject", [](Emulator& self, const std::string& slot, const py::kwargs& options) {
                     return MediaCallPy(self, "eject", slot, "", options);
                 }, py::arg("slot"), "Take the medium out (save=True / export=path / discard=True for a dirty medium)")
            .def("media_save", [](Emulator& self, const std::string& slot, const std::string& path, const py::kwargs& options) {
                     return MediaCallPy(self, "save", slot, path, options);
                 }, py::arg("slot"), py::arg("path") = "", "Floppies: write the disk back (or to path)")
            .def("media_export", [](Emulator& self, const std::string& slot, const std::string& path) {
                     return MediaCallPy(self, "export", slot, path, py::kwargs());
                 }, py::arg("slot"), py::arg("path"), "A copy of the medium as it is now")
            .def("media_discard", [](Emulator& self, const std::string& slot, const py::kwargs& options) {
                     return MediaCallPy(self, "discard", slot, "", options);
                 }, py::arg("slot"), "Drop the unsaved writes")
            .def("media_rescan", [](Emulator& self, const std::string& slot, const py::kwargs& options) {
                     return MediaCallPy(self, "rescan", slot, "", options);
                 }, py::arg("slot"), "Build a folder medium again from its folder")
            .def("media_create", [](Emulator& self, const std::string& slot, const py::kwargs& options) {
                     return MediaCallPy(self, "create", slot, "", options);
                 }, py::arg("slot"), "A blank floppy (format, cylinders, sides) or card (size)")
            .def("media_protect", [](Emulator& self, const std::string& slot, bool on) {
                     py::kwargs options;
                     options["on"] = on;
                     return MediaCallPy(self, "protect", slot, "", options);
                 }, py::arg("slot"), py::arg("on"), "The slot's write-protect switch")
            .def("disk_create", [](Emulator& self, int drive, int cylinders, int sides, const std::string& format) -> bool {
                Emulator::BlankDiskFormat parsed = Emulator::BlankDiskFormat::Auto;
                if (drive < 0 || drive > 3 || !Emulator::ParseBlankDiskFormat(format, parsed)) return false;
                if (cylinders < 0 || cylinders > 255 || sides < 0 || sides > 255) return false;
                if (std::string refusal = self.RecordingGuard(ttd::TTDGuardedAction::CreateDisk); !refusal.empty())
                    throw std::runtime_error(refusal);  // TTD is recording: RuntimeError with the reason
                return self.CreateBlankDisk(static_cast<uint8_t>(drive), parsed, static_cast<uint8_t>(cylinders),
                                            static_cast<uint8_t>(sides));
            }, "Create blank disk (format: auto = plus3 on a +3, unformatted elsewhere; 0 = the format's geometry)",
               py::arg("drive"), py::arg("cylinders") = 0, py::arg("sides") = 0, py::arg("format") = "auto")
            .def("disk_load", [](Emulator& self, const std::string& path, int drive, bool autostart) -> py::dict {
                py::dict result;
                if (drive < 0 || drive > 3)
                {
                    result["success"] = false;
                    result["message"] = "invalid drive " + std::to_string(drive) + " (valid range: 0-3 / A-D)";
                    return result;
                }
                if (autostart)
                {
                    // Drive A (0) only - a TR-DOS/Beta 128 hardware convention: requesting it for another
                    // drive is a hard failure (result["success"]=False, message explains why), never
                    // silently ignored or redirected to drive A.
                    Emulator::DiskAutostartResult r = self.AutostartDisk(path, static_cast<uint8_t>(drive));
                    result["success"] = r.mounted;
                    result["started"] = r.started;
                    result["message"] = r.message;
                }
                else
                {
                    std::string error;
                    bool ok = self.LoadDisk(path, static_cast<uint8_t>(drive), &error);
                    result["success"] = ok;
                    result["started"] = false;
                    result["message"] = ok ? "mounted" : error;
                }
                return result;
            }, "Insert a disk image (.trd/.scl/.fdi/.udi/...) into the requested drive (0-3 / A-D), optionally "
               "quick-resetting into TR-DOS to run it (autostart, drive A only - same as the Qt UI's "
               "drag-and-drop autostart)",
               py::arg("path"), py::arg("drive") = 0, py::arg("autostart") = false)
            .def("disk_list", [](Emulator& self) -> py::list {
                py::list drives;
                auto* ctx = self.GetContext();
                if (ctx) {
                    for (int i = 0; i < 4; i++) {
                        py::dict drive;
                        drive["id"] = i;
                        drive["letter"] = std::string(1, 'A' + i);
                        drive["inserted"] = ctx->coreState.diskDrives[i] && 
                                           ctx->coreState.diskDrives[i]->isDiskInserted();
                        drive["path"] = ctx->coreState.diskFilePaths[i];
                        drives.append(drive);
                    }
                }
                return drives;
            }, "List all disk drives")
            
            // Execution control
            // step / steps return how the run ended: {executed, stopped, breakpoint_id, address, access}
            // (stopped: a breakpoint ended it - an execution one before its instruction)
            .def("step", [](Emulator& self, bool skipBreakpoints) {
                self.RunSingleCPUCycle(skipBreakpoints);
                const Emulator::BreakpointStop& stop = self.LastDirectStop();
                return StepOutcome(self, (stop.hit && stop.kind == BreakpointHitKind::Execute) ? 0u : 1u);
            }, "Execute single CPU instruction; returns {executed, stopped, breakpoint_id, address, access}",
               py::arg("skip_breakpoints") = true)
            .def("steps", [](Emulator& self, unsigned count, bool skipBreakpoints) {
                return StepOutcome(self, self.RunNCPUCycles(count, skipBreakpoints));
            }, "Execute N CPU instructions; returns {executed, stopped, breakpoint_id, address, access}",
               py::arg("count"), py::arg("skip_breakpoints") = false)
            .def("stepover", &Emulator::StepOver, "Step over call instructions")

            // Frame stepping
            .def("run_frame", [](Emulator& self, bool skipBreakpoints) {
                self.RunFrame(skipBreakpoints);
            }, "Execute one frame", py::arg("skip_breakpoints") = true)
            .def("run_frames", [](Emulator& self, unsigned count, bool skipBreakpoints) {
                self.RunNFrames(count, skipBreakpoints);
            }, "Execute N frames", py::arg("count"), py::arg("skip_breakpoints") = true)

            // T-state and scanline stepping
            .def("run_tstates", [](Emulator& self, unsigned count, bool skipBreakpoints) {
                self.RunTStates(count, skipBreakpoints);
            }, "Execute N T-states", py::arg("count"), py::arg("skip_breakpoints") = true)
            .def("run_to_scanline", [](Emulator& self, unsigned scanline, bool skipBreakpoints) {
                self.RunUntilScanline(scanline, skipBreakpoints);
            }, "Run until specific scanline", py::arg("scanline"), py::arg("skip_breakpoints") = true)
            .def("run_scanlines", [](Emulator& self, unsigned count, bool skipBreakpoints) {
                self.RunNScanlines(count, skipBreakpoints);
            }, "Run N scanlines", py::arg("count"), py::arg("skip_breakpoints") = true)
            .def("run_to_pixel", [](Emulator& self, bool skipBreakpoints) {
                self.RunUntilNextScreenPixel(skipBreakpoints);
            }, "Run until next screen pixel", py::arg("skip_breakpoints") = true)
            .def("run_to_interrupt", [](Emulator& self, bool skipBreakpoints) {
                self.RunUntilInterrupt(skipBreakpoints);
            }, "Run until next interrupt", py::arg("skip_breakpoints") = true)

            // Tape operations
            .def("tape_load", [](Emulator& self, const std::string& path) -> bool {
                if (std::string refusal = self.RecordingGuard(ttd::TTDGuardedAction::LoadTape); !refusal.empty())
                    throw std::runtime_error(refusal);  // TTD is recording: RuntimeError with the reason
                return self.LoadTape(path);
            }, "Load tape file (RuntimeError while TTD records)", py::arg("path"))
            .def("tape_is_inserted", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                return ctx && ctx->pTape && !ctx->coreState.tapeFilePath.empty();
            }, "Check if tape is inserted")
            .def("tape_get_path", [](Emulator& self) -> std::string {
                auto* ctx = self.GetContext();
                return ctx ? ctx->coreState.tapeFilePath : "";
            }, "Get tape file path")
            .def("tape_play", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTape) return false;

                EmulatorPauseBracket bracket(&self);

                // Parse-once (idempotent); paused -> resume the frozen position
                // in place, otherwise start at the consumption cursor — the same
                // semantics as `tape play` / POST /tape/play
                if (!ctx->pTape->EnsureImageLoaded())
                    return false;
                if (ctx->pTape->GetPlaybackState() == TapePlaybackState::Paused)
                    ctx->pTape->ResumePlaybackFromPause();
                else
                    ctx->pTape->StartPlaybackAtCursor();
                return true;
            }, "Start (or resume in place) tape playback")
            .def("tape_stop", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (ctx && ctx->pTape) {
                    ctx->pTape->stopTape();
                    return true;
                }
                return false;
            }, "Stop tape playback")
            .def("tape_rewind", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTape) return false;

                EmulatorPauseBracket bracket(&self);

                // Rewind keeps the image and catalog — unlike stop/eject
                // (same semantics as `tape rewind` / POST /tape/rewind)
                ctx->pTape->EnsureImageLoaded();
                ctx->pTape->RewindToStart();
                return true;
            }, "Rewind tape to beginning (image kept)")
            .def("tape_eject", [](Emulator& self) -> bool {
                return self.EjectTape();
            }, "Eject tape (the media manager's tape slot)")
            .def("tape_pause", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTape) return false;

                EmulatorPauseBracket bracket(&self);

                const TapePlaybackState state = ctx->pTape->GetPlaybackState();
                if (state == TapePlaybackState::Paused)
                    return true;  // idempotent, mirrors "Tape already paused"
                if (state != TapePlaybackState::Playing)
                    return false;
                ctx->pTape->pausePlayback();  // play resumes in place afterwards
                return true;
            }, "Pause tape playback; the next tape_play resumes in place")
            .def("tape_seek", [](Emulator& self, int blockIndex) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTape || blockIndex < 0) return false;

                EmulatorPauseBracket bracket(&self);

                if (!ctx->pTape->EnsureImageLoaded())
                    return false;
                return ctx->pTape->SeekToBlock(static_cast<size_t>(blockIndex));
            }, "Position the tape at block <block_index>", py::arg("block_index"))
            .def("tape_pos", [](Emulator& self) -> py::object {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTape || !ctx->pTape->EnsureImageLoaded()) return py::none();

                EmulatorPauseBracket bracket(&self);

                py::dict pos;
                pos["state"] = getTapePlaybackStateName(ctx->pTape->GetPlaybackState());

                std::optional<TapePosition> position = ctx->pTape->GetPosition();
                if (position.has_value())
                {
                    pos["block"] = position->blockIndex;
                    pos["pulse"] = position->pulseIndex;
                    pos["seconds_into_block"] = position->secondsIntoBlock;
                    pos["block_total_seconds"] = position->blockTotalSeconds;
                }

                pos["cursor"] = ctx->pTape->GetConsumptionCursor();
                pos["block_count"] = ctx->pTape->GetBlockCatalog().size();
                return pos;
            }, "One-line playback position (dict) or None when no tape is loaded")
            .def("tape_blocks", [](Emulator& self) -> py::object {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTape || !ctx->pTape->EnsureImageLoaded()) return py::none();

                EmulatorPauseBracket bracket(&self);

                const std::vector<TapeBlockDescriptor>& catalog = ctx->pTape->GetBlockCatalog();
                const TapeFastLoadPlan& plan = ctx->pTape->GetFastLoadPlan();

                py::list blocks;
                for (const TapeBlockDescriptor& descriptor : catalog)
                {
                    py::dict block;
                    block["index"] = descriptor.index;
                    block["kind"] = getTapeBlockKindName(descriptor.kind);

                    if (descriptor.kind == TapeBlockKindEnum::Header || descriptor.kind == TapeBlockKindEnum::Data ||
                        descriptor.kind == TapeBlockKindEnum::Custom)
                    {
                        block["headerless"] = descriptor.headerless;
                    }

                    if (descriptor.headerValid)
                    {
                        block["name"] = descriptor.name;
                        block["type"] = getTapeBlockTypeName(descriptor.headerType);
                        block["declared_length"] = descriptor.declaredLength;
                        block["param1"] = descriptor.param1;
                        block["param2"] = descriptor.param2;
                    }

                    if (descriptor.pairedDataIndex != SIZE_MAX)
                        block["paired_data_index"] = descriptor.pairedDataIndex;
                    if (descriptor.pairedHeaderIndex != SIZE_MAX)
                        block["paired_header_index"] = descriptor.pairedHeaderIndex;

                    py::dict speed;
                    speed["profile"] = getTapeSpeedProfileName(descriptor.timing.profile);
                    if (descriptor.baudEstimate > 0)
                        speed["baud"] = descriptor.baudEstimate;
                    block["speed"] = speed;

                    block["checksum_valid"] = descriptor.checksumValid;
                    block["checksum_applicable"] = descriptor.rawSize > 0;
                    block["seconds"] = descriptor.estimatedSeconds;
                    if (descriptor.rawSize > 0)
                        block["raw_size"] = descriptor.rawSize;
                    block["playable"] = descriptor.playable;

                    if (descriptor.kind != TapeBlockKindEnum::Control && plan.perBlock.size() > descriptor.index)
                    {
                        block["fast_load"] = plan.perBlock[descriptor.index] == FastLoadRejectEnum::None
                                                 ? "yes"
                                                 : getFastLoadRejectName(plan.perBlock[descriptor.index]);
                    }

                    blocks.append(block);
                }
                return blocks;
            }, "Block catalog as a list of dicts (mirrors GET /tape blocks[]) or None")
            .def("tape_info", [](Emulator& self) -> py::object {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTape) return py::none();

                EmulatorPauseBracket bracket(&self);

                const std::string& path = ctx->coreState.tapeFilePath;
                const bool loaded = !path.empty() && ctx->pTape->EnsureImageLoaded();

                py::dict info;
                info["status"] = loaded ? "loaded" : (path.empty() ? "empty" : "error");
                info["file"] = path;
                info["state"] = loaded ? getTapePlaybackStateName(ctx->pTape->GetPlaybackState()) : "idle";
                if (!loaded) return info;

                const TapeFastLoadPlan& plan = ctx->pTape->GetFastLoadPlan();
                info["format"] = ctx->pTape->GetLoadedFormatId();
                info["cursor"] = ctx->pTape->GetConsumptionCursor();
                info["block_count"] = ctx->pTape->GetBlockCatalog().size();
                info["total_seconds"] = plan.totalSeconds;

                FeatureManager* fm = self.GetFeatureManager();
                info["fast_tape"] = fm && fm->isEnabled(Features::kFastTape);
                info["turbo_tape"] = fm && fm->isEnabled(Features::kTurboTape);

                py::dict fastLoad;
                fastLoad["verdict"] = getFastLoadVerdictName(plan.verdict);
                fastLoad["eligible_blocks"] = plan.eligibleBlocks;
                fastLoad["accelerated_seconds"] = plan.acceleratedSeconds;
                fastLoad["total_seconds"] = plan.totalSeconds;
                fastLoad["summary"] = plan.summary;
                info["fast_load"] = fastLoad;
                return info;
            }, "Detailed tape status (dict, mirrors GET /tape) or None")
            // Tape audio bridge: pure path-to-path conversions through the same
            // engine as `tape render` / POST /tape/render — no emulator state involved
            .def("tape_render", [](Emulator&, const std::string& source, const std::string& output, py::dict options) -> py::dict {
                TapeRenderRequest request;
                request.sourcePath = source;
                request.outputPath = output;
                if (options.contains("first_block")) request.firstBlock = options["first_block"].cast<size_t>();
                if (options.contains("last_block")) request.lastBlock = options["last_block"].cast<size_t>();
                if (options.contains("sample_rate")) request.sampleRate = options["sample_rate"].cast<uint32_t>();
                if (options.contains("amplitude")) request.amplitude = options["amplitude"].cast<double>();
                if (options.contains("invert_level")) request.invertLevel = options["invert_level"].cast<bool>();

                TapeRenderResult result = RenderTapeToAudio(request);

                py::dict ret;
                ret["ok"] = result.ok;
                ret["error"] = result.errorText;
                ret["duration_sec"] = result.durationSec;
                ret["samples"] = result.samplesWritten;
                ret["blocks"] = result.blocksRendered;
                ret["encoder"] = result.encoderUsed;
                py::list warnings;
                for (const std::string& warning : result.warnings)
                    warnings.append(warning);
                ret["warnings"] = warnings;
                return ret;
            }, "Render a tape image to WAV/FLAC audio (pure file conversion)",
               py::arg("source"), py::arg("output"), py::arg("options") = py::dict())
            // Same engine as `tape import` / POST /tape/import: decode + extract
            // + recognize, then an extension-dispatched save (.tzx exact, .tap gated)
            .def("tape_import", [](Emulator&, const std::string& source, const std::string& output, py::object hysteresis) -> py::dict {
                TapeImportRequest request;
                request.sourcePath = source;
                if (!hysteresis.is_none())
                    request.hysteresis = hysteresis.cast<double>();

                TapeImportResult imported = ImportAudioToTape(request);
                TapeSaveResult saved;
                if (imported.ok)
                    saved = SaveTapeImage(imported.image, output);

                py::dict ret;
                ret["ok"] = imported.ok && saved.ok;
                ret["error"] = !imported.ok ? imported.errorText : saved.errorText;
                ret["decoder"] = imported.decoderUsed;
                ret["sample_rate"] = imported.sampleRate;
                ret["samples_decoded"] = imported.samplesDecoded;
                ret["signal_edges"] = imported.signalEdges;
                ret["blocks_recognized"] = imported.blocksRecognized;
                ret["blocks_written"] = saved.blocksWritten;
                ret["output_path"] = output;
                py::list warnings;
                for (const std::string& warning : imported.warnings)
                    warnings.append(warning);
                ret["warnings"] = warnings;
                return ret;
            }, "Import WAV/FLAC/MP3 audio as a .tzx/.tap image",
               py::arg("source"), py::arg("output"), py::arg("hysteresis") = py::none())
            
            // Snapshot operations
            .def("snapshot_load", [](Emulator& self, const std::string& path) -> bool {
                if (std::string refusal = self.RecordingGuard(ttd::TTDGuardedAction::LoadSnapshot); !refusal.empty())
                    throw std::runtime_error(refusal);  // TTD is recording: RuntimeError with the reason
                // This object is one machine: a file for another model (an SPG:
                // TS-Conf) is refused; unreal.snapshot_load switches the model
                SnapshotLoadRequest request;
                request.emulatorId = self.GetId();
                request.path = path;
                request.switchModel = false;
                const SnapshotLoadResult result = SnapshotLauncher::Load(request);
                if (result.modelMismatch)
                    throw std::runtime_error(result.message + " (unreal.snapshot_load switches it)");
                return result.ok;
            }, "Load snapshot file (RuntimeError while TTD records or when the file needs another model)", py::arg("path"))
            .def("snapshot_save", &Emulator::SaveSnapshot, "Save snapshot file", py::arg("path"))
            // RZX playback on this machine (unreal.rzx_play plays, switching the model when needed)
            .def("rzx_stop", [](Emulator& self) { return self.StopRzx(); }, "Stop RZX playback")
            .def("rzx_status", [](Emulator& self) { return python_rzx::StatusDict(self.GetRzxStatus()); },
                 "RZX playback status")
            
            // Breakpoint management
            .def("bp", [](Emulator& self, uint16_t addr, const std::string& page, int32_t to, bool slot_only,
                             const std::string& hits) -> int {
                return PythonScriptBreakpoint(self, BRK_MEMORY, BRK_MEM_EXECUTE, addr, page, to, slot_only, -1, hits);
            }, "Add a breakpoint (bp); page 'ram32' / 'rom3' / 'cache0': physical, through any slot that shows "
               "it (slot_only: only through the slot of addr); to: range end; hits '5' / '>=5' / '%5'. -1 when refused",
               py::arg("addr"), py::arg("page") = "", py::arg("to") = -1, py::arg("slot_only") = false,
               py::arg("hits") = "")
            .def("bp_read", [](Emulator& self, uint16_t addr, const std::string& page, int32_t to, bool slot_only,
                             const std::string& hits) -> int {
                return PythonScriptBreakpoint(self, BRK_MEMORY, BRK_MEM_READ, addr, page, to, slot_only, -1, hits);
            }, "Add a breakpoint (bp_read); page 'ram32' / 'rom3' / 'cache0': physical, through any slot that shows "
               "it (slot_only: only through the slot of addr); to: range end; hits '5' / '>=5' / '%5'. -1 when refused",
               py::arg("addr"), py::arg("page") = "", py::arg("to") = -1, py::arg("slot_only") = false,
               py::arg("hits") = "")
            .def("bp_write", [](Emulator& self, uint16_t addr, const std::string& page, int32_t to, bool slot_only,
                             const std::string& hits) -> int {
                return PythonScriptBreakpoint(self, BRK_MEMORY, BRK_MEM_WRITE, addr, page, to, slot_only, -1, hits);
            }, "Add a breakpoint (bp_write); page 'ram32' / 'rom3' / 'cache0': physical, through any slot that shows "
               "it (slot_only: only through the slot of addr); to: range end; hits '5' / '>=5' / '%5'. -1 when refused",
               py::arg("addr"), py::arg("page") = "", py::arg("to") = -1, py::arg("slot_only") = false,
               py::arg("hits") = "")
            .def("bp_port_in", [](Emulator& self, uint16_t port, int32_t mask, const std::string& hits) -> int {
                return PythonScriptBreakpoint(self, BRK_IO, BRK_IO_IN, port, "", -1, false, mask, hits);
            }, "Add a port breakpoint (bp_port_in); mask: matches every port with (port & mask) == (port & mask); "
               "hits '5' / '>=5' / '%5'. -1 when refused",
               py::arg("port"), py::arg("mask") = -1, py::arg("hits") = "")
            .def("bp_port_out", [](Emulator& self, uint16_t port, int32_t mask, const std::string& hits) -> int {
                return PythonScriptBreakpoint(self, BRK_IO, BRK_IO_OUT, port, "", -1, false, mask, hits);
            }, "Add a port breakpoint (bp_port_out); mask: matches every port with (port & mask) == (port & mask); "
               "hits '5' / '>=5' / '%5'. -1 when refused",
               py::arg("port"), py::arg("mask") = -1, py::arg("hits") = "")
            .def("bp_remove", [](Emulator& self, uint16_t id) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return false;
                BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
                return bpm ? bpm->RemoveBreakpointByID(id) : false;
            }, "Remove breakpoint by ID", py::arg("id"))
            .def("bp_clear", [](Emulator& self) {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return;
                BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
                if (bpm) bpm->ClearBreakpoints();
            }, "Clear all breakpoints")
            .def("bp_enable", [](Emulator& self, uint16_t id) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return false;
                BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
                return bpm ? bpm->ActivateBreakpoint(id) : false;
            }, "Enable breakpoint", py::arg("id"))
            .def("bp_disable", [](Emulator& self, uint16_t id) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return false;
                BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
                return bpm ? bpm->DeactivateBreakpoint(id) : false;
            }, "Disable breakpoint", py::arg("id"))
            .def("bp_note", [](Emulator& self, uint16_t id, const std::string& note) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return false;
                BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
                return bpm ? bpm->SetBreakpointNote(id, note) : false;
            }, "Set a breakpoint's note (empty clears it); False for an unknown id", py::arg("id"), py::arg("note"))
            .def("bp_group", [](Emulator& self, uint16_t id, const std::string& group) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return false;
                BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
                return bpm ? bpm->SetBreakpointGroup(id, group) : false;
            }, "Move a breakpoint into a group (created on use); False for an unknown id or an empty name",
               py::arg("id"), py::arg("group"))
            .def("bp_reset_hits", [](Emulator& self, int id) -> bool {
                BreakpointManager* bpm = self.GetBreakpointManager();
                if (!bpm)
                    return false;
                if (id >= 0)
                    return bpm->ResetHitCount(static_cast<uint16_t>(id));
                bpm->ResetAllHitCounts();
                return true;
            }, "Hit counters back to 0: one breakpoint (id), or all (no id). False for an unknown id",
               py::arg("id") = -1)
            .def("bp_count", [](Emulator& self) -> size_t {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return 0;
                BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
                return bpm ? bpm->GetBreakpointsCount() : 0;
            }, "Get breakpoint count")
            .def("bp_list", [](Emulator& self) -> std::string {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return "";
                BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
                return bpm ? bpm->GetBreakpointListAsString() : "";
            }, "Get formatted breakpoint list")
            .def("bp_status", [](Emulator& self) -> py::dict {
                py::dict result;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) {
                    result["valid"] = false;
                    return result;
                }
                BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
                if (!bpm) {
                    result["valid"] = false;
                    return result;
                }
                auto info = bpm->GetLastTriggeredBreakpointInfo();
                result["valid"] = info.valid;
                if (info.valid) {
                    result["id"] = info.id;
                    result["type"] = info.type;
                    result["address"] = info.address;
                    result["access"] = info.access;
                    result["active"] = info.active;
                    result["note"] = info.note;
                    result["group"] = info.group;
                    result["hit_count"] = info.hitCount;
                    if (!info.pageKind.empty())
                    {
                        py::dict page;
                        page["kind"] = info.pageKind;
                        page["page"] = info.pageNumber;
                        result["page"] = page;
                    }
                }
                return result;
            }, "Get last triggered breakpoint info (id, type, address, access)")
            .def("bp_clear_last", [](Emulator& self) {
                auto* ctx = self.GetContext();
                if (ctx && ctx->pDebugManager) {
                    BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
                    if (bpm) bpm->ClearLastTriggeredBreakpoint();
                }
            }, "Clear last triggered breakpoint tracking")

            // Labels/Symbols
            .def("label_get", [](Emulator& self, const std::string& name) -> py::object {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return py::none();
                LabelManager* lm = ctx->pDebugManager->GetLabelManager();
                auto label = lm ? lm->GetLabelByName(name) : nullptr;
                if (!label) return py::none();
                py::dict result;
                result["name"] = label->name;
                result["address"] = label->address;
                if (label->bank != UINT16_MAX) {
                    result["bank"] = label->bank;
                    result["bankType"] = label->isROM() ? "rom" : "ram";
                }
                result["type"] = label->type;
                result["module"] = label->module;
                result["comment"] = label->comment;
                result["active"] = label->active;
                return result;
            }, "Get label by name", py::arg("name"))
            .def("label_at", [](Emulator& self, uint16_t address) -> py::object {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return py::none();
                LabelManager* lm = ctx->pDebugManager->GetLabelManager();
                auto label = lm ? lm->GetLabelByZ80Address(address) : nullptr;
                if (!label) return py::none();
                py::dict result;
                result["name"] = label->name;
                result["address"] = label->address;
                if (label->bank != UINT16_MAX) {
                    result["bank"] = label->bank;
                    result["bankType"] = label->isROM() ? "rom" : "ram";
                }
                result["type"] = label->type;
                result["module"] = label->module;
                result["active"] = label->active;
                return result;
            }, "Get label at address", py::arg("address"))
            .def("label_add", [](Emulator& self, const std::string& name, uint16_t address,
                                 const std::string& type, const std::string& module,
                                 const std::string& comment) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return false;
                LabelManager* lm = ctx->pDebugManager->GetLabelManager();
                return lm && lm->AddLabel(name, address, UINT16_MAX, UINT16_MAX, type, module, comment);
            }, "Add a label", py::arg("name"), py::arg("address"),
               py::arg("type") = "", py::arg("module") = "", py::arg("comment") = "")
            .def("label_remove", [](Emulator& self, const std::string& name) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return false;
                LabelManager* lm = ctx->pDebugManager->GetLabelManager();
                return lm && lm->RemoveLabel(name);
            }, "Remove label by name", py::arg("name"))
            .def("label_count", [](Emulator& self) -> size_t {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return 0;
                LabelManager* lm = ctx->pDebugManager->GetLabelManager();
                return lm ? lm->GetLabelCount() : 0;
            }, "Get label count")
            .def("labels_list", [](Emulator& self, const std::string& module,
                                   const std::string& type) -> py::list {
                py::list result;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return result;
                LabelManager* lm = ctx->pDebugManager->GetLabelManager();
                if (!lm) return result;

                LabelManager::LabelFilter filter;
                if (!module.empty()) filter.module = module;
                if (!type.empty()) filter.type = type;

                auto labels = lm->GetLabels(filter);
                for (const auto& label : labels) {
                    py::dict lbl;
                    lbl["name"] = label->name;
                    lbl["address"] = label->address;
                    if (label->bank != UINT16_MAX) lbl["bank"] = label->bank;
                    lbl["type"] = label->type;
                    lbl["module"] = label->module;
                    lbl["active"] = label->active;
                    result.append(lbl);
                }
                return result;
            }, "List labels with optional filter", py::arg("module") = "", py::arg("type") = "")
            .def("labels_clear", [](Emulator& self) {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return;
                LabelManager* lm = ctx->pDebugManager->GetLabelManager();
                if (lm) lm->ClearAllLabels();
            }, "Clear all labels")
            .def("symbols_load", [](Emulator& self, const std::string& path) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return false;
                LabelManager* lm = ctx->pDebugManager->GetLabelManager();
                return lm && lm->LoadLabels(path);
            }, "Load symbols from file", py::arg("path"))
            .def("symbols_save", [](Emulator& self, const std::string& path) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return false;
                LabelManager* lm = ctx->pDebugManager->GetLabelManager();
                return lm && lm->SaveLabels(path);
            }, "Save symbols to file", py::arg("path"))

            // Disassembly
            .def("disasm", [](Emulator& self, int address, int count) -> py::list {
                py::list result;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager || !ctx->pDebugManager->GetDisassembler()) {
                    return result;
                }
                Z80Disassembler* disasm = ctx->pDebugManager->GetDisassembler().get();
                Memory* memory = ctx->pMemory;
                Z80* z80 = ctx->pCore->GetZ80();
                LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
                
                uint16_t addr = address < 0 ? ctx->pCore->GetZ80()->pc : static_cast<uint16_t>(address);
                if (count < 1) count = 10;
                if (count > 100) count = 100;
                
                for (int i = 0; i < count; ++i) {
                    std::vector<uint8_t> buffer;
                    // Direct (non-mutating) reads: disassembly must not strobe
                    // the ProfROM quadrant machine on #0000-#0003
                    for (int j = 0; j < 4; ++j) {
                        buffer.push_back(memory->DirectReadFromZ80Memory(static_cast<uint16_t>(addr + j)));
                    }
                    
                    uint8_t cmdLen = 0;
                    DecodedInstruction decoded;
                    std::string mnemonic = disasm->disassembleSingleCommandWithRuntime(buffer, addr, &cmdLen, z80, memory, &decoded);
                    if (cmdLen == 0) cmdLen = 1;
                    
                    py::dict instr;
                    instr["address"] = addr;
                    std::string hexBytes;
                    for (uint8_t j = 0; j < cmdLen; ++j) {
                        char buf[4];
                        snprintf(buf, sizeof(buf), "%02X", buffer[j]);
                        hexBytes += buf;
                    }
                    instr["bytes"] = hexBytes;
                    instr["mnemonic"] = mnemonic;
                    instr["size"] = cmdLen;
                    
                    // Label at the instruction address itself (e.g. jump destination marker)
                    if (labelMgr) {
                        auto label = labelMgr->GetLabelByZ80Address(addr);
                        if (label && !label->name.empty())
                            instr["label"] = label->name;
                    }
                    
                    // Target address for jumps/calls. Indirect targets (JP (HL), JP (IX)) are only
                    // known at runtime - the field is omitted when the target could not be resolved
                    if (decoded.hasJump || decoded.hasRelativeJump) {
                        uint16_t target = decoded.hasRelativeJump ? decoded.relJumpAddr : decoded.jumpAddr;
                        if (!decoded.hasIndirect || decoded.hasRuntime) {
                            instr["target"] = target;
                            
                            if (labelMgr) {
                                auto targetLabel = labelMgr->GetLabelByZ80Address(target);
                                if (targetLabel && !targetLabel->name.empty())
                                    instr["targetLabel"] = targetLabel->name;
                            }
                        }
                    }
                    
                    // Effective memory address for indexed (IX/IY+d) instructions - requires runtime registers
                    if (decoded.hasDisplacement && decoded.hasRuntime) {
                        instr["displacement"] = decoded.displacement;
                        instr["effectiveAddress"] = decoded.displacementAddr;
                        
                        if (labelMgr) {
                            auto effectiveLabel = labelMgr->GetLabelByZ80Address(decoded.displacementAddr);
                            if (effectiveLabel && !effectiveLabel->name.empty())
                                instr["effectiveAddressLabel"] = effectiveLabel->name;
                        }
                    }
                    
                    result.append(instr);
                    addr += cmdLen;
                }
                return result;
            }, py::arg("address") = -1, py::arg("count") = 10, "Disassemble code at address (default: PC)")
            
            // Physical page disassembly
            .def("disasm_page", [](Emulator& self, const std::string& type, int page, int offset, int count) -> py::list {
                py::list result;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager || !ctx->pDebugManager->GetDisassembler()) {
                    return result;
                }
                Z80Disassembler* disasm = ctx->pDebugManager->GetDisassembler().get();
                Memory* memory = ctx->pMemory;
                LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
                
                bool isROM = (type == "rom");
                uint8_t* pageBase = isROM ? memory->ROMPageHostAddress(static_cast<uint8_t>(page)) 
                                          : memory->RAMPageAddress(static_cast<uint16_t>(page));
                if (!pageBase) return result;
                
                if (offset < 0) offset = 0;
                if (offset >= PAGE_SIZE) offset = PAGE_SIZE - 1;
                if (count < 1) count = 10;
                if (count > 100) count = 100;
                
                uint16_t currentOffset = static_cast<uint16_t>(offset);
                for (int i = 0; i < count && currentOffset < PAGE_SIZE; ++i) {
                    std::vector<uint8_t> buffer(4, 0);
                    for (int j = 0; j < 4 && (currentOffset + j) < PAGE_SIZE; ++j) {
                        buffer[j] = pageBase[currentOffset + j];
                    }
                    
                    uint8_t cmdLen = 0;
                    DecodedInstruction decoded;
                    std::string mnemonic = disasm->disassembleSingleCommand(buffer, currentOffset, &cmdLen, &decoded);
                    if (cmdLen == 0) cmdLen = 1;
                    
                    py::dict instr;
                    instr["offset"] = currentOffset;
                    std::string hexBytes;
                    for (uint8_t j = 0; j < cmdLen; ++j) {
                        char buf[4];
                        snprintf(buf, sizeof(buf), "%02X", buffer[j]);
                        hexBytes += buf;
                    }
                    instr["bytes"] = hexBytes;
                    instr["mnemonic"] = mnemonic;
                    instr["size"] = cmdLen;
                    
                    // Label at the instruction offset itself (e.g. jump destination marker)
                    if (labelMgr) {
                        auto label = labelMgr->GetLabelByZ80Address(currentOffset);
                        if (label && !label->name.empty())
                            instr["label"] = label->name;
                    }
                    
                    // Target address for jumps/calls. Static view has no runtime registers, so indirect
                    // targets (JP (HL), JP (IX)) can not be resolved and the field is omitted
                    if (decoded.hasJump || decoded.hasRelativeJump) {
                        uint16_t target = decoded.hasRelativeJump ? decoded.relJumpAddr : decoded.jumpAddr;
                        if (!decoded.hasIndirect) {
                            instr["target"] = target;
                            
                            if (labelMgr) {
                                auto targetLabel = labelMgr->GetLabelByZ80Address(target);
                                if (targetLabel && !targetLabel->name.empty())
                                    instr["targetLabel"] = targetLabel->name;
                            }
                        }
                    }
                    
                    result.append(instr);
                    currentOffset += cmdLen;
                }
                return result;
            }, py::arg("type"), py::arg("page"), py::arg("offset") = 0, py::arg("count") = 10, 
               "Disassemble from physical RAM/ROM page (bypasses Z80 paging). type='ram'|'rom'")
            
            // Analyzer management
            .def("analyzer_list", [](Emulator& self) -> py::list {
                py::list analyzers;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return analyzers;
                AnalyzerManager* am = ctx->pDebugManager->GetAnalyzerManager();
                if (!am) return analyzers;
                for (const auto& name : am->getRegisteredAnalyzers()) {
                    analyzers.append(name);
                }
                return analyzers;
            }, "List registered analyzers")
            .def("analyzer_enable", [](Emulator& self, const std::string& name) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return false;
                AnalyzerManager* am = ctx->pDebugManager->GetAnalyzerManager();
                return am ? am->activate(name) : false;
            }, "Enable analyzer", py::arg("name"))
            .def("analyzer_disable", [](Emulator& self, const std::string& name) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return false;
                AnalyzerManager* am = ctx->pDebugManager->GetAnalyzerManager();
                return am ? am->deactivate(name) : false;
            }, "Disable analyzer", py::arg("name"))
            .def("analyzer_status", [](Emulator& self, const std::string& name) -> py::dict {
                py::dict status;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return status;
                AnalyzerManager* am = ctx->pDebugManager->GetAnalyzerManager();
                if (!am || !am->hasAnalyzer(name)) return status;
                
                status["enabled"] = am->isActive(name);
                
                if (name == "trdos") {
                    TRDOSAnalyzer* trdos = dynamic_cast<TRDOSAnalyzer*>(am->getAnalyzer(name));
                    if (trdos) {
                        std::string stateStr;
                        switch (trdos->getState()) {
                            case TRDOSAnalyzerState::IDLE: stateStr = "IDLE"; break;
                            case TRDOSAnalyzerState::IN_TRDOS: stateStr = "IN_TRDOS"; break;
                            case TRDOSAnalyzerState::IN_COMMAND: stateStr = "IN_COMMAND"; break;
                            case TRDOSAnalyzerState::IN_SECTOR_OP: stateStr = "IN_SECTOR_OP"; break;
                            case TRDOSAnalyzerState::IN_CUSTOM: stateStr = "IN_CUSTOM"; break;
                            default: stateStr = "UNKNOWN"; break;
                        }
                        status["state"] = stateStr;
                        status["event_count"] = trdos->getEventCount();
                    }
                }
                return status;
            }, "Get analyzer status", py::arg("name"))
            .def("analyzer_events", [](Emulator& self, const std::string& name, size_t limit) -> py::list {
                py::list events;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return events;
                AnalyzerManager* am = ctx->pDebugManager->GetAnalyzerManager();
                if (!am) return events;
                
                if (name == "trdos") {
                    TRDOSAnalyzer* trdos = dynamic_cast<TRDOSAnalyzer*>(am->getAnalyzer(name));
                    if (trdos) {
                        auto evts = trdos->getEvents();
                        size_t start = (evts.size() > limit) ? evts.size() - limit : 0;
                        for (size_t i = start; i < evts.size(); i++) {
                            events.append(evts[i].format());
                        }
                    }
                }
                return events;
            }, "Get analyzer events", py::arg("name"), py::arg("limit") = 50)
            .def("analyzer_clear", [](Emulator& self, const std::string& name) {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return;
                AnalyzerManager* am = ctx->pDebugManager->GetAnalyzerManager();
                if (!am) return;
                
                if (name == "trdos") {
                    TRDOSAnalyzer* trdos = dynamic_cast<TRDOSAnalyzer*>(am->getAnalyzer(name));
                    if (trdos) trdos->clear();
                }
            }, "Clear analyzer events", py::arg("name"))
            
            // Screen state
            .def("screen_get_mode", [](Emulator& self) -> std::string {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pScreen) return "";
                return Screen::GetVideoModeName(ctx->pScreen->GetVideoMode());
            }, "Get video mode name")
            .def("screen_get_border", [](Emulator& self) -> int {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pScreen) return 0;
                return ctx->pScreen->GetBorderColor();
            }, "Get border color (0-7)")
            .def("screen_get_flash", [](Emulator& self) -> int {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pScreen) return 0;
                return ctx->pScreen->_vid.flash;
            }, "Get flash counter")
            .def("screen_get_active", [](Emulator& self) -> int {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pScreen) return 0;
                return ctx->pScreen->GetActiveScreen();
            }, "Get active screen (0=normal, 1=shadow)")

            // Screen reports: the same core reports every automation module returns
            .def("screen_state", [](Emulator& self, bool verbose) -> py::object {
                return StateNodeToPy(DeviceState::Screen(self.GetContext(), verbose));
            }, "Screen state: model, video mode, resolution, border, shadow screen, active screen and RAM pages, contention, flash (verbose adds per-screen mapping and #7FFD)", py::arg("verbose") = false)
            .def("screen_mode", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::ScreenMode(self.GetContext()));
            }, "Video mode report: picture format, memory layout, displayed RAM pages, video latches")
            .def("screen_flash", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::ScreenFlash(self.GetContext()));
            }, "FLASH phase and timing")
            .def("screen_attributes", [](Emulator& self, int screen) -> py::object {
                return StateNodeToPy(DeviceState::ScreenAttributes(self.GetContext(), screen));
            }, "Per-cell ink/paper/bright/flash decoded from screen attribute memory", py::arg("screen") = -1)
            .def("screen_video_state", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::ScreenMode(self.GetContext()));
            }, "Former name of screen_mode, kept for existing scripts")

            // Capture operations
            .def("framebuffer", [](Emulator& self, const std::string& format) -> py::dict {
                FramebufferExport::Frame frame;
                std::string error;
                if (!FramebufferExport::Capture(self.GetContext(), format, frame, error))
                    throw py::value_error(error);
                py::dict d;
                d["width"] = frame.width;
                d["height"] = frame.height;
                d["format"] = frame.format;
                d["encoding"] = frame.encoding;
                py::bytes data(reinterpret_cast<const char*>(frame.bytes.data()), frame.bytes.size());
                d["data"] = data;
                // numpy when it is installed: (height, width, 4) uint8 for rgba, (height, width) uint16 for index
                try
                {
                    py::module_ np = py::module_::import("numpy");
                    py::object array = np.attr("frombuffer")(data, frame.format == "index" ? "<u2" : "u1");
                    if (frame.format == "index")
                        d["array"] = array.attr("reshape")(frame.height, frame.width);
                    else
                        d["array"] = array.attr("reshape")(frame.height, frame.width, 4);
                }
                catch (const py::error_already_set&)
                {
                }
                return d;
            }, py::arg("format") = "rgba",
               "The picture as raw pixels: width, height, format, encoding, data (bytes) and array (numpy, when installed); format 'rgba' (R,G,B,A) or 'index' (the Sprinter's u16 pens)")
            .def("capture_ocr", [](Emulator& self) -> std::string {
                return ScreenOCR::ocrScreen(self.GetId());
            }, "OCR text from screen (32x24 chars)")
            .def("capture_screen", [](Emulator& self, const std::string& format, py::object fullLegacy, const std::string& area,
                                       const std::string& path, const std::string& source) -> py::dict {
                py::dict result;
                auto fail = [&](const std::string& message, const char* kind) {
                    result["success"] = false;
                    result["error"] = message;
                    result["kind"] = kind;
                    return result;
                };
                ScreenshotOptions options;  // the whole frame, PNG
                if (!Screenshotter::ParseFormat(format, options.format))
                    return fail("Unknown format '" + format + "': use png or gif", "bad-parameter");
                if (!area.empty() && !Screenshotter::ParseArea(area, options.area))
                    return fail("Unknown area '" + area + "': use full or screen", "bad-parameter");
                if (!source.empty() && !Screenshotter::ParseSource(source, options.source))
                    return fail("Unknown source '" + source + "': use presented or live", "bad-parameter");
                if (!fullLegacy.is_none())
                {
                    // Deprecated: full=True is area="full", full=False is area="screen"
                    const ScreenshotArea fromFull = fullLegacy.cast<bool>() ? ScreenshotArea::Full : ScreenshotArea::Screen;
                    if (!area.empty() && fromFull != options.area)
                        return fail("area and the deprecated full= disagree", "bad-parameter");
                    options.area = fromFull;
                }
                options.saveTo = path;

                if (!self.GetContext() || !self.GetContext()->pScreen)
                    return fail("The emulator has no screen", "no-frame");
                const ScreenshotResult shot =
                    Screenshotter::TakeFrom(*self.GetContext()->pScreen, options, self.IsEmulationParked());
                if (!shot.ok)
                    return fail(shot.errorMessage, Screenshotter::ErrorName(shot.error));

                auto rect = [](const PictureRect& r) {
                    py::dict d;
                    d["x"] = r.x;
                    d["y"] = r.y;
                    d["width"] = r.width;
                    d["height"] = r.height;
                    return d;
                };
                py::dict frame;
                frame["width"] = shot.frame.width;
                frame["height"] = shot.frame.height;
                frame["mode"] = shot.frame.source == FrameSource::External ? std::string("external")
                                                                           : Screen::GetVideoModeName(shot.frame.videoMode);
                frame["source"] = Screenshotter::SourceName(shot.frame.source);
                frame["frame_number"] = shot.frame.frameNumber;
                if (shot.frame.beamLine >= 0)
                {
                    frame["partial"] = shot.frame.partial;
                    py::dict beam;
                    beam["line"] = shot.frame.beamLine;
                    beam["tstate"] = shot.frame.beamTstate;
                    frame["beam"] = beam;
                }
                result["success"] = true;
                result["source"] = Screenshotter::RequestSourceName(options.source);
                result["format"] = Screenshotter::FormatName(shot.format);
                result["area"] = Screenshotter::AreaName(options.area);
                result["width"] = shot.width;
                result["height"] = shot.height;
                result["size"] = shot.encodedSize;
                result["crop"] = rect(shot.crop);
                result["screen_window"] = rect(shot.frame.screenWindow);
                result["frame"] = frame;
                if (!shot.savedFile.empty())
                    result["file"] = shot.savedFile;
                else
                    result["data"] = Screenshotter::Base64Encode(shot.bytes);
                return result;
            }, "Screenshot of the presented frame: the whole frame (area='full', default) or the working picture "
               "(area='screen'), PNG (default) or GIF; returns a dict with the image base64 in 'data' (or 'file' when "
               "path is given), the frame geometry ('frame', 'screen_window') and the rectangle cut ('crop'). "
               "source='live' takes the frame as drawn now instead of the presented one (a paused machine adds the beam "
               "position and 'partial' to 'frame'). full= is a deprecated alias of area (True = 'full', False = 'screen')",
               py::arg("format") = "png", py::arg("full") = py::none(), py::arg("area") = "", py::arg("path") = "",
               py::arg("source") = "")
            
            // Audio state
            .def("audio_is_muted", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pSoundManager) return true;
                return ctx->pSoundManager->isMuted();
            }, "Check if audio is muted")
            .def("audio_ay_read", [](Emulator& self, int chip, int reg) -> int {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pSoundManager) return 0;
                auto* ay = ctx->pSoundManager->getAYChip(chip);
                if (!ay || reg < 0 || reg > 15) return 0;
                return ay->readRegister(static_cast<uint8_t>(reg));
            }, "Read AY chip register", py::arg("chip") = 0, py::arg("reg"))
            .def("audio_ay_registers", [](Emulator& self, int chip) -> py::list {
                py::list regs;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pSoundManager) return regs;
                auto* ay = ctx->pSoundManager->getAYChip(chip);
                if (!ay) return regs;
                const uint8_t* data = ay->getRegisters();
                for (int i = 0; i < 16; i++) {
                    regs.append(data[i]);
                }
                return regs;
            }, "Get all 16 AY registers", py::arg("chip") = 0)
            .def("audio_ay_count", [](Emulator& self) -> int {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pSoundManager) return 0;
                return ctx->pSoundManager->getAYChipCount();
            }, "Get AY chip count (TurboSound=2)")
            // Device state reports (core DeviceState: the same trees the
            // WebAPI, Lua, CLI and MCP return); chip=-1 -> overview
            .def("audio_ay_state", [](Emulator& self, int chip) -> py::object {
                return StateNodeToPy(chip < 0 ? DeviceState::Ay(self.GetContext()) : DeviceState::AyChip(self.GetContext(), chip));
            }, "AY/SSG state report: overview (chip=-1) or one chip fully decoded", py::arg("chip") = -1)
            .def("audio_fm_state", [](Emulator& self, int chip) -> py::object {
                return StateNodeToPy(chip < 0 ? DeviceState::Fm(self.GetContext()) : DeviceState::FmChip(self.GetContext(), chip));
            }, "TurboSound FM state report: board + chip summary (chip=-1) or one YM2203 FM half in full", py::arg("chip") = -1)
            .def("ide_state", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::Ide(self.GetContext()));
            }, "IDE board: scheme, latches, both units (task file, command, CD sense); available=False without one")
            // CD audio of the ATAPI CD drives (CdAudioControl, PLAN #83)
            .def("cdaudio_state", [](Emulator& self) -> py::object {
                return StateNodeToPy(CdAudioControl::State(self.GetContext()));
            }, "Every CD drive: disc and tracks, audio status, head (LBA, MSF, track, index), play range, page 0Eh, mixer row")
            .def("cdaudio", [](Emulator& self, const std::string& verb, const std::string& drive, const py::kwargs& options) -> py::object {
                CdAudioRequest request;
                request.verb = verb;
                request.drive = drive;
                for (const auto& item : options)
                {
                    const std::string name = py::str(item.first);
                    const py::handle value = item.second;
                    if (py::isinstance<py::bool_>(value))
                        request.options[name] = value.cast<bool>() ? "true" : "false";
                    else
                        request.options[name] = py::str(value);
                }
                return StateNodeToPy(CdAudioControl(self.GetContext()).Execute(request).ToValue());
            }, py::arg("verb") = "status", py::arg("drive") = "",
               "A CD audio verb: status, play (track=N [to=M] | lba=X frames=N | msf='MM:SS:FF' end=...), pause, resume, stop, "
               "volume (left=, right=, route=, sotc=), mixer (volume=, mute=, solo=); options as keywords")
            .def("tsconf_state", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::TsConf(self.GetContext()));
            }, "TS-Conf machine: memory map, video (mode, geometry, TSU, the engine's line), interrupts, DMA, clock, SD; available=False on other machines")
            .def("tsconf_tsu", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::TsConfTsu(self.GetContext()));
            }, "TS-Conf TSU and palette for debug views: tile layers, all 85 sprite descriptors decoded, the 256 CRAM cells; available=False on other machines")
            // Sprinter Sp2000: the same reports every interface uses (DeviceState::Sprinter,
            // SprinterPortTable, SprinterPortLookup); map / dos / pn5 / rw omitted = the machine's current state
            .def("sprinter_state", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::Sprinter(self.GetContext()));
            }, "Sprinter Sp2000: PLD configuration (module Standard / Game, selected_by + why, the Game grid offset), port map, windows, registers and cells, clock, frame, video summary, Z84C15, floppy latch, CMOS / IDE links, BIOS images; available=False on other machines")
            .def("sprinter_text", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::SprinterText(self.GetContext()));
            }, "Sprinter screen text: the mode table's text squares, 80 x 32 (BIOS SETUP, DSS); available=False on other machines")
            // Device memory regions (emulator/memory/devicememory.h): the Sprinter's video RAM "vram"
            .def("memory_regions", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::MemoryRegions(self.GetContext()));
            }, "Device memory regions (memory a device owns outside the CPU's pages): name, description, size, page size, writable, write path")
            .def("region_read", [](Emulator& self, const std::string& name, uint32_t offset, uint32_t length) -> py::bytes {
                std::vector<uint8_t> bytes;
                std::string error;
                if (!DeviceMemory::Read(self.GetContext(), name, offset, length, bytes, error))
                    throw py::value_error(error);
                return py::bytes(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            }, py::arg("name"), py::arg("offset") = 0, py::arg("length") = 256,
               "Read bytes [offset, offset + length) of a device memory region ('vram'): bytes")
            .def("region_write", [](Emulator& self, const std::string& name, uint32_t offset, py::object data) -> int {
                std::vector<uint8_t> bytes;
                if (py::isinstance<py::str>(data))
                {
                    if (!DeviceMemory::ParseHexBytes(data.cast<std::string>(), bytes))
                        throw py::value_error("data: bytes, a list of ints or a hex string ('0000A8')");
                }
                else if (py::isinstance<py::bytes>(data))
                {
                    const std::string raw = data.cast<std::string>();
                    bytes.assign(raw.begin(), raw.end());
                }
                else
                {
                    for (const py::handle item : data)
                        bytes.push_back(static_cast<uint8_t>(item.cast<int>() & 0xFF));
                }
                std::string error;
                if (!DeviceMemory::Write(self.GetContext(), name, offset, bytes, "Python region write", error))
                    throw py::value_error(error);
                return static_cast<int>(bytes.size());
            }, py::arg("name"), py::arg("offset"), py::arg("data"),
               "Write bytes (bytes, list of ints or a hex string) through the device's own write path; returns the count")
            .def("region_save", [](Emulator& self, const std::string& name, const std::string& path, uint32_t offset, uint32_t length) {
                std::string error;
                if (!DeviceMemory::Save(self.GetContext(), name, path, offset, length, error))
                    throw py::value_error(error);
            }, py::arg("name"), py::arg("path"), py::arg("offset") = 0, py::arg("length") = 0,
               "Save a device memory region (or a part; length 0 = to the end) to a file")
            .def("region_load", [](Emulator& self, const std::string& name, const std::string& path, uint32_t offset) -> size_t {
                std::string error;
                size_t written = 0;
                if (!DeviceMemory::Load(self.GetContext(), name, path, offset, written, error))
                    throw py::value_error(error);
                return written;
            }, py::arg("name"), py::arg("path"), py::arg("offset") = 0,
               "Load a file into a device memory region at offset (through the device's write path); returns the byte count")
            .def("sprinter_video", [](Emulator& self, py::object page, bool all, bool squares) -> py::object {
                DeviceState::SprinterVideoQuery query;
                std::string error;
                const std::string pageText = page.is_none() ? std::string() : std::string(py::str(page));
                if (!DeviceState::SprinterVideoQueryFromStrings(pageText, all ? "1" : "0", squares ? "1" : "0", query, error))
                    throw py::value_error(error);
                return StateNodeToPy(DeviceState::SprinterVideo(self.GetContext(), query));
            }, py::arg("page") = py::none(), py::arg("all") = false, py::arg("squares") = true,
               "Sprinter mode table per square: HOLD, frame length, RGMOD, PORT_Y, counts, map (one letter a square: G 320, g 640, T text 40, t text 80, Z Spectrum cell, B border, . blank, * INT), palettes used, squares[b][a] decoded; page 0/1 (default RGMOD's), all=True = 56 x 40")
            .def("sprinter_palette", [](Emulator& self, py::object k) -> py::object {
                int palette = DeviceState::kSprinterPalettesUsed;
                std::string error;
                const std::string text = k.is_none() ? std::string() : std::string(py::str(k));
                if (!DeviceState::SprinterPaletteFromString(text, palette, error))
                    throw py::value_error(error);
                return StateNodeToPy(DeviceState::SprinterPalette(self.GetContext(), palette));
            }, py::arg("k") = py::none(),
               "Sprinter palettes from video RAM: pens (n, rgb '#RRGGBB' = R, G, B as stored, vram address); k 0-7, 'all' or 'used' (default)")
            .def("sprinter_bios", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::SprinterBios(self.GetContext()));
            }, "Sprinter BIOS images (file, alias, version, CRC-32, present, loaded, selected, known_issues), known_issues of the loaded image, reload_pending, start options; available=False on other machines")
            .def("sprinter_bios_select", [](Emulator& self, py::object bios, py::object fastStart, py::object accelIntSuspend, bool reset) -> py::object {
                auto text = [](const py::object& value) -> std::string {
                    if (value.is_none())
                        return std::string();
                    if (py::isinstance<py::bool_>(value))
                        return value.cast<bool>() ? "1" : "0";
                    return py::str(value);
                };
                SprinterBios::Options options;
                std::string error;
                if (!SprinterBios::OptionsFromStrings(text(bios), text(fastStart), text(accelIntSuspend), reset ? "1" : "0",
                                                      options, error))
                    throw py::value_error(error);
                const StateNode report = DeviceState::SprinterBiosSelect(self.GetContext(), options);
                const StateNode* available = report.find("available");
                if (available && !available->b)
                    throw py::value_error(report.find("description") ? report.find("description")->s : std::string("failed"));
                return StateNodeToPy(report);
            }, py::arg("bios") = py::none(), py::arg("fast_start") = py::none(), py::arg("accel_int_suspend") = py::none(),
               py::arg("reset") = true,
               "Select the Sprinter BIOS (3.04 / 3.06 / 3.07 / a file) and start options; the image loads at the reset (now unless reset=False)")
            .def("sprinter_zx_mode", [](Emulator& self, bool deep) -> py::object {
                return StateNodeToPy(DeviceState::SprinterZxMode(self.GetContext(), deep));
            }, py::arg("deep") = true,
               "Sprinter ZX (Spectrum) mode: active, the launcher configuration (each .ZX option from the hardware, the "
               "launcher's text in RAM), best-matching mode file with confidence, clock (CNF request, F12, MHz), frame / INT, "
               "ROMs by CRC, the decode of #7FFD / #1FFD / #01FD / #xxFD / #FE / #1F; deep=False skips the whole-RAM search")
            .def("sprinter_pld_journal", [](Emulator& self, py::object kinds, py::object since, py::object from_frame,
                                            py::object to_frame, py::object limit, const std::string& source) -> py::object {
                auto text = [](const py::object& value) -> std::string {
                    if (value.is_none())
                        return std::string();
                    return py::str(value);
                };
                DeviceState::SprinterJournalQuery query;
                std::string error;
                if (!DeviceState::SprinterJournalQueryFromStrings(text(kinds), text(since), text(from_frame), text(to_frame),
                                                                  text(limit), source, query, error))
                    throw py::value_error(error);
                return StateNodeToPy(DeviceState::SprinterJournal(self.GetContext(), query));
            }, py::arg("kinds") = py::none(), py::arg("since") = py::none(), py::arg("from_frame") = py::none(),
               py::arg("to_frame") = py::none(), py::arg("limit") = py::none(), py::arg("source") = "live",
               "Sprinter PLD journal: who changed the PLD setup (port table, CNF / turbo, clock, #7FFD / #1FFD, ALL_MODE, RGMOD, "
               "HOLD, frame length, PLD load, F12, Ctrl+Alt+Del, resets) with frame, T, PC; source='ttd' reads the recording")
            .def("sprinter_pld_journal_control", [](Emulator& self, py::object enabled, bool clear) -> py::object {
                const int enable = enabled.is_none() ? -1 : (enabled.cast<bool>() ? 1 : 0);
                return StateNodeToPy(DeviceState::SprinterJournalControl(self.GetContext(), enable, clear));
            }, py::arg("enabled") = py::none(), py::arg("clear") = false, "Switch (enabled=True/False) or clear the Sprinter PLD journal")
            .def("sprinter_sound_ring", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::SprinterSoundRing(self.GetContext()));
            }, "Sprinter Covox-Blaster ring: 256 words, play / write index; available=False on other machines")
            .def("sprinter_ports", [](Emulator& self, py::object map, py::object dos, py::object pn5, const std::string& rw) -> py::object {
                auto text = [](const py::object& value) -> std::string {
                    if (value.is_none())
                        return std::string();
                    if (py::isinstance<py::bool_>(value))
                        return value.cast<bool>() ? "1" : "0";
                    return py::str(value);
                };
                DeviceState::SprinterPortQuery query;
                std::string error;
                if (!DeviceState::SprinterPortQueryFromStrings(text(map), text(dos), text(pn5), rw, query, error))
                    throw py::value_error(error);
                return StateNodeToPy(DeviceState::SprinterPortTable(self.GetContext(), query));
            }, py::arg("map") = py::none(), py::arg("dos") = py::none(), py::arg("pn5") = py::none(), py::arg("rw") = "",
               "The decoded Sprinter port table (RAM page #40): rows of code, name, address pattern; map 0-3, dos 0/1 (1 = TR-DOS on), pn5 0/1, rw 'r'/'w'/'rw'")
            .def("sprinter_port", [](Emulator& self, py::object port, const std::string& rw, py::object map, py::object dos, py::object pn5) -> py::object {
                auto text = [](const py::object& value) -> std::string {
                    if (value.is_none())
                        return std::string();
                    if (py::isinstance<py::bool_>(value))
                        return value.cast<bool>() ? "1" : "0";
                    return py::str(value);
                };
                uint16_t number = 0;
                if (py::isinstance<py::int_>(port))
                {
                    const long long value = port.cast<long long>();
                    if (value < 0 || value > 0xFFFF)
                        throw py::value_error("port must be 0-0xFFFF");
                    number = static_cast<uint16_t>(value);
                }
                else if (!py::isinstance<py::str>(port) || !DeviceState::SprinterPortFromString(port.cast<std::string>(), number))
                    throw py::value_error("port: an int or a hex string ('21BC', '#21BC')");
                DeviceState::SprinterPortQuery query;
                std::string error;
                if (!DeviceState::SprinterPortQueryFromStrings(text(map), text(dos), text(pn5), rw, query, error))
                    throw py::value_error(error);
                return StateNodeToPy(DeviceState::SprinterPortLookup(self.GetContext(), number, query));
            }, py::arg("port"), py::arg("rw") = "", py::arg("map") = py::none(), py::arg("dos") = py::none(), py::arg("pn5") = py::none(),
               "One Sprinter port through the port table: index into page #40, code and name (or the Z84C15 when the chip answers it)")
            .def("network_state", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::Network(self.GetContext()));
            }, "Network adapters: card (ZXNETUSB ports, W5300 address registers and sockets), virtual network (DHCP leases, sockets, guest servers, counters, recent activity); available=False without one")
            .def("network_configure", [](Emulator& self, py::kwargs settings) {
                NetworkManager* manager = self.GetContext()->pCore ? self.GetContext()->pCore->GetNetworkManager() : nullptr;
                if (!manager)
                    throw py::value_error("no network support in this machine");
                std::vector<std::pair<std::string, std::string>> kv;
                for (auto item : settings)
                {
                    const std::string key = py::str(item.first);
                    std::string text;
                    if (py::isinstance<py::bool_>(item.second))
                        text = item.second.cast<bool>() ? "on" : "off";
                    else
                        text = py::str(item.second);
                    kv.emplace_back(key, text);
                }
                NetworkManager::Change change;
                std::string error;
                if (!NetworkManager::ParseChange(kv, change, error) || !manager->RequestChange(change, error))
                    throw py::value_error(error);
            }, "Change network settings: card='none'|'zxnetusb'|'zxwifi'|'atm2ioesp' (a list with ','), host_access=True|False, dns_mode='host'|'pass', hosts='name=ip,...', forwards='tcp:host:guest,...', remote_access=True|False (the host listeners of guest servers: 0.0.0.0, every interface, or 127.0.0.1 only; alone it keeps every connection), connect_timeout_ms=n, com_port='none'|'loopback'|'tcp:host:port'|'serial:device[,baud]'|'espnet[,baud]'|'at[,firmware][,baud]' (firmware: 'esp32'|'esp8266'|'esp8266-at221'|'esp8266-at222', for this module alone) (the machine's serial port: the ZX-Evo AVR's, the ATM Turbo 2+ keyboard controller's or the ZX Profi v5's 8251; an ESP module's baud defaults to the port's, 38400 on ATM2, else 115200), zx_wifi='at'|'espnet'|... (the ZX-WiFi card's ESP), com_modem_lines=True|False, esp_chip='esp32'|'esp8266'|'esp8266-at221'|'esp8266-at222' (the Sprinter's SprinterESP takes an ESP8266 build, else esp8266-at222), isa1_peer / isa2_peer='at'|'modem[,guest port]'|'loopback'|'tcp:host:port'|'serial:device[,baud]' (Sprinter: a UART card's line - SprinterESP default 'at', ISA modem default 'modem', SprinterSerial COM1 default 'none'), isa1_peer_b / isa2_peer_b (SprinterSerial COM2), modem_phonebook='5551234=host:port,...' (the numbers a Hayes modem peer dials; com_port='modem' puts one on any machine's serial port), avr_firmware='baseconf'|'base2010'..'base2023'|'ts'|'ts2013'|'ts2016-02'|'ts2016-04' (ZX-Evo), kbc_firmware='none'|'v22-7'..'v41' (ATM Turbo 2+ keyboard controller; com_port is its RS-232 from v31 on), atm2ioesp='at'|'espnet'|... and atm2ioesp_address=0xF0|0xF8 (the ATM2IOESP card on the ATM Turbo 2+ INTERNAL I/O connector), zifi='none'|'at[,firmware]'|'zifi-native[,s3|esp01s]'|'loopback'|'tcp:host:port'|'serial:device[,baud]' (TS-Conf, ZX-Evo with a TS firmware: the ZiFi board's ESP; 'at' = the original ESP-01, NonOS AT 1.7.4 unless an ESP8266 build is named; 'zifi-native' = the 2026 firmware, s3 = ESP32-S3-Zero, esp01s = ESP-01S), ethernet_mode='nat'|'bridge' and bridge_adapter='en0' (the frame cards: the gateway's NAT or their frames on a host adapter, see network_adapters()); applied at the next frame boundary, every connection closes")
            .def("rtc_state", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::Rtc(self.GetContext()));
            }, "CMOS clock: part, ports, NVRAM file, time base, time, registers A-D, alarms, cell dump; available=False without one")
            .def("profi_state", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::ProfiPeripherals(self.GetContext()));
            }, "ZX Profi board chips: port map in force, 8255, 8253 counters, 8251 and the #B3 latch; available=False on other machines")
            .def("rtc_read", [](Emulator& self, unsigned start, unsigned count) -> py::bytes {
                std::vector<uint8_t> bytes;
                std::string error;
                if (!RtcAccess::Read(self.GetContext(), start, count, bytes, error))
                    throw py::value_error(error);
                return py::bytes(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            }, py::arg("start"), py::arg("count") = 1,
               "Read CMOS cells as the guest reads them (no side effects); returns bytes")
            .def("rtc_write", [](Emulator& self, unsigned start, const std::vector<int>& values) {
                std::vector<uint8_t> bytes;
                for (int v : values)
                {
                    if (v < 0 || v > 255)
                        throw py::value_error("Every value must be 0-255");
                    bytes.push_back(static_cast<uint8_t>(v));
                }
                std::string error;
                if (!RtcAccess::Write(self.GetContext(), start, bytes, "Python rtc_write", error))
                    throw py::value_error(error);
            }, py::arg("start"), py::arg("values"),
               "Write CMOS cells like a guest write (time registers set the clock); values: list of ints 0-255 (list(b) for bytes)")
            // ISA slots (Sprinter ISA tdd §10): DeviceState::Isa and IsaAccess, as every interface
            .def("isa_state", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::Isa(self.GetContext()));
            }, "ISA slots (Sprinter): the #9FBD latch, window 3, slots with the configured and fitted cards, counters; available=False elsewhere")
            .def("network_frames", [](Emulator& self, const std::string& link, unsigned last) -> py::object {
                return StateNodeToPy(EthernetAccess::Frames(self.GetContext(), link, last));
            }, py::arg("link") = "", py::arg("last") = 32,
               "The Ethernet gateway's capture of the frame-level cards (index, frame, direction, port, summary, hex)")
            .def("network_frames_pcap", [](Emulator& self, const std::string& link) -> py::bytes {
                std::vector<uint8_t> pcap;
                std::string error;
                if (!EthernetAccess::Pcap(self.GetContext(), link, pcap, error))
                    throw py::value_error(error);
                return py::bytes(reinterpret_cast<const char*>(pcap.data()), pcap.size());
            }, py::arg("link") = "", "The capture as a pcap file (bytes)")
            .def("network_adapters", [](Emulator&) -> py::object {
                return StateNodeToPy(EthernetAccess::Adapters());
            }, "The host adapters the bridge can use (ethernet_mode='bridge'): name, ipv4, wireless, bridgeable; library, error")
            .def("network_inject_frame", [](Emulator& self, const std::string& link, const std::string& hex) {
                std::string error;
                if (!EthernetAccess::Inject(self.GetContext(), link, hex, "Python network_inject_frame", error))
                    throw py::value_error(error);
            }, py::arg("link"), py::arg("hex"), "A frame towards the card `link` (hex), offered at the next frame boundary")
            .def("isa_journal", [](Emulator& self, unsigned last) -> py::object {
                return StateNodeToPy(DeviceState::IsaJournal(self.GetContext(), last));
            }, py::arg("last") = 64, "ISA access journal: the last N card accesses and bus events (frame, t, pc, slot, register)")
            .def("isa_io_read", [](Emulator& self, int slot, py::object address) { return PyIsaCycle(self, "io_read", slot, address, -1); },
                 py::arg("slot"), py::arg("address"), "One ISA I/O read cycle (slot 1 or 2, 20-bit ISA address as int or '#30A' text); returns the byte")
            .def("isa_io_write", [](Emulator& self, int slot, py::object address, int value) { return PyIsaCycle(self, "io_write", slot, address, value); },
                 py::arg("slot"), py::arg("address"), py::arg("value"), "One ISA I/O write cycle")
            .def("isa_io_peek", [](Emulator& self, int slot, py::object address) { return PyIsaCycle(self, "io_peek", slot, address, -1); },
                 py::arg("slot"), py::arg("address"), "What the card shows at an I/O address, no side effect")
            .def("isa_mem_read", [](Emulator& self, int slot, py::object address) { return PyIsaCycle(self, "mem_read", slot, address, -1); },
                 py::arg("slot"), py::arg("address"), "One ISA memory read cycle; returns the byte")
            .def("isa_mem_write", [](Emulator& self, int slot, py::object address, int value) { return PyIsaCycle(self, "mem_write", slot, address, value); },
                 py::arg("slot"), py::arg("address"), py::arg("value"), "One ISA memory write cycle")
            .def("isa_reset", [](Emulator& self) { return PyIsaCycle(self, "reset", 0, py::int_(0), -1); },
                 "One RESET DRV pulse to both ISA slots")
            .def("isa_latch", [](Emulator& self, int value) { return PyIsaCycle(self, "latch", 0, py::int_(0), value); },
                 py::arg("value"), "Write the #9FBD latch (A19-A14, AEN bit 6, RESET bit 7)")
            .def("fdc_state", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::Fdc(self.GetContext()));
            }, "Beta Disk WD1793 state report: registers, status bits, FSM, signals, drives")
            .def("contention_state", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::Contention(self.GetContext()));
            }, "Memory contention report: rule, switch, interface, contended slots, per-kind waits while debugging")

            // General Sound card (GS design §11.6). All actions mirror the
            // host-port semantics - each flushes the coprocessor to the
            // current ZX tact first. No-ops / None when the card is not fitted.
            .def("gs_enabled", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                return ctx && ctx->pSoundManager && ctx->pSoundManager->getGeneralSound() != nullptr;
            }, "Check if the General Sound card is fitted")
            // One report for every interface (DeviceState::Gs); available=False
            // with a description when no card is fitted
            .def("gs_state", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::Gs(self.GetContext()));
            }, "General Sound state (the report WebAPI /state/audio/gs serves): device, mailbox, MPAG page, DAC "
               "channels, card CPU; 'neogs' on the NeoGS card")
            .def("audio_covox_state", [](Emulator& self) -> py::object {
                return StateNodeToPy(DeviceState::Covox(self.GetContext()));
            }, "Covox / SoundDrive state: fitment, the ports this model decodes, Beta-128 shared ports, DAC latches")
            .def("audio_moonsound_state", [](Emulator& self, const std::string& part) -> py::object {
                if (part == "fm")
                    return StateNodeToPy(DeviceState::MoonSoundFm(self.GetContext()));
                if (part == "pcm")
                    return StateNodeToPy(DeviceState::MoonSoundPcm(self.GetContext()));
                if (!part.empty())
                    throw py::value_error("part must be '', 'fm' or 'pcm'");
                return StateNodeToPy(DeviceState::MoonSound(self.GetContext()));
            }, "MoonSound (OPL4) state: overview (part=''), the FM half (part='fm') or the wavetable half (part='pcm')",
               py::arg("part") = "")
            // NeoGS media: checked here, carried out on the machine's thread
            // (neogsmedia.h); true when accepted. Insert / eject are refused
            // while a TTD recording runs
            .def("gs_sd_insert", [](Emulator& self, const std::string& path) -> bool {
                return NeoGSMediaAccepted(NeoGSRequestSdInsert(self.GetContext(), path));
            }, "NeoGS: insert an SD card image (refused while a TTD recording runs)", py::arg("path"))
            .def("gs_sd_eject", [](Emulator& self) -> bool {
                return NeoGSMediaAccepted(NeoGSRequestSdEject(self.GetContext()));
            }, "NeoGS: remove the SD card (refused while a TTD recording runs)")
            .def("gs_flash_save", [](Emulator& self) -> bool {
                return NeoGSMediaAccepted(NeoGSRequestFlashSave(self.GetContext()));
            }, "NeoGS: save the reprogrammed flash (loaded in place of the shipped image with [NGS] FlashWrite=persist)")
            .def("gs_stereo_mode", [](Emulator& self, const std::string& mode) -> bool {
                NeoGSConfig::StereoMode parsed = NeoGSConfig::StereoMode::Separated;
                SoundManager* sm = self.GetContext() ? self.GetContext()->pSoundManager : nullptr;
                if (!sm || !neogsParseStereoMode(mode, parsed))
                    return false;
                sm->setNeoGSStereoMode(parsed);
                return true;
            }, "NeoGS: 'separated' (as on the board), 'gs' (50% cross-feed like the classic GS) or 'mono'; applied at the next frame", py::arg("mode"))
            .def("gs_reset", [](Emulator& self) {
                return SubmitGSInput(self, ttd::TTDInputKind::GSReset);
            }, "Full power-on reset of the General Sound card (live input; True when submitted)")
            .def("gs_reset_card", [](Emulator& self) {
                // #33 bit7 semantics: CPU/banking/timing only, mailbox survives
                return SubmitGSInput(self, ttd::TTDInputKind::GSResetCard);
            }, "#33 bit7 card reset (CPU/banking/timing only; live input; True when submitted)")
            .def("gs_nmi", [](Emulator& self) {
                return SubmitGSInput(self, ttd::TTDInputKind::GSNmi);
            }, "Pulse the #33 bit6 NMI line (live input; True when submitted)")
            .def("gs_send_command", [](Emulator& self, int byte) {
                return SubmitGSInput(self, ttd::TTDInputKind::GSCommand, byte);
            }, "Send command byte to GS (OUT #BB semantics; live input; True when submitted)", py::arg("byte"))
            .def("gs_send_data", [](Emulator& self, int byte) {
                return SubmitGSInput(self, ttd::TTDInputKind::GSData, byte);
            }, "Send data byte to GS (OUT #B3 semantics; live input; True when submitted)", py::arg("byte"))
            .def("gs_read_data", [](Emulator& self) -> int {
                auto* ctx = self.GetContext();
                GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
                return gs ? gs->getDataToHost() : -1;
            }, "Peek the GS data-to-host byte (IN #B3 value; status bit 7 is not cleared)")
            .def("gs_read_status", [](Emulator& self) -> int {
                auto* ctx = self.GetContext();
                GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
                return gs ? (gs->getStatusRaw() | 0x7E) : -1;
            }, "Peek the GS status register (IN #BB value)")
            .def("gs_switch_personality", [](Emulator& self, const std::string& personality) -> bool {
                // Runtime personality switch (GS card personalities design
                // §11.3): requested here, applied at the next frame boundary
                // on the emulation thread - same semantics as the WebAPI
                // switch_personality action and the MCP gs_switch_personality
                // tool action
                auto* ctx = self.GetContext();
                SoundManager* sm = ctx ? ctx->pSoundManager : nullptr;
                if (!sm) return false;

                GSTypeKind target;
                if (!gsParsePersonality(personality, target))
                    return false;

                std::string refusal;
                const bool requested = sm->requestGeneralSoundCardSwitch(target, &refusal);
                if (!requested && !refusal.empty())
                    throw std::runtime_error(refusal);  // a TTD recording refuses the switch (FR-4)
                return requested;
            }, "Request a GS card personality swap ('z80'/'lle', 'lw'/'lightweight' or 'ngs'/'neogs'), applied at the next "
               "frame boundary (RuntimeError while TTD records)",
               py::arg("personality"))
            .def("gs_dump_module", [](Emulator& self, const std::string& path) -> py::object {
                // Diagnostics: write the last completed COM30..D2 upload
                // (the raw ProTracker module the host streamed) to a file -
                // same data dump_module serves via the WebAPI/MCP
                auto* ctx = self.GetContext();
                GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
                if (!gs) return py::none();

                std::vector<uint8_t> bytes;
                bool playing = false;
                if (!gs->captureModuleUpload(bytes, playing))
                    return py::none();

                std::ofstream out(path, std::ios::binary);
                if (!out) return py::none();
                out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

                py::dict d;
                d["path"] = path;
                d["bytes"] = bytes.size();
                d["playing"] = playing;
                return d;
            }, "Write the last completed COM30..D2 module upload to a file (default 'gs-module-dump.mod')",
               py::arg("path") = "gs-module-dump.mod")

            // GS coprocessor triage: always-on activity counters + opt-in
            // port/DAC event trace - the "is the GS Z80 alive and doing DAC
            // pushes" tool, same data model as CLI 'gsporttrace' / WebAPI /
            // MCP / Lua (see gsporttrace.h).
            .def("gs_counters", [](Emulator& self) -> py::object {
                auto* ctx = self.GetContext();
                GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
                if (!gs) return py::none();

                const GSActivityCounters& c = gs->getActivityCounters();
                py::dict d;
                d["cpu_steps"] = c.cpuSteps;
                d["interrupts_accepted"] = c.interruptsAccepted;
                d["interrupt_periods"] = c.interruptPeriods;
                d["interrupts_coalesced"] = c.interruptsCoalesced;
                d["nmis_accepted"] = c.nmisAccepted;
                d["dac_fetches"] = c.dacFetches;
                d["volume_latch_writes"] = c.volumeLatchWrites;
                d["host_commands_received"] = c.hostCommandsReceived;
                d["host_commands_dropped"] = c.hostCommandsDropped;
                d["host_data_written"] = c.hostDataWritten;
                d["host_data_dropped"] = c.hostDataDropped;
                d["host_data_read"] = c.hostDataRead;
                d["last_dac_fetch_gs_cycle"] = c.lastDacFetchGsCycle;
                d["last_dac_fetch_frame"] = c.lastDacFetchFrame;
                d["trace_capturing"] = gs->isPortTraceCapturing();
                d["trace_event_count"] = gs->getPortTraceEventCount();
                d["pc"] = gs->getCPUReg(GSCpuRegister::PC);
                d["halted"] = gs->isCPUHalted();
                return d;
            }, "GS activity counters: CPU steps, interrupts/NMIs accepted + period/coalesce accounting, DAC fetches, volume writes, host mailbox traffic + FIFO drops")
            .def("gs_porttrace_start", [](Emulator& self) {
                auto* ctx = self.GetContext();
                GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
                if (gs) gs->startPortTrace();
            }, "Start capturing the GS port/DAC event trace (clears the buffer)")
            .def("gs_porttrace_stop", [](Emulator& self) {
                auto* ctx = self.GetContext();
                GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
                if (gs) gs->stopPortTrace();
            }, "Stop capturing the GS port/DAC event trace")
            .def("gs_porttrace_pause", [](Emulator& self) {
                auto* ctx = self.GetContext();
                GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
                if (gs) gs->pausePortTrace();
            }, "Pause the GS port/DAC event trace")
            .def("gs_porttrace_resume", [](Emulator& self) {
                auto* ctx = self.GetContext();
                GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
                if (gs) gs->resumePortTrace();
            }, "Resume a paused GS port/DAC event trace")
            .def("gs_porttrace_clear", [](Emulator& self) {
                auto* ctx = self.GetContext();
                GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
                if (gs) gs->clearPortTrace();
            }, "Clear the GS port/DAC event trace buffer")
            .def("gs_porttrace_events", [](Emulator& self, int count) -> py::object {
                auto* ctx = self.GetContext();
                GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
                if (!gs) return py::none();

                auto events = gs->getPortTraceLast(static_cast<size_t>(count));
                py::list result;
                for (const auto& e : events)
                {
                    py::dict ev;
                    ev["timestamp"] = e.timestamp;
                    ev["frame"] = e.frameNumber;
                    switch (e.side)
                    {
                        case GSTraceSide::Host: ev["side"] = "host"; break;
                        case GSTraceSide::GsInternal: ev["side"] = "gs"; break;
                        case GSTraceSide::DacFetch: ev["side"] = "dac"; break;
                        case GSTraceSide::Interrupt: ev["side"] = "interrupt"; break;
                        case GSTraceSide::ZxDma: ev["side"] = "zxdma"; break;
                    }
                    ev["direction"] = e.isOut() ? "out" : "in";
                    ev["port"] = e.port;
                    ev["value"] = e.value;
                    ev["pc"] = e.pc;
                    if (e.side == GSTraceSide::DacFetch) ev["channel"] = e.channel;
                    if (e.side == GSTraceSide::ZxDma) ev["card_address"] = (static_cast<uint32_t>(e.channel) << 16) | e.port;
                    if (e.side == GSTraceSide::Interrupt) ev["nmi"] = e.isNmi();
                    result.append(ev);
                }
                return result;
            }, "Last N buffered GS trace events (host ports, GS-side ports, DAC fetches, interrupts)", py::arg("count") = 50)

            // Advanced disk operations
            .def("disk_info", [](Emulator& self, int drive) -> py::dict {
                py::dict info;
                auto* ctx = self.GetContext();
                if (!ctx || drive < 0 || drive > 3) return info;
                FDD* fdd = ctx->coreState.diskDrives[drive];
                if (!fdd) return info;
                DiskImage* disk = fdd->getDiskImage();
                if (!disk) return info;
                info["cylinders"] = disk->getCylinders();
                info["sides"] = disk->getSides();
                info["tracks"] = disk->getCylinders() * disk->getSides();
                // Geometry of track 0 side 0 (tracks may differ on non-TR-DOS images)
                auto* track0 = disk->getTrackForCylinderAndSide(0, 0);
                info["sectors_per_track"] = track0 ? static_cast<int>(track0->sectorCount()) : 0;
                info["sector_size"] = (track0 && track0->sectorCount() > 0) ? static_cast<int>(track0->getRawSector(0)->dataSize) : 0;
                info["track_size"] = track0 ? static_cast<int>(track0->rawSize()) : 0;
                return info;
            }, "Get disk geometry info", py::arg("drive"))
            .def("disk_read_sector", [](Emulator& self, int drive, int cyl, int side, int sector) -> py::bytes {
                auto* ctx = self.GetContext();
                if (!ctx || drive < 0 || drive > 3) return py::bytes();
                FDD* fdd = ctx->coreState.diskDrives[drive];
                if (!fdd) return py::bytes();
                DiskImage* disk = fdd->getDiskImage();
                if (!disk) return py::bytes();
                auto* track = disk->getTrackForCylinderAndSide(cyl, side);
                if (!track) return py::bytes();
                auto* sec = track->getSector(static_cast<uint8_t>(sector));  // Sector number = sector + 1
                if (!sec || !sec->hasData) return py::bytes();
                return py::bytes(reinterpret_cast<char*>(sec->data), sec->dataSize);
            }, "Read sector data (128..1024 bytes depending on the sector's ID field)", py::arg("drive"), py::arg("cyl"), py::arg("side"), py::arg("sector"))
            .def("disk_write_sector", [](Emulator& self, int drive, int cyl, int side, int sector, const py::bytes& data,
                                         uint32_t offset) {
                if (drive < 0 || drive > 3)
                    throw py::value_error("bad drive (0-3)");
                const std::string text = data;
                const std::vector<uint8_t> bytes(text.begin(), text.end());
                const SectorWrite::Result result = SectorWrite::Write(&self, static_cast<uint8_t>(drive), cyl, side,
                                                                      sector + 1, offset, bytes, "python");
                if (result.busy)
                    throw std::runtime_error(result.error);
                if (!result.ok)
                    throw py::value_error(result.error);
            }, "Write bytes into a sector's data field (SectorWrite: data CRC follows, the image counts as modified, a TTD "
               "tool edit). sector is 0-based as in disk_read_sector (ID - 1). ValueError when refused (empty drive, "
               "write-protected, no such sector, past the data field), RuntimeError when no coherent moment came",
               py::arg("drive"), py::arg("cyl"), py::arg("side"), py::arg("sector"), py::arg("data"), py::arg("offset") = 0)
            .def("disk_read_sector_hex", [](Emulator& self, int drive, int track, int sector) -> std::string {
                auto* ctx = self.GetContext();
                if (!ctx || drive < 0 || drive > 3) return "";
                FDD* fdd = ctx->coreState.diskDrives[drive];
                if (!fdd) return "";
                DiskImage* disk = fdd->getDiskImage();
                if (!disk) return "";
                return disk->DumpSectorHex(track, sector);
            }, "Read sector as hex dump", py::arg("drive"), py::arg("track"), py::arg("sector"))
            
            // Debug mode control
            .def("debugmode", [](Emulator& self, bool enable) -> bool {
                FeatureManager* fm = self.GetFeatureManager();
                return fm ? fm->setFeature("debugmode", enable) : false;
            }, "Enable/disable debug mode", py::arg("enable"))
            .def("is_debugmode", [](Emulator& self) -> bool {
                FeatureManager* fm = self.GetFeatureManager();
                return fm ? fm->isEnabled("debugmode") : false;
            }, "Check if debug mode is enabled")
            
            // Memory access counters - uses memory->GetAccessTracker() API
            .def("memcounters", [](Emulator& self) -> py::dict {
                py::dict result;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pMemory) {
                    result["error"] = "Memory not available";
                    return result;
                }
                Memory* memory = ctx->pMemory;
                MemoryAccessTracker& tracker = memory->GetAccessTracker();
                
                // Sum Z80 banks
                uint64_t totalReads = 0;
                uint64_t totalWrites = 0;
                uint64_t totalExecutes = 0;
                
                py::list banks;
                for (int bank = 0; bank < 4; bank++) {
                    uint64_t reads = tracker.GetZ80BankReadAccessCount(bank);
                    uint64_t writes = tracker.GetZ80BankWriteAccessCount(bank);
                    uint64_t executes = tracker.GetZ80BankExecuteAccessCount(bank);
                    
                    totalReads += reads;
                    totalWrites += writes;
                    totalExecutes += executes;
                    
                    py::dict bankInfo;
                    bankInfo["bank"] = bank;
                    bankInfo["reads"] = reads;
                    bankInfo["writes"] = writes;
                    bankInfo["executes"] = executes;
                    bankInfo["total"] = reads + writes + executes;
                    banks.append(bankInfo);
                }
                
                result["total_reads"] = totalReads;
                result["total_writes"] = totalWrites;
                result["total_executes"] = totalExecutes;
                result["total_accesses"] = totalReads + totalWrites + totalExecutes;
                result["banks"] = banks;
                return result;
            }, "Get memory access counters")
            .def("memcounters_reset", [](Emulator& self) {
                auto* ctx = self.GetContext();
                if (ctx && ctx->pMemory) {
                    ctx->pMemory->GetAccessTracker().ResetCounters();
                }
            }, "Reset memory access counters")
            
            // Call trace
            .def("calltrace", [](Emulator& self, int limit) -> py::list {
                py::list result;
                FeatureManager* fm = self.GetFeatureManager();
                bool enabled = fm ? fm->isEnabled("calltrace") : false;
                // TODO: Add actual call trace entries when CallTraceManager exposes API
                return result;
            }, "Get call trace entries (requires calltrace feature)", py::arg("limit") = 50)
            .def("is_calltrace", [](Emulator& self) -> bool {
                FeatureManager* fm = self.GetFeatureManager();
                return fm ? fm->isEnabled("calltrace") : false;
            }, "Check if call trace is enabled")
            
            // Opcode profiler
            .def("profiler_start", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pCore) return false;
                Z80* z80 = ctx->pCore->GetZ80();
                if (!z80) return false;
                OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
                if (!profiler) return false;
                FeatureManager* fm = self.GetFeatureManager();
                if (fm) fm->setFeature("opcode_profiler", true);
                profiler->Start();
                return true;
            }, "Start opcode profiler session (enables feature, clears data)")
            .def("profiler_stop", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pCore) return false;
                Z80* z80 = ctx->pCore->GetZ80();
                if (!z80) return false;
                OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
                if (!profiler) return false;
                profiler->Stop();
                return true;
            }, "Stop opcode profiler session")
            .def("profiler_clear", [](Emulator& self) {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pCore) return;
                Z80* z80 = ctx->pCore->GetZ80();
                if (!z80) return;
                OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
                if (profiler) profiler->Clear();
            }, "Clear profiler data")
            .def("profiler_status", [](Emulator& self) -> py::dict {
                py::dict result;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pCore) return result;
                Z80* z80 = ctx->pCore->GetZ80();
                if (!z80) return result;
                OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
                if (!profiler) return result;
                FeatureManager* fm = self.GetFeatureManager();
                auto status = profiler->GetStatus();
                result["feature_enabled"] = fm ? fm->isEnabled("opcode_profiler") : false;
                result["capturing"] = status.capturing;
                result["total_executions"] = status.totalExecutions;
                result["trace_size"] = status.traceSize;
                result["trace_capacity"] = status.traceCapacity;
                return result;
            }, "Get profiler status")
            .def("profiler_counters", [](Emulator& self, size_t limit) -> py::list {
                py::list result;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pCore) return result;
                Z80* z80 = ctx->pCore->GetZ80();
                if (!z80) return result;
                OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
                if (!profiler) return result;
                auto counters = profiler->GetTopOpcodes(limit);
                for (const auto& counter : counters) {
                    py::dict entry;
                    entry["prefix"] = counter.prefix;
                    entry["opcode"] = counter.opcode;
                    entry["count"] = counter.count;
                    entry["mnemonic"] = counter.mnemonic;
                    result.append(entry);
                }
                return result;
            }, "Get top opcodes by execution count", py::arg("limit") = 100)
            .def("profiler_trace", [](Emulator& self, size_t count) -> py::list {
                py::list result;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pCore) return result;
                Z80* z80 = ctx->pCore->GetZ80();
                if (!z80) return result;
                OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
                if (!profiler) return result;
                auto trace = profiler->GetRecentTrace(count);
                for (const auto& entry : trace) {
                    py::dict item;
                    item["pc"] = entry.pc;
                    item["prefix"] = entry.prefix;
                    item["opcode"] = entry.opcode;
                    item["flags"] = entry.flags;
                    item["a"] = entry.a;
                    item["frame"] = entry.frame;
                    item["tstate"] = entry.tState;
                    result.append(item);
                }
                return result;
            }, "Get recent execution trace", py::arg("count") = 100)
            .def("profiler_pause", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pCore) return false;
                Z80* z80 = ctx->pCore->GetZ80();
                if (!z80) return false;
                OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
                if (!profiler) return false;
                profiler->Pause();
                return true;
            }, "Pause opcode profiler (retain data)")
            .def("profiler_resume", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pCore) return false;
                Z80* z80 = ctx->pCore->GetZ80();
                if (!z80) return false;
                OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
                if (!profiler) return false;
                profiler->Resume();
                return true;
            }, "Resume paused opcode profiler")
            .def("profiler_opcode_session_state", [](Emulator& self) -> std::string {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pCore) return "unavailable";
                Z80* z80 = ctx->pCore->GetZ80();
                if (!z80) return "unavailable";
                OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
                if (!profiler) return "unavailable";
                switch (profiler->GetSessionState()) {
                    case ProfilerSessionState::Stopped: return "stopped";
                    case ProfilerSessionState::Capturing: return "capturing";
                    case ProfilerSessionState::Paused: return "paused";
                    default: return "unknown";
                }
            }, "Get opcode profiler session state")
            
            // Memory profiler session control
            .def("memory_profiler_start", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pMemory) return false;
                auto* tracker = &ctx->pMemory->GetAccessTracker();
                FeatureManager* fm = self.GetFeatureManager();
                if (fm) {
                    fm->setFeature("debugmode", true);
                    fm->setFeature("memorytracking", true);
                    tracker->UpdateFeatureCache();
                }
                tracker->StartMemorySession();
                return true;
            }, "Start memory profiler session (enables features, clears data)")
            .def("memory_profiler_pause", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pMemory) return false;
                ctx->pMemory->GetAccessTracker().PauseMemorySession();
                return true;
            }, "Pause memory profiler (retain data)")
            .def("memory_profiler_resume", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pMemory) return false;
                ctx->pMemory->GetAccessTracker().ResumeMemorySession();
                return true;
            }, "Resume paused memory profiler")
            .def("memory_profiler_stop", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pMemory) return false;
                ctx->pMemory->GetAccessTracker().StopMemorySession();
                return true;
            }, "Stop memory profiler (retain data)")
            .def("memory_profiler_clear", [](Emulator& self) {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pMemory) return;
                ctx->pMemory->GetAccessTracker().ClearMemoryData();
            }, "Clear memory profiler data")
            .def("memory_profiler_status", [](Emulator& self) -> py::dict {
                py::dict result;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pMemory) return result;
                auto& tracker = ctx->pMemory->GetAccessTracker();
                FeatureManager* fm = self.GetFeatureManager();
                result["feature_enabled"] = fm ? fm->isEnabled("memorytracking") : false;
                result["capturing"] = tracker.IsMemoryCapturing();
                switch (tracker.GetMemorySessionState()) {
                    case ProfilerSessionState::Stopped: result["session_state"] = "stopped"; break;
                    case ProfilerSessionState::Capturing: result["session_state"] = "capturing"; break;
                    case ProfilerSessionState::Paused: result["session_state"] = "paused"; break;
                    default: result["session_state"] = "unknown"; break;
                }
                return result;
            }, "Get memory profiler status")
            
            // Calltrace profiler session control
            .def("calltrace_profiler_start", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pMemory) return false;
                auto* tracker = &ctx->pMemory->GetAccessTracker();
                FeatureManager* fm = self.GetFeatureManager();
                if (fm) {
                    fm->setFeature("debugmode", true);
                    fm->setFeature("calltrace", true);
                    tracker->UpdateFeatureCache();
                }
                tracker->StartCalltraceSession();
                return true;
            }, "Start calltrace profiler session (enables features, clears data)")
            .def("calltrace_profiler_pause", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pMemory) return false;
                ctx->pMemory->GetAccessTracker().PauseCalltraceSession();
                return true;
            }, "Pause calltrace profiler (retain data)")
            .def("calltrace_profiler_resume", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pMemory) return false;
                ctx->pMemory->GetAccessTracker().ResumeCalltraceSession();
                return true;
            }, "Resume paused calltrace profiler")
            .def("calltrace_profiler_stop", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pMemory) return false;
                ctx->pMemory->GetAccessTracker().StopCalltraceSession();
                return true;
            }, "Stop calltrace profiler (retain data)")
            .def("calltrace_profiler_clear", [](Emulator& self) {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pMemory) return;
                ctx->pMemory->GetAccessTracker().ClearCalltraceData();
            }, "Clear calltrace profiler data")
            .def("calltrace_profiler_status", [](Emulator& self) -> py::dict {
                py::dict result;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pMemory) return result;
                auto& tracker = ctx->pMemory->GetAccessTracker();
                FeatureManager* fm = self.GetFeatureManager();
                result["feature_enabled"] = fm ? fm->isEnabled("calltrace") : false;
                result["capturing"] = tracker.IsCalltraceCapturing();
                switch (tracker.GetCalltraceSessionState()) {
                    case ProfilerSessionState::Stopped: result["session_state"] = "stopped"; break;
                    case ProfilerSessionState::Capturing: result["session_state"] = "capturing"; break;
                    case ProfilerSessionState::Paused: result["session_state"] = "paused"; break;
                    default: result["session_state"] = "unknown"; break;
                }
                auto* buffer = tracker.GetCallTraceBuffer();
                if (buffer) {
                    result["entry_count"] = buffer->GetCount();
                    result["capacity"] = buffer->GetCapacity();
                }
                return result;
            }, "Get calltrace profiler status")
            
            // Unified profiler control (all profilers at once)
            .def("profilers_start_all", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx) return false;
                FeatureManager* fm = self.GetFeatureManager();
                
                // Enable all profiler features
                if (fm) {
                    fm->setFeature("debugmode", true);
                    fm->setFeature("memorytracking", true);
                    fm->setFeature("calltrace", true);
                    fm->setFeature("opcode_profiler", true);
                }
                
                // Start memory and calltrace profilers
                if (ctx->pMemory) {
                    auto& tracker = ctx->pMemory->GetAccessTracker();
                    tracker.UpdateFeatureCache();
                    tracker.StartMemorySession();
                    tracker.StartCalltraceSession();
                }
                
                // Start opcode profiler
                if (ctx->pCore) {
                    Z80* z80 = ctx->pCore->GetZ80();
                    if (z80) {
                        z80->UpdateFeatureCache();
                        OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
                        if (profiler) profiler->Start();
                    }
                }
                return true;
            }, "Start all profilers (opcode, memory, calltrace)")
            .def("profilers_pause_all", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx) return false;
                
                if (ctx->pMemory) {
                    auto& tracker = ctx->pMemory->GetAccessTracker();
                    tracker.PauseMemorySession();
                    tracker.PauseCalltraceSession();
                }
                if (ctx->pCore) {
                    Z80* z80 = ctx->pCore->GetZ80();
                    if (z80) {
                        OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
                        if (profiler) profiler->Pause();
                    }
                }
                return true;
            }, "Pause all profilers")
            .def("profilers_resume_all", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx) return false;
                
                if (ctx->pMemory) {
                    auto& tracker = ctx->pMemory->GetAccessTracker();
                    tracker.ResumeMemorySession();
                    tracker.ResumeCalltraceSession();
                }
                if (ctx->pCore) {
                    Z80* z80 = ctx->pCore->GetZ80();
                    if (z80) {
                        OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
                        if (profiler) profiler->Resume();
                    }
                }
                return true;
            }, "Resume all profilers")
            .def("profilers_stop_all", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx) return false;
                
                if (ctx->pMemory) {
                    auto& tracker = ctx->pMemory->GetAccessTracker();
                    tracker.StopMemorySession();
                    tracker.StopCalltraceSession();
                }
                if (ctx->pCore) {
                    Z80* z80 = ctx->pCore->GetZ80();
                    if (z80) {
                        OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
                        if (profiler) profiler->Stop();
                    }
                }
                return true;
            }, "Stop all profilers")
            .def("profilers_clear_all", [](Emulator& self) {
                auto* ctx = self.GetContext();
                if (!ctx) return;
                
                if (ctx->pMemory) {
                    auto& tracker = ctx->pMemory->GetAccessTracker();
                    tracker.ClearMemoryData();
                    tracker.ClearCalltraceData();
                }
                if (ctx->pCore) {
                    Z80* z80 = ctx->pCore->GetZ80();
                    if (z80) {
                        OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
                        if (profiler) profiler->Clear();
                    }
                }
            }, "Clear all profiler data")
            .def("profilers_status_all", [](Emulator& self) -> py::dict {
                py::dict result;
                auto* ctx = self.GetContext();
                if (!ctx) return result;
                
                // Memory profiler status
                py::dict memStatus;
                if (ctx->pMemory) {
                    auto& tracker = ctx->pMemory->GetAccessTracker();
                    FeatureManager* fm = self.GetFeatureManager();
                    memStatus["feature_enabled"] = fm ? fm->isEnabled("memorytracking") : false;
                    memStatus["capturing"] = tracker.IsMemoryCapturing();
                    switch (tracker.GetMemorySessionState()) {
                        case ProfilerSessionState::Stopped: memStatus["session_state"] = "stopped"; break;
                        case ProfilerSessionState::Capturing: memStatus["session_state"] = "capturing"; break;
                        case ProfilerSessionState::Paused: memStatus["session_state"] = "paused"; break;
                        default: memStatus["session_state"] = "unknown"; break;
                    }
                }
                result["memory"] = memStatus;
                
                // Calltrace profiler status
                py::dict ctStatus;
                if (ctx->pMemory) {
                    auto& tracker = ctx->pMemory->GetAccessTracker();
                    FeatureManager* fm = self.GetFeatureManager();
                    ctStatus["feature_enabled"] = fm ? fm->isEnabled("calltrace") : false;
                    ctStatus["capturing"] = tracker.IsCalltraceCapturing();
                    switch (tracker.GetCalltraceSessionState()) {
                        case ProfilerSessionState::Stopped: ctStatus["session_state"] = "stopped"; break;
                        case ProfilerSessionState::Capturing: ctStatus["session_state"] = "capturing"; break;
                        case ProfilerSessionState::Paused: ctStatus["session_state"] = "paused"; break;
                        default: ctStatus["session_state"] = "unknown"; break;
                    }
                    auto* buffer = tracker.GetCallTraceBuffer();
                    if (buffer) {
                        ctStatus["entry_count"] = buffer->GetCount();
                    }
                }
                result["calltrace"] = ctStatus;
                
                // Opcode profiler status
                py::dict opStatus;
                if (ctx->pCore) {
                    Z80* z80 = ctx->pCore->GetZ80();
                    if (z80) {
                        OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
                        if (profiler) {
                            FeatureManager* fm = self.GetFeatureManager();
                            auto status = profiler->GetStatus();
                            opStatus["feature_enabled"] = fm ? fm->isEnabled("opcode_profiler") : false;
                            opStatus["capturing"] = status.capturing;
                            opStatus["total_executions"] = status.totalExecutions;
                            switch (profiler->GetSessionState()) {
                                case ProfilerSessionState::Stopped: opStatus["session_state"] = "stopped"; break;
                                case ProfilerSessionState::Capturing: opStatus["session_state"] = "capturing"; break;
                                case ProfilerSessionState::Paused: opStatus["session_state"] = "paused"; break;
                                default: opStatus["session_state"] = "unknown"; break;
                            }
                        }
                    }
                }
                result["opcode"] = opStatus;
                
                return result;
            }, "Get status of all profilers")
            
            .def("key_route", [](Emulator& self, const std::string& route) -> std::string {
                Keyboard* keyboard = self.GetContext()->pKeyboard;
                if (!keyboard)
                    throw py::value_error("no keyboard");
                std::string error;
                if (!route.empty() && !keyboard->RequestHostRoute(route, error))
                    throw py::value_error(error);
                return Keyboard::HostRouteName(keyboard->EffectiveHostRoute());
            }, py::arg("route") = "", "Where host and injected keys go: route='auto'|'matrix'|'ps2'|'both' (the ZX matrix, the PS/2 controller of a ZX-Evo / ATM Turbo 2+, both); empty = query. Returns the route in force")
            .def("keyboard_controller", [](Emulator& self) -> std::string {
                Keyboard* keyboard = self.GetContext() ? self.GetContext()->pKeyboard : nullptr;
                return keyboard && keyboard->HasPs2Sink() ? keyboard->GetPs2Sink()->ControllerName() : std::string();
            }, "The PS/2 / XT keyboard controller's name ('PROFI-XT firmware 1.27', ...); '' when there is none or it has no name")
            .def("key_tap", [](Emulator& self, const std::string& keyName, uint16_t holdFrames) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager->GetKeyboardManager()) return false;
                ctx->pDebugManager->GetKeyboardManager()->TapKey(keyName, holdFrames);
                return true;
            }, "Tap a key (press, hold, release)", py::arg("key"), py::arg("frames") = 2)
            .def("key_press", [](Emulator& self, const std::string& keyName) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager->GetKeyboardManager()) return false;
                ctx->pDebugManager->GetKeyboardManager()->PressKey(keyName);
                return true;
            }, "Press and hold a key", py::arg("key"))
            .def("key_release", [](Emulator& self, const std::string& keyName) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager->GetKeyboardManager()) return false;
                ctx->pDebugManager->GetKeyboardManager()->ReleaseKey(keyName);
                return true;
            }, "Release a held key", py::arg("key"))
            .def("key_combo", [](Emulator& self, const std::vector<std::string>& keyNames, uint16_t holdFrames) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager->GetKeyboardManager()) return false;
                ctx->pDebugManager->GetKeyboardManager()->TapCombo(keyNames, holdFrames);
                return true;
            }, "Tap multiple keys simultaneously", py::arg("keys"), py::arg("frames") = 2)
            .def("key_macro", [](Emulator& self, const std::string& macroName) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager->GetKeyboardManager()) return false;
                return ctx->pDebugManager->GetKeyboardManager()->ExecuteNamedSequence(macroName);
            }, "Execute predefined macro (e_mode, format, cat, etc.)", py::arg("name"))
            .def("key_type", [](Emulator& self, const std::string& text, uint16_t charDelayFrames) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager->GetKeyboardManager()) return false;
                ctx->pDebugManager->GetKeyboardManager()->TypeText(text, charDelayFrames);
                return true;
            }, "Type text with auto modifier handling", py::arg("text"), py::arg("delay_frames") = 2)
            .def("key_trdos_command", [](Emulator& self, const std::string& keyword, const std::string& argument) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager->GetKeyboardManager()) return false;
                ctx->pDebugManager->GetKeyboardManager()->TypeTRDOSCommand(keyword, argument);
                return true;
            }, "Type TR-DOS command with argument", py::arg("keyword"), py::arg("argument") = "")
            .def("key_release_all", [](Emulator& self) {
                auto* ctx = self.GetContext();
                if (ctx && ctx->pDebugManager->GetKeyboardManager()) {
                    ctx->pDebugManager->GetKeyboardManager()->ReleaseAllKeys();
                }
            }, "Release all currently pressed keys")
            .def("key_is_running", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager->GetKeyboardManager()) return false;
                return ctx->pDebugManager->GetKeyboardManager()->IsSequenceRunning();
            }, "Check if a key sequence is currently running")
            .def("key_abort", [](Emulator& self) {
                auto* ctx = self.GetContext();
                if (ctx && ctx->pDebugManager->GetKeyboardManager()) {
                    ctx->pDebugManager->GetKeyboardManager()->AbortSequence();
                }
            }, "Abort current key sequence")
            .def("key_list", []() -> py::list {
                py::list keys;
                auto names = DebugKeyboardManager::GetAllKeyNames();
                for (const auto& name : names) {
                    keys.append(name);
                }
                return keys;
            }, "List all recognized key names")

            // -----------------------------------------------------------------
            // Kempston Mouse injection (automation-interfaces §4.6)
            // Errors raise: ValueError for bad arguments, RuntimeError for
            // TTD replay / missing device. Success returns the state dict.
            // -----------------------------------------------------------------
            .def("mouse_move", [](Emulator& self, int dx, int dy) -> py::dict {
                DebugMouseManager& mgr = MouseManagerOrThrow(self);
                return MouseResultOrThrow(mgr, mgr.Move(dx, dy));
            }, "Move the mouse by dx,dy emulated pixels (+x right, +y up; -127..127)", py::arg("dx"), py::arg("dy"))
            .def("mouse_glide", [](Emulator& self, int dx, int dy) -> py::dict {
                DebugMouseManager& mgr = MouseManagerOrThrow(self);
                return MouseResultOrThrow(mgr, mgr.Glide(dx, dy));
            }, "Long move (-4096..4096) in steps the program follows, one per frame; input sent meanwhile queues",
               py::arg("dx"), py::arg("dy"))
            .def("mouse_devices", [](Emulator& self) -> py::list {
                DebugMouseManager& mgr = MouseManagerOrThrow(self);
                py::list devices;
                for (const MouseDeviceStatus& device : mgr.GetState().devices)
                    devices.append(MouseDeviceDict(device));
                return devices;
            }, "The machine's mouse devices (kempston, sprinter, evo-ps2)")
            .def("mouse_busy", [](Emulator& self) -> bool {
                return MouseManagerOrThrow(self).IsBusy();
            }, "True while a glide (and the input queued behind it) is in progress")
            .def("mouse_press", [](Emulator& self, const std::string& button) -> py::dict {
                MouseButton resolved = MouseButtonOrThrow(button);
                DebugMouseManager& mgr = MouseManagerOrThrow(self);
                return MouseResultOrThrow(mgr, mgr.PressButton(resolved));
            }, "Press and hold a mouse button (left|right|middle, or l|r|m)", py::arg("button"))
            .def("mouse_release", [](Emulator& self, const std::string& button) -> py::dict {
                MouseButton resolved = MouseButtonOrThrow(button);
                DebugMouseManager& mgr = MouseManagerOrThrow(self);
                return MouseResultOrThrow(mgr, mgr.ReleaseButton(resolved));
            }, "Release a mouse button", py::arg("button"))
            .def("mouse_click", [](Emulator& self, const std::string& button, int64_t frames) -> py::dict {
                MouseButton resolved = MouseButtonOrThrow(button);
                DebugMouseManager& mgr = MouseManagerOrThrow(self);
                if (frames < 0 || frames > static_cast<int64_t>(std::numeric_limits<uint32_t>::max()))
                    throw py::value_error("frames=" + std::to_string(frames) + " out of range 1.." +
                                          std::to_string(DebugMouseManager::MAX_CLICK_FRAMES));
                return MouseResultOrThrow(mgr, mgr.Click(resolved, static_cast<uint32_t>(frames)));
            }, "Press a mouse button, hold for frames, release", py::arg("button"),
               py::arg("frames") = static_cast<int64_t>(DebugMouseManager::DEFAULT_CLICK_FRAMES))
            .def("mouse_buttons", [](Emulator& self, const std::vector<std::string>& pressed) -> py::dict {
                uint8_t bits = 0;
                for (const auto& name : pressed)
                    bits |= static_cast<uint8_t>(MouseButtonOrThrow(name));
                DebugMouseManager& mgr = MouseManagerOrThrow(self);
                return MouseResultOrThrow(mgr, mgr.SetPressedButtons(bits));
            }, "Set exactly which mouse buttons are pressed ([] = none)", py::arg("pressed"))
            .def("mouse_wheel", [](Emulator& self, int steps) -> py::dict {
                DebugMouseManager& mgr = MouseManagerOrThrow(self);
                return MouseResultOrThrow(mgr, mgr.Wheel(steps));
            }, "Scroll the mouse wheel by steps (-7..7, + = away from user)", py::arg("steps"))
            .def("mouse_release_all", [](Emulator& self) -> py::dict {
                DebugMouseManager& mgr = MouseManagerOrThrow(self);
                return MouseResultOrThrow(mgr, mgr.ReleaseAllButtons());
            }, "Release all mouse buttons and cancel a pending click")
            .def("mouse_set_counters", [](Emulator& self, int x, int y) -> py::dict {
                DebugMouseManager& mgr = MouseManagerOrThrow(self);
                return MouseResultOrThrow(mgr, mgr.SetCounters(x, y));
            }, "Debug: write raw mouse X/Y counters (0..255)", py::arg("x"), py::arg("y"))
            .def("mouse_status", [](Emulator& self, const std::string& device) -> py::dict {
                DebugMouseManager& mgr = MouseManagerOrThrow(self);
                if (const MouseInjectResult check = mgr.CheckDevice(device); !check.ok())
                    throw py::value_error(check.message);
                MouseStateSnapshot state = mgr.GetState(device);
                if (!state.available)
                    throw std::runtime_error("Mouse device not available");
                py::dict d = MouseStateDict(state);
                // Routing mirrors GET /mouse/status: `present` alone cannot distinguish
                // "not fitted" from "fitted but shadowed" (mouse design Q4 / gap D-1)
                EmulatorContext* context = self.GetContext();
                if (context && context->pPortDecoder)
                {
                    bool decoded = false;
                    std::string note;
                    context->pPortDecoder->GetMouseRoutingState(decoded, note);
                    py::dict routing;
                    routing["ports_decoded"] = decoded;
                    routing["note"] = note;
                    d["routing"] = routing;
                }
                return d;
            }, "Get mouse counters, buttons, wheel, port values, port routing and the machine's mouse device",
               py::arg("device") = std::string())
            .def("mouse_click_pending", [](Emulator& self) -> bool {
                return MouseManagerOrThrow(self).IsClickPending();
            }, "True while a timed mouse click is still holding its button")
            .def("mouse_button_names", [](Emulator&) -> std::vector<std::string> {
                return DebugMouseManager::GetAllButtonNames();
            }, "List mouse button names")

            // -----------------------------------------------------------------
            // Kempston joystick injection (joystick TDD §5). Same contract as the mouse:
            // errors raise (ValueError for bad arguments, RuntimeError for TTD replay /
            // missing device), success returns the state dict (same keys as GET /joystick).
            // -----------------------------------------------------------------
            .def("joystick_press", [](Emulator& self, const py::object& buttons) -> py::dict {
                const std::string names = JoystickNamesOrThrow(buttons);
                DebugJoystickManager& mgr = JoystickManagerOrThrow(self);
                return JoystickResultOrThrow(mgr, mgr.Press(names));
            }, "Press and hold joystick buttons (up|down|left|right|fire|b5..b7; 'up+fire' or a list)", py::arg("buttons"))
            .def("joystick_release", [](Emulator& self, const py::object& buttons) -> py::dict {
                const std::string names = JoystickNamesOrThrow(buttons);
                DebugJoystickManager& mgr = JoystickManagerOrThrow(self);
                return JoystickResultOrThrow(mgr, mgr.Release(names));
            }, "Release joystick buttons", py::arg("buttons"))
            .def("joystick_set", [](Emulator& self, const py::object& state) -> py::dict {
                DebugJoystickManager& mgr = JoystickManagerOrThrow(self);
                if (py::isinstance<py::int_>(state))
                    return JoystickResultOrThrow(mgr, mgr.SetStateChecked(JoystickIntOrThrow(state, "state")));
                const std::string names = JoystickNamesOrThrow(state);
                if (names.empty())
                    return JoystickResultOrThrow(mgr, mgr.ReleaseAll());
                const uint8_t mask = DebugJoystickManager::ResolveButtonNames(names);
                if (mask == 0)  // the manager words the unknown-name error
                    return JoystickResultOrThrow(mgr, mgr.Press(names));
                return JoystickResultOrThrow(mgr, mgr.SetStateChecked(mask));
            }, "Set exactly the held joystick buttons: a byte 0..255 or a list / string of names ([] = none)",
               py::arg("state"))
            .def("joystick_tap", [](Emulator& self, const py::object& buttons, const py::object& frames) -> py::dict {
                const std::string names = JoystickNamesOrThrow(buttons);
                const long long count = frames.is_none() ? static_cast<long long>(DebugJoystickManager::DEFAULT_TAP_FRAMES)
                                                         : JoystickIntOrThrow(frames, "frames");
                DebugJoystickManager& mgr = JoystickManagerOrThrow(self);
                return JoystickResultOrThrow(mgr, mgr.TapChecked(names, count));
            }, "Press joystick buttons, hold for frames (default 2), release", py::arg("buttons"),
               py::arg("frames") = py::none())
            .def("joystick_state", [](Emulator& self) -> py::dict {
                DebugJoystickManager& mgr = JoystickManagerOrThrow(self);
                const JoystickStateSnapshot state = mgr.GetState();
                if (!state.available)
                    throw std::runtime_error("Joystick device not available");
                return JoystickStateDict(state);
            }, "Get the joystick byte, held buttons, the IN #1F value, wiring, host keys and a pending tap")
            .def("joystick_tap_pending", [](Emulator& self) -> bool {
                return JoystickManagerOrThrow(self).IsTapPending();
            }, "True while a timed joystick tap is still holding its buttons")
            .def("joystick_button_names", [](Emulator&) -> std::vector<std::string> {
                return DebugJoystickManager::GetAllButtonNames();
            }, "List joystick button names")

            // -----------------------------------------------------------------
            // TTD (Time-Travel Debug) bindings — Phase 2 surface
            // -----------------------------------------------------------------

            // Session status — returns a dict mirroring the WebAPI shape
            .def("ttd_status", [](Emulator& self) -> py::dict {
                py::dict info;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager)
                {
                    info["state"] = "idle";
                    info["ttd_available"] = false;
                    return info;
                }
                ttd::TimeTravelManager* mgr = ctx->pTimeTravelManager;
                ttd::TTDSessionInfo si = mgr->ReadSessionInfo();
                info["state"]                    = ttd::TTDSessionStateToString(si.state);
                info["session_start_frame"]      = py::cast(si.sessionStartFrame);
                info["current_end_frame"]        = py::cast(si.currentEndFrame);
                info["checkpoint_count"]         = py::cast(si.checkpointCount);
                info["page_store_bytes"]         = py::cast(si.pageStoreBytes);
                info["page_store_used_bytes"]    = py::cast(si.pageStoreUsedBytes);
                info["baseline_frames_captured"] = py::cast(si.baselineFramesCaptured);
                info["session_heap_bytes"]       = py::cast(si.sessionHeapBytes);
                info["history_limit_frames"]     = py::cast(si.historyLimitFrames);
                info["history_limit_bytes"]      = py::cast(si.historyLimitBytes);
                info["history_bytes"]            = py::cast(si.historyBytes);
                info["evicted_checkpoints"]      = py::cast(si.evictedCheckpoints);
                // Provenance and section sizes: "is this something I recorded
                // or something I opened, and what is inside it".
                info["loaded_from_file"]         = py::cast(si.loadedFromFile);
                info["source_path"]              = py::cast(si.sourcePath);
                info["captured_at_unix_ms"]      = py::cast(si.capturedAtUnixMs);
                info["model_id"]                 = py::cast(si.modelId);
                info["model_ram_pages"]          = py::cast(si.modelRamPages);
                info["write_journal_records"]    = py::cast(si.writeJournalRecords);
                info["write_journal_bytes"]      = py::cast(si.writeJournalBytes);
                info["coverage_index_frames"]    = py::cast(si.coverageIndexFrames);
                info["coverage_index_bytes"]     = py::cast(si.coverageIndexBytes);
                info["write_journal_enabled"]    = py::cast(si.writeJournalEnabled);
                info["write_journal_complete"]   = py::cast(si.writeJournalComplete);
                py::list segments;
                for (const auto& [from, to] : si.writeJournalSpans)
                {
                    py::dict span;
                    span["from_frame"]    = py::cast(from.frame);
                    span["from_tinframe"] = py::cast(from.tInFrame);
                    span["to_frame"]      = py::cast(to.frame);
                    span["to_tinframe"]   = py::cast(to.tInFrame);
                    segments.append(span);
                }
                info["write_journal_segments"] = segments;
                info["bookmark_count"]           = py::cast(static_cast<uint64_t>(si.bookmarkCount));
                info["input_event_count"]        = py::cast(static_cast<uint64_t>(si.inputEventCount));
                info["external_event_count"]     = py::cast(static_cast<uint64_t>(si.externalEventCount));
                info["input_history_complete"]   = py::cast(si.inputHistoryComplete);
                info["port_journal_active"]      = py::cast(si.portJournalActive);
                info["port_journal_off_reason"]  = si.portJournalOffReason.empty()
                                                       ? py::object(py::none())
                                                       : py::object(py::cast(si.portJournalOffReason));
                info["port_read_count"]          = py::cast(si.portReadCount);
                info["port_write_count"]         = py::cast(si.portWriteCount);
                info["port_journal_bytes"]       = py::cast(static_cast<uint64_t>(si.portJournalBytes));
                info["port_replay_value_mismatches"] = py::cast(si.portReplayValueMismatches);
                info["port_replay_divergences"]  = py::cast(si.portReplayDivergences);
                info["last_drop_reason"]         = si.lastDropReason.empty() ? py::object(py::none())
                                                                             : py::object(py::cast(si.lastDropReason));
                info["unavailable_reason"]       = si.unavailableReason.empty() ? py::object(py::none())
                                                                                : py::object(py::cast(si.unavailableReason));
                // The recorded machine (None while there is no session) and, for a
                // loaded file, the instance that recorded it
                info["machine"] = si.checkpointCount != 0 ? py::object(TtdRecordedMachineDict(si.machine))
                                                          : py::object(py::none());
                info["recorded_by"] = si.recordedBy.empty() ? py::object(py::none())
                                                            : py::object(py::cast(si.recordedBy));
                info["ttd_available"]            = true;
                return info;
            }, "Get TTD session status")
            .def("ttd_file_info", [](Emulator& /*self*/, const std::string& path) -> py::dict {
                py::dict r;
                ttd::TTDFileInfo fi;
                std::string err;
                if (!ttd::ReadTTDFileInfo(path, fi, err))
                {
                    r["ok"] = false;
                    r["path"] = path;
                    r["error"] = err;
                    return r;
                }
                r["ok"] = true;
                r["path"] = fi.path;
                r["file_bytes"] = py::cast(fi.fileBytes);
                r["schema_version"] = py::cast(static_cast<unsigned>(fi.schemaVersion));
                r["flags"] = py::cast(static_cast<unsigned>(fi.flags));
                r["captured_at_unix_ms"] = py::cast(fi.capturedAtUnixMs);
                r["recorded_by"] = fi.emulatorId.empty() ? py::object(py::none()) : py::object(py::cast(fi.emulatorId));
                r["session_state"] = py::cast(std::string(
                    ttd::TTDSessionStateToString(static_cast<ttd::TTDSessionState>(fi.sessionState))));
                r["session_start_frame"] = py::cast(fi.startFrame);
                r["session_end_frame"] = py::cast(fi.endFrame);
                r["checkpoint_count"] = py::cast(static_cast<uint64_t>(fi.checkpointCount));
                r["page_slot_count"] = py::cast(static_cast<uint64_t>(fi.pageStoreCount));
                py::dict sections;
                sections["write_journal"] = py::cast(fi.hasWriteJournal);
                sections["write_journal_complete"] = py::cast(fi.writeJournalComplete);
                sections["coverage_index"] = py::cast(fi.hasCoverageIndex);
                sections["bookmarks"] = py::cast(fi.hasBookmarks);
                sections["input_journal"] = py::cast(fi.hasInputJournal);
                sections["external_events"] = py::cast(fi.hasExternalEvents);
                sections["port_journals"] = py::cast(fi.hasPortJournals);
                sections["top_clock_time"] = py::cast(fi.topClockTime);
                r["sections"] = sections;
                r["machine"] = TtdRecordedMachineDict(fi.machine);
                r["peripherals_from_header"] = py::cast(fi.peripheralsFromHeader);
                return r;
            }, "Describe a .ttd file without loading it: header, sections and the recorded machine "
               "(model, ROM signature, General Sound card, devices)", py::arg("path"))

            // journal=True also records the write journal; without it the
            // ttd_set_journal_enabled choice stands (off by default, D40)
            .def("ttd_start", [](Emulator& self, py::object journalObj) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager) return false;
                if (!journalObj.is_none())
                    ctx->pTimeTravelManager->SetEnableWriteJournal(journalObj.cast<bool>());
                return ctx->pTimeTravelManager->StartRecording();
            }, "Start TTD recording (journal=True also records the write journal)",
               py::arg("journal") = py::none())

            .def("ttd_set_history_limit", [](Emulator& self, py::object framesObj, py::object bytesObj) -> py::tuple {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager)
                    throw std::runtime_error("TTD not available");
                const ttd::TTDSessionInfo si = ctx->pTimeTravelManager->ReadSessionInfo();
                ctx->pTimeTravelManager->SetHistoryLimit(
                    framesObj.is_none() ? si.historyLimitFrames : framesObj.cast<uint64_t>(),
                    bytesObj.is_none() ? si.historyLimitBytes : bytesObj.cast<uint64_t>());
                const ttd::TTDSessionInfo now = ctx->pTimeTravelManager->ReadSessionInfo();
                return py::make_tuple(now.historyLimitFrames, now.historyLimitBytes);
            }, "Bound the TTD history: while recording, the oldest frames are released beyond `frames` checkpoints "
               "or `bytes` of checkpoint data (0 = no limit, None keeps the current value). Returns (frames, bytes) in force",
               py::arg("frames") = py::none(), py::arg("bytes") = py::none())

            .def("ttd_set_journal_enabled", [](Emulator& self, bool enabled) {
                auto* ctx = self.GetContext();
                if (ctx && ctx->pTimeTravelManager && !ctx->pTimeTravelManager->SwitchWriteJournal(enabled))
                    throw std::runtime_error("write journal not available");
            }, "Switch the write journal at any moment, also while recording: a journal segment starts or ends there",
               py::arg("enabled"))

            .def("ttd_get_journal_enabled", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                return ctx && ctx->pTimeTravelManager && ctx->pTimeTravelManager->GetEnableWriteJournal();
            }, "Whether the write journal is recorded (off by default)")

            .def("ttd_build_journal", [](Emulator& self, py::object fromObj, py::object toObj) -> py::dict {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager)
                    throw std::runtime_error("TTD not available");
                const ttd::TTDJournalBuildResult b = ctx->pTimeTravelManager->BuildWriteJournalFrames(
                    fromObj.is_none() ? 0 : fromObj.cast<uint64_t>(),
                    toObj.is_none() ? UINT64_MAX : toObj.cast<uint64_t>());
                py::dict r;
                r["ok"] = b.ok;
                r["error"] = b.ok ? py::object(py::none()) : py::object(py::str(b.error));
                r["cancelled"] = b.cancelled;
                r["frames_built"] = b.framesBuilt;
                r["frames_covered"] = b.framesCovered;
                r["frames_refused"] = b.framesRefused;
                r["records"] = b.records;
                return r;
            }, "Build the write journal for frames from_frame..to_frame (default: the whole session) by replaying "
               "them, about 2-4 ms per frame; not while recording",
               py::arg("from_frame") = py::none(), py::arg("to_frame") = py::none())

            .def("ttd_stop", [](Emulator& self) {
                auto* ctx = self.GetContext();
                if (ctx && ctx->pTimeTravelManager)
                    ctx->pTimeTravelManager->StopRecording();
            }, "Stop TTD recording (history retained)")

            .def("ttd_invalidate", [](Emulator& self, const std::string& reason) {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager)
                    return;
                if (std::string refusal = ctx->pTimeTravelManager->RecordingGuard(ttd::TTDGuardedAction::Invalidate);
                    !refusal.empty())
                    throw std::runtime_error(refusal);  // recording: stop it first
                ctx->pTimeTravelManager->InvalidateSession(reason.c_str());
            }, "Drop all TTD history (RuntimeError while recording)", py::arg("reason") = "python invalidate")

            .def("ttd_seek", [](Emulator& self, uint64_t frame, uint32_t tInFrame) -> py::dict {
                py::dict result;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager)
                {
                    result["reached"] = false;
                    result["error"]   = "TTD not available";
                    return result;
                }
                ttd::TTDTimePoint target{frame, tInFrame};
                ttd::TimeTravelManager::TTDSeekResult r;
                bool reached = ctx->pTimeTravelManager->SeekTo(target, &r);
                result["reached"] = reached;

                py::dict arrivedAt;
                arrivedAt["frame"]    = py::cast(r.arrivedAt.frame);
                arrivedAt["tinframe"] = py::cast(r.arrivedAt.tInFrame);
                result["arrived_at"]  = arrivedAt;

                const char* reasonStr = "target";
                switch (r.haltReason)
                {
                    case ttd::TimeTravelManager::TTDSeekHaltReason::ExternalEvent: reasonStr = "external_event"; break;
                    case ttd::TimeTravelManager::TTDSeekHaltReason::OutOfRange:    reasonStr = "out_of_range"; break;
                    default: break;
                }
                result["halt_reason"] = reasonStr;

                if (r.haltReason == ttd::TimeTravelManager::TTDSeekHaltReason::ExternalEvent)
                {
                    py::dict marker;
                    marker["frame"]    = py::cast(r.blockingMarker.time.frame);
                    marker["tinframe"] = py::cast(r.blockingMarker.time.tInFrame);
                    marker["kind"]     = ttd::TTDExternalEventKindToString(r.blockingMarker.kind);
                    marker["reason"]   = r.blockingMarker.reason;
                    result["blocking_marker"] = marker;
                }
                return result;
            }, "Seek to a point in the timeline", py::arg("frame"), py::arg("tinframe") = 0)

            .def("ttd_step_back", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager) return false;
                return ctx->pTimeTravelManager->StepBackFrame();
            }, "Step back one frame")

            .def("ttd_step_forward", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager) return false;
                return ctx->pTimeTravelManager->StepForwardFrame();
            }, "Step forward one frame")

            .def("ttd_resume", [](Emulator& self, py::object frameObj, uint32_t tInFrame) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager) return false;
                // No frame: resume exactly where the machine stands (as CLI and WebAPI do)
                ttd::TTDTimePoint from = ctx->pTimeTravelManager->CurrentPosition();
                if (!frameObj.is_none())
                {
                    from.frame = frameObj.cast<uint64_t>();
                    from.tInFrame = tInFrame;
                }
                return ctx->pTimeTravelManager->ResumeRecordingFrom(from);
            }, "Resume recording from current or specified point",
               py::arg("frame") = py::none(), py::arg("tinframe") = 0)

            .def("ttd_position", [](Emulator& self) -> py::dict {
                py::dict result;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager)
                {
                    result["error"] = "TTD not available";
                    return result;
                }
                ttd::TTDTimePoint pos = ctx->pTimeTravelManager->CurrentPosition();
                ttd::TTDTimePoint end = ctx->pTimeTravelManager->SessionEndPosition();
                py::dict current;
                current["frame"]    = py::cast(pos.frame);
                current["tinframe"] = py::cast(pos.tInFrame);
                result["current"]   = current;
                py::dict sessionEnd;
                sessionEnd["frame"]    = py::cast(end.frame);
                sessionEnd["tinframe"] = py::cast(end.tInFrame);
                result["session_end"]  = sessionEnd;
                return result;
            }, "Get current TTD position")

            .def("ttd_markers", [](Emulator& self) -> py::list {
                py::list markers;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager) return markers;
                const auto& journal = ctx->pTimeTravelManager->GetExternalEvents();
                for (const auto& e : journal.SnapshotEvents())
                {
                    py::dict marker;
                    marker["frame"]    = py::cast(e.time.frame);
                    marker["tinframe"] = py::cast(e.time.tInFrame);
                    marker["kind"]     = ttd::TTDExternalEventKindToString(e.kind);
                    marker["reason"]   = e.reason;
                    markers.append(marker);
                }
                return markers;
            }, "List external-event markers (replay barriers)")

            // -------------------------------------------------------------
            // TD-4 — agent bookmarks (advisory annotations, never barriers).
            // Labels are keys: non-empty, at most 63 chars, unique per session.
            // -------------------------------------------------------------
            .def("ttd_bookmark_add", [](Emulator& self, const std::string& label,
                                         py::object frameObj, uint32_t tInFrame) -> py::dict {
                py::dict result;
                result["added"] = false;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager)
                {
                    result["error"] = "TTD not available";
                    return result;
                }

                // Position omitted → current position (mark here).
                ttd::TTDTimePoint time = ctx->pTimeTravelManager->CurrentPosition();
                if (!frameObj.is_none())
                {
                    time.frame    = frameObj.cast<uint64_t>();
                    time.tInFrame = tInFrame;
                }

                std::string err;
                if (!ctx->pTimeTravelManager->AddBookmark(time, label, &err))
                {
                    result["error"] = err;
                    return result;
                }
                result["added"]    = true;
                result["label"]    = label;
                result["frame"]    = py::cast(time.frame);
                result["tinframe"] = py::cast(time.tInFrame);
                return result;
            }, "Add an agent bookmark (advisory, never a replay barrier); omit frame to mark the current position",
               py::arg("label"), py::arg("frame") = py::none(), py::arg("tinframe") = 0)

            .def("ttd_bookmarks", [](Emulator& self) -> py::list {
                py::list bookmarks;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager) return bookmarks;
                for (const auto& bm : ctx->pTimeTravelManager->GetBookmarks())
                {
                    py::dict entry;
                    entry["frame"]    = py::cast(bm.time.frame);
                    entry["tinframe"] = py::cast(bm.time.tInFrame);
                    entry["label"]    = bm.label;
                    bookmarks.append(entry);
                }
                return bookmarks;
            }, "List agent bookmarks (time-sorted)")

            .def("ttd_bookmark_delete", [](Emulator& self, const std::string& label) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager) return false;
                return ctx->pTimeTravelManager->RemoveBookmark(label);
            }, "Delete an agent bookmark by label", py::arg("label"))

            // A bookmark seek IS a seek — identical result shape to ttd_seek
            // (plus the resolved label); a bookmark never halts anything.
            .def("ttd_seek_bookmark", [](Emulator& self, const std::string& label) -> py::dict {
                py::dict result;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager)
                {
                    result["reached"] = false;
                    result["error"]   = "TTD not available";
                    return result;
                }
                ttd::TimeTravelManager::TTDSeekResult r;
                std::string err;
                const bool reached = ctx->pTimeTravelManager->SeekToBookmark(label, &r, &err);
                result["reached"]  = reached;
                if (!err.empty())
                    result["error"] = err;

                py::dict arrivedAt;
                arrivedAt["frame"]    = py::cast(r.arrivedAt.frame);
                arrivedAt["tinframe"] = py::cast(r.arrivedAt.tInFrame);
                result["arrived_at"]  = arrivedAt;

                const char* reasonStr = "target";
                switch (r.haltReason)
                {
                    case ttd::TimeTravelManager::TTDSeekHaltReason::ExternalEvent: reasonStr = "external_event"; break;
                    case ttd::TimeTravelManager::TTDSeekHaltReason::OutOfRange:    reasonStr = "out_of_range"; break;
                    default: break;
                }
                result["halt_reason"] = reasonStr;
                if (r.haltReason == ttd::TimeTravelManager::TTDSeekHaltReason::ExternalEvent)
                {
                    py::dict marker;
                    marker["frame"]    = py::cast(r.blockingMarker.time.frame);
                    marker["tinframe"] = py::cast(r.blockingMarker.time.tInFrame);
                    marker["kind"]     = ttd::TTDExternalEventKindToString(r.blockingMarker.kind);
                    marker["reason"]   = std::string(r.blockingMarker.reason);
                    result["blocking_marker"] = marker;
                }
                result["bookmark"]    = label;
                return result;
            }, "Seek to an agent bookmark by label", py::arg("label"))

            // -------------------------------------------------------------
            // Phase 4 — Reverse search + dump + instruction step
            // -------------------------------------------------------------
            .def("ttd_dump", [](Emulator& self, const std::string& path) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager) return false;
                std::ofstream out(path, std::ios::binary);
                if (!out.is_open()) return false;
                std::string err;
                return ctx->pTimeTravelManager->SerializeSession(out, err);
            }, "Dump TTD session to .ttd file", py::arg("path"))

            // Loading refuses a session recorded on a different machine model:
            // a checkpoint is raw RAM pages plus a chipset snapshot, so it only
            // restores into an instance of the model it came from. Returns a
            // dict rather than a bool so the caller can show the reason.
            .def("ttd_load", [](Emulator& self, const std::string& path) -> py::dict {
                py::dict result;
                result["ok"] = false;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager)
                {
                    result["error"] = "TTD not available";
                    return result;
                }
                std::ifstream in(path, std::ios::binary);
                if (!in.is_open())
                {
                    result["error"] = "Cannot open file: " + path;
                    return result;
                }
                std::string err;
                ctx->pTimeTravelManager->SetSessionSourcePath(path);
                if (!ctx->pTimeTravelManager->DeserializeSession(in, err))
                {
                    result["error"] = err;
                    return result;
                }
                const ttd::TTDSessionInfo info = ctx->pTimeTravelManager->ReadSessionInfo();
                result["ok"] = true;
                result["checkpoint_count"] = static_cast<uint64_t>(info.checkpointCount);
                result["session_start_frame"] = info.sessionStartFrame;
                result["current_end_frame"] = info.currentEndFrame;
                return result;
            }, "Load a .ttd session for playback (seek to position the emulator)", py::arg("path"))

            .def("ttd_port_events", [](Emulator& self, const std::string& event, py::object argObj,
                                        py::kwargs options) -> py::dict {
                py::dict result;
                result["ok"] = false;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager)
                {
                    result["error"] = "TTD engine not available";
                    return result;
                }
                auto text = [](const py::handle& o) -> std::string {
                    if (py::isinstance<py::bool_>(o))
                        return o.cast<bool>() ? "true" : "false";
                    return py::str(o).cast<std::string>();
                };
                ttd::TTDPortQuery q;
                std::string err;
                if (!ttd::BuildPortEventQuery(event, argObj.is_none() ? std::string() : text(argObj), q, err))
                {
                    result["error"] = err;
                    return result;
                }
                std::string file;  // file=: a .ttd on disk, searched without loading it
                for (const auto& [key, value] : options)
                {
                    const std::string name = py::str(key).cast<std::string>();
                    if (name == "file")
                    {
                        file = text(value);
                        continue;
                    }
                    if (!ttd::ApplyPortQueryOption(q, name, text(value), err))
                    {
                        result["error"] = err;
                        return result;
                    }
                }
                const ttd::TTDPortSearchResult found = file.empty()
                                                           ? ctx->pTimeTravelManager->SearchPortEvents(q)
                                                           : ctx->pTimeTravelManager->SearchPortEventsInFile(file, q);
                if (!found.ok)
                {
                    result["error"] = found.error;
                    return result;
                }
                result["ok"] = true;
                result["direction"] = ttd::PortDirectionName(q.direction);
                result["count"] = py::cast(static_cast<uint64_t>(found.hits.size()));
                result["truncated"] = found.truncated;
                result["scanned"] = py::cast(found.scanned);
                py::list hits;
                for (const ttd::TTDPortHit& h : found.hits)
                {
                    py::dict hit;
                    hit["index"] = py::cast(h.index);
                    hit["frame"] = py::cast(h.record.frame);
                    hit["tinframe"] = py::cast(h.record.tInFrame);
                    hit["port"] = py::cast(h.record.port);
                    hit["value"] = py::cast(h.record.value);
                    hit["pc"] = py::cast(h.record.pc);
                    if (h.ayRegister >= 0)
                        hit["ay_register"] = py::cast(h.ayRegister);
                    hits.append(hit);
                }
                result["hits"] = hits;
                return result;
            }, "When did the program ...: search the port journals for an event (key, ear, ay-read, ay-write, "
               "ay-select, border, beeper, in, out) with an optional argument (a key name, an AY register) and "
               "options (limit, newest, from, to, port, port_mask, value, value_mask, match, trigger, ay_register; "
               "file= searches a .ttd on disk without loading it)",
               py::arg("event"), py::arg("arg") = py::none())

            .def("ttd_find_last", [](Emulator& self, py::object addrObj,
                                      const std::string& access,
                                      py::object valueObj,
                                      py::object pcFromObj,
                                      py::object pcToObj,
                                      py::object beforeFrameObj,
                                      uint32_t beforeTin,
                                      py::object physPageObj,
                                      py::object addrFromObj,
                                      py::object addrToObj) -> py::object {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager) return py::none();

                ttd::TTDSearchQuery q;
                if (!addrObj.is_none())
                {
                    q.addrFrom = q.addrTo = static_cast<uint16_t>(addrObj.cast<int>());
                }
                else
                {
                    if (!addrFromObj.is_none()) q.addrFrom = static_cast<uint16_t>(addrFromObj.cast<int>());
                    if (!addrToObj.is_none()) q.addrTo = static_cast<uint16_t>(addrToObj.cast<int>());
                }
                q.access = ttd::TTDAccessTypeFromString(access.c_str());

                if (!valueObj.is_none())
                {
                    q.value = static_cast<uint8_t>(valueObj.cast<int>());
                    q.hasValueFilter = true;
                }
                if (!pcFromObj.is_none())
                {
                    q.pcFrom = static_cast<uint16_t>(pcFromObj.cast<int>());
                    q.hasPcFilter = true;
                }
                if (!pcToObj.is_none())
                {
                    q.pcTo = static_cast<uint16_t>(pcToObj.cast<int>());
                    if (!q.hasPcFilter) q.hasPcFilter = true;
                }
                // Bank-aware search: pins the query to one physical RAM page.
                if (!physPageObj.is_none())
                {
                    const int page = physPageObj.cast<int>();
                    if (page < 0 || page > ttd::kPhysPageMax)
                        throw py::value_error("phys_page expects 0..255");
                    q.physPage = static_cast<ttd::PhysPage>(page);
                    q.hasPhysPageFilter = true;
                }

                if (!beforeFrameObj.is_none())
                    q.beforeGlobalT = ctx->pTimeTravelManager->GlobalT({beforeFrameObj.cast<uint64_t>(), beforeTin});

                ttd::TTDExternalEvent marker{};
                ttd::TTDSearchWindow window;
                auto result = ctx->pTimeTravelManager->FindLastAccess(q, &marker, &window);
                if (!result)
                {
                    if (marker.reason[0] == '\0')
                        return py::none();  // genuinely no match
                    // A replay barrier stopped the search before any match
                    py::dict blocked;
                    blocked["found"]           = false;
                    blocked["blocked"]         = true;
                    blocked["marker_frame"]    = py::cast(marker.time.frame);
                    blocked["marker_tinframe"] = py::cast(marker.time.tInFrame);
                    blocked["marker_kind"]     = ttd::TTDExternalEventKindToString(marker.kind);
                    blocked["marker_reason"]   = std::string(marker.reason);
                    // TD-8: the part of history the search examined
                    if (window.searched)
                    {
                        blocked["covered_from"]          = py::cast(window.from.frame);
                        blocked["covered_from_tinframe"] = py::cast(window.from.tInFrame);
                        blocked["covered_to"]            = py::cast(window.to.frame);
                        blocked["covered_to_tinframe"]   = py::cast(window.to.tInFrame);
                    }
                    return blocked;
                }

                py::dict r;
                r["found"]     = true;
                r["frame"]     = py::cast(result->time.frame);
                r["tinframe"]  = py::cast(result->time.tInFrame);
                r["pc"]        = py::cast(result->pc);
                r["value"]     = py::cast(result->value);
                // None = the access had no RAM page (ROM, cache, I/O)
                r["phys_page"] = result->physPage == ttd::kPhysPageNone ? py::object(py::none())
                                                                         : py::object(py::cast(result->physPage));
                r["access"]    = ttd::TTDAccessTypeToString(result->access);
                // TD-8: the part of history the search examined
                if (window.searched)
                {
                    r["covered_from"]          = py::cast(window.from.frame);
                    r["covered_from_tinframe"] = py::cast(window.from.tInFrame);
                    r["covered_to"]            = py::cast(window.to.frame);
                    r["covered_to_tinframe"]   = py::cast(window.to.tInFrame);
                }
                return r;
            }, "Reverse search: find last access at address or within address/PC range",
               py::arg("addr") = py::none(),
               py::arg("access") = "write",
               py::arg("value") = py::none(),
               py::arg("pc_from") = py::none(),
               py::arg("pc_to") = py::none(),
               py::arg("before_frame") = py::none(),
               py::arg("before_tin") = 0,
               py::arg("phys_page") = py::none(),
               py::arg("addr_from") = py::none(),
               py::arg("addr_to") = py::none())

            .def("ttd_step_instruction_back", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager) return false;
                return ctx->pTimeTravelManager->StepBackInstruction();
            }, "Step back one instruction")

            .def("ttd_step_instruction_forward", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager) return false;
                return ctx->pTimeTravelManager->StepForwardInstruction();
            }, "Step forward one instruction")

        // -----------------------------------------------------------------
        // Phase 4 reverse execution (multi-step + reverse-continue).
        // -----------------------------------------------------------------
            .def("ttd_reverse_step", [](Emulator& self, uint32_t count) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager) return false;
                return ctx->pTimeTravelManager->ReverseStepInstructions(count);
            }, "Step back N instructions (M1 boundaries)",
               py::arg("count") = 1)

            .def("ttd_reverse_step_tstates", [](Emulator& self, uint64_t tstates) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager) return false;
                return ctx->pTimeTravelManager->ReverseStepTStates(tstates);
            }, "Step back N t-states (lands at nearest M1 <= target)",
               py::arg("tstates"))

            .def("ttd_reverse_continue", [](Emulator& self, const std::vector<uint16_t>& pcs) -> py::object {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager) return py::none();
                auto r = ctx->pTimeTravelManager->ReverseContinue(pcs);
                const bool blocked = r.blockingMarker.reason[0] != '\0';
                if (!r.matched && !blocked)
                    return py::none();
                py::dict d;
                d["matched"]  = r.matched;
                d["pc"]       = r.pc;
                d["frame"]    = r.arrivedAt.frame;
                d["tinframe"] = r.arrivedAt.tInFrame;
                if (blocked)
                {
                    py::dict m;
                    m["kind"]     = ttd::TTDExternalEventKindToString(r.blockingMarker.kind);
                    m["reason"]   = std::string(r.blockingMarker.reason);
                    m["frame"]    = py::cast(r.blockingMarker.time.frame);
                    m["tinframe"] = py::cast(r.blockingMarker.time.tInFrame);
                    d["blocked_by_marker"] = m;
                }
                // TD-8: the part of history the search examined
                if (r.window.searched)
                {
                    d["covered_from"]          = py::cast(r.window.from.frame);
                    d["covered_from_tinframe"] = py::cast(r.window.from.tInFrame);
                    d["covered_to"]            = py::cast(r.window.to.frame);
                    d["covered_to_tinframe"]   = py::cast(r.window.to.tInFrame);
                }
                return d;
            }, "Run backward until any PC matches; returns dict or None",
               py::arg("pcs"))

            .def("ttd_coverage_probe", [](Emulator& self, uint64_t frame, const std::string& kindStr,
                                          uint16_t addrFrom, uint16_t addrTo, py::object pageObj) -> py::dict {
                py::dict d;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager)
                {
                    d["index_available"] = false;
                    d["touched"] = false;
                    return d;
                }
                ttd::TTDCoverageKind kind = ttd::TTDCoverageKind::Executed;
                ttd::TTDCoverageKindFromString(kindStr, kind);
                std::optional<ttd::PhysPage> physPage;
                if (!pageObj.is_none())
                {
                    const int page = pageObj.cast<int>();
                    if (page < 0 || page > ttd::kPhysPageMax)
                        throw py::value_error("phys_page expects 0..255");
                    physPage = static_cast<ttd::PhysPage>(page);
                }

                auto res = ctx->pTimeTravelManager->QueryCoverageProbe(frame, kind, addrFrom, addrTo, physPage);
                d["frame"] = res.frame;
                d["kind"] = ttd::TTDCoverageKindToString(res.kind);
                d["touched"] = res.touched;
                d["index_available"] = res.indexAvailable;
                return d;
            }, "Probe coverage for a frame and address range",
               py::arg("frame") = 0, py::arg("kind") = "executed", py::arg("addr_from") = 0, py::arg("addr_to") = 0xFFFF, py::arg("phys_page") = py::none())

            .def("ttd_coverage_scan", [](Emulator& self, uint64_t fromFrame, py::object toFrameObj,
                                         const std::string& kindStr, uint16_t addrFrom, uint16_t addrTo,
                                         py::object pageObj, size_t limit) -> py::dict {
                py::dict d;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager)
                {
                    d["index_available"] = false;
                    d["scanned_frames"] = 0;
                    d["matching_frames"] = 0;
                    d["frames"] = py::list();
                    return d;
                }
                auto* mgr = ctx->pTimeTravelManager;
                uint64_t toFrame = toFrameObj.is_none() ? mgr->ReadSessionInfo().currentEndFrame : toFrameObj.cast<uint64_t>();
                ttd::TTDCoverageKind kind = ttd::TTDCoverageKind::Executed;
                ttd::TTDCoverageKindFromString(kindStr, kind);
                std::optional<ttd::PhysPage> physPage;
                if (!pageObj.is_none())
                {
                    const int page = pageObj.cast<int>();
                    if (page < 0 || page > ttd::kPhysPageMax)
                        throw py::value_error("phys_page expects 0..255");
                    physPage = static_cast<ttd::PhysPage>(page);
                }

                auto res = mgr->QueryCoverageScan(fromFrame, toFrame, kind, addrFrom, addrTo, physPage, limit);
                d["kind"] = ttd::TTDCoverageKindToString(res.kind);
                d["scanned_frames"] = res.scannedFrames;
                d["matching_frames"] = res.matchingFrames;
                d["first_match"] = res.firstMatch;
                d["last_match"] = res.lastMatch;
                d["covered_from"] = res.coveredFrom;
                d["covered_to"] = res.coveredTo;
                d["truncated"] = res.truncated;
                d["index_available"] = res.indexAvailable;
                py::list frameList;
                for (uint64_t f : res.frames) frameList.append(f);
                d["frames"] = frameList;
                return d;
            }, "Scan frames in [fromFrame, toFrame] touching range",
               py::arg("from_frame") = 0, py::arg("to_frame") = py::none(), py::arg("kind") = "executed",
               py::arg("addr_from") = 0, py::arg("addr_to") = 0xFFFF, py::arg("phys_page") = py::none(), py::arg("limit") = 200)

            .def("ttd_coverage_summary", [](Emulator& self, uint64_t fromFrame, py::object toFrameObj,
                                            py::object kindObj, uint64_t bucketSize, size_t limit) -> py::dict {
                py::dict d;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager)
                {
                    d["index_available"] = false;
                    d["buckets"] = py::list();
                    return d;
                }
                auto* mgr = ctx->pTimeTravelManager;
                uint64_t toFrame = toFrameObj.is_none() ? mgr->ReadSessionInfo().currentEndFrame : toFrameObj.cast<uint64_t>();
                std::optional<ttd::TTDCoverageKind> optKind;
                if (!kindObj.is_none())
                {
                    ttd::TTDCoverageKind k;
                    if (ttd::TTDCoverageKindFromString(kindObj.cast<std::string>(), k)) optKind = k;
                }

                auto res = mgr->QueryCoverageSummary(fromFrame, toFrame, optKind, bucketSize, limit);
                d["from_frame"] = res.fromFrame;
                d["to_frame"] = res.toFrame;
                d["covered_from"] = res.coveredFrom;
                d["covered_to"] = res.coveredTo;
                d["bucket_size"] = res.bucketSize;
                d["bucket_count"] = res.bucketCount;
                d["index_available"] = res.indexAvailable;
                py::list bucketList;
                for (const auto& b : res.buckets)
                {
                    py::dict bObj;
                    bObj["frame_start"] = b.frameStart;
                    bObj["frame_end"] = b.frameEnd;
                    bObj["executed_distinct"] = b.executedDistinct;
                    bObj["written_distinct"] = b.writtenDistinct;
                    bObj["read_distinct"] = b.readDistinct;
                    bObj["has_keyframe"] = b.hasKeyframe;
                    bucketList.append(bObj);
                }
                d["buckets"] = bucketList;
                return d;
            }, "Activity heatmap over [fromFrame, toFrame]",
               py::arg("from_frame") = 0, py::arg("to_frame") = py::none(), py::arg("kind") = py::none(),
               py::arg("bucket_size") = 0, py::arg("limit") = 100);

        // ================================================================
        // Phase-2 analysis capabilities — parity with WebAPI/MCP/CLI/Lua:
        // step out, skip until, memory find, screen digest, beam, frame cost,
        // coverage, AY log, audio capture, assembler, label resolve, listings.
        // ================================================================

        emulatorClass.def("step_out", [](Emulator& self) -> py::dict {
            py::dict d;
            try
            {
                self.StepOut();
                Z80State* z80 = self.GetZ80State();
                if (z80)
                {
                    d["pc"] = z80->pc;
                    d["sp"] = z80->sp;
                }
                d["ok"] = true;
            }
            catch (const std::exception& e)
            {
                d["ok"] = false;
                d["error"] = e.what();
            }
            return d;
        }, "Step out of the current subroutine (emulation ends paused)")

        .def("skip_until", [](Emulator& self, py::object pcValue, unsigned maxTStates) -> py::dict {
            py::dict d;
            uint32_t target32 = 0;
            if (py::isinstance<std::string>(pcValue))
            {
                try { target32 = static_cast<uint32_t>(std::stoul(pcValue.cast<std::string>(), nullptr, 0)); }
                catch (...) { d["error"] = "invalid pc"; return d; }
            }
            else
            {
                target32 = static_cast<uint32_t>(pcValue.cast<long>());
            }

            if (target32 > 0xFFFF) { d["error"] = "pc out of 16-bit range"; return d; }
            const uint16_t target = static_cast<uint16_t>(target32);

            // Safety budget: default 100 frames of emulated time, hard cap 200 s
            EmulatorContext* context = self.GetContext();
            if (maxTStates == 0 && context)
                maxTStates = context->config.frame * 100;
            if (maxTStates == 0)
                maxTStates = 6988800;
            if (maxTStates > 700000000u)
                maxTStates = 700000000u;

            self.RunUntilCondition([target](const Z80State& state) { return state.pc == target; }, maxTStates);

            Z80State* z80 = self.GetZ80State();
            d["hit"] = z80 && z80->pc == target;
            d["max_tstates"] = maxTStates;
            if (z80)
            {
                d["pc"] = z80->pc;
                d["sp"] = z80->sp;
            }
            return d;
        }, "Fast-forward until PC reaches the target (breakpoints skipped)",
           py::arg("pc"), py::arg("max_tstates") = 0)

        .def("mem_find",
             [](Emulator& self, py::object patternValue, uint32_t start, py::object endValue, unsigned alignment,
                unsigned max, const std::string& space, const std::string& mask) -> py::object {
            std::vector<uint8_t> bytes;
            const bool number = py::isinstance<py::int_>(patternValue);
            const bool text = number || py::isinstance<py::str>(patternValue);
            if (!text)
                for (auto item : patternValue)
                    bytes.push_back(static_cast<uint8_t>(item.cast<int>() & 0xFF));
            MemorySearchRequest request;
            std::string message;
            const uint32_t end = endValue.is_none() ? 0xFFFFFFFFu : endValue.cast<uint32_t>();
            const std::string pattern = number ? MemorySearch::NumberPattern(patternValue.cast<uint64_t>())
                                        : text ? patternValue.cast<std::string>() : std::string();
            if (!MemorySearch::BuildRequest(pattern, text ? nullptr : &bytes,
                                            mask, space, start, end, max, alignment, request, message))
            {
                py::dict d;
                d["error"] = message;
                return std::move(d);
            }
            return StateNodeToPy(MemorySearch::ToState(request, MemorySearch::Search(self.GetContext(), request)));
        }, "Search memory for a byte pattern (MemorySearch): hex text with ?? / A? wildcards or a byte sequence; "
           "space 'cpu' (default), 'ram' (every RAM page) or a page ('ram5', 'rom2', 'cache0'); mask = hex bytes, "
           "1 bits must match. Matches: address (cpu) or page {kind, page} + offset, context_start, context "
           "(4 bytes before, the match, 4 after)",
           py::arg("pattern"), py::arg("start") = 0, py::arg("end") = py::none(), py::arg("alignment") = 1,
           py::arg("max") = 64, py::arg("space") = "", py::arg("mask") = "")

        .def("screen_digest",
             [](Emulator& self, py::object startValue, py::object endValue, bool includeBorder,
                const std::string& mode) -> py::dict {
            py::dict d;
            EmulatorContext* context = self.GetContext();
            if (!context || !context->pMemory || !context->pScreen)
            {
                d["error"] = "context not initialized";
                return d;
            }

            // One computation for every interface (ScreenDigestCompute): the same pages, surface and change tracking
            ScreenDigestQuery query;
            if (mode == "active")
                query.active = true;
            else if (mode != "default")
                throw py::value_error("mode must be 'default' or 'active'");
            if (!startValue.is_none() || !endValue.is_none())
            {
                query.range = true;
                query.start = startValue.is_none() ? 0x4000 : startValue.cast<uint16_t>();
                query.end = endValue.is_none() ? 0x7FFF : endValue.cast<uint16_t>();
                if (query.start > query.end)
                {
                    d["error"] = "start must be <= end";
                    return d;
                }
            }
            query.includeBorder = includeBorder;
            const ScreenDigestResult r = ScreenDigestCompute::Compute(context, query);
            if (!r.ok)
            {
                d["error"] = r.error;
                return d;
            }
            if (r.range)
            {
                d["range_start"] = r.start;
                d["range_end"] = r.end;
                d["range_digest"] = r.rangeDigest;
            }
            else if (r.deviceSurface)
            {
                // A picture outside the RAM pages (the Sprinter's video RAM)
                py::dict activeSurface;
                activeSurface["video_mode"] = r.videoMode;
                activeSurface["memory"] = r.surface.name;
                activeSurface["bytes"] = r.surface.bytes;
                activeSurface["digest"] = r.surface.digest;
                d["active_surface"] = activeSurface;
            }
            else
            {
                if (r.activeSurface)
                {
                    py::dict activeSurface;
                    activeSurface["video_mode"] = r.videoMode;
                    py::list pages;
                    for (uint16_t page : r.activePages)
                        pages.append(page);
                    activeSurface["pages"] = pages;
                    d["active_surface"] = activeSurface;
                }
                py::dict perBank;
                for (const auto& [page, digest] : r.banks)
                    perBank[py::int_(page)] = digest;
                d["banks"] = perBank;
            }
            if (r.includeBorder)
                d["border_color"] = r.border;
            d["combined"] = r.combined;
            d["frame"] = r.frame;
            d["algorithm"] = "fnv1a-64";
            d["changed"] = r.changed;
            d["previous_digest"] = r.previousDigest;
            if (r.previousFrame != 0)
                d["previous_digest_frame"] = r.previousFrame;
            return d;
        }, "Screen-area FNV-1a-64 digest (change detection without pixel transfer)",
           py::arg("start") = py::none(), py::arg("end") = py::none(), py::arg("include_border") = true,
           py::arg("mode") = "default")

        .def("ports_map", [](Emulator& self) -> py::dict {
            py::dict d;
            EmulatorContext* context = self.GetContext();
            if (!context || !context->pPortDecoder)
            {
                d["error"] = "context not initialized";
                return d;
            }

            auto portHex = [](uint16_t value) {
                char text[8];
                std::snprintf(text, sizeof(text), "0x%04X", value);
                return std::string(text);
            };

            d["model"] = Config::GetModelFullName(context->config.mem_model);

            py::list entries;
            for (const PortMapEntry& entry : context->pPortDecoder->getPortMapEntries())
            {
                py::dict item;
                item["port"] = portHex(entry.port);
                item["mask"] = portHex(entry.mask);
                item["match"] = portHex(entry.match);
                item["device"] = entry.device;
                item["gate"] = entry.gate ? py::object(py::str(entry.gate)) : py::object(py::none());

                // Tagged registry fields (P1-2): names from the core single
                // source - identical strings on WebAPI /ports, MCP and Lua
                py::list tagNames;
                for (const std::string& tagName : PortTagSetToStrings(entry.tags))
                    tagNames.append(tagName);
                item["tags"] = tagNames;  // empty list = untagged row
                const char* latchName = PagingLatchToString(entry.latch);
                item["latch"] = latchName ? py::object(py::str(latchName)) : py::object(py::none());
                if (latchName)
                {
                    // The latch's value now, and decoded (PortDecoder::ReadPagingLatch / DecodePagingLatch)
                    const uint32_t value = PortDecoder::ReadPagingLatch(entry.latch, context->emulatorState);
                    item["latch_value"] = value;
                    py::dict fields;
                    for (const DecodedLatchField& field :
                         DecodePagingLatch(entry.latch, value, context->config.mem_model, context->config.ramsize))
                    {
                        if (field.isBool)
                            fields[py::str(field.key)] = field.boolValue;
                        else
                            fields[py::str(field.key)] = field.intValue;
                    }
                    item["latch_fields"] = fields;
                }

                entries.append(item);
            }
            d["entries"] = entries;

            bool mouseDecoded = false;
            std::string mouseNote;
            context->pPortDecoder->GetMouseRoutingState(mouseDecoded, mouseNote);

            const CONFIG& config = context->config;
            const EmulatorState& state = context->emulatorState;
            py::dict live;
            live["trdos_active"] = (state.flags & (CF_TRDOS | CF_DOSPORTS)) != 0;
            live["mouse_ports_decoded"] = mouseDecoded;
            live["mouse_routing_note"] = mouseNote;
            const bool scorpion = (config.mem_model == MM_SCORP || config.mem_model == MM_PROFSCORP);
            if (scorpion)
                live["shadow_monitor_paged"] = (state.p1FFD & 0x02) != 0;
            else
                live["shadow_monitor_paged"] = py::none();  // latch does not exist on this model
            // Sprinter: the map / DOS / PN5 the port table is read with now (as WebAPI /ports)
            if (config.mem_model == MM_SPRINTER)
            {
                const StateNode sprinter = DeviceState::Sprinter(context);
                if (const StateNode* decoderNode = sprinter.find("decoder"))
                    live["sprinter_port_table"] = StateNodeToPy(*decoderNode);
            }
            d["live"] = live;
            return d;
        }, "Static port map: which devices answer which I/O ports on this model, "
           "under which gating conditions, plus the live routing flags")

        .def("paging_state", [](Emulator& self) -> py::dict {
            py::dict d;
            EmulatorContext* context = self.GetContext();
            if (!context || !context->pPortDecoder || !context->pMemory)
            {
                d["error"] = "context not initialized";
                return d;
            }

            auto hexByte = [](uint32_t value) {
                char text[8];
                std::snprintf(text, sizeof(text), "0x%02X", value);
                return std::string(text);
            };
            auto hexWord = [](uint16_t value) {
                char text[8];
                std::snprintf(text, sizeof(text), "0x%04X", value);
                return std::string(text);
            };

            const CONFIG& config = context->config;
            const EmulatorState& state = context->emulatorState;
            Memory& memory = *context->pMemory;
            PortDecoder* decoder = context->pPortDecoder;
            ROM* rom = context->pCore ? context->pCore->GetROM() : nullptr;

            d["model"] = Config::GetModelFullName(config.mem_model);
            if (IsProfiModel(config.mem_model))
            {
                // The board, its sync PROM and the keyboard on its connector (as GET /state/paging)
                d["profi_board"] = config.mem_model == MM_PROFI3 ? "v3" : "v5";
                d["profi_sync_prom"] = ProfiSyncPromName(
                    ProfiResolveSyncProm(static_cast<ProfiSyncProm>(config.profi_sync_prom), config.mem_model));
                d["profi_keyboard"] = ProfiKeyboardName(ProfiKeyboardInForce(context));
                // The hi-res clocks (design-hires.md): the CPU clock there (no turbo), the v5's ZQ3 and SB7
                d["profi_hires_cpu_hz"] = ProfiHiresCpuHz(config.mem_model == MM_PROFI, config.profi_zq3_mhz);
                d["profi_zq3_mhz"] = static_cast<int>(ProfiClampZq3(config.profi_zq3_mhz));
                d["profi_ay_clock"] = (config.mem_model == MM_PROFI && config.profi_ay_clock_new) ? "new" : "old";
            }
            d["paging_locked"] = (state.p7FFD & PORT_7FFD_LOCK) != 0;
            d["trdos_active"] = (state.flags & (CF_TRDOS | CF_DOSPORTS)) != 0;

            // Latches array
            py::list latches;
            for (const PortMapEntry& entry : decoder->GetPagingLatches(Tags(PortTag::Memory)))
            {
                py::dict latch;
                latch["port"] = hexWord(entry.port);
                latch["device"] = entry.device ? entry.device : "";
                latch["gate"] = entry.gate ? py::object(py::str(entry.gate)) : py::object(py::none());

                // Tag names + latch binding from the core single source -
                // identical strings on /state/paging, MCP, CLI and Lua
                py::list tagNames;
                for (const std::string& tagName : PortTagSetToStrings(entry.tags))
                    tagNames.append(tagName);
                latch["tags"] = tagNames;
                const char* latchName = PagingLatchToString(entry.latch);
                latch["latch"] = latchName ? py::object(py::str(latchName)) : py::object(py::none());

                uint32_t value = PortDecoder::ReadPagingLatch(entry.latch, state);
                latch["value"] = hexByte(value);

                // Decoded bits: core §5.1 dictionary (DecodePagingLatch) with
                // native ints/bools, matching /state/paging verbatim
                py::dict decoded;
                for (const DecodedLatchField& field : DecodePagingLatch(entry.latch, value, config.mem_model, config.ramsize))
                {
                    if (field.isBool)
                        decoded[field.key.c_str()] = field.boolValue;
                    else
                        decoded[field.key.c_str()] = field.intValue;
                }
                if (decoded.size() > 0)
                    latch["decoded"] = decoded;
                latches.append(latch);
            }
            d["latches"] = latches;

            // Banks array
            py::list banks;
            const char* ranges[] = {"0x0000-0x3FFF", "0x4000-0x7FFF", "0x8000-0xBFFF", "0xC000-0xFFFF"};
            for (int i = 0; i < 4; ++i)
            {
                py::dict bank;
                bank["bank"] = i;
                bank["address_range"] = ranges[i];

                if (i == 0 && memory.IsBank0ROM())
                {
                    bank["type"] = "ROM";
                    uint8_t romPage = memory.GetROMPage();
                    bank["page"] = static_cast<int>(romPage);
                    if (rom)
                    {
                        uint8_t* pagePtr = memory.ROMPageHostAddress(romPage);
                        if (pagePtr)
                        {
                            std::string sig = rom->CalculateSignature(pagePtr, 0x4000);
                            // GetROMTitle carries the "Unknown ROM, <digest>"
                            // fallback; role = core layout table (§5.2) - a
                            // role/name mismatch is the wrong-ROM signal
                            bank["name"] = rom->GetROMTitle(sig);
                            bank["signature"] = sig;
                        }
                        bank["role"] = rom->GetROMPageRole(romPage);
                    }
                }
                else
                {
                    bank["type"] = "RAM";
                    switch (i) {
                        case 0: bank["page"] = static_cast<int>(memory.GetRAMPageForBank0()); break;
                        case 1: bank["page"] = static_cast<int>(memory.GetRAMPageForBank1()); break;
                        case 2: bank["page"] = static_cast<int>(memory.GetRAMPageForBank2()); break;
                        case 3: bank["page"] = static_cast<int>(memory.GetRAMPageForBank3()); break;
                    }
                }
                // The CPU waits for the video logic there (Core::IsSlotContended)
                bank["contended"] = context->pCore && context->pCore->IsSlotContended(static_cast<uint8_t>(i));
                banks.append(bank);
            }

            // Sprinter: the PLD maps the windows - kind and physical page from DeviceState::SprinterPaging
            // (the /state/paging view), and the whole view as `sprinter`
            if (config.mem_model == MM_SPRINTER)
            {
                const StateNode sprinter = DeviceState::SprinterPaging(context);
                if (const StateNode* windows = sprinter.find("windows"))
                {
                    for (size_t i = 0; i < windows->items.size() && i < 4; i++)
                    {
                        py::dict bank = banks[i].cast<py::dict>();
                        if (const StateNode* kind = windows->items[i].find("kind"))
                            bank["type"] = kind->s;
                        if (const StateNode* page = windows->items[i].find("page"))
                            bank["page"] = page->i;
                    }
                }
                d["sprinter"] = StateNodeToPy(sprinter);
            }
            d["banks"] = banks;
            return d;
        }, "Tagged paging latches + bank table (P1-2 design)")

        // Beam and video debug translation (PLAN #42): the same DeviceState reports every interface returns
        .def("beam_position", [](Emulator& self) -> py::object {
            return StateNodeToPy(DeviceState::VideoBeam(self.GetContext()));
        }, "Raster beam position and zone at the current t-state, plus the layer pixel under it (layers)")
        .def("video_layout", [](Emulator& self) -> py::object {
            return StateNodeToPy(DeviceState::VideoLayout(self.GetContext()));
        }, "Current video mode: layers, beam windows, framebuffer placement")
        .def("video_pixel", [](Emulator& self, unsigned x, unsigned y, unsigned layer) -> py::object {
            return StateNodeToPy(DeviceState::VideoPixel(self.GetContext(), layer, x, y));
        }, "Memory, registers and palette cell behind a surface pixel", py::arg("x"), py::arg("y"), py::arg("layer") = 0)
        .def("video_pixel_at", [](Emulator& self, unsigned t) -> py::object {
            return StateNodeToPy(DeviceState::VideoPixelAtBeam(self.GetContext(), t));
        }, "The same for the point under the beam at a frame T-state (layer pixel or border)", py::arg("t"))
        .def("video_address", [](Emulator& self, unsigned page, unsigned offset) -> py::object {
            return StateNodeToPy(DeviceState::VideoAddress(self.GetContext(), page, offset));
        }, "Pixels a RAM byte (page, offset 0..0x3FFF) feeds", py::arg("page"), py::arg("offset"))
        .def("video_address_in", [](Emulator& self, const std::string& space, unsigned offset, unsigned page) -> py::object {
            return StateNodeToPy(DeviceState::VideoAddressIn(self.GetContext(), space, page, offset));
        }, "Pixels a byte of a space feeds: ram (page, offset), sprite_ram (attribute word, byte offset) or "
           "palette (16-bit cell, byte offset)", py::arg("space"), py::arg("offset"), py::arg("page") = 0)
        .def("video_address_z80", [](Emulator& self, unsigned address) -> py::object {
            return StateNodeToPy(DeviceState::VideoAddressZ80(self.GetContext(), address));
        }, "Pixels the byte at a Z80 address feeds (current paging)", py::arg("address"))
        .def("video_changes", [](Emulator& self, unsigned frames) -> py::object {
            return StateNodeToPy(DeviceState::VideoChanges(self.GetContext(), frames));
        }, py::arg("frames") = 2,
           "Video change log: per frame the latches at its start, every latch change (t, line, t_in_line, pc, changes 'old -> new') and palette / mode table write counts; frames 1 = the last completed frame, 2 = also the current one (paused)")
        .def("video_text", [](Emulator& self, unsigned layer) -> py::object {
            return StateNodeToPy(DeviceState::VideoText(self.GetContext(), layer));
        }, "Text grid of a text mode (ATM / ZX-Evo)", py::arg("layer") = 0)
        // Temporal effects (ZX DLSS de-flicker): status and switch, the TemporalStatus report every interface returns
        .def("video_temporal", [](Emulator& self) -> py::object {
            return StateNodeToPy(TemporalStatus::Report(self.GetContext()));
        }, "ZX DLSS de-flicker status: algorithm, active, video / audio delay, frame counters, timing, algorithms")
        .def("video_temporal_set", [](Emulator& self, const std::string& name) -> py::object {
            if (!TemporalStatus::Set(self.GetContext(), name))
            {
                StateNode error = StateNode::Object();
                error["ok"] = false;
                error["error"] = "Unknown temporal algorithm '" + name + "'. Valid: " + TemporalStatus::OfferedList() + ", off";
                return StateNodeToPy(error);
            }
            return StateNodeToPy(TemporalStatus::Report(self.GetContext()));
        }, "Switch the ZX DLSS de-flicker: an algorithm name, or \"\" / \"off\"; returns the new status", py::arg("name"))

        .def("frame_cost", [](Emulator& self) -> py::dict {
            py::dict d;
            EmulatorContext* context = self.GetContext();
            if (!context) { d["error"] = "no context"; return d; }

            const CONFIG& config = context->config;
            const EmulatorState& state = context->emulatorState;

            const uint64_t frameBudget = static_cast<uint64_t>(config.frame) * state.current_z80_frequency_multiplier;
            const uint64_t lastHalted = state.tstates_halted_last;
            const uint64_t lastActive = frameBudget > lastHalted ? frameBudget - lastHalted : 0;

            py::dict last;
            last["tstates_total"] = frameBudget;
            last["tstates_halted"] = lastHalted;
            last["tstates_active"] = lastActive;
            last["halted_percent"] = frameBudget ? lastHalted * 100.0 / frameBudget : 0.0;
            d["last"] = last;

            py::dict average;
            average["frames"] = static_cast<uint64_t>(state.frame_cost_frames);
            average["tstates_total"] = static_cast<uint64_t>(state.tstates_frame_total);
            average["tstates_halted"] = static_cast<uint64_t>(state.tstates_halted_total);
            average["tstates_active"] = static_cast<uint64_t>(state.tstates_frame_total - state.tstates_halted_total);
            d["average"] = average;
            return d;
        }, "Halt/active cost of the last frame plus session averages")

        .def("coverage_start", [](Emulator& self, bool keep) -> py::dict {
            py::dict d;
            auto* context = self.GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            CoverageAnalyzer* coverage = manager ? manager->getAnalyzer<CoverageAnalyzer>("coverage") : nullptr;
            if (!coverage || !manager) { d["error"] = "coverage analyzer not available"; return d; }

            if (!keep)
                coverage->clear();
            d["success"] = manager->activate("coverage");
            d["recording"] = coverage->isRecording();
            return d;
        }, "Start a coverage session", py::arg("keep") = false)

        .def("coverage_stop", [](Emulator& self) -> py::dict {
            py::dict d;
            auto* context = self.GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            if (!manager) { d["error"] = "analyzer manager not available"; return d; }

            d["success"] = manager->deactivate("coverage");
            return d;
        }, "Stop the coverage session (data kept for queries)")

        .def("coverage_status", [](Emulator& self, unsigned maxRanges) -> py::dict {
            py::dict d;
            auto* context = self.GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            CoverageAnalyzer* coverage = manager ? manager->getAnalyzer<CoverageAnalyzer>("coverage") : nullptr;
            if (!coverage || !manager) { d["error"] = "coverage analyzer not available"; return d; }

            const size_t executedCount = coverage->getExecutedCount();
            d["active"] = manager->isActive("coverage");
            d["recording"] = coverage->isRecording();
            d["executed_count"] = executedCount;
            d["coverage_percent"] = executedCount * 100.0 / 65536.0;
            d["instructions"] = static_cast<uint64_t>(coverage->getInstructionCount());

            py::list ranges;
            for (const auto& range : coverage->getExecutedRanges(maxRanges))
            {
                py::dict item;
                item["start"] = range.first;
                item["end"] = range.second;
                ranges.append(item);
            }
            d["ranges"] = ranges;
            return d;
        }, "Coverage summary with executed ranges", py::arg("max_ranges") = 100)

        .def("coverage_gaps", [](Emulator& self, uint16_t start, uint16_t end, unsigned max) -> py::dict {
            py::dict d;
            auto* context = self.GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            CoverageAnalyzer* coverage = manager ? manager->getAnalyzer<CoverageAnalyzer>("coverage") : nullptr;
            if (!coverage || !manager) { d["error"] = "coverage analyzer not available"; return d; }

            py::list gaps;
            for (const auto& gap : coverage->getGaps(start, end, max))
            {
                py::dict item;
                item["start"] = gap.first;
                item["end"] = gap.second;
                gaps.append(item);
            }
            d["gaps"] = gaps;
            d["count"] = py::len(gaps);
            return d;
        }, "Unexecuted address gaps within [start, end]",
           py::arg("start") = 0x4000, py::arg("end") = 0xFFFF, py::arg("max") = 100)

        .def("ay_log_start", [](Emulator& self, unsigned capacity) -> py::dict {
            py::dict d;
            auto* context = self.GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            AYLogAnalyzer* aylog = manager ? manager->getAnalyzer<AYLogAnalyzer>("aylog") : nullptr;
            if (!aylog || !manager) { d["error"] = "AY log analyzer not available"; return d; }

            d["success"] = manager->activate("aylog");
            aylog->setCapacity(capacity);
            d["capacity"] = static_cast<uint64_t>(aylog->getCapacity());
            return d;
        }, "Start AY register-write logging", py::arg("capacity") = 4096)

        .def("ay_log_stop", [](Emulator& self) -> py::dict {
            py::dict d;
            auto* context = self.GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            if (!manager) { d["error"] = "analyzer manager not available"; return d; }

            d["success"] = manager->deactivate("aylog");
            return d;
        }, "Stop AY logging (records kept for queries)")

        .def("ay_log_status", [](Emulator& self) -> py::dict {
            py::dict d;
            auto* context = self.GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            AYLogAnalyzer* aylog = manager ? manager->getAnalyzer<AYLogAnalyzer>("aylog") : nullptr;
            if (!aylog || !manager) { d["error"] = "AY log analyzer not available"; return d; }

            d["active"] = manager->isActive("aylog");
            d["recording"] = aylog->isRecording();
            d["entry_count"] = static_cast<uint64_t>(aylog->getEntryCount());
            d["capacity"] = static_cast<uint64_t>(aylog->getCapacity());
            d["dropped"] = static_cast<uint64_t>(aylog->getDroppedCount());
            return d;
        }, "AY log session status")

        .def("ay_log_dump", [](Emulator& self, unsigned count, py::object offsetValue) -> py::dict {
            py::dict d;
            auto* context = self.GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            AYLogAnalyzer* aylog = manager ? manager->getAnalyzer<AYLogAnalyzer>("aylog") : nullptr;
            if (!aylog || !manager) { d["error"] = "AY log analyzer not available"; return d; }

            const size_t total = aylog->getEntryCount();
            const size_t offset = offsetValue.is_none() ?
                                      (total > count ? total - count : 0) :
                                      offsetValue.cast<size_t>();

            py::list records;
            for (const auto& record : aylog->getEntries(offset, count))
            {
                py::dict item;
                item["frame"] = static_cast<uint64_t>(record.frame);
                item["tacts"] = record.tacts;
                item["pc"] = record.pc;
                item["port"] = record.port;
                item["chip"] = record.chip;
                item["reg"] = record.reg;
                item["value"] = record.value;
                item["type"] = record.port == 0xFFFD ? (record.value > 0x0F ? "switch" : "select") : "write";
                records.append(item);
            }
            d["records"] = records;
            d["total"] = static_cast<uint64_t>(total);
            return d;
        }, "Dump AY log records (latest by default)",
           py::arg("count") = 20, py::arg("offset") = py::none())

        .def("audio_mixer", [](Emulator& self) -> py::object {
            return StateNodeToPy(DeviceState::AudioMixer(self.GetContext()));
        }, "Per-device mixer: master (muted, rate) and devices (source key, name, muted, solo, audible, volume, gain_db, peak, active, capturable)")
        .def("audio_mixer_set", [](Emulator& self, const std::string& source, py::object muted, py::object solo, py::object volume, py::object gainDb) -> py::object {
            auto text = [](const py::object& value) -> std::string {
                if (value.is_none())
                    return std::string();
                if (py::isinstance<py::bool_>(value))
                    return value.cast<bool>() ? "1" : "0";
                return py::str(value);
            };
            AudioMixer::Change change;
            std::string error;
            if (!AudioMixer::ChangeFromStrings(text(muted), text(solo), text(volume), text(gainDb), change, error) ||
                !AudioMixer::Apply(self.GetContext(), source, change, error))
                throw py::value_error(error);
            return StateNodeToPy(DeviceState::AudioMixer(self.GetContext()));
        }, py::arg("source"), py::arg("muted") = py::none(), py::arg("solo") = py::none(), py::arg("volume") = py::none(),
           py::arg("gain_db") = py::none(), "Set one mixer device (source key from audio_mixer(), or 'master' for muted): returns the mixer")
        .def("audio_capture_start", [](Emulator& self, double seconds, const std::string& sourceKey) -> py::dict {
            py::dict d;
            auto* context = self.GetContext();
            if (!context || !context->pDebugManager) { d["error"] = "debug manager not available"; return d; }

            if (seconds < 0.01 || seconds > 30.0)
            {
                d["error"] = "seconds must be within [0.01, 30.0]";
                return d;
            }

            AnalyzerManager* manager = context->pDebugManager->GetAnalyzerManager();
            AudioCaptureAnalyzer* capture =
                manager ? manager->getAnalyzer<AudioCaptureAnalyzer>("audiocapture") : nullptr;
            if (!capture || !manager) { d["error"] = "audio capture analyzer not available"; return d; }

            AudioSourceType source = AudioSourceType::MasterMix;
            std::string sourceError;
            if (!AudioMixer::Capturable(context, sourceKey, source, sourceError))
                throw py::value_error(sourceError);

            const size_t rate = context->pSoundManager ? context->pSoundManager->getCoreRate() : 44100;
            const size_t target = static_cast<size_t>(seconds * static_cast<double>(rate)) * 2;

            manager->activate("audiocapture");
            capture->startCapture(target, source);

            d["armed"] = capture->isCaptureArmed();
            d["source"] = AudioMixer::Key(source);
            d["target_samples"] = static_cast<uint64_t>(target);
            d["sample_rate"] = static_cast<uint64_t>(rate);
            return d;
        }, "Arm a buffered stereo capture (source: a mixer key - beeper, ay1, covox, gs ... - for one device's own buffer; default the master mix)",
           py::arg("seconds") = 1.0, py::arg("source") = "")

        .def("set_speed", [](Emulator& self, int multiplier) -> bool {
            if (multiplier != 1 && multiplier != 2 && multiplier != 4 && multiplier != 8 && multiplier != 16)
                throw py::value_error("speed must be 1, 2, 4, 8 or 16");
            return self.SetSpeedMultiplier(static_cast<uint8_t>(multiplier));
        }, py::arg("multiplier"),
           "Host speed multiplier (1, 2, 4, 8, 16); False while TTD records")

        .def("get_speed", [](Emulator& self) -> py::dict {
            py::dict d;
            auto* context = self.GetContext();
            if (!context || !context->pCore) { d["error"] = "core not available"; return d; }
            FeatureManager* fm = self.GetFeatureManager();
            d["multiplier"]   = context->pCore->GetHostSpeedMultiplier();
            d["effective"]    = context->pCore->GetSpeedMultiplier();  // with the machine's hardware turbo
            d["turbo_mode"]   = fm && fm->isEnabled(Features::kTurboMode);
            d["turbo_active"] = context->config.turbo_mode;
            d["turbo_audio"]  = context->config.turbo_mode_audio;
            return d;
        }, "Host speed multiplier, effective multiplier and turbo state")

        // Core audio rate control (same switch as CLI 'setting audio_rate',
        // WebAPI PUT settings/audio_rate and Lua set_audio_rate). Pin the
        // rate (44100..192000) for this run - never persisted to the ini;
        // 0 = auto (follow the priority chain: device > [SOUND] CoreRate >
        // 44100). Applied at the next frame boundary; deferred while a
        // recording is in progress. Returns False for unsupported rates.
        .def("set_audio_rate", [](Emulator& self, int rate) -> bool {
            auto* context = self.GetContext();
            if (!context || !context->pSoundManager) return false;
            context->pSoundManager->setCoreRatePin(static_cast<uint32_t>(rate));
            return context->pSoundManager->getCoreRatePin() == static_cast<uint32_t>(rate);
        }, py::arg("rate"),
           "Pin the core audio rate (44100..192000 Hz); 0 releases the pin (auto)")

        .def("get_audio_rate", [](Emulator& self) -> py::dict {
            py::dict d;
            auto* context = self.GetContext();
            if (!context || !context->pSoundManager) { d["error"] = "sound manager not available"; return d; }
            SoundManager* sound = context->pSoundManager;
            d["pin"] = sound->getCoreRatePin();  // 0 = auto
            d["core_rate"] = static_cast<uint64_t>(sound->getCoreRate());
            d["target_rate"] = static_cast<uint64_t>(sound->getTargetCoreRate());
            return d;
        }, "Core audio rate state: pin (0 = auto), effective core_rate, target_rate")

        // Sound character (same source as CLI 'setting ay_voicing|ay_punch|
        // ay_room|beeper_punch', WebAPI PUT settings/<name> and Lua
        // set_sound_character): runtime only, applied at the next frame boundary
        .def("get_sound_character", [](Emulator& self) -> py::dict {
            py::dict d;
            auto* context = self.GetContext();
            if (!context || !context->pSoundManager) { d["error"] = "sound manager not available"; return d; }
            for (const SoundCharacterSettings::Descriptor& desc : SoundCharacterSettings::Descriptors())
                d[desc.name] = SoundCharacterSettings::Get(*context->pSoundManager, desc.name);
            return d;
        }, "Sound character settings as text: ay_voicing, ay_punch, ay_room, beeper_punch")

        .def("set_sound_character", [](Emulator& self, const std::string& name, const std::string& value) -> py::dict {
            py::dict d;
            d["ok"] = false;
            auto* context = self.GetContext();
            if (!context || !context->pSoundManager) { d["error"] = "sound manager not available"; return d; }
            std::string error;
            if (!SoundCharacterSettings::Set(*context->pSoundManager, name, value, error))
            {
                d["error"] = error;
                return d;
            }
            d["ok"] = true;
            d["value"] = SoundCharacterSettings::Get(*context->pSoundManager, name);
            return d;
        }, py::arg("name"), py::arg("value"),
           "Set ay_voicing (headphones|classic|flat|warm|tv|small_speaker), ay_punch (on|off), ay_room (off|15db..1db) or beeper_punch (on|off)")

        .def("audio_capture_status", [](Emulator& self) -> py::dict {
            py::dict d;
            auto* context = self.GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            AudioCaptureAnalyzer* capture =
                manager ? manager->getAnalyzer<AudioCaptureAnalyzer>("audiocapture") : nullptr;
            if (!capture || !manager) { d["error"] = "audio capture analyzer not available"; return d; }

            d["armed"] = capture->isCaptureArmed();
            d["complete"] = capture->isCaptureComplete();
            d["captured_samples"] = static_cast<uint64_t>(capture->getCapturedSamples());
            d["target_samples"] = static_cast<uint64_t>(capture->getTargetSamples());
            return d;
        }, "Audio capture progress")

        .def("audio_capture_result", [](Emulator& self, py::object pathValue) -> py::dict {
            py::dict d;
            auto* context = self.GetContext();
            if (!context || !context->pDebugManager) { d["error"] = "debug manager not available"; return d; }

            AnalyzerManager* manager = context->pDebugManager->GetAnalyzerManager();
            AudioCaptureAnalyzer* capture =
                manager ? manager->getAnalyzer<AudioCaptureAnalyzer>("audiocapture") : nullptr;
            if (!capture || !manager) { d["error"] = "audio capture analyzer not available"; return d; }

            const auto& buffer = capture->getBuffer();
            const size_t frames = buffer.size() / 2;
            if (frames == 0)
            {
                d["error"] = "no captured audio — call audio_capture_start first";
                return d;
            }

            const size_t rate = context->pSoundManager ? context->pSoundManager->getCoreRate() : 44100;

            double peak[2] = {0.0, 0.0};
            double sumSquares[2] = {0.0, 0.0};
            for (size_t frame = 0; frame < frames; frame++)
            {
                for (int channel = 0; channel < 2; channel++)
                {
                    const double normalized = static_cast<double>(buffer[frame * 2 + channel]) / 32768.0;
                    const double magnitude = std::fabs(normalized);
                    if (magnitude > peak[channel])
                        peak[channel] = magnitude;
                    sumSquares[channel] += normalized * normalized;
                }
            }

            d["frames"] = static_cast<uint64_t>(frames);
            d["sample_rate"] = static_cast<uint64_t>(rate);
            d["duration_seconds"] = static_cast<double>(frames) / rate;
            d["complete"] = capture->isCaptureComplete();
            d["left_peak"] = peak[0];
            d["left_rms"] = std::sqrt(sumSquares[0] / frames);
            d["right_peak"] = peak[1];
            d["right_rms"] = std::sqrt(sumSquares[1] / frames);

            if (!pathValue.is_none())
            {
                const std::string path = pathValue.cast<std::string>();
                TinyWav wav{};
                if (tinywav_open_write(&wav, 2, static_cast<int32_t>(rate), TW_INT16, TW_INTERLEAVED,
                                       path.c_str()) == 0)
                {
                    tinywav_write_i(&wav, const_cast<void*>(static_cast<const void*>(buffer.data())),
                                    static_cast<int>(frames));
                    tinywav_close_write(&wav);
                    d["saved"] = path;
                }
                else
                {
                    d["save_error"] = "failed to open wav";
                }
            }
            return d;
        }, "Capture stats (peak/RMS per channel) with optional WAV export",
           py::arg("path") = py::none())

        .def("video_record", [](Emulator& self, const std::string& action, py::object optsValue) -> py::dict {
#ifdef ENABLE_RECORDING
            py::dict d;
            auto* context = self.GetContext();
            RecordingManager* rm = context ? context->pRecordingManager : nullptr;
            if (!rm) { d["error"] = "recording manager not available"; return d; }

            if (action == "start")
            {
                if (rm->IsRecording() || rm->IsPaused())
                {
                    d["error"] = "a recording is already active — stop it first";
                    return d;
                }

                std::string format = "gif";
                std::string filename;
                float fps = 50.0f;
                int scale = 1;
                std::string region = "full";
                std::string audio;
                long videoBitrate = 0;
                long audioBitrate = 0;
                if (py::isinstance<py::dict>(optsValue))
                {
                    py::dict opts = optsValue;
                    // Optional sound track: "aac" (or True = aac); None/False = video only
                    if (opts.contains("audio") && !opts["audio"].is_none())
                    {
                        if (py::isinstance<py::bool_>(opts["audio"]))
                            audio = opts["audio"].cast<bool>() ? "aac" : "";
                        else if (py::isinstance<py::str>(opts["audio"]))
                            audio = RecordingRequest::NormalizeAudioCodec(opts["audio"].cast<std::string>());
                        else
                        {
                            d["error"] = "audio must be a codec name (aac, mp3, opus, vorbis, flac, pcm_s16le) or a bool";
                            return d;
                        }
                    }
                    if (opts.contains("video_bitrate"))
                        videoBitrate = opts["video_bitrate"].cast<long>();
                    if (opts.contains("audio_bitrate"))
                        audioBitrate = opts["audio_bitrate"].cast<long>();
                    if (opts.contains("format") && py::isinstance<py::str>(opts["format"]))
                        format = opts["format"].cast<std::string>();
                    if (opts.contains("filename") && py::isinstance<py::str>(opts["filename"]))
                        filename = opts["filename"].cast<std::string>();
                    if (opts.contains("fps"))
                        fps = opts["fps"].cast<float>();
                    if (opts.contains("scale"))
                        scale = opts["scale"].cast<int>();
                    if (opts.contains("region") && py::isinstance<py::str>(opts["region"]))
                        region = opts["region"].cast<std::string>();
                }

                std::string extension = format;
                if (format == "h264" || format == "h265" || format == "hevc" || format == "vp9")
                    extension = "mp4";
                else if (format == "rawvideo")
                    extension = "avi";

                if (filename.empty())
                {
                    std::filesystem::path dir = std::filesystem::temp_directory_path() / "unreal-python";
                    std::error_code ec;
                    std::filesystem::create_directories(dir, ec);
                    static std::atomic<unsigned> counter{0};
                    const long long stamp =
                        static_cast<long long>(std::time(nullptr)) * 1000 + (counter++ % 1000);
                    filename = (dir / ("video-" + std::to_string(stamp) + "." + extension)).string();
                }

                // Same rules as the WebAPI/CLI/Lua (RecordingRequest): the codec must fit the container
                if (videoBitrate < 0 || audioBitrate < 0 || videoBitrate > 1000000 || audioBitrate > 1000000)
                {
                    d["error"] = "video_bitrate / audio_bitrate must be >= 0 (kbps)";
                    return d;
                }
                {
                    std::string codecError = RecordingRequest::ValidateAudio(format, filename, audio);
                    if (codecError.empty())
                        codecError = RecordingRequest::ValidateBitrates(static_cast<uint32_t>(videoBitrate),
                                                                        static_cast<uint32_t>(audioBitrate), audio);
                    if (!codecError.empty())
                    {
                        d["error"] = codecError;
                        return d;
                    }
                }

                if (fps < 1.0f) fps = 1.0f;
                if (fps > 100.0f) fps = 100.0f;
                rm->SetVideoFrameRate(fps);

                if (scale < 1) scale = 1;
                if (scale > 4) scale = 4;
                rm->SetScaleFactor(static_cast<uint32_t>(scale));

                rm->SetCaptureRegion((region == "screen" || region == "main")
                                         ? VideoCaptureRegion::MainScreen
                                         : VideoCaptureRegion::FullFrame);

                FeatureManager* fm = context->pFeatureManager;
                const bool featureWasOff = fm && !fm->isEnabled(Features::kRecording);
                if (featureWasOff) fm->setFeature(Features::kRecording, true);

                const bool wasRunning = self.IsRunning() && !self.IsPaused();
                if (wasRunning) self.Pause();

                const bool started = rm->StartRecording(filename, format, audio, static_cast<uint32_t>(videoBitrate),
                                                        static_cast<uint32_t>(audioBitrate));

                if (wasRunning) self.Resume();

                if (!started)
                {
                    if (featureWasOff) fm->setFeature(Features::kRecording, false);
                    d["error"] = "recording start failed";
                    d["message"] = rm->GetLastRecordingError();
                    return d;
                }

                d["recording"] = true;
                d["format"] = format;
                d["fps"] = fps;
                d["scale"] = scale;
                d["region"] = region;
                d["audio"] = rm->HasAudio();
                d["audio_codec"] = audio;
                d["audio_sample_rate"] = rm->HasAudio() ? rm->GetAudioSampleRate() : 0u;
                d["audio_channels"] = rm->HasAudio() ? rm->GetAudioChannels() : 0u;
                d["feature_auto_enabled"] = featureWasOff;
                d["output"] = filename;
                return d;
            }

            if (action == "stop")
            {
                if (!rm->IsRecording() && !rm->IsPaused())
                {
                    d["error"] = "no active recording to stop";
                    return d;
                }
                rm->StopRecording();
            }
            else if (action == "pause")
            {
                if (!rm->IsRecording())
                {
                    d["error"] = "no active recording to pause";
                    return d;
                }
                rm->PauseRecording();
            }
            else if (action == "resume")
            {
                if (!rm->IsPaused())
                {
                    d["error"] = "recording is not paused";
                    return d;
                }
                rm->ResumeRecording();
            }
            else
            {
                d["error"] = "unknown action '" + action + "' (expected start|stop|pause|resume)";
                return d;
            }

            const RecordingManager::RecordingStats stats = rm->GetStats();
            d["recording"] = rm->IsRecording();
            d["paused"] = rm->IsPaused();
            d["frames_recorded"] = static_cast<uint64_t>(stats.framesRecorded);
            d["recorded_duration"] = stats.recordedDuration;
            d["emulated_duration"] = stats.emulatedDuration;
            d["output_file_size"] = static_cast<uint64_t>(stats.outputFileSize);
            d["average_frame_time_ms"] = stats.averageFrameTime;
            d["recent_fps"] = stats.recentFps;
            d["audio_samples_recorded"] = static_cast<uint64_t>(stats.audioSamplesRecorded);
            d["video_codec"] = rm->GetVideoCodec();
            d["audio"] = rm->HasAudio();
            d["audio_codec"] = rm->HasAudio() ? rm->GetAudioCodec() : std::string();
            d["audio_sample_rate"] = rm->HasAudio() ? rm->GetAudioSampleRate() : 0u;
            d["audio_channels"] = rm->HasAudio() ? rm->GetAudioChannels() : 0u;
            d["audio_duration"] = rm->HasAudio() ? rm->GetAudioDuration() : 0.0;
            d["output"] = rm->GetOutputFilename();
            return d;
#else
            (void)self;
            (void)action;
            (void)optsValue;
            py::dict d;
            d["error"] = "recording support is disabled in this build (ENABLE_RECORDING=OFF)";
            return d;
#endif
        }, "Video recording control (action=start|stop|pause|resume)",
           py::arg("action"), py::arg("opts") = py::none())

        .def("video_record_status", [](Emulator& self) -> py::dict {
#ifdef ENABLE_RECORDING
            py::dict d;
            auto* context = self.GetContext();
            RecordingManager* rm = context ? context->pRecordingManager : nullptr;
            if (!rm) { d["error"] = "recording manager not available"; return d; }

            d["recording"] = rm->IsRecording();
            d["paused"] = rm->IsPaused();
            d["feature_enabled"] = rm->isFeatureEnabled();
            d["realtime_capable"] = rm->IsRealtimeCapable();
            if (!rm->GetLastRecordingError().empty())
                d["last_error"] = rm->GetLastRecordingError();

            const RecordingManager::RecordingStats stats = rm->GetStats();
            d["frames_recorded"] = static_cast<uint64_t>(stats.framesRecorded);
            d["recorded_duration"] = stats.recordedDuration;
            d["emulated_duration"] = stats.emulatedDuration;
            d["output_file_size"] = static_cast<uint64_t>(stats.outputFileSize);
            d["average_frame_time_ms"] = stats.averageFrameTime;
            d["recent_fps"] = stats.recentFps;
            d["audio_samples_recorded"] = static_cast<uint64_t>(stats.audioSamplesRecorded);
            d["video_codec"] = rm->GetVideoCodec();
            d["audio"] = rm->HasAudio();
            d["audio_codec"] = rm->HasAudio() ? rm->GetAudioCodec() : std::string();
            d["audio_sample_rate"] = rm->HasAudio() ? rm->GetAudioSampleRate() : 0u;
            d["audio_channels"] = rm->HasAudio() ? rm->GetAudioChannels() : 0u;
            d["audio_duration"] = rm->HasAudio() ? rm->GetAudioDuration() : 0.0;
            d["output"] = rm->GetOutputFilename();
            return d;
#else
            (void)self;
            py::dict d;
            d["error"] = "recording support is disabled in this build (ENABLE_RECORDING=OFF)";
            return d;
#endif
        }, "Current recording state and live statistics")

        .def("assemble", [](Emulator& self, const std::string& code, py::object addressValue, bool write) -> py::dict {
            py::dict d;
            uint16_t address = 0;
            if (py::isinstance<std::string>(addressValue))
            {
                try { address = static_cast<uint16_t>(std::stoul(addressValue.cast<std::string>(), nullptr, 0)); }
                catch (...) { d["error"] = "invalid address"; return d; }
            }
            else
            {
                address = static_cast<uint16_t>(addressValue.cast<long>() & 0xFFFF);
            }

            Z80TextAssembler assembler;
            AsmResult asmResult = assembler.Assemble(code, address);

            d["ok"] = asmResult.ok;
            if (!asmResult.ok)
            {
                d["error"] = asmResult.error.message;
                d["error_line"] = asmResult.error.line;
                return d;
            }

            if (write)
            {
                Memory* memory = self.GetMemory();
                if (memory)
                {
                    self.EditMemoryFromTool("Python assemble write", [&] {
                        uint32_t addr = asmResult.startAddress;
                        for (uint8_t b : asmResult.bytes)
                            memory->ToolWriteToZ80Memory(static_cast<uint16_t>((addr++) & 0xFFFF), b);
                    });
                    d["written"] = true;
                }
            }

            d["address"] = asmResult.startAddress;
            d["end_address"] = asmResult.endAddress;
            d["bytes"] = asmResult.bytes;

            if (!asmResult.symbols.empty())
            {
                py::dict symbols;
                for (const auto& sym : asmResult.symbols)
                    symbols[sym.first.c_str()] = sym.second;
                d["symbols"] = symbols;
            }
            return d;
        }, "Assemble Z80 source text; optionally write the bytes into RAM",
           py::arg("code"), py::arg("address"), py::arg("write") = false)

        .def("label_resolve", [](Emulator& self, py::object queryValue) -> py::dict {
            py::dict d;
            auto* ctx = self.GetContext();
            LabelManager* labelMgr = ctx && ctx->pDebugManager ? ctx->pDebugManager->GetLabelManager() : nullptr;
            if (!labelMgr) { d["error"] = "label manager not available"; return d; }

            std::string query;
            if (py::isinstance<std::string>(queryValue))
                query = queryValue.cast<std::string>();
            else
                query = std::to_string(queryValue.cast<long>());

            // Name direction
            auto label = labelMgr->GetLabelByName(query);
            if (label)
            {
                d["found"] = true;
                d["query"] = "name";
                d["address"] = label->address;
                d["name"] = label->name;
                if (!label->type.empty())
                    d["type"] = label->type;
                return d;
            }

            // Address direction: 0x / $ / decimal
            uint16_t address = 0;
            bool isAddress = false;
            try
            {
                if (query.rfind("0x", 0) == 0 || query.rfind("0X", 0) == 0)
                {
                    address = static_cast<uint16_t>(std::stoul(query.substr(2), nullptr, 16));
                    isAddress = true;
                }
                else if (query[0] == '$')
                {
                    address = static_cast<uint16_t>(std::stoul(query.substr(1), nullptr, 16));
                    isAddress = true;
                }
                else if (!query.empty() && query.find_first_not_of("0123456789") == std::string::npos)
                {
                    address = static_cast<uint16_t>(std::stoul(query));
                    isAddress = true;
                }
            }
            catch (...)
            {
            }

            if (!isAddress)
            {
                d["found"] = false;
                d["query"] = "name";
                if (labelMgr->GetLabelCount() == 0)
                    d["hint"] = "no labels loaded — symbols_load first";
                return d;
            }

            d["query"] = "address";
            d["address"] = address;

            auto exact = labelMgr->GetLabelByZ80Address(address);
            d["found"] = exact != nullptr;
            if (exact)
                d["name"] = exact->name;

            auto atAddress = labelMgr->GetAllLabelsAtAddress(address);
            if (!atAddress.empty())
            {
                py::list aliases;
                for (const auto& l : atAddress)
                {
                    py::dict item;
                    item["name"] = l->name;
                    item["address"] = l->address;
                    aliases.append(item);
                }
                d["aliases"] = aliases;
            }

            const Label* bestBelow = nullptr;
            const Label* bestAbove = nullptr;
            for (const auto& l : labelMgr->GetAllLabels())
            {
                if (l->address < address && (!bestBelow || l->address > bestBelow->address))
                    bestBelow = l.get();
                else if (l->address > address && (!bestAbove || l->address < bestAbove->address))
                    bestAbove = l.get();
            }
            if (bestBelow)
            {
                d["nearest_below"] = bestBelow->name;
                d["nearest_below_address"] = bestBelow->address;
            }
            if (bestAbove)
            {
                d["nearest_above"] = bestAbove->name;
                d["nearest_above_address"] = bestAbove->address;
            }
            return d;
        }, "Resolve a label name to its address, or an address to label(s)", py::arg("query"))

        .def("listing_load", [](Emulator& self, const std::string& path) -> py::dict {
            py::dict d;
            auto* ctx = self.GetContext();
            ListingParser* parser = ctx && ctx->pDebugManager ? ctx->pDebugManager->GetListingParser() : nullptr;
            if (!parser) { d["error"] = "listing parser not available"; return d; }

            d["ok"] = parser->LoadListing(path);
            if (d["ok"].cast<bool>())
            {
                d["lines"] = static_cast<uint64_t>(parser->GetLineCount());
                d["code_lines"] = static_cast<uint64_t>(parser->GetCodeLineCount());
                d["total_bytes"] = static_cast<uint64_t>(parser->GetTotalBytes());
                d["min_address"] = parser->GetMinAddress();
                d["max_address"] = parser->GetMaxAddress();
            }
            return d;
        }, "Load a sjasmplus .lst source listing", py::arg("path"))

        .def("listing_source_at", [](Emulator& self, py::object addressValue) -> py::dict {
            py::dict d;
            auto* ctx = self.GetContext();
            ListingParser* parser = ctx && ctx->pDebugManager ? ctx->pDebugManager->GetListingParser() : nullptr;
            if (!parser) { d["error"] = "listing parser not available"; return d; }
            if (!parser->IsLoaded()) { d["error"] = "no listing loaded"; return d; }

            Z80State* z80 = self.GetZ80State();
            if (!z80) { d["error"] = "Z80 state not available"; return d; }

            const uint16_t address = addressValue.is_none() ? z80->pc : addressValue.cast<uint16_t>();
            const ListingLine* line = parser->FindLineByAddress(address);
            d["found"] = line != nullptr;
            if (line)
            {
                d["line"] = line->lineNumber;
                d["source"] = line->source;
                d["has_code"] = line->hasCode;
                if (line->hasCode)
                {
                    d["address"] = line->addressStart;
                    d["address_end"] = line->addressEnd;
                }
            }
            return d;
        }, "Source line for an address (default: PC)", py::arg("address") = py::none())

        .def("listing_step_line", [](Emulator& self) -> py::dict {
            py::dict d;
            auto* ctx = self.GetContext();
            if (!ctx || !ctx->pDebugManager) { d["error"] = "debug manager not available"; return d; }
            ListingParser* parser = ctx->pDebugManager->GetListingParser();
            if (!parser) { d["error"] = "listing parser not available"; return d; }
            if (!parser->IsLoaded()) { d["error"] = "no listing loaded"; return d; }

            Z80State* z80 = self.GetZ80State();
            if (!z80) { d["error"] = "Z80 state not available"; return d; }

            const ListingLine* startLine = parser->FindLineByAddress(z80->pc);
            const int startLineNumber = startLine ? startLine->lineNumber : -1;

            const unsigned maxTStates = ctx->config.frame * 100;
            self.RunUntilCondition(
                [parser, startLineNumber](const Z80State& state) {
                    const ListingLine* line = parser->FindLineByAddress(state.pc);
                    return line != nullptr && line->lineNumber != startLineNumber;
                },
                maxTStates);

            z80 = self.GetZ80State();
            const ListingLine* endLine = z80 ? parser->FindLineByAddress(z80->pc) : nullptr;
            d["line_changed"] = endLine != nullptr && endLine->lineNumber != startLineNumber;
            if (z80)
                d["pc"] = z80->pc;
            if (endLine)
            {
                d["line"] = endLine->lineNumber;
                d["source"] = endLine->source;
            }
            return d;
        }, "Step to the next source line")

        .def("listing_run_to_line", [](Emulator& self, int lineNumber) -> py::dict {
            py::dict d;
            auto* ctx = self.GetContext();
            if (!ctx || !ctx->pDebugManager) { d["error"] = "debug manager not available"; return d; }
            ListingParser* parser = ctx->pDebugManager->GetListingParser();
            if (!parser) { d["error"] = "listing parser not available"; return d; }
            if (!parser->IsLoaded()) { d["error"] = "no listing loaded"; return d; }

            Z80State* z80 = self.GetZ80State();
            if (!z80) { d["error"] = "Z80 state not available"; return d; }

            const ListingLine* target = parser->FindNextCodeLine(lineNumber);
            if (!target)
            {
                d["error"] = "no code line at or after line " + std::to_string(lineNumber);
                return d;
            }

            const uint16_t targetAddress = target->addressStart;
            const bool alreadyAt = z80->pc == targetAddress;
            if (!alreadyAt)
            {
                const unsigned maxTStates = ctx->config.frame * 500;
                self.RunUntilCondition(
                    [targetAddress](const Z80State& state) { return state.pc == targetAddress; }, maxTStates);
            }

            z80 = self.GetZ80State();
            const bool reached = z80 && z80->pc == targetAddress;
            d["reached"] = reached;
            d["already_at"] = alreadyAt;
            if (z80)
                d["pc"] = z80->pc;
            d["line"] = target->lineNumber;
            d["source"] = target->source;
            return d;
        }, "Run until PC reaches the first code byte of a listing line", py::arg("line"));

        // Port trace (PDR) bindings — runtime feature "porttrace"
        registerPortTraceBindings(emulatorClass);

        // TS-Conf VDAC2 card (FT812): bus capture
        registerVdac2Bindings(emulatorClass);
    }
}