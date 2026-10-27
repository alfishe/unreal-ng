#pragma once

#include "common/base64.h"
#include "emulator/memory/devicememory.h"
#include "emulator/execution/companionregistry.h"
#include "emulator/execution/runpacer.h"
#include "emulator/execution/scriptsession.h"
#include "emulator/sound/audiomixer.h"
#include "emulator/video/framebufferexport.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/ports/models/profiboard.h"
#include "emulator/video/screenshotter.h"
#include "emulator/video/next/nextlayerimage.h"
#include "emulator/ports/models/sprinter/sprinterbios.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/zxpoly/zxpolygroup.h"
#include "debugger/memory/memoryread.h"
#include "debugger/media/sectorwrite.h"
#include "debugger/pchistory/pchistory.h"
#include "debugger/ports/portwrite.h"
#include "debugger/snapshot/debugsnapshot.h"
#include "debugger/search/memorysearch.h"
#include <algorithm>
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
#include <emulator/media/recentfiles.h>
#include <emulator/capture/recordingcontrol.h>
#include <cmath>
#include <filesystem>
#include <map>
#include <system_error>
#include <emulator/slots/slotcontrol.h>
#include <emulator/io/controllers/controllercontrol.h>
#include <emulator/actions/actionregistry.h>
#include <emulator/actions/contextstate.h>
#include <emulator/actions/surfacesapi.h>
#include <emulator/companions/streamdeckapi.h>
#include <emulator/monitoring/monitoringapi.h>
#include <emulator/execution/automationgate.h>
#include <emulator/sound/midi/midicontrol.h>
#ifdef ENABLE_CONTROLLERS
#include "hostinputcontrol.h"
#endif
#include <emulator/io/fdc/diskimage.h>
#include <emulator/io/tape/tape.h>
#include <tapeaudio/tapeaudioimporter.h>
#include <tapeaudio/tapeaudiorenderer.h>
#include <emulator/cpu/z80.h>
#include <emulator/video/screen.h>
#include <emulator/sound/soundcharactersettings.h>
#include <emulator/sound/chips/neogs/neogsmedia.h>
#include <emulator/memory/atm/evoflashrequest.h>
#include <emulator/sound/soundmanager.h>
#include <emulator/sound/chips/soundchip_ay8910.h>
#include <emulator/sound/chips/gs/soundchip_gs.h>
#include "../../../automation.h"
#include "../../../temporalstatus.h"
#include "emulator/video/viewportcontrol.h"
#include "emulator/video/windowrecording.h"
#include "../../../windowdisplay.h"
#include "../../../operationsstatus.h"
#include <debugger/ttd/ttdcontrol.h>
#include <debugger/debugmanager.h>
#include <debugger/keyboard/debugkeyboardmanager.h>
#include <debugger/mouse/debugmousemanager.h>
#include <debugger/joystick/debugjoystickmanager.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/breakpoints/breakpointselection.h>
#include "debugger/tracecontrol/tracecontrol.h"
#include "debugger/analyzers/analysis/analysiscontrol.h"
#include <debugger/disassembler/z80disasm.h>
#include <debugger/labels/labelmanager.h>
#include "debugger/labels/symbolcontrol.h"
#include "debugger/asm/asmcontrol.h"
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
#include <debugger/analyzers/coverage/coverageexport.h>
#include <debugger/analyzers/osaware/osaware_event.h>
#include <debugger/assembler/z80textassembler.h>
#include <debugger/listing/listingparser.h>
#include <emulator/platform.h>
#include <emulator/ports/portdecoder.h>
#include <emulator/config.h>
#include <emulator/io/rtc/rtcaccess.h>
#include <emulator/io/sprinter/isa/isaaccess.h>
#include <emulator/io/network/traffic/trafficaccess.h>
#include <emulator/io/network/vnet/ethernetaccess.h>
#include <emulator/state/devicestate.h>
#include <common/stringhelper.h>
#include <debugger/ports/nextregwrite.h>
#include <emulator/io/z80n/nextregjournal.h>
#include <emulator/io/z80n/nextdivmmcjournal.h>
#include <emulator/io/z80n/nextintjournal.h>
#include <emulator/io/z80n/nextsdjournal.h>
#include <emulator/bus/bustrace.h>
#include <emulator/io/z80n/nextreportquery.h>
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

#ifdef ENABLE_CONTROLLERS
/// A host controllers reply (HostReply::ToValue) -> Lua table, as StateNodeToLua
inline sol::object JsonValueToLua(sol::this_state s, const controllers::JsonValue& value)
{
    sol::state_view lua(s);
    if (value.IsBool())
        return sol::make_object(lua, value.AsBool());
    if (value.IsNumber())
        return sol::make_object(lua, value.AsNumber());
    if (value.IsString())
        return sol::make_object(lua, value.AsString());
    if (value.IsObject())
    {
        sol::table t = lua.create_table();
        for (const auto& [key, member] : value.GetMembers())
            t[key] = JsonValueToLua(s, member);
        return t;
    }
    if (value.IsArray())
    {
        sol::table t = lua.create_table();
        for (size_t i = 0; i < value.Items().size(); i++)
            t[i + 1] = JsonValueToLua(s, value.Items()[i]);
        return t;
    }
    return sol::make_object(lua, sol::lua_nil);
}
#endif


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

    /// One time-travel verb through TTDControl, the layer every surface shares
    /// (Phase 5, Step 1). Without an emulator: NotAvailable "no emulator"
    ttd::TTDReply TtdRun(const std::string& verb, std::map<std::string, std::string> options = {}) const
    {
        Emulator* emulator = effectiveEmulator();
        if (!emulator)
        {
            ttd::TTDReply reply;
            reply.error = ttd::TTDControlError::NotAvailable;
            reply.message = "no emulator";
            return reply;
        }
        return ttd::TTDControl(emulator->GetContext()).Execute({verb, std::move(options)});
    }

    /// A reply as a script sees it: the body, and on failure ok = false and
    /// error = the message (a body that names its own error keeps it)
    static StateNode TtdScriptValue(const ttd::TTDReply& reply)
    {
        StateNode value = reply.body;
        if (!reply.Ok())
        {
            value["ok"] = false;
            if (!value.find("error"))
                value["error"] = reply.message;
        }
        return value;
    }

    /// A Lua value as option text: true / false, an integer, a string; nothing else
    static std::string TtdOptionText(const sol::object& o)
    {
        if (o.is<bool>())
            return o.as<bool>() ? "true" : "false";
        if (o.get_type() == sol::type::number)
            return std::to_string(static_cast<long long>(o.as<double>()));
        return o.is<std::string>() ? o.as<std::string>() : std::string();
    }

    /// A Lua argument as SymbolControl options: a string is `firstKey`, a table gives each key (a list of strings is a
    /// comma list, as sets = {"a", "b"})
    static std::map<std::string, std::string> SymbolOptions(const sol::object& arg, const std::string& firstKey)
    {
        std::map<std::string, std::string> options;
        if (arg.is<std::string>() && !firstKey.empty())
            options[firstKey] = arg.as<std::string>();
        else if (arg.get_type() == sol::type::table)
            for (const auto& [key, value] : arg.as<sol::table>())
            {
                if (!key.is<std::string>())
                    continue;
                if (value.get_type() == sol::type::table)
                {
                    std::string list;
                    for (const auto& item : value.as<sol::table>())
                        if (item.second.is<std::string>())
                            list += (list.empty() ? "" : ",") + item.second.as<std::string>();
                    options[key.as<std::string>()] = list;
                }
                else
                    options[key.as<std::string>()] = TtdOptionText(value);
            }
        return options;
    }

    sol::object symbolsRun(sol::this_state ts, const std::string& verb, std::map<std::string, std::string> options) const
    {
        Emulator* emulator = effectiveEmulator();
        SymbolReply reply;
        if (!emulator)
        {
            reply.error = SymbolControlError::NotAvailable;
            reply.message = "no emulator";
        }
        else
            reply = SymbolControl(emulator->GetContext()).Execute({verb, std::move(options)});
        StateNode value = reply.body;
        if (!reply.Ok())
        {
            value["ok"] = false;
            value["error"] = reply.message;
        }
        return StateNodeToLua(ts, value);
    }

    sol::object asmRun(sol::this_state ts, const std::string& verb, std::map<std::string, std::string> options) const
    {
        Emulator* emulator = effectiveEmulator();
        const AsmReply reply = AsmControl(emulator ? emulator->GetContext() : nullptr).Execute({verb, std::move(options)});
        StateNode value = reply.body;
        if (!reply.Ok())
        {
            value["ok"] = false;
            value["error"] = reply.message;
        }
        return StateNodeToLua(ts, value);
    }

    sol::object symbolsCall(sol::this_state ts, const std::string& verb, const sol::object& arg, const std::string& firstKey) const
    {
        return symbolsRun(ts, verb, SymbolOptions(arg, firstKey));
    }

    /// A seek's answer as a table: the reply's fields; a refused seek has reached = false
    sol::object TtdSeekValue(sol::this_state ts, const ttd::TTDReply& reply) const
    {
        StateNode value = TtdScriptValue(reply);
        if (!value.find("reached"))
            value["reached"] = false;
        return StateNodeToLua(ts, value);
    }

    /// One slots verb (SlotControl) for the effective emulator. `opts`: the flags (replace, dry_run, media, adapter),
    /// options = "..." or {name = value}, and - when `cardOptions` - any other key as a card option. A restart of the
    /// instance this interpreter is bound to rebinds it to the new machine
    SlotControlReply slotsCall(SlotControlRequest request, const sol::optional<sol::table>& opts, bool cardOptions)
    {
        auto text = [](const sol::object& value) -> std::string {
            if (value.is<bool>())
                return value.as<bool>() ? "true" : "false";
            if (value.get_type() == sol::type::number)
            {
                const double number = value.as<double>();
                return number == static_cast<double>(static_cast<long long>(number))
                           ? std::to_string(static_cast<long long>(number))
                           : std::to_string(number);
            }
            if (value.get_type() == sol::type::table)
            {
                std::string list;
                for (const auto& item : value.as<sol::table>())
                {
                    if (item.second.is<std::string>())
                        list += (list.empty() ? "" : ",") + item.second.as<std::string>();
                }
                return list.empty() ? "none" : list;
            }
            return value.is<std::string>() ? value.as<std::string>() : std::string();
        };
        auto addOption = [&request](const std::string& name, const std::string& value) {
            request.options += (request.options.empty() ? "" : " ") + name + "=" + value;
        };
        if (opts)
        {
            for (const auto& [keyObject, value] : *opts)
            {
                if (!keyObject.is<std::string>())
                    continue;
                const std::string key = keyObject.as<std::string>();
                if (key == "replace" || key == "replaceIfIncompatible" || key == "replace_if_incompatible")
                    request.replaceIfIncompatible = value.is<bool>() ? value.as<bool>() : text(value) == "true";
                else if (key == "dry_run" || key == "dryRun")
                    request.dryRun = value.is<bool>() ? value.as<bool>() : text(value) == "true";
                else if (key == "media" || key == "mediaDisposition")
                    request.media = text(value);
                else if (key == "adapter")
                    request.adapter = text(value);
                else if (key == "options" && value.is<std::string>())
                    request.options += (request.options.empty() ? "" : " ") + value.as<std::string>();
                else if (key == "options" && value.get_type() == sol::type::table)
                {
                    for (const auto& [name, v] : value.as<sol::table>())
                    {
                        if (name.is<std::string>())
                            addOption(name.as<std::string>(), text(v));
                    }
                }
                else if (cardOptions)
                    addOption(key, text(value));
            }
        }
        Emulator* emulator = effectiveEmulator();
        if (!emulator && request.verb != "matrix")
        {
            SlotControlReply none;
            none.status = "no-machine";
            none.httpStatus = 404;
            none.message = "no emulator selected";
            return none;
        }
        const bool bound = _emulator != nullptr && _emulator == emulator;
        request.emulatorId = emulator ? emulator->GetId() : std::string();
        emulator = nullptr;
        SlotControlReply reply = SlotControl::Execute(request);
        if (bound && reply.emulator)
            _emulator = reply.emulator.get();   // the restarted machine; the manager owns it
        return reply;
    }

    /// The CPU held around every mutating tape / media operation of this surface (Emulator::CpuHold). RAII, so
    /// an early return cannot leave it held.
    class EmulatorPauseBracket
    {
    public:
        // The CPU held for the operation (Emulator::CpuHold, execution arbiter): nothing executes meanwhile, the
        // pause is left as it was, nothing is broadcast. Before: Pause() + a 10 ms sleep + Resume(), which lifted a
        // pause somebody else asked for meanwhile
        explicit EmulatorPauseBracket(Emulator* emulator)
        {
            if (emulator)
                _hold.emplace(*emulator, "lua");
        }

    private:
        std::optional<Emulator::CpuHold> _hold;
    };
    /// endregion </Fields>

    /// bp / bp_read / bp_write / bp_port_in / bp_port_out: `options` is nothing, a page string ("ram32"), or
    /// a table {page=, to=, slot_only=, mask=, hits=}. The breakpoint id, or -1 when refused
    int AddScriptBreakpoint(BreakpointTypeEnum type, uint8_t access, uint16_t address, const sol::optional<sol::object>& options)
    {
        Emulator* emulator = effectiveEmulator();
        BreakpointManager* bpm = emulator ? emulator->GetBreakpointManager() : nullptr;
        if (!bpm)
            return -1;
        BreakpointSpec spec;
        spec.type = type;
        spec.access = access;
        spec.address = address;
        std::string page, hits;
        int32_t to = -1, mask = -1;
        bool slotOnly = false;
        if (options && options->is<std::string>())
            page = options->as<std::string>();
        else if (options && options->is<sol::table>())
        {
            sol::table t = options->as<sol::table>();
            page = t.get<sol::optional<std::string>>("page").value_or("");
            to = t.get<sol::optional<int32_t>>("to").value_or(-1);
            slotOnly = t.get<sol::optional<bool>>("slot_only").value_or(false);
            mask = t.get<sol::optional<int32_t>>("mask").value_or(-1);
            hits = t.get<sol::optional<std::string>>("hits").value_or("");
            if (hits.empty())
                if (auto n = t.get<sol::optional<int64_t>>("hits"))
                    hits = std::to_string(*n);  // hits=5: the 5th hit
        }
        std::string error;
        if (!BreakpointManager::ApplyScriptOptions(spec, page, to, slotOnly, mask, hits, error))
            return -1;
        const uint16_t id = bpm->AddBreakpoint(spec, error);
        return id == BRK_INVALID ? -1 : static_cast<int>(id);
    }

    /// bp_nextreg_write / bp_nextreg_read / bp_nextreg: `options` is nothing or a table {to=, doors=, value=, vmask=, hits=}
    /// (to = register range end; doors "nextreg,port,copper"; value = stop only on it, vmask = the compared bits). The id, or -1
    int AddScriptNextRegBreakpoint(uint8_t access, uint16_t reg, const sol::optional<sol::table>& options)
    {
        Emulator* emulator = effectiveEmulator();
        BreakpointManager* bpm = emulator ? emulator->GetBreakpointManager() : nullptr;
        if (!bpm)
            return -1;
        BreakpointSpec spec;
        spec.type = BRK_NEXTREG;
        spec.access = access;
        spec.address = reg;
        std::string doors, hits;
        int32_t to = -1, value = -1, vmask = -1;
        if (options)
        {
            const sol::table& t = *options;
            to = t.get<sol::optional<int32_t>>("to").value_or(-1);
            doors = t.get<sol::optional<std::string>>("doors").value_or("");
            value = t.get<sol::optional<int32_t>>("value").value_or(-1);
            vmask = t.get<sol::optional<int32_t>>("vmask").value_or(-1);
            hits = t.get<sol::optional<std::string>>("hits").value_or("");
            if (hits.empty())
                if (auto n = t.get<sol::optional<int64_t>>("hits"))
                    hits = std::to_string(*n);
        }
        std::string error;
        if (!BreakpointManager::ApplyNextRegOptions(spec, to, doors, value, vmask, hits, error))
            return -1;
        const uint16_t id = bpm->AddBreakpoint(spec, error);
        return id == BRK_INVALID ? -1 : static_cast<int>(id);
    }

    /// bp_beam / bp_copper (ZX Next): `options` is nothing or a table {hc=, to=, hits=} (hc: the pixel clock of a beam place; to: the
    /// range end of copper list indexes). The id, or -1
    int AddScriptPositionBreakpoint(BreakpointTypeEnum type, uint16_t position, const sol::optional<sol::table>& options)
    {
        Emulator* emulator = effectiveEmulator();
        BreakpointManager* bpm = emulator ? emulator->GetBreakpointManager() : nullptr;
        if (!bpm)
            return -1;
        BreakpointSpec spec;
        spec.type = type;
        spec.access = BRK_IO_OUT;
        spec.address = position;
        std::string hits;
        if (options)
        {
            const sol::table& t = *options;
            spec.beamHc = static_cast<uint16_t>(t.get<sol::optional<int32_t>>("hc").value_or(0));
            const int32_t to = t.get<sol::optional<int32_t>>("to").value_or(-1);
            if (to >= 0)
            {
                spec.hasEnd = true;
                spec.addressEnd = static_cast<uint16_t>(to);
            }
            hits = t.get<sol::optional<std::string>>("hits").value_or("");
            if (hits.empty())
                if (auto n = t.get<sol::optional<int64_t>>("hits"))
                    hits = std::to_string(*n);
        }
        std::string error;
        if (!BreakpointManager::ParseHitSpec(hits, spec.hitMode, spec.hitTarget, error))
            return -1;
        const uint16_t id = bpm->AddBreakpoint(spec, error);
        return id == BRK_INVALID ? -1 : static_cast<int>(id);
    }

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

    /// One mouse device of the machine: same keys as the WebAPI `device` object
    static sol::table mouseDeviceTable(sol::this_state s, const MouseDeviceStatus& device)
    {
        sol::state_view lua(s);
        auto hexRow = [&lua](const uint8_t* bytes, size_t count) {
            sol::table row = lua.create_table();
            for (size_t i = 0; i < count; i++)
            {
                char text[4];
                std::snprintf(text, sizeof(text), "%02X", bytes[i]);
                row[i + 1] = std::string(text);
            }
            return row;
        };
        sol::table t = lua.create_table();
        t["id"] = device.id;
        t["name"] = device.name;
        t["kind"] = MouseDeviceStatus::KindName(device.kind);
        t["fitted"] = device.fitted;
        t["in_use"] = device.inUse;
        t["wheel"] = device.wheel;
        t["buttons"] = static_cast<int>(device.buttons);
        t["x"] = static_cast<int>(device.x);
        t["y"] = static_cast<int>(device.y);
        t["button_mask"] = static_cast<int>(device.buttonMask);
        if (device.hasPorts)
        {
            sol::table ports = lua.create_table();
            ports["FADF"] = static_cast<int>(device.portButtons);
            ports["FBDF"] = static_cast<int>(device.portX);
            ports["FFDF"] = static_cast<int>(device.portY);
            t["ports"] = ports;
        }
        if (device.hasSerial)
        {
            const MouseDeviceStatus::Serial& serial = device.serial;
            sol::table line = lua.create_table();
            line["baud"] = serial.baud;
            line["receiver_baud"] = serial.receiverBaud;
            line["receiver_in_tune"] = serial.receiverInTune;
            line["receiver_enabled"] = serial.receiverEnabled;
            line["packet_in_flight"] = serial.packetInFlight;
            line["packet"] = hexRow(serial.packet, 3);
            line["packet_bytes_sent"] = static_cast<int>(serial.packetBytesSent);
            sol::table pending = lua.create_table();
            pending["dx"] = serial.pendingDx;
            pending["dy"] = serial.pendingDy;
            line["pending"] = pending;
            line["packets_sent"] = static_cast<double>(serial.packetsSent);
            line["bytes_received"] = static_cast<double>(serial.bytesReceived);
            line["framing_errors"] = static_cast<double>(serial.framingErrors);
            line["receiver_fifo"] = hexRow(serial.fifo, serial.fifoCount);
            line["receiver_overrun"] = serial.overrun;
            t["serial"] = line;
        }
        if (device.hasPs2)
        {
            sol::table ps2 = lua.create_table();
            ps2["connected"] = device.ps2.connected;
            ps2["resolution"] = static_cast<int>(device.ps2.resolution);
            ps2["counts_per_mm"] = 1 << device.ps2.resolution;
            t["ps2"] = ps2;
        }
        return t;
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
        // The machine's mouse (design 2026-10-03); `device` is absent (nil) when none is fitted
        t["mouse_fitted"] = state.mouseFitted;
        if (state.device)
            t["device"] = mouseDeviceTable(s, *state.device);
        sol::table devices = lua.create_table();
        for (size_t i = 0; i < state.devices.size(); i++)
            devices[i + 1] = mouseDeviceTable(s, state.devices[i]);
        t["devices"] = devices;
        sol::table queue = lua.create_table();
        queue["ops"] = static_cast<int>(state.queuedOps);
        sol::table remaining = lua.create_table();
        remaining["dx"] = state.glideRemainingDx;
        remaining["dy"] = state.glideRemainingDy;
        queue["glide_remaining"] = remaining;
        t["queue"] = queue;
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
        t["fit"] = state.fit;
        t["state"] = static_cast<int>(state.state);
        t["port_value"] = static_cast<int>(state.portValue);
        t["buttons"] = buttons;
        t["pressed"] = pressed;
        t["button_names"] = names;
        t["keys"] = state.keys;
        if (state.hasPads)
        {
            sol::table extras = lua.create_table();
            for (size_t i = 0; i < state.leftExtraButtons.size(); i++)
                extras[i + 1] = state.leftExtraButtons[i];
            t["left_extras"] = extras;
            sol::table rightPad = lua.create_table();
            rightPad["state"] = static_cast<int>(state.rightPad);
            sol::table rightButtons = lua.create_table();
            for (size_t i = 0; i < state.rightButtons.size(); i++)
                rightButtons[i + 1] = state.rightButtons[i];
            rightPad["buttons"] = rightButtons;
            t["right_pad"] = rightPad;
        }
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
            "pause", [](Emulator& self) -> std::string {
                execution::script::Check(self, execution::Origin::Lua, false);   // a pause opens no session
                return execution::script::PauseStatus(
                    self.Pause(execution::script::RequesterOf(self, execution::Origin::Lua), "lua"));
            },
            "resume", [](Emulator& self) {
                execution::script::Check(self, execution::Origin::Lua);
                self.Resume(execution::script::RequesterOf(self, execution::Origin::Lua));
            },
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
                    case StateRetired: return "retired";
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
            // A tool edit (TTD records it); refused in the recorded past: a script error
            std::string refused;
            const bool set = emulator->SetRegisterFromTool("lua register", name, value, &refused);
            if (!refused.empty())
                throw std::runtime_error(refused);
            return set;
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
            effectiveEmulator()->EditMemoryFromToolOrThrow("Lua memory write", [&] { mem->ToolWriteToZ80Memory(addr, value); });
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
            effectiveEmulator()->EditMemoryFromToolOrThrow("Lua memory write", [&] {
                mem->ToolWriteToZ80Memory(addr, value & 0xFF);
                mem->ToolWriteToZ80Memory(static_cast<uint16_t>(addr + 1), (value >> 8) & 0xFF);
            });
        });

        // debug_snapshot{disasm = 21, stack = 8, memory = {"cpu:0x8000:256", "ram5:0:6912"}} - one coherent debugger
        // snapshot (core DebugSnapshot, the WebAPI GET /debug/snapshot): memory windows carry their bytes as Lua strings
        // (field bytes); nil, error when refused
        // Long-poll (debugger additions tdd §6): blocks until the snapshot's seq moves past `since` (default: the
        // current one) or timeout_ms (default 10000, at most 60000) -> {seq, changed, state, pause} | nil, error
        lua.set_function("debug_wait", [this](sol::this_state s, sol::optional<double> since, sol::optional<uint32_t> timeoutMs)
                                           -> std::tuple<sol::object, sol::object> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return {sol::make_object(s, sol::lua_nil), sol::make_object(s, std::string("no emulator"))};
            const uint64_t from = since ? static_cast<uint64_t>(*since) : emulator->DebugSeq();
            const uint32_t timeout = std::min<uint32_t>(timeoutMs.value_or(10000), 60000);
            return {StateNodeToLua(s, DebugSnapshot::Wait(emulator, from, timeout)), sol::make_object(s, sol::lua_nil)};
        });

        // PC history (debugger additions tdd §7): pc_history([depth]) -> {armed, started_now, total, capacity, entries
        // [{address, kind, page}]} | nil, error (the first call starts recording); pc_history_arm(on) -> true | nil, error
        lua.set_function("pc_history", [this](sol::this_state s, sol::optional<unsigned> depth) -> std::tuple<sol::object, sol::object> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return {sol::make_object(s, sol::lua_nil), sol::make_object(s, std::string("no emulator"))};
            const PcHistory::Result result = PcHistory::Report(emulator, depth.value_or(32));
            if (!result.error.empty())
                return {sol::make_object(s, sol::lua_nil), sol::make_object(s, result.error)};
            return {StateNodeToLua(s, result.report), sol::make_object(s, sol::lua_nil)};
        });
        lua.set_function("pc_history_arm", [this](sol::this_state s, bool on) -> std::tuple<sol::object, sol::object> {
            Emulator* emulator = effectiveEmulator();
            const std::string error = emulator ? PcHistory::SetArmed(emulator, on) : std::string("no emulator");
            if (!error.empty())
                return {sol::make_object(s, sol::lua_nil), sol::make_object(s, error)};
            return {sol::make_object(s, true), sol::make_object(s, sol::lua_nil)};
        });

        lua.set_function("debug_snapshot", [this](sol::this_state s, sol::optional<sol::table> opts) -> std::tuple<sol::object, sol::object> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return {sol::make_object(s, sol::lua_nil), sol::make_object(s, std::string("no emulator"))};
            DebugSnapshot::Options options;
            options.rawBytes = true;
            if (opts)
            {
                options.disasm = std::min<unsigned>(opts->get_or("disasm", 0u), 100u);
                options.stack = opts->get_or("stack", 8u);
                options.pchist = opts->get_or("pchist", 0u);
                if (sol::optional<sol::table> windows = opts->get<sol::optional<sol::table>>("memory"))
                    for (auto& pair : *windows)
                        if (pair.second.is<std::string>())
                            options.memory.push_back(pair.second.as<std::string>());
            }
            const DebugSnapshot::Result result = DebugSnapshot::Build(emulator, options);
            if (!result.error.empty())
                return {sol::make_object(s, sol::lua_nil), sol::make_object(s, result.error)};
            return {StateNodeToLua(s, result.snapshot), sol::make_object(s, sol::lua_nil)};
        });

        // mem_read_bytes(addr, len [, space]) -> the bytes as a Lua string (MemoryRead): the CPU view (wraps at #FFFF,
        // up to 65536), a page "ram5" / "rom2" / "cache0" or "ram" (every RAM page back to back); nil, error on refusal
        lua.set_function("mem_read_bytes", [this](sol::this_state s, uint32_t addr, uint32_t len,
                                                  sol::optional<std::string> space) -> std::tuple<sol::object, sol::object> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return {sol::make_object(s, sol::lua_nil), sol::make_object(s, std::string("no emulator"))};
            const MemoryRead::Result read = MemoryRead::Bytes(emulator->GetContext(), space.value_or(""), addr, len);
            if (!read.error.empty())
                return {sol::make_object(s, sol::lua_nil), sol::make_object(s, read.error)};
            return {sol::make_object(s, std::string(read.bytes.begin(), read.bytes.end())), sol::make_object(s, sol::lua_nil)};
        });

        // A debugger's port write through the decoder (PortWrite): port_out(0x13AF, 0x20) -> true | nil, error
        lua.set_function("port_out", [this](sol::this_state s, int64_t port, int64_t value) -> std::tuple<sol::object, sol::object> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return {sol::make_object(s, sol::lua_nil), sol::make_object(s, std::string("no emulator"))};
            uint16_t p = 0;
            uint8_t v = 0;
            std::string error;
            if (port < 0 || value < 0 || !PortWrite::Parse(std::to_string(port), std::to_string(value), p, v, error))
                return {sol::make_object(s, sol::lua_nil),
                        sol::make_object(s, error.empty() ? std::string("port and value must not be negative") : error)};
            const PortWrite::Result result = PortWrite::Write(emulator, p, v, "lua");
            if (!result.ok)
                return {sol::make_object(s, sol::lua_nil), sol::make_object(s, result.error)};
            return {sol::make_object(s, true), sol::make_object(s, sol::lua_nil)};
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
            effectiveEmulator()->EditMemoryFromToolOrThrow("Lua memory write", [&] {
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
            effectiveEmulator()->EditMemoryFromToolOrThrow("Lua page write", [&] {
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
            effectiveEmulator()->EditMemoryFromToolOrThrow("Lua page write", [&] {
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
        // screenshot([{area = "full"|"screen", format = "png"|"gif", source = "presented"|"live"|"window", path = "file"}]): a
        // screenshot (core Screenshotter): the whole frame (default) or the working picture, PNG (default) or GIF,
        // of the presented frame (default) or the live one as drawn now (a paused machine adds frame.partial and
        // frame.beam = {line, tstate}), or the picture as the unreal-qt window draws it ("window": GUI only, area full;
        // adds window = {viewport, picture, device_pixel_ratio, backend}).
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
                    return mouseError(s, "Unknown source '" + source + "': use presented, live or window");
                if (!format.empty() && !Screenshotter::ParseFormat(format, options.format))
                    return mouseError(s, "Unknown format '" + format + "': use png or gif");
                options.saveTo = opts->get_or<std::string>("path", "");
            }

            const ScreenshotResult shot = Screenshotter::TakeFrom(*context->pScreen, options, emulator->IsEmulationParked());
            if (!shot.ok) return mouseError(s, shot.errorMessage);

            sol::state_view view(s);
            auto rect = [&view](const PictureRect& r) {
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
            if (shot.fromWindow)
            {
                sol::table window = view.create_table();
                window["viewport"] = rect(shot.window.viewport);
                window["picture"] = rect(shot.window.picture);
                window["device_pixel_ratio"] = shot.window.devicePixelRatio;
                window["backend"] = shot.window.backend;
                t["window"] = window;
            }
            if (!shot.savedFile.empty())
                t["file"] = shot.savedFile;
            else
                t["data"] = std::string(shot.bytes.begin(), shot.bytes.end());
            sol::variadic_results out;
            out.push_back(t);
            return out;
        });
        // capture_screen_layer(layer [, {format = "png"|"rgba", alpha = true|false, path = "file"}]): ONE LAYER of the last completed frame of a ZX
        // Spectrum Next (design-layer-capture.md): "ula" (with the border and LoRes), "layer2", "tilemap", "sprites" - alpha 0 where the layer
        // does not paint - or "ids" (every pixel coloured by the layer that won it). 640 x 512, the geometry of screenshot(). Returns
        // {layer, format, alpha, width, height, size, frame = {sequence, frame_number}, data = the PNG (or the raw RGBA bytes) as a string,
        // or `file`, legend = {{id, name, colour}...} for ids}; nil, error when the machine is not a Next, the capture is off
        // (next_layer_capture_control{enabled = true}) or no frame was completed yet
        lua.set_function("capture_screen_layer", [this](sol::this_state s, const std::string& layer, sol::optional<sol::table> opts) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            std::string format, alpha, path, size;
            if (opts)
            {
                format = opts->get_or<std::string>("format", "");
                size = opts->get_or<std::string>("size", "");
                path = opts->get_or<std::string>("path", "");
                sol::object a = (*opts)["alpha"];
                if (a.get_type() == sol::type::boolean)
                    alpha = a.as<bool>() ? "true" : "false";
            }
            const NextLayer::Result shot = NextLayer::Request(emulator->GetContext(), layer, format, alpha, path, size);
            if (shot.status != NextLayer::Status::Ok) return mouseError(s, shot.message);
            sol::state_view view(s);
            sol::table t = view.create_table();
            t["layer"] = NextLayer::KindName(shot.kind);
            t["format"] = shot.rgba ? "rgba" : "png";
            t["alpha"] = shot.alpha;
            t["size_name"] = NextLayer::ScaleName(shot.scale);
            t["width"] = shot.width;
            t["height"] = shot.height;
            t["size"] = shot.size;
            sol::table frame = view.create_table();
            frame["sequence"] = shot.sequence;
            frame["frame_number"] = shot.frameCounter;
            t["frame"] = frame;
            if (shot.kind == NextLayer::Kind::Ids)
            {
                sol::table legend = view.create_table();
                for (unsigned id = 0; id < NextLayer::kIdCount; id++)
                {
                    sol::table entry = view.create_table();
                    const uint32_t c = NextLayer::IdColour(id);
                    char colour[8];
                    std::snprintf(colour, sizeof colour, "#%02X%02X%02X", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
                    entry["id"] = id;
                    entry["name"] = NextLayer::IdName(id);
                    entry["colour"] = std::string(colour);
                    legend[id + 1] = entry;
                }
                t["legend"] = legend;
            }
            if (!shot.savedFile.empty())
                t["file"] = shot.savedFile;
            else
                t["data"] = std::string(shot.bytes.begin(), shot.bytes.end());
            sol::variadic_results out;
            out.push_back(t);
            return out;
        });
        // next_layer_capture_control([{enabled = true|false}]): switch the per-layer record of the Next screen (on by default; takes effect at
        // the next frame start); returns {available, enabled, frames, last_frame, bytes}. No argument: only report
        lua.set_function("next_layer_capture_control", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            int enable = -1;
            if (options)
            {
                sol::object e = (*options)["enabled"];
                if (e.get_type() == sol::type::boolean)
                    enable = e.as<bool>() ? 1 : 0;
            }
            return StateNodeToLua(s, DeviceState::NextLayerCaptureControl(emulator->GetContext(), enable));
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

        // region <ZX-bus slots through SlotControl (ZX-bus slots architecture.md §9)>
        // slots_state(), slots_catalog(), slots_matrix([table]),
        // slots_plug(slot, card [, opts]), slots_remove(slot [, opts]), slots_set(slot, opts), slots_gs(card [, opts]).
        // slot: zxbus.1, zxbus.next, ay-socket ("" or "auto" for plug: the planner's choice).
        // opts: {options = "dip=ym,saa" | {dip = "ym,saa"}, replace = true, dry_run = true, media = "save" | "discard",
        //        adapter = "..."}; any other key is a card option (slots_set(slot, {dip = "ym,gs"})).
        // Each returns the reply table every surface returns: ok, status, message and the verb's fields (the report,
        // cards, tables; for a change the plan, restart = {restarted, emulatorId, ...}, media). A change restarts
        // the machine: a script bound to the old instance follows it to the new one
        lua.set_function("slots_state", [this](sol::this_state s) {
            SlotControlRequest request;
            request.verb = "list";
            return StateNodeToLua(s, slotsCall(request, sol::nullopt, false).ToValue());
        });
        lua.set_function("slots_catalog", [this](sol::this_state s) {
            SlotControlRequest request;
            request.verb = "catalog";
            return StateNodeToLua(s, slotsCall(request, sol::nullopt, false).ToValue());
        });
        lua.set_function("slots_matrix", [this](sol::this_state s, sol::optional<std::string> table) {
            SlotControlRequest request;
            request.verb = "matrix";
            request.table = table.value_or("");
            return StateNodeToLua(s, slotsCall(request, sol::nullopt, false).ToValue());
        });
        lua.set_function("slots_plug", [this](sol::this_state s, const std::string& slot, const std::string& card,
                                              sol::optional<sol::table> opts) {
            SlotControlRequest request;
            request.verb = "plug";
            request.slot = slot == "auto" ? std::string() : slot;
            request.card = card;
            return StateNodeToLua(s, slotsCall(request, opts, true).ToValue());
        });
        lua.set_function("slots_remove", [this](sol::this_state s, const std::string& slot, sol::optional<sol::table> opts) {
            SlotControlRequest request;
            request.verb = "remove";
            request.slot = slot;
            return StateNodeToLua(s, slotsCall(request, opts, false).ToValue());
        });
        lua.set_function("slots_set", [this](sol::this_state s, const std::string& slot, sol::optional<sol::table> opts) {
            SlotControlRequest request;
            request.verb = "set";
            request.slot = slot;
            return StateNodeToLua(s, slotsCall(request, opts, true).ToValue());
        });
        lua.set_function("slots_gs", [this](sol::this_state s, const std::string& card, sol::optional<sol::table> opts) {
            SlotControlRequest request;
            request.verb = "gs";
            request.card = card;
            return StateNodeToLua(s, slotsCall(request, opts, false).ToValue());
        });
        // endregion </ZX-bus slots>

        // region <Media: every slot through MediaControl (media-control-design.md)>
        // media_list(), media_info(slot), media_formats([kind]),
        // media_insert(slot, path [, opts]), media_swap(slot, path [, opts]),
        // media_eject(slot [, opts]), media_save(slot [, path] [, opts]),
        // media_export(slot, path), media_discard(slot [, opts]),
        // media_rescan(slot [, opts]), media_create(slot [, opts]),
        // media_protect(slot, on), media_compose(descriptor [, opts]), media_layers(slot),
        // media_volumes(slot), media_files(slot [, dir, opts]), media_read(slot, file [, opts]),
        // media_read_bytes(slot, file [, volume]), media_write(slot, file, bytes [, opts]) (bytes nil: opts["in"]
        // names a host file), media_delete(slot, file [, opts]), media_rename(slot, file, to [, opts]),
        // media_mkdir(slot, dir [, opts]), media_attrib(slot, file, opts), media_label(slot, name [, opts]),
        // and media(verb, slot, path, opts).
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
        // controllers(verb, [connector], [device], [{options}]) -> the reply table every surface returns: the machine's
        // joystick connectors (list, get, plug, unplug, options, targets, auto, input; ControllerControl, as the
        // CLI `controllers`, WebAPI /controllers and Python controllers_*)
        lua.set_function("controllers", [this](sol::this_state s, const std::string& verb, sol::optional<std::string> connector,
                                               sol::optional<std::string> device, sol::optional<sol::table> opts) {
            controllers::ControllerRequest request;
            request.verb = verb;
            request.selector = connector.value_or("");
            request.device = device.value_or("");
            if (opts)
                for (const auto& [key, value] : *opts)
                {
                    if (!key.is<std::string>())
                        continue;
                    std::string text;
                    if (value.is<bool>())
                        text = value.as<bool>() ? "true" : "false";
                    else if (value.get_type() == sol::type::number)
                        text = std::to_string(value.as<long long>());
                    else if (value.is<std::string>())
                        text = value.as<std::string>();
                    request.options[key.as<std::string>()] = text;
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
            return StateNodeToLua(s, controllers::ControllerControl(emulator->GetContext()).Execute(request).ToValue());
        });
        // actions() -> {instance, actions = {{id, label, group, kind, held, gui, scope, needs_pause, available, error,
        // reason}, ...}}; action(id, [value], [params]) -> {ok, action, error, message, result}: the core
        // ActionRegistry (control surfaces actions.md §6), as the CLI `action`, WebAPI /controllers/actions, MCP
        // `actions` and Python actions() / action(). value by the action's kind: nil (a press), true / false, a number
        // (delta steps or a 0..1 level), a string (text), or a table {pressed, delta, rate, level, text}. A changing
        // action passes the script gate as the script's own control calls (an implicit session without one)
        lua.set_function("actions", [this](sol::this_state s) {
            Emulator* emulator = effectiveEmulator();
            const std::string instance = emulator ? emulator->GetId() : std::string();
            actions::ActionOrigin origin;
            origin.origin = execution::Origin::Lua;
            origin.identity = "lua|script";
            StateNode body = StateNode::Object();
            body["instance"] = instance;
            body["rev"] = static_cast<int64_t>(actions::ActionRegistry::Instance().Revision());
            StateNode& rows = body["actions"];
            rows = StateNode::Array();
            for (const actions::ActionStatus& status : actions::ActionRegistry::Instance().Statuses(instance, origin))
            {
                StateNode row = StateNode::Object();
                row["id"] = status.info.id;
                row["label"] = status.info.label;
                row["group"] = status.info.group;
                row["kind"] = actions::ValueKindName(status.info.kind);
                row["held"] = status.info.held;
                row["gui"] = status.info.gui;
                row["scope"] = automation_gate::CompanionScopeName(status.scope);
                row["needs_pause"] = status.needsPause;
                row["toggle"] = status.info.toggle;
                row["description"] = status.info.description;
                if (!status.info.params.empty())
                    row["params"] = status.info.params;
                row["available"] = status.available;
                if (!status.available)
                {
                    row["error"] = status.error;
                    row["reason"] = status.reason;
                }
                rows.push(std::move(row));
            }
            return StateNodeToLua(s, body);
        });
        lua.set_function("action", [this](sol::this_state s, const std::string& id, sol::optional<sol::object> rawValue,
                                          sol::optional<sol::table> params) {
            auto fail = [&s, &id](const std::string& error, const std::string& message) {
                StateNode out = StateNode::Object();
                out["ok"] = false;
                out["action"] = id;
                out["error"] = error;
                out["message"] = message;
                return StateNodeToLua(s, out);
            };
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return fail("Not Found", "no emulator selected");
            actions::ActionRegistry& registry = actions::ActionRegistry::Instance();
            const std::optional<actions::ActionInfo> info = registry.Find(id);
            if (!info)
                return fail("Not Found", "unknown action '" + id + "'");
            auto toNode = [](const sol::object& value) -> StateNode {
                if (value.is<bool>())
                    return StateNode(value.as<bool>());
                if (value.get_type() == sol::type::number)
                {
                    const double number = value.as<double>();
                    return number == std::floor(number) ? StateNode(static_cast<int64_t>(number)) : StateNode(number);
                }
                if (value.is<std::string>())
                    return StateNode(value.as<std::string>());
                return StateNode();
            };
            actions::ActionValue value;
            value.kind = info->kind;
            std::string error;
            if (rawValue && rawValue->valid() && rawValue->get_type() != sol::type::lua_nil)
            {
                const sol::object& raw = *rawValue;
                if (raw.is<bool>())
                {
                    value.pressed = raw.as<bool>();
                    value.level = value.pressed ? 1.0 : 0.0;
                }
                else if (raw.get_type() == sol::type::number)
                {
                    value.delta = raw.as<double>();
                    value.level = std::clamp(raw.as<double>(), 0.0, 1.0);
                    value.pressed = value.level > 0;
                }
                else if (raw.is<std::string>())
                {
                    if (!actions::ValueFromText(info->kind, raw.as<std::string>(), value, error))
                        return fail("Bad Request", error);
                }
                else if (raw.get_type() == sol::type::table)
                {
                    const sol::table table = raw.as<sol::table>();
                    value.pressed = table.get_or("pressed", true);
                    value.delta = table.get_or("delta", 0.0);
                    value.rate = table.get_or("rate", 0.0);
                    value.level = std::clamp(table.get_or("level", 0.0), 0.0, 1.0);
                    value.text = table.get_or<std::string>("text", "");
                }
                else
                    return fail("Bad Request", "value: nil, a boolean, a number, a string or a table");
            }
            if (params)
                for (const auto& [key, item] : *params)
                    if (key.is<std::string>())
                        value.params[key.as<std::string>()] = toNode(item);
            actions::ActionOrigin origin;
            origin.origin = execution::Origin::Lua;
            origin.identity = "lua|script";
            const actions::ActionResult result = registry.Run(id, std::move(value), emulator->GetId(), origin);
            StateNode out = StateNode::Object();
            out["ok"] = result.ok;
            out["action"] = id;
            if (!result.ok)
                out["error"] = result.error;
            out["message"] = result.message;
            out["result"] = result.body;
            return StateNodeToLua(s, out);
        });
        // context() -> {instance, context, set, by, default}: the instance's surface context (actions::ContextState,
        // control surfaces architecture §6), as the CLI `action context`, WebAPI GET /api/v1/controllers/context, MCP
        // `actions` context and Python context(); set it with action("ctx.set", "ttd") ("" returns to the default)
        lua.set_function("context", [this](sol::this_state s) {
            Emulator* emulator = effectiveEmulator();
            StateNode out = StateNode::Object();
            if (!emulator)
            {
                out["ok"] = false;
                out["error"] = "Not Found";
                out["message"] = "no emulator selected";
                return StateNodeToLua(s, out);
            }
            const actions::ContextInfo info = actions::ContextState::Instance().Get(emulator->GetId(), emulator);
            out["ok"] = true;
            out["instance"] = emulator->GetId();
            out["context"] = info.context;
            out["set"] = info.set;
            out["by"] = info.by;
            out["default"] = info.fallback;
            return StateNodeToLua(s, out);
        });
        // surfaces(verb, {args}) -> the reply table of a control surfaces verb (actions::surfaces::Call: views, target,
        // sessions, session, pin, readouts, activity, view_add, view_update, view_remove, session_open,
        // session_close), as WebAPI /api/v1/surfaces/..., the CLI `surface`, MCP `actions` and Python surfaces()
        lua.set_function("surfaces", [](sol::this_state s, sol::optional<std::string> verb, sol::optional<sol::table> opts) {
            std::function<StateNode(const sol::object&)> toNode = [&toNode](const sol::object& value) -> StateNode {
                if (value.is<bool>())
                    return StateNode(value.as<bool>());
                if (value.get_type() == sol::type::number)
                {
                    const double number = value.as<double>();
                    return number == std::floor(number) ? StateNode(static_cast<int64_t>(number)) : StateNode(number);
                }
                if (value.is<std::string>())
                    return StateNode(value.as<std::string>());
                if (value.get_type() == sol::type::table)
                {
                    const sol::table table = value.as<sol::table>();
                    // A sequence is a list ({"run.state", "media.*"}), anything else an object ({view = "v1"})
                    if (table.size() > 0)
                    {
                        StateNode array = StateNode::Array();
                        for (size_t i = 1; i <= table.size(); i++)
                            array.push(toNode(table[i]));
                        return array;
                    }
                    StateNode object = StateNode::Object();
                    for (const auto& [key, item] : table)
                        if (key.is<std::string>())
                            object[key.as<std::string>()] = toNode(item);
                    return object;
                }
                return StateNode();
            };
            StateNode args = StateNode::Object();
            if (opts)
                for (const auto& [key, item] : *opts)
                    if (key.is<std::string>())
                        args[key.as<std::string>()] = toNode(item);
            return StateNodeToLua(s, actions::surfaces::Call(verb.value_or("views"), args).body);
        });
        // streamdeck(verb, {args}) -> the reply table of a Stream Deck daemon verb (companion::streamdeck::Call:
        // status, start, stop, restart, settings, pairing, pair, pair_cancel, revoke, brightness, packs, pack, udev),
        // as WebAPI /api/v1/streamdeck/..., the CLI `streamdeck`, MCP `streamdeck` and Python streamdeck():
        // streamdeck("pack", {deck = "A1B2C3", pack = "os4"})
        lua.set_function("streamdeck", [](sol::this_state s, sol::optional<std::string> verb, sol::optional<sol::table> opts) {
            StateNode args = StateNode::Object();
            if (opts)
                for (const auto& [key, item] : *opts)
                {
                    if (!key.is<std::string>())
                        continue;
                    const std::string name = key.as<std::string>();
                    if (item.is<bool>())
                        args[name] = StateNode(item.as<bool>());
                    else if (item.get_type() == sol::type::number)
                    {
                        const double number = item.as<double>();
                        args[name] = number == std::floor(number) ? StateNode(static_cast<int64_t>(number))
                                                                  : StateNode(number);
                    }
                    else if (item.is<std::string>())
                        args[name] = StateNode(item.as<std::string>());
                }
            return StateNodeToLua(s, companion::streamdeck::Call(verb.value_or("status"), args).body);
        });
        // monitor(name, {instance = }) -> the reply table of a monitoring verb (monitoring::Call: all, host.stats,
        // host.clients, emulator.stats, debug.breakpoints, media.slots, recording.state, debug.labels, ttd.status,
        // debug.analyzers, controllers.state, game.events; an instance's name defaults to this emulator), as WebAPI
        // /api/v1/monitor[/{name}], the CLI `monitor`, MCP inspect_state aspect `monitor` and Python monitor()
        lua.set_function("monitor", [this](sol::this_state s, sol::optional<std::string> verb, sol::optional<sol::table> opts) {
            Emulator* emulator = effectiveEmulator();
            StateNode args = StateNode::Object();
            if (opts)
                for (const auto& [key, item] : *opts)
                {
                    if (!key.is<std::string>())
                        continue;
                    const std::string name = key.as<std::string>();
                    if (item.get_type() == sol::type::number)
                    {
                        const double number = item.as<double>();
                        args[name] = number == std::floor(number) ? StateNode(static_cast<int64_t>(number))
                                                                  : StateNode(number);
                    }
                    else if (item.is<std::string>())
                        args[name] = StateNode(item.as<std::string>());
                }
            const std::string name = verb.value_or("all");
            if (args.find("instance") == nullptr && name != "all" && emulator != nullptr)
                args["instance"] = emulator->GetId();
            return StateNodeToLua(s, monitoring::Call(name, args).body);
        });
        // controllers_host(verb, {options}) -> the reply table every surface returns: the host game controllers
        // (devices, profiles, bindings, haptics, rumble, light; HostInputControl, the process's, not the machine's)
        auto hostControllersCall = [](sol::this_state s, const std::string& verb, sol::optional<sol::table> opts) {
#ifdef ENABLE_CONTROLLERS
            controllers::HostRequest request;
            request.verb = verb;
            if (opts)
                for (const auto& [key, value] : *opts)
                {
                    if (!key.is<std::string>())
                        continue;
                    std::string text;
                    if (value.is<bool>())
                        text = value.as<bool>() ? "true" : "false";
                    else if (value.get_type() == sol::type::number)
                        text = std::to_string(value.as<long long>());
                    else if (value.is<std::string>())
                        text = value.as<std::string>();
                    request.options[key.as<std::string>()] = text;
                }
            return JsonValueToLua(s, controllers::HostInputControl::Process(request).ToValue());
#else
            (void)verb;
            (void)opts;
            StateNode reply = StateNode::Object();
            reply["ok"] = false;
            reply["error"] = "unavailable";
            reply["message"] = "built without host game controllers (ENABLE_CONTROLLERS=OFF)";
            return StateNodeToLua(s, reply);
#endif
        };
        lua.set_function("controllers_host", hostControllersCall);
        // controllers_haptics([device]) -> devices[]: available or reason, actuators, the test effects
        lua.set_function("controllers_haptics", [hostControllersCall](sol::this_state s, sol::optional<std::string> device) {
            sol::table opts = sol::state_view(s).create_table();
            if (device && !device->empty())
                opts["device"] = *device;
            return hostControllersCall(s, "haptics", opts);
        });
        // controllers_rumble(device, [effect]) -> play a test effect (identify, low, high, clicks, left-trigger,
        // right-trigger, swell)
        lua.set_function("controllers_rumble", [hostControllersCall](sol::this_state s, const std::string& device,
                                                                     sol::optional<std::string> effect) {
            sol::table opts = sol::state_view(s).create_table();
            opts["device"] = device;
            opts["effect"] = effect.value_or("identify");
            return hostControllersCall(s, "rumble", opts);
        });
        // controllers_output(device, pattern | {steps=, clicks=, marks=}) -> a control surface's outputs (the Surface
        // Dial): a haptic pattern (tick, double, buzz), its steps per turn and clicks, or a seek's marks as its layer
        // plays them
        lua.set_function("controllers_output", [hostControllersCall](sol::this_state s, const std::string& device,
                                                                     sol::object what) {
            sol::table opts = sol::state_view(s).create_table();
            if (what.is<sol::table>())
                for (const auto& [key, value] : what.as<sol::table>())
                    opts[key] = value;
            else if (what.is<std::string>())
                opts["pattern"] = what.as<std::string>();
            opts["device"] = device;
            return hostControllersCall(s, "output", opts);
        });
        // controllers_light(device, color) -> a pad's light colour (#RRGGBB, r,g,b or off: a DualSense / DualShock 4
        // light bar)
        lua.set_function("controllers_light", [hostControllersCall](sol::this_state s, const std::string& device,
                                                                    const std::string& color) {
            sol::table opts = sol::state_view(s).create_table();
            opts["device"] = device;
            opts["color"] = color;
            return hostControllersCall(s, "light", opts);
        });
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
        // A composition descriptor (*.ucompose.yaml, or its JSON text) built without inserting it
        lua.set_function("media_compose", [mediaCall](sol::this_state s, const std::string& descriptor,
                                                      sol::optional<sol::table> opts) {
            return mediaCall(s, "compose", "", descriptor, opts);
        });
        lua.set_function("media_layers", [mediaCall](sol::this_state s, const std::string& slot) {
            return mediaCall(s, "layers", slot, "", sol::nullopt);
        });
        // The guest's unsaved writes as file operations, with the layer each touched
        lua.set_function("media_changes", [mediaCall](sol::this_state s, const std::string& slot) {
            return mediaCall(s, "changes", slot, "", sol::nullopt);
        });
        // The files on a slot's disk (DiskFs): TR-DOS, +3DOS, iS-DOS / TASiS, ATM CP/M, MB-02, MDOS, FAT (DSS, NedoOS); write, delete, rename, mkdir
        lua.set_function("media_volumes", [mediaCall](sol::this_state s, const std::string& slot) {
            return mediaCall(s, "volumes", slot, "", sol::nullopt);
        });
        lua.set_function("media_files", [mediaCall](sol::this_state s, const std::string& slot, sol::optional<std::string> path,
                                                    sol::optional<sol::table> opts) {
            return mediaCall(s, "files", slot, path.value_or(""), opts);
        });
        lua.set_function("media_read", [mediaCall](sol::this_state s, const std::string& slot, const std::string& path,
                                                   sol::optional<sol::table> opts) {
            return mediaCall(s, "read", slot, path, opts);
        });
        // A file's bytes as a Lua string, or nil and the reason
        lua.set_function("media_read_bytes", [this](sol::this_state s, const std::string& slot, const std::string& path,
                                                    sol::optional<std::string> volume) -> std::tuple<sol::object, sol::object> {
            sol::state_view view(s);
            MediaRequest request{"read", slot, path, {}};
            if (volume && !volume->empty())
                request.options["volume"] = *volume;
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return {sol::make_object(view, sol::lua_nil), sol::make_object(view, "no emulator selected")};
            const MediaReply reply = MediaControl(emulator->GetContext()).Execute(request);
            if (!reply.result.Ok())
                return {sol::make_object(view, sol::lua_nil), sol::make_object(view, reply.result.message)};
            std::vector<uint8_t> bytes;
            base64::Decode(reply.body.find("data")->s, bytes);
            return {sol::make_object(view, std::string(bytes.begin(), bytes.end())), sol::make_object(view, sol::lua_nil)};
        });
        // A file onto a slot's disk (TR-DOS, +3DOS, iS-DOS, FAT): `bytes` a Lua string, or nil with opts["in"] a host file
        lua.set_function("media_write", [mediaCall](sol::this_state s, const std::string& slot, const std::string& path,
                                                    sol::optional<std::string> bytes, sol::optional<sol::table> opts) {
            if (!bytes)
                return mediaCall(s, "write", slot, path, opts);
            sol::state_view view(s);
            sol::table all = view.create_table();
            if (opts)
                for (const auto& [key, value] : *opts)
                    all[key] = value;
            all["data"] = base64::Encode(std::vector<uint8_t>(bytes->begin(), bytes->end()));
            return mediaCall(s, "write", slot, path, all);
        });
        lua.set_function("media_delete", [mediaCall](sol::this_state s, const std::string& slot, const std::string& path,
                                                     sol::optional<sol::table> opts) {
            return mediaCall(s, "delete", slot, path, opts);
        });
        // A new directory (FAT, iS-DOS catalogue); opts {parents = true} makes the missing ones on the way
        lua.set_function("media_mkdir", [mediaCall](sol::this_state s, const std::string& slot, const std::string& path,
                                                    sol::optional<sol::table> opts) {
            return mediaCall(s, "mkdir", slot, path, opts);
        });
        // A file's attributes and time: opts {attributes = "+R-H", time = "2026-10-26 14:30"}
        lua.set_function("media_attrib", [mediaCall](sol::this_state s, const std::string& slot, const std::string& path,
                                                     sol::optional<sol::table> opts) {
            return mediaCall(s, "attrib", slot, path, opts);
        });
        // The volume's name: FAT label ("" removes it), TR-DOS disk title, iS-DOS device name
        lua.set_function("media_label", [mediaCall](sol::this_state s, const std::string& slot, const std::string& label,
                                                    sol::optional<sol::table> opts) {
            sol::state_view view(s);
            sol::table all = view.create_table();
            if (opts)
                for (const auto& [key, value] : *opts)
                    all[key] = value;
            all["label"] = label;
            return mediaCall(s, "label", slot, "", all);
        });
        // A bare `to` stays in the file's directory; a path from the root moves it (iS-DOS, FAT)
        lua.set_function("media_rename", [mediaCall](sol::this_state s, const std::string& slot, const std::string& path,
                                                     const std::string& to, sol::optional<sol::table> opts) {
            sol::state_view view(s);
            sol::table all = view.create_table();
            if (opts)
                for (const auto& [key, value] : *opts)
                    all[key] = value;
            all["to"] = to;
            return mediaCall(s, "rename", slot, path, all);
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
        // A composite by a named strategy: strategy = "flat" (path) | "delta" | "commit"; plan, force
        lua.set_function("media_flatten", [mediaCall](sol::this_state s, const std::string& slot, sol::optional<std::string> path,
                                                      sol::optional<sol::table> opts) {
            return mediaCall(s, "flatten", slot, path.value_or(""), opts);
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
        // step / steps return how the run ended: {executed, stopped, breakpoint_id, address, access}
        // (stopped: a breakpoint ended it - an execution one before its instruction)
        auto stepOutcome = [](sol::state_view state, Emulator& emulator, unsigned executed) {
            const Emulator::BreakpointStop& stop = emulator.LastDirectStop();
            sol::table result = state.create_table();
            result["executed"] = executed;
            result["stopped"] = stop.hit;
            if (stop.hit)
            {
                result["breakpoint_id"] = stop.breakpointId;
                result["address"] = stop.address;
                result["access"] = BreakpointHitKindName(stop.kind);
            }
            return result;
        };

        // Automation session (execution arbiter design §6a): the GUI shows it and locks its controls; when the user
        // takes control back, control calls raise an error (revoked_by_user) - stop, do not retry, ask the user
        lua.set_function("take_control", [this](sol::optional<std::string> client) {
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                throw std::runtime_error("take_control: no emulator");
            std::string reason;
            if (!execution::script::TakeControl(*emulator, execution::Origin::Lua, client.value_or("lua"), reason))
                throw std::runtime_error("take_control refused: " + reason + " (do not retry)");
        });
        lua.set_function("release_control", [this]() {
            if (Emulator* emulator = effectiveEmulator())
                execution::script::ReleaseControl(*emulator, execution::Origin::Lua);
        });
        // Companion clients (docs/inprogress/2026-10-16-companion-client): devices the user paired at the GUI to act
        // as the user's hands. A script lists and revokes them; it never pairs, approves or acts as one
        lua.set_function("companions", [](sol::this_state s) -> sol::object {
            return StateNodeToLua(s, companion::CompanionRegistry::Instance().Report());
        });
        lua.set_function("companion_revoke", [](const std::string& idOrName) -> size_t {
            return companion::CompanionRegistry::Instance().Revoke(idOrName);
        });
        lua.set_function("step", [this, stepOutcome](sol::optional<bool> skipBP, sol::this_state ts) -> sol::object {
            if (!effectiveEmulator()) return sol::lua_nil;
            execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
            Emulator& emulator = *effectiveEmulator();
            emulator.RunSingleCPUCycle(skipBP.value_or(true));
            const Emulator::BreakpointStop& stop = emulator.LastDirectStop();
            const unsigned executed = (stop.hit && stop.kind == BreakpointHitKind::Execute) ? 0 : 1;
            return stepOutcome(sol::state_view(ts), emulator, executed);
        });

        lua.set_function("steps", [this, stepOutcome](unsigned count, sol::optional<bool> skipBP, sol::this_state ts) -> sol::object {
            if (!effectiveEmulator()) return sol::lua_nil;
            execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
            Emulator& emulator = *effectiveEmulator();
            const unsigned executed = emulator.RunNCPUCycles(count, skipBP.value_or(false));
            return stepOutcome(sol::state_view(ts), emulator, executed);
        });

        lua.set_function("stepover", [this]() {
            if (!effectiveEmulator()) return;
            execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
            effectiveEmulator()->StepOver();
        });

        // Frame stepping methods
        lua.set_function("run_frame", [this](sol::optional<bool> skipBP) {
            if (!effectiveEmulator()) return;
            execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
            effectiveEmulator()->RunFrame(skipBP.value_or(true));
        });

        lua.set_function("run_frames", [this](unsigned count, sol::optional<bool> skipBP) {
            if (!effectiveEmulator()) return;
            execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
            effectiveEmulator()->RunNFrames(count, skipBP.value_or(true));
        });

        // Atomic stepping methods
        lua.set_function("run_tstates", [this](unsigned count, sol::optional<bool> skipBP) {
            if (!effectiveEmulator()) return;
            execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
            effectiveEmulator()->RunTStates(count, skipBP.value_or(true));
        });

        lua.set_function("run_to_scanline", [this](unsigned scanline, sol::optional<bool> skipBP) {
            if (!effectiveEmulator()) return;
            execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
            effectiveEmulator()->RunUntilScanline(scanline, skipBP.value_or(true));
        });

        lua.set_function("run_scanlines", [this](unsigned count, sol::optional<bool> skipBP) {
            if (!effectiveEmulator()) return;
            execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
            effectiveEmulator()->RunNScanlines(count, skipBP.value_or(true));
        });

        lua.set_function("run_to_pixel", [this](sol::optional<bool> skipBP) {
            if (!effectiveEmulator()) return;
            execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
            effectiveEmulator()->RunUntilNextScreenPixel(skipBP.value_or(true));
        });

        // The paced run (core execution::RunPacer; the same pacer as WebAPI /run_pace, the actions run.pace_* and the
        // Qt speed control): pace() -> {preset, label, unit, interval_ms, running, stop_reason, presets}; the others
        // answer the reply as a table with ok (false: error = the message) and the pace
        auto paceReply = [](sol::this_state ts, const execution::RunReply& reply) -> sol::object {
            StateNode value = reply.body;
            value["ok"] = reply.Ok();
            if (!reply.Ok())
                value["error"] = reply.message;
            return StateNodeToLua(ts, value);
        };
        auto pacerOf = [this]() -> std::shared_ptr<execution::RunPacer> {
            Emulator* emulator = effectiveEmulator();
            return emulator ? execution::RunPacer::For(EmulatorManager::GetInstance()->GetEmulator(emulator->GetId()))
                            : nullptr;
        };
        lua.set_function("pace", [this](sol::this_state ts) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return sol::lua_nil;
            const auto pacer = execution::RunPacer::Find(emulator->GetId());
            return StateNodeToLua(ts, pacer ? pacer->StateBody(true) : execution::RunPacer::DefaultStateBody(true));
        });
        // pace_set(preset [, interval_ms]): full, 25fps, 5fps, 1fps, frame, 100lines, 10lines, 1line, 1op
        lua.set_function("pace_set", [this, paceReply, pacerOf](sol::this_state ts, const std::string& name,
                                                                sol::optional<int> intervalMs) -> sol::object {
            const auto pacer = pacerOf();
            if (!pacer)
                return sol::lua_nil;
            size_t index = 0;
            if (!execution::RunPacer::PresetIndex(name, index))
                throw std::runtime_error("pace_set: preset full, 25fps, 5fps, 1fps, frame, 100lines, 10lines, 1line or 1op");
            execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
            return paceReply(ts, pacer->Set(execution::script::RequesterOf(*effectiveEmulator(), execution::Origin::Lua),
                                            index, intervalMs ? std::optional<int>(*intervalMs) : std::nullopt));
        });
        lua.set_function("pace_interval", [this, paceReply, pacerOf](sol::this_state ts, int intervalMs) -> sol::object {
            const auto pacer = pacerOf();
            if (!pacer)
                return sol::lua_nil;
            execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
            return paceReply(ts, pacer->Set(execution::script::RequesterOf(*effectiveEmulator(), execution::Origin::Lua),
                                            std::nullopt, intervalMs));
        });
        for (const char* name : {"pace_next", "pace_prev", "pace_start", "pace_stop", "pace_toggle", "pace_step"})
        {
            const std::string verb = std::string(name).substr(5);
            lua.set_function(name, [this, paceReply, pacerOf, verb](sol::this_state ts) -> sol::object {
                const auto pacer = pacerOf();
                if (!pacer)
                    return sol::lua_nil;
                Emulator& emulator = *effectiveEmulator();
                if (verb != "stop")
                    execution::script::Check(emulator, execution::Origin::Lua);
                const execution::Requester requester = execution::script::RequesterOf(emulator, execution::Origin::Lua);
                if (verb == "next" || verb == "prev")
                    return paceReply(ts, pacer->Shift(requester, verb == "next" ? 1 : -1));
                if (verb == "start")
                    return paceReply(ts, pacer->Start(requester));
                if (verb == "stop")
                    return paceReply(ts, pacer->Stop());
                if (verb == "toggle")
                    return paceReply(ts, pacer->Toggle(requester));
                return paceReply(ts, pacer->Step(requester));
            });
        }

        lua.set_function("run_to_interrupt", [this](sol::optional<bool> skipBP) {
            if (!effectiveEmulator()) return;
            execution::script::Check(*effectiveEmulator(), execution::Origin::Lua);
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
            // false: a character no key types, nothing typed
            return ctx->pDebugManager->GetKeyboardManager()->TypeText(text, delayFrames.value_or(2)).empty();
        });

        lua.set_function("key_release_all", [this]() {
            Emulator* emulator = effectiveEmulator();
            EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
            if (ctx && ctx->pDebugManager->GetKeyboardManager())
                ctx->pDebugManager->GetKeyboardManager()->ReleaseAllKeys();
        });

        // Tape operations
        // ok, reason: the reason is set when the insert is refused (TTD v1 while recording)
        lua.set_function("tape_load", [this](const std::string& path) -> std::tuple<bool, std::string> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return {false, "no emulator"};
            if (std::string refusal = emulator->AskMediaChange(Emulator::MediaChange::LoadTape, 0, path); !refusal.empty())
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

        // A long move (-4096..4096) in steps the program follows, one per frame; input sent meanwhile queues
        lua.set_function("mouse_glide", [this](sol::this_state s, sol::object dxArg, sol::object dyArg) {
            DebugMouseManager* mgr = mouseManager();
            if (!mgr)
                return mouseError(s, "mouse manager not available");
            std::string error;
            long long dx = 0;
            long long dy = 0;
            if (!mouseIntArg(dxArg, "dx", INT_MIN, INT_MAX, dx, error) ||
                !mouseIntArg(dyArg, "dy", INT_MIN, INT_MAX, dy, error))
                return mouseError(s, error);
            return mouseResult(s, *mgr, mgr->Glide(static_cast<int>(dx), static_cast<int>(dy)));
        });

        // The machine's mouse devices (table of device tables; empty when the machine has none)
        lua.set_function("mouse_devices", [this](sol::this_state s) {
            DebugMouseManager* mgr = mouseManager();
            if (!mgr)
                return mouseError(s, "mouse manager not available");
            sol::state_view lua(s);
            sol::table devices = lua.create_table();
            const MouseStateSnapshot state = mgr->GetState();
            for (size_t i = 0; i < state.devices.size(); i++)
                devices[i + 1] = mouseDeviceTable(s, state.devices[i]);
            sol::variadic_results results;
            results.push_back(sol::make_object(s, devices));
            return results;
        });

        lua.set_function("mouse_busy", [this]() -> bool {
            DebugMouseManager* mgr = mouseManager();
            return mgr && mgr->IsBusy();
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

        lua.set_function("mouse_status", [this](sol::this_state s, sol::object deviceArg) {
            DebugMouseManager* mgr = mouseManager();
            if (!mgr)
                return mouseError(s, "mouse manager not available");
            // mouse_status([device]): the machine's mouse, or the device named ("kempston", "sprinter", "evo-ps2")
            std::string deviceId;
            if (deviceArg.valid() && deviceArg.get_type() != sol::type::lua_nil)
            {
                if (deviceArg.get_type() != sol::type::string)
                    return mouseError(s, "device must be a string (kempston, sprinter, evo-ps2)");
                deviceId = deviceArg.as<std::string>();
            }
            if (const MouseInjectResult check = mgr->CheckDevice(deviceId); !check.ok())
                return mouseError(s, check.message);
            const MouseStateSnapshot state = mgr->GetState(deviceId);
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
            return joystickResult(s, *mgr, mgr->SetNames(names));
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
        // snapshot_load(path [, commit]) -> ok, reason, emulator_id: the reason is set when TTD refuses the load
        // (recording), the snapshot pipeline refuses it (the machine's policy, an unknown commit name) or the load
        // fails. `commit` chooses who writes the machine: omitted = the plan decides (the machine's own policy, else
        // the legacy commit), "legacy" = the legacy commit always, or a registered policy name. A file that needs
        // another model (an SPG: TS-Conf) switches it first when this Lua is not bound to one machine (as
        // rzx_play); emulator_id is then the new instance
        lua.set_function("snapshot_load", [this](const std::string& path, sol::optional<std::string> commit)
                                              -> std::tuple<bool, std::string, std::string> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return {false, "no emulator", ""};
            SnapshotLoadRequest request;
            request.emulatorId = emulator->GetId();
            request.path = path;
            if (_emulator != nullptr)
                request.switchModel = false;   // a script bound to a machine never replaces it; unbound: the launcher's default
            request.commit = commit.value_or("");
            const SnapshotLoadResult result = SnapshotLauncher::Load(request);
            return {result.ok, result.message, result.emulator ? result.emulator->GetId() : std::string()};
        });

        // snapshot_inspect(path [, commit]) -> table | nil, error: what loading the file would do here, nothing
        // written. {path, image = {format, format_version, machine_hint, memory_model, banks = {{bank, size, hash}},
        // cpu, paging, border, ay, extensions, warnings}, plan = {commit, refused, reason, needs, verdicts,
        // warnings}, would_load, would_commit}
        lua.set_function("snapshot_inspect", [this](sol::this_state state, const std::string& path,
                                                    sol::optional<std::string> commit)
                                                     -> std::tuple<sol::object, std::string> {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return {sol::lua_nil, "no emulator"};
            StateNode result;
            std::string error;
            if (!SnapshotLauncher::Inspect(emulator->GetId(), path, commit.value_or(""), result, error))
                return {sol::lua_nil, error};
            return {StateNodeToLua(state, result), std::string()};
        });

        // snapshot_report() -> table | nil: what the snapshot pipeline did with the last load {format, machine_hint,
        // commit, refused, reason, needs, verdicts, items = {{item, outcome, note}}, warnings}
        lua.set_function("snapshot_report", [this](sol::this_state state) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator || emulator->LastSnapshotReport().format.empty())
                return sol::lua_nil;
            return StateNodeToLua(state, emulator->LastSnapshotReport().ToStateNode());
        });

        // RZX input recordings. rzx_play(path [, options]) -> table {ok, message,
        // emulator_id, model_switched, required_model, summary}; options:
        // desync_mode ("strict" / "tolerant"), ei_short_frame_blocks_int,
        // ld_air_parity_quirk, ignore_later_snapshots, switch_model. The model
        // switches only when this interpreter follows the selected machine (a
        // bound one reports the mismatch: its machine must not be replaced)
        // The files opened lately (RecentFiles; open one: action("media.open_recent", nil, {index = 0, kind = "tape"}))
        lua.set_function("recent_files", [](sol::this_state ts, sol::optional<std::string> kind) -> sol::object {
            const std::string k = kind.value_or("all");
            if (k != "all" && !RecentFiles::IsKind(k))
                throw sol::error("kind '" + k + "': all, snapshot, tape, disk, rzx, image or cd");
            return StateNodeToLua(ts, RecentFiles::Instance().ToValue(k));
        });
        lua.set_function("recent_clear", [](sol::optional<std::string> kind) {
            const std::string k = kind.value_or("all");
            if (k != "all" && !RecentFiles::IsKind(k))
                throw sol::error("kind '" + k + "': all, snapshot, tape, disk, rzx, image or cd");
            RecentFiles::Instance().Clear(k);
        });
        // The core's recording presets (video_record("start", {preset = "gameplay"}))
        lua.set_function("recording_presets", [](sol::this_state ts) -> sol::object {
            return StateNodeToLua(ts, capture::RecordingPresetsValue());
        });

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

        // snapshot_save(path) -> bool; false = refused or failed, snapshot_save_report() says why
        lua.set_function("snapshot_save", [this](const std::string& path) -> bool {
            if (!effectiveEmulator()) return false;
            return effectiveEmulator()->SaveSnapshot(path);
        });

        // snapshot_save_report() -> table | nil: the last save {ok, message, format, path, machine, reason, needs, warnings}
        lua.set_function("snapshot_save_report", [this](sol::this_state state) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator || emulator->LastSaveResult().path.empty())
                return sol::lua_nil;
            return StateNodeToLua(state, emulator->LastSaveResult().ToStateNode());
        });

        // snapshot_formats() -> table: which formats this machine can be saved in right now {view_available, machine, view,
        // formats = {{format, available, reason, needs, note}}}
        lua.set_function("snapshot_formats", [this](sol::this_state state) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return sol::lua_nil;
            return StateNodeToLua(state, emulator->SnapshotSaveFormats().ToStateNode());
        });

        // Breakpoint management
        // Options: a page string ("ram32": physical, through any slot that shows it), or a table
        // {page=, to=, slot_only=, hits=} (to = range end; hits "5" | ">=5" | "%5"). -1 when refused
        lua.set_function("bp", [this](uint16_t addr, sol::optional<sol::object> options) -> int {
            return AddScriptBreakpoint(BRK_MEMORY, BRK_MEM_EXECUTE, addr, options);
        });

        // Options: a page string ("ram32": physical, through any slot that shows it), or a table
        // {page=, to=, slot_only=, hits=} (to = range end; hits "5" | ">=5" | "%5"). -1 when refused
        lua.set_function("bp_read", [this](uint16_t addr, sol::optional<sol::object> options) -> int {
            return AddScriptBreakpoint(BRK_MEMORY, BRK_MEM_READ, addr, options);
        });

        // Options: a page string ("ram32": physical, through any slot that shows it), or a table
        // {page=, to=, slot_only=, hits=} (to = range end; hits "5" | ">=5" | "%5"). -1 when refused
        lua.set_function("bp_write", [this](uint16_t addr, sol::optional<sol::object> options) -> int {
            return AddScriptBreakpoint(BRK_MEMORY, BRK_MEM_WRITE, addr, options);
        });

        // Options: a table {mask=, hits=} (mask: (port & mask) == (port & mask) matches)
        lua.set_function("bp_port_in", [this](uint16_t port, sol::optional<sol::object> options) -> int {
            return AddScriptBreakpoint(BRK_IO, BRK_IO_IN, port, options);
        });

        // Options: a table {mask=, hits=} (mask: (port & mask) == (port & mask) matches)
        lua.set_function("bp_port_out", [this](uint16_t port, sol::optional<sol::object> options) -> int {
            return AddScriptBreakpoint(BRK_IO, BRK_IO_OUT, port, options);
        });

        // ZX Next register breakpoints. Options: a table {to=, doors=, value=, vmask=, hits=} (to = register range end; doors
        // "nextreg,port,copper" a write may come through; value = stop only on it, vmask = the compared bits). -1 when refused
        lua.set_function("bp_nextreg_write", [this](uint16_t reg, sol::optional<sol::table> options) -> int {
            return AddScriptNextRegBreakpoint(BRK_IO_OUT, reg, options);
        });
        lua.set_function("bp_nextreg_read", [this](uint16_t reg, sol::optional<sol::table> options) -> int {
            return AddScriptNextRegBreakpoint(BRK_IO_IN, reg, options);
        });
        // bp_beam(line, {hc=}) stops when the beam passes the place (the copper's counters); bp_copper(index, {to=}) when the copper
        // completes that list instruction. -1 when refused
        lua.set_function("bp_beam", [this](uint16_t line, sol::optional<sol::table> options) -> int {
            return AddScriptPositionBreakpoint(BRK_BEAM, line, options);
        });
        lua.set_function("bp_copper", [this](uint16_t index, sol::optional<sol::table> options) -> int {
            return AddScriptPositionBreakpoint(BRK_COPPER, index, options);
        });
        // bp_magic("ed_ff" | "dd_01", {hits=}) stops after the CPU ran the magic opcode (a NOP / LD BC,nn on the hardware). -1 when refused
        lua.set_function("bp_magic", [this](const std::string& opcode, sol::optional<sol::table> options) -> int {
            if (opcode != "ed_ff" && opcode != "dd_01")
                return -1;
            return AddScriptPositionBreakpoint(BRK_MAGIC, opcode == "ed_ff" ? BRK_MAGIC_ED_FF : BRK_MAGIC_DD_01, options);
        });
        lua.set_function("bp_nextreg", [this](uint16_t reg, sol::optional<sol::table> options) -> int {
            return AddScriptNextRegBreakpoint(BRK_IO_IN | BRK_IO_OUT, reg, options);
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
            if (bpm) bpm->ClearUserBreakpoints();
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

        lua.set_function("bp_note", [this](uint16_t id, const std::string& note) -> bool {
            if (!effectiveEmulator()) return false;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pDebugManager) return false;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? bpm->SetBreakpointNote(id, note) : false;
        });

        lua.set_function("bp_group", [this](uint16_t id, const std::string& group) -> bool {
            if (!effectiveEmulator()) return false;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pDebugManager) return false;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? bpm->SetBreakpointGroup(id, group) : false;
        });

        // Breakpoints by selection (BreakpointSelection: all, an id, a kind, group:<name>): bp_select(select, op) with op
        // enable | disable | toggle | remove answers {select, selected, changed, active}; a refusal: ok = false, error
        lua.set_function("bp_select", [this](sol::this_state ts, const std::string& select, const std::string& op) -> sol::object {
            StateNode out = StateNode::Object();
            std::string error;
            BreakpointManager* bpm = effectiveEmulator() ? effectiveEmulator()->GetBreakpointManager() : nullptr;
            if (!bpm)
                error = "no emulator";
            else if (BreakpointSelection::Run(*bpm, select, op, out, error))
                return StateNodeToLua(ts, out);
            out["ok"] = false;
            out["error"] = error;
            return StateNodeToLua(ts, out);
        });
        // bp_key(key, {on = "press" | "release" | "both", hits =, group =, note =}): stops on the first keyboard read
        // that sees the key ("any": any key) go down / up. The id, or nil and the reason
        lua.set_function("bp_key", [this](sol::this_state ts, const std::string& key, sol::optional<sol::table> opts)
                                       -> std::tuple<sol::object, sol::object> {
            auto fail = [&ts](const std::string& why) {
                return std::make_tuple(sol::make_object(ts, sol::lua_nil), sol::make_object(ts, why));
            };
            BreakpointManager* bpm = effectiveEmulator() ? effectiveEmulator()->GetBreakpointManager() : nullptr;
            if (!bpm)
                return fail("no emulator");
            BreakpointSpec spec;
            std::string error;
            const std::string on = opts ? opts->get_or<std::string>("on", "press") : std::string("press");
            if (!BreakpointManager::ParseKeyboardInto(key, on, spec, error) ||
                (opts && !BreakpointManager::ParseHitSpec(opts->get_or<std::string>("hits", ""), spec.hitMode,
                                                          spec.hitTarget, error)))
                return fail(error);
            if (opts)
            {
                spec.group = opts->get_or<std::string>("group", "");
                spec.note = opts->get_or<std::string>("note", "");
            }
            const uint16_t id = bpm->AddBreakpoint(spec, error);
            if (id == BRK_INVALID)
                return fail(error);
            return std::make_tuple(sol::make_object(ts, id), sol::make_object(ts, sol::lua_nil));
        });

        lua.set_function("bp_reset_hits", [this](sol::optional<uint16_t> id) -> bool {
            if (!effectiveEmulator()) return false;
            BreakpointManager* bpm = effectiveEmulator()->GetBreakpointManager();
            if (!bpm) return false;
            if (id)
                return bpm->ResetHitCount(*id);
            bpm->ResetAllHitCounts();
            return true;
        });

        lua.set_function("bp_count", [this]() -> size_t {
            if (!effectiveEmulator()) return 0;
            auto* ctx = effectiveEmulator()->GetContext();
            if (!ctx || !ctx->pDebugManager) return 0;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? bpm->GetBreakpointsCount() : 0;
        });

        // bp_groups() -> {ok, instance, groups = {{name, total, active, ids}}}: as GET .../breakpoints/groups
        lua.set_function("bp_groups", [this](sol::this_state s) {
            StateNode args = StateNode::Object();
            if (Emulator* emulator = effectiveEmulator())
                args["instance"] = emulator->GetId();
            return StateNodeToLua(s, monitoring::Call("breakpoint_groups", args).body);
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
                if (!info.pageKind.empty())
                    result["page"] = lua_view.create_table_with("kind", info.pageKind, "page", info.pageNumber);
                result["hit_count"] = info.hitCount;
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

        // Symbol files and sets through SymbolControl (the same verbs, checks and fields as the WebAPI, CLI, MCP and
        // Python): symbols_import("f.sym") or symbols_import{path=..., format=, set=, space=, base=, policy=},
        // symbols_export{path=..., format=, sets={...}, pages=}, symbols_sets(), symbols_set(id, {enabled=, priority=}),
        // symbols_drop(id), symbols_formats(), symbols_detect(path), symbols_scan(), symbols_import_live{scanner=, page=,
        // offset=, set=, policy=}. A refusal: the table has ok = false, error = why
        lua.set_function("symbols_formats", [this](sol::this_state ts) -> sol::object { return symbolsRun(ts, "formats", {}); });
        lua.set_function("symbols_sets", [this](sol::this_state ts) -> sol::object { return symbolsRun(ts, "sets", {}); });
        lua.set_function("symbols_detect", [this](sol::this_state ts, sol::object arg) -> sol::object { return symbolsCall(ts, "detect", arg, "path"); });
        lua.set_function("symbols_import", [this](sol::this_state ts, sol::object arg) -> sol::object { return symbolsCall(ts, "import", arg, "path"); });
        lua.set_function("symbols_export", [this](sol::this_state ts, sol::object arg) -> sol::object { return symbolsCall(ts, "export", arg, "path"); });
        lua.set_function("symbols_drop", [this](sol::this_state ts, sol::object arg) -> sol::object { return symbolsCall(ts, "drop", arg, "id"); });
        // Assembler sources through AsmControl (the same verbs and fields as the WebAPI, CLI, MCP, Python and the Qt disk
        // browser): asm_formats(), asm_dialects(), asm_files("A"), asm_detect(path), asm_decode(path) or asm_decode{path=,
        // codec=, version=, output=}, asm_encode{text= | input=, codec=, version=, output=}, asm_convert{path=, to=, ...}.
        // A path is a host file or "disk:A/NAME.T". A refusal: ok = false, error = why
        lua.set_function("asm_formats", [this](sol::this_state ts) -> sol::object { return asmRun(ts, "formats", {}); });
        lua.set_function("asm_dialects", [this](sol::this_state ts) -> sol::object { return asmRun(ts, "dialects", {}); });
        lua.set_function("asm_files", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "files", SymbolOptions(arg, "drive")); });
        lua.set_function("asm_detect", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "detect", SymbolOptions(arg, "path")); });
        lua.set_function("asm_decode", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "decode", SymbolOptions(arg, "path")); });
        lua.set_function("asm_encode", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "encode", SymbolOptions(arg, "text")); });
        lua.set_function("asm_convert", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "convert", SymbolOptions(arg, "path")); });
        // The source an assembler holds in RAM (asm-synchronizer): asm_sync_status(), asm_sync_probe(), asm_sync_extract{as=, to=,
        // output=}, asm_sync_watch{interval=, quiet=, as=, to=, output=} (live labels in live:sync:<assembler>), asm_sync_unwatch(),
        // asm_sync_hints(), asm_sync_inject{text= | path=, from=, title=, force=, backup=} (a host source as the assembler's text);
        // snapshot= (a .szx / .sna / .z80 file) makes status / probe / extract / inject work on that file (inject writes output=);
        // asm_sync_push_watch{path=, from=, title=, force=, interval=, quiet=} injects each save of the host file, asm_sync_push_unwatch()
        lua.set_function("asm_sync_status", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "sync-status", SymbolOptions(arg, "assembler")); });
        lua.set_function("asm_sync_probe", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "sync-probe", SymbolOptions(arg, "snapshot")); });
        lua.set_function("asm_sync_extract", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "sync-extract", SymbolOptions(arg, "as")); });
        lua.set_function("asm_sync_watch", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "sync-watch", SymbolOptions(arg, "assembler")); });
        lua.set_function("asm_sync_unwatch", [this](sol::this_state ts) -> sol::object { return asmRun(ts, "sync-unwatch", {}); });
        lua.set_function("asm_sync_hints", [this](sol::this_state ts) -> sol::object { return asmRun(ts, "sync-hints", {}); });
        lua.set_function("asm_sync_inject", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "sync-inject", SymbolOptions(arg, "path")); });
        lua.set_function("asm_sync_push_watch", [this](sol::this_state ts, sol::object arg) -> sol::object { return asmRun(ts, "sync-push-watch", SymbolOptions(arg, "path")); });
        lua.set_function("asm_sync_push_unwatch", [this](sol::this_state ts) -> sol::object { return asmRun(ts, "sync-push-unwatch", {}); });
        lua.set_function("symbols_scan", [this](sol::this_state ts) -> sol::object { return symbolsRun(ts, "scan", {}); });
        lua.set_function("symbols_import_source", [this](sol::this_state ts, sol::object arg) -> sol::object {
            return symbolsCall(ts, "import-source", arg, "path");
        });
        lua.set_function("symbols_import_live", [this](sol::this_state ts, sol::optional<sol::table> opts) -> sol::object {
            return symbolsRun(ts, "import-live", opts ? SymbolOptions(sol::make_object(ts, *opts), "") : std::map<std::string, std::string>{});
        });
        lua.set_function("symbols_set", [this](sol::this_state ts, sol::object id, sol::optional<sol::table> opts) -> sol::object {
            std::map<std::string, std::string> options;
            if (opts)
                options = SymbolOptions(sol::make_object(ts, *opts), "");
            if (id.is<std::string>())
                options["id"] = id.as<std::string>();
            else
                for (auto& [key, value] : SymbolOptions(id, "id"))
                    options[key] = value;
            return symbolsRun(ts, "set", std::move(options));
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
        // next_state() / next_regs() / next_mmu(): the ZX Spectrum Next reports (DeviceState::Next / NextRegs / NextMmu;
        // available=false on other machines)
        lua.set_function("next_state", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::Next(emulator->GetContext()));
        });
        lua.set_function("next_regs", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::NextRegs(emulator->GetContext()));
        });
        lua.set_function("next_mmu", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::NextMmu(emulator->GetContext()));
        });
        // next_reg_journal([{regs="07,02", sources="nextreg,copper", since=N, from=F, to=F, limit=N}]): who wrote which NextREG,
        // when (frame, T, PC) and through which door (DeviceState::NextRegJournalReport); nil + error on a bad option.
        // Off by default: next_reg_journal_control({enabled=true}) first
        lua.set_function("next_reg_journal", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
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
            NextRegJournalQuery query;
            std::string error;
            if (!NextRegJournalQueryFromStrings(text("regs"), text("sources"), text("since"), text("from"), text("to"), text("limit"),
                                                query, error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, DeviceState::NextRegJournalReport(emulator->GetContext(), query)));
            return out;
        });
        // next_reg_journal_control({enabled=true|false, clear=true, capacity=N}): switch / clear / resize the NextREG journal
        lua.set_function("next_reg_journal_control", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            int enable = -1;
            bool clear = false;
            size_t capacity = 0;
            if (options)
            {
                sol::object e = (*options)["enabled"];
                if (e.get_type() == sol::type::boolean)
                    enable = e.as<bool>() ? 1 : 0;
                sol::object c = (*options)["clear"];
                if (c.get_type() == sol::type::boolean)
                    clear = c.as<bool>();
                sol::object n = (*options)["capacity"];
                if (n.get_type() == sol::type::number)
                    capacity = static_cast<size_t>(n.as<long long>());
            }
            return StateNodeToLua(s, DeviceState::NextRegJournalControl(emulator->GetContext(), enable, clear, capacity));
        });
        // next_sd([{kinds="command,read,write", card=0|1, since=N, from=F, to=F, limit=N}]): the Next's SD cards, the SPI engine and the
        // SD journal (DeviceState::NextSdReport); nil + error on a bad option. The journal is off by default: next_sd_journal_control({enabled=true}) first
        lua.set_function("next_sd", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
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
            NextSdJournalQuery query;
            std::string error;
            if (!NextSdJournalQueryFromStrings(text("kinds"), text("card"), text("since"), text("from"), text("to"), text("limit"), query,
                                               error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, DeviceState::NextSdReport(emulator->GetContext(), query)));
            return out;
        });
        // next_sd_journal_control({enabled=true|false, clear=true, capacity=N}): switch / clear / resize the SD journal
        lua.set_function("next_sd_journal_control", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            int enable = -1;
            bool clear = false;
            size_t capacity = 0;
            if (options)
            {
                sol::object e = (*options)["enabled"];
                if (e.get_type() == sol::type::boolean)
                    enable = e.as<bool>() ? 1 : 0;
                sol::object c = (*options)["clear"];
                if (c.get_type() == sol::type::boolean)
                    clear = c.as<bool>();
                sol::object n = (*options)["capacity"];
                if (n.get_type() == sol::type::number)
                    capacity = static_cast<size_t>(n.as<long long>());
            }
            return StateNodeToLua(s, DeviceState::NextSdJournalControl(emulator->GetContext(), enable, clear, capacity));
        });
        // Bus analyzer (docs/inprogress/2026-10-11-bus-analyzer): bus_list(), bus_events(bus, {kinds=, slot=, since=, from=, to=,
        // limit=}), bus_findings(bus, {rules=, since=, limit=}), bus_control(bus|"all", {enabled=, checks=, clear=, capacity=, protocol_only=}),
        // bus_sd_timing(bus, {profile="ideal|realistic|slow", seed=N, slot=N}), bus_export(bus, {format="vcd|jsonl", ...}) -> text.
        // nil + error on a bad option or an unknown bus
        auto busOption = [](const sol::optional<sol::table>& options, const char* key) -> std::string {
            if (!options)
                return std::string();
            sol::object value = (*options)[key];
            if (value.get_type() == sol::type::number)
                return std::to_string(value.as<long long>());
            if (value.get_type() == sol::type::string)
                return value.as<std::string>();
            return std::string();
        };
        auto busSwitch = [](const sol::optional<sol::table>& options, const char* key) -> int {
            if (!options)
                return -1;
            sol::object value = (*options)[key];
            return value.get_type() == sol::type::boolean ? (value.as<bool>() ? 1 : 0) : -1;
        };
        auto busReply = [](sol::this_state s, const StateNode& node) -> sol::variadic_results {
            const StateNode* available = node.find("available");
            if (available && available->kind == StateNode::Kind::Bool && !available->b)
            {
                const StateNode* description = node.find("description");
                return mouseError(s, description ? description->s : "bus not available");
            }
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, node));
            return out;
        };
        lua.set_function("bus_list", [this, busReply](sol::this_state s) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            return busReply(s, DeviceState::BusList(emulator->GetContext()));
        });
        lua.set_function("bus_events", [this, busOption, busReply](sol::this_state s, const std::string& bus,
                                                                   sol::optional<sol::table> options) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            BusEventQuery query;
            std::string error;
            if (!BusEventQueryFromStrings(busOption(options, "kinds"), busOption(options, "slot"), busOption(options, "since"),
                                          busOption(options, "from"), busOption(options, "to"), busOption(options, "limit"), query, error))
                return mouseError(s, error);
            return busReply(s, DeviceState::BusEvents(emulator->GetContext(), bus, query));
        });
        lua.set_function("bus_findings", [this, busOption, busReply](sol::this_state s, const std::string& bus,
                                                                     sol::optional<sol::table> options) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            BusFindingQuery query;
            std::string error;
            if (!BusFindingQueryFromStrings(busOption(options, "rules"), busOption(options, "since"), busOption(options, "limit"), query, error))
                return mouseError(s, error);
            return busReply(s, DeviceState::BusFindings(emulator->GetContext(), bus, query));
        });
        lua.set_function("bus_control", [this, busOption, busSwitch, busReply](sol::this_state s, const std::string& bus,
                                                                               sol::optional<sol::table> options) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            const std::string capacity = busOption(options, "capacity");
            return busReply(s, DeviceState::BusControl(emulator->GetContext(), bus, busSwitch(options, "enabled"), busSwitch(options, "checks"),
                                                       busSwitch(options, "clear") == 1,
                                                       capacity.empty() ? 0 : static_cast<size_t>(std::stoull(capacity)),
                                                       busSwitch(options, "protocol_only")));
        });
        lua.set_function("bus_sd_timing", [this, busOption, busReply](sol::this_state s, const std::string& bus,
                                                                      sol::optional<sol::table> options) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            const std::string slot = busOption(options, "slot");
            const std::string seed = busOption(options, "seed");
            return busReply(s, DeviceState::BusSdTiming(emulator->GetContext(), bus, slot.empty() ? -1 : std::stoi(slot),
                                                        busOption(options, "profile"), seed.empty() ? 0 : std::stoll(seed)));
        });
        lua.set_function("bus_export", [this, busOption](sol::this_state s, const std::string& bus,
                                                         sol::optional<sol::table> options) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            BusEventQuery query;
            std::string error;
            const std::string limit = busOption(options, "limit");
            if (!BusEventQueryFromStrings(busOption(options, "kinds"), busOption(options, "slot"), busOption(options, "since"),
                                          busOption(options, "from"), busOption(options, "to"), limit.empty() ? "0" : limit, query, error))
                return mouseError(s, error);
            const std::string format = busOption(options, "format");
            const std::string text = DeviceState::BusExport(emulator->GetContext(), bus, format.empty() ? "vcd" : format, query, error);
            if (!error.empty())
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(sol::make_object(s, text));
            return out;
        });
        // next_divmmc([{kinds="map_in,esx_call", since=N, from=F, to=F, limit=N}]): the Next's DivMMC, its automap entries and the
        // DivMMC journal (DeviceState::NextDivMmcReport); nil + error on a bad option. The journal is off by default: next_divmmc_journal_control({enabled=true}) first
        lua.set_function("next_divmmc", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
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
            NextDivMmcJournalQuery query;
            std::string error;
            if (!NextDivMmcJournalQueryFromStrings(text("kinds"), text("since"), text("from"), text("to"), text("limit"), query,
                                               error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, DeviceState::NextDivMmcReport(emulator->GetContext(), query)));
            return out;
        });
        // next_divmmc_journal_control({enabled=true|false, clear=true, capacity=N}): switch / clear / resize the DivMMC journal
        lua.set_function("next_divmmc_journal_control", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            int enable = -1;
            bool clear = false;
            size_t capacity = 0;
            if (options)
            {
                sol::object e = (*options)["enabled"];
                if (e.get_type() == sol::type::boolean)
                    enable = e.as<bool>() ? 1 : 0;
                sol::object c = (*options)["clear"];
                if (c.get_type() == sol::type::boolean)
                    clear = c.as<bool>();
                sol::object n = (*options)["capacity"];
                if (n.get_type() == sol::type::number)
                    capacity = static_cast<size_t>(n.as<long long>());
            }
            return StateNodeToLua(s, DeviceState::NextDivMmcJournalControl(emulator->GetContext(), enable, clear, capacity));
        });
        // next_audio() / next_input(): the Next's sound as configured (chips, pan, registers, DAC) and its joystick modes with the Kempston port
        // (DeviceState::NextAudioReport / NextInputReport; no side effect on the machine)
        lua.set_function("next_audio", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::NextAudioReport(emulator->GetContext()));
        });
        lua.set_function("next_input", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::NextInputReport(emulator->GetContext()));
        });
        // next_uart(): the Next's two UARTs (ESP, Pi) - baud, frame, FIFOs, status, interrupts, peers (DeviceState::NextUartReport; no side effect)
        lua.set_function("next_uart", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::NextUartReport(emulator->GetContext()));
        });
        // next_nmi([{kinds="int_ack,nmi_request", since=N, from=F, to=F, limit=N}]): the Next's NMI sources, Multiface, interrupts, reset and the
        // interrupt / NMI / reset journal (DeviceState::NextNmiReport); nil + error on a bad option. The journal is off by default:
        // next_nmi_journal_control({enabled=true}) first
        lua.set_function("next_nmi", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
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
            NextIntJournalQuery query;
            std::string error;
            if (!NextIntJournalQueryFromStrings(text("kinds"), text("since"), text("from"), text("to"), text("limit"), query, error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, DeviceState::NextNmiReport(emulator->GetContext(), query)));
            return out;
        });
        // next_nmi_journal_control({enabled=true|false, clear=true, capacity=N}): switch / clear / resize the interrupt / NMI / reset journal
        lua.set_function("next_nmi_journal_control", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            int enable = -1;
            bool clear = false;
            size_t capacity = 0;
            if (options)
            {
                sol::object e = (*options)["enabled"];
                if (e.get_type() == sol::type::boolean)
                    enable = e.as<bool>() ? 1 : 0;
                sol::object c = (*options)["clear"];
                if (c.get_type() == sol::type::boolean)
                    clear = c.as<bool>();
                sol::object n = (*options)["capacity"];
                if (n.get_type() == sol::type::number)
                    capacity = static_cast<size_t>(n.as<long long>());
            }
            return StateNodeToLua(s, DeviceState::NextNmiJournalControl(emulator->GetContext(), enable, clear, capacity));
        });
        // next_magic_port([{since=N, limit=N}]): the magic debug port - its setting and the text a program wrote to it (DeviceState::NextMagicPortReport)
        lua.set_function("next_magic_port", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            uint64_t since = 0;
            size_t limit = 200;
            if (options)
            {
                sol::object a = (*options)["since"];
                if (a.get_type() == sol::type::number)
                    since = static_cast<uint64_t>(a.as<long long>());
                sol::object b = (*options)["limit"];
                if (b.get_type() == sol::type::number)
                    limit = static_cast<size_t>(b.as<long long>());
            }
            return StateNodeToLua(s, DeviceState::NextMagicPortReport(emulator->GetContext(), since, limit));
        });
        // next_magic_port_control({enabled=true|false, port=N, mode="hex|dec|ascii|line", clear=true}): switch the magic port, choose its port and mode
        lua.set_function("next_magic_port_control", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            int enable = -1;
            int port = -1;
            bool clear = false;
            std::string mode;
            if (options)
            {
                sol::object e = (*options)["enabled"];
                if (e.get_type() == sol::type::boolean)
                    enable = e.as<bool>() ? 1 : 0;
                sol::object p = (*options)["port"];
                if (p.get_type() == sol::type::number)
                    port = static_cast<int>(p.as<long long>() & 0xFFFF);
                sol::object m = (*options)["mode"];
                if (m.get_type() == sol::type::string)
                    mode = m.as<std::string>();
                sol::object c = (*options)["clear"];
                if (c.get_type() == sol::type::boolean)
                    clear = c.as<bool>();
            }
            return StateNodeToLua(s, DeviceState::NextMagicPortControl(emulator->GetContext(), enable, port, mode, clear));
        });
        // next_dma() / next_video(): the Next's DMA and video layers (DeviceState::NextDma / NextVideo; no side effect on the machine)
        lua.set_function("next_dma", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::NextDma(emulator->GetContext()));
        });
        lua.set_function("next_video", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::NextVideo(emulator->GetContext()));
        });
        // next_palette([{palette="sprites_1"|0-7|"all", range="0-15"}]): the 9-bit palettes (DeviceState::NextPalette); nil + error on a bad option
        lua.set_function("next_palette", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            auto text = [&](const char* key) -> std::string {
                if (!options) return std::string();
                sol::object value = (*options)[key];
                if (value.get_type() == sol::type::number) return std::to_string(value.as<long long>());
                if (value.get_type() == sol::type::string) return value.as<std::string>();
                return std::string();
            };
            NextPaletteQuery query;
            std::string error;
            if (!NextPaletteQueryFromStrings(text("palette"), text("range"), query, error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, DeviceState::NextPalette(emulator->GetContext(), query)));
            return out;
        });
        // next_ports([port [, "r"|"w"]]): the internal port enable word and which device answers `port` (a number is the port, a string is hex text)
        lua.set_function("next_ports", [this](sol::this_state s, sol::optional<sol::object> port, sol::optional<std::string> access) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            std::string portText;
            if (port && port->get_type() == sol::type::number)
                portText = StringHelper::Format("%llx", static_cast<unsigned long long>(port->as<long long>()));
            else if (port && port->get_type() == sol::type::string)
                portText = port->as<std::string>();
            NextPortsQuery query;
            std::string error;
            if (!NextPortsQueryFromStrings(portText, access.value_or(""), query, error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, DeviceState::NextPorts(emulator->GetContext(), query)));
            return out;
        });
        // next_nextreg([reg [, changed]]): one NextREG with decoded bits, or all of them, without side effects (a number is the register)
        lua.set_function("next_nextreg", [this](sol::this_state s, sol::optional<sol::object> reg, sol::optional<bool> changed) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            std::string regText;
            if (reg && reg->get_type() == sol::type::number)
                regText = StringHelper::Format("%llx", static_cast<unsigned long long>(reg->as<long long>()));
            else if (reg && reg->get_type() == sol::type::string)
                regText = reg->as<std::string>();
            NextRegReadQuery query;
            std::string error;
            if (!NextRegReadQueryFromStrings(regText, changed.value_or(false) ? "true" : "", query, error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, DeviceState::NextRegRead(emulator->GetContext(), query)));
            return out;
        });
        // next_copper([{from=0, count=64, raw=true}]): the copper list as a disassembly with the control mode, write address and pc
        // (DeviceState::NextCopper); next_sprites([{from=0, count=128, all=true}]): the 128 sprite attributes decoded, switches, flags,
        // pattern memory summary (DeviceState::NextSprites); nil + error on a bad option
        lua.set_function("next_copper", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            auto text = [&](const char* key) -> std::string {
                if (!options) return std::string();
                sol::object value = (*options)[key];
                if (value.get_type() == sol::type::number) return std::to_string(value.as<long long>());
                if (value.get_type() == sol::type::string) return value.as<std::string>();
                if (value.get_type() == sol::type::boolean) return value.as<bool>() ? "true" : "false";
                return std::string();
            };
            NextCopperQuery query;
            std::string error;
            if (!NextCopperQueryFromStrings(text("from"), text("count"), text("raw"), query, error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, DeviceState::NextCopper(emulator->GetContext(), query)));
            return out;
        });
        lua.set_function("next_sprites", [this](sol::this_state s, sol::optional<sol::table> options) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            auto text = [&](const char* key) -> std::string {
                if (!options) return std::string();
                sol::object value = (*options)[key];
                if (value.get_type() == sol::type::number) return std::to_string(value.as<long long>());
                if (value.get_type() == sol::type::string) return value.as<std::string>();
                if (value.get_type() == sol::type::boolean) return value.as<bool>() ? "true" : "false";
                return std::string();
            };
            NextSpritesQuery query;
            std::string error;
            if (!NextSpritesQueryFromStrings(text("from"), text("count"), text("all"), query, error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, DeviceState::NextSprites(emulator->GetContext(), query)));
            return out;
        });
        // next_pixel(x, y [, sx]): the pixel inspector of the last captured Next frame (DeviceState::NextPixel; design-layer-capture.md): x 0-319,
        // y 0-255, sx the sub-pixel 0 | 1. Returns {frame, pixel, winner = {layer, id}, colour = {rgb9, rgb}, layer_order, layers = {ula, layer2,
        // tilemap, sprites: {opaque, rgb9, wins, why, source}}}; nil + error on a bad coordinate, on another machine, with the capture off or
        // before the first frame
        lua.set_function("next_pixel", [this](sol::this_state s, int64_t x, int64_t y, sol::optional<int64_t> sx) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            NextPixelQuery query;
            std::string error;
            if (!NextPixelQueryFromStrings(std::to_string(x), std::to_string(y), sx ? std::to_string(*sx) : std::string(), query, error))
                return mouseError(s, error);
            const StateNode report = DeviceState::NextPixel(emulator->GetContext(), query);
            const StateNode* available = report.find("available");
            if (available && available->kind == StateNode::Kind::Bool && !available->b)
                return mouseError(s, "This machine is not a ZX Spectrum Next");
            const StateNode* ok = report.find("ok");
            if (ok && ok->kind == StateNode::Kind::Bool && !ok->b)
                return mouseError(s, report.find("message") ? report.find("message")->s : std::string("no frame"));
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, report));
            return out;
        });
        // next_nextreg_write(reg, value [, door]): write a NextREG through the board's write choke point (NextRegWriteControl); door = "nextreg"
        // (default, the NEXTREG instruction), "port" or "internal"; returns the reply table, or nil + error
        lua.set_function("next_nextreg_write", [this](sol::this_state s, int64_t reg, int64_t value, sol::optional<std::string> door) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            if (reg < 0 || reg > 255 || value < 0 || value > 255)
                return mouseError(s, "reg and value must be 0..255");
            NextRegWriteControl::Door which = NextRegWriteControl::Door::NextReg;
            if (!NextRegWriteControl::ParseDoor(door.value_or(""), which))
                return mouseError(s, "door must be nextreg, port or internal");
            const NextRegWriteControl::Result result =
                NextRegWriteControl::Write(emulator, static_cast<uint8_t>(reg), static_cast<uint8_t>(value), which, "lua");
            if (!result.ok)
                return mouseError(s, result.error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, NextRegWriteControl::ToState(result)));
            return out;
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
        // network_configure{card="zxnetusb", host_access=true, hosts="name=1.2.3.4", remote_access=false} [, opts]
        //   -> the reply table (truthy) | nil, err, reply
        // (NetworkManager::ParseChange keys; remote_access: guest servers listen on 0.0.0.0, off = 127.0.0.1 only).
        // SlotControl verb network (ZX-bus slots, owner decision Q11): a change of the ZX-bus cards is a slot change
        // applied by a restart (opts: replace, dry_run, media as for slots_plug; an interpreter bound to the machine
        // follows the restarted one), the other keys go to the restarted machine; without one they apply in place
        // (status "accepted")
        lua.set_function("network_configure", [this](sol::this_state s, sol::table settings,
                                                     sol::optional<sol::table> opts) -> sol::variadic_results {
            sol::variadic_results out;
            SlotControlRequest request;
            request.verb = "network";
            for (const auto& [key, value] : settings)
            {
                std::string text;
                if (value.get_type() == sol::type::boolean)
                    text = value.as<bool>() ? "on" : "off";
                else if (value.get_type() == sol::type::number)
                    text = std::to_string(value.as<long long>());
                else
                    text = value.as<std::string>();
                request.settings.emplace_back(key.as<std::string>(), text);
            }
            const SlotControlReply reply = slotsCall(request, opts, false);
            sol::object table = StateNodeToLua(s, reply.ToValue());
            if (reply.Ok())
            {
                out.push_back(table);
                return out;
            }
            out.push_back(sol::make_object(s, sol::lua_nil));
            out.push_back(sol::make_object(s, reply.message));
            out.push_back(table);
            return out;
        });

        // CMOS clock: the same report and cell access every interface uses
        // (DeviceState::Rtc, RtcAccess). Cells are numbered as the guest numbers them
        // The execution arbiter: intent and who set it, the command holding the CPU, queue, claim (DeviceState::Execution)
        lua.set_function("execution_state", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::Execution(emulator->GetContext()));
        });
        lua.set_function("rtc_state", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::Rtc(emulator->GetContext()));
        });
        // ZX Profi board chips: port map, 8255, 8253, 8251 (DeviceState::ProfiPeripherals); available = false elsewhere
        lua.set_function("profi_state", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::ProfiPeripherals(emulator->GetContext()));
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

        // ISA slots (Sprinter ISA tdd §10): the report and cycles every interface uses (DeviceState::Isa,
        // IsaAccess). Addresses are 20-bit ISA addresses (a number, or "#30A" / "0x30A" text)
        lua.set_function("isa_state", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::Isa(emulator->GetContext()));
        });
        // Ethernet frames of the frame-level cards (EthernetAccess): the capture, a frame towards a card
        lua.set_function("network_frames", [this](sol::this_state s, sol::optional<std::string> link, sol::optional<int> last) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, EthernetAccess::Frames(emulator->GetContext(), link.value_or(""), static_cast<unsigned>(last.value_or(32))));
        });
        // Everything the network adapters sent and received (TrafficAccess, network #91)
        lua.set_function("network_traffic", [this](sol::this_state s, sol::optional<sol::table> q) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            TrafficAccess::Query query;
            if (q)
            {
                query.since = (*q).get_or("since", uint64_t{0});
                query.adapter = (*q).get_or("adapter", std::string());
                query.kind = (*q).get_or("kind", std::string());
                query.last = (*q).get_or("last", 64u);
            }
            return StateNodeToLua(s, TrafficAccess::Records(emulator->GetContext(), query));
        });
        lua.set_function("network_traffic_control", [this](sol::this_state s, const std::string& action, sol::optional<std::string> path,
                                                           sol::optional<uint64_t> value) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            std::string error;
            // value: ring -> its bytes, stream -> the TCP port (0 = any free one)
            if (!TrafficAccess::Control(emulator->GetContext(), action, path.value_or(""), value.value_or(0), error))
                return mouseError(s, error);
            sol::variadic_results r;
            r.push_back(sol::make_object(s, true));
            return r;
        });
        // The host adapters the bridge can use (ethernet_mode=bridge, network SN6)
        lua.set_function("network_adapters", [](sol::this_state s) -> sol::object {
            return StateNodeToLua(s, EthernetAccess::Adapters());
        });
        lua.set_function("network_inject_frame", [this](sol::this_state s, const std::string& link, const std::string& hex) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            std::string error;
            if (!EthernetAccess::Inject(emulator->GetContext(), link, hex, "Lua network_inject_frame", error))
                return mouseError(s, error);
            sol::variadic_results results;
            results.push_back(sol::make_object(s, true));
            return results;
        });
        lua.set_function("isa_journal", [this](sol::this_state s, sol::optional<int> last) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, DeviceState::IsaJournal(emulator->GetContext(), static_cast<unsigned>(last.value_or(64))));
        });
        auto isaCycle = [this](sol::this_state s, const std::string& action, int slot, sol::object address,
                               int value) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            uint32_t addr = 0;
            if (address.is<int>())
                addr = static_cast<uint32_t>(address.as<int>());
            else if (!address.is<std::string>() || !IsaAccess::ParseAddress(address.as<std::string>(), addr))
                return mouseError(s, "address: a number or \"#..\" / \"0x..\" text");
            StateNode result;
            std::string error;
            if (!IsaAccess::Execute(emulator->GetContext(), action, slot, addr, value, "Lua isa", result, error))
                return mouseError(s, error);
            sol::variadic_results results;
            const StateNode* v = result.find("value");
            if (v && (action == "io_read" || action == "mem_read" || action == "io_peek" || action == "mem_peek"))
                results.push_back(sol::make_object(s, static_cast<int>(std::strtol(v->s.c_str() + 1, nullptr, 16))));
            else
                results.push_back(sol::make_object(s, true));
            return results;
        };
        lua.set_function("isa_io_read", [isaCycle](sol::this_state s, int slot, sol::object address) {
            return isaCycle(s, "io_read", slot, address, -1);
        });
        lua.set_function("isa_io_write", [isaCycle](sol::this_state s, int slot, sol::object address, int value) {
            return isaCycle(s, "io_write", slot, address, value);
        });
        lua.set_function("isa_io_peek", [isaCycle](sol::this_state s, int slot, sol::object address) {
            return isaCycle(s, "io_peek", slot, address, -1);
        });
        lua.set_function("isa_mem_read", [isaCycle](sol::this_state s, int slot, sol::object address) {
            return isaCycle(s, "mem_read", slot, address, -1);
        });
        lua.set_function("isa_mem_write", [isaCycle](sol::this_state s, int slot, sol::object address, int value) {
            return isaCycle(s, "mem_write", slot, address, value);
        });
        lua.set_function("isa_reset", [isaCycle](sol::this_state s) {
            return isaCycle(s, "reset", 0, sol::make_object(s, 0), -1);
        });
        lua.set_function("isa_latch", [isaCycle](sol::this_state s, int value) {
            return isaCycle(s, "latch", 0, sol::make_object(s, 0), value);
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
        // ZX-MultiSound (DeviceState::MultiSound), its MIDI synthesizer (DeviceState::Midi) and the MIDI panic
        // (MidiControl: every voice stops, a TTD live input); unavailable / (false, reason) without the card
        lua.set_function("multisound_state", [this](sol::this_state s) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::MultiSound(ctx));
        });
        lua.set_function("midi_state", [this](sol::this_state s) -> sol::object {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::Midi(ctx));
        });
        lua.set_function("midi_panic", [this]() -> std::tuple<bool, std::string> {
            EmulatorContext* ctx = effectiveEmulator() ? effectiveEmulator()->GetContext() : nullptr;
            if (!ctx)
                return {false, "no emulator selected"};
            const MidiControlReply reply = MidiControl::Execute(ctx, "panic");
            return {reply.ok, reply.message};
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
        // ZX-Evo flash ROM (TS-Conf, ATM3): the saved flash (evoflashrequest.h). rom_flash_state() is nil on other
        // machines; save / discard return true when accepted (discard: the shipped ROM returns at the next reset)
        lua.set_function("rom_flash_state", [this](sol::this_state s) -> sol::object {
            EvoFlash::PersistStatus st;
            if (!_emulator || !EvoFlashGetStatus(_emulator->GetContext(), st))
                return sol::make_object(s, sol::lua_nil);
            sol::state_view lv(s);
            sol::table t = lv.create_table();
            t["machine"] = st.machine;
            t["file"] = st.path;
            t["file_exists"] = st.fileExists;
            t["base_rom_sha256"] = st.baseDigest;
            t["loaded_from_file"] = st.loadedFromFile;
            t["unsaved"] = st.unsaved;
            t["changes"] = st.changes;
            sol::table others = lv.create_table();
            for (size_t i = 0; i < st.otherImageFiles.size(); ++i)
                others[i + 1] = st.otherImageFiles[i];
            t["other_image_files"] = others;
            return t;
        });
        lua.set_function("rom_flash_save", [this]() -> bool {
            return _emulator && EvoFlashAccepted(EvoFlashRequestSave(_emulator->GetContext()));
        });
        lua.set_function("rom_flash_discard", [this]() -> bool {
            return _emulator && EvoFlashAccepted(EvoFlashRequestDiscard(_emulator->GetContext()));
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
            if (!ctx || !ctx->pTimeTravelHooks) return false;
            if (!ctx->pSoundManager || !ctx->pSoundManager->getGeneralSound()) return false;
            ttd::TTDInputEvent ev;
            ev.kind = kind;
            ev.value = static_cast<uint8_t>(value);
            return ctx->pTimeTravelHooks->SubmitLiveInput(ev);
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

        // The General Sound personality: the card in the GS slot replaced as a slot change, applied by a machine
        // restart (ZX-bus slots, owner decision Q10) - the same as slots_gs(card). Returns (ok, message); the reply
        // table of slots_gs has the plan and the new emulator id
        lua.set_function("gs_switch_personality", [this](const std::string& personality,
                                                          sol::optional<sol::table> opts) -> std::tuple<bool, std::string> {
            SlotControlRequest request;
            request.verb = "gs";
            request.card = personality;
            const SlotControlReply reply = slotsCall(request, opts, false);
            return {reply.Ok(), reply.message};
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

        // A debugger's write into the sector's data field (SectorWrite); sector 0-based as disk_read_sector (ID - 1);
        // data a string of bytes or a table -> true | nil, error
        lua.set_function("disk_write_sector", [this](sol::this_state s, int drive, int cyl, int side, int sector,
                                                     sol::object data, sol::optional<uint32_t> offset)
                                                  -> std::tuple<sol::object, sol::object> {
            auto fail = [&](const std::string& error) -> std::tuple<sol::object, sol::object> {
                return {sol::make_object(s, sol::lua_nil), sol::make_object(s, error)};
            };
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return fail("no emulator");
            if (drive < 0 || drive > 3)
                return fail("bad drive (0-3)");
            std::vector<uint8_t> bytes;
            if (data.is<std::string>())
            {
                const std::string text = data.as<std::string>();
                bytes.assign(text.begin(), text.end());
            }
            else if (data.is<sol::table>())
            {
                for (const auto& kv : data.as<sol::table>())
                {
                    if (!kv.second.is<int>() || kv.second.as<int>() < 0 || kv.second.as<int>() > 255)
                        return fail("data table must hold bytes 0..255");
                    bytes.push_back(static_cast<uint8_t>(kv.second.as<int>()));
                }
            }
            else
                return fail("data must be a string of bytes or a table");
            const SectorWrite::Result result = SectorWrite::Write(emulator, static_cast<uint8_t>(drive), cyl, side,
                                                                  sector + 1, offset.value_or(0), bytes, "lua");
            if (!result.ok)
                return fail(result.error);
            return {sol::make_object(s, true), sol::make_object(s, sol::lua_nil)};
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

        lua.set_function("ttd_status", [this](sol::this_state ts) -> sol::object {
            const ttd::TTDReply reply = TtdRun("status");
            return StateNodeToLua(ts, reply.Ok() ? reply.body : ttd::TTDControl::StatusBody(nullptr));
        });

        // ttd_file_info(path) - a .ttd file's header, sections and recorded machine,
        // read without loading it: {ok, error | path, file_bytes, ..., machine, sections}
        lua.set_function("ttd_file_info", [this](sol::this_state ts, const std::string& path) -> sol::object {
            return StateNodeToLua(ts, TtdScriptValue(ttd::TTDControl(nullptr).Execute({"file-info", {{"path", path}}})));
        });

        // ttd_start([journal], [black_box], [minutes]) - start recording; journal = true also records the
        // write journal. Without it the ttd_set_journal_enabled choice stands (off by default, D40).
        // black_box = true records as the black box: the last `minutes` (default 5), turbo and host speed free
        lua.set_function("ttd_start", [this](sol::optional<bool> journalOpt, sol::optional<bool> blackBoxOpt,
                                             sol::optional<uint32_t> minutesOpt) -> bool {
            std::map<std::string, std::string> options;
            if (journalOpt.has_value())
                options["journal"] = journalOpt.value() ? "true" : "false";
            if (blackBoxOpt.has_value())
                options["black_box"] = blackBoxOpt.value() ? "true" : "false";
            if (minutesOpt.has_value())
                options["minutes"] = std::to_string(minutesOpt.value());
            const ttd::TTDReply reply = TtdRun("start", options);
            return reply.Ok() && (reply.body.find("started")->b || reply.body.find("already_active")->b);
        });

        // ttd_set_history_limit(frames, bytes) - bound the recorded history: while
        // recording, the oldest frames are released beyond either limit (0 = none;
        // nil keeps the current value). Returns the limit now in force: frames, bytes
        lua.set_function("ttd_set_history_limit",
                         [this](sol::optional<uint64_t> frames, sol::optional<uint64_t> bytes) -> std::tuple<uint64_t, uint64_t> {
            std::map<std::string, std::string> options;
            if (frames.has_value())
                options["frames"] = std::to_string(frames.value());
            if (bytes.has_value())
                options["bytes"] = std::to_string(bytes.value());
            const ttd::TTDReply reply = TtdRun("history-limit", options);
            if (!reply.Ok())
                return {0, 0};
            return {static_cast<uint64_t>(reply.body.find("history_limit_frames")->i),
                    static_cast<uint64_t>(reply.body.find("history_limit_bytes")->i)};
        });

        // ttd_set_journal_enabled(bool) - switch the write journal at any moment,
        // also while recording: a journal segment starts or ends there (D40). ok, reason
        lua.set_function("ttd_set_journal_enabled", [this](bool enabled) -> std::tuple<bool, std::string> {
            const ttd::TTDReply reply = TtdRun("journal", {{"enabled", enabled ? "true" : "false"}});
            return {reply.Ok(), reply.message};
        });

        // ttd_build_journal([from_frame], [to_frame]) - build the write journal for
        // frames from..to (default: the whole session) by replaying them, about
        // 2-4 ms per frame; not while recording. Returns a table: ok, error,
        // cancelled, frames_built, frames_covered, frames_refused, records
        lua.set_function("ttd_build_journal", [this](sol::this_state ts, sol::optional<uint64_t> fromOpt,
                                                    sol::optional<uint64_t> toOpt) -> sol::object {
            std::map<std::string, std::string> options;
            if (fromOpt.has_value())
                options["from_frame"] = std::to_string(fromOpt.value());
            if (toOpt.has_value())
                options["to_frame"] = std::to_string(toOpt.value());
            return StateNodeToLua(ts, TtdScriptValue(TtdRun("journal-build", options)));
        });

        // ttd_cancel_journal_build() -> {cancelled}: stops a write-journal build that runs (another thread's call)
        lua.set_function("ttd_cancel_journal_build", [this](sol::this_state ts) -> sol::object {
            return StateNodeToLua(ts, TtdScriptValue(TtdRun("journal-build-cancel")));
        });

        lua.set_function("ttd_get_journal_enabled", [this]() -> bool {
            const ttd::TTDReply reply = TtdRun("journal");
            return reply.Ok() && reply.body.find("write_journal_setting")->b;
        });

        lua.set_function("ttd_stop", [this]() { (void)TtdRun("stop"); });

        // ok, reason: refused while recording (stop the recording first)
        lua.set_function("ttd_invalidate", [this](sol::optional<std::string> reason) -> std::tuple<bool, std::string> {
            const ttd::TTDReply reply = TtdRun("invalidate", {{"reason", reason.value_or("lua invalidate")}});
            return {reply.Ok(), reply.message};
        });

        lua.set_function("ttd_seek", [this](sol::this_state ts, uint64_t frame, sol::optional<uint32_t> tInFrameOpt) -> sol::object {
            // Without a T-state: the frame's end on the engine (D13), its start on v1
            std::map<std::string, std::string> options{{"frame", std::to_string(frame)}};
            if (tInFrameOpt)
                options["tinframe"] = std::to_string(*tInFrameOpt);
            return TtdSeekValue(ts, TtdRun("seek", options));
        });

        lua.set_function("ttd_step_back", [this]() -> bool {
            const ttd::TTDReply reply = TtdRun("step-back");
            return reply.Ok() && reply.body.find("stepped")->b;
        });

        lua.set_function("ttd_step_forward", [this]() -> bool {
            const ttd::TTDReply reply = TtdRun("step-forward");
            return reply.Ok() && reply.body.find("stepped")->b;
        });

        lua.set_function("ttd_resume", [this](sol::optional<uint64_t> frameOpt, sol::optional<uint32_t> tInFrameOpt) -> bool {
            // No frame: resume exactly where the machine stands
            std::map<std::string, std::string> options;
            if (frameOpt)
            {
                options["frame"] = std::to_string(*frameOpt);
                options["tinframe"] = std::to_string(tInFrameOpt.value_or(0));
            }
            const ttd::TTDReply reply = TtdRun("resume", options);
            return reply.Ok() && reply.body.find("resumed")->b;
        });

        lua.set_function("ttd_position", [this](sol::this_state ts) -> sol::object {
            return StateNodeToLua(ts, TtdScriptValue(TtdRun("position")));
        });

        lua.set_function("ttd_markers", [this](sol::this_state ts) -> sol::object {
            const ttd::TTDReply reply = TtdRun("markers");
            return StateNodeToLua(ts, reply.Ok() ? *reply.body.find("markers") : StateNode::Array());
        });

        // -----------------------------------------------------------------
        // TD-4 — agent bookmarks (advisory annotations, never barriers).
        // Labels are keys: non-empty, at most 63 chars, unique per session.
        // -----------------------------------------------------------------

        lua.set_function("ttd_bookmark_add", [this](sol::this_state ts, const std::string& label,
                                                     sol::optional<uint64_t> frameOpt,
                                                     sol::optional<uint32_t> tInFrameOpt) -> sol::object {
            // Position omitted: the current position (mark here)
            std::map<std::string, std::string> options{{"label", label}};
            if (frameOpt)
            {
                options["frame"] = std::to_string(*frameOpt);
                options["tinframe"] = std::to_string(tInFrameOpt.value_or(0));
            }
            const ttd::TTDReply reply = TtdRun("bookmark-add", options);
            StateNode value = reply.body;
            if (!reply.Ok())
            {
                value["added"] = false;
                value["error"] = reply.message;
            }
            return StateNodeToLua(ts, value);
        });

        lua.set_function("ttd_bookmarks", [this](sol::this_state ts) -> sol::object {
            const ttd::TTDReply reply = TtdRun("bookmarks");
            return StateNodeToLua(ts, reply.Ok() ? *reply.body.find("bookmarks") : StateNode::Array());
        });

        lua.set_function("ttd_bookmark_delete", [this](const std::string& label) -> bool {
            return TtdRun("bookmark-delete", {{"label", label}}).Ok();
        });

        // A bookmark seek IS a seek — identical result shape to ttd_seek
        // (plus the resolved label), so a real barrier between the restore
        // checkpoint and the target still surfaces as halt_reason
        // "external_event". A bookmark itself never halts anything.
        lua.set_function("ttd_seek_bookmark", [this](sol::this_state ts, const std::string& label) -> sol::object {
            return TtdSeekValue(ts, TtdRun("seek", {{"bookmark", label}}));
        });

        // -----------------------------------------------------------------
        // Phase 4 — Reverse search + dump + instruction step
        // -----------------------------------------------------------------

        lua.set_function("ttd_dump", [this](const std::string& path) -> bool {
            return TtdRun("dump", {{"path", path}}).Ok();
        });

        // Loading refuses a session recorded on a different machine model: a
        // checkpoint is raw RAM pages plus a chipset snapshot, so it only
        // restores into an instance of the model it came from. Returns a table
        // with ok/error so scripts can report the reason.
        lua.set_function("ttd_load", [this](sol::this_state ts, const std::string& path) -> sol::object {
            return StateNodeToLua(ts, TtdScriptValue(TtdRun("load", {{"path", path}})));
        });

        // ttd_export_clip(from_frame, to_frame, dir, [chunk]) - frames from..to as a lossless
        // clip in dir (final picture, plane B when zxdlss is on, frame meta), one call instead
        // of a seek and a capture per frame; not while recording. {ok, frames, bytes, planeb,
        // width, height, seconds, path} or {ok = false, error}
        lua.set_function("ttd_export_clip", [this](sol::this_state ts, uint64_t fromFrame, uint64_t toFrame,
                                                    const std::string& dir, sol::optional<uint32_t> chunkOpt) -> sol::object {
            std::map<std::string, std::string> options{
                {"from", std::to_string(fromFrame)}, {"to", std::to_string(toFrame)}, {"path", dir}};
            if (chunkOpt)
                options["chunk"] = std::to_string(*chunkOpt);
            return StateNodeToLua(ts, TtdScriptValue(TtdRun("export-clip", options)));
        });

        // "When did the program ...": ttd_port_events(event, [arg], [options])
        // over the session's port journals (ttdportsearch.h). event: "key",
        // "ear", "ay-read", "ay-write", "ay-select", "border", "beeper", "in",
        // "out"; arg: a key name or an AY register; options: a table of
        // ttd::ApplyPortQueryOption names (limit, newest, from, to, port,
        // port_mask, value, value_mask, match, trigger, ay_register; file = a
        // .ttd path searched without loading it)
        lua.set_function("ttd_port_events", [this](sol::this_state ts, const std::string& event, sol::object argObj,
                                                     sol::object optionsObj) -> sol::object {
            std::map<std::string, std::string> options{{"event", event}};
            if (argObj.valid() && argObj.get_type() != sol::type::lua_nil)
                options["arg"] = TtdOptionText(argObj);
            if (optionsObj.is<sol::table>())
                for (const auto& [key, value] : optionsObj.as<sol::table>())
                    options[key.as<std::string>()] = TtdOptionText(value);
            const ttd::TTDReply reply = TtdRun("port-events", options);
            StateNode value = TtdScriptValue(reply);
            if (reply.Ok())
                value["ok"] = true;
            return StateNodeToLua(ts, value);
        });

        lua.set_function("ttd_find_last", [this](sol::this_state ts, sol::object firstArgOpt,
                                                   sol::optional<std::string> accessOpt,
                                                   sol::optional<uint8_t> valueOpt,
                                                   sol::optional<uint16_t> pcFromOpt,
                                                   sol::optional<uint16_t> pcToOpt,
                                                   sol::optional<uint64_t> beforeFrameOpt,
                                                   sol::optional<uint32_t> beforeTinOpt,
                                                   sol::optional<uint32_t> physPageOpt,
                                                   sol::optional<uint16_t> addrFromOpt,
                                                   sol::optional<uint16_t> addrToOpt,
                                                   sol::optional<std::string> spaceOpt) -> sol::object {
            std::map<std::string, std::string> options;
            if (firstArgOpt.is<sol::table>())
            {
                // A table of options; the camelCase spellings are accepted as before
                static const std::map<std::string, std::string> aliases = {
                    {"addrFrom", "addr_from"}, {"addrTo", "addr_to"}, {"pcFrom", "pc_from"},
                    {"pcTo", "pc_to"},         {"physPage", "phys_page"}};
                for (const auto& [key, value] : firstArgOpt.as<sol::table>())
                {
                    const std::string name = key.as<std::string>();
                    const auto alias = aliases.find(name);
                    options[alias == aliases.end() ? name : alias->second] = TtdOptionText(value);
                }
            }
            else
            {
                if (firstArgOpt.get_type() == sol::type::number)
                    options["addr"] = TtdOptionText(firstArgOpt);
                if (addrFromOpt)
                    options["addr_from"] = std::to_string(*addrFromOpt);
                if (addrToOpt)
                    options["addr_to"] = std::to_string(*addrToOpt);
                if (accessOpt)
                    options["access"] = *accessOpt;
                if (valueOpt)
                    options["value"] = std::to_string(*valueOpt);
                if (pcFromOpt)
                    options["pc_from"] = std::to_string(*pcFromOpt);
                if (pcToOpt)
                    options["pc_to"] = std::to_string(*pcToOpt);
                if (physPageOpt)
                    options["phys_page"] = std::to_string(*physPageOpt);
                if (spaceOpt)
                    options["space"] = *spaceOpt;   // "vram" / "cache": the address is an offset in that memory
                if (beforeFrameOpt)
                {
                    options["before_frame"] = std::to_string(*beforeFrameOpt);
                    options["before_tin"] = std::to_string(beforeTinOpt.value_or(0));
                }
            }
            StateNode value = TtdScriptValue(TtdRun("find-last", options));
            if (!value.find("found"))
                value["found"] = false;
            return StateNodeToLua(ts, value);
        });

        lua.set_function("ttd_step_instruction_back", [this]() -> bool {
            const ttd::TTDReply reply = TtdRun("step-instruction", {{"dir", "back"}});
            return reply.Ok() && reply.body.find("stepped")->b;
        });

        lua.set_function("ttd_step_instruction_forward", [this]() -> bool {
            const ttd::TTDReply reply = TtdRun("step-instruction", {{"dir", "forward"}});
            return reply.Ok() && reply.body.find("stepped")->b;
        });

        // -----------------------------------------------------------------
        // Phase 4 — Reverse execution (multi-step)
        // -----------------------------------------------------------------

        lua.set_function("ttd_reverse_step", [this](sol::optional<uint32_t> countOpt) -> bool {
            const ttd::TTDReply reply = TtdRun("reverse-step", {{"count", std::to_string(countOpt.value_or(1))}});
            return reply.Ok() && reply.body.find("reached")->b;
        });

        lua.set_function("ttd_reverse_step_tstates", [this](uint64_t tstates) -> bool {
            const ttd::TTDReply reply = TtdRun("reverse-step", {{"tstates", std::to_string(tstates)}});
            return reply.Ok() && reply.body.find("reached")->b;
        });

        // ttd_reverse_step_scanlines(n): n scanlines of the machine back (lands on an instruction start)
        lua.set_function("ttd_reverse_step_scanlines", [this](uint64_t scanlines) -> bool {
            const ttd::TTDReply reply = TtdRun("reverse-step", {{"scanlines", std::to_string(scanlines)}});
            return reply.Ok() && reply.body.find("reached")->b;
        });

        lua.set_function("ttd_reverse_continue", [this](sol::this_state ts, sol::table pcsTable) -> sol::object {
            std::string pcs;
            for (auto& pair : pcsTable)
                pcs += (pcs.empty() ? "" : ",") + TtdOptionText(pair.second);
            StateNode value = TtdScriptValue(TtdRun("reverse-continue", {{"pcs", pcs}}));
            if (!value.find("matched"))
                value["matched"] = false;
            return StateNodeToLua(ts, value);
        });

        lua.set_function("ttd_coverage_probe", [this](sol::this_state ts, sol::table argsTable) -> sol::object {
            std::map<std::string, std::string> options;
            for (const auto& [key, value] : argsTable)
                options[key.as<std::string>()] = TtdOptionText(value);
            return StateNodeToLua(ts, TtdScriptValue(TtdRun("coverage-probe", options)));
        });

        lua.set_function("ttd_coverage_scan", [this](sol::this_state ts, sol::table argsTable) -> sol::object {
            std::map<std::string, std::string> options;
            for (const auto& [key, value] : argsTable)
                options[key.as<std::string>()] = TtdOptionText(value);
            return StateNodeToLua(ts, TtdScriptValue(TtdRun("coverage-scan", options)));
        });

        lua.set_function("ttd_coverage_summary", [this](sol::this_state ts, sol::table argsTable) -> sol::object {
            std::map<std::string, std::string> options;
            for (const auto& [key, value] : argsTable)
                options[key.as<std::string>()] = TtdOptionText(value);
            return StateNodeToLua(ts, TtdScriptValue(TtdRun("coverage-summary", options)));
        });

        // A memory at a past checkpoint and what changed between two, from the store (no seek):
        // ttd_memory_at{space = "vram", frame = F, offset = 0x4805, length = 16}
        // ttd_memory_diff{space = "ram5", from_frame = F1, to_frame = F2, limit = 100}
        for (const char* verb : {"memory-at", "memory-diff"})
        {
            const std::string name = std::string("ttd_") + (verb[7] == 'a' ? "memory_at" : "memory_diff");
            lua.set_function(name, [this, verb](sol::this_state ts, sol::table argsTable) -> sol::object {
                std::map<std::string, std::string> options;
                for (const auto& [key, value] : argsTable)
                    options[key.as<std::string>()] = TtdOptionText(value);
                return StateNodeToLua(ts, TtdScriptValue(TtdRun(verb, options)));
            });
        }

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
        // mem_find(pattern [, start, end, alignment, max, space, mask]) - MemorySearch: hex text with ?? / A?
        // wildcards or a byte table; space "cpu" (default), "ram" (every RAM page) or a page ("ram5", "rom2")
        lua.set_function("mem_find",
                         [this](sol::this_state s, sol::object patternValue, sol::optional<uint32_t> startOpt,
                                sol::optional<uint32_t> endOpt, sol::optional<unsigned> alignOpt,
                                sol::optional<unsigned> maxOpt, sol::optional<std::string> spaceOpt,
                                sol::optional<std::string> maskOpt) -> sol::object {
            StateNode error = StateNode::Object();
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
            {
                error["error"] = "no emulator";
                return StateNodeToLua(s, error);
            }
            std::vector<uint8_t> bytes;
            const bool table = patternValue.is<sol::table>();
            if (table)
                for (auto& pair : patternValue.as<sol::table>())
                    bytes.push_back(static_cast<uint8_t>(pair.second.as<int>() & 0xFF));
            MemorySearchRequest request;
            std::string message;
            std::string text;
            if (patternValue.get_type() == sol::type::number)
                text = MemorySearch::NumberPattern(static_cast<uint64_t>(patternValue.as<int64_t>()));
            else if (patternValue.is<std::string>())
                text = patternValue.as<std::string>();
            if (!MemorySearch::BuildRequest(text, table ? &bytes : nullptr,
                                            maskOpt.value_or(""), spaceOpt.value_or(""), startOpt.value_or(0),
                                            endOpt.value_or(0xFFFFFFFFu), maxOpt.value_or(64), alignOpt.value_or(1),
                                            request, message))
            {
                error["error"] = message;
                return StateNodeToLua(s, error);
            }
            return StateNodeToLua(s, MemorySearch::ToState(request, MemorySearch::Search(emulator->GetContext(), request)));
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
                {
                    item["latch"] = latchName;
                    // The latch's value now, and decoded (PortDecoder::ReadPagingLatch / DecodePagingLatch)
                    const uint32_t value = PortDecoder::ReadPagingLatch(entry.latch, context->emulatorState);
                    item["latch_value"] = value;
                    sol::table fields = lua.create_table();
                    for (const DecodedLatchField& field :
                         DecodePagingLatch(entry.latch, value, context->config.mem_model, context->config.ramsize))
                    {
                        if (field.isBool)
                            fields[field.key] = field.boolValue;
                        else
                            fields[field.key] = field.intValue;
                    }
                    item["latch_fields"] = fields;
                }

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
                // A write reaches the page (the mapper's view: ROM, TS-Conf W0_WE, ...)
                bank["writable"] = memory.IsWindowWritable(static_cast<uint8_t>(i));
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
        // The operation log (machine state coordinator): reset, snapshot load, media changes, refusals included - the
        // OperationsStatus report every interface returns. since: entries after that seq; limit: the newest N (0: all)
        lua.set_function("operations", [this](sol::this_state s, sol::optional<uint64_t> since,
                                               sol::optional<uint64_t> limit) -> sol::object {
            return StateNodeToLua(s, OperationsStatus::Report(effectiveEmulator(), since.value_or(0),
                                                              static_cast<size_t>(limit.value_or(0))));
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
        // GUI window display settings (GPU, CRT effects / profile, temporal blending): the WindowDisplay report every
        // interface returns. window_display() reads; window_display_set({gpu = true, crt_profile = "basic",
        // blending = true, blending_frames = 3, blending_weights = "exponential", crt = false}) changes any of them the
        // way the View menu does. Headless: {ok = false, error, kind = "no-window"}
        lua.set_function("window_display", [](sol::this_state s) -> sol::object {
            const WindowDisplay::Result r = WindowDisplay::Get();
            return StateNodeToLua(s, r.Ok() ? WindowDisplay::Report(r.settings) : WindowDisplay::ErrorNode(r));
        });
        lua.set_function("window_display_set", [](sol::this_state s, sol::table changes) -> sol::object {
            WindowDisplayChange change;
            WindowDisplay::Result bad;
            bad.status = WindowDisplay::Status::BadParameter;
            for (const auto& [k, v] : changes)
            {
                const std::string key = k.is<std::string>() ? k.as<std::string>() : std::string("?");
                std::string word;
                // hud_notices = {["ram-page"] = false}, next_layers_shown = {sprites = false}: one key each
                const char* prefix = key == "hud_notices" ? WindowDisplay::kHudNoticePrefix
                                     : key == "next_layers_shown" ? WindowDisplay::kNextLayerPrefix
                                                                  : nullptr;
                if (prefix != nullptr && v.get_type() == sol::type::table)
                {
                    for (const auto& [id, flag] : v.as<sol::table>())
                    {
                        const std::string name = id.is<std::string>() ? id.as<std::string>() : std::string("?");
                        const std::string onOff = flag.get_type() == sol::type::boolean ? (flag.as<bool>() ? "on" : "off")
                                                                                        : std::string("?");
                        if (!WindowDisplay::ApplyWord(change, prefix + name, onOff, bad.error))
                            return StateNodeToLua(s, WindowDisplay::ErrorNode(bad));
                    }
                    continue;
                }
                if (v.get_type() == sol::type::boolean)
                    word = v.as<bool>() ? "on" : "off";
                else if (v.get_type() == sol::type::number)
                    word = WindowDisplay::NumberWord(v.as<double>());  // 0.3 stays 0.3, whatever the locale
                else if (v.get_type() == sol::type::string)
                    word = v.as<std::string>();
                if (word.empty())
                    bad.error = "'" + key + "' takes a boolean, a number or a string";
                if (!bad.error.empty() || !WindowDisplay::ApplyWord(change, key, word, bad.error))
                    return StateNodeToLua(s, WindowDisplay::ErrorNode(bad));
            }
            const WindowDisplay::Result r = WindowDisplay::Set(change);
            return StateNodeToLua(s, r.Ok() ? WindowDisplay::Report(r.settings) : WindowDisplay::ErrorNode(r));
        });
        // The active CRT profile's parameters: window_crt_params() reads {profile, kind, parameters = {{name, value,
        // initial, minimum, maximum, step, description}, ...}}; window_crt_params_set({CRTgamma = 2.4}, {steps =
        // {CRTgamma = -1}, reset = true | "name"}) sets values, moves by steps, resets. Headless: {ok = false, ...}
        lua.set_function("window_crt_params", [](sol::this_state s) -> sol::object {
            const WindowDisplay::ParametersResult r = WindowDisplay::GetCrtParameters();
            return StateNodeToLua(s, r.Ok() ? WindowDisplay::CrtParametersReport(r.parameters) : WindowDisplay::ErrorNode(r));
        });
        lua.set_function("window_crt_params_set", [](sol::this_state s, sol::optional<sol::table> values,
                                                     sol::optional<sol::table> options) -> sol::object {
            CrtParameterChange change;
            WindowDisplay::ParametersResult bad;
            bad.status = WindowDisplay::Status::BadParameter;
            auto numbers = [&](const sol::table& table, std::vector<std::pair<std::string, double>>& out) {
                for (const auto& [k, v] : table)
                {
                    if (!k.is<std::string>() || v.get_type() != sol::type::number)
                    {
                        bad.error = "A parameter takes a number: {name = value}";
                        return false;
                    }
                    out.emplace_back(k.as<std::string>(), v.as<double>());
                }
                return true;
            };
            if (values && !numbers(*values, change.values))
                return StateNodeToLua(s, WindowDisplay::ErrorNode(bad));
            if (options)
            {
                const sol::object steps = (*options)["steps"];
                if (steps.get_type() == sol::type::table && !numbers(steps.as<sol::table>(), change.steps))
                    return StateNodeToLua(s, WindowDisplay::ErrorNode(bad));
                const sol::object reset = (*options)["reset"];
                if (reset.get_type() == sol::type::boolean)
                    change.resetAll = reset.as<bool>();
                else if (reset.get_type() == sol::type::string)
                    change.reset.push_back(reset.as<std::string>());
            }
            const WindowDisplay::ParametersResult r = WindowDisplay::SetCrtParameters(change);
            return StateNodeToLua(s, r.Ok() ? WindowDisplay::CrtParametersReport(r.parameters) : WindowDisplay::ErrorNode(r));
        });
        // The GUI window recording (the picture as drawn, CRT and all, the on-screen keyboard, the machine's sound):
        // window_record(path, {keyboard = "default", codec = "h264", audio = true, scale = 2, picture_height = 1080,
        // width = 3840, height = 2160, layout = "side"}) starts, window_record_frames(n) runs a paused machine n frames
        // with every one recorded, window_record_stop() finishes the file, window_record_status() reads. The
        // WindowRecording report every interface returns; refused: {ok = false, error, kind}
        lua.set_function("window_record", [this](sol::this_state s, const std::string& path,
                                                 sol::optional<sol::table> options) -> sol::object {
            WindowRecording::Result bad;
            bad.status = WindowRecording::Status::BadParameter;
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
            {
                bad.error = "No emulator selected";
                return StateNodeToLua(s, WindowRecording::ErrorNode(bad));
            }
            WindowRecordingRequest request;
            request.path = path;
            if (options)
            {
                for (const auto& [k, v] : *options)
                {
                    const std::string key = k.is<std::string>() ? k.as<std::string>() : std::string("?");
                    std::string word;
                    if (v.get_type() == sol::type::boolean)
                        word = v.as<bool>() ? "on" : "off";
                    else if (v.get_type() == sol::type::number)
                        word = WindowDisplay::NumberWord(v.as<double>());
                    else if (v.get_type() == sol::type::string)
                        word = v.as<std::string>();
                    else
                        bad.error = "'" + key + "' takes a boolean, a number or a string";
                    if (!bad.error.empty() || !WindowRecording::ApplyWord(request, key, word, bad.error))
                        return StateNodeToLua(s, WindowRecording::ErrorNode(bad));
                }
            }
            const WindowRecording::Result r = WindowRecording::Start(emulator->GetId(), request);
            return StateNodeToLua(s, r.Ok() ? WindowRecording::Report(r.recording) : WindowRecording::ErrorNode(r));
        });
        lua.set_function("window_record_frames", [this](sol::this_state s, int frames) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return sol::make_object(s, sol::lua_nil);
            execution::script::Check(*emulator, execution::Origin::Lua);
            const WindowRecording::Result r = WindowRecording::RunFrames(*emulator, frames);
            if (!r.Ok())
                return StateNodeToLua(s, WindowRecording::ErrorNode(r));
            StateNode report = WindowRecording::Report(r.recording);
            report["ran"] = r.ran;
            return StateNodeToLua(s, report);
        });
        lua.set_function("window_record_stop", [](sol::this_state s) -> sol::object {
            const WindowRecording::Result r = WindowRecording::Stop();
            return StateNodeToLua(s, r.Ok() ? WindowRecording::Report(r.recording) : WindowRecording::ErrorNode(r));
        });
        lua.set_function("window_record_status", [](sol::this_state s) -> sol::object {
            const WindowRecording::Result r = WindowRecording::Get();
            return StateNodeToLua(s, r.Ok() ? WindowRecording::Report(r.recording) : WindowRecording::ErrorNode(r));
        });
        // The overscan frame's viewport (the machine's: answers headless too): video_viewport() reads, video_viewport_set
        // ("symmetric" | "full" | "standard" | "screen_only" | "next" | "prev") crops it; {ok = false, error} refused
        lua.set_function("video_viewport", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, VideoViewport::Report(*emulator));
        });
        lua.set_function("video_viewport_set", [this](sol::this_state s, const std::string& name) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
                return sol::make_object(s, sol::lua_nil);
            const VideoViewport::Result r = VideoViewport::SetViewport(*emulator, name);
            if (!r.Ok())
            {
                StateNode error = StateNode::Object();
                error["ok"] = false;
                error["error"] = r.error;
                return StateNodeToLua(s, error);
            }
            return StateNodeToLua(s, VideoViewport::Report(*emulator));
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

        // Profilers, traces and captures through tracecontrol (the catalogue's prof.* actions): trace(name, op, opts)
        // with name opcode | memory | calltrace | coverage | porttrace | gsporttrace | pchist | aylog | audio | all, op
        // start | stop | toggle | pause | resume | pause_toggle | clear | status, opts {keep=, capacity=, seconds=,
        // source=, on=}. The answer {name, state, can_pause, ...}; a refusal: ok = false, error = why
        lua.set_function("trace", [this](sol::this_state ts, const std::string& name, sol::optional<std::string> op,
                                         sol::optional<sol::object> opts) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            tracecontrol::Reply reply;
            if (!emulator)
                reply.error = "Conflict", reply.message = "no emulator";
            else
                reply = tracecontrol::Execute(*emulator, name, op.value_or("status"),
                                              opts ? SymbolOptions(*opts, "") : tracecontrol::Options{});
            StateNode value = reply.body;
            if (!reply.Ok())
            {
                value["ok"] = false;
                value["error"] = reply.message;
            }
            return StateNodeToLua(ts, value);
        });
        // The analysis layers through analysis::Execute (analysiscontrol.h): analysis(action, opts) with action start |
        // stop | status | build | cancel | explain | query | export | gallery | findings | review | experiment | cover; opts a table ({level=, mode=, record=} /
        // {frames=, path=, wait=} / {address=, page=, what=} / {kind=, range=, limit=} / {format=, path=, ranges=,
        // width=} / {address=, kind=, scale=, path=, limit=}) or, for explain and gallery, the address, for start the
        // level, for build the frames, for query the kind, for export the format. The answer: the reply's fields plus
        // text (gallery: the PNG base64 in png); a refusal: ok = false, error = why. L3: findings {op=, kind=, status=,
        // format=, path=}, review {op=, id=, verdict=, label=, title=, comment=, kind=, names=<JSON text>, reason=,
        // offset=, limit=, experiment=<JSON text>} (or the op), experiment {spec=<JSON text>, path=, finding=} (or the
        // spec's JSON text), cover {budget=, frames=, ranges=} (or the budget)
        lua.set_function("analysis", [this](sol::this_state ts, sol::optional<std::string> action,
                                            sol::optional<sol::object> opts) -> sol::object {
            const std::string verb = action.value_or("status");
            Emulator* emulator = effectiveEmulator();
            analysis::Reply reply;
            if (!emulator)
                reply.error = analysis::Error::NotAvailable, reply.message = "no emulator";
            else
                reply = analysis::Execute(*emulator, verb,
                                          opts ? SymbolOptions(*opts, verb == "explain" || verb == "gallery" ? "address"
                                                             : verb == "start" ? "level"
                                                             : verb == "build" ? "frames"
                                                             : verb == "query" ? "kind"
                                                             : verb == "export" ? "format"
                                                             : verb == "findings" || verb == "review" ? "op"
                                                             : verb == "experiment" ? "spec"
                                                             : verb == "cover" ? "budget"
                                                                               : "")
                                               : analysis::Options{});
            StateNode value = reply.ToValue();
            if (!reply.Ok())
            {
                value = StateNode::Object();
                value["ok"] = false;
                value["error"] = reply.message;
            }
            return StateNodeToLua(ts, value);
        });
        // The analyzers (GET /analyzers): analyzer_list(), analyzer_enable(name), analyzer_disable(name),
        // analyzer_active(name) (nil: no such analyzer)
        auto analyzers = [this]() -> AnalyzerManager* {
            Emulator* emulator = effectiveEmulator();
            auto* context = emulator ? emulator->GetContext() : nullptr;
            return context && context->pDebugManager ? context->pDebugManager->GetAnalyzerManager() : nullptr;
        };
        lua.set_function("analyzer_list", [this, analyzers]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (AnalyzerManager* manager = analyzers())
            {
                int index = 1;
                for (const std::string& id : manager->getRegisteredAnalyzers())
                    result[index++] = lua_view.create_table_with("id", id, "active", manager->isActive(id));
            }
            return result;
        });
        lua.set_function("analyzer_enable", [analyzers](const std::string& name) -> bool {
            AnalyzerManager* manager = analyzers();
            return manager && manager->activate(name);
        });
        lua.set_function("analyzer_disable", [analyzers](const std::string& name) -> bool {
            AnalyzerManager* manager = analyzers();
            return manager && manager->deactivate(name);
        });
        lua.set_function("analyzer_active", [analyzers](sol::this_state ts, const std::string& name) -> sol::object {
            AnalyzerManager* manager = analyzers();
            if (!manager || !manager->hasAnalyzer(name))
                return sol::make_object(ts, sol::lua_nil);
            return sol::make_object(ts, manager->isActive(name));
        });

        // OS-aware analyzers (nedoos, dss): analyzer_events(name, limit),
        // analyzer_state(name) - tasks, page/block ownership, file handles
        lua.set_function("analyzer_events", [this, analyzers](const std::string& name,
                                                              sol::optional<unsigned> limitOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            AnalyzerManager* manager = analyzers();
            IOSAnalyzer* osAnalyzer = manager ? dynamic_cast<IOSAnalyzer*>(manager->getAnalyzer(name)) : nullptr;
            if (!osAnalyzer) { result["error"] = "not an OS analyzer"; return result; }

            auto events = osAnalyzer->getOsEvents();
            const size_t limit = limitOpt.value_or(50);
            size_t index = 0;
            size_t start = events.size() > limit ? events.size() - limit : 0;
            for (size_t i = start; i < events.size(); i++)
            {
                sol::table item = lua_view.create_table();
                item["timestamp"] = events[i].timestamp;
                item["frame"] = events[i].frameNumber;
                item["kind"] = OsEventKindName(events[i].kind);
                item["severity"] = OsSeverityName(events[i].severity);
                item["formatted"] = events[i].format();
                item["pc"] = events[i].pc;
                item["task"] = events[i].taskId;
                result[index + 1] = item;
                index++;
            }
            return result;
        });
        lua.set_function("analyzer_state", [this, analyzers](const std::string& name) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            AnalyzerManager* manager = analyzers();
            IOSAnalyzer* osAnalyzer = manager ? dynamic_cast<IOSAnalyzer*>(manager->getAnalyzer(name)) : nullptr;
            if (!osAnalyzer) { result["error"] = "not an OS analyzer"; return result; }

            OsSnapshot snapshot = osAnalyzer->getOsSnapshot();
            result["os"] = snapshot.os;
            result["detected"] = snapshot.detected;
            result["profile"] = snapshot.profile;

            sol::table tasks = lua_view.create_table();
            size_t index = 0;
            for (const auto& task : snapshot.tasks)
            {
                sol::table item = lua_view.create_table();
                item["id"] = task.id;
                item["parent"] = task.parent;
                item["flags"] = task.flags;
                item["mainpg"] = task.mainpg;
                item["name"] = task.name;
                item["stdin"] = task.stdinFd;
                item["stdout"] = task.stdoutFd;
                item["stderr"] = task.stderrFd;
                item["last_time"] = task.lastTime;
                tasks[index + 1] = item;
                index++;
            }
            result["tasks"] = tasks;

            sol::table blocks = lua_view.create_table();
            index = 0;
            for (const auto& block : snapshot.blocks)
            {
                sol::table item = lua_view.create_table();
                item["index"] = block.index;
                item["owner"] = block.owner;
                blocks[index + 1] = item;
                index++;
            }
            result["blocks"] = blocks;

            sol::table handles = lua_view.create_table();
            index = 0;
            for (const auto& handle : snapshot.handles)
            {
                sol::table item = lua_view.create_table();
                item["index"] = handle.index;
                item["owner"] = handle.owner;
                item["name"] = handle.name;
                item["position"] = handle.position;
                item["size"] = handle.size;
                handles[index + 1] = item;
                index++;
            }
            result["handles"] = handles;

            sol::table pipes = lua_view.create_table();
            index = 0;
            for (const auto& pipe : snapshot.pipes)
            {
                sol::table item = lua_view.create_table();
                item["handle"] = pipe.handle;
                item["open_sides"] = pipe.openSides;
                item["reader"] = pipe.reader;
                item["lines"] = pipe.lines;
                item["queued"] = pipe.queued;
                pipes[index + 1] = item;
                index++;
            }
            result["pipes"] = pipes;
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

        // coverage_export([path], [{format=, start=, ["end"]=, merge=}]) or coverage_export({path=, ...}): the coverage
        // as SkoolKit's code execution map (coverageexport.h); without a path the map comes back in result.map
        lua.set_function("coverage_export", [this](sol::object first, sol::object second) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            coverageexport::Options options;
            const auto take = [&options](const sol::table& table) {
                for (const auto& [key, value] : table)
                {
                    if (!key.is<std::string>())
                        continue;
                    const std::string name = key.as<std::string>();
                    if (value.is<bool>())
                        options[name] = value.as<bool>() ? "true" : "false";
                    else if (value.get_type() == sol::type::number)
                        options[name] = std::to_string(value.as<int64_t>());
                    else if (value.is<std::string>())
                        options[name] = value.as<std::string>();
                }
            };
            if (first.is<std::string>())
                options["path"] = first.as<std::string>();
            else if (first.is<sol::table>())
                take(first.as<sol::table>());
            if (second.is<sol::table>())
                take(second.as<sol::table>());

            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }
            auto* context = emulator->GetContext();
            AnalyzerManager* manager = context && context->pDebugManager ?
                                           context->pDebugManager->GetAnalyzerManager() : nullptr;
            const coverageexport::Result r =
                coverageexport::Export(manager ? manager->getAnalyzer<CoverageAnalyzer>("coverage") : nullptr, options);
            if (!r.Ok()) { result["error"] = r.message; return result; }
            result["success"] = true;
            result["format"] = r.format;
            result["start"] = r.start;
            result["end"] = r.end;
            result["count"] = r.count;
            if (r.path.empty())
                result["map"] = r.text;
            else
            {
                result["path"] = r.path;
                result["merged"] = r.merged;
            }
            result["message"] = r.message;
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
        // volume=, gain_db=}) -> the mixer, or nil, error (AudioMixer::Apply; source "master" takes muted only; a
        // channel is "<source>.<channel>": "ay1.a", "gs.3", "moonsound_fm.7")
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
        // audio_mixer_get(source) -> one row ("ay1") or channel ("ay1.a"), or nil, error; audio_levels() -> every
        // row's and channel's levels and mute / solo (the WebSocket topic audio.mixer)
        lua.set_function("audio_mixer_get", [this](sol::this_state s, const std::string& source) -> sol::variadic_results {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return mouseError(s, "No emulator selected");
            StateNode node;
            std::string error;
            if (!AudioMixer::Read(emulator->GetContext(), source, node, error))
                return mouseError(s, error);
            sol::variadic_results out;
            out.push_back(StateNodeToLua(s, node));
            return out;
        });
        lua.set_function("audio_levels", [this](sol::this_state s) -> sol::object {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return sol::make_object(s, sol::lua_nil);
            return StateNodeToLua(s, AudioMixer::Levels(emulator->GetContext()));
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
        lua.set_function("video_record", [this](sol::this_state ts, const std::string& action,
                                                sol::optional<sol::table> optsOpt) -> sol::object {
            sol::state_view lua_view(ts);
            Emulator* emulator = effectiveEmulator();
            if (!emulator)
            {
                sol::table result = lua_view.create_table();
                result["error"] = "no emulator";
                return result;
            }
            // capture::RecordingControl: the presets, modes and option rules every plane shares
            std::map<std::string, std::string> options;
            if (optsOpt.has_value() && optsOpt->valid())
            {
                for (const auto& [key, value] : *optsOpt)
                {
                    if (!key.is<std::string>())
                        continue;
                    const std::string name = key.as<std::string>();
                    if (value.get_type() == sol::type::boolean)
                    {
                        // audio = true / false: aac or none, as before; other flags as text
                        const bool on = value.as<bool>();
                        options[name] = name == "audio" ? (on ? "aac" : "none") : (on ? "true" : "false");
                    }
                    else if (value.get_type() == sol::type::number)
                    {
                        const double number = value.as<double>();
                        options[name] = number == std::floor(number) ? std::to_string(static_cast<long long>(number))
                                                                     : std::to_string(number);
                    }
                    else if (value.get_type() == sol::type::string)
                        options[name] = value.as<std::string>();
                }
            }
            // audio_rate (a number or "auto", as the CLI's --audio-rate): the core rate pinned before the start, so
            // the recording is stamped with it; applied at the next frame boundary
            SoundManager* sound = emulator->GetContext() ? emulator->GetContext()->pSoundManager : nullptr;
            if (auto rate = options.find("audio_rate"); rate != options.end())
            {
                const std::string text = rate->second;
                options.erase(rate);
                if (action == "start" || action == "toggle")
                {
                    uint32_t audioRate = 0;
                    if (text != "auto")
                    {
                        char* end = nullptr;
                        const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
                        if (text.empty() || end == nullptr || *end != '\0')
                        {
                            sol::table result = lua_view.create_table();
                            result["error"] = "audio_rate must be a number or 'auto'";
                            return result;
                        }
                        audioRate = static_cast<uint32_t>(parsed);
                    }
                    if (sound == nullptr)
                    {
                        sol::table result = lua_view.create_table();
                        result["error"] = "sound manager not available";
                        return result;
                    }
                    sound->setCoreRatePin(audioRate);
                    for (int attempt = 0; attempt < 100 && sound->getCoreRate() != sound->getTargetCoreRate(); attempt++)
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    if (sound->getCoreRate() != sound->getTargetCoreRate())
                    {
                        sol::table result = lua_view.create_table();
                        result["error"] = "audio_rate not applied within 1s (emulator paused?) - "
                                          "resume it or call set_audio_rate before pausing";
                        return result;
                    }
                }
            }
            std::error_code ec;
            const std::string folder = (std::filesystem::temp_directory_path(ec) / "unreal-lua").string();
            const capture::RecordingReply reply = capture::RecordingControl(*emulator, folder).Execute(action, options);
            StateNode body = reply.body;
            if (reply.ok && sound != nullptr && (action == "start" || action == "toggle"))
                body["audio_rate"] = static_cast<uint64_t>(sound->getCoreRate());
            if (!reply.ok)
            {
                body["error"] = reply.message.empty() ? reply.error : reply.message;
                body["code"] = reply.error;
            }
            return StateNodeToLua(ts, body);
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
            result["profile"] = rm->GetOutputProfile();
            result["acceleration"] = EncoderAccelerationName(rm->GetEncoderAcceleration());
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
                    emulator->EditMemoryFromToolOrThrow("Lua assemble write", [&] {
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