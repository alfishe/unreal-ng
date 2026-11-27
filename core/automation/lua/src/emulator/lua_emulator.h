#pragma once

#include "common/base64.h"
#include "emulator/state/statenodejson.h"
#include "emulator/memory/devicememory.h"
#include "emulator/execution/companionregistry.h"
#include "emulator/execution/runpacer.h"
#include "emulator/execution/runcontrol.h"
#include "debugger/watch/framewatch.h"
#include "emulator/execution/scriptsession.h"
#include "emulator/preset/machinepreset.h"
#include "emulator/sound/audiomixer.h"
#include "emulator/video/framebufferexport.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/machinecatalog.h"
#include "emulator/ports/models/profiboard.h"
#include "emulator/video/screenshotter.h"
#include "emulator/video/next/nextlayerimage.h"
#include "emulator/ports/models/sprinter/sprinterbios.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/zxpoly/zxpolygroup.h"
#include "debugger/memory/memorycounters.h"
#include "debugger/memory/memoryread.h"
#include "debugger/memory/memorywatch.h"
#include "debugger/media/sectorwrite.h"
#include "debugger/pchistory/pchistory.h"
#include "debugger/timeline/twintimeline.h"
#include "debugger/ports/portwrite.h"
#include "debugger/snapshot/debugsnapshot.h"
#include "debugger/search/memorysearch.h"
#include "debugger/boards/debugboards.h"
#include "debugger/targets/cpuview.h"
#include <algorithm>
#include <functional>
#include <sol/sol.hpp>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/rzx/rzxlauncher.h>
#include <loaders/snapshot/snapshotlauncher.h>
#include <loaders/snapshot/statetransfercontrol.h>
#include "../bindings/lua_porttrace.h"
#include "../bindings/lua_vdac2.h"
#include "../bindings/lua_vdac4.h"
#include <emulator/memory/memory.h>
#include <emulator/memory/memorymap.h>  // TD-3 sparse map + hexdump
#include <emulator/io/fdc/fdd.h>
#include <emulator/io/ide/cdaudiocontrol.h>
#include <emulator/media/floppyformats.h>
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
#include <emulator/sound/chips/gs/firmware/gsfirmwareobjects.h>
#include <emulator/sound/chips/gs/gszxdmatrace.h>
#include <emulator/sound/chips/gs/gsprofilereport.h>
#include <emulator/sound/chips/gs/gsanalysisreport.h>
#include <emulator/sound/chips/gs/gsslotstats.h>
#include <emulator/sound/chips/gs/soundchip_gs.h>
#include "../../../temporalstatus.h"
#include "emulator/video/viewportcontrol.h"
#include "emulator/video/windowrecording.h"
#include "../../../windowdisplay.h"
#include "../../../common/guicall.h"
#include "../../../operationsstatus.h"
#include <debugger/ttd/ttdcontrol.h>
#include <debugger/debugmanager.h>
#include <debugger/keyboard/debugkeyboardmanager.h>
#include <debugger/mouse/debugmousemanager.h>
#include <debugger/joystick/debugjoystickmanager.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/breakpoints/breakpointcondition.h>
#include <debugger/callbacks/scriptcallbacks.h>
#include "../bindings/lua_callbackhit.h"
#include <chrono>
#include <set>
#include <tuple>
#include <debugger/breakpoints/breakpointselection.h>
#include "debugger/tracecontrol/tracecontrol.h"
#include "debugger/analyzers/analysis/analysiscontrol.h"
#include "emulator/jobs/jobcontrol.h"
#include "debugger/markup/markupcontrol.h"
#include "debugger/analyzers/profile/profilecontrol.h"
#include <debugger/disassembler/z80disasm.h>
#include <debugger/labels/labelmanager.h>
#include "debugger/labels/symbolcontrol.h"
#include "debugger/asm/asmcontrol.h"
#include <debugger/ttd/timetravelcontroller.h>
#include <debugger/ttd/machinestatehash.h>
#include <debugger/ttd/ttdfileinfo.h>
#include <tuple>
#include <debugger/ttd/ttdexternalevents.h>
#include <debugger/ttd/ttdprobe.h>
#include <debugger/analyzers/analyzermanager.h>
#include <debugger/analyzers/audiocapture/audiocaptureanalyzer.h>
#include <debugger/analyzers/aylog/ayloganalyzer.h>
#include <debugger/analyzers/sectiontimer/sectiontimeranalyzer.h>
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
#include <emulator/state/devicecatalog.h>
#include <debugger/history/historycontrol.h>
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


/// Lua value -> StateNode: a whole number an Int, a table with keys 1..n an array, another table an object
inline StateNode LuaToStateNode(const sol::object& value)
{
    switch (value.get_type())
    {
        case sol::type::boolean: return StateNode(value.as<bool>());
        case sol::type::number:
        {
            const double number = value.as<double>();
            if (number == std::floor(number) && std::abs(number) < 9.0e15)
                return StateNode(static_cast<int64_t>(number));
            return StateNode(number);
        }
        case sol::type::string: return StateNode(value.as<std::string>());
        case sol::type::table:
        {
            const sol::table table = value.as<sol::table>();
            const size_t length = table.size();
            size_t count = 0;
            for (const auto& entry : table)
            {
                (void)entry;
                count++;
            }
            if (length > 0 && count == length)
            {
                StateNode array = StateNode::Array();
                for (size_t i = 1; i <= length; i++)
                    array.push(LuaToStateNode(table[i]));
                return array;
            }
            StateNode object = StateNode::Object();
            for (const auto& [key, item] : table)
            {
                const std::string name = key.is<std::string>()               ? key.as<std::string>()
                                         : key.get_type() == sol::type::number ? std::to_string(key.as<int64_t>())
                                                                               : std::string("?");
                object[name] = LuaToStateNode(item);
            }
            return object;
        }
        default: return StateNode();
    }
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
    /// Script callbacks (docs/features/script-callbacks.md): the functions by (emulator id, callback id). Lua thread
    std::map<std::pair<std::string, uint32_t>, sol::protected_function> _callbacks;
    std::set<std::string> _hookEmulators;  ///< emulators this script gave a frame or sync callback (chunks)
    /// Wakes the Lua thread when a callback has hits (AutomationLua sets it)
    std::function<void()> _callbackWake;
    static constexpr const char* kCallbackOwner = "lua";
    static constexpr size_t kCallbackDrainBatch = 64;


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
        if (o.get_type() == sol::type::table)
        {
            // A list (tags, watched addresses) as its items joined by commas, in order
            const sol::table t = o.as<sol::table>();
            std::string text;
            for (size_t i = 1; i <= t.size(); ++i)
                text += (i > 1 ? "," : "") + TtdOptionText(t[i]);
            return text;
        }
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

    /// The CPU a register, memory or disassembly call names (none or "main": the main CPU; "gs" / "ms": a sound card's,
    /// cpuview.h): a script error for an unknown name or a card CPU that is not there
    DebugCpuId ScriptCpu(const sol::optional<std::string>& name) const
    {
        DebugCpuId cpu = DebugCpuId::Main;
        Emulator* emulator = effectiveEmulator();
        if (!name || !emulator)
            return cpu;
        const std::string error = CpuView::Select(emulator->GetContext(), *name, cpu);
        if (!error.empty())
            throw std::runtime_error(error);
        return cpu;
    }

    /// A card CPU's step or run (`run`: the RunControl call): the reply's body and ok = true, or ok = false and error.
    /// The machine runs until the card ends it, then is paused
    sol::table CardRun(sol::state_view lua,
                       const std::function<execution::RunReply(execution::RunControl&, const execution::Requester&)>& run) const
    {
        sol::table result = lua.create_table();
        Emulator* emulator = effectiveEmulator();
        std::shared_ptr<Emulator> shared =
            emulator ? EmulatorManager::GetInstance()->GetEmulator(emulator->GetId()) : nullptr;
        if (!shared)
        {
            result["ok"] = false;
            result["error"] = "no emulator";
            return result;
        }
        // The automation gate as every step of a script: its session, or an implicit one
        execution::script::Check(*emulator, execution::Origin::Lua);
        execution::RunControl control(shared);
        const execution::RunReply reply = run(control, execution::script::RequesterOf(*emulator, execution::Origin::Lua));
        if (reply.status == execution::RunReply::Status::Revoked)
            throw std::runtime_error(reply.message);
        if (!reply.Ok())
        {
            result["ok"] = false;
            result["error"] = reply.message;
            return result;
        }
        for (const auto& [key, value] : reply.body.members)
            result[key] = StateNodeToLua(lua.lua_state(), value);
        result["ok"] = true;
        return result;
    }

    /// A sound card CPU's step (RunControl::CardStep; `kind` in, over, out, to `address`)
    sol::table CardStep(sol::state_view lua, DebugCpuId cpu, const char* kind, unsigned count, uint16_t address = 0) const
    {
        return CardRun(lua, [&](execution::RunControl& control, const execution::Requester& requester) {
            return control.CardStep(requester, cpu, kind, count, address);
        });
    }

    /// A table's members as frame watch options (docs/inprogress/2026-11-03-run-until-watches)
    static std::vector<std::pair<std::string, std::string>> WatchOptions(const sol::optional<sol::table>& opts)
    {
        std::vector<std::pair<std::string, std::string>> options;
        if (opts)
            for (const auto& [key, value] : *opts)
                if (key.get_type() == sol::type::string)
                    options.emplace_back(key.as<std::string>(), TtdOptionText(value));
        return options;
    }

    /// run_until{...}: whole frames until the rules hold at a frame end (RunControl::RunUntil); the reply's body with
    /// ok, or ok = false and error
    sol::table RunUntil(sol::state_view lua, const sol::optional<sol::table>& opts) const
    {
        sol::table result = lua.create_table();
        Emulator* emulator = effectiveEmulator();
        std::shared_ptr<Emulator> shared =
            emulator ? EmulatorManager::GetInstance()->GetEmulator(emulator->GetId()) : nullptr;
        framewatch::Spec spec;
        unsigned maxFrames = 0;
        bool breakpoints = false;
        std::string error;
        if (!shared)
            error = "no emulator";
        else if (!framewatch::RunOptions(WatchOptions(opts), spec, maxFrames, breakpoints, error))
            error = error.empty() ? "bad options" : error;
        if (!error.empty())
        {
            result["ok"] = false;
            result["error"] = error;
            return result;
        }
        execution::script::Check(*emulator, execution::Origin::Lua);
        const execution::RunReply reply = execution::RunControl(shared).RunUntil(
            execution::script::RequesterOf(*emulator, execution::Origin::Lua), spec, maxFrames, breakpoints);
        if (reply.status == execution::RunReply::Status::Revoked)
            throw std::runtime_error(reply.message);
        if (!reply.Ok())
        {
            result["ok"] = false;
            result["error"] = reply.message;
            return result;
        }
        for (const auto& [key, value] : reply.body.members)
            result[key] = StateNodeToLua(lua.lua_state(), value);
        result["ok"] = true;
        return result;
    }

    /// A watch verb's outcome as a table: the body with ok (and error on a refusal)
    static sol::table WatchOutcome(sol::state_view lua, const framewatch::Outcome& outcome)
    {
        sol::table result = lua.create_table();
        for (const auto& [key, value] : outcome.body.members)
            result[key] = StateNodeToLua(lua.lua_state(), value);
        result["ok"] = outcome.status < 400;
        if (outcome.status >= 400)
            result["error"] = outcome.text;
        result["text"] = outcome.text;
        return result;
    }

    /// The labels of a CPU by its name (none: the main CPU's; "gs" / "ms": the sound card CPU's own store, gs-debugger
    /// design §6.2); an unknown name or a card CPU the machine does not have throws (ScriptCpu)
    LabelManager* ScriptLabels(const sol::optional<std::string>& cpuName) const
    {
        Emulator* emulator = effectiveEmulator();
        EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
        if (!ctx || !ctx->pDebugManager)
            return nullptr;
        return ctx->pDebugManager->GetLabelManager(ScriptCpu(cpuName));
    }

    /// The breakpoints of a CPU by its name ("main" or none, "gs": the sound card's CPU); null when there is none
    BreakpointManager* ScriptBreakpoints(const std::string& cpuName) const
    {
        Emulator* emulator = effectiveEmulator();
        EmulatorContext* ctx = emulator ? emulator->GetContext() : nullptr;
        DebugCpuId cpu = DebugCpuId::Main;
        if (!ctx || !ctx->pDebugManager || !DebugManager::ParseCpuName(cpuName, cpu))
            return nullptr;
        return ctx->pDebugManager->GetBreakpointsManager(cpu);
    }

    /// bp / bp_read / bp_write / bp_port_in / bp_port_out: `options` is nothing, a page string ("ram32"), or
    /// a table {page=, to=, slot_only=, mask=, hits=, condition=, dialect=, cpu=, stop=, hit_log=, hit_log_keep=}
    /// (stop = false: it only counts, bp_hits reads the count, bp_hit_log the per-hit record of hit_log hits, default
    /// 1000, 0 none; hit_log_keep "first" or "last"). The breakpoint id; -1 and the reason when refused
    std::tuple<int, sol::optional<std::string>> AddScriptBreakpoint(BreakpointTypeEnum type, uint8_t access,
                                                                    uint16_t address,
                                                                    const sol::optional<sol::object>& options)
    {
        // cpu = "gs": the sound card CPU's breakpoint
        std::string cpu;
        if (options && options->is<sol::table>())
            cpu = options->as<sol::table>().get<sol::optional<std::string>>("cpu").value_or("");
        BreakpointManager* bpm = ScriptBreakpoints(cpu);
        if (!bpm)
            return {-1, std::string(effectiveEmulator() ? "cpu must be 'main', 'gs' or 'ms'" : "no emulator")};
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
            spec.stop = t.get<sol::optional<bool>>("stop").value_or(true);
            if (auto log = t.get<sol::optional<int64_t>>("hit_log"))
            {
                if (*log < 0 || *log > BreakpointHitLog::kMaxCapacity)
                    return {-1, "hit_log must be 0.." + std::to_string(BreakpointHitLog::kMaxCapacity)};
                spec.hitLog = static_cast<uint32_t>(*log);
            }
            if (auto keep = t.get<sol::optional<std::string>>("hit_log_keep"))
                if (!BreakpointManager::ParseHitLogKeep(*keep, spec.hitLogKeep))
                    return {-1, std::string("hit_log_keep must be 'first' or 'last'")};
            hits = t.get<sol::optional<std::string>>("hits").value_or("");
            if (hits.empty())
                if (auto n = t.get<sol::optional<int64_t>>("hits"))
                    hits = std::to_string(*n);  // hits=5: the 5th hit
            spec.condition = t.get<sol::optional<std::string>>("condition").value_or("");
            if (!BreakpointConditionProgram::ParseDialect(t.get<sol::optional<std::string>>("dialect").value_or(""),
                                                          spec.conditionDialect))
                return {-1, std::string("dialect must be 'unreal' or 'c'")};
        }
        std::string error;
        if (!BreakpointManager::ApplyScriptOptions(spec, page, to, slotOnly, mask, hits, error))
            return {-1, error};
        const uint16_t id = bpm->AddBreakpoint(spec, error);
        if (id == BRK_INVALID)
            return {-1, error};
        return {static_cast<int>(id), sol::nullopt};
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
        t["port_decoded"] = state.portDecoded;
        t["port_source"] = state.portSource;
        t["kempston_mouse_feature"] = state.mouseFeatureOn;
        t["kempston_mouse_fitted"] = state.mouseFitted;
        t["ports_text"] = DebugJoystickManager::DescribePorts(state);
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
    /// The bindings, registered in this order; each group in a source file of its own (emulator/register/)
    void registerType(sol::state& lua)
    {
        RegisterMachines(lua);
        RegisterScreenAndMedia(lua);
        RegisterDiskAndTape(lua);
        RegisterInputAndBreakpoints(lua);
        RegisterLabelsAndBoards(lua);
        RegisterNetworkAndGs(lua);
        RegisterGsAndTimeTravel(lua);
        RegisterTimeTravelAndWindow(lua);
        RegisterVideoAndAudio(lua);
        RegisterAudioAndScripts(lua);
    }

private:
    void RegisterMachines(sol::state& lua);
    void RegisterScreenAndMedia(sol::state& lua);
    void RegisterDiskAndTape(sol::state& lua);
    void RegisterInputAndBreakpoints(sol::state& lua);
    void RegisterLabelsAndBoards(sol::state& lua);
    void RegisterNetworkAndGs(sol::state& lua);
    void RegisterGsAndTimeTravel(sol::state& lua);
    void RegisterTimeTravelAndWindow(sol::state& lua);
    void RegisterVideoAndAudio(sol::state& lua);
    void RegisterAudioAndScripts(sol::state& lua);

public:
    void SetCallbackWake(std::function<void()> wake)
    {
        _callbackWake = std::move(wake);
    }

    static ScriptCallbackManager* CallbacksOf(Emulator* emulator)
    {
        EmulatorContext* context = emulator ? emulator->GetContext() : nullptr;
        return context && context->pDebugManager ? context->pDebugManager->GetScriptCallbacks() : nullptr;
    }

    /// The callbacks of emulator @p id (the manager's, else the bound one); @p hold keeps it alive
    ScriptCallbackManager* CallbacksById(const std::string& id, std::shared_ptr<Emulator>& hold) const
    {
        auto* manager = EmulatorManager::GetInstance();
        hold = manager ? manager->GetEmulator(id) : nullptr;
        if (hold)
            return CallbacksOf(hold.get());
        return _emulator && _emulator->GetId() == id ? CallbacksOf(_emulator) : nullptr;
    }

    /// The hit as the function gets it
    static sol::table CallbackHitTable(sol::state_view lua, const CallbackHit& hit, const CallbackInfo* info)
    {
        return LuaCallbackHitTable(lua, hit, info != nullptr ? info->event : std::string());
    }

    static sol::table CallbackInfoTable(sol::state_view lua, const CallbackInfo& info)
    {
        sol::table t = lua.create_table();
        t["id"] = info.id;
        t["on"] = CallbackTriggerName(info.trigger);
        t["mode"] = CallbackModeName(info.mode);
        t["owner"] = info.owner;
        t["label"] = info.label;
        t["target"] = info.target;
        t["cpu"] = DebugCpuName(info.cpu);
        t["breakpoint"] = info.breakpointId;
        if (!info.event.empty())
            t["event"] = info.event;
        t["policy"] = CallbackPolicyName(info.policy);
        t["state"] = CallbackStateName(info.state);
        if (!info.reason.empty())
            t["reason"] = info.reason;
        t["budget_us"] = info.budgetUs;
        t["ring"] = info.ring;
        t["pending"] = info.pending;
        const CallbackStats& s = info.stats;
        t["hits"] = s.hits; t["dropped"] = s.dropped; t["calls"] = s.calls; t["errors"] = s.errors;
        t["overruns"] = s.overruns; t["total_us"] = s.totalUs; t["max_us"] = s.maxUs; t["p99_us"] = s.p99Us;
        if (info.mode != CallbackMode::Deferred)
        {
            t["aborted"] = s.aborted;
            t["instructions"] = info.instructions;
            t["role"] = CallbackRoleName(info.role);
        }
        if (s.overruns > 0)
        {
            t["last_overrun_frame"] = s.lastOverrunFrame;
            t["last_overrun_us"] = s.lastOverrunUs;
        }
        if (!s.lastError.empty())
            t["last_error"] = s.lastError;
        return t;
    }

    /// on_tap / on_breakpoint / on_game_event: `options` {mode=, role=, policy=, budget_us=, instructions=, ring=,
    /// label=}.
    /// A function runs deferred; a string is a chunk for the hook state (mode frame, the default, or sync). The id; -1
    /// and the reason
    std::tuple<int, sol::optional<std::string>> RegisterCallback(CallbackSpec spec, const sol::object& fn,
                                                                 const sol::optional<sol::table>& options)
    {
        Emulator* emulator = effectiveEmulator();
        ScriptCallbackManager* callbacks = CallbacksOf(emulator);
        if (!callbacks)
            return {-1, std::string("no emulator")};
        const bool chunk = fn.get_type() == sol::type::string;
        if (!chunk && fn.get_type() != sol::type::function)
            return {-1, std::string("a function, or a chunk (Lua source that returns one) for the hook state")};
        spec.owner = kCallbackOwner;
        spec.mode = chunk ? CallbackMode::Frame : CallbackMode::Deferred;
        if (chunk)
            spec.chunk = fn.as<std::string>();
        if (options)
        {
            const sol::table& t = *options;
            if (auto mode = t.get<sol::optional<std::string>>("mode"))
            {
                if (!ParseCallbackMode(*mode, spec.mode))
                    return {-1, "mode must be deferred, frame or sync, not '" + *mode + "'"};
                if (chunk == (spec.mode == CallbackMode::Deferred))
                    return {-1, std::string(chunk ? "a chunk runs in the hook state: mode frame or sync"
                                                  : "a function runs deferred: frame and sync take a chunk (Lua source)")};
            }
            const std::string policy = t.get<sol::optional<std::string>>("policy").value_or("suspend");
            if (!ParseCallbackPolicy(policy, spec.policy))
                return {-1, "policy must be suspend, disable, ignore or unpaced, not '" + policy + "'"};
            const std::string role = t.get<sol::optional<std::string>>("role").value_or("view");
            if (!ParseCallbackRole(role, spec.role))
                return {-1, "role must be view or logic, not '" + role + "'"};
            spec.budgetUs = t.get<sol::optional<uint32_t>>("budget_us").value_or(spec.budgetUs);
            spec.instructions = t.get<sol::optional<uint64_t>>("instructions").value_or(0);
            spec.ring = t.get<sol::optional<uint32_t>>("ring").value_or(spec.ring);
            spec.label = t.get<sol::optional<std::string>>("label").value_or("");
        }
        if (!chunk)
            callbacks->SetWake(kCallbackOwner, _callbackWake);
        std::string error;
        const uint32_t id = callbacks->Register(spec, error);
        if (id == 0)
            return {-1, error};
        if (chunk)
            _hookEmulators.insert(emulator->GetId());
        else
            _callbacks[{emulator->GetId(), id}] = fn.as<sol::protected_function>();
        return {static_cast<int>(id), sol::nullopt};
    }

    /// The Lua thread: run every callback with hits waiting, the oldest first, and report each call's time. True when a
    /// ring still holds hits (more than one drain takes)
    bool DrainCallbacks()
    {
        if (_callbacks.empty() || _lua == nullptr)
            return false;
        bool more = false;
        std::vector<CallbackHit> hits;
        for (auto it = _callbacks.begin(); it != _callbacks.end();)
        {
            std::shared_ptr<Emulator> emulator;
            ScriptCallbackManager* callbacks = CallbacksById(it->first.first, emulator);
            if (!callbacks)
            {
                // The emulator went: its callbacks with it
                it = _callbacks.erase(it);
                continue;
            }
            const uint32_t id = it->first.second;
            CallbackInfo info;
            if (!callbacks->Get(id, info))
            {
                it = _callbacks.erase(it);
                continue;
            }
            hits.clear();
            more |= callbacks->Drain(id, hits) == kCallbackDrainBatch;
            sol::protected_function fn = it->second;
            ++it;
            for (const CallbackHit& hit : hits)
            {
                const auto start = std::chrono::steady_clock::now();
                const sol::protected_function_result result = fn(CallbackHitTable(*_lua, hit, &info));
                const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
                std::string error;
                if (!result.valid())
                {
                    sol::error raised = result;
                    error = raised.what();
                }
                callbacks->Report(id, static_cast<uint32_t>(std::min<int64_t>(us, 0xFFFF'FFFF)), !result.valid(), error);
            }
        }
        return more;
    }

    /// The Lua state goes (stop, reset): every callback of this script on every emulator
    void ForgetCallbacks()
    {
        std::set<std::string> emulators = std::move(_hookEmulators);
        _hookEmulators.clear();
        for (const auto& [key, fn] : _callbacks)
            emulators.insert(key.first);
        for (const std::string& id : emulators)
        {
            std::shared_ptr<Emulator> emulator;
            if (ScriptCallbackManager* callbacks = CallbacksById(id, emulator))
            {
                callbacks->RemoveOwner(kCallbackOwner);
                callbacks->SetWake(kCallbackOwner, nullptr);
            }
        }
        _callbacks.clear();
    }

    void setEmulator(Emulator* emulator) { _emulator = emulator; }
    void setLuaState(sol::state* lua) { _lua = lua; }

    void unregisterType(sol::state& lua)
    {
        // No specific cleanup needed
    }
    /// endregion </Lua SOL lifecycle>
};