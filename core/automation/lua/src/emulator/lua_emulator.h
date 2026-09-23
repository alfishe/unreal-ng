#pragma once

#include <sol/sol.hpp>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include "../bindings/lua_porttrace.h"
#include <emulator/memory/memory.h>
#include <emulator/memory/memorymap.h>  // TD-3 sparse map + hexdump
#include <emulator/io/fdc/fdd.h>
#include <emulator/io/fdc/diskimage.h>
#include <emulator/io/tape/tape.h>
#include <tapeaudio/tapeaudioimporter.h>
#include <tapeaudio/tapeaudiorenderer.h>
#include <emulator/cpu/z80.h>
#include <emulator/video/screen.h>
#include <emulator/sound/soundmanager.h>
#include <emulator/sound/chips/soundchip_ay8910.h>
#include <emulator/sound/chips/gs/soundchip_gs.h>
#include "../../../automation.h"
#include <debugger/debugmanager.h>
#include <debugger/mouse/debugmousemanager.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/disassembler/z80disasm.h>
#include <debugger/labels/labelmanager.h>
#include <debugger/ttd/timetravelmanager.h>
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
#include <emulator/state/devicestate.h>
#include <emulator/video/screendigest.h>
#include <base/featuremanager.h>
#ifdef ENABLE_RECORDING
#include "recordingmanager.h"
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
#include <string>
#include <thread>

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
            if (!_emulator) return 0;
            Z80State* z80 = _emulator->GetZ80State();
            return z80 ? z80->pc : 0;
        });

        lua.set_function("get_sp", [this]() -> uint16_t {
            if (!_emulator) return 0;
            Z80State* z80 = _emulator->GetZ80State();
            return z80 ? z80->sp : 0;
        });

        lua.set_function("get_af", [this]() -> uint16_t {
            if (!_emulator) return 0;
            Z80State* z80 = _emulator->GetZ80State();
            return z80 ? z80->af : 0;
        });

        lua.set_function("get_bc", [this]() -> uint16_t {
            if (!_emulator) return 0;
            Z80State* z80 = _emulator->GetZ80State();
            return z80 ? z80->bc : 0;
        });

        lua.set_function("get_de", [this]() -> uint16_t {
            if (!_emulator) return 0;
            Z80State* z80 = _emulator->GetZ80State();
            return z80 ? z80->de : 0;
        });

        lua.set_function("get_hl", [this]() -> uint16_t {
            if (!_emulator) return 0;
            Z80State* z80 = _emulator->GetZ80State();
            return z80 ? z80->hl : 0;
        });

        lua.set_function("get_ix", [this]() -> uint16_t {
            if (!_emulator) return 0;
            Z80State* z80 = _emulator->GetZ80State();
            return z80 ? z80->ix : 0;
        });

        lua.set_function("get_iy", [this]() -> uint16_t {
            if (!_emulator) return 0;
            Z80State* z80 = _emulator->GetZ80State();
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
            if (!_emulator) return 0;
            Memory* mem = _emulator->GetMemory();
            return mem ? mem->DirectReadFromZ80Memory(addr) : 0;
        });

        lua.set_function("mem_write", [this](uint16_t addr, uint8_t value) {
            if (!_emulator) return;
            Memory* mem = _emulator->GetMemory();
            if (mem) mem->MemoryWriteFast(addr, value);
        });

        lua.set_function("mem_read_word", [this](uint16_t addr) -> uint16_t {
            if (!_emulator) return 0;
            Memory* mem = _emulator->GetMemory();
            if (!mem) return 0;
            return mem->DirectReadFromZ80Memory(addr) | (mem->DirectReadFromZ80Memory(static_cast<uint16_t>(addr + 1)) << 8);
        });

        lua.set_function("mem_write_word", [this](uint16_t addr, uint16_t value) {
            if (!_emulator) return;
            Memory* mem = _emulator->GetMemory();
            if (!mem) return;
            mem->MemoryWriteFast(addr, value & 0xFF);
            mem->MemoryWriteFast(addr + 1, (value >> 8) & 0xFF);
        });

        lua.set_function("mem_read_block", [this](uint16_t addr, uint16_t len) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table data = lua_view.create_table();
            if (!_emulator) return data;
            Memory* mem = _emulator->GetMemory();
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
            if (!_emulator) return result;
            Memory* mem = _emulator->GetMemory();
            EmulatorContext* ctx = _emulator->GetContext();
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
            if (!_emulator) return "";
            Memory* mem = _emulator->GetMemory();
            if (!mem) return "";
            const size_t len = lenOpt ? static_cast<size_t>(*lenOpt) : 64;
            if (len < 1 || len > 4096) return "";
            std::vector<uint8_t> buffer(len);
            for (size_t i = 0; i < len; i++)
                buffer[i] = mem->DirectReadFromZ80Memory(static_cast<uint16_t>(addr + i));
            return FormatHexDump(buffer.data(), buffer.size(), addr);
        });

        lua.set_function("mem_write_block", [this](uint16_t addr, sol::table data) {
            if (!_emulator) return;
            Memory* mem = _emulator->GetMemory();
            if (!mem) return;
            for (auto& pair : data) {
                int idx = pair.first.as<int>() - 1;  // Lua tables start at 1
                uint8_t val = pair.second.as<uint8_t>();
                mem->MemoryWriteFast((addr + idx) & 0xFFFF, val);
            }
        });

        // Physical page access (ram/rom/cache/misc)
        lua.set_function("page_read", [this](const std::string& type, int page, int offset) -> int {
            if (!_emulator) return 0;
            Memory* mem = _emulator->GetMemory();
            if (!mem) return 0;
            uint8_t* pagePtr = nullptr;
            if (type == "ram" && page < MAX_RAM_PAGES)
                pagePtr = mem->RAMPageAddress(page);
            else if (type == "rom" && page < MAX_ROM_PAGES)
                pagePtr = mem->ROMPageHostAddress(page);
            else if (type == "cache" && page < MAX_CACHE_PAGES)
                pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
            else if (type == "misc" && page < MAX_MISC_PAGES)
                pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
            if (!pagePtr || offset < 0 || offset >= PAGE_SIZE) return 0;
            return pagePtr[offset];
        });

        lua.set_function("page_write", [this](const std::string& type, int page, int offset, uint8_t value) {
            if (!_emulator) return;
            Memory* mem = _emulator->GetMemory();
            if (!mem) return;
            uint8_t* pagePtr = nullptr;
            if (type == "ram" && page < MAX_RAM_PAGES)
                pagePtr = mem->RAMPageAddress(page);
            else if (type == "rom" && page < MAX_ROM_PAGES)
                pagePtr = mem->ROMPageHostAddress(page);
            else if (type == "cache" && page < MAX_CACHE_PAGES)
                pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
            else if (type == "misc" && page < MAX_MISC_PAGES)
                pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
            if (pagePtr && offset >= 0 && offset < PAGE_SIZE) {
                pagePtr[offset] = value;
            }
        });

        lua.set_function("page_read_block", [this](const std::string& type, int page, int offset, int len) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table data = lua_view.create_table();
            if (!_emulator) return data;
            Memory* mem = _emulator->GetMemory();
            if (!mem) return data;
            uint8_t* pagePtr = nullptr;
            if (type == "ram" && page < MAX_RAM_PAGES)
                pagePtr = mem->RAMPageAddress(page);
            else if (type == "rom" && page < MAX_ROM_PAGES)
                pagePtr = mem->ROMPageHostAddress(page);
            else if (type == "cache" && page < MAX_CACHE_PAGES)
                pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
            else if (type == "misc" && page < MAX_MISC_PAGES)
                pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
            if (!pagePtr) return data;
            if (offset < 0) offset = 0;
            if (offset >= PAGE_SIZE) return data;
            if (offset + len > PAGE_SIZE) len = PAGE_SIZE - offset;
            for (int i = 0; i < len; i++) {
                data[i + 1] = pagePtr[offset + i];
            }
            return data;
        });

        lua.set_function("page_write_block", [this](const std::string& type, int page, int offset, sol::table data) {
            if (!_emulator) return;
            Memory* mem = _emulator->GetMemory();
            if (!mem) return;
            uint8_t* pagePtr = nullptr;
            if (type == "ram" && page < MAX_RAM_PAGES)
                pagePtr = mem->RAMPageAddress(page);
            else if (type == "rom" && page < MAX_ROM_PAGES)
                pagePtr = mem->ROMPageHostAddress(page);
            else if (type == "cache" && page < MAX_CACHE_PAGES)
                pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
            else if (type == "misc" && page < MAX_MISC_PAGES)
                pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
            if (!pagePtr || offset < 0 || offset >= PAGE_SIZE) return;
            int maxLen = PAGE_SIZE - offset;
            int idx = 0;
            for (auto& pair : data) {
                if (idx >= maxLen) break;
                uint8_t val = pair.second.as<uint8_t>();
                pagePtr[offset + idx] = val;
                idx++;
            }
        });

        lua.set_function("memory_info", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table info = lua_view.create_table();
            if (!_emulator) return info;
            Memory* mem = _emulator->GetMemory();
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

        lua.set_function("feature_set", [this](const std::string& name, bool enabled) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            FeatureManager* fm = emulator->GetFeatureManager();
            return fm ? fm->setFeature(name, enabled) : false;
        });

        // Disk inspection functions
        lua.set_function("disk_is_inserted", [this](int drive) -> bool {
            if (!_emulator || drive < 0 || drive > 3) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->coreState.diskDrives[drive]) return false;
            return ctx->coreState.diskDrives[drive]->isDiskInserted();
        });

        lua.set_function("disk_get_path", [this](int drive) -> std::string {
            if (!_emulator || drive < 0 || drive > 3) return "";
            auto* ctx = _emulator->GetContext();
            if (!ctx) return "";
            return ctx->coreState.diskFilePaths[drive];
        });

        lua.set_function("disk_eject", [this](int drive) -> bool {
            if (!_emulator || drive < 0 || drive > 3) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->coreState.diskDrives[drive]) return false;
            ctx->coreState.diskDrives[drive]->ejectDisk();
            ctx->coreState.diskFilePaths[drive] = "";
            return true;
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

        lua.set_function("disk_create", [this](int drive, sol::optional<int> cyl, sol::optional<int> sides) -> bool {
            if (!_emulator || drive < 0 || drive > 3) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->coreState.diskDrives[drive]) return false;
            
            uint8_t cylinders = cyl.value_or(80);
            uint8_t numSides = sides.value_or(2);
            
            if (cylinders != 40 && cylinders != 80) return false;
            if (numSides != 1 && numSides != 2) return false;
            
            DiskImage* diskImage = new DiskImage(cylinders, numSides);
            FDD* fdd = ctx->coreState.diskDrives[drive];
            fdd->insertDisk(diskImage);
            ctx->coreState.diskFilePaths[drive] = "<blank>";
            
            return true;
        });

        lua.set_function("disk_list", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table drives = lua_view.create_table();
            if (_emulator) {
                auto* ctx = _emulator->GetContext();
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
            if (!_emulator) return;
            _emulator->RunSingleCPUCycle(skipBP.value_or(true));
        });

        lua.set_function("steps", [this](unsigned count, sol::optional<bool> skipBP) {
            if (!_emulator) return;
            _emulator->RunNCPUCycles(count, skipBP.value_or(false));
        });

        lua.set_function("stepover", [this]() {
            if (!_emulator) return;
            _emulator->StepOver();
        });

        // Frame stepping methods
        lua.set_function("run_frame", [this](sol::optional<bool> skipBP) {
            if (!_emulator) return;
            _emulator->RunFrame(skipBP.value_or(true));
        });

        lua.set_function("run_frames", [this](unsigned count, sol::optional<bool> skipBP) {
            if (!_emulator) return;
            _emulator->RunNFrames(count, skipBP.value_or(true));
        });

        // Atomic stepping methods
        lua.set_function("run_tstates", [this](unsigned count, sol::optional<bool> skipBP) {
            if (!_emulator) return;
            _emulator->RunTStates(count, skipBP.value_or(true));
        });

        lua.set_function("run_to_scanline", [this](unsigned scanline, sol::optional<bool> skipBP) {
            if (!_emulator) return;
            _emulator->RunUntilScanline(scanline, skipBP.value_or(true));
        });

        lua.set_function("run_scanlines", [this](unsigned count, sol::optional<bool> skipBP) {
            if (!_emulator) return;
            _emulator->RunNScanlines(count, skipBP.value_or(true));
        });

        lua.set_function("run_to_pixel", [this](sol::optional<bool> skipBP) {
            if (!_emulator) return;
            _emulator->RunUntilNextScreenPixel(skipBP.value_or(true));
        });

        lua.set_function("run_to_interrupt", [this](sol::optional<bool> skipBP) {
            if (!_emulator) return;
            _emulator->RunUntilInterrupt(skipBP.value_or(true));
        });

        lua.set_function("run_until_condition", [this](sol::function predicate, sol::optional<unsigned> maxTStates) {
            if (!_emulator) return;
            _emulator->RunUntilCondition([&predicate](const Z80State& state) -> bool {
                return predicate(state.pc, state.af, state.bc, state.de, state.hl).get<bool>();
            }, maxTStates.value_or(0));
        });

        // Tape operations
        lua.set_function("tape_load", [this](const std::string& path) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            return emulator->LoadTape(path);
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
            auto* ctx = emulator->GetContext();
            if (ctx && ctx->pTape) {
                ctx->pTape->reset();
                ctx->coreState.tapeFilePath = "";
                return true;
            }
            return false;
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

        // Snapshot operations
        lua.set_function("snapshot_load", [this](const std::string& path) -> bool {
            if (!_emulator) return false;
            return _emulator->LoadSnapshot(path);
        });

        lua.set_function("snapshot_save", [this](const std::string& path) -> bool {
            if (!_emulator) return false;
            return _emulator->SaveSnapshot(path);
        });

        // Breakpoint management
        lua.set_function("bp", [this](uint16_t addr) -> int {
            if (!_emulator) return -1;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return -1;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? static_cast<int>(bpm->AddExecutionBreakpoint(addr)) : -1;
        });

        lua.set_function("bp_read", [this](uint16_t addr) -> int {
            if (!_emulator) return -1;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return -1;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? static_cast<int>(bpm->AddMemReadBreakpoint(addr)) : -1;
        });

        lua.set_function("bp_write", [this](uint16_t addr) -> int {
            if (!_emulator) return -1;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return -1;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? static_cast<int>(bpm->AddMemWriteBreakpoint(addr)) : -1;
        });

        lua.set_function("bp_port_in", [this](uint16_t port) -> int {
            if (!_emulator) return -1;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return -1;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? static_cast<int>(bpm->AddPortInBreakpoint(port)) : -1;
        });

        lua.set_function("bp_port_out", [this](uint16_t port) -> int {
            if (!_emulator) return -1;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return -1;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? static_cast<int>(bpm->AddPortOutBreakpoint(port)) : -1;
        });

        lua.set_function("bp_remove", [this](uint16_t id) -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return false;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? bpm->RemoveBreakpointByID(id) : false;
        });

        lua.set_function("bp_clear", [this]() {
            if (!_emulator) return;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            if (bpm) bpm->ClearBreakpoints();
        });

        lua.set_function("bp_enable", [this](uint16_t id) -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return false;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? bpm->ActivateBreakpoint(id) : false;
        });

        lua.set_function("bp_disable", [this](uint16_t id) -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return false;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? bpm->DeactivateBreakpoint(id) : false;
        });

        lua.set_function("bp_count", [this]() -> size_t {
            if (!_emulator) return 0;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return 0;
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? bpm->GetBreakpointsCount() : 0;
        });

        lua.set_function("bp_list", [this]() -> std::string {
            if (!_emulator) return "";
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return "";
            BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
            return bpm ? bpm->GetBreakpointListAsString() : "";
        });

        lua.set_function("bp_status", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!_emulator) {
                result["valid"] = false;
                return result;
            }
            auto* ctx = _emulator->GetContext();
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
            if (!_emulator) return;
            auto* ctx = _emulator->GetContext();
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
            if (!_emulator) return "";
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pScreen) return "";
            return Screen::GetVideoModeName(ctx->pScreen->GetVideoMode());
        });

        lua.set_function("screen_get_border", [this]() -> int {
            if (!_emulator) return 0;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pScreen) return 0;
            return ctx->pScreen->GetBorderColor();
        });

        lua.set_function("screen_get_flash", [this]() -> int {
            if (!_emulator) return 0;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pScreen) return 0;
            return ctx->pScreen->_vid.flash;
        });

        lua.set_function("screen_get_active", [this]() -> int {
            if (!_emulator) return 0;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pScreen) return 0;
            return ctx->pScreen->GetActiveScreen();
        });

        // Detailed video mode state including Pentagon 16-color mode
        lua.set_function("screen_video_state", [this](sol::this_state s) -> sol::table {
            sol::state_view lua(s);
            sol::table result = lua.create_table();

            if (!_emulator) return result;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pScreen) return result;

            Screen* screen = ctx->pScreen;
            VideoModeEnum mode = screen->GetVideoMode();

            result["video_mode"] = Screen::GetVideoModeName(mode);
            result["border_color"] = screen->GetBorderColor();
            result["active_screen"] = screen->GetActiveScreen();

            // Mode-specific details
            switch (mode)
            {
                case M_P16:
                    result["resolution"] = "256x192";
                    result["bpp"] = 4;
                    result["colors"] = 16;
                    result["eff7_16col"] = true;
                    break;
                case M_PMC:
                    result["resolution"] = "256x192";
                    result["eff7_hwmc"] = true;
                    break;
                case M_PHR:
                    result["resolution"] = "512x192";
                    result["bpp"] = 1;
                    result["eff7_512"] = true;
                    break;
                case M_P384:
                    result["resolution"] = "384x304";
                    result["overscan"] = true;
                    break;
                default:
                    result["resolution"] = "256x192";
                    result["bpp"] = 1;
                    break;
            }

            // Pentagon EFF7 state
            if (ctx->config.mem_model == MM_PENTAGON)
            {
                uint8_t eff7 = ctx->emulatorState.pEFF7;
                if (eff7 != 0)
                {
                    sol::table eff7_state = lua.create_table();
                    eff7_state["value"] = static_cast<int>(eff7);
                    eff7_state["16col_enabled"] = (eff7 & EFF7_4BPP) != 0;
                    eff7_state["512_enabled"] = (eff7 & EFF7_512) != 0;
                    eff7_state["hwmc_enabled"] = (eff7 & EFF7_HWMC) != 0;
                    eff7_state["384_enabled"] = (eff7 & EFF7_384) != 0;
                    result["eff7"] = eff7_state;
                }
            }

            return result;
        });

        // Device state reports (core DeviceState: the same trees the WebAPI,
        // Python, CLI and MCP return). Optional chip index -> the chip's
        // full report, no index -> the overview
        lua.set_function("audio_ay_state", [this](sol::this_state s, sol::optional<int> chip) -> sol::object {
            EmulatorContext* ctx = _emulator ? _emulator->GetContext() : nullptr;
            return StateNodeToLua(s, chip ? DeviceState::AyChip(ctx, *chip) : DeviceState::Ay(ctx));
        });
        lua.set_function("audio_fm_state", [this](sol::this_state s, sol::optional<int> chip) -> sol::object {
            EmulatorContext* ctx = _emulator ? _emulator->GetContext() : nullptr;
            return StateNodeToLua(s, chip ? DeviceState::FmChip(ctx, *chip) : DeviceState::Fm(ctx));
        });
        lua.set_function("fdc_state", [this](sol::this_state s) -> sol::object {
            EmulatorContext* ctx = _emulator ? _emulator->GetContext() : nullptr;
            return StateNodeToLua(s, DeviceState::Fdc(ctx));
        });

        // Audio state
        lua.set_function("audio_is_muted", [this]() -> bool {
            if (!_emulator) return true;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pSoundManager) return true;
            return ctx->pSoundManager->isMuted();
        });

        lua.set_function("audio_ay_read", [this](int chip, int reg) -> int {
            if (!_emulator) return 0;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pSoundManager) return 0;
            auto* ay = ctx->pSoundManager->getAYChip(chip);
            if (!ay || reg < 0 || reg > 15) return 0;
            return ay->readRegister(static_cast<uint8_t>(reg));
        });

        lua.set_function("audio_ay_registers", [this](sol::optional<int> chip) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table regs = lua_view.create_table();
            if (!_emulator) return regs;
            auto* ctx = _emulator->GetContext();
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
            if (!_emulator) return 0;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pSoundManager) return 0;
            return ctx->pSoundManager->getAYChipCount();
        });

        // General Sound card (GS design §11.5). All actions mirror the
        // host-port semantics - each flushes the coprocessor to the current
        // ZX tact first. No-ops / nil when the card is not fitted.
        lua.set_function("gs_enabled", [this]() -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            return ctx && ctx->pSoundManager && ctx->pSoundManager->getGeneralSound() != nullptr;
        });

        lua.set_function("gs_state", [this]() -> sol::object {
            sol::state_view lua_view(*_lua);
            if (!_emulator) return sol::make_object(lua_view, sol::lua_nil);
            auto* ctx = _emulator->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (!gs) return sol::make_object(lua_view, sol::lua_nil);

            sol::table t = lua_view.create_table();
            const uint8_t status = gs->getStatusRaw();
            t["device"] = gs->hasCoprocessor() ? "General Sound (Z80 coprocessor @ 12 MHz, 4 x 8-bit DAC)"
                                               : "General Sound (lightweight mod player, 4 x 8-bit DAC)";
            t["implementation"] = gs->implementation() == GSCardImplementation::LLE ? "lle" : "lightweight";
            t["rom_loaded"] = gs->isROMLoaded();
            t["ram_kb"] = static_cast<int>(gs->getRamSizeKB());
            t["status"] = status;
            t["command_pending"] = (status & 0x01) != 0;
            t["data_pending"] = (status & 0x80) != 0;
            t["command_queue_count"] = static_cast<double>(gs->getCommandQueueCount());
            t["data_queue_count"] = static_cast<double>(gs->getDataQueueCount());
            t["command_from_host"] = gs->getCommandFromHost();
            t["data_from_host"] = gs->getDataFromHost();
            t["data_to_host"] = gs->getDataToHost();
            t["page"] = gs->getMPAG();

            sol::table channels = lua_view.create_table();
            for (int i = 0; i < 4; i++) {
                sol::table channel = lua_view.create_table();
                channel["sample"] = gs->getChannelSample(i);
                channel["volume"] = gs->getChannelVolume(i);
                channels[i + 1] = channel;  // Lua tables start at 1
            }
            t["channels"] = channels;

            sol::table cpu = lua_view.create_table();
            cpu["coprocessor"] = gs->hasCoprocessor();
            cpu["pc"] = gs->getCPUReg(regPC);
            cpu["sp"] = gs->getCPUReg(regSP);
            cpu["af"] = gs->getCPUReg(regAF);
            cpu["halted"] = gs->isCPUHalted();
            t["cpu"] = cpu;
            return t;
        });

        lua.set_function("gs_reset", [this]() {
            if (!_emulator) return;
            auto* ctx = _emulator->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (gs) gs->reset();
        });

        lua.set_function("gs_reset_card", [this]() {
            // #33 bit7 semantics: CPU/banking/timing only, mailbox survives
            if (!_emulator) return;
            auto* ctx = _emulator->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (gs) gs->resetCard();
        });

        lua.set_function("gs_nmi", [this]() {
            if (!_emulator) return;
            auto* ctx = _emulator->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (gs) gs->triggerNMI();
        });

        lua.set_function("gs_send_command", [this](int byte) {
            if (!_emulator || byte < 0 || byte > 255) return;
            auto* ctx = _emulator->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (gs) gs->sendCommand(static_cast<uint8_t>(byte));
        });

        lua.set_function("gs_send_data", [this](int byte) {
            if (!_emulator || byte < 0 || byte > 255) return;
            auto* ctx = _emulator->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (gs) gs->sendData(static_cast<uint8_t>(byte));
        });

        lua.set_function("gs_read_data", [this]() -> int {
            if (!_emulator) return -1;
            auto* ctx = _emulator->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            return gs ? gs->readData() : -1;
        });

        lua.set_function("gs_read_status", [this]() -> int {
            if (!_emulator) return -1;
            auto* ctx = _emulator->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            return gs ? gs->readStatus() : -1;
        });

        // Runtime personality switch (GS card personalities design §11.3):
        // requested here, applied at the next frame boundary on the
        // emulation thread - same semantics as the WebAPI switch_personality
        // action and the MCP gs_switch_personality tool action
        lua.set_function("gs_switch_personality", [this](const std::string& personality) -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            SoundManager* sm = ctx ? ctx->pSoundManager : nullptr;
            if (!sm) return false;

            GSTypeKind target;
            if (personality == "z80" || personality == "lle")
                target = GSTypeKind::Z80;
            else if (personality == "lw" || personality == "lightweight")
                target = GSTypeKind::LW;
            else
                return false;

            return sm->requestGeneralSoundCardSwitch(target);
        });

        // Diagnostics: write the last completed COM30..D2 upload (the raw
        // ProTracker module the host streamed) to a file - same data
        // dump_module serves via the WebAPI/MCP
        lua.set_function("gs_dump_module", [this](sol::optional<std::string> path) -> sol::object {
            sol::state_view lua_view(*_lua);
            if (!_emulator) return sol::make_object(lua_view, sol::lua_nil);
            auto* ctx = _emulator->GetContext();
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
            if (!_emulator) return sol::make_object(lua_view, sol::lua_nil);
            auto* ctx = _emulator->GetContext();
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
            t["pc"] = gs->getCPUReg(regPC);
            t["halted"] = gs->isCPUHalted();
            return t;
        });

        lua.set_function("gs_porttrace_start", [this]() {
            if (!_emulator) return;
            auto* ctx = _emulator->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (gs) gs->startPortTrace();
        });
        lua.set_function("gs_porttrace_stop", [this]() {
            if (!_emulator) return;
            auto* ctx = _emulator->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (gs) gs->stopPortTrace();
        });
        lua.set_function("gs_porttrace_pause", [this]() {
            if (!_emulator) return;
            auto* ctx = _emulator->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (gs) gs->pausePortTrace();
        });
        lua.set_function("gs_porttrace_resume", [this]() {
            if (!_emulator) return;
            auto* ctx = _emulator->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (gs) gs->resumePortTrace();
        });
        lua.set_function("gs_porttrace_clear", [this]() {
            if (!_emulator) return;
            auto* ctx = _emulator->GetContext();
            GeneralSoundCard* gs = ctx && ctx->pSoundManager ? ctx->pSoundManager->getGeneralSound() : nullptr;
            if (gs) gs->clearPortTrace();
        });

        lua.set_function("gs_porttrace_events", [this](sol::optional<int> count) -> sol::object {
            sol::state_view lua_view(*_lua);
            if (!_emulator) return sol::make_object(lua_view, sol::lua_nil);
            auto* ctx = _emulator->GetContext();
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
                }
                ev["direction"] = e.isOut() ? "out" : "in";
                ev["port"] = e.port;
                ev["value"] = e.value;
                ev["pc"] = e.pc;
                if (e.side == GSTraceSide::DacFetch) ev["channel"] = e.channel;
                if (e.side == GSTraceSide::Interrupt) ev["nmi"] = e.isNmi();
                result[idx++] = ev;
            }
            return result;
        });

        // Advanced disk operations
        lua.set_function("disk_info", [this](int drive) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table info = lua_view.create_table();
            if (!_emulator || drive < 0 || drive > 3) return info;
            auto* ctx = _emulator->GetContext();
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
            if (!_emulator || drive < 0 || drive > 3) return data;
            auto* ctx = _emulator->GetContext();
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
            if (!_emulator || drive < 0 || drive > 3) return "";
            auto* ctx = _emulator->GetContext();
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
            info["ttd_available"]            = true;
            return info;
        });

        // ttd_start([mode]) - start recording
        // mode: "gaming" (smaller files, no journal) or "development" (default, full journal)
        lua.set_function("ttd_start", [this](sol::optional<std::string> modeOpt) -> bool {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return false;
            auto* ctx = emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            // Set journal mode before starting
            bool enableJournal = true;  // default: development mode
            if (modeOpt.has_value())
            {
                const std::string& mode = modeOpt.value();
                if (mode == "gaming")
                    enableJournal = false;
            }
            ctx->pTimeTravelManager->SetEnableWriteJournal(enableJournal);
            return ctx->pTimeTravelManager->StartRecording();
        });

        // ttd_set_journal_enabled(bool) - configure write journal capture
        lua.set_function("ttd_set_journal_enabled", [this](bool enabled) {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return;
            auto* ctx = emulator->GetContext();
            if (ctx && ctx->pTimeTravelManager)
                ctx->pTimeTravelManager->SetEnableWriteJournal(enabled);
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

        lua.set_function("ttd_invalidate", [this](sol::optional<std::string> reason) {
            Emulator* emulator = effectiveEmulator();
            if (!emulator) return;
            auto* ctx = emulator->GetContext();
            if (ctx && ctx->pTimeTravelManager)
                ctx->pTimeTravelManager->InvalidateSession(
                    reason.value_or("lua invalidate").c_str());
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
            ttd::TTDTimePoint from = ctx->pTimeTravelManager->CurrentPosition();
            if (frameOpt)
                from.frame = *frameOpt;
            from.tInFrame = tInFrameOpt.value_or(0);
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

        lua.set_function("ttd_find_last", [this](sol::object firstArgOpt,
                                                   sol::optional<std::string> accessOpt,
                                                   sol::optional<uint8_t> valueOpt,
                                                   sol::optional<uint16_t> pcFromOpt,
                                                   sol::optional<uint16_t> pcToOpt,
                                                   sol::optional<uint64_t> beforeFrameOpt,
                                                   sol::optional<uint32_t> beforeTinOpt,
                                                   sol::optional<uint8_t> physPageOpt,
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

                if (tbl["phys_page"].valid()) { q.hasPhysPageFilter = true; q.physPage = tbl["phys_page"].get<uint8_t>(); }
                else if (tbl["physPage"].valid()) { q.hasPhysPageFilter = true; q.physPage = tbl["physPage"].get<uint8_t>(); }

                const uint32_t frameT = ctx->config.frame;
                if (tbl["before_frame"].valid())
                {
                    uint64_t f = tbl["before_frame"].get<uint64_t>();
                    uint32_t tin = tbl["before_tin"].valid() ? tbl["before_tin"].get<uint32_t>() : 0;
                    q.beforeGlobalT = f * frameT + tin;
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
                if (physPageOpt) { q.hasPhysPageFilter = true; q.physPage = *physPageOpt; }
                const uint32_t frameT = ctx->config.frame;
                if (beforeFrameOpt)
                    q.beforeGlobalT = static_cast<uint64_t>(*beforeFrameOpt) * frameT + beforeTinOpt.value_or(0);
            }

            auto found = ctx->pTimeTravelManager->FindLastAccess(q);
            if (!found) { result["found"] = false; return result; }
            result["found"]    = true;
            result["frame"]    = found->time.frame;
            result["tinframe"]  = found->time.tInFrame;
            result["pc"]        = found->pc;
            result["value"]     = found->value;
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

            std::optional<uint8_t> physPage;
            if (argsTable["phys_page"].valid()) physPage = static_cast<uint8_t>(argsTable.get<uint32_t>("phys_page"));

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

            std::optional<uint8_t> physPage;
            if (argsTable["phys_page"].valid()) physPage = static_cast<uint8_t>(argsTable.get<uint32_t>("phys_page"));

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

            const CONFIG& config = context->config;
            EmulatorState& state = context->emulatorState;
            Memory* memory = context->pMemory;

            const bool is128K = (config.mem_model == MM_SPECTRUM128 || config.mem_model == MM_PENTAGON ||
                                 config.mem_model == MM_PLUS3);

            // mode: "active" hashes the RAM pages the CURRENT video mode actually
            // displays (ATM hardware modes follow the 7FFD-selected bit-plane pair);
            // "default" / omitted keeps the model-dependent pages 5/7
            bool activeMode = false;
            if (modeOpt.has_value())
            {
                if (*modeOpt == "active")
                    activeMode = true;
                else if (*modeOpt != "default")
                {
                    result["error"] = "mode must be 'default' or 'active'";
                    return result;
                }
            }

            const uint64_t previousDigest = state.last_screen_digest;
            const uint64_t previousFrame = state.last_screen_digest_frame;

            uint64_t combined = ScreenDigest::kInitialValue;

            if (startOpt.has_value() || endOpt.has_value())
            {
                const uint16_t start = static_cast<uint16_t>(startOpt.value_or(0x4000));
                const uint16_t end = static_cast<uint16_t>(endOpt.value_or(0x7FFF));
                if (start > end)
                {
                    result["error"] = "start must be <= end";
                    return result;
                }

                const uint64_t rangeDigest = ScreenDigest::DigestZ80Range(memory, start, end);
                for (int shift = 0; shift < 64; shift += 8)
                    combined = ScreenDigest::MixValue(combined, static_cast<uint8_t>((rangeDigest >> shift) & 0xFF));

                result["range_start"] = start;
                result["range_end"] = end;
                result["range_digest"] = rangeDigest;
            }
            else
            {
                std::vector<uint16_t> banks;
                if (activeMode)
                {
                    // Surface actually displayed by the current video mode: ZX modes
                    // keep pages 5/7, ATM hardware modes hash the 7FFD-selected
                    // bit-plane pair - flipping FF77 between ZX and 16c surfaces now
                    // flips the digest even with constant underlying pages
                    const VideoModeEnum videoMode = context->pScreen->GetVideoMode();
                    banks = Screen::GetActiveSurfaceRAMPages(videoMode, state.p7FFD, is128K);

                    sol::table pages = lua_view.create_table();
                    for (uint16_t page : banks)
                        pages.add(page);
                    sol::table activeSurface = lua_view.create_table();
                    activeSurface["video_mode"] = Screen::GetVideoModeName(videoMode);
                    activeSurface["pages"] = pages;
                    result["active_surface"] = activeSurface;
                }
                else
                {
                    banks.push_back(ScreenDigest::kScreen0RAMPage);
                    if (is128K)
                        banks.push_back(ScreenDigest::kScreen1RAMPage);
                }

                sol::table perBank = lua_view.create_table();
                for (uint16_t page : banks)
                {
                    const uint64_t digest = ScreenDigest::DigestRAMPage(memory, page);
                    for (int shift = 0; shift < 64; shift += 8)
                        combined = ScreenDigest::MixValue(combined, static_cast<uint8_t>((digest >> shift) & 0xFF));
                    perBank[page] = digest;
                }
                result["banks"] = perBank;
            }

            const bool includeBorder = includeBorderOpt.value_or(true);
            if (includeBorder)
            {
                const uint8_t borderColor = context->pScreen->GetBorderColor();
                combined = ScreenDigest::MixValue(combined, borderColor);
                result["border_color"] = borderColor;
            }

            state.last_screen_digest = combined;
            state.last_screen_digest_frame = state.frame_counter;

            result["combined"] = combined;
            result["frame"] = static_cast<uint64_t>(state.frame_counter);
            result["algorithm"] = "fnv1a-64";
            result["changed"] = combined != previousDigest;
            result["previous_digest"] = previousDigest;
            if (previousFrame != 0)
                result["previous_digest_frame"] = static_cast<uint64_t>(previousFrame);
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
                        case 1: bank["page"] = static_cast<int>(memory.GetRAMPageForBank1()); bank["contended"] = true; break;
                        case 2: bank["page"] = static_cast<int>(memory.GetRAMPageForBank2()); break;
                        case 3: bank["page"] = static_cast<int>(memory.GetRAMPageForBank3()); break;
                    }
                }
                banks.add(bank);
            }
            result["banks"] = banks;

            results.push_back(sol::make_object(s, result));
            return results;
        });

        // Raster beam position and zone at the current t-state
        lua.set_function("beam_position", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            Emulator* emulator = effectiveEmulator();
            if (!emulator) { result["error"] = "no emulator"; return result; }

            EmulatorContext* context = emulator->GetContext();
            if (!context || !context->pScreen) { result["error"] = "context not initialized"; return result; }

            const CONFIG& config = context->config;
            Screen* screen = context->pScreen;
            if (config.t_line == 0 || config.frame == 0)
            {
                result["error"] = "machine timing not initialized";
                return result;
            }

            Z80* cpu = context->pCore ? context->pCore->GetZ80() : nullptr;
            const uint32_t tstate = cpu ? static_cast<uint32_t>(cpu->t) : screen->GetCurrentTstate();
            const uint32_t tInFrame = tstate % config.frame;

            const VideoModeEnum mode = screen->GetVideoMode();
            const RasterDescriptor& rd = screen->rasterDescriptors[mode];
            const RasterState& rs = screen->GetRasterState();

            const bool rasterValid = rs.tstatesPerLine != 0;
            const uint32_t tstatesPerLine = rasterValid ? rs.tstatesPerLine : config.t_line;
            const uint32_t line = tInFrame / tstatesPerLine;
            const uint32_t dotInLine = tInFrame % tstatesPerLine;

            std::string vZone = "beyond_raster";
            if (rasterValid)
            {
                if (tInFrame <= rs.blankAreaEnd)
                    vZone = (line < rd.vSyncLines) ? "vsync" : "vblank";
                else if (tInFrame <= rs.topBorderAreaEnd)
                    vZone = "top_border";
                else if (tInFrame <= rs.screenAreaEnd)
                    vZone = "screen";
                else if (tInFrame <= rs.bottomBorderAreaEnd)
                    vZone = "bottom_border";
            }

            std::string hZone = "-";
            if (vZone == "screen")
            {
                if (dotInLine <= rs.blankLineAreaEnd)
                    hZone = "hblank";
                else if (dotInLine <= rs.leftBorderAreaEnd)
                    hZone = "left_border";
                else if (dotInLine <= rs.screenLineAreaEnd)
                    hZone = "paper";
                else if (dotInLine <= rs.rightBorderAreaEnd)
                    hZone = "right_border";
                else
                    hZone = "beyond_line";
            }

            std::string zone = vZone;
            if (vZone == "screen")
                zone = (hZone == "paper") ? "paper" : (hZone == "hblank" ? "hblank" : "border");

            result["tstate"] = tstate;
            result["tstate_in_frame"] = tInFrame;
            result["frame"] = static_cast<uint64_t>(context->emulatorState.frame_counter);
            result["line"] = line;
            result["dot_in_line"] = dotInLine;
            result["beam_x"] = dotInLine * rs.pixelsPerTState;
            result["beam_y"] = line;
            result["zone"] = zone;
            result["vertical_zone"] = vZone;
            result["horizontal_zone"] = hZone;
            result["in_paper"] = zone == "paper";
            return result;
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
        lua.set_function("audio_capture_start", [this](double seconds) -> sol::table {
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

            const size_t rate = context->pSoundManager ? context->pSoundManager->getCoreRate() : 44100;
            const size_t target = static_cast<size_t>(seconds * static_cast<double>(rate)) * 2;

            manager->activate("audiocapture");
            capture->startCapture(target);

            result["armed"] = capture->isCaptureArmed();
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

                const bool started = rm->StartRecording(filename, format, "");

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
                    uint32_t addr = asmResult.startAddress;
                    for (uint8_t b : asmResult.bytes)
                        memory->MemoryWriteFast(static_cast<uint16_t>((addr++) & 0xFFFF), b);
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
    }

    void setEmulator(Emulator* emulator) { _emulator = emulator; }
    void setLuaState(sol::state* lua) { _lua = lua; }

    void unregisterType(sol::state& lua)
    {
        // No specific cleanup needed
    }
    /// endregion </Lua SOL lifecycle>
};