#pragma once

#include "emulator/memory/devicememory.h"
#include "emulator/sound/audiomixer.h"
#include "emulator/video/framebufferexport.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/ports/models/profiboard.h"
#include "emulator/video/screenshotter.h"
#include "emulator/ports/models/sprinter/sprinterbios.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/zxpoly/zxpolygroup.h"
#include <sol/sol.hpp>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/rzx/rzxlauncher.h>
#include <loaders/snapshot/snapshotlauncher.h>
#include "../bindings/lua_porttrace.h"
#include "../bindings/lua_vdac2.h"
#include <emulator/memory/memory.h>
#include <emulator/memory/memorymap.h>  // TD-3 sparse map + hexdump
#include <emulator/io/fdc/fdd.h>
#include <emulator/io/ide/cdaudiocontrol.h>
#include <emulator/media/mediacontrol.h>
#include <emulator/io/fdc/diskimage.h>
#include <emulator/io/tape/tape.h>
#include <tapeaudio/tapeaudioimporter.h>
#include <tapeaudio/tapeaudiorenderer.h>
#include <emulator/cpu/z80.h>
#include <emulator/video/screen.h>
#include <emulator/sound/soundcharactersettings.h>
#include <emulator/sound/chips/neogs/neogsmedia.h>
#include <emulator/sound/soundmanager.h>
#include <emulator/sound/chips/soundchip_ay8910.h>
#include <emulator/sound/chips/gs/soundchip_gs.h>
#include "../../../automation.h"
#include "../../../temporalstatus.h"
#include <debugger/debugmanager.h>
#include <debugger/keyboard/debugkeyboardmanager.h>
#include <debugger/mouse/debugmousemanager.h>
#include <debugger/joystick/debugjoystickmanager.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/disassembler/z80disasm.h>
#include <debugger/labels/labelmanager.h>
#include <debugger/ttd/timetravelmanager.h>
#include <debugger/ttd/machinestatehash.h>
#include <debugger/ttd/ttdfileinfo.h>
#include <tuple>
#include <debugger/ttd/ttdexternalevents.h>
#include <debugger/ttd/ttdprobe.h>
#include <debugger/analyzers/analyzermanager.h>
#include <debugger/analyzers/audiocapture/audiocaptureanalyzer.h>
#include <debugger/analyzers/aylog/ayloganalyzer.h>
#include <debugger/analyzers/coverage/coverageanalyzer.h>
#include <debugger/assembler/z80textassembler.h>
#include <debugger/listing/listingparser.h>
#include <emulator/platform.h>
#include <emulator/ports/portdecoder.h>
#include <emulator/config.h>
#include <emulator/io/rtc/rtcaccess.h>
#include <emulator/state/devicestate.h>
#include <emulator/video/screendigest.h>
#include <base/featuremanager.h>
#ifdef ENABLE_RECORDING
#include "recordingmanager.h"
#include "recordingrequest.h"
#include <atomic>
#include <ctime>
#include <filesystem>
#endif
#include <3rdparty/tinywav/tinywav.h>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

/// Shared page-index validation for the page_* bindings: an invalid index
/// must raise a Lua error instead of silently returning zeros or touching
/// memory outside the page (a negative cache/misc page reads before the
/// buffer)
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


/// StateNode -> Lua table (objects keep their keys, arrays become 1-based
/// sequences). The one converter Lua needs for every DeviceState report.
inline sol::object StateNodeToLua(sol::this_state s, const StateNode& node)
{
    sol::state_view lua(s);
    switch (node.kind)
    {
        case StateNode::Kind::Bool: return sol::make_object(lua, node.b);
        case StateNode::Kind::Int: return sol::make_object(lua, node.i);
        case StateNode::Kind::Double: return sol::make_object(lua, node.d);
        case StateNode::Kind::String: return sol::make_object(lua, node.s);
        case StateNode::Kind::Object:
        {
            sol::table t = lua.create_table();
            for (const auto& m : node.members)
                t[m.first] = StateNodeToLua(s, m.second);
            return t;
        }
        case StateNode::Kind::Array:
        {
            sol::table t = lua.create_table();
            for (size_t i = 0; i < node.items.size(); i++)
                t[i + 1] = StateNodeToLua(s, node.items[i]);
            return t;
        }
        default: return sol::make_object(lua, sol::lua_nil);
    }
}


/// The recorded machine of a TTD session / file as a Lua table (the same keys
/// as the WebAPI: model_id, model, ram_page_bound, rom_signature, peripheral_mask,
/// peripherals, general_sound, turbo_sound)
inline sol::table TtdRecordedMachineTable(sol::state_view& lua, const ttd::TTDRecordedMachine& m)
{
    sol::table t = lua.create_table();
    t["model_id"] = static_cast<unsigned>(m.modelId);
    if (!m.model.empty())
        t["model"] = m.model;
    t["ram_page_bound"] = static_cast<unsigned>(m.ramPageBound);
    if (m.romSignature != 0)
        t["rom_signature"] = "0x" + ttd::HashToString(m.romSignature);
    t["peripheral_mask"] = m.peripheralMask;
    sol::table list = lua.create_table();
    for (size_t i = 0; i < m.peripherals.size(); ++i)
        list[i + 1] = m.peripherals[i];
    t["peripherals"] = list;
    t["general_sound"] = ttd::GeneralSoundName(m.generalSound);
    t["turbo_sound"] = m.turboSound;
    return t;
}

class LuaEmulator
{
    /// region <Fields>
protected:
    Emulator* _emulator = nullptr;
    sol::state* _lua = nullptr;

    /// Resolve the emulator to operate on: the explicitly bound instance if set,
    /// otherwise the currently selected emulator (same source the REST API uses).
    Emulator* effectiveEmulator() const
    {
        if (_emulator)
            return _emulator;

        auto* mgr = EmulatorManager::GetInstance();
        if (mgr)
        {
            std::string id = mgr->GetSelectedEmulatorId();
            if (!id.empty())
                return mgr->GetEmulator(id).get();
        }
        return nullptr;
    }

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
    /// endregion </Fields>

    /// region <Kempston Mouse helpers (automation-interfaces §4.7)>
protected:
    DebugMouseManager* mouseManager() const
    {
        Emulator* emu = effectiveEmulator();
        EmulatorContext* ctx = emu ? emu->GetContext() : nullptr;
        return (ctx && ctx->pDebugManager) ? ctx->pDebugManager->GetMouseManager() : nullptr;
    }

    /// Integral Lua number -> long long. Rejects nil, strings and 1.5 (sol2 would truncate silently).
    static bool mouseIntArg(const sol::object& obj, const char* name, long long minValue, long long maxValue,
                            long long& out, std::string& error)
    {
        if (obj.get_type() == sol::type::number)
        {
            const double value = obj.as<double>();
            if (std::isfinite(value) && std::trunc(value) == value && value >= -9007199254740992.0 &&
                value <= 9007199254740992.0)
            {
                const long long integral = static_cast<long long>(value);
                if (integral >= minValue && integral <= maxValue)
                {
                    out = integral;
                    return true;
                }
            }
        }
        error = std::string(name) + " must be an integer";
        return false;
    }

    static sol::variadic_results mouseError(sol::this_state s, const std::string& message)
    {
        sol::variadic_results results;
        results.push_back(sol::make_object(s, sol::lua_nil));
        results.push_back(sol::make_object(s, message));
        return results;
    }

    /// State table: same keys as the WebAPI state object
    static sol::table mouseStateTable(sol::this_state s, const MouseStateSnapshot& state,
                                      const std::string& warning = "")
    {
        sol::state_view lua(s);
        sol::table buttons = lua.create_table();
        buttons["left"] = state.IsPressed(MouseButton::Left);
        buttons["right"] = state.IsPressed(MouseButton::Right);
        buttons["middle"] = state.IsPressed(MouseButton::Middle);

        auto hex = [](uint8_t value) {
            char text[8];
            std::snprintf(text, sizeof(text), "0x%02X", value);
            return std::string(text);
        };
        sol::table ports = lua.create_table();
        ports["FADF"] = static_cast<int>(state.portButtons);  // integers, same as the WebAPI
        ports["FBDF"] = static_cast<int>(state.portX);  // integers, same as the WebAPI
        ports["FFDF"] = static_cast<int>(state.portY);  // integers, same as the WebAPI

        sol::table t = lua.create_table();
        t["x"] = state.x;
        t["y"] = state.y;
        t["buttons"] = buttons;
        t["button_mask"] = state.buttonMask;
        t["wheel"] = state.wheel;
        t["wheel_enabled"] = state.wheelEnabled;
        t["present"] = state.present;
        t["ports"] = ports;
        if (state.pendingClickButton.has_value())
        {
            sol::table pending = lua.create_table();
            pending["button"] = DebugMouseManager::GetButtonName(*state.pendingClickButton);
            pending["frames_left"] = state.pendingClickFramesLeft;
            t["pending_click"] = pending;
        }
        t["ttd_journal"] = state.journalSupported ? "supported" : "unsupported";
        if (!warning.empty())
            t["warning"] = warning;
        return t;
    }

    static sol::variadic_results mouseResult(sol::this_state s, DebugMouseManager& mgr,
                                             const MouseInjectResult& result)
    {
        if (!result.ok())
            return mouseError(s, result.message);
        sol::variadic_results results;
        results.push_back(sol::make_object(s, mouseStateTable(s, mgr.GetState(), result.warning)));
        return results;
    }

    static std::optional<MouseButton> mouseButtonArg(const sol::object& obj, std::string& error)
    {
        if (obj.get_type() == sol::type::string)
        {
            const std::string name = obj.as<std::string>();
            if (auto button = DebugMouseManager::ResolveButtonName(name))
                return button;
            error = "Unknown mouse button '" + name + "'. Valid: left, right, middle (or l, r, m)";
            return std::nullopt;
        }
        error = "button must be a string (left, right, middle or l, r, m)";
        return std::nullopt;
    }
    /// endregion </Kempston Mouse helpers>

    /// region <Kempston joystick helpers (joystick TDD §5)>
protected:
    DebugJoystickManager* joystickManager() const
    {
        Emulator* emu = effectiveEmulator();
        EmulatorContext* ctx = emu ? emu->GetContext() : nullptr;
        return (ctx && ctx->pDebugManager) ? ctx->pDebugManager->GetJoystickManager() : nullptr;
    }

    /// State table: same keys as the WebAPI state object
    static sol::table joystickStateTable(sol::this_state s, const JoystickStateSnapshot& state,
                                         const std::string& warning = "")
    {
        sol::state_view lua(s);
        sol::table buttons = lua.create_table();
        for (const std::string& name : DebugJoystickManager::GetAllButtonNames())
            buttons[name] = false;
        sol::table pressed = lua.create_table();
        int index = 1;
        for (const std::string& name : state.buttons)
        {
            buttons[name] = true;
            pressed[index++] = name;
        }
        sol::table names = lua.create_table();
        index = 1;
        for (const std::string& name : DebugJoystickManager::GetAllButtonNames())
            names[index++] = name;

        sol::table t = lua.create_table();
        t["available"] = state.available;
        t["present"] = state.present;
        t["wired"] = state.wired;
        t["state"] = static_cast<int>(state.state);
        t["port_value"] = static_cast<int>(state.portValue);
        t["buttons"] = buttons;
        t["pressed"] = pressed;
        t["button_names"] = names;
        t["keys"] = state.keys;
        if (state.pendingTapMask != 0)
        {
            sol::table pending = lua.create_table();
            pending["mask"] = static_cast<int>(state.pendingTapMask);
            pending["frames_left"] = static_cast<int>(state.pendingTapFramesLeft);
            t["pending_tap"] = pending;
        }
        if (!warning.empty())
            t["warning"] = warning;
        return t;
    }

    static sol::variadic_results joystickResult(sol::this_state s, DebugJoystickManager& mgr,
                                                const JoystickInjectResult& result)
    {
        if (!result.ok())
            return mouseError(s, result.message);
        sol::variadic_results results;
        results.push_back(sol::make_object(s, joystickStateTable(s, mgr.GetState(), result.warning)));
        return results;
    }

    /// Button list: a string ("up+fire", "up,fire") or a table of names -> one list string
    static bool joystickNamesArg(const sol::object& obj, std::string& names, std::string& error)
    {
        names.clear();
        if (obj.get_type() == sol::type::string)
        {
            names = obj.as<std::string>();
            return true;
        }
        if (obj.get_type() == sol::type::table)
        {
            sol::table table = obj.as<sol::table>();
            for (size_t i = 1; i <= table.size(); i++)
            {
                sol::object item = table[i];
                if (item.get_type() != sol::type::string)
                {
                    error = "buttons must be a string or a table of button names";
                    return false;
                }
                names += (names.empty() ? "" : ",") + item.as<std::string>();
            }
            return true;
        }
        error = "buttons must be a string or a table of button names";
        return false;
    }
    /// endregion </Kempston joystick helpers>

    /// region <Constructors / destructors>
public:
    LuaEmulator() = default;
    virtual ~LuaEmulator() = default;
    /// endregion </Constructors / destructors>

    /// region <Lua SOL lifecycle>
public:
    void registerType(sol::state& lua)
    {
        // Register the Emulator class with Sol2 - extended bindings
        lua.new_usertype<Emulator>(
            "Emulator",
            // Lifecycle control
            "start", &Emulator::Start,
            "stop", &Emulator::Stop,
            "pause", sol::resolve<void(bool)>(&Emulator::Pause),
            "resume", sol::resolve<void(bool)>(&Emulator::Resume),
            "reset", &Emulator::Reset,
            "request_nmi", &Emulator::RequestNMI,
            "request_mni", &Emulator::RequestMNI,
            // Front-panel switches (Profi "turbo"): get -> true/false, nil when absent; set -> true when done
            "get_switch", [](Emulator& self, const std::string& name) -> sol::optional<bool> {
                FrontPanelSwitch sw;
                if (!ParseFrontPanelSwitch(name, sw))
                    return sol::nullopt;
                const int value = self.GetFrontPanelSwitch(sw);
                if (value < 0)
                    return sol::nullopt;
                return value != 0;
            },
            "set_switch", [](Emulator& self, const std::string& name, bool on) {
                FrontPanelSwitch sw;
                return ParseFrontPanelSwitch(name, sw) && self.SetFrontPanelSwitch(sw, on);
            },
            
            // State queries
            "is_running", &Emulator::IsRunning,
            "is_paused", &Emulator::IsPaused,
            "get_id", &Emulator::GetId,
            "get_state", [](Emulator& emu) -> std::string {
                switch (emu.GetState()) {
                    case StateRun: return "running";
                    case StatePaused: return "paused";
                    case StateStopped: return "stopped";
                    case StateInitialized: return "initialized";
                    case StateResumed: return "resumed";
                    default: return "unknown";
                }
            },
            // RAM contents the machine was created with: "random" | "zero"
            "ram_power_on", [](Emulator& emu) -> std::string {
                EmulatorContext* context = emu.GetContext();
                return context ? Config::RamPowerOnName(context->config.ramPowerOn) : "";
            }
        );

        // EmulatorManager bindings for multi-instance support
        lua.set_function("emu_list", []() -> sol::as_table_t<std::vector<std::string>> {
            auto* mgr = EmulatorManager::GetInstance();
            return sol::as_table(mgr->GetEmulatorIds());
        });

        lua.set_function("emu_count", []() -> int {
            auto* mgr = EmulatorManager::GetInstance();
            return static_cast<int>(mgr->GetEmulatorIds().size());
        });

        // ZX-Poly machines (EmulatorManager::CreateZXPolyMachine - the entry point
        // every surface uses): four synchronized instances of one model.
        // zxpoly_start([model], [file], [ram_power_on]) -> master id, or nil + error.
        // ram_power_on: "random" | "zero" (RAM contents of all four modules;
        // default: the model's unreal.ini)
        lua.set_function("zxpoly_start", [](sol::optional<std::string> model, sol::optional<std::string> file,
                                            sol::optional<std::string> ramPowerOn,
                                            sol::this_state state) -> sol::variadic_results {
            sol::variadic_results results;
            auto* mgr = EmulatorManager::GetInstance();
            std::function<void(CONFIG&)> configOverride;
            if (ramPowerOn)
            {
                RamPowerOn mode = RamPowerOn::Random;
                if (!Config::ParseRamPowerOn(*ramPowerOn, mode))
                {
                    results.push_back(sol::make_object(state, sol::lua_nil));
                    results.push_back(sol::make_object(state, "ram_power_on must be random or zero"));
                    return results;
                }
                configOverride = Config::RamPowerOnOverride(mode);
            }
            std::string error;
            auto master = mgr->CreateZXPolyMachine("", model.value_or("PENTAGON"), file.value_or(""), &error,
                                                   configOverride);
            if (!master)
            {
                results.push_back(sol::make_object(state, sol::lua_nil));
                results.push_back(sol::make_object(state, "cannot start ZX-Poly: " + error));
                return results;
            }
            mgr->StartEmulatorAsync(master->GetId());
            mgr->SetSelectedEmulatorId(master->GetId());
            results.push_back(sol::make_object(state, master->GetId()));
            return results;
        });

        // zxpoly_status(id) -> table (nil if not a ZX-Poly machine)
        lua.set_function("zxpoly_status", [](const std::string& id, sol::this_state state) -> sol::object {
            ZXPolyGroup* group = EmulatorManager::GetInstance()->GetZXPolyGroup(id);
            if (!group)
                return sol::make_object(state, sol::lua_nil);
            const ZXPolyGroup::Status status = group->GetStatus();
            sol::state_view view(state);
            sol::table out = view.create_table();
            out["master_id"] = status.memberIds[0];
            out["locked"] = status.locked;
            out["slaves_running"] = status.slavesRunning;
            out["parallel_slaves"] = status.parallelSlaves;
            out["pipelined_slaves"] = status.pipelinedSlaves;
            out["port_3d00"] = status.port3D00;
            out["video_mode"] = status.videoMode;
            sol::table modules = view.create_table();
            for (size_t m = 0; m < ZXPolyGroup::MODULES; m++)
            {
                sol::table module = view.create_table();
                module["module"] = m;
                module["id"] = status.memberIds[m];
                sol::table registers = view.create_table();
                for (size_t r = 0; r < 4; r++)
                    registers[r + 1] = status.registers[m][r];
                module["registers"] = registers;
                modules[m + 1] = module;
            }
            out["modules"] = modules;
            out["diverged"] = status.divergence.diverged;
            out["divergence"] = status.divergence.what;
            return out;
        });

        lua.set_function("emu_get", [](const std::string& id) -> Emulator* {
            auto* mgr = EmulatorManager::GetInstance();
            auto emu = mgr->GetEmulator(id);
            return emu.get();
        });

        lua.set_function("emu_get_selected", []() -> Emulator* {
            auto* mgr = EmulatorManager::GetInstance();
            if (mgr) {
                std::string id = mgr->GetSelectedEmulatorId();
                if (!id.empty()) {
                    return mgr->GetEmulator(id).get();
                }
            }
            return nullptr;
        });

        lua.set_function("videowall_singlesync", [](bool enable, sol::optional<std::string> emulatorId) -> bool {
            std::string id = emulatorId.value_or("");
            return Automation::GetInstance().SetVideowallSingleSyncMode(enable, id);
        });

        // Register access - requires emulator instance
        lua.set_function("get_pc", [this]() -> uint16_t {
            if (!effectiveEmulator()) return 0;
            Z80State* z80 = effectiveEmulator()->GetZ80State();
            return z80 ? z80->pc : 0;
        });

        lua.set_function("get_sp", [this]() -> uint16_t {
            if (!effectiveEmulator()) return 0;
            Z80State* z80 = effectiveEmulator()->GetZ80State();
            return z80 ? z80->sp : 0;
        });

        lua.set_function("get_af", [this]() -> uint16_t {
            if (!effectiveEmulator()) return 0;
            Z80State* z80 = effectiveEmulator()->GetZ80State();
            return z80 ? z80->af : 0;
        });

        lua.set_function("get_bc", [this]() -> uint16_t {
            if (!effectiveEmulator()) return 0;
            Z80State* z80 = effectiveEmulator()->GetZ80State();
            return z80 ? z80->bc : 0;
        });

        lua.set_function("get_de", [this]() -> uint16_t {
            if (!effectiveEmulator()) return 0;
            Z80State* z80 = effectiveEmulator()->GetZ80State();
            return z80 ? z80->de : 0;
        });

        lua.set_function("get_hl", [this]() -> uint16_t {
            if (!effectiveEmulator()) return 0;
            Z80State* z80 = effectiveEmulator()->GetZ80State();
            return z80 ? z80->hl : 0;
        });

        lua.set_function("get_ix", [this]() -> uint16_t {
            if (!effectiveEmulator()) return 0;
            Z80State* z80 = effectiveEmulator()->GetZ80State();
            return z80 ? z80->ix : 0;
        });

        lua.set_function("get_iy", [this]() -> uint16_t {
            if (!effectiveEmulator()) return 0;
            Z80State* z80 = effectiveEmulator()->GetZ80State();
            return z80 ? z80->iy : 0;
        });

        lua.set_function("get_registers", [this](sol::this_state s) -> sol::table {
            // Use the calling Lua state directly (safe even if _lua was never wired)
            sol::state_view lua_view(s);
            sol::table regs = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (emulator) {
                Z80State* z80 = emulator->GetZ80State();
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
                    regs["r"] = (z80->r_hi << 7) | (z80->r_low & 0x7F);
                }
            }
            return regs;
        });

        lua.set_function("get_register", [this](sol::this_state s, const std::string& name) -> sol::object {
            sol::state_view lua_view(s);
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return sol::make_object(lua_view, sol::lua_nil);
            Z80State* z80 = emulator->GetZ80State();
            if (!z80)
                return sol::make_object(lua_view, sol::lua_nil);
            uint16_t value = 0;
            bool is16bit = false;
            if (!Z80::GetRegisterValue(z80, name, value, is16bit))
                return sol::make_object(lua_view, sol::lua_nil);
            return sol::make_object(lua_view, value);
        });

        lua.set_function("set_register", [this](const std::string& name, uint16_t value) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return false;
            Z80State* z80 = emulator->GetZ80State();
            if (!z80)
                return false;
            return Z80::SetRegisterValue(z80, name, value);
        });

        // Memory access: direct (non-mutating) reads so inspecting memory
        // never drives the ProfROM quadrant machine
        lua.set_function("mem_read", [this](uint16_t addr) -> uint8_t {
            if (!effectiveEmulator()) return 0;
            Memory* mem = effectiveEmulator()->GetMemory();
            return mem ? mem->DirectReadFromZ80Memory(addr) : 0;
        });

        lua.set_function("mem_write", [this](uint16_t addr, uint8_t value) {
            if (!effectiveEmulator()) return;
            Memory* mem = effectiveEmulator()->GetMemory();
            if (!mem) return;
            effectiveEmulator()->EditMemoryFromTool("Lua memory write", [&] { mem->ToolWriteToZ80Memory(addr, value); });
        });

        lua.set_function("mem_read_word", [this](uint16_t addr) -> uint16_t {
            if (!effectiveEmulator()) return 0;
            Memory* mem = effectiveEmulator()->GetMemory();
            if (!mem) return 0;
            return mem->DirectReadFromZ80Memory(addr) | (mem->DirectReadFromZ80Memory(static_cast<uint16_t>(addr + 1)) << 8);
        });

        lua.set_function("mem_write_word", [this](uint16_t addr, uint16_t value) {
            if (!effectiveEmulator()) return;
            Memory* mem = effectiveEmulator()->GetMemory();
            if (!mem) return;
            effectiveEmulator()->EditMemoryFromTool("Lua memory write", [&] {
                mem->ToolWriteToZ80Memory(addr, value & 0xFF);
                mem->ToolWriteToZ80Memory(static_cast<uint16_t>(addr + 1), (value >> 8) & 0xFF);
            });
        });

        lua.set_function("mem_read_block", [this](uint16_t addr, uint16_t len) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table data = lua_view.create_table();
            if (!effectiveEmulator()) return data;
            Memory* mem = effectiveEmulator()->GetMemory();
            if (!mem) return data;
            for (uint16_t i = 0; i < len; i++) {
                data[i + 1] = mem->DirectReadFromZ80Memory(static_cast<uint16_t>(addr + i));
            }
            return data;
        });

        // TD-3 Phase 1: sparse non-zero block overview (same core source as
        // GET /memory/map and MCP inspect_state 'memory_map').
        // memory_map() | memory_map("ram") | memory_map("address", 64, 48)
        lua.set_function("memory_map", [this](sol::optional<std::string> viewName, sol::optional<int> minRunOpt,
                                              sol::optional<int> maxBlocksOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!effectiveEmulator()) return result;
            Memory* mem = effectiveEmulator()->GetMemory();
            EmulatorContext* ctx = effectiveEmulator()->GetContext();
            if (!mem || !ctx) return result;

            const MemoryMapView view = (viewName && (*viewName == "ram" || *viewName == "pages"))
                                           ? MemoryMapView::RamPages
                                           : MemoryMapView::AddressSpace;
            const uint32_t minRun = (minRunOpt && *minRunOpt > 0) ? static_cast<uint32_t>(*minRunOpt) : kMemoryMapDefaultMinRun;
            const uint32_t maxBlocks = (maxBlocksOpt && *maxBlocksOpt > 0) ? static_cast<uint32_t>(*maxBlocksOpt)
                                                                           : kMemoryMapDefaultMaxBlocks;

            const MemoryMapReport report = BuildMemoryMap(*mem, ctx->config, view, minRun, maxBlocks);
            result["model"] = report.model;
            result["view"] = report.ramView ? "ram" : "address";
            result["total_size"] = report.totalSize;
            result["non_zero_bytes"] = report.nonZeroBytes;
            result["min_run"] = report.minRun;
            result["truncated"] = report.truncated;

            sol::table blocks = lua_view.create_table();
            for (size_t i = 0; i < report.blocks.size(); i++) {
                const MemoryMapBlock& block = report.blocks[i];
                sol::table item = lua_view.create_table();
                item["address"] = block.address;
                item["size"] = block.size;
                item["type"] = block.typeName;
                item["bank"] = block.bank == 0xFF ? -1 : static_cast<int>(block.bank);
                item["page"] = block.page;
                item["rom"] = block.isRom;
                item["status"] = block.IsZeroFill() ? "zeros" : "data";
                item["non_zero"] = block.nonZero;
                if (!block.IsZeroFill())
                    item["hash"] = block.hash;  // FNV-1a 64 fingerprint (lua_Integer)
                blocks[i + 1] = item;
            }
            result["blocks"] = blocks;
            return result;
        });

        // TD-3 compact read format: classic 16B/line hexdump + ASCII sidebar
        lua.set_function("mem_hexdump", [this](uint16_t addr, sol::optional<int> lenOpt) -> std::string {
            if (!effectiveEmulator()) return "";
            Memory* mem = effectiveEmulator()->GetMemory();
            if (!mem) return "";
            const size_t len = lenOpt ? static_cast<size_t>(*lenOpt) : 64;
            if (len < 1 || len > 4096) return "";
            std::vector<uint8_t> buffer(len);
            for (size_t i = 0; i < len; i++)
                buffer[i] = mem->DirectReadFromZ80Memory(static_cast<uint16_t>(addr + i));
            return FormatHexDump(buffer.data(), buffer.size(), addr);
        });

        lua.set_function("mem_write_block", [this](uint16_t addr, sol::table data) {
            if (!effectiveEmulator()) return;
            Memory* mem = effectiveEmulator()->GetMemory();
            if (!mem) return;
            effectiveEmulator()->EditMemoryFromTool("Lua memory write", [&] {
                for (auto& pair : data) {
                    int idx = pair.first.as<int>() - 1;  // Lua tables start at 1
                    uint8_t val = pair.second.as<uint8_t>();
                    mem->ToolWriteToZ80Memory(static_cast<uint16_t>((addr + idx) & 0xFFFF), val);
                }
            });
        });

        // Physical page access (ram/rom/cache/misc)
        lua.set_function("page_read", [this](const std::string& type, int page, int offset) -> int {
            if (!effectiveEmulator()) return 0;
            Memory* mem = effectiveEmulator()->GetMemory();
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
        });

        lua.set_function("page_write", [this](const std::string& type, int page, int offset, uint8_t value) {
            if (!effectiveEmulator()) return;
            Memory* mem = effectiveEmulator()->GetMemory();
            if (!mem) return;
            ValidatePageIndex("page_write", type, page, offset);
            uint8_t* pagePtr = nullptr;
            if (type == "ram")
                pagePtr = mem->RAMPageAddress(static_cast<uint16_t>(page));
            else if (type == "rom")
                pagePtr = mem->ROMPageHostAddress(static_cast<uint8_t>(page));
            else if (type == "cache")
                pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
            else
                pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
            effectiveEmulator()->EditMemoryFromTool("Lua page write", [&] {
                pagePtr[offset] = value;
                if (type == "ram")
                    mem->MarkRamPageEdited(static_cast<uint16_t>(page));
            });
        });

        lua.set_function("page_read_block", [this](const std::string& type, int page, int offset, int len) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table data = lua_view.create_table();
            if (!effectiveEmulator()) return data;
            Memory* mem = effectiveEmulator()->GetMemory();
            if (!mem) return data;
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
            if (len > PAGE_SIZE - offset) len = PAGE_SIZE - offset;
            for (int i = 0; i < len; i++) {
                data[i + 1] = pagePtr[offset + i];
            }
            return data;
        });

        lua.set_function("page_write_block", [this](const std::string& type, int page, int offset, sol::table data) {
            if (!effectiveEmulator()) return;
            Memory* mem = effectiveEmulator()->GetMemory();
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
            int maxLen = PAGE_SIZE - offset;
            effectiveEmulator()->EditMemoryFromTool("Lua page write", [&] {
                int idx = 0;
                for (auto& pair : data) {
                    if (idx >= maxLen) break;
                    uint8_t val = pair.second.as<uint8_t>();
                    pagePtr[offset + idx] = val;
                    idx++;
                }
                if (type == "ram")
                    mem->MarkRamPageEdited(static_cast<uint16_t>(page));
            });
        });

        // Device memory regions (emulator/memory/devicememory.h): the Sprinter's video RAM "vram".
        // memory_regions() -> {available, regions = {...}}; region_read(name, offset, len) -> table of bytes
        // (nil, error on a bad range); region_write(name, offset, {bytes} | "hex") -> true | nil, error;
        // region_save(name, path [, offset, len]) / region_load(name, path [, offset]) -> true | nil, error
        // framebuffer([format]): the picture as raw pixels (FramebufferExport): {width, height, format, encoding,
        // data = a string of bytes}; "rgba" (default) or "index" (the Sprinter's pens); nil, error otherwise
        lua.set_function("framebuffer", [this](sol::this_state s, sol::optional<std::string> format) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            FramebufferExport::Frame frame;
            std::string error;
            if (!FramebufferExport::Capture(emulator->GetContext(), format.value_or("rgba"), frame, error))
                return mouseError(s, error);
            sol::state_view view(s);
            sol::table t = view.create_table();
            t["width"] = frame.width;
            t["height"] = frame.height;
            t["format"] = frame.format;
            t["encoding"] = frame.encoding;
            t["data"] = std::string(frame.bytes.begin(), frame.bytes.end());
            sol::variadic_results out;
            out.push_back(t);
            return out;
        });
        // screenshot([{area = "full"|"screen", format = "png"|"gif", source = "presented"|"live", path = "file"}]): a
        // screenshot (core Screenshotter): the whole frame (default) or the working picture, PNG (default) or GIF,
        // of the presented frame (default) or the live one as drawn now (a paused machine adds frame.partial and
        // frame.beam = {line, tstate}).
        // Returns {format, area, width, height, size, crop = {x,y,width,height}, screen_window = {...}, frame = {width,
        // height, mode, source, frame_number}, data = the encoded image as a string of bytes} or, with a path, `file`
        // instead of `data`; nil, error on a bad word, a missing emulator or no frame
        lua.set_function("screenshot", [this](sol::this_state s, sol::optional<sol::table> opts) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            EmulatorContext* context = emulator->GetContext();
            if (!context || !context->pScreen) return mouseError(s, "The emulator has no screen");

            ScreenshotOptions options;  // the whole frame, PNG
            if (opts)
            {
                const std::string area = opts->get_or<std::string>("area", "");
                const std::string format = opts->get_or<std::string>("format", "");
                const std::string source = opts->get_or<std::string>("source", "");
                if (!area.empty() && !Screenshotter::ParseArea(area, options.area))
                    return mouseError(s, "Unknown area '" + area + "': use full or screen");
                if (!source.empty() && !Screenshotter::ParseSource(source, options.source))
                    return mouseError(s, "Unknown source '" + source + "': use presented or live");
                if (!format.empty() && !Screenshotter::ParseFormat(format, options.format))
                    return mouseError(s, "Unknown format '" + format + "': use png or gif");
                options.saveTo = opts->get_or<std::string>("path", "");
            }

            const ScreenshotResult shot = Screenshotter::TakeFrom(*context->pScreen, options, emulator->IsEmulationParked());
            if (!shot.ok) return mouseError(s, shot.errorMessage);

            sol::state_view view(s);
            auto rect = [&view](const VideoFrameRect& r) {
                sol::table t = view.create_table();
                t["x"] = r.x;
                t["y"] = r.y;
                t["width"] = r.width;
                t["height"] = r.height;
                return t;
            };
            sol::table t = view.create_table();
            t["format"] = Screenshotter::FormatName(shot.format);
            t["area"] = Screenshotter::AreaName(options.area);
            t["source"] = Screenshotter::RequestSourceName(options.source);
            t["width"] = shot.width;
            t["height"] = shot.height;
            t["size"] = shot.encodedSize;
            t["crop"] = rect(shot.crop);
            t["screen_window"] = rect(shot.frame.screenWindow);
            sol::table frame = view.create_table();
            frame["width"] = shot.frame.width;
            frame["height"] = shot.frame.height;
            frame["mode"] = shot.frame.source == FrameSource::External ? std::string("external")
                                                                       : Screen::GetVideoModeName(shot.frame.videoMode);
            frame["source"] = Screenshotter::SourceName(shot.frame.source);
            frame["frame_number"] = shot.frame.frameNumber;
            if (shot.frame.beamLine >= 0)
            {
                frame["partial"] = shot.frame.partial;
                sol::table beam = view.create_table();
                beam["line"] = shot.frame.beamLine;
                beam["tstate"] = shot.frame.beamTstate;
                frame["beam"] = beam;
            }
            t["frame"] = frame;
            if (!shot.savedFile.empty())
                t["file"] = shot.savedFile;
            else
                t["data"] = std::string(shot.bytes.begin(), shot.bytes.end());
            sol::variadic_results out;
            out.push_back(t);
            return out;
        });
        lua.set_function("memory_regions", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::MemoryRegions(emulator->GetContext()));
        });
        lua.set_function("region_read", [this](sol::this_state s, const std::string& name, uint32_t offset,
                                                sol::optional<uint32_t> length) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            std::vector<uint8_t> bytes;
            std::string error;
            if (!DeviceMemory::Read(emulator->GetContext(), name, offset, length.value_or(256), bytes, error))
                return mouseError(s, error);
            sol::state_view view(s);
            sol::table data = view.create_table(static_cast<int>(bytes.size()), 0);
            for (size_t i = 0; i < bytes.size(); i++)
                data[i + 1] = bytes[i];
            sol::variadic_results out;
            out.push_back(data);
            return out;
        });
        lua.set_function("region_write", [this](sol::this_state s, const std::string& name, uint32_t offset,
                                                 sol::object data) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            std::vector<uint8_t> bytes;
            if (data.get_type() == sol::type::string)
            {
                if (!DeviceMemory::ParseHexBytes(data.as<std::string>(), bytes))
                    return mouseError(s, "data: a table of bytes or a hex string (\"0000A8\")");
            }
            else if (data.get_type() == sol::type::table)
            {
                sol::table t = data.as<sol::table>();
                for (size_t i = 1; i <= t.size(); i++)
                    bytes.push_back(static_cast<uint8_t>(t.get<int>(i) & 0xFF));
            }
            else
                return mouseError(s, "data: a table of bytes or a hex string");
            std::string error;
            if (!DeviceMemory::Write(emulator->GetContext(), name, offset, bytes, "Lua region write", error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(sol::make_object(s, true));
            return out;
        });
        lua.set_function("region_save", [this](sol::this_state s, const std::string& name, const std::string& path,
                                                sol::optional<uint32_t> offset, sol::optional<uint32_t> length) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            std::string error;
            if (!DeviceMemory::Save(emulator->GetContext(), name, path, offset.value_or(0), length.value_or(0), error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(sol::make_object(s, true));
            return out;
        });
        lua.set_function("region_load", [this](sol::this_state s, const std::string& name, const std::string& path,
                                                sol::optional<uint32_t> offset) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            std::string error;
            size_t written = 0;
            if (!DeviceMemory::Load(emulator->GetContext(), name, path, offset.value_or(0), written, error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(sol::make_object(s, static_cast<double>(written)));
            return out;
        });

        lua.set_function("memory_info", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table info = lua_view.create_table();
            if (!effectiveEmulator()) return info;
            Memory* mem = effectiveEmulator()->GetMemory();
            if (!mem) return info;
            
            sol::table pages = lua_view.create_table();
            pages["ram_count"] = MAX_RAM_PAGES;
            pages["rom_count"] = MAX_ROM_PAGES;
            pages["cache_count"] = MAX_CACHE_PAGES;
            pages["misc_count"] = MAX_MISC_PAGES;
            info["pages"] = pages;
            
            sol::table banks = lua_view.create_table();
            for (int bank = 0; bank < 4; bank++) {
                sol::table bankInfo = lua_view.create_table();
                bankInfo["bank"] = bank;
                bankInfo["start"] = bank * 0x4000;
                bankInfo["end"] = (bank + 1) * 0x4000 - 1;
                bankInfo["mapping"] = mem->GetCurrentBankName(bank);
                banks[bank + 1] = bankInfo;
            }
            info["z80_banks"] = banks;
            return info;
        });

        // Feature management (using correct FeatureManager API)
        // Same listFeatures() enumeration the CLI `feature` table and the
        // WebAPI /features endpoint use — keyed by feature id, so scripts
        // keep working as new features (fasttape, turbotape, …) register
        lua.set_function("feature_list", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table features = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (emulator) {
                FeatureManager* fm = emulator->GetFeatureManager();
                if (fm) {
                    for (const FeatureManager::FeatureInfo& feature : fm->listFeatures())
                        features[feature.id] = feature.enabled;
                }
            }
            return features;
        });

        lua.set_function("feature_get", [this](const std::string& name) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            FeatureManager* fm = emulator->GetFeatureManager();
            return fm ? fm->isEnabled(name) : false;
        });

        // ok, reason: the reason says why a known feature was refused (TTD holds it)
        lua.set_function("feature_set", [this](const std::string& name, bool enabled) -> std::tuple<bool, std::string> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return {false, "no emulator"};
            FeatureManager* fm = emulator->GetFeatureManager();
            if (!fm) return {false, "FeatureManager not available"};
            if (fm->setFeature(name, enabled)) return {true, ""};
            return {false, fm->hasFeature(name) ? fm->refusalReason(name, enabled) : "unknown feature: " + name};
        });

        // Disk inspection functions
        lua.set_function("disk_is_inserted", [this](int drive) -> bool {
            if (!effectiveEmulator() || drive < 0 || drive > 3) return false;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->coreState.diskDrives[drive]) return false;
            return ctx->coreState.diskDrives[drive]->isDiskInserted();
        });

        // region <Media: every slot through MediaControl (media-control-design.md)>
        // media_list(), media_info(slot), media_formats([kind]),
        // media_insert(slot, path [, opts]), media_swap(slot, path [, opts]),
        // media_eject(slot [, opts]), media_save(slot [, path] [, opts]),
        // media_export(slot, path), media_discard(slot [, opts]),
        // media_rescan(slot [, opts]), media_create(slot [, opts]),
        // media_protect(slot, on), and media(verb, slot, path, opts).
        // slot: fdd.b, B, b:, sd, floppy:1, tag:sd+neogs ("auto" for insert).
        // opts: {access="readonly", save=true, export="x.trd", discard=true, async=true, ...}.
        // Each returns the reply table every surface returns: ok, error, message,
        // slot, pending, revision, report, and the verb's fields (slots, info, ...)
        auto mediaCall = [this](sol::this_state s, const std::string& verb, const std::string& selector,
                                const std::string& path, sol::optional<sol::table> opts) -> sol::object {
            MediaRequest request;
            request.verb = verb;
            request.selector = selector;
            request.path = path;
            if (opts)
            {
                for (const auto& [key, value] : *opts)
                {
                    if (!key.is<std::string>())
                        continue;
                    std::string text;
                    if (value.is<bool>())
                        text = value.as<bool>() ? "true" : "false";
                    else if (value.get_type() == sol::type::number)
                    {
                        const double number = value.as<double>();
                        text = number == static_cast<double>(static_cast<long long>(number))
                                   ? std::to_string(static_cast<long long>(number))
                                   : std::to_string(number);
                    }
                    else if (value.is<std::string>())
                        text = value.as<std::string>();
                    request.options[key.as<std::string>()] = text;
                }
            }
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
            {
                StateNode none = StateNode::Object();
                none["ok"] = false;
                none["error"] = "unknown-emulator";
                none["message"] = "no emulator selected";
                return StateNodeToLua(s, none);
            }
            return StateNodeToLua(s, MediaControl(emulator->GetContext()).Execute(request).ToValue());
        };
        lua.set_function("media", [mediaCall](sol::this_state s, const std::string& verb, sol::optional<std::string> slot,
                                              sol::optional<std::string> path, sol::optional<sol::table> opts) {
            return mediaCall(s, verb, slot.value_or(""), path.value_or(""), opts);
        });
        lua.set_function("media_list", [mediaCall](sol::this_state s) { return mediaCall(s, "list", "", "", sol::nullopt); });
        lua.set_function("media_info", [mediaCall](sol::this_state s, const std::string& slot) {
            return mediaCall(s, "info", slot, "", sol::nullopt);
        });
        lua.set_function("media_formats", [mediaCall](sol::this_state s, sol::optional<std::string> kind) {
            sol::state_view view(s);
            sol::optional<sol::table> opts;
            if (kind)
            {
                sol::table t = view.create_table();
                t["kind"] = *kind;
                opts = t;
            }
            return mediaCall(s, "formats", "", "", opts);
        });
        // Where a file can go: what it is, the slots that take it (in a chooser's order), the default, the refusal
        lua.set_function("media_targets", [mediaCall](sol::this_state s, const std::string& path) {
            return mediaCall(s, "targets", "", path, sol::nullopt);
        });
        for (const char* verb : {"insert", "swap"})
        {
            lua.set_function(std::string("media_") + verb,
                             [mediaCall, verb = std::string(verb)](sol::this_state s, const std::string& slot, const std::string& path,
                                                                   sol::optional<sol::table> opts) {
                                 return mediaCall(s, verb, slot, path, opts);
                             });
        }
        for (const char* verb : {"eject", "discard", "rescan", "create"})
        {
            lua.set_function(std::string("media_") + verb,
                             [mediaCall, verb = std::string(verb)](sol::this_state s, const std::string& slot,
                                                                   sol::optional<sol::table> opts) {
                                 return mediaCall(s, verb, slot, "", opts);
                             });
        }
        lua.set_function("media_save", [mediaCall](sol::this_state s, const std::string& slot, sol::optional<std::string> path,
                                                   sol::optional<sol::table> opts) {
            return mediaCall(s, "save", slot, path.value_or(""), opts);
        });
        lua.set_function("media_export", [mediaCall](sol::this_state s, const std::string& slot, const std::string& path) {
            return mediaCall(s, "export", slot, path, sol::nullopt);
        });
        lua.set_function("media_protect", [mediaCall](sol::this_state s, const std::string& slot, bool on) {
            sol::state_view view(s);
            sol::table t = view.create_table();
            t["on"] = on;
            return mediaCall(s, "protect", slot, "", sol::optional<sol::table>(t));
        });
        // endregion <Media>

        lua.set_function("disk_get_path", [this](int drive) -> std::string {
            if (!effectiveEmulator() || drive < 0 || drive > 3) return "";
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx) return "";
            return ctx->coreState.diskFilePaths[drive];
        });

        lua.set_function("disk_eject", [this](int drive) -> bool {
            if (!effectiveEmulator() || drive < 0 || drive > 3) return false;
            return effectiveEmulator()->EjectDisk(static_cast<uint8_t>(drive), /*force*/ true);
        });

        // disk_load(path [, drive=0] [, autostart=false]) - insert a disk image (.trd/.scl/.fdi/.udi/...)
        // into the requested drive (0-3 / A-D). autostart quick-resets into TR-DOS and runs the disk,
        // matching the Qt UI's drag-and-drop autostart and the WebAPI's "autostart" insert flag/CLI's
        // "disk insert <drive> <file> autostart" - drive A (0) only, a TR-DOS/Beta 128 hardware
        // convention: requesting it for another drive is a hard failure (result.success=false,
        // result.message explains why), not silently ignored or redirected to drive A.
        lua.set_function("disk_load", [this](const std::string& path, sol::optional<int> driveOpt,
                                              sol::optional<bool> autostartOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
            {
                result["success"] = false;
                result["message"] = "no emulator";
                return result;
            }

            int drive = driveOpt.value_or(0);
            bool autostart = autostartOpt.value_or(false);
            if (drive < 0 || drive > 3)
            {
                result["success"] = false;
                result["message"] = "invalid drive " + std::to_string(drive) + " (valid range: 0-3 / A-D)";
                return result;
            }

            if (autostart)
            {
                Emulator::DiskAutostartResult r = emulator->AutostartDisk(path, static_cast<uint8_t>(drive));
                result["success"] = r.mounted;
                result["started"] = r.started;
                result["message"] = r.message;
            }
            else
            {
                std::string error;
                bool ok = emulator->LoadDisk(path, static_cast<uint8_t>(drive), &error);
                result["success"] = ok;
                result["started"] = false;
                result["message"] = ok ? "mounted" : error;
            }
            return result;
        });

        // disk_create(drive [, cylinders [, sides [, format]]]): format auto (plus3 on a +3, unformatted
        // elsewhere), unformatted or plus3; cylinders / sides 0 or left out = the format's geometry
        lua.set_function("disk_create", [this](int drive, sol::optional<int> cyl, sol::optional<int> sides,
                                               sol::optional<std::string> format) -> std::tuple<bool, std::string> {
            if (!effectiveEmulator()) return {false, "no emulator"};
            if (drive < 0 || drive > 3) return {false, "invalid drive (valid range: 0-3)"};
            Emulator::BlankDiskFormat parsed = Emulator::BlankDiskFormat::Auto;
            if (!Emulator::ParseBlankDiskFormat(format.value_or("auto"), parsed)) return {false, "unknown disk format"};
            const int cylinders = cyl.value_or(0);
            const int numSides = sides.value_or(0);
            if (cylinders < 0 || cylinders > 255 || numSides < 0 || numSides > 255)
                return {false, "cylinders must be 40 or 80, sides 1 or 2"};
            std::string error;  // carries the TTD refusal while recording
            const bool ok = effectiveEmulator()->CreateBlankDisk(static_cast<uint8_t>(drive), parsed,
                                                                 static_cast<uint8_t>(cylinders),
                                                                 static_cast<uint8_t>(numSides), &error);
            return {ok, ok ? std::string() : error};
        });

        lua.set_function("disk_list", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table drives = lua_view.create_table();
            if (effectiveEmulator()) {
                auto* ctx = effectiveEmulator()->GetContext();
                if (ctx) {
                    for (int i = 0; i < 4; i++) {
                        sol::table drive = lua_view.create_table();
                        drive["id"] = i;
                        drive["letter"] = std::string(1, 'A' + i);
                        drive["inserted"] = ctx->coreState.diskDrives[i] && 
                                           ctx->coreState.diskDrives[i]->isDiskInserted();
                        drive["path"] = ctx->coreState.diskFilePaths[i];
                        drives[i + 1] = drive;
                    }
                }
            }
            return drives;
        });

        // Execution control
        lua.set_function("step", [this](sol::optional<bool> skipBP) {
            if (!effectiveEmulator()) return;
            effectiveEmulator()->RunSingleCPUCycle(skipBP.value_or(true));
        });

        lua.set_function("steps", [this](unsigned count, sol::optional<bool> skipBP) {
            if (!effectiveEmulator()) return;
            effectiveEmulator()->RunNCPUCycles(count, skipBP.value_or(false));
        });

        lua.set_function("stepover", [this]() {
            if (!effectiveEmulator()) return;
            effectiveEmulator()->StepOver();
        });

        // Frame stepping methods
        lua.set_function("run_frame", [this](sol::optional<bool> skipBP) {
            if (!effectiveEmulator()) return;
            effectiveEmulator()->RunFrame(skipBP.value_or(true));
        });

        lua.set_function("run_frames", [this](unsigned count, sol::optional<bool> skipBP) {
            if (!effectiveEmulator()) return;
            effectiveEmulator()->RunNFrames(count, skipBP.value_or(true));
        });

        // Atomic stepping methods
        lua.set_function("run_tstates", [this](unsigned count, sol::optional<bool> skipBP) {
            if (!effectiveEmulator()) return;
            effectiveEmulator()->RunTStates(count, skipBP.value_or(true));
        });

        lua.set_function("run_to_scanline", [this](unsigned scanline, sol::optional<bool> skipBP) {
            if (!effectiveEmulator()) return;
            effectiveEmulator()->RunUntilScanline(scanline, skipBP.value_or(true));
        });

        lua.set_function("run_scanlines", [this](unsigned count, sol::optional<bool> skipBP) {
            if (!effectiveEmulator()) return;
            effectiveEmulator()->RunNScanlines(count, skipBP.value_or(true));
        });

        lua.set_function("run_to_pixel", [this](sol::optional<bool> skipBP) {
            if (!effectiveEmulator()) return;
            effectiveEmulator()->RunUntilNextScreenPixel(skipBP.value_or(true));
        });

        lua.set_function("run_to_interrupt", [this](sol::optional<bool> skipBP) {
            if (!effectiveEmulator()) return;
            effectiveEmulator()->RunUntilInterrupt(skipBP.value_or(true));
        });

        lua.set_function("run_until_condition", [this](sol::function predicate, sol::optional<unsigned> maxTStates) {
            if (!effectiveEmulator()) return;
            effectiveEmulator()->RunUntilCondition([&predicate](const Z80State& state) -> bool {
                return predicate(state.pc, state.af, state.bc, state.de, state.hl).get<bool>();
            }, maxTStates.value_or(0));
        });

        // Keyboard operations. Same `key` name space as WebAPI/CLI/Python
        // (DebugKeyboardManager::ResolveKeyName for ZX matrix keys, falling
        // back to pckey::FromName for a PC-only key with no ZX equivalent at
        // all - e.g. "f12", "rshift"; combos accept a mix of both, needed for
        // TS-Conf's BIOS Setup entry, Right Shift + F12)
        // key_route("auto"|"matrix"|"ps2"|"both") -> route in force | nil, err; key_route() queries
        lua.set_function("key_route", [this](sol::this_state s, sol::optional<std::string> route) -> sol::variadic_results {
            sol::variadic_results out;
            Emulator* emulator = effectiveEmulator();
            Keyboard* keyboard = emulator ? emulator->GetContext()->pKeyboard : nullptr;
            if (!keyboard)
            {
                out.push_back(sol::make_object(s, sol::lua_nil));
                out.push_back(sol::make_object(s, std::string("no keyboard")));
                return out;
            }
            std::string error;
            if (route && !route->empty() && !keyboard->RequestHostRoute(*route, error))
            {
                out.push_back(sol::make_object(s, sol::lua_nil));
                out.push_back(sol::make_object(s, error));
                return out;
            }
            out.push_back(sol::make_object(s, std::string(Keyboard::HostRouteName(keyboard->EffectiveHostRoute()))));
            return out;
        });

        // keyboard_controller() -> the PS/2 / XT keyboard controller's name ("PROFI-XT firmware 1.27", ...), "" when the
        // machine has none or it has no name (GET .../keyboard/status keyboard_controller)
        lua.set_function("keyboard_controller", [this]() -> std::string {
            Emulator* emulator = effectiveEmulator();
            EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
            Keyboard* keyboard = ctx ? ctx->pKeyboard : nullptr;
            return keyboard && keyboard->HasPs2Sink() ? keyboard->GetPs2Sink()->ControllerName() : std::string();
        });

        lua.set_function("key_tap", [this](const std::string& keyName, sol::optional<uint16_t> holdFrames) -> bool {
            Emulator* emulator = effectiveEmulator();
            EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
            if (!ctx || !ctx->pDebugManager->GetKeyboardManager())
                return false;
            ctx->pDebugManager->GetKeyboardManager()->TapKey(keyName, holdFrames.value_or(2));
            return true;
        });

        lua.set_function("key_press", [this](const std::string& keyName) -> bool {
            Emulator* emulator = effectiveEmulator();
            EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
            if (!ctx || !ctx->pDebugManager->GetKeyboardManager())
                return false;
            ctx->pDebugManager->GetKeyboardManager()->PressKey(keyName);
            return true;
        });

        lua.set_function("key_release", [this](const std::string& keyName) -> bool {
            Emulator* emulator = effectiveEmulator();
            EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
            if (!ctx || !ctx->pDebugManager->GetKeyboardManager())
                return false;
            ctx->pDebugManager->GetKeyboardManager()->ReleaseKey(keyName);
            return true;
        });

        lua.set_function("key_combo", [this](const std::vector<std::string>& keyNames, sol::optional<uint16_t> holdFrames) -> bool {
            Emulator* emulator = effectiveEmulator();
            EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
            if (!ctx || !ctx->pDebugManager->GetKeyboardManager())
                return false;
            ctx->pDebugManager->GetKeyboardManager()->TapCombo(keyNames, holdFrames.value_or(2));
            return true;
        });

        lua.set_function("key_macro", [this](const std::string& macroName) -> bool {
            Emulator* emulator = effectiveEmulator();
            EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
            if (!ctx || !ctx->pDebugManager->GetKeyboardManager())
                return false;
            return ctx->pDebugManager->GetKeyboardManager()->ExecuteNamedSequence(macroName);
        });

        lua.set_function("key_type", [this](const std::string& text, sol::optional<uint16_t> delayFrames) -> bool {
            Emulator* emulator = effectiveEmulator();
            EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
            if (!ctx || !ctx->pDebugManager->GetKeyboardManager())
                return false;
            ctx->pDebugManager->GetKeyboardManager()->TypeText(text, delayFrames.value_or(2));
            return true;
        });

        lua.set_function("key_release_all", [this]() {
            Emulator* emulator = effectiveEmulator();
            EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
            if (ctx && ctx->pDebugManager->GetKeyboardManager())
                ctx->pDebugManager->GetKeyboardManager()->ReleaseAllKeys();
        });

        // Tape operations
        // ok, reason: the reason is set when TTD refuses the insert (recording)
        lua.set_function("tape_load", [this](const std::string& path) -> std::tuple<bool, std::string> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return {false, "no emulator"};
            if (std::string refusal = emulator->RecordingGuard(ttd::TTDGuardedAction::LoadTape); !refusal.empty())
                return {false, refusal};
            std::string reason;
            const bool loaded = emulator->LoadTape(path, &reason);
            return {loaded, reason};
        });

        lua.set_function("tape_is_inserted", [this]() -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            return ctx && ctx->pTape && !ctx->coreState.tapeFilePath.empty();
        });

        lua.set_function("tape_get_path", [this]() -> std::string {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return "";
            auto* ctx = emulator->GetContext();
            return ctx ? ctx->coreState.tapeFilePath : "";
        });

        lua.set_function("tape_play", [this]() -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTape) return false;

            EmulatorPauseBracket bracket(emulator);

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
        });

        lua.set_function("tape_stop", [this]() -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (ctx && ctx->pTape) {
                ctx->pTape->stopTape();
                return true;
            }
            return false;
        });

        lua.set_function("tape_rewind", [this]() -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTape) return false;

            EmulatorPauseBracket bracket(emulator);

            // Rewind keeps the image and catalog — unlike stop/eject
            // (same semantics as `tape rewind` / POST /tape/rewind)
            ctx->pTape->EnsureImageLoaded();
            ctx->pTape->RewindToStart();
            return true;
        });

        lua.set_function("tape_eject", [this]() -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            return emulator->EjectTape();
        });

        lua.set_function("tape_pause", [this]() -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTape) return false;

            EmulatorPauseBracket bracket(emulator);

            const TapePlaybackState state = ctx->pTape->GetPlaybackState();
            if (state == TapePlaybackState::Paused)
                return true;  // idempotent, mirrors "Tape already paused"
            if (state != TapePlaybackState::Playing)
                return false;
            ctx->pTape->pausePlayback();  // play resumes in place afterwards
            return true;
        });

        lua.set_function("tape_seek", [this](int blockIndex) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator || blockIndex < 0) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTape) return false;

            EmulatorPauseBracket bracket(emulator);

            if (!ctx->pTape->EnsureImageLoaded())
                return false;
            return ctx->pTape->SeekToBlock(static_cast<size_t>(blockIndex));
        });

        lua.set_function("tape_pos", [this]() -> sol::optional<sol::table> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::nullopt;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTape || !ctx->pTape->EnsureImageLoaded()) return sol::nullopt;

            EmulatorPauseBracket bracket(emulator);

            sol::state_view lua_view(*_lua);
            sol::table pos = lua_view.create_table();
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
        });

        lua.set_function("tape_blocks", [this]() -> sol::optional<sol::table> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::nullopt;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTape || !ctx->pTape->EnsureImageLoaded()) return sol::nullopt;

            EmulatorPauseBracket bracket(emulator);

            const std::vector<TapeBlockDescriptor>& catalog = ctx->pTape->GetBlockCatalog();
            const TapeFastLoadPlan& plan = ctx->pTape->GetFastLoadPlan();

            sol::state_view lua_view(*_lua);
            sol::table blocks = lua_view.create_table(static_cast<int>(catalog.size()));
            size_t arrayIndex = 1;
            for (const TapeBlockDescriptor& descriptor : catalog)
            {
                sol::table block = lua_view.create_table();
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

                sol::table speed = lua_view.create_table();
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

                blocks[arrayIndex++] = block;
            }
            return blocks;
        });

        lua.set_function("tape_info", [this]() -> sol::optional<sol::table> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::nullopt;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTape) return sol::nullopt;

            EmulatorPauseBracket bracket(emulator);

            const std::string& path = ctx->coreState.tapeFilePath;
            const bool loaded = !path.empty() && ctx->pTape->EnsureImageLoaded();

            sol::state_view lua_view(*_lua);
            sol::table info = lua_view.create_table();
            info["status"] = loaded ? "loaded" : (path.empty() ? "empty" : "error");
            info["file"] = path;
            info["state"] = loaded ? getTapePlaybackStateName(ctx->pTape->GetPlaybackState()) : "idle";
            if (!loaded) return info;

            const TapeFastLoadPlan& plan = ctx->pTape->GetFastLoadPlan();
            info["format"] = ctx->pTape->GetLoadedFormatId();
            info["cursor"] = ctx->pTape->GetConsumptionCursor();
            info["block_count"] = ctx->pTape->GetBlockCatalog().size();
            info["total_seconds"] = plan.totalSeconds;

            FeatureManager* fm = emulator->GetFeatureManager();
            info["fast_tape"] = fm && fm->isEnabled(Features::kFastTape);
            info["turbo_tape"] = fm && fm->isEnabled(Features::kTurboTape);

            sol::table fastLoad = lua_view.create_table();
            fastLoad["verdict"] = getFastLoadVerdictName(plan.verdict);
            fastLoad["eligible_blocks"] = plan.eligibleBlocks;
            fastLoad["accelerated_seconds"] = plan.acceleratedSeconds;
            fastLoad["total_seconds"] = plan.totalSeconds;
            fastLoad["summary"] = plan.summary;
            info["fast_load"] = fastLoad;
            return info;
        });

        // Tape audio bridge: pure path-to-path conversions through the same
        // engine as `tape render` / POST /tape/render — no emulator state involved
        lua.set_function("tape_render",
            [this](const std::string& source, const std::string& output, sol::optional<sol::table> options) -> sol::table {
            TapeRenderRequest request;
            request.sourcePath = source;
            request.outputPath = output;
            if (options.has_value())
            {
                sol::optional<size_t> firstBlock = options.value()["first_block"];
                sol::optional<size_t> lastBlock = options.value()["last_block"];
                sol::optional<uint32_t> sampleRate = options.value()["sample_rate"];
                sol::optional<double> amplitude = options.value()["amplitude"];
                sol::optional<bool> invertLevel = options.value()["invert_level"];
                if (firstBlock.has_value()) request.firstBlock = firstBlock.value();
                if (lastBlock.has_value()) request.lastBlock = lastBlock.value();
                if (sampleRate.has_value()) request.sampleRate = sampleRate.value();
                if (amplitude.has_value()) request.amplitude = amplitude.value();
                if (invertLevel.has_value()) request.invertLevel = invertLevel.value();
            }

            TapeRenderResult result = RenderTapeToAudio(request);

            sol::state_view lua_view(*_lua);
            sol::table ret = lua_view.create_table();
            ret["ok"] = result.ok;
            ret["error"] = result.errorText;
            ret["duration_sec"] = result.durationSec;
            ret["samples"] = result.samplesWritten;
            ret["blocks"] = result.blocksRendered;
            ret["encoder"] = result.encoderUsed;
            sol::table warnings = lua_view.create_table();
            int warningIndex = 1;
            for (const std::string& warning : result.warnings)
                warnings[warningIndex++] = warning;
            ret["warnings"] = warnings;
            return ret;
        });

        // Same engine as `tape import` / POST /tape/import: decode + extract
        // + recognize, then an extension-dispatched save (.tzx exact, .tap gated)
        lua.set_function("tape_import",
            [this](const std::string& source, const std::string& output, sol::optional<double> hysteresis) -> sol::table {
            TapeImportRequest request;
            request.sourcePath = source;
            if (hysteresis.has_value())
                request.hysteresis = hysteresis.value();

            TapeImportResult imported = ImportAudioToTape(request);
            TapeSaveResult saved;
            if (imported.ok)
                saved = SaveTapeImage(imported.image, output);

            sol::state_view lua_view(*_lua);
            sol::table ret = lua_view.create_table();
            ret["ok"] = imported.ok && saved.ok;
            ret["error"] = !imported.ok ? imported.errorText : saved.errorText;
            ret["decoder"] = imported.decoderUsed;
            ret["sample_rate"] = imported.sampleRate;
            ret["samples_decoded"] = imported.samplesDecoded;
            ret["signal_edges"] = imported.signalEdges;
            ret["blocks_recognized"] = imported.blocksRecognized;
            ret["blocks_written"] = saved.blocksWritten;
            ret["output_path"] = output;
            sol::table warnings = lua_view.create_table();
            int warningIndex = 1;
            for (const std::string& warning : imported.warnings)
                warnings[warningIndex++] = warning;
            ret["warnings"] = warnings;
            return ret;
        });

        // Mouse injection (Kempston Mouse, automation-interfaces §4.7)
        // Target: effectiveEmulator() (bound instance, else the selected one).
        // Success returns the state table; failure returns nil, "message".
        lua.set_function("mouse_move", [this](sol::this_state s, sol::object dxArg, sol::object dyArg) {
            DebugMouseManager* mgr = mouseManager();
            if (!mgr)
                return mouseError(s, "mouse manager not available");
            std::string error;
            long long dx = 0;
            long long dy = 0;
            if (!mouseIntArg(dxArg, "dx", INT_MIN, INT_MAX, dx, error) ||
                !mouseIntArg(dyArg, "dy", INT_MIN, INT_MAX, dy, error))
                return mouseError(s, error);
            return mouseResult(s, *mgr, mgr->Move(static_cast<int>(dx), static_cast<int>(dy)));
        });

        lua.set_function("mouse_press", [this](sol::this_state s, sol::object buttonArg) {
            DebugMouseManager* mgr = mouseManager();
            if (!mgr)
                return mouseError(s, "mouse manager not available");
            std::string error;
            auto button = mouseButtonArg(buttonArg, error);
            if (!button)
                return mouseError(s, error);
            return mouseResult(s, *mgr, mgr->PressButton(*button));
        });

        lua.set_function("mouse_release", [this](sol::this_state s, sol::object buttonArg) {
            DebugMouseManager* mgr = mouseManager();
            if (!mgr)
                return mouseError(s, "mouse manager not available");
            std::string error;
            auto button = mouseButtonArg(buttonArg, error);
            if (!button)
                return mouseError(s, error);
            return mouseResult(s, *mgr, mgr->ReleaseButton(*button));
        });

        lua.set_function("mouse_click", [this](sol::this_state s, sol::object buttonArg, sol::object framesArg) {
            DebugMouseManager* mgr = mouseManager();
            if (!mgr)
                return mouseError(s, "mouse manager not available");
            std::string error;
            auto button = mouseButtonArg(buttonArg, error);
            if (!button)
                return mouseError(s, error);
            long long frames = DebugMouseManager::DEFAULT_CLICK_FRAMES;
            if (framesArg.valid() && framesArg.get_type() != sol::type::lua_nil &&
                !mouseIntArg(framesArg, "frames", LLONG_MIN, LLONG_MAX, frames, error))
                return mouseError(s, error);
            if (frames < 0 || frames > static_cast<long long>(UINT32_MAX))
                return mouseError(s, "frames=" + std::to_string(frames) + " out of range 1.." +
                                         std::to_string(DebugMouseManager::MAX_CLICK_FRAMES));
            return mouseResult(s, *mgr, mgr->Click(*button, static_cast<uint32_t>(frames)));
        });

        lua.set_function("mouse_buttons", [this](sol::this_state s, sol::object pressedArg) {
            DebugMouseManager* mgr = mouseManager();
            if (!mgr)
                return mouseError(s, "mouse manager not available");
            uint8_t bits = 0;
            if (pressedArg.get_type() != sol::type::lua_nil)
            {
                if (pressedArg.get_type() != sol::type::table)
                    return mouseError(s, "pressed must be a table of button names ({} = none)");
                sol::table pressed = pressedArg.as<sol::table>();
                std::string error;
                for (size_t i = 1; i <= pressed.size(); ++i)
                {
                    sol::object item = pressed[i];
                    auto button = mouseButtonArg(item, error);
                    if (!button)
                        return mouseError(s, error);
                    bits |= static_cast<uint8_t>(*button);
                }
            }
            return mouseResult(s, *mgr, mgr->SetPressedButtons(bits));
        });

        lua.set_function("mouse_wheel", [this](sol::this_state s, sol::object stepsArg) {
            DebugMouseManager* mgr = mouseManager();
            if (!mgr)
                return mouseError(s, "mouse manager not available");
            std::string error;
            long long steps = 0;
            if (!mouseIntArg(stepsArg, "steps", INT_MIN, INT_MAX, steps, error))
                return mouseError(s, error);
            return mouseResult(s, *mgr, mgr->Wheel(static_cast<int>(steps)));
        });

        lua.set_function("mouse_release_all", [this](sol::this_state s) {
            DebugMouseManager* mgr = mouseManager();
            if (!mgr)
                return mouseError(s, "mouse manager not available");
            return mouseResult(s, *mgr, mgr->ReleaseAllButtons());
        });

        lua.set_function("mouse_set_counters", [this](sol::this_state s, sol::object xArg, sol::object yArg) {
            DebugMouseManager* mgr = mouseManager();
            if (!mgr)
                return mouseError(s, "mouse manager not available");
            std::string error;
            long long x = 0;
            long long y = 0;
            if (!mouseIntArg(xArg, "x", INT_MIN, INT_MAX, x, error) ||
                !mouseIntArg(yArg, "y", INT_MIN, INT_MAX, y, error))
                return mouseError(s, error);
            return mouseResult(s, *mgr, mgr->SetCounters(static_cast<int>(x), static_cast<int>(y)));
        });

        lua.set_function("mouse_status", [this](sol::this_state s) {
            DebugMouseManager* mgr = mouseManager();
            if (!mgr)
                return mouseError(s, "mouse manager not available");
            const MouseStateSnapshot state = mgr->GetState();
            if (!state.available)
                return mouseError(s, "Mouse device not available");
            sol::variadic_results results;
            sol::table table = mouseStateTable(s, state);
            // Routing mirrors GET /mouse/status: `present` alone cannot distinguish
            // "not fitted" from "fitted but shadowed" (mouse design Q4 / gap D-1)
            Emulator* emulator = effectiveEmulator();
            EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
            if (context && context->pPortDecoder)
            {
                bool decoded = false;
                std::string note;
                context->pPortDecoder->GetMouseRoutingState(decoded, note);
                sol::state_view lua(s);
                sol::table routing = lua.create_table();
                routing["ports_decoded"] = decoded;
                routing["note"] = note;
                table["routing"] = routing;
            }
            results.push_back(sol::make_object(s, table));
            return results;
        });

        lua.set_function("mouse_click_pending", [this]() -> bool {
            DebugMouseManager* mgr = mouseManager();
            return mgr && mgr->IsClickPending();
        });

        lua.set_function("mouse_button_names", []() -> sol::as_table_t<std::vector<std::string>> {
            return sol::as_table(DebugMouseManager::GetAllButtonNames());
        });

        // Joystick injection (Kempston joystick, joystick TDD §5). Same contract as the mouse functions:
        // success returns the state table (same keys as GET /joystick), failure returns nil, "message".
        lua.set_function("joystick_press", [this](sol::this_state s, sol::object buttonsArg) {
            DebugJoystickManager* mgr = joystickManager();
            if (!mgr)
                return mouseError(s, "joystick manager not available");
            std::string names;
            std::string error;
            if (!joystickNamesArg(buttonsArg, names, error))
                return mouseError(s, error);
            return joystickResult(s, *mgr, mgr->Press(names));
        });

        lua.set_function("joystick_release", [this](sol::this_state s, sol::object buttonsArg) {
            DebugJoystickManager* mgr = joystickManager();
            if (!mgr)
                return mouseError(s, "joystick manager not available");
            std::string names;
            std::string error;
            if (!joystickNamesArg(buttonsArg, names, error))
                return mouseError(s, error);
            return joystickResult(s, *mgr, mgr->Release(names));
        });

        // joystick_set(0xE5) sets the raw byte; joystick_set({"up", "fire"}) / joystick_set("up+fire") the held set; {} releases all
        lua.set_function("joystick_set", [this](sol::this_state s, sol::object stateArg) {
            DebugJoystickManager* mgr = joystickManager();
            if (!mgr)
                return mouseError(s, "joystick manager not available");
            std::string error;
            if (stateArg.get_type() == sol::type::number)
            {
                long long state = 0;
                if (!mouseIntArg(stateArg, "state", LLONG_MIN, LLONG_MAX, state, error))
                    return mouseError(s, error);
                return joystickResult(s, *mgr, mgr->SetStateChecked(state));
            }
            std::string names;
            if (!joystickNamesArg(stateArg, names, error))
                return mouseError(s, "state must be an integer, a string or a table of button names");
            if (names.empty())
                return joystickResult(s, *mgr, mgr->ReleaseAll());
            const uint8_t mask = DebugJoystickManager::ResolveButtonNames(names);
            if (mask == 0)  // the manager words the unknown-name error
                return joystickResult(s, *mgr, mgr->Press(names));
            return joystickResult(s, *mgr, mgr->SetStateChecked(mask));
        });

        lua.set_function("joystick_tap", [this](sol::this_state s, sol::object buttonsArg, sol::object framesArg) {
            DebugJoystickManager* mgr = joystickManager();
            if (!mgr)
                return mouseError(s, "joystick manager not available");
            std::string names;
            std::string error;
            if (!joystickNamesArg(buttonsArg, names, error))
                return mouseError(s, error);
            long long frames = DebugJoystickManager::DEFAULT_TAP_FRAMES;
            if (framesArg.valid() && framesArg.get_type() != sol::type::lua_nil &&
                !mouseIntArg(framesArg, "frames", LLONG_MIN, LLONG_MAX, frames, error))
                return mouseError(s, error);
            return joystickResult(s, *mgr, mgr->TapChecked(names, frames));
        });

        lua.set_function("joystick_state", [this](sol::this_state s) {
            DebugJoystickManager* mgr = joystickManager();
            if (!mgr)
                return mouseError(s, "joystick manager not available");
            const JoystickStateSnapshot state = mgr->GetState();
            if (!state.available)
                return mouseError(s, "Joystick device not available");
            sol::variadic_results results;
            results.push_back(sol::make_object(s, joystickStateTable(s, state)));
            return results;
        });

        lua.set_function("joystick_tap_pending", [this]() -> bool {
            DebugJoystickManager* mgr = joystickManager();
            return mgr && mgr->IsTapPending();
        });

        lua.set_function("joystick_button_names", []() -> sol::as_table_t<std::vector<std::string>> {
            return sol::as_table(DebugJoystickManager::GetAllButtonNames());
        });

        // Snapshot operations
        // ok, reason, emulator_id: the reason is set when TTD refuses the load
        // (recording) or the load fails. A file that needs another model (an
        // SPG: TS-Conf) switches it first when this Lua is not bound to one
        // machine (as rzx_play); emulator_id is then the new instance
        lua.set_function("snapshot_load", [this](const std::string& path) -> std::tuple<bool, std::string, std::string> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return {false, "no emulator", ""};
            if (std::string refusal = emulator->RecordingGuard(ttd::TTDGuardedAction::LoadSnapshot); !refusal.empty())
                return {false, refusal, emulator->GetId()};
            SnapshotLoadRequest request;
            request.emulatorId = emulator->GetId();
            request.path = path;
            request.switchModel = _emulator == nullptr;
            const SnapshotLoadResult result = SnapshotLauncher::Load(request);
            return {result.ok, result.message, result.emulator ? result.emulator->GetId() : std::string()};
        });

        // RZX input recordings. rzx_play(path [, options]) -> table {ok, message,
        // emulator_id, model_switched, required_model, summary}; options:
        // desync_mode ("strict" / "tolerant"), ei_short_frame_blocks_int,
        // ld_air_parity_quirk, ignore_later_snapshots, switch_model. The model
        // switches only when this interpreter follows the selected machine (a
        // bound one reports the mismatch: its machine must not be replaced)
        lua.set_function("rzx_play", [this](const std::string& path, sol::optional<sol::table> options) -> sol::table {
            sol::state_view view(*_lua);
            sol::table out = view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
            {
                out["ok"] = false;
                out["message"] = "no emulator";
                return out;
            }
            rzx::LaunchRequest request;
            request.emulatorId = emulator->GetId();
            request.path = path;
            request.switchModel = _emulator == nullptr;
            if (options)
            {
                const sol::table& o = *options;
                sol::optional<std::string> mode = o["desync_mode"];
                if (mode && !rzx::RzxLauncher::ParseDesyncMode(*mode, request.options.desyncMode))
                {
                    out["ok"] = false;
                    out["message"] = "desync_mode '" + *mode + "': expected strict or tolerant";
                    return out;
                }
                request.options.eiShortFrameBlocksInt = o.get_or("ei_short_frame_blocks_int", false);
                request.options.ldAirParityQuirk = o.get_or("ld_air_parity_quirk", false);
                request.options.ignoreLaterSnapshots = o.get_or("ignore_later_snapshots", false);
                request.switchModel = request.switchModel && o.get_or("switch_model", true);
            }
            const rzx::LaunchResult result = rzx::RzxLauncher::Play(request);
            out["ok"] = result.play.Ok();
            out["error"] = std::string(rzx::PlayErrorName(result.play.error));
            out["message"] = result.play.message;
            out["emulator_id"] = result.emulator ? result.emulator->GetId() : std::string();
            out["model_switched"] = result.modelSwitched;
            out["required_model"] = result.play.requiredModel;
            out["model"] = result.switchedToModel;
            if (result.emulator)
                out["summary"] = rzx::RzxLauncher::StatusLine(result.emulator->GetRzxStatus());
            return out;
        });

        // rzx_seek(frame) -> ok, reason: to the boundary after `frame` frames
        lua.set_function("rzx_seek", [this](uint64_t frame) -> std::tuple<bool, std::string> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return {false, "no emulator"};
            std::string error;
            const bool ok = emulator->SeekRzx(frame, &error);
            return {ok, error};
        });

        lua.set_function("rzx_stop", [this]() -> bool {
            Emulator* emulator = effectiveEmulator();
            return emulator && emulator->StopRzx();
        });

        lua.set_function("rzx_status", [this]() -> sol::table {
            sol::state_view view(*_lua);
            sol::table t = view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
            {
                t["loaded"] = false;
                return t;
            }
            const rzx::SessionStatus status = emulator->GetRzxStatus();
            t["loaded"] = status.loaded;
            t["active"] = status.active;
            t["summary"] = rzx::RzxLauncher::StatusLine(status);
            if (!status.loaded)
                return t;
            const rzx::PlayerStatus& player = status.player;
            t["path"] = status.path;
            t["creator"] = status.creator;
            t["state"] = std::string(rzx::StateName(player.state));
            t["frame"] = player.frame;
            t["total_frames"] = player.totalFrames;
            t["block"] = player.block + 1;
            t["blocks"] = player.blocks;
            t["interrupts"] = player.interrupts;
            t["desyncs"] = player.desyncs;
            t["drift"] = player.drift;
            t["max_drift"] = player.maxDrift;
            t["snapshots_applied"] = player.snapshotsApplied;
            t["keyframes"] = player.keyframes;
            t["keyframe_bytes"] = player.keyframeBytes;
            t["reason"] = player.stopReason;
            if (player.desyncs > 0)
            {
                sol::table first = view.create_table();
                first["kind"] = std::string(rzx::DesyncName(player.firstDesync.kind));
                first["frame"] = player.firstDesync.frame;
                first["expected"] = player.firstDesync.expected;
                first["actual"] = player.firstDesync.actual;
                first["pc"] = player.firstDesync.pc;
                t["first_desync"] = first;
            }
            return t;
        });

        lua.set_function("snapshot_save", [this](const std::string& path) -> bool {
            if (!effectiveEmulator()) return false;
            return effectiveEmulator()->SaveSnapshot(path);
        });

        // Breakpoint management
        lua.set_function("bp", [this](uint16_t addr) -> int {
            if (!effectiveEmulator()) return -1;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pDebugManager) return -1;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? static_cast<int>(bpm->AddExecutionBreakpoint(addr)) : -1;
        });

        lua.set_function("bp_read", [this](uint16_t addr) -> int {
            if (!effectiveEmulator()) return -1;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pDebugManager) return -1;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? static_cast<int>(bpm->AddMemReadBreakpoint(addr)) : -1;
        });

        lua.set_function("bp_write", [this](uint16_t addr) -> int {
            if (!effectiveEmulator()) return -1;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pDebugManager) return -1;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? static_cast<int>(bpm->AddMemWriteBreakpoint(addr)) : -1;
        });

        lua.set_function("bp_port_in", [this](uint16_t port) -> int {
            if (!effectiveEmulator()) return -1;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pDebugManager) return -1;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? static_cast<int>(bpm->AddPortInBreakpoint(port)) : -1;
        });

        lua.set_function("bp_port_out", [this](uint16_t port) -> int {
            if (!effectiveEmulator()) return -1;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pDebugManager) return -1;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? static_cast<int>(bpm->AddPortOutBreakpoint(port)) : -1;
        });

        lua.set_function("bp_remove", [this](uint16_t id) -> bool {
            if (!effectiveEmulator()) return false;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pDebugManager) return false;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? bpm->RemoveBreakpointByID(id) : false;
        });

        lua.set_function("bp_clear", [this]() {
            if (!effectiveEmulator()) return;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pDebugManager) return;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            if (bpm) bpm->ClearBreakpoints();
        });

        lua.set_function("bp_enable", [this](uint16_t id) -> bool {
            if (!effectiveEmulator()) return false;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pDebugManager) return false;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? bpm->ActivateBreakpoint(id) : false;
        });

        lua.set_function("bp_disable", [this](uint16_t id) -> bool {
            if (!effectiveEmulator()) return false;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pDebugManager) return false;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? bpm->DeactivateBreakpoint(id) : false;
        });

        lua.set_function("bp_count", [this]() -> size_t {
            if (!effectiveEmulator()) return 0;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pDebugManager) return 0;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? bpm->GetBreakpointsCount() : 0;
        });

        lua.set_function("bp_list", [this]() -> std::string {
            if (!effectiveEmulator()) return "";
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pDebugManager) return "";
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? bpm->GetBreakpointListAsString() : "";
        });

        lua.set_function("bp_status", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!effectiveEmulator()) {
                result["valid"] = false;
                return result;
            }
            auto* ctx = effectiveEmulator()->GetContext();
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
            }
            return result;
        });

        lua.set_function("bp_clear_last", [this]() {
            if (!effectiveEmulator()) return;
            auto* ctx = effectiveEmulator()->GetContext();
            if (ctx && ctx->pDebugManager) {
                BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
                if (bpm) bpm->ClearLastTriggeredBreakpoint();
            }
        });

        // Labels/Symbols
        lua.set_function("label_get", [this](const std::string& name) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return result;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return result;
            LabelManager* lm = ctx->pDebugManager->GetLabelManager();
            auto label = lm ? lm->GetLabelByName(name) : nullptr;
            if (!label) return result;
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
        });

        lua.set_function("label_at", [this](uint16_t address) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return result;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return result;
            LabelManager* lm = ctx->pDebugManager->GetLabelManager();
            auto label = lm ? lm->GetLabelByZ80Address(address) : nullptr;
            if (!label) return result;
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
        });

        lua.set_function("label_add", [this](const std::string& name, uint16_t address,
                                             sol::optional<std::string> type,
                                             sol::optional<std::string> module,
                                             sol::optional<std::string> comment) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return false;
            LabelManager* lm = ctx->pDebugManager->GetLabelManager();
            return lm && lm->AddLabel(name, address, UINT16_MAX, UINT16_MAX,
                                      type.value_or(""), module.value_or(""), comment.value_or(""));
        });

        lua.set_function("label_remove", [this](const std::string& name) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return false;
            LabelManager* lm = ctx->pDebugManager->GetLabelManager();
            return lm && lm->RemoveLabel(name);
        });

        lua.set_function("label_count", [this]() -> int {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return 0;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return 0;
            LabelManager* lm = ctx->pDebugManager->GetLabelManager();
            return lm ? static_cast<int>(lm->GetLabelCount()) : 0;
        });

        lua.set_function("labels_list", [this](sol::optional<std::string> module,
                                               sol::optional<std::string> type) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return result;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return result;
            LabelManager* lm = ctx->pDebugManager->GetLabelManager();
            if (!lm) return result;

            LabelManager::LabelFilter filter;
            if (module.has_value()) filter.module = module.value();
            if (type.has_value()) filter.type = type.value();

            auto labels = lm->GetLabels(filter);
            int idx = 1;
            for (const auto& label : labels) {
                sol::table lbl = lua_view.create_table();
                lbl["name"] = label->name;
                lbl["address"] = label->address;
                if (label->bank != UINT16_MAX) lbl["bank"] = label->bank;
                lbl["type"] = label->type;
                lbl["module"] = label->module;
                lbl["active"] = label->active;
                result[idx++] = lbl;
            }
            return result;
        });

        lua.set_function("labels_clear", [this]() {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return;
            LabelManager* lm = ctx->pDebugManager->GetLabelManager();
            if (lm) lm->ClearAllLabels();
        });

        lua.set_function("symbols_load", [this](const std::string& path) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return false;
            LabelManager* lm = ctx->pDebugManager->GetLabelManager();
            return lm && lm->LoadLabels(path);
        });

        lua.set_function("symbols_save", [this](const std::string& path) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return false;
            LabelManager* lm = ctx->pDebugManager->GetLabelManager();
            return lm && lm->SaveLabels(path);
        });

        // Disassembly
        lua.set_function("disasm", [this](sol::optional<int> address, sol::optional<int> count) -> sol::table {
            sol::table result = _lua->create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return result;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pDebugManager || !ctx->pDebugManager->GetDisassembler()) return result;
            
            Z80Disassembler* disasm = ctx->pDebugManager->GetDisassembler().get();
            Memory* memory = ctx->pMemory;
            Z80* z80 = ctx->pCore->GetZ80();
            LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
            
            uint16_t addr = address.value_or(-1) < 0 ? ctx->pCore->GetZ80()->pc : static_cast<uint16_t>(address.value_or(0));
            int cnt = count.value_or(10);
            if (cnt < 1) cnt = 10;
            if (cnt > 100) cnt = 100;
            
            int idx = 1;
            for (int i = 0; i < cnt; ++i) {
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
                
                sol::table instr = _lua->create_table();
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
                
                result[idx++] = instr;
                addr += cmdLen;
            }
            return result;
        });

        // Physical page disassembly
        lua.set_function("disasm_page", [this](const std::string& type, int page, sol::optional<int> offset, sol::optional<int> count) -> sol::table {
            sol::table result = _lua->create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return result;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pDebugManager || !ctx->pDebugManager->GetDisassembler()) return result;
            
            Z80Disassembler* disasm = ctx->pDebugManager->GetDisassembler().get();
            Memory* memory = ctx->pMemory;
            LabelManager* labelMgr = ctx->pDebugManager->GetLabelManager();
            
            bool isROM = (type == "rom");
            if (!isROM && type != "ram")
                throw std::invalid_argument("disasm_page: unknown page type '" + type + "' (use ram, rom)");
            ValidatePageIndex("disasm_page", type, page, 0);
            uint8_t* pageBase = isROM ? memory->ROMPageHostAddress(static_cast<uint8_t>(page))
                                      : memory->RAMPageAddress(static_cast<uint16_t>(page));
            if (!pageBase) return result;
            
            int off = offset.value_or(0);
            int cnt = count.value_or(10);
            if (off < 0) off = 0;
            if (off >= PAGE_SIZE) off = PAGE_SIZE - 1;
            if (cnt < 1) cnt = 10;
            if (cnt > 100) cnt = 100;
            
            uint16_t currentOffset = static_cast<uint16_t>(off);
            int idx = 1;
            for (int i = 0; i < cnt && currentOffset < PAGE_SIZE; ++i) {
                std::vector<uint8_t> buffer(4, 0);
                for (int j = 0; j < 4 && (currentOffset + j) < PAGE_SIZE; ++j) {
                    buffer[j] = pageBase[currentOffset + j];
                }
                
                uint8_t cmdLen = 0;
                DecodedInstruction decoded;
                std::string mnemonic = disasm->disassembleSingleCommand(buffer, currentOffset, &cmdLen, &decoded);
                if (cmdLen == 0) cmdLen = 1;
                
                sol::table instr = _lua->create_table();
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
                
                result[idx++] = instr;
                currentOffset += cmdLen;
            }
            return result;
        });

        // Screen state
        lua.set_function("screen_get_mode", [this]() -> std::string {
            if (!effectiveEmulator()) return "";
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pScreen) return "";
            return Screen::GetVideoModeName(ctx->pScreen->GetVideoMode());
        });

        lua.set_function("screen_get_border", [this]() -> int {
            if (!effectiveEmulator()) return 0;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pScreen) return 0;
            return ctx->pScreen->GetBorderColor();
        });

        lua.set_function("screen_get_flash", [this]() -> int {
            if (!effectiveEmulator()) return 0;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pScreen) return 0;
            return ctx->pScreen->_vid.flash;
        });

        lua.set_function("screen_get_active", [this]() -> int {
            if (!effectiveEmulator()) return 0;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pScreen) return 0;
            return ctx->pScreen->GetActiveScreen();
        });

        // Screen reports: the same core reports every automation module returns
        lua.set_function("screen_state", [this](sol::this_state s, sol::optional<bool> verbose) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::Screen(ctx, verbose.value_or(false)));
        });
        lua.set_function("screen_mode", [this](sol::this_state s) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::ScreenMode(ctx));
        });
        lua.set_function("screen_flash", [this](sol::this_state s) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::ScreenFlash(ctx));
        });
        // Per-cell ink/paper/bright/flash decoded from screen attribute memory
        lua.set_function("screen_attributes", [this](sol::this_state s, sol::optional<int> screen) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::ScreenAttributes(ctx, screen.value_or(-1)));
        });
        // Former name of screen_mode, kept for existing scripts
        lua.set_function("screen_video_state", [this](sol::this_state s) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::ScreenMode(ctx));
        });

        // Device state reports (core DeviceState: the same trees the WebAPI,
        // Python, CLI and MCP return). Optional chip index -> the chip's
        // full report, no index -> the overview
        lua.set_function("audio_ay_state", [this](sol::this_state s, sol::optional<int> chip) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, chip ? DeviceState::AyChip(ctx, *chip) : DeviceState::Ay(ctx));
        });
        lua.set_function("audio_fm_state", [this](sol::this_state s, sol::optional<int> chip) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, chip ? DeviceState::FmChip(ctx, *chip) : DeviceState::Fm(ctx));
        });
        lua.set_function("ide_state", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::Ide(emulator->GetContext()));
        });

        // CD audio of the ATAPI CD drives (CdAudioControl, PLAN #83): cdaudio_state() reports every
        // drive; cdaudio(verb, drive, {options}) runs status / play / pause / resume / stop / volume / mixer
        lua.set_function("cdaudio_state", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, CdAudioControl::State(emulator->GetContext()));
        });
        lua.set_function("cdaudio", [this](sol::this_state s, const std::string& verb, sol::optional<std::string> drive,
                                           sol::optional<sol::table> opts) -> sol::object {
            CdAudioRequest request;
            request.verb = verb;
            request.drive = drive.value_or("");
            if (opts)
            {
                for (const auto& [key, value] : *opts)
                {
                    std::string text;
                    if (value.is<bool>())
                        text = value.as<bool>() ? "true" : "false";
                    else if (value.get_type() == sol::type::number)
                    {
                        const double number = value.as<double>();
                        text = number == static_cast<double>(static_cast<long long>(number))
                                   ? std::to_string(static_cast<long long>(number))
                                   : std::to_string(number);
                    }
                    else if (value.is<std::string>())
                        text = value.as<std::string>();
                    request.options[key.as<std::string>()] = text;
                }
            }
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
            {
                StateNode none = StateNode::Object();
                none["ok"] = false;
                none["error"] = "unknown-emulator";
                none["message"] = "no emulator selected";
                return StateNodeToLua(s, none);
            }
            return StateNodeToLua(s, CdAudioControl(emulator->GetContext()).Execute(request).ToValue());
        });

        lua.set_function("tsconf_state", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::TsConf(emulator->GetContext()));
        });
        lua.set_function("tsconf_tsu", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::TsConfTsu(emulator->GetContext()));
        });

        // Sprinter Sp2000: the same reports every interface uses (DeviceState::Sprinter,
        // SprinterPortTable, SprinterPortLookup). Options {map=0-3, dos=0|1, pn5=0|1, rw="r"|"w"|"rw"},
        // omitted = the machine's current map / DOS / PN5 and both directions
        auto sprinterQuery = [](sol::optional<sol::table> options, DeviceState::SprinterPortQuery& query, std::string& error) {
            auto text = [&](const char* key) -> std::string {
                if (!options)
                    return std::string();
                sol::object value = (*options)[key];
                if (value.get_type() == sol::type::number)
                    return std::to_string(value.as<long long>());
                if (value.get_type() == sol::type::boolean)
                    return value.as<bool>() ? "1" : "0";
                if (value.get_type() == sol::type::string)
                    return value.as<std::string>();
                return std::string();
            };
            return DeviceState::SprinterPortQueryFromStrings(text("map"), text("dos"), text("pn5"), text("rw"), query, error);
        };
        lua.set_function("sprinter_state", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::Sprinter(emulator->GetContext()));
        });
        lua.set_function("sprinter_text", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::SprinterText(emulator->GetContext()));
        });
        // sprinter_video{page=0|1, all=true, squares=false}: the mode table per square (DeviceState::SprinterVideo)
        lua.set_function("sprinter_video", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            auto text = [&](const char* key) -> std::string {
                if (!options)
                    return std::string();
                sol::object value = (*options)[key];
                if (value.get_type() == sol::type::number)
                    return std::to_string(value.as<long long>());
                if (value.get_type() == sol::type::boolean)
                    return value.as<bool>() ? "1" : "0";
                if (value.get_type() == sol::type::string)
                    return value.as<std::string>();
                return std::string();
            };
            DeviceState::SprinterVideoQuery query;
            std::string error;
            if (!DeviceState::SprinterVideoQueryFromStrings(text("page"), text("all"), text("squares"), query, error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, DeviceState::SprinterVideo(emulator->GetContext(), query)));
            return out;
        });
        // sprinter_palette([k]): k = 0-7, "all" or "used" (default) (DeviceState::SprinterPalette)
        lua.set_function("sprinter_palette", [this](sol::this_state s, sol::object k) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            std::string text;
            if (k.get_type() == sol::type::number)
                text = std::to_string(k.as<long long>());
            else if (k.get_type() == sol::type::string)
                text = k.as<std::string>();
            int palette = DeviceState::kSprinterPalettesUsed;
            std::string error;
            if (!DeviceState::SprinterPaletteFromString(text, palette, error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, DeviceState::SprinterPalette(emulator->GetContext(), palette)));
            return out;
        });
        lua.set_function("sprinter_bios", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::SprinterBios(emulator->GetContext()));
        });
        // sprinter_bios_select{bios="3.06", fast_start=false, accel_int_suspend=false, reset=true}: the image loads at
        // the reset (now unless reset=false) -> the BIOS report, or nil, error (SprinterBios, DeviceState::SprinterBiosSelect)
        lua.set_function("sprinter_bios_select", [this](sol::this_state s, sol::table options) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            auto text = [&](const char* key) -> std::string {
                sol::object value = options[key];
                if (value.get_type() == sol::type::boolean)
                    return value.as<bool>() ? "1" : "0";
                if (value.get_type() == sol::type::number)
                    return std::to_string(value.as<long long>());
                if (value.get_type() == sol::type::string)
                    return value.as<std::string>();
                return std::string();
            };
            SprinterBios::Options parsed;
            std::string error;
            if (!SprinterBios::OptionsFromStrings(text("bios"), text("fast_start"), text("accel_int_suspend"), text("reset"),
                                                  parsed, error))
                return mouseError(s, error);
            const StateNode report = DeviceState::SprinterBiosSelect(emulator->GetContext(), parsed);
            const StateNode* available = report.find("available");
            if (available && !available->b)
                return mouseError(s, report.find("description") ? report.find("description")->s : std::string("failed"));
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, report));
            return out;
        });
        // sprinter_zx_mode([deep]): the ZX (Spectrum) mode report (DeviceState::SprinterZxMode); deep = false skips
        // the whole-RAM search for the launcher's option table
        lua.set_function("sprinter_zx_mode", [this](sol::this_state s, sol::optional<bool> deep) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::SprinterZxMode(emulator->GetContext(), deep.value_or(true)));
        });
        // sprinter_pld_journal([{kinds="cnf,port_1ffd", since=N, from=F, to=F, limit=N, source="live"|"ttd"}]): who
        // changed the PLD setup, when (DeviceState::SprinterJournal); nil + error on a bad option
        lua.set_function("sprinter_pld_journal", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            auto text = [&](const char* key) -> std::string {
                if (!options)
                    return std::string();
                sol::object value = (*options)[key];
                if (value.get_type() == sol::type::number)
                    return std::to_string(value.as<long long>());
                if (value.get_type() == sol::type::string)
                    return value.as<std::string>();
                return std::string();
            };
            DeviceState::SprinterJournalQuery query;
            std::string error;
            if (!DeviceState::SprinterJournalQueryFromStrings(text("kinds"), text("since"), text("from"), text("to"), text("limit"),
                                                              text("source"), query, error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, DeviceState::SprinterJournal(emulator->GetContext(), query)));
            return out;
        });
        // sprinter_pld_journal_control({enabled=true|false, clear=true}): switch / clear the PLD journal
        lua.set_function("sprinter_pld_journal_control", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            int enable = -1;
            bool clear = false;
            if (options)
            {
                sol::object e = (*options)["enabled"];
                if (e.get_type() == sol::type::boolean)
                    enable = e.as<bool>() ? 1 : 0;
                sol::object c = (*options)["clear"];
                if (c.get_type() == sol::type::boolean)
                    clear = c.as<bool>();
            }
            return StateNodeToLua(s, DeviceState::SprinterJournalControl(emulator->GetContext(), enable, clear));
        });
        lua.set_function("sprinter_sound_ring", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::SprinterSoundRing(emulator->GetContext()));
        });
        lua.set_function("sprinter_ports", [this, sprinterQuery](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            DeviceState::SprinterPortQuery query;
            std::string error;
            if (!sprinterQuery(options, query, error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, DeviceState::SprinterPortTable(emulator->GetContext(), query)));
            return out;
        });
        // sprinter_port(0x21BC [, {rw="w"}]) or sprinter_port("21BC", ...)
        lua.set_function("sprinter_port", [this, sprinterQuery](sol::this_state s, sol::object portArg, sol::optional<sol::table> options) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            uint16_t port = 0;
            if (portArg.get_type() == sol::type::number)
            {
                const long long value = portArg.as<long long>();
                if (value < 0 || value > 0xFFFF)
                    return mouseError(s, "port must be 0-0xFFFF");
                port = static_cast<uint16_t>(value);
            }
            else if (portArg.get_type() != sol::type::string || !DeviceState::SprinterPortFromString(portArg.as<std::string>(), port))
                return mouseError(s, "port: a number or a hex string (\"21BC\", \"#21BC\")");
            DeviceState::SprinterPortQuery query;
            std::string error;
            if (!sprinterQuery(options, query, error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, DeviceState::SprinterPortLookup(emulator->GetContext(), port, query)));
            return out;
        });

        // Network adapters: the same report every interface uses (DeviceState::Network)
        lua.set_function("network_state", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::Network(emulator->GetContext()));
        });
        // network_configure{card="zxnetusb", host_access=true, hosts="name=1.2.3.4"} -> true | nil, err
        lua.set_function("network_configure", [this](sol::this_state s, sol::table settings) -> sol::variadic_results {
            sol::variadic_results out;
            Emulator* emulator = effectiveEmulator();
            NetworkManager* manager = (emulator && emulator->GetContext()->pCore)
                                          ? emulator->GetContext()->pCore->GetNetworkManager()
                                          : nullptr;
            std::string error = emulator ? "no network support in this machine" : "No emulator selected";
            if (manager)
            {
                std::vector<std::pair<std::string, std::string>> kv;
                for (const auto& [key, value] : settings)
                {
                    std::string text;
                    if (value.get_type() == sol::type::boolean)
                        text = value.as<bool>() ? "on" : "off";
                    else if (value.get_type() == sol::type::number)
                        text = std::to_string(value.as<long long>());
                    else
                        text = value.as<std::string>();
                    kv.emplace_back(key.as<std::string>(), text);
                }
                NetworkManager::Change change;
                if (NetworkManager::ParseChange(kv, change, error) && manager->RequestChange(change, error))
                {
                    out.push_back(sol::make_object(s, true));
                    return out;
                }
            }
            out.push_back(sol::make_object(s, sol::lua_nil));
            out.push_back(sol::make_object(s, error));
            return out;
        });

        // CMOS clock: the same report and cell access every interface uses
        // (DeviceState::Rtc, RtcAccess). Cells are numbered as the guest numbers them
        lua.set_function("rtc_state", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::Rtc(emulator->GetContext()));
        });
        lua.set_function("rtc_read", [this](sol::this_state s, int start, sol::optional<int> count) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            std::vector<uint8_t> bytes;
            std::string error;
            if (start < 0 || count.value_or(1) < 0 ||
                !RtcAccess::Read(emulator->GetContext(), static_cast<unsigned>(start),
                                 static_cast<unsigned>(count.value_or(1)), bytes, error))
                return mouseError(s, error.empty() ? "start and count must not be negative" : error);
            sol::state_view view(s);
            sol::table out = view.create_table();
            for (size_t i = 0; i < bytes.size(); ++i)
                out[i + 1] = bytes[i];
            sol::variadic_results results;
            results.push_back(out);
            return results;
        });
        lua.set_function("rtc_write", [this](sol::this_state s, int start, sol::table values) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            std::vector<uint8_t> bytes;
            for (size_t i = 1; i <= values.size(); ++i)
            {
                const int v = values.get_or(i, -1);
                if (v < 0 || v > 255)
                    return mouseError(s, "Every value must be 0-255");
                bytes.push_back(static_cast<uint8_t>(v));
            }
            std::string error;
            if (start < 0 || !RtcAccess::Write(emulator->GetContext(), static_cast<unsigned>(start), bytes,
                                               "Lua rtc_write", error))
                return mouseError(s, error.empty() ? "start must not be negative" : error);
            sol::variadic_results results;
            results.push_back(sol::make_object(s, true));
            return results;
        });

        lua.set_function("fdc_state", [this](sol::this_state s) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::Fdc(ctx));
        });
        lua.set_function("contention_state", [this](sol::this_state s) -> sol::object {
            EmulatorContext* ctx = _emulator ? _emulator->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::Contention(ctx));
        });

        // Audio state
        lua.set_function("audio_is_muted", [this]() -> bool {
            if (!effectiveEmulator()) return true;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pSoundManager) return true;
            return ctx->pSoundManager->isMuted();
        });

        lua.set_function("audio_ay_read", [this](int chip, int reg) -> int {
            if (!effectiveEmulator()) return 0;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pSoundManager) return 0;
            auto* ay = ctx->pSoundManager->getAYChip(chip);
            if (!ay || reg < 0 || reg > 15) return 0;
            return ay->readRegister(static_cast<uint8_t>(reg));
        });

        lua.set_function("audio_ay_registers", [this](sol::optional<int> chip) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table regs = lua_view.create_table();
            if (!effectiveEmulator()) return regs;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pSoundManager) return regs;
            auto* ay = ctx->pSoundManager->getAYChip(chip.value_or(0));
            if (!ay) return regs;
            const uint8_t* data = ay->getRegisters();
            for (int i = 0; i < 16; i++) {
                regs[i + 1] = data[i];  // Lua tables start at 1
            }
            return regs;
        });

        lua.set_function("audio_ay_count", [this]() -> int {
            if (!effectiveEmulator()) return 0;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pSoundManager) return 0;
            return ctx->pSoundManager->getAYChipCount();
        });

        // General Sound card (GS design §11.5). All actions mirror the
        // host-port semantics - each flushes the coprocessor to the current
        // ZX tact first. No-ops / nil when the card is not fitted.
        lua.set_function("gs_enabled", [this]() -> bool {
            if (!effectiveEmulator()) return false;
            auto* ctx = effectiveEmulator()->GetContext();
            return ctx && ctx->pSoundManager && ctx->pSoundManager->getGeneralSound() != nullptr;
        });

        // One report for every interface (DeviceState::Gs); `available = false`
        // with a description when no card is fitted
        lua.set_function("gs_state", [this](sol::this_state s) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::Gs(ctx));
        });
        lua.set_function("audio_covox_state", [this](sol::this_state s) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::Covox(ctx));
        });
        // MoonSound: overview, or part "fm" / "pcm"
        lua.set_function("audio_moonsound_state", [this](sol::this_state s, sol::optional<std::string> part) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            if (part && *part == "fm")
                return StateNodeToLua(s, DeviceState::MoonSoundFm(ctx));
            if (part && *part == "pcm")
                return StateNodeToLua(s, DeviceState::MoonSoundPcm(ctx));
            return StateNodeToLua(s, DeviceState::MoonSound(ctx));
        });

        // NeoGS SD slot and flash (other cards: false / no-op)
        // NeoGS media: checked here, carried out on the machine's thread
        // (neogsmedia.h); true when accepted. Insert / eject are refused while
        // a TTD recording runs
        lua.set_function("gs_sd_insert", [this](const std::string& path) -> bool {
            return _emulator && NeoGSMediaAccepted(NeoGSRequestSdInsert(_emulator->GetContext(), path));
        });
        lua.set_function("gs_sd_eject", [this]() -> bool {
            return _emulator && NeoGSMediaAccepted(NeoGSRequestSdEject(_emulator->GetContext()));
        });
        lua.set_function("gs_flash_save", [this]() -> bool {
            return _emulator && NeoGSMediaAccepted(NeoGSRequestFlashSave(_emulator->GetContext()));
        });
        // NeoGS stereo mode: "separated" (as on the board), "gs" (50% cross-feed
        // like the classic GS) or "mono"; applied at the next frame. False on
        // an unknown name
        lua.set_function("gs_stereo_mode", [this](const std::string& mode) -> bool {
            NeoGSConfig::StereoMode parsed = NeoGSConfig::StereoMode::Separated;
            SoundManager* sm = _emulator && _emulator->GetContext() ? _emulator->GetContext()->pSoundManager : nullptr;
            if (!sm || !neogsParseStereoMode(mode, parsed))
                return false;
            sm->setNeoGSStereoMode(parsed);
            return true;
        });

        // Host-port stimuli step the card's Z80, so they go through the live-input
        // path: applied on the machine's thread at an instruction boundary and
        // journaled for TTD replay. Each returns true when submitted; false when
        // no card is fitted, the byte is out of range or TTD replay owns input
        auto submitGS = [this](ttd::TTDInputKind kind, int value) -> bool {
            if (!effectiveEmulator() || value < 0 || value > 255) return false;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            if (!ctx->pSoundManager || !ctx->pSoundManager->getGeneralSound()) return false;
            ttd::TTDInputEvent ev;
            ev.kind = kind;
            ev.value = static_cast<uint8_t>(value);
            return ctx->pTimeTravelManager->SubmitLiveInput(ev);
        };

        lua.set_function("gs_reset", [submitGS]() { return submitGS(ttd::TTDInputKind::GSReset, 0); });

        // #33 bit7 semantics: CPU/banking/timing only, mailbox survives
        lua.set_function("gs_reset_card", [submitGS]() { return submitGS(ttd::TTDInputKind::GSResetCard, 0); });

        lua.set_function("gs_nmi", [submitGS]() { return submitGS(ttd::TTDInputKind::GSNmi, 0); });

        lua.set_function("gs_send_command",
                         [submitGS](int byte) { return submitGS(ttd::TTDInputKind::GSCommand, byte); });

        lua.set_function("gs_send_data", [submitGS](int byte) { return submitGS(ttd::TTDInputKind::GSData, byte); });

        // Reads are peeks: no host read cycle (read_data leaves status bit 7
        // set) and the card is not stepped
        lua.set_function("gs_read_data", [this]() -> int {
            if (!effectiveEmulator()) return -1;
            auto* ctx = effectiveEmulator()->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            return gs ? gs->getDataToHost() : -1;
        });

        lua.set_function("gs_read_status", [this]() -> int {
            if (!effectiveEmulator()) return -1;
            auto* ctx = effectiveEmulator()->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            return gs ? (gs->getStatusRaw() | 0x7E) : -1;
        });

        // Runtime personality switch (GS card personalities design §11.3):
        // requested here, applied at the next frame boundary on the
        // emulation thread - same semantics as the WebAPI switch_personality
        // action and the MCP gs_switch_personality tool action
        lua.set_function("gs_switch_personality", [this](const std::string& personality) -> std::tuple<bool, std::string> {
            if (!effectiveEmulator()) return {false, ""};
            auto* ctx = effectiveEmulator()->GetContext();
            SoundManager* sm = ctx ? ctx->pSoundManager : nullptr;
            if (!sm) return {false, ""};

            GSTypeKind target;
            if (!gsParsePersonality(personality, target))
                return {false, ""};

            std::string refusal;  // a TTD recording refuses the switch (FR-4)
            const bool requested = sm->requestGeneralSoundCardSwitch(target, &refusal);
            return {requested, refusal};
        });

        // Diagnostics: write the last completed COM30..D2 upload (the raw
        // ProTracker module the host streamed) to a file - same data
        // dump_module serves via the WebAPI/MCP
        lua.set_function("gs_dump_module", [this](sol::optional<std::string> path) -> sol::object {
            sol::state_view lua_view(*_lua);
            if (!effectiveEmulator()) return sol::make_object(lua_view, sol::lua_nil);
            auto* ctx = effectiveEmulator()->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (!gs) return sol::make_object(lua_view, sol::lua_nil);

            std::vector<uint8_t> bytes;
            bool playing = false;
            if (!gs->captureModuleUpload(bytes, playing))
                return sol::make_object(lua_view, sol::lua_nil);

            const std::string outPath = path.value_or("gs-module-dump.mod");
            std::ofstream out(outPath, std::ios::binary);
            if (!out)
                return sol::make_object(lua_view, sol::lua_nil);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

            sol::table t = lua_view.create_table();
            t["path"] = outPath;
            t["bytes"] = static_cast<double>(bytes.size());
            t["playing"] = playing;
            return t;
        });

        // GS coprocessor triage: always-on activity counters + opt-in
        // port/DAC event trace - the "is the GS Z80 alive and doing DAC
        // pushes" tool, same data model as CLI 'gsporttrace' / WebAPI /
        // MCP / Python (see gsporttrace.h).
        lua.set_function("gs_counters", [this]() -> sol::object {
            sol::state_view lua_view(*_lua);
            if (!effectiveEmulator()) return sol::make_object(lua_view, sol::lua_nil);
            auto* ctx = effectiveEmulator()->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (!gs) return sol::make_object(lua_view, sol::lua_nil);

            const GSActivityCounters& c = gs->getActivityCounters();
            sol::table t = lua_view.create_table();
            t["cpu_steps"] = static_cast<double>(c.cpuSteps);
            t["interrupts_accepted"] = static_cast<double>(c.interruptsAccepted);
            t["interrupt_periods"] = static_cast<double>(c.interruptPeriods);
            t["interrupts_coalesced"] = static_cast<double>(c.interruptsCoalesced);
            t["nmis_accepted"] = static_cast<double>(c.nmisAccepted);
            t["dac_fetches"] = static_cast<double>(c.dacFetches);
            t["volume_latch_writes"] = static_cast<double>(c.volumeLatchWrites);
            t["host_commands_received"] = static_cast<double>(c.hostCommandsReceived);
            t["host_commands_dropped"] = static_cast<double>(c.hostCommandsDropped);
            t["host_data_written"] = static_cast<double>(c.hostDataWritten);
            t["host_data_dropped"] = static_cast<double>(c.hostDataDropped);
            t["host_data_read"] = static_cast<double>(c.hostDataRead);
            t["last_dac_fetch_gs_cycle"] = static_cast<double>(c.lastDacFetchGsCycle);
            t["last_dac_fetch_frame"] = static_cast<double>(c.lastDacFetchFrame);
            t["trace_capturing"] = gs->isPortTraceCapturing();
            t["trace_event_count"] = static_cast<double>(gs->getPortTraceEventCount());
            t["pc"] = gs->getCPUReg(GSCpuRegister::PC);
            t["halted"] = gs->isCPUHalted();
            return t;
        });

        lua.set_function("gs_porttrace_start", [this]() {
            if (!effectiveEmulator()) return;
            auto* ctx = effectiveEmulator()->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (gs) gs->startPortTrace();
        });
        lua.set_function("gs_porttrace_stop", [this]() {
            if (!effectiveEmulator()) return;
            auto* ctx = effectiveEmulator()->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (gs) gs->stopPortTrace();
        });
        lua.set_function("gs_porttrace_pause", [this]() {
            if (!effectiveEmulator()) return;
            auto* ctx = effectiveEmulator()->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (gs) gs->pausePortTrace();
        });
        lua.set_function("gs_porttrace_resume", [this]() {
            if (!effectiveEmulator()) return;
            auto* ctx = effectiveEmulator()->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (gs) gs->resumePortTrace();
        });
        lua.set_function("gs_porttrace_clear", [this]() {
            if (!effectiveEmulator()) return;
            auto* ctx = effectiveEmulator()->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (gs) gs->clearPortTrace();
        });

        lua.set_function("gs_porttrace_events", [this](sol::optional<int> count) -> sol::object {
            sol::state_view lua_view(*_lua);
            if (!effectiveEmulator()) return sol::make_object(lua_view, sol::lua_nil);
            auto* ctx = effectiveEmulator()->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (!gs) return sol::make_object(lua_view, sol::lua_nil);

            auto events = gs->getPortTraceLast(static_cast<size_t>(count.value_or(50)));
            sol::table result = lua_view.create_table();
            int idx = 1;
            for (const auto& e : events)
            {
                sol::table ev = lua_view.create_table();
                ev["timestamp"] = static_cast<double>(e.timestamp);
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
                result[idx++] = ev;
            }
            return result;
        });

        // Advanced disk operations
        lua.set_function("disk_info", [this](int drive) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table info = lua_view.create_table();
            if (!effectiveEmulator() || drive < 0 || drive > 3) return info;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx) return info;
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
        });

        lua.set_function("disk_read_sector", [this](int drive, int cyl, int side, int sector) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table data = lua_view.create_table();
            if (!effectiveEmulator() || drive < 0 || drive > 3) return data;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx) return data;
            FDD* fdd = ctx->coreState.diskDrives[drive];
            if (!fdd) return data;
            DiskImage* disk = fdd->getDiskImage();
            if (!disk) return data;
            auto* track = disk->getTrackForCylinderAndSide(cyl, side);
            if (!track) return data;
            auto* sec = track->getSector(static_cast<uint8_t>(sector));  // Sector number = sector + 1
            if (!sec || !sec->hasData) return data;
            for (int i = 0; i < sec->dataSize; i++) {
                data[i + 1] = sec->data[i];  // Lua tables start at 1
            }
            return data;
        });

        lua.set_function("disk_read_sector_hex", [this](int drive, int trackNo, int sector) -> std::string {
            if (!effectiveEmulator() || drive < 0 || drive > 3) return "";
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx) return "";
            FDD* fdd = ctx->coreState.diskDrives[drive];
            if (!fdd) return "";
            DiskImage* disk = fdd->getDiskImage();
            if (!disk) return "";
            return disk->DumpSectorHex(trackNo, sector);
        });

        // Set the emulator instance in the Lua environment
        lua["emulator"] = _emulator;

        // -----------------------------------------------------------------
        // TTD (Time-Travel Debug) bindings — Phase 2 surface
        // -----------------------------------------------------------------
        // All functions return tables or booleans matching the WebAPI/Python
        // shape. No-op (return false / empty table) when TTD is unavailable.
        // -----------------------------------------------------------------

        lua.set_function("ttd_status", [this]() -> sol::table {
            Emulator* emulator = effectiveEmulator();
            sol::state_view lua_view(*_lua);
            sol::table info = lua_view.create_table();
            if (!emulator) { info["ttd_available"] = false; return info; }
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager)
            {
                info["state"]         = "idle";
                info["ttd_available"] = false;
                return info;
            }
            ttd::TimeTravelManager* mgr = ctx->pTimeTravelManager;
            ttd::TTDSessionInfo si = mgr->GetSessionInfo();
            info["state"]                    = ttd::TTDSessionStateToString(si.state);
            info["session_start_frame"]      = si.sessionStartFrame;
            info["current_end_frame"]        = si.currentEndFrame;
            info["checkpoint_count"]         = static_cast<uint64_t>(si.checkpointCount);
            info["page_store_bytes"]         = static_cast<uint64_t>(si.pageStoreBytes);
            info["page_store_used_bytes"]    = static_cast<uint64_t>(si.pageStoreUsedBytes);
            info["baseline_frames_captured"] = si.baselineFramesCaptured;
            info["session_heap_bytes"]       = static_cast<uint64_t>(si.sessionHeapBytes);
            info["history_limit_frames"]     = si.historyLimitFrames;
            info["history_limit_bytes"]      = si.historyLimitBytes;
            info["history_bytes"]            = si.historyBytes;
            info["evicted_checkpoints"]      = si.evictedCheckpoints;
            info["loaded_from_file"]      = si.loadedFromFile;
            info["source_path"]           = si.sourcePath;
            info["captured_at_unix_ms"]   = si.capturedAtUnixMs;
            info["model_id"]              = si.modelId;
            info["model_ram_pages"]       = si.modelRamPages;
            info["write_journal_records"] = si.writeJournalRecords;
            info["write_journal_bytes"]   = si.writeJournalBytes;
            info["coverage_index_frames"] = si.coverageIndexFrames;
            info["coverage_index_bytes"]  = si.coverageIndexBytes;
            info["write_journal_enabled"]    = si.writeJournalEnabled;
            info["write_journal_complete"]   = si.writeJournalComplete;
            info["write_journal_wrapped"]    = si.writeJournalWrapped;
            if (!si.journalGapReason.empty())
            {
                sol::table gap = lua_view.create_table();
                gap["reason"] = si.journalGapReason;
                if (si.journalGapHasPosition)
                {
                    gap["frame"]    = si.journalGapAt.frame;
                    gap["tinframe"] = si.journalGapAt.tInFrame;
                }
                info["write_journal_gap"] = gap;
            }
            info["bookmark_count"]           = static_cast<uint64_t>(si.bookmarkCount);
            info["input_event_count"]        = static_cast<uint64_t>(si.inputEventCount);
            info["external_event_count"]     = static_cast<uint64_t>(si.externalEventCount);
            info["input_history_complete"]   = si.inputHistoryComplete;
            info["port_journal_active"]      = si.portJournalActive;
            if (!si.portJournalOffReason.empty())
                info["port_journal_off_reason"] = si.portJournalOffReason;
            info["port_read_count"]          = si.portReadCount;
            info["port_write_count"]         = si.portWriteCount;
            info["port_journal_bytes"]       = static_cast<uint64_t>(si.portJournalBytes);
            info["port_replay_value_mismatches"] = si.portReplayValueMismatches;
            info["port_replay_divergences"]  = si.portReplayDivergences;
            if (!si.lastDropReason.empty())
                info["last_drop_reason"]     = si.lastDropReason;  // "" until a history is dropped
            if (!si.unavailableReason.empty())
                info["unavailable_reason"]   = si.unavailableReason;  // e.g. a ZX-Poly member
            if (si.checkpointCount != 0)
                info["machine"] = TtdRecordedMachineTable(lua_view, si.machine);  // the recorded machine
            if (!si.recordedBy.empty())
                info["recorded_by"] = si.recordedBy;  // the instance that recorded a loaded file
            info["ttd_available"]            = true;
            return info;
        });

        // ttd_file_info(path) - a .ttd file's header, sections and recorded machine,
        // read without loading it: {ok, error | path, file_bytes, ..., machine, sections}
        lua.set_function("ttd_file_info", [this](const std::string& path) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table r = lua_view.create_table();
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
            r["file_bytes"] = fi.fileBytes;
            r["schema_version"] = static_cast<unsigned>(fi.schemaVersion);
            r["flags"] = static_cast<unsigned>(fi.flags);
            r["captured_at_unix_ms"] = fi.capturedAtUnixMs;
            if (!fi.emulatorId.empty())
                r["recorded_by"] = fi.emulatorId;
            r["session_state"] = ttd::TTDSessionStateToString(static_cast<ttd::TTDSessionState>(fi.sessionState));
            r["session_start_frame"] = fi.startFrame;
            r["session_end_frame"] = fi.endFrame;
            r["checkpoint_count"] = static_cast<uint64_t>(fi.checkpointCount);
            r["page_slot_count"] = static_cast<uint64_t>(fi.pageStoreCount);
            sol::table sections = lua_view.create_table();
            sections["write_journal"] = fi.hasWriteJournal;
            sections["write_journal_complete"] = fi.writeJournalComplete;
            sections["coverage_index"] = fi.hasCoverageIndex;
            sections["bookmarks"] = fi.hasBookmarks;
            sections["input_journal"] = fi.hasInputJournal;
            sections["external_events"] = fi.hasExternalEvents;
            sections["port_journals"] = fi.hasPortJournals;
            sections["top_clock_time"] = fi.topClockTime;
            r["sections"] = sections;
            r["machine"] = TtdRecordedMachineTable(lua_view, fi.machine);
            r["peripherals_from_header"] = fi.peripheralsFromHeader;
            return r;
        });

        // ttd_start([mode]) - start recording
        // mode: "gaming" (smaller files, no journal) or "development" (default, full journal)
        lua.set_function("ttd_start", [this](sol::optional<std::string> modeOpt) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            // A mode overrides the journal choice; without one the choice made
            // by ttd_set_journal_enabled stands (journal on by default)
            if (modeOpt.has_value())
                ctx->pTimeTravelManager->SetEnableWriteJournal(modeOpt.value() != "gaming");
            return ctx->pTimeTravelManager->StartRecording();
        });

        // ttd_set_history_limit(frames, bytes) - bound the recorded history: while
        // recording, the oldest frames are released beyond either limit (0 = none;
        // nil keeps the current value). Returns the limit now in force: frames, bytes
        lua.set_function("ttd_set_history_limit",
                         [this](sol::optional<uint64_t> frames, sol::optional<uint64_t> bytes) -> std::tuple<uint64_t, uint64_t> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return {0, 0};
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return {0, 0};
            const ttd::TTDSessionInfo si = ctx->pTimeTravelManager->GetSessionInfo();
            ctx->pTimeTravelManager->SetHistoryLimit(frames.value_or(si.historyLimitFrames),
                                                     bytes.value_or(si.historyLimitBytes));
            const ttd::TTDSessionInfo now = ctx->pTimeTravelManager->GetSessionInfo();
            return {now.historyLimitFrames, now.historyLimitBytes};
        });

        // ttd_set_journal_enabled(bool) - configure write journal capture
        // ok, reason: refused while recording (a recording keeps its journal mode)
        lua.set_function("ttd_set_journal_enabled", [this](bool enabled) -> std::tuple<bool, std::string> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return {false, "no emulator"};
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return {false, "TTD not available"};
            if (ctx->pTimeTravelManager->SetEnableWriteJournal(enabled)) return {true, ""};
            return {false, ctx->pTimeTravelManager->RecordingGuard(ttd::TTDGuardedAction::ChangeWriteJournal)};
        });

        lua.set_function("ttd_get_journal_enabled", [this]() -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return true;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return true;
            return ctx->pTimeTravelManager->GetEnableWriteJournal();
        });

        lua.set_function("ttd_stop", [this]() {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return;
            auto* ctx = emulator->GetContext();
            if (ctx && ctx->pTimeTravelManager)
                ctx->pTimeTravelManager->StopRecording();
        });

        // ok, reason: refused while recording (stop the recording first)
        lua.set_function("ttd_invalidate", [this](sol::optional<std::string> reason) -> std::tuple<bool, std::string> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return {false, "no emulator"};
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return {false, "TTD not available"};
            if (std::string refusal = ctx->pTimeTravelManager->RecordingGuard(ttd::TTDGuardedAction::Invalidate);
                !refusal.empty())
                return {false, refusal};
            ctx->pTimeTravelManager->InvalidateSession(reason.value_or("lua invalidate").c_str());
            return {true, ""};
        });

        lua.set_function("ttd_seek", [this](uint64_t frame, sol::optional<uint32_t> tInFrameOpt) -> sol::table {
            Emulator* emulator = effectiveEmulator();
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!emulator) { result["reached"] = false; return result; }
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager)
            {
                result["reached"] = false;
                result["error"]   = "TTD not available";
                return result;
            }
            uint32_t tInFrame = tInFrameOpt.value_or(0);
            ttd::TTDTimePoint target{frame, tInFrame};
            ttd::TimeTravelManager::TTDSeekResult r;
            bool reached = ctx->pTimeTravelManager->SeekTo(target, &r);
            result["reached"] = reached;

            sol::table arrivedAt = lua_view.create_table();
            arrivedAt["frame"]    = r.arrivedAt.frame;
            arrivedAt["tinframe"] = r.arrivedAt.tInFrame;
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
                sol::table marker = lua_view.create_table();
                marker["frame"]    = r.blockingMarker.time.frame;
                marker["tinframe"] = r.blockingMarker.time.tInFrame;
                marker["kind"]     = ttd::TTDExternalEventKindToString(r.blockingMarker.kind);
                marker["reason"]   = r.blockingMarker.reason;
                result["blocking_marker"] = marker;
            }
            return result;
        });

        lua.set_function("ttd_step_back", [this]() -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            return ctx->pTimeTravelManager->StepBackFrame();
        });

        lua.set_function("ttd_step_forward", [this]() -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            return ctx->pTimeTravelManager->StepForwardFrame();
        });

        lua.set_function("ttd_resume", [this](sol::optional<uint64_t> frameOpt, sol::optional<uint32_t> tInFrameOpt) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            // No frame: resume exactly where the machine stands (as CLI and WebAPI do)
            ttd::TTDTimePoint from = ctx->pTimeTravelManager->CurrentPosition();
            if (frameOpt)
            {
                from.frame = *frameOpt;
                from.tInFrame = tInFrameOpt.value_or(0);
            }
            return ctx->pTimeTravelManager->ResumeRecordingFrom(from);
        });

        lua.set_function("ttd_position", [this]() -> sol::table {
            Emulator* emulator = effectiveEmulator();
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager)
            {
                result["error"] = "TTD not available";
                return result;
            }
            ttd::TTDTimePoint pos = ctx->pTimeTravelManager->CurrentPosition();
            ttd::TTDTimePoint end = ctx->pTimeTravelManager->SessionEndPosition();
            sol::table current = lua_view.create_table();
            current["frame"]    = pos.frame;
            current["tinframe"] = pos.tInFrame;
            result["current"]   = current;
            sol::table sessionEnd = lua_view.create_table();
            sessionEnd["frame"]    = end.frame;
            sessionEnd["tinframe"] = end.tInFrame;
            result["session_end"]  = sessionEnd;
            return result;
        });

        lua.set_function("ttd_markers", [this]() -> sol::table {
            Emulator* emulator = effectiveEmulator();
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!emulator) return result;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return result;
            const auto& journal = ctx->pTimeTravelManager->GetExternalEvents();
            int idx = 1;  // Lua tables are 1-based
            for (const auto& e : journal.Events())
            {
                sol::table marker = lua_view.create_table();
                marker["frame"]    = e.time.frame;
                marker["tinframe"] = e.time.tInFrame;
                marker["kind"]     = ttd::TTDExternalEventKindToString(e.kind);
                marker["reason"]   = e.reason;
                result[idx++]       = marker;
            }
            return result;
        });

        // -----------------------------------------------------------------
        // TD-4 — agent bookmarks (advisory annotations, never barriers).
        // Labels are keys: non-empty, at most 63 chars, unique per session.
        // -----------------------------------------------------------------

        lua.set_function("ttd_bookmark_add", [this](const std::string& label,
                                                     sol::optional<uint64_t> frameOpt,
                                                     sol::optional<uint32_t> tInFrameOpt) -> sol::table {
            Emulator* emulator = effectiveEmulator();
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            result["added"] = false;
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) { result["error"] = "TTD not available"; return result; }

            // Position omitted → current position (mark here).
            ttd::TTDTimePoint time = ctx->pTimeTravelManager->CurrentPosition();
            if (frameOpt)
            {
                time.frame    = *frameOpt;
                time.tInFrame = tInFrameOpt.value_or(0);
            }

            std::string err;
            if (!ctx->pTimeTravelManager->AddBookmark(time, label, &err))
            {
                result["error"] = err;
                return result;
            }
            result["added"]    = true;
            result["label"]    = label;
            result["frame"]    = time.frame;
            result["tinframe"] = time.tInFrame;
            return result;
        });

        lua.set_function("ttd_bookmarks", [this]() -> sol::table {
            Emulator* emulator = effectiveEmulator();
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!emulator) return result;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return result;
            int idx = 1;  // Lua tables are 1-based
            for (const auto& bm : ctx->pTimeTravelManager->GetBookmarks())
            {
                sol::table entry = lua_view.create_table();
                entry["frame"]    = bm.time.frame;
                entry["tinframe"] = bm.time.tInFrame;
                entry["label"]    = bm.label;
                result[idx++]     = entry;
            }
            return result;
        });

        lua.set_function("ttd_bookmark_delete", [this](const std::string& label) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            return ctx->pTimeTravelManager->RemoveBookmark(label);
        });

        // A bookmark seek IS a seek — identical result shape to ttd_seek
        // (plus the resolved label), so a real barrier between the restore
        // checkpoint and the target still surfaces as halt_reason
        // "external_event". A bookmark itself never halts anything.
        lua.set_function("ttd_seek_bookmark", [this](const std::string& label) -> sol::table {
            Emulator* emulator = effectiveEmulator();
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!emulator) { result["reached"] = false; return result; }
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager)
            {
                result["reached"] = false;
                result["error"]   = "TTD not available";
                return result;
            }
            ttd::TimeTravelManager::TTDSeekResult r;
            std::string err;
            const bool reached = ctx->pTimeTravelManager->SeekToBookmark(label, &r, &err);
            result["reached"] = reached;
            if (!err.empty())
                result["error"] = err;

            sol::table arrivedAt = lua_view.create_table();
            arrivedAt["frame"]    = r.arrivedAt.frame;
            arrivedAt["tinframe"] = r.arrivedAt.tInFrame;
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
                sol::table marker = lua_view.create_table();
                marker["frame"]    = r.blockingMarker.time.frame;
                marker["tinframe"] = r.blockingMarker.time.tInFrame;
                marker["kind"]     = ttd::TTDExternalEventKindToString(r.blockingMarker.kind);
                marker["reason"]   = r.blockingMarker.reason;
                result["blocking_marker"] = marker;
            }
            result["bookmark"]    = label;
            return result;
        });

        // -----------------------------------------------------------------
        // Phase 4 — Reverse search + dump + instruction step
        // -----------------------------------------------------------------

        lua.set_function("ttd_dump", [this](const std::string& path) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            std::ofstream out(path, std::ios::binary);
            if (!out.is_open()) return false;
            std::string err;
            return ctx->pTimeTravelManager->SerializeSession(out, err);
        });

        // Loading refuses a session recorded on a different machine model: a
        // checkpoint is raw RAM pages plus a chipset snapshot, so it only
        // restores into an instance of the model it came from. Returns a table
        // with ok/error so scripts can report the reason.
        lua.set_function("ttd_load", [this](const std::string& path) -> sol::table {
            Emulator* emulator = effectiveEmulator();
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            result["ok"] = false;
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) { result["error"] = "TTD not available"; return result; }
            std::ifstream in(path, std::ios::binary);
            if (!in.is_open()) { result["error"] = "cannot open file: " + path; return result; }
            std::string err;
            ctx->pTimeTravelManager->SetSessionSourcePath(path);
            if (!ctx->pTimeTravelManager->DeserializeSession(in, err))
            {
                result["error"] = err;
                return result;
            }
            const ttd::TTDSessionInfo info = ctx->pTimeTravelManager->GetSessionInfo();
            result["ok"] = true;
            result["checkpoint_count"] = static_cast<uint64_t>(info.checkpointCount);
            result["session_start_frame"] = info.sessionStartFrame;
            result["current_end_frame"] = info.currentEndFrame;
            return result;
        });

        // "When did the program ...": ttd_port_events(event, [arg], [options])
        // over the session's port journals (ttdportsearch.h). event: "key",
        // "ear", "ay-read", "ay-write", "ay-select", "border", "beeper", "in",
        // "out"; arg: a key name or an AY register; options: a table of
        // ttd::ApplyPortQueryOption names (limit, newest, from, to, port,
        // port_mask, value, value_mask, match, trigger, ay_register; file = a
        // .ttd path searched without loading it)
        lua.set_function("ttd_port_events", [this](const std::string& event, sol::object argObj,
                                                     sol::object optionsObj) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            result["ok"] = false;
            Emulator* emulator = effectiveEmulator();
            auto* ctx = emulator ? emulator->GetContext() : nullptr;
            if (!ctx || !ctx->pTimeTravelManager)
            {
                result["error"] = "TTD engine not available";
                return result;
            }
            auto text = [](const sol::object& o) -> std::string {
                if (o.is<bool>())
                    return o.as<bool>() ? "true" : "false";
                if (o.is<double>())
                    return std::to_string(static_cast<long long>(o.as<double>()));
                return o.is<std::string>() ? o.as<std::string>() : std::string();
            };
            ttd::TTDPortQuery q;
            std::string err;
            const std::string arg = (argObj.valid() && argObj.get_type() != sol::type::lua_nil) ? text(argObj) : "";
            if (!ttd::BuildPortEventQuery(event, arg, q, err))
            {
                result["error"] = err;
                return result;
            }
            std::string file;  // options.file: a .ttd on disk, searched without loading it
            if (optionsObj.is<sol::table>())
            {
                for (const auto& [key, value] : optionsObj.as<sol::table>())
                {
                    if (key.as<std::string>() == "file")
                    {
                        file = text(value);
                        continue;
                    }
                    if (!ttd::ApplyPortQueryOption(q, key.as<std::string>(), text(value), err))
                    {
                        result["error"] = err;
                        return result;
                    }
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
            result["count"] = static_cast<uint64_t>(found.hits.size());
            result["truncated"] = found.truncated;
            result["scanned"] = found.scanned;
            sol::table hits = lua_view.create_table();
            int i = 1;
            for (const ttd::TTDPortHit& h : found.hits)
            {
                sol::table hit = lua_view.create_table();
                hit["index"] = h.index;
                hit["frame"] = h.record.frame;
                hit["tinframe"] = h.record.tInFrame;
                hit["port"] = h.record.port;
                hit["value"] = h.record.value;
                hit["pc"] = h.record.pc;
                if (h.ayRegister >= 0)
                    hit["ay_register"] = h.ayRegister;
                hits[i++] = hit;
            }
            result["hits"] = hits;
            return result;
        });

        lua.set_function("ttd_find_last", [this](sol::object firstArgOpt,
                                                   sol::optional<std::string> accessOpt,
                                                   sol::optional<uint8_t> valueOpt,
                                                   sol::optional<uint16_t> pcFromOpt,
                                                   sol::optional<uint16_t> pcToOpt,
                                                   sol::optional<uint64_t> beforeFrameOpt,
                                                   sol::optional<uint32_t> beforeTinOpt,
                                                   sol::optional<uint32_t> physPageOpt,
                                                   sol::optional<uint16_t> addrFromOpt,
                                                   sol::optional<uint16_t> addrToOpt) -> sol::table {
            Emulator* emulator = effectiveEmulator();
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!emulator) { result["found"] = false; return result; }
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) { result["found"] = false; return result; }

            ttd::TTDSearchQuery q;
            if (firstArgOpt.is<sol::table>())
            {
                sol::table tbl = firstArgOpt.as<sol::table>();
                if (tbl["addr"].valid())
                {
                    uint16_t a = tbl["addr"].get<uint16_t>();
                    q.addrFrom = q.addrTo = a;
                }
                else
                {
                    q.addrFrom = tbl["addr_from"].valid() ? tbl["addr_from"].get<uint16_t>() : (tbl["addrFrom"].valid() ? tbl["addrFrom"].get<uint16_t>() : 0);
                    q.addrTo = tbl["addr_to"].valid() ? tbl["addr_to"].get<uint16_t>() : (tbl["addrTo"].valid() ? tbl["addrTo"].get<uint16_t>() : 0xFFFF);
                }

                std::string accStr = tbl["access"].valid() ? tbl["access"].get<std::string>() : "write";
                q.access = ttd::TTDAccessTypeFromString(accStr.c_str());

                if (tbl["value"].valid()) { q.hasValueFilter = true; q.value = tbl["value"].get<uint8_t>(); }
                if (tbl["pc_from"].valid()) { q.hasPcFilter = true; q.pcFrom = tbl["pc_from"].get<uint16_t>(); q.pcTo = tbl["pc_to"].valid() ? tbl["pc_to"].get<uint16_t>() : 0xFFFF; }
                else if (tbl["pcFrom"].valid()) { q.hasPcFilter = true; q.pcFrom = tbl["pcFrom"].get<uint16_t>(); q.pcTo = tbl["pcTo"].valid() ? tbl["pcTo"].get<uint16_t>() : 0xFFFF; }

                sol::object pageObj = tbl["phys_page"];
                if (!pageObj.valid())
                    pageObj = tbl["physPage"];
                if (pageObj.valid())
                {
                    const uint32_t page = pageObj.as<uint32_t>();
                    if (page > ttd::kPhysPageMax)
                    {
                        result["found"] = false;
                        result["error"] = "phys_page expects 0..255";
                        return result;
                    }
                    q.hasPhysPageFilter = true;
                    q.physPage = static_cast<ttd::PhysPage>(page);
                }

                if (tbl["before_frame"].valid())
                {
                    uint64_t f = tbl["before_frame"].get<uint64_t>();
                    uint32_t tin = tbl["before_tin"].valid() ? tbl["before_tin"].get<uint32_t>() : 0;
                    q.beforeGlobalT = ctx->pTimeTravelManager->GlobalT({f, tin});
                }
                else if (tbl["before"].valid())
                {
                    q.beforeGlobalT = tbl["before"].get<uint64_t>();
                }
            }
            else
            {
                if (firstArgOpt.is<uint16_t>())
                {
                    q.addrFrom = q.addrTo = firstArgOpt.as<uint16_t>();
                }
                else
                {
                    q.addrFrom = addrFromOpt.value_or(0);
                    q.addrTo = addrToOpt.value_or(0xFFFF);
                }
                q.access = ttd::TTDAccessTypeFromString(accessOpt.value_or("write").c_str());
                if (valueOpt) { q.hasValueFilter = true; q.value = *valueOpt; }
                if (pcFromOpt) { q.hasPcFilter = true; q.pcFrom = *pcFromOpt; q.pcTo = pcToOpt.value_or(0xFFFF); }
                if (physPageOpt)
                {
                    if (*physPageOpt > ttd::kPhysPageMax)
                    {
                        result["found"] = false;
                        result["error"] = "phys_page expects 0..255";
                        return result;
                    }
                    q.hasPhysPageFilter = true;
                    q.physPage = static_cast<ttd::PhysPage>(*physPageOpt);
                }
                if (beforeFrameOpt)
                    q.beforeGlobalT = ctx->pTimeTravelManager->GlobalT(
                        {static_cast<uint64_t>(*beforeFrameOpt), beforeTinOpt.value_or(0)});
            }

            ttd::TTDExternalEvent marker{};
            ttd::TTDSearchWindow window;
            auto found = ctx->pTimeTravelManager->FindLastAccess(q, &marker, &window);
            // TD-8: the part of history the search examined
            if (window.searched)
            {
                result["covered_from"]          = window.from.frame;
                result["covered_from_tinframe"] = window.from.tInFrame;
                result["covered_to"]            = window.to.frame;
                result["covered_to_tinframe"]   = window.to.tInFrame;
            }
            if (!found)
            {
                result["found"] = false;
                if (marker.reason[0] != '\0')
                {
                    // A replay barrier stopped the search before any match
                    result["blocked"]         = true;
                    result["marker_frame"]    = marker.time.frame;
                    result["marker_tinframe"] = marker.time.tInFrame;
                    result["marker_kind"]     = ttd::TTDExternalEventKindToString(marker.kind);
                    result["marker_reason"]   = marker.reason;
                }
                return result;
            }
            result["found"]    = true;
            result["frame"]    = found->time.frame;
            result["tinframe"]  = found->time.tInFrame;
            result["pc"]        = found->pc;
            result["value"]     = found->value;
            // nil = the access had no RAM page (ROM, cache, I/O)
            if (found->physPage != ttd::kPhysPageNone)
                result["phys_page"] = found->physPage;
            result["access"]    = ttd::TTDAccessTypeToString(found->access);
            return result;
        });

        lua.set_function("ttd_step_instruction_back", [this]() -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            return ctx->pTimeTravelManager->StepBackInstruction();
        });

        lua.set_function("ttd_step_instruction_forward", [this]() -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            return ctx->pTimeTravelManager->StepForwardInstruction();
        });

        // -----------------------------------------------------------------
        // Phase 4 — Reverse execution (multi-step)
        // -----------------------------------------------------------------

        lua.set_function("ttd_reverse_step", [this](sol::optional<uint32_t> countOpt) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            return ctx->pTimeTravelManager->ReverseStepInstructions(
                countOpt.value_or(1));
        });

        lua.set_function("ttd_reverse_step_tstates", [this](uint64_t tstates) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            return ctx->pTimeTravelManager->ReverseStepTStates(tstates);
        });

        lua.set_function("ttd_reverse_continue", [this](sol::table pcsTable) -> sol::table {
            Emulator* emulator = effectiveEmulator();
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!emulator) { result["matched"] = false; return result; }
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) { result["matched"] = false; return result; }

            std::vector<uint16_t> pcs;
            pcs.reserve(pcsTable.size());
            for (auto& pair : pcsTable)
            {
                uint16_t pc = static_cast<uint16_t>(pair.second.as<uint32_t>());
                pcs.push_back(pc);
            }

            auto r = ctx->pTimeTravelManager->ReverseContinue(pcs);
            result["matched"] = r.matched;
            result["pc"]      = r.pc;
            if (r.matched)
            {
                result["frame"]   = r.arrivedAt.frame;
                result["tinframe"] = r.arrivedAt.tInFrame;
            }
            if (r.blockingMarker.reason[0] != '\0')
            {
                sol::table m = lua_view.create_table();
                m["kind"]     = ttd::TTDExternalEventKindToString(r.blockingMarker.kind);
                m["reason"]   = r.blockingMarker.reason;
                m["frame"]    = r.blockingMarker.time.frame;
                m["tinframe"] = r.blockingMarker.time.tInFrame;
                result["blocked_by_marker"] = m;
            }
            // TD-8: the part of history the search examined
            if (r.window.searched)
            {
                result["covered_from"]          = r.window.from.frame;
                result["covered_from_tinframe"] = r.window.from.tInFrame;
                result["covered_to"]            = r.window.to.frame;
                result["covered_to_tinframe"]   = r.window.to.tInFrame;
            }
            return result;
        });

        lua.set_function("ttd_coverage_probe", [this](sol::table argsTable) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator || !emulator->GetContext() || !emulator->GetContext()->pTimeTravelManager)
            {
                result["index_available"] = false;
                result["touched"] = false;
                return result;
            }
            uint64_t frame = 0;
            if (argsTable["frame"].valid()) frame = argsTable.get<uint64_t>("frame");
            std::string kindStr = "executed";
            if (argsTable["kind"].valid()) kindStr = argsTable.get<std::string>("kind");
            ttd::TTDCoverageKind kind = ttd::TTDCoverageKind::Executed;
            ttd::TTDCoverageKindFromString(kindStr, kind);

            uint16_t addrFrom = 0;
            if (argsTable["addr_from"].valid()) addrFrom = static_cast<uint16_t>(argsTable.get<uint32_t>("addr_from"));
            uint16_t addrTo = 0xFFFF;
            if (argsTable["addr_to"].valid()) addrTo = static_cast<uint16_t>(argsTable.get<uint32_t>("addr_to"));

            std::optional<ttd::PhysPage> physPage;
            if (argsTable["phys_page"].valid())
            {
                const uint32_t page = argsTable.get<uint32_t>("phys_page");
                if (page > ttd::kPhysPageMax)
                {
                    result["index_available"] = false;
                    result["error"] = "phys_page expects 0..255";
                    return result;
                }
                physPage = static_cast<ttd::PhysPage>(page);
            }

            auto res = emulator->GetContext()->pTimeTravelManager->QueryCoverageProbe(frame, kind, addrFrom, addrTo, physPage);
            result["frame"] = res.frame;
            result["kind"] = ttd::TTDCoverageKindToString(res.kind);
            result["touched"] = res.touched;
            result["index_available"] = res.indexAvailable;
            return result;
        });

        lua.set_function("ttd_coverage_scan", [this](sol::table argsTable) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator || !emulator->GetContext() || !emulator->GetContext()->pTimeTravelManager)
            {
                result["index_available"] = false;
                result["scanned_frames"] = 0;
                result["matching_frames"] = 0;
                result["frames"] = lua_view.create_table();
                return result;
            }
            auto* mgr = emulator->GetContext()->pTimeTravelManager;
            uint64_t fromFrame = 0;
            if (argsTable["from_frame"].valid()) fromFrame = argsTable.get<uint64_t>("from_frame");
            uint64_t toFrame = mgr->GetSessionInfo().currentEndFrame;
            if (argsTable["to_frame"].valid()) toFrame = argsTable.get<uint64_t>("to_frame");
            std::string kindStr = "executed";
            if (argsTable["kind"].valid()) kindStr = argsTable.get<std::string>("kind");
            ttd::TTDCoverageKind kind = ttd::TTDCoverageKind::Executed;
            ttd::TTDCoverageKindFromString(kindStr, kind);

            uint16_t addrFrom = 0;
            if (argsTable["addr_from"].valid()) addrFrom = static_cast<uint16_t>(argsTable.get<uint32_t>("addr_from"));
            uint16_t addrTo = 0xFFFF;
            if (argsTable["addr_to"].valid()) addrTo = static_cast<uint16_t>(argsTable.get<uint32_t>("addr_to"));
            size_t limit = 200;
            if (argsTable["limit"].valid()) limit = static_cast<size_t>(argsTable.get<uint32_t>("limit"));

            std::optional<ttd::PhysPage> physPage;
            if (argsTable["phys_page"].valid())
            {
                const uint32_t page = argsTable.get<uint32_t>("phys_page");
                if (page > ttd::kPhysPageMax)
                {
                    result["index_available"] = false;
                    result["error"] = "phys_page expects 0..255";
                    return result;
                }
                physPage = static_cast<ttd::PhysPage>(page);
            }

            auto res = mgr->QueryCoverageScan(fromFrame, toFrame, kind, addrFrom, addrTo, physPage, limit);
            result["kind"] = ttd::TTDCoverageKindToString(res.kind);
            result["scanned_frames"] = res.scannedFrames;
            result["matching_frames"] = res.matchingFrames;
            result["first_match"] = res.firstMatch;
            result["last_match"] = res.lastMatch;
            result["covered_from"] = res.coveredFrom;
            result["covered_to"] = res.coveredTo;
            result["truncated"] = res.truncated;
            result["index_available"] = res.indexAvailable;

            sol::table framesTbl = lua_view.create_table();
            for (size_t i = 0; i < res.frames.size(); ++i)
            {
                framesTbl[i + 1] = res.frames[i];
            }
            result["frames"] = framesTbl;
            return result;
        });

        lua.set_function("ttd_coverage_summary", [this](sol::table argsTable) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator || !emulator->GetContext() || !emulator->GetContext()->pTimeTravelManager)
            {
                result["index_available"] = false;
                result["buckets"] = lua_view.create_table();
                return result;
            }
            auto* mgr = emulator->GetContext()->pTimeTravelManager;
            uint64_t fromFrame = 0;
            if (argsTable["from_frame"].valid()) fromFrame = argsTable.get<uint64_t>("from_frame");
            uint64_t toFrame = mgr->GetSessionInfo().currentEndFrame;
            if (argsTable["to_frame"].valid()) toFrame = argsTable.get<uint64_t>("to_frame");
            std::optional<ttd::TTDCoverageKind> optKind;
            if (argsTable["kind"].valid())
            {
                ttd::TTDCoverageKind k;
                if (ttd::TTDCoverageKindFromString(argsTable.get<std::string>("kind"), k)) optKind = k;
            }
            uint64_t bucketSize = 0;
            if (argsTable["bucket_size"].valid()) bucketSize = argsTable.get<uint64_t>("bucket_size");
            size_t limit = 100;
            if (argsTable["limit"].valid()) limit = static_cast<size_t>(argsTable.get<uint32_t>("limit"));

            auto res = mgr->QueryCoverageSummary(fromFrame, toFrame, optKind, bucketSize, limit);
            result["from_frame"] = res.fromFrame;
            result["to_frame"] = res.toFrame;
            result["covered_from"] = res.coveredFrom;
            result["covered_to"] = res.coveredTo;
            result["bucket_size"] = res.bucketSize;
            result["bucket_count"] = res.bucketCount;
            result["index_available"] = res.indexAvailable;

            sol::table bucketsTbl = lua_view.create_table();
            for (size_t i = 0; i < res.buckets.size(); ++i)
            {
                sol::table bObj = lua_view.create_table();
                bObj["frame_start"] = res.buckets[i].frameStart;
                bObj["frame_end"] = res.buckets[i].frameEnd;
                bObj["executed_distinct"] = res.buckets[i].executedDistinct;
                bObj["written_distinct"] = res.buckets[i].writtenDistinct;
                bObj["read_distinct"] = res.buckets[i].readDistinct;
                bObj["has_keyframe"] = res.buckets[i].hasKeyframe;
                bucketsTbl[i + 1] = bObj;
            }
            result["buckets"] = bucketsTbl;
            return result;
        });

        // ====================================================================
        // Phase-2 analysis capabilities — parity with WebAPI/MCP/CLI:
        // step out, skip until, memory find, screen digest, beam, frame cost,
        // coverage, AY log, audio capture, assembler, label resolve, listings.
        // ====================================================================

        // Step out of the current subroutine (emulation ends paused)
        lua.set_function("step_out", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }

            try
            {
                emulator->StepOut();
                Z80State* z80 = emulator->GetZ80State();
                if (z80)
                {
                    result["pc"] = z80->pc;
                    result["sp"] = z80->sp;
                }
                result["ok"] = true;
            }
            catch (const std::exception& e)
            {
                result["ok"] = false;
                result["error"] = e.what();
            }
            return result;
        });

        // Fast-forward until PC reaches the target (breakpoints skipped)
        lua.set_function("skip_until", [this](sol::object pcValue, sol::optional<unsigned> maxTStatesOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }

            uint32_t target32 = 0;
            if (pcValue.is<std::string>())
            {
                try { target32 = static_cast<uint32_t>(std::stoul(pcValue.as<std::string>(), nullptr, 0)); }
                catch (...) { result["error"] = "invalid pc"; return result; }
            }
            else if (pcValue.is<int>())
            {
                target32 = static_cast<uint32_t>(pcValue.as<int>());
            }
            else
            {
                result["error"] = "pc must be a number or hex string";
                return result;
            }

            if (target32 > 0xFFFF) { result["error"] = "pc out of 16-bit range"; return result; }
            const uint16_t target = static_cast<uint16_t>(target32);

            // Safety budget: default 100 frames of emulated time, hard cap 200 s
            EmulatorContext* context = emulator->GetContext();
            unsigned maxTStates = maxTStatesOpt.value_or(0);
            if (maxTStates == 0 && context)
                maxTStates = context->config.frame * 100;
            if (maxTStates == 0)
                maxTStates = 6988800;
            if (maxTStates > 700000000u)
                maxTStates = 700000000u;

            emulator->RunUntilCondition([target](const Z80State& state) { return state.pc == target; }, maxTStates);

            Z80State* z80 = emulator->GetZ80State();
            result["hit"] = z80 && z80->pc == target;
            result["max_tstates"] = maxTStates;
            if (z80)
            {
                result["pc"] = z80->pc;
                result["sp"] = z80->sp;
            }
            return result;
        });

        // Search the CPU view of memory for a byte pattern (hex string or byte table)
        lua.set_function("mem_find",
                         [this](sol::object patternValue, sol::optional<unsigned> startOpt,
                                sol::optional<unsigned> endOpt, sol::optional<unsigned> alignOpt,
                                sol::optional<unsigned> maxOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }

            Memory* memory = emulator->GetMemory();
            if (!memory) { result["error"] = "memory not available"; return result; }

            std::vector<uint8_t> pattern;
            if (patternValue.is<std::string>())
            {
                std::string digits;
                for (char c : patternValue.as<std::string>())
                {
                    if (c == ' ' || c == ':')
                        continue;
                    if (!std::isxdigit(static_cast<unsigned char>(c)))
                    {
                        result["error"] = "invalid hex pattern";
                        return result;
                    }
                    digits += static_cast<char>(std::toupper(c));
                }
                if (digits.empty() || digits.size() % 2 != 0)
                {
                    result["error"] = "invalid hex pattern";
                    return result;
                }
                for (size_t i = 0; i < digits.size(); i += 2)
                    pattern.push_back(static_cast<uint8_t>(std::stoul(digits.substr(i, 2), nullptr, 16)));
            }
            else if (patternValue.is<sol::table>())
            {
                for (auto& pair : patternValue.as<sol::table>())
                    pattern.push_back(static_cast<uint8_t>(pair.second.as<int>() & 0xFF));
            }

            if (pattern.empty() || pattern.size() > 64)
            {
                result["error"] = "pattern must be 1..64 bytes";
                return result;
            }

            const size_t start = startOpt.value_or(0);
            const size_t end = std::min<size_t>(endOpt.value_or(0xFFFF), 0xFFFF);
            const unsigned alignment = alignOpt.value_or(1);
            const unsigned max = maxOpt.value_or(64);
            if (start > end || (alignment != 1 && alignment != 2))
            {
                result["error"] = "invalid range or alignment";
                return result;
            }

            sol::table matches = lua_view.create_table();
            size_t found = 0;
            bool truncated = false;
            const size_t searchLimit = end - pattern.size() + 1;

            for (size_t position = start; position <= searchLimit; position += alignment)
            {
                if (memory->DirectReadFromZ80Memory(static_cast<uint16_t>(position)) != pattern[0])
                    continue;

                bool matched = true;
                for (size_t i = 1; i < pattern.size(); i++)
                {
                    if (memory->DirectReadFromZ80Memory(static_cast<uint16_t>(position + i)) != pattern[i])
                    {
                        matched = false;
                        break;
                    }
                }
                if (!matched)
                    continue;

                if (found >= max)
                {
                    truncated = true;
                    break;
                }

                sol::table match = lua_view.create_table();
                match["address"] = static_cast<unsigned>(position);
                sol::table context = lua_view.create_table();
                const size_t contextStart = position > 4 ? position - 4 : 0;
                for (size_t i = 0; i < pattern.size() + 4; i++)
                {
                    const size_t address = contextStart + i;
                    if (address > 0xFFFF)
                        break;
                    context[i + 1] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(address));
                }
                match["context"] = context;
                matches[found + 1] = match;
                found++;
            }

            result["matches"] = matches;
            result["count"] = found;
            result["truncated"] = truncated;
            return result;
        });

        // Screen-area FNV-1a-64 digest — change detection without pixel transfer.
        // screen_digest()                            -> default banks (both screen pages on 128K)
        // screen_digest(start, end)                  -> explicit Z80 range
        // screen_digest(nil, nil, nil, "active")     -> banks of the video mode actually
        //                                               displayed (P1-3; explicit range wins)
        lua.set_function("screen_digest", [this](sol::optional<unsigned> startOpt,
                                                 sol::optional<unsigned> endOpt,
                                                 sol::optional<bool> includeBorderOpt,
                                                 sol::optional<std::string> modeOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }

            EmulatorContext* context = emulator->GetContext();
            if (!context || !context->pMemory || !context->pScreen)
            {
                result["error"] = "context not initialized";
                return result;
            }

            // One computation for every interface (ScreenDigestCompute): the same pages, surface and change tracking
            ScreenDigestQuery query;
            if (modeOpt.has_value())
            {
                if (*modeOpt == "active")
                    query.active = true;
                else if (*modeOpt != "default")
                {
                    result["error"] = "mode must be 'default' or 'active'";
                    return result;
                }
            }
            if (startOpt.has_value() || endOpt.has_value())
            {
                query.range = true;
                query.start = static_cast<uint16_t>(startOpt.value_or(0x4000));
                query.end = static_cast<uint16_t>(endOpt.value_or(0x7FFF));
                if (query.start > query.end)
                {
                    result["error"] = "start must be <= end";
                    return result;
                }
            }
            query.includeBorder = includeBorderOpt.value_or(true);
            const ScreenDigestResult r = ScreenDigestCompute::Compute(context, query);
            if (!r.ok)
            {
                result["error"] = r.error;
                return result;
            }
            if (r.range)
            {
                result["range_start"] = r.start;
                result["range_end"] = r.end;
                result["range_digest"] = r.rangeDigest;
            }
            else if (r.deviceSurface)
            {
                // A picture outside the RAM pages (the Sprinter's video RAM)
                sol::table activeSurface = lua_view.create_table();
                activeSurface["video_mode"] = r.videoMode;
                activeSurface["memory"] = r.surface.name;
                activeSurface["bytes"] = r.surface.bytes;
                activeSurface["digest"] = r.surface.digest;
                result["active_surface"] = activeSurface;
            }
            else
            {
                if (r.activeSurface)
                {
                    sol::table pages = lua_view.create_table();
                    for (uint16_t page : r.activePages)
                        pages.add(page);
                    sol::table activeSurface = lua_view.create_table();
                    activeSurface["video_mode"] = r.videoMode;
                    activeSurface["pages"] = pages;
                    result["active_surface"] = activeSurface;
                }
                sol::table perBank = lua_view.create_table();
                for (const auto& [page, digest] : r.banks)
                    perBank[page] = digest;
                result["banks"] = perBank;
            }
            if (r.includeBorder)
                result["border_color"] = r.border;
            result["combined"] = r.combined;
            result["frame"] = r.frame;
            result["algorithm"] = "fnv1a-64";
            result["changed"] = r.changed;
            result["previous_digest"] = r.previousDigest;
            if (r.previousFrame != 0)
                result["previous_digest_frame"] = r.previousFrame;
            return result;
        });

        // Static port-map introspection (P1-5): which devices answer which I/O
        // ports on this model, under which gating conditions, plus the live
        // routing flags. Mirrors GET /api/v1/emulator/{id}/ports
        // (PortDecoder::getPortMapEntries / GetMouseRoutingState, single source).
        lua.set_function("ports_map", [this](sol::this_state s) -> sol::variadic_results {
            sol::variadic_results results;
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return mouseError(s, "no emulator");

            EmulatorContext* context = emulator->GetContext();
            if (!context || !context->pPortDecoder)
                return mouseError(s, "context not initialized");

            auto portHex = [](uint16_t value) {
                char text[8];
                std::snprintf(text, sizeof(text), "0x%04X", value);
                return std::string(text);
            };

            sol::state_view lua(s);
            sol::table result = lua.create_table();
            result["model"] = Config::GetModelFullName(context->config.mem_model);

            sol::table entries = lua.create_table();
            for (const PortMapEntry& entry : context->pPortDecoder->getPortMapEntries())
            {
                sol::table item = lua.create_table();
                item["port"] = portHex(entry.port);
                item["mask"] = portHex(entry.mask);
                item["match"] = portHex(entry.match);
                item["device"] = entry.device;
                if (entry.gate)  // absent key = ungated (WebAPI sends null)
                    item["gate"] = entry.gate;

                // Tagged registry fields (P1-2): names from the core single
                // source - identical strings on WebAPI /ports, MCP and Python
                sol::table tagNames = lua.create_table();
                for (const std::string& tagName : PortTagSetToStrings(entry.tags))
                    tagNames.add(tagName);
                item["tags"] = tagNames;  // empty table = untagged row
                const char* latchName = PagingLatchToString(entry.latch);
                if (latchName)  // absent key = no live-value binding
                    item["latch"] = latchName;

                entries.add(item);
            }
            result["entries"] = entries;

            bool mouseDecoded = false;
            std::string mouseNote;
            context->pPortDecoder->GetMouseRoutingState(mouseDecoded, mouseNote);

            const CONFIG& config = context->config;
            const EmulatorState& state = context->emulatorState;
            sol::table live = lua.create_table();
            live["trdos_active"] = (state.flags & (CF_TRDOS | CF_DOSPORTS)) != 0;
            live["mouse_ports_decoded"] = mouseDecoded;
            live["mouse_routing_note"] = mouseNote;
            const bool scorpion = (config.mem_model == MM_SCORP || config.mem_model == MM_PROFSCORP);
            if (scorpion)
                live["shadow_monitor_paged"] = (state.p1FFD & 0x02) != 0;
            // else: key absent = the #1FFD latch does not exist on this model
            // Sprinter: the map / DOS / PN5 the port table is read with now (as WebAPI /ports)
            if (config.mem_model == MM_SPRINTER)
            {
                const StateNode sprinter = DeviceState::Sprinter(context);
                if (const StateNode* decoderNode = sprinter.find("decoder"))
                    live["sprinter_port_table"] = StateNodeToLua(s, *decoderNode);
            }
            result["live"] = live;

            results.push_back(sol::make_object(s, result));
            return results;
        });

        // Tagged paging latches + bank table (P1-2 design).
        // Mirrors GET /api/v1/emulator/{id}/state/paging.
        lua.set_function("paging_state", [this](sol::this_state s) -> sol::variadic_results {
            sol::variadic_results results;
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return mouseError(s, "no emulator");

            EmulatorContext* context = emulator->GetContext();
            if (!context || !context->pPortDecoder || !context->pMemory)
                return mouseError(s, "context not initialized");

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

            sol::state_view lua(s);
            sol::table result = lua.create_table();
            const CONFIG& config = context->config;
            const EmulatorState& state = context->emulatorState;
            Memory& memory = *context->pMemory;
            PortDecoder* decoder = context->pPortDecoder;
            ROM* rom = context->pCore ? context->pCore->GetROM() : nullptr;

            result["model"] = Config::GetModelFullName(config.mem_model);
            if (IsProfiModel(config.mem_model))
            {
                // The board, its sync PROM and the keyboard on its connector (as GET /state/paging)
                result["profi_board"] = config.mem_model == MM_PROFI3 ? "v3" : "v5";
                result["profi_sync_prom"] = ProfiSyncPromName(
                    ProfiResolveSyncProm(static_cast<ProfiSyncProm>(config.profi_sync_prom), config.mem_model));
                result["profi_keyboard"] = ProfiKeyboardName(ProfiKeyboardInForce(context));
                // The hi-res clocks (design-hires.md): the CPU clock there (no turbo), the v5's ZQ3 and SB7
                result["profi_hires_cpu_hz"] = ProfiHiresCpuHz(config.mem_model == MM_PROFI, config.profi_zq3_mhz);
                result["profi_zq3_mhz"] = static_cast<int>(ProfiClampZq3(config.profi_zq3_mhz));
                result["profi_ay_clock"] = (config.mem_model == MM_PROFI && config.profi_ay_clock_new) ? "new" : "old";
            }
            result["paging_locked"] = (state.p7FFD & PORT_7FFD_LOCK) != 0;
            result["trdos_active"] = (state.flags & (CF_TRDOS | CF_DOSPORTS)) != 0;

            // Latches array
            sol::table latches = lua.create_table();
            for (const PortMapEntry& entry : decoder->GetPagingLatches(Tags(PortTag::Memory)))
            {
                sol::table latch = lua.create_table();
                latch["port"] = hexWord(entry.port);
                latch["device"] = entry.device ? entry.device : "";
                if (entry.gate) latch["gate"] = entry.gate;

                // Tag names + latch binding from the core single source -
                // identical strings on /state/paging, MCP, CLI and Python
                sol::table tagNames = lua.create_table();
                for (const std::string& tagName : PortTagSetToStrings(entry.tags))
                    tagNames.add(tagName);
                latch["tags"] = tagNames;
                const char* latchName = PagingLatchToString(entry.latch);
                if (latchName)
                    latch["latch"] = latchName;

                uint32_t value = PortDecoder::ReadPagingLatch(entry.latch, state);
                latch["value"] = hexByte(value);

                // Decoded bits: core §5.1 dictionary (DecodePagingLatch) with
                // native ints/bools, matching /state/paging verbatim
                sol::table decoded = lua.create_table();
                for (const DecodedLatchField& field : DecodePagingLatch(entry.latch, value, config.mem_model, config.ramsize))
                {
                    decoded[field.key] = field.isBool ? sol::make_object(s, field.boolValue)
                                                      : sol::make_object(s, field.intValue);
                }
                if (decoded.size() > 0)
                    latch["decoded"] = decoded;
                latches.add(latch);
            }
            result["latches"] = latches;

            // Banks array
            sol::table banks = lua.create_table();
            for (int i = 0; i < 4; ++i)
            {
                sol::table bank = lua.create_table();
                bank["bank"] = i;
                const char* ranges[] = {"0x0000-0x3FFF", "0x4000-0x7FFF", "0x8000-0xBFFF", "0xC000-0xFFFF"};
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
                banks.add(bank);
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
                        sol::table bank = banks[i + 1];
                        if (const StateNode* kind = windows->items[i].find("kind"))
                            bank["type"] = kind->s;
                        if (const StateNode* page = windows->items[i].find("page"))
                            bank["page"] = page->i;
                    }
                }
                result["sprinter"] = StateNodeToLua(s, sprinter);
            }
            result["banks"] = banks;

            results.push_back(sol::make_object(s, result));
            return results;
        });

        // Beam and video debug translation (PLAN #42): the same DeviceState reports every interface returns
        lua.set_function("beam_position", [this](sol::this_state s) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::VideoBeam(ctx));
        });
        lua.set_function("video_layout", [this](sol::this_state s) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::VideoLayout(ctx));
        });
        lua.set_function("video_pixel", [this](sol::this_state s, unsigned x, unsigned y, sol::optional<unsigned> layer) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::VideoPixel(ctx, layer.value_or(0), x, y));
        });
        lua.set_function("video_pixel_at", [this](sol::this_state s, unsigned t) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::VideoPixelAtBeam(ctx, t));
        });
        lua.set_function("video_address", [this](sol::this_state s, unsigned page, unsigned offset) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::VideoAddress(ctx, page, offset));
        });
        // video_address_in(space, offset[, page]): "ram", "sprite_ram" or "palette"
        lua.set_function("video_address_in", [this](sol::this_state s, const std::string& space, unsigned offset,
                                                    sol::optional<unsigned> page) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::VideoAddressIn(ctx, space, page.value_or(0), offset));
        });
        lua.set_function("video_address_z80", [this](sol::this_state s, unsigned address) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::VideoAddressZ80(ctx, address));
        });
        // video_changes([frames]): the video change log (DeviceState::VideoChanges): 1 = the last completed frame,
        // 2 (default) = the current one too while paused
        lua.set_function("video_changes", [this](sol::this_state s, sol::optional<unsigned> frames) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::VideoChanges(emulator->GetContext(), frames.value_or(2)));
        });
        lua.set_function("video_text", [this](sol::this_state s, sol::optional<unsigned> layer) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::VideoText(ctx, layer.value_or(0)));
        });
        // Temporal effects (ZX DLSS de-flicker): status and switch, the TemporalStatus report every interface returns
        lua.set_function("video_temporal", [this](sol::this_state s) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, TemporalStatus::Report(ctx));
        });
        lua.set_function("video_temporal_set", [this](sol::this_state s, const std::string& name) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            if (!TemporalStatus::Set(ctx, name))
            {
                StateNode error = StateNode::Object();
                error["ok"] = false;
                error["error"] = ctx ? "Unknown temporal algorithm '" + name + "'. Valid: " + TemporalStatus::OfferedList() + ", off"
                                     : std::string("No emulator selected");
                return StateNodeToLua(s, error);
            }
            return StateNodeToLua(s, TemporalStatus::Report(ctx));
        });

        // Halt/active cost of the last frame plus session averages
        lua.set_function("frame_cost", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }

            EmulatorContext* context = emulator->GetContext();
            if (!context) { result["error"] = "no context"; return result; }

            const CONFIG& config = context->config;
            const EmulatorState& state = context->emulatorState;

            const uint64_t frameBudget = static_cast<uint64_t>(config.frame) * state.current_z80_frequency_multiplier;
            const uint64_t lastHalted = state.tstates_halted_last;
            const uint64_t lastActive = frameBudget > lastHalted ? frameBudget - lastHalted : 0;

            sol::table last = lua_view.create_table();
            last["tstates_total"] = frameBudget;
            last["tstates_halted"] = lastHalted;
            last["tstates_active"] = lastActive;
            last["halted_percent"] = frameBudget ? lastHalted * 100.0 / frameBudget : 0.0;
            result["last"] = last;

            sol::table average = lua_view.create_table();
            average["frames"] = static_cast<uint64_t>(state.frame_cost_frames);
            average["tstates_total"] = static_cast<uint64_t>(state.tstates_frame_total);
            average["tstates_halted"] = static_cast<uint64_t>(state.tstates_halted_total);
            average["tstates_active"] = static_cast<uint64_t>(state.tstates_frame_total - state.tstates_halted_total);
            result["average"] = average;
            return result;
        });

        // Code coverage control and queries
        lua.set_function("coverage_start", [this](sol::optional<bool> keepOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* context = emulator->GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            CoverageAnalyzer* coverage = manager ? manager->getAnalyzer<CoverageAnalyzer>("coverage") : nullptr;
            if (!coverage || !manager) { result["error"] = "coverage analyzer not available"; return result; }

            if (!keepOpt.value_or(false))
                coverage->clear();
            result["success"] = manager->activate("coverage");
            result["recording"] = coverage->isRecording();
            return result;
        });

        lua.set_function("coverage_stop", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* context = emulator->GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            if (!manager) { result["error"] = "analyzer manager not available"; return result; }

            result["success"] = manager->deactivate("coverage");
            return result;
        });

        lua.set_function("coverage_status", [this](sol::optional<unsigned> maxRangesOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* context = emulator->GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            CoverageAnalyzer* coverage = manager ? manager->getAnalyzer<CoverageAnalyzer>("coverage") : nullptr;
            if (!coverage || !manager) { result["error"] = "coverage analyzer not available"; return result; }

            const size_t executedCount = coverage->getExecutedCount();
            result["active"] = manager->isActive("coverage");
            result["recording"] = coverage->isRecording();
            result["executed_count"] = executedCount;
            result["coverage_percent"] = executedCount * 100.0 / 65536.0;
            result["instructions"] = static_cast<uint64_t>(coverage->getInstructionCount());

            sol::table ranges = lua_view.create_table();
            size_t index = 0;
            for (const auto& range : coverage->getExecutedRanges(maxRangesOpt.value_or(100)))
            {
                sol::table item = lua_view.create_table();
                item["start"] = range.first;
                item["end"] = range.second;
                ranges[index + 1] = item;
                index++;
            }
            result["ranges"] = ranges;
            return result;
        });

        lua.set_function("coverage_gaps",
                         [this](sol::optional<unsigned> startOpt, sol::optional<unsigned> endOpt,
                                sol::optional<unsigned> maxOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* context = emulator->GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            CoverageAnalyzer* coverage = manager ? manager->getAnalyzer<CoverageAnalyzer>("coverage") : nullptr;
            if (!coverage || !manager) { result["error"] = "coverage analyzer not available"; return result; }

            const uint16_t start = static_cast<uint16_t>(startOpt.value_or(0x4000));
            const uint16_t end = static_cast<uint16_t>(endOpt.value_or(0xFFFF));

            sol::table gaps = lua_view.create_table();
            size_t index = 0;
            for (const auto& gap : coverage->getGaps(start, end, maxOpt.value_or(100)))
            {
                sol::table item = lua_view.create_table();
                item["start"] = gap.first;
                item["end"] = gap.second;
                gaps[index + 1] = item;
                index++;
            }
            result["gaps"] = gaps;
            result["count"] = index;
            return result;
        });

        // AY register-write logging
        lua.set_function("ay_log_start", [this](sol::optional<unsigned> capacityOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* context = emulator->GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            AYLogAnalyzer* aylog = manager ? manager->getAnalyzer<AYLogAnalyzer>("aylog") : nullptr;
            if (!aylog || !manager) { result["error"] = "AY log analyzer not available"; return result; }

            result["success"] = manager->activate("aylog");
            aylog->setCapacity(capacityOpt.value_or(4096));
            result["capacity"] = aylog->getCapacity();
            return result;
        });

        lua.set_function("ay_log_stop", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* context = emulator->GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            if (!manager) { result["error"] = "analyzer manager not available"; return result; }

            result["success"] = manager->deactivate("aylog");
            return result;
        });

        lua.set_function("ay_log_status", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* context = emulator->GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            AYLogAnalyzer* aylog = manager ? manager->getAnalyzer<AYLogAnalyzer>("aylog") : nullptr;
            if (!aylog || !manager) { result["error"] = "AY log analyzer not available"; return result; }

            result["active"] = manager->isActive("aylog");
            result["recording"] = aylog->isRecording();
            result["entry_count"] = static_cast<uint64_t>(aylog->getEntryCount());
            result["capacity"] = static_cast<uint64_t>(aylog->getCapacity());
            result["dropped"] = static_cast<uint64_t>(aylog->getDroppedCount());
            return result;
        });

        lua.set_function("ay_log_dump", [this](sol::optional<unsigned> countOpt,
                                                sol::optional<unsigned> offsetOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* context = emulator->GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            AYLogAnalyzer* aylog = manager ? manager->getAnalyzer<AYLogAnalyzer>("aylog") : nullptr;
            if (!aylog || !manager) { result["error"] = "AY log analyzer not available"; return result; }

            const size_t total = aylog->getEntryCount();
            const size_t count = countOpt.value_or(20);
            const size_t offset = offsetOpt.value_or(total > count ? static_cast<unsigned>(total - count) : 0u);

            sol::table records = lua_view.create_table();
            size_t index = 0;
            for (const auto& record : aylog->getEntries(offset, count))
            {
                sol::table item = lua_view.create_table();
                item["frame"] = static_cast<uint64_t>(record.frame);
                item["tacts"] = record.tacts;
                item["pc"] = record.pc;
                item["port"] = record.port;
                item["chip"] = record.chip;
                item["reg"] = record.reg;
                item["value"] = record.value;
                item["type"] = record.port == 0xFFFD ? (record.value > 0x0F ? "switch" : "select") : "write";
                records[index + 1] = item;
                index++;
            }
            result["records"] = records;
            result["total"] = static_cast<uint64_t>(total);
            return result;
        });

        // Buffered stereo audio capture (records into analyzer RAM, then export)
        // audio_mixer(): the per-device mixer (DeviceState::AudioMixer); audio_mixer_set(source, {muted=, solo=,
        // volume=, gain_db=}) -> the mixer, or nil, error (AudioMixer::Apply; source "master" takes muted only)
        lua.set_function("audio_mixer", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::AudioMixer(emulator->GetContext()));
        });
        lua.set_function("audio_mixer_set", [this](sol::this_state s, const std::string& source, sol::table options) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            auto text = [&](const char* key) -> std::string {
                sol::object value = options[key];
                if (value.get_type() == sol::type::boolean)
                    return value.as<bool>() ? "1" : "0";
                if (value.get_type() == sol::type::number)
                    return std::to_string(value.as<double>());
                if (value.get_type() == sol::type::string)
                    return value.as<std::string>();
                return std::string();
            };
            AudioMixer::Change change;
            std::string error;
            if (!AudioMixer::ChangeFromStrings(text("muted"), text("solo"), text("volume"), text("gain_db"), change, error) ||
                !AudioMixer::Apply(emulator->GetContext(), source, change, error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, DeviceState::AudioMixer(emulator->GetContext())));
            return out;
        });

        // audio_capture_start(seconds [, source]): source = a mixer key (beeper, ay1, covox, gs ...) records that
        // device's own buffer; default the master mix
        lua.set_function("audio_capture_start", [this](double seconds, sol::optional<std::string> sourceKey) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* context = emulator->GetContext();
            if (!context || !context->pDebugManager) { result["error"] = "debug manager not available"; return result; }

            if (seconds < 0.01 || seconds > 30.0)
            {
                result["error"] = "seconds must be within [0.01, 30.0]";
                return result;
            }

            AnalyzerManager* manager = context->pDebugManager->GetAnalyzerManager();
            AudioCaptureAnalyzer* capture =
                manager ? manager->getAnalyzer<AudioCaptureAnalyzer>("audiocapture") : nullptr;
            if (!capture || !manager) { result["error"] = "audio capture analyzer not available"; return result; }

            AudioSourceType source = AudioSourceType::MasterMix;
            std::string sourceError;
            if (!AudioMixer::Capturable(context, sourceKey.value_or(""), source, sourceError))
            {
                result["error"] = sourceError;
                return result;
            }

            const size_t rate = context->pSoundManager ? context->pSoundManager->getCoreRate() : 44100;
            const size_t target = static_cast<size_t>(seconds * static_cast<double>(rate)) * 2;

            manager->activate("audiocapture");
            capture->startCapture(target, source);

            result["armed"] = capture->isCaptureArmed();
            result["source"] = AudioMixer::Key(source);
            result["target_samples"] = static_cast<uint64_t>(target);
            result["sample_rate"] = static_cast<uint64_t>(rate);
            return result;
        });

        lua.set_function("audio_capture_status", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* context = emulator->GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            AudioCaptureAnalyzer* capture =
                manager ? manager->getAnalyzer<AudioCaptureAnalyzer>("audiocapture") : nullptr;
            if (!capture || !manager) { result["error"] = "audio capture analyzer not available"; return result; }

            result["armed"] = capture->isCaptureArmed();
            result["complete"] = capture->isCaptureComplete();
            result["captured_samples"] = static_cast<uint64_t>(capture->getCapturedSamples());
            result["target_samples"] = static_cast<uint64_t>(capture->getTargetSamples());
            return result;
        });

        // Capture stats (peak/RMS per channel) + optional WAV export
        lua.set_function("audio_capture_result", [this](sol::optional<std::string> pathOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* context = emulator->GetContext();
            if (!context || !context->pDebugManager) { result["error"] = "debug manager not available"; return result; }

            AnalyzerManager* manager = context->pDebugManager->GetAnalyzerManager();
            AudioCaptureAnalyzer* capture =
                manager ? manager->getAnalyzer<AudioCaptureAnalyzer>("audiocapture") : nullptr;
            if (!capture || !manager) { result["error"] = "audio capture analyzer not available"; return result; }

            const auto& buffer = capture->getBuffer();
            const size_t frames = buffer.size() / 2;
            if (frames == 0)
            {
                result["error"] = "no captured audio — call audio_capture_start first";
                return result;
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

            result["frames"] = static_cast<uint64_t>(frames);
            result["sample_rate"] = static_cast<uint64_t>(rate);
            result["duration_seconds"] = static_cast<double>(frames) / rate;
            result["complete"] = capture->isCaptureComplete();
            result["left_peak"] = peak[0];
            result["left_rms"] = std::sqrt(sumSquares[0] / frames);
            result["right_peak"] = peak[1];
            result["right_rms"] = std::sqrt(sumSquares[1] / frames);

            // Optional WAV export to the given path
            if (pathOpt.has_value())
            {
                TinyWav wav{};
                if (tinywav_open_write(&wav, 2, static_cast<int32_t>(rate), TW_INT16, TW_INTERLEAVED,
                                       pathOpt->c_str()) == 0)
                {
                    tinywav_write_i(&wav, const_cast<void*>(static_cast<const void*>(buffer.data())),
                                    static_cast<int>(frames));
                    tinywav_close_write(&wav);
                    result["saved"] = *pathOpt;
                }
                else
                {
                    result["save_error"] = "failed to open wav";
                }
            }
            return result;
        });

        // Core audio rate control (same switch as CLI 'setting audio_rate' and
        // WebAPI PUT settings/audio_rate). Pin the rate (44100..192000) for
        // this run - never persisted to the ini; 0 = auto (follow the
        // priority chain: device > [SOUND] CoreRate > 44100). Applied at the
        // next frame boundary; deferred while a recording is in progress.
        // Host speed multiplier 1/2/4/8/16 (same switch as CLI 'setting speed N'
        // and WebAPI setting 'speed'). Refused (false) while TTD records.
        lua.set_function("set_speed", [this](int multiplier) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            if (multiplier != 1 && multiplier != 2 && multiplier != 4 && multiplier != 8 && multiplier != 16)
                return false;
            return emulator->SetSpeedMultiplier(static_cast<uint8_t>(multiplier));
        });

        lua.set_function("get_speed", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* context = emulator->GetContext();
            if (!context || !context->pCore) { result["error"] = "core not available"; return result; }
            FeatureManager* fm = emulator->GetFeatureManager();
            result["multiplier"]   = context->pCore->GetHostSpeedMultiplier();
            result["effective"]    = context->pCore->GetSpeedMultiplier();  // with the machine's hardware turbo
            result["turbo_mode"]   = fm && fm->isEnabled(Features::kTurboMode);
            result["turbo_active"] = context->config.turbo_mode;
            result["turbo_audio"]  = context->config.turbo_mode_audio;
            return result;
        });

        lua.set_function("set_audio_rate", [this](int rate) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* context = emulator->GetContext();
            if (!context || !context->pSoundManager) return false;
            context->pSoundManager->setCoreRatePin(static_cast<uint32_t>(rate));
            return context->pSoundManager->getCoreRatePin() == static_cast<uint32_t>(rate);
        });

        lua.set_function("get_audio_rate", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* context = emulator->GetContext();
            if (!context || !context->pSoundManager) { result["error"] = "sound manager not available"; return result; }
            SoundManager* sound = context->pSoundManager;
            result["pin"] = sound->getCoreRatePin();  // 0 = auto
            result["core_rate"] = static_cast<uint64_t>(sound->getCoreRate());
            result["target_rate"] = static_cast<uint64_t>(sound->getTargetCoreRate());
            return result;
        });

        // Sound character (same source as CLI 'setting ay_voicing|ay_punch|
        // ay_room|beeper_punch' and WebAPI PUT settings/<name>): runtime only,
        // applied at the next frame boundary. get_sound_character() returns
        // all four as text; set_sound_character(name, value) returns
        // {ok=true, value=...} or {ok=false, error=...}
        lua.set_function("get_sound_character", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* context = emulator->GetContext();
            if (!context || !context->pSoundManager) { result["error"] = "sound manager not available"; return result; }
            for (const SoundCharacterSettings::Descriptor& d : SoundCharacterSettings::Descriptors())
                result[d.name] = SoundCharacterSettings::Get(*context->pSoundManager, d.name);
            return result;
        });

        lua.set_function("set_sound_character", [this](const std::string& name, const std::string& value) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            result["ok"] = false;
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* context = emulator->GetContext();
            if (!context || !context->pSoundManager) { result["error"] = "sound manager not available"; return result; }
            std::string error;
            if (!SoundCharacterSettings::Set(*context->pSoundManager, name, value, error))
            {
                result["error"] = error;
                return result;
            }
            result["ok"] = true;
            result["value"] = SoundCharacterSettings::Get(*context->pSoundManager, name);
            return result;
        });

        // Video recording control over the RecordingManager (mirrors POST /video/record)
#ifdef ENABLE_RECORDING
        lua.set_function("video_record", [this](const std::string& action, sol::optional<sol::table> optsOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }

            auto* context = emulator->GetContext();
            RecordingManager* rm = context ? context->pRecordingManager : nullptr;
            if (!rm) { result["error"] = "recording manager not available"; return result; }

            if (action == "start")
            {
                if (rm->IsRecording() || rm->IsPaused())
                {
                    result["error"] = "a recording is already active — stop it first";
                    return result;
                }

                sol::table opts = lua_view.create_table();
                if (optsOpt.has_value() && optsOpt->valid()) opts = *optsOpt;

                std::string format = opts.get_or<std::string>("format", "gif");
                std::string extension = format;
                if (format == "h264" || format == "h265" || format == "hevc" || format == "vp9")
                    extension = "mp4";
                else if (format == "rawvideo")
                    extension = "avi";

                std::string filename = opts.get_or<std::string>("filename", "");
                if (filename.empty())
                {
                    std::filesystem::path dir = std::filesystem::temp_directory_path() / "unreal-lua";
                    std::error_code ec;
                    std::filesystem::create_directories(dir, ec);
                    static std::atomic<unsigned> counter{0};
                    const long long stamp =
                        static_cast<long long>(std::time(nullptr)) * 1000 + (counter++ % 1000);
                    filename = (dir / ("video-" + std::to_string(stamp) + "." + extension)).string();
                }

                // Optional sound track: audio = "aac" (or true = aac); none = video only. Same rules as the
                // WebAPI/CLI (RecordingRequest): the codec must fit the container, gif has no audio
                std::string audio;
                {
                    const sol::object audioObj = opts["audio"];
                    if (audioObj.valid() && audioObj.get_type() != sol::type::lua_nil)
                    {
                        if (audioObj.is<bool>())
                            audio = audioObj.as<bool>() ? "aac" : "";
                        else if (audioObj.is<std::string>())
                            audio = RecordingRequest::NormalizeAudioCodec(audioObj.as<std::string>());
                        else
                        {
                            result["error"] = "audio must be a codec name (aac, mp3, opus, vorbis, flac, pcm_s16le) or a boolean";
                            return result;
                        }
                    }
                }
                const int videoBitrate = opts.get_or("video_bitrate", 0);
                const int audioBitrate = opts.get_or("audio_bitrate", 0);
                if (videoBitrate < 0 || audioBitrate < 0)
                {
                    result["error"] = "video_bitrate / audio_bitrate must be >= 0 (kbps)";
                    return result;
                }
                {
                    std::string codecError = RecordingRequest::ValidateAudio(format, filename, audio);
                    if (codecError.empty())
                        codecError = RecordingRequest::ValidateBitrates(static_cast<uint32_t>(videoBitrate),
                                                                        static_cast<uint32_t>(audioBitrate), audio);
                    if (!codecError.empty())
                    {
                        result["error"] = codecError;
                        return result;
                    }
                }

                float fps = opts.get_or("fps", 50.0f);
                if (fps < 1.0f) fps = 1.0f;
                if (fps > 100.0f) fps = 100.0f;
                rm->SetVideoFrameRate(fps);

                int scale = opts.get_or("scale", 1);
                if (scale < 1) scale = 1;
                if (scale > 4) scale = 4;
                rm->SetScaleFactor(static_cast<uint32_t>(scale));

                const std::string region = opts.get_or<std::string>("region", "full");
                rm->SetCaptureRegion((region == "screen" || region == "main")
                                         ? VideoCaptureRegion::MainScreen
                                         : VideoCaptureRegion::FullFrame);

                // Optional audio-rate pin (same switch as CLI videorecord
                // --audio-rate): number or "auto"; applied at the next frame
                // boundary so the recording is stamped with the requested rate
                SoundManager* sound = context->pSoundManager;
                bool hasAudioRate = false;
                uint32_t audioRate = 0;
                {
                    const sol::object rateObj = opts["audio_rate"];
                    if (rateObj.valid())
                    {
                        hasAudioRate = true;
                        if (rateObj.is<int>())
                        {
                            audioRate = static_cast<uint32_t>(rateObj.as<int>());
                        }
                        else if (rateObj.is<std::string>() && rateObj.as<std::string>() == "auto")
                        {
                            audioRate = 0;
                        }
                        else
                        {
                            result["error"] = "audio_rate must be a number or 'auto'";
                            return result;
                        }
                    }
                }
                if (hasAudioRate)
                {
                    if (!sound)
                    {
                        result["error"] = "sound manager not available";
                        return result;
                    }
                    sound->setCoreRatePin(audioRate);
                    for (int attempt = 0; attempt < 100 && sound->getCoreRate() != sound->getTargetCoreRate(); attempt++)
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    if (sound->getCoreRate() != sound->getTargetCoreRate())
                    {
                        result["error"] = "audio_rate not applied within 1s (emulator paused?) - "
                                          "resume it or call set_audio_rate before pausing";
                        return result;
                    }
                }

                FeatureManager* fm = context->pFeatureManager;
                const bool featureWasOff = fm && !fm->isEnabled(Features::kRecording);
                if (featureWasOff) fm->setFeature(Features::kRecording, true);

                const bool wasRunning = emulator->IsRunning() && !emulator->IsPaused();
                if (wasRunning) emulator->Pause();

                const bool started = rm->StartRecording(filename, format, audio, static_cast<uint32_t>(videoBitrate),
                                                        static_cast<uint32_t>(audioBitrate));

                if (wasRunning) emulator->Resume();

                if (!started)
                {
                    if (featureWasOff) fm->setFeature(Features::kRecording, false);
                    result["error"] = "recording start failed";
                    result["message"] = rm->GetLastRecordingError();
                    return result;
                }

                result["recording"] = true;
                result["format"] = format;
                result["fps"] = fps;
                result["scale"] = scale;
                result["region"] = region;
                if (sound)
                    result["audio_rate"] = static_cast<uint64_t>(sound->getCoreRate());
                result["audio"] = rm->HasAudio();
                result["audio_codec"] = audio;
                result["audio_sample_rate"] = static_cast<uint64_t>(rm->HasAudio() ? rm->GetAudioSampleRate() : 0);
                result["audio_channels"] = static_cast<uint64_t>(rm->HasAudio() ? rm->GetAudioChannels() : 0);
                result["feature_auto_enabled"] = featureWasOff;
                result["output"] = filename;
                return result;
            }

            if (action == "stop")
            {
                if (!rm->IsRecording() && !rm->IsPaused())
                {
                    result["error"] = "no active recording to stop";
                    return result;
                }
                rm->StopRecording();
            }
            else if (action == "pause")
            {
                if (!rm->IsRecording())
                {
                    result["error"] = "no active recording to pause";
                    return result;
                }
                rm->PauseRecording();
            }
            else if (action == "resume")
            {
                if (!rm->IsPaused())
                {
                    result["error"] = "recording is not paused";
                    return result;
                }
                rm->ResumeRecording();
            }
            else
            {
                result["error"] = "unknown action '" + action + "' (expected start|stop|pause|resume)";
                return result;
            }

            const RecordingManager::RecordingStats stats = rm->GetStats();
            result["recording"] = rm->IsRecording();
            result["paused"] = rm->IsPaused();
            result["frames_recorded"] = static_cast<uint64_t>(stats.framesRecorded);
            result["recorded_duration"] = stats.recordedDuration;
            result["emulated_duration"] = stats.emulatedDuration;
            result["output_file_size"] = static_cast<uint64_t>(stats.outputFileSize);
            result["average_frame_time_ms"] = stats.averageFrameTime;
            result["recent_fps"] = stats.recentFps;
            result["audio_samples_recorded"] = static_cast<uint64_t>(stats.audioSamplesRecorded);
            result["video_codec"] = rm->GetVideoCodec();
            result["audio"] = rm->HasAudio();
            result["audio_codec"] = rm->HasAudio() ? rm->GetAudioCodec() : std::string();
            result["audio_sample_rate"] = static_cast<uint64_t>(rm->HasAudio() ? rm->GetAudioSampleRate() : 0);
            result["audio_channels"] = static_cast<uint64_t>(rm->HasAudio() ? rm->GetAudioChannels() : 0);
            result["audio_duration"] = rm->HasAudio() ? rm->GetAudioDuration() : 0.0;
            result["output"] = rm->GetOutputFilename();
            return result;
        });

        // Current recording state + live statistics (mirrors GET /video/record/status)
        lua.set_function("video_record_status", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }

            auto* context = emulator->GetContext();
            RecordingManager* rm = context ? context->pRecordingManager : nullptr;
            if (!rm) { result["error"] = "recording manager not available"; return result; }

            result["recording"] = rm->IsRecording();
            result["paused"] = rm->IsPaused();
            result["feature_enabled"] = rm->isFeatureEnabled();
            result["realtime_capable"] = rm->IsRealtimeCapable();
            if (!rm->GetLastRecordingError().empty())
                result["last_error"] = rm->GetLastRecordingError();

            const RecordingManager::RecordingStats stats = rm->GetStats();
            result["frames_recorded"] = static_cast<uint64_t>(stats.framesRecorded);
            result["recorded_duration"] = stats.recordedDuration;
            result["emulated_duration"] = stats.emulatedDuration;
            result["output_file_size"] = static_cast<uint64_t>(stats.outputFileSize);
            result["average_frame_time_ms"] = stats.averageFrameTime;
            result["recent_fps"] = stats.recentFps;
            result["audio_samples_recorded"] = static_cast<uint64_t>(stats.audioSamplesRecorded);
            result["video_codec"] = rm->GetVideoCodec();
            result["audio"] = rm->HasAudio();
            result["audio_codec"] = rm->HasAudio() ? rm->GetAudioCodec() : std::string();
            result["audio_sample_rate"] = static_cast<uint64_t>(rm->HasAudio() ? rm->GetAudioSampleRate() : 0);
            result["audio_channels"] = static_cast<uint64_t>(rm->HasAudio() ? rm->GetAudioChannels() : 0);
            result["audio_duration"] = rm->HasAudio() ? rm->GetAudioDuration() : 0.0;
            result["output"] = rm->GetOutputFilename();
            return result;
        });
#else
        lua.set_function("video_record", [this](const std::string& action, sol::optional<sol::table>) -> sol::table {
            (void)action;
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            result["error"] = "recording support is disabled in this build (ENABLE_RECORDING=OFF)";
            return result;
        });

        lua.set_function("video_record_status", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            result["error"] = "recording support is disabled in this build (ENABLE_RECORDING=OFF)";
            return result;
        });
#endif

        // Assemble Z80 source text; optionally write the bytes into RAM
        lua.set_function("assemble", [this](const std::string& code, sol::object addressValue,
                                            sol::optional<bool> writeOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }

            uint16_t address = 0;
            if (addressValue.is<std::string>())
            {
                try { address = static_cast<uint16_t>(std::stoul(addressValue.as<std::string>(), nullptr, 0)); }
                catch (...) { result["error"] = "invalid address"; return result; }
            }
            else if (addressValue.is<int>())
            {
                address = static_cast<uint16_t>(addressValue.as<int>() & 0xFFFF);
            }

            Z80TextAssembler assembler;
            AsmResult asmResult = assembler.Assemble(code, address);

            result["ok"] = asmResult.ok;
            if (!asmResult.ok)
            {
                result["error"] = asmResult.error.message;
                result["error_line"] = asmResult.error.line;
                return result;
            }

            if (writeOpt.value_or(false))
            {
                Memory* memory = emulator->GetMemory();
                if (memory)
                {
                    emulator->EditMemoryFromTool("Lua assemble write", [&] {
                        uint32_t addr = asmResult.startAddress;
                        for (uint8_t b : asmResult.bytes)
                            memory->ToolWriteToZ80Memory(static_cast<uint16_t>((addr++) & 0xFFFF), b);
                    });
                    result["written"] = true;
                }
            }

            result["address"] = asmResult.startAddress;
            result["end_address"] = asmResult.endAddress;

            sol::table bytes = lua_view.create_table();
            for (size_t i = 0; i < asmResult.bytes.size(); i++)
                bytes[i + 1] = asmResult.bytes[i];
            result["bytes"] = bytes;

            sol::table symbols = lua_view.create_table();
            for (const auto& sym : asmResult.symbols)
                symbols[sym.first] = sym.second;
            if (!asmResult.symbols.empty())
                result["symbols"] = symbols;
            return result;
        });

        // Resolve a label name to its address, or an address to label(s)
        lua.set_function("label_resolve", [this](sol::object queryValue) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* ctx = emulator->GetContext();
            LabelManager* labelMgr = ctx && ctx->pDebugManager ? ctx->pDebugManager->GetLabelManager() : nullptr;
            if (!labelMgr) { result["error"] = "label manager not available"; return result; }

            std::string query;
            if (queryValue.is<std::string>())
                query = queryValue.as<std::string>();
            else if (queryValue.is<int>())
                query = std::to_string(queryValue.as<int>());
            else
            {
                result["error"] = "query must be a label name or address";
                return result;
            }

            // Name direction
            auto label = labelMgr->GetLabelByName(query);
            if (label)
            {
                result["found"] = true;
                result["query"] = "name";
                result["address"] = label->address;
                result["name"] = label->name;
                if (!label->type.empty())
                    result["type"] = label->type;
                return result;
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
                result["found"] = false;
                result["query"] = "name";
                if (labelMgr->GetLabelCount() == 0)
                    result["hint"] = "no labels loaded — symbols_load first";
                return result;
            }

            result["query"] = "address";
            result["address"] = address;

            auto exact = labelMgr->GetLabelByZ80Address(address);
            result["found"] = exact != nullptr;
            if (exact)
                result["name"] = exact->name;

            auto atAddress = labelMgr->GetAllLabelsAtAddress(address);
            if (!atAddress.empty())
            {
                sol::table aliases = lua_view.create_table();
                size_t index = 0;
                for (const auto& l : atAddress)
                {
                    sol::table item = lua_view.create_table();
                    item["name"] = l->name;
                    item["address"] = l->address;
                    aliases[index + 1] = item;
                    index++;
                }
                result["aliases"] = aliases;
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
                result["nearest_below"] = bestBelow->name;
                result["nearest_below_address"] = bestBelow->address;
            }
            if (bestAbove)
            {
                result["nearest_above"] = bestAbove->name;
                result["nearest_above_address"] = bestAbove->address;
            }
            return result;
        });

        // sjasmplus .lst source-listing navigation
        lua.set_function("listing_load", [this](const std::string& path) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* ctx = emulator->GetContext();
            ListingParser* parser = ctx && ctx->pDebugManager ? ctx->pDebugManager->GetListingParser() : nullptr;
            if (!parser) { result["error"] = "listing parser not available"; return result; }

            result["ok"] = parser->LoadListing(path);
            if (result["ok"])
            {
                result["lines"] = static_cast<uint64_t>(parser->GetLineCount());
                result["code_lines"] = static_cast<uint64_t>(parser->GetCodeLineCount());
                result["total_bytes"] = static_cast<uint64_t>(parser->GetTotalBytes());
                result["min_address"] = parser->GetMinAddress();
                result["max_address"] = parser->GetMaxAddress();
            }
            return result;
        });

        lua.set_function("listing_source_at", [this](sol::optional<unsigned> addressOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* ctx = emulator->GetContext();
            ListingParser* parser = ctx && ctx->pDebugManager ? ctx->pDebugManager->GetListingParser() : nullptr;
            if (!parser) { result["error"] = "listing parser not available"; return result; }
            if (!parser->IsLoaded()) { result["error"] = "no listing loaded"; return result; }

            Z80State* z80 = emulator->GetZ80State();
            if (!z80) { result["error"] = "Z80 state not available"; return result; }

            const uint16_t address = addressOpt.has_value() ? static_cast<uint16_t>(addressOpt.value()) : z80->pc;
            const ListingLine* line = parser->FindLineByAddress(address);
            result["found"] = line != nullptr;
            if (line)
            {
                result["line"] = line->lineNumber;
                result["source"] = line->source;
                result["has_code"] = line->hasCode;
                if (line->hasCode)
                {
                    result["address"] = line->addressStart;
                    result["address_end"] = line->addressEnd;
                }
            }
            return result;
        });

        lua.set_function("listing_step_line", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) { result["error"] = "debug manager not available"; return result; }
            ListingParser* parser = ctx->pDebugManager->GetListingParser();
            if (!parser) { result["error"] = "listing parser not available"; return result; }
            if (!parser->IsLoaded()) { result["error"] = "no listing loaded"; return result; }

            Z80State* z80 = emulator->GetZ80State();
            if (!z80) { result["error"] = "Z80 state not available"; return result; }

            const ListingLine* startLine = parser->FindLineByAddress(z80->pc);
            const int startLineNumber = startLine ? startLine->lineNumber : -1;

            const unsigned maxTStates = ctx->config.frame * 100;
            emulator->RunUntilCondition(
                [parser, startLineNumber](const Z80State& state) {
                    const ListingLine* line = parser->FindLineByAddress(state.pc);
                    return line != nullptr && line->lineNumber != startLineNumber;
                },
                maxTStates);

            z80 = emulator->GetZ80State();
            const ListingLine* endLine = z80 ? parser->FindLineByAddress(z80->pc) : nullptr;
            result["line_changed"] = endLine != nullptr && endLine->lineNumber != startLineNumber;
            if (z80)
                result["pc"] = z80->pc;
            if (endLine)
            {
                result["line"] = endLine->lineNumber;
                result["source"] = endLine->source;
            }
            return result;
        });

        lua.set_function("listing_run_to_line", [this](int lineNumber) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) { result["error"] = "debug manager not available"; return result; }
            ListingParser* parser = ctx->pDebugManager->GetListingParser();
            if (!parser) { result["error"] = "listing parser not available"; return result; }
            if (!parser->IsLoaded()) { result["error"] = "no listing loaded"; return result; }

            Z80State* z80 = emulator->GetZ80State();
            if (!z80) { result["error"] = "Z80 state not available"; return result; }

            const ListingLine* target = parser->FindNextCodeLine(lineNumber);
            if (!target)
            {
                result["error"] = "no code line at or after line " + std::to_string(lineNumber);
                return result;
            }

            const uint16_t targetAddress = target->addressStart;
            const bool alreadyAt = z80->pc == targetAddress;
            if (!alreadyAt)
            {
                const unsigned maxTStates = ctx->config.frame * 500;
                emulator->RunUntilCondition(
                    [targetAddress](const Z80State& state) { return state.pc == targetAddress; }, maxTStates);
            }

            z80 = emulator->GetZ80State();
            const bool reached = z80 && z80->pc == targetAddress;
            result["reached"] = reached;
            result["already_at"] = alreadyAt;
            if (z80)
                result["pc"] = z80->pc;
            result["line"] = target->lineNumber;
            result["source"] = target->source;
            return result;
        });

        // Port trace (PDR) bindings — runtime feature "porttrace"
        LuaPortTrace::registerBindings(lua, [this]() -> Emulator* { return effectiveEmulator(); });

        // TS-Conf VDAC2 card (FT812): bus capture
        LuaVdac2::registerBindings(lua, [this]() -> Emulator* { return effectiveEmulator(); });
    }

    void setEmulator(Emulator* emulator) { _emulator = emulator; }
    void setLuaState(sol::state* lua) { _lua = lua; }

    void unregisterType(sol::state& lua)
    {
        // No specific cleanup needed
    }
    /// endregion </Lua SOL lifecycle>
};