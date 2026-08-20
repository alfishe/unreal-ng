#pragma once

#include <sol/sol.hpp>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/memory/memory.h>
#include <emulator/io/fdc/fdd.h>
#include <emulator/io/fdc/diskimage.h>
#include <emulator/io/tape/tape.h>
#include <emulator/cpu/z80.h>
#include <emulator/video/screen.h>
#include <emulator/sound/soundmanager.h>
#include <emulator/sound/chips/soundchip_ay8910.h>
#include "../../../automation.h"
#include <debugger/debugmanager.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/disassembler/z80disasm.h>
#include <debugger/ttd/timetravelmanager.h>
#include <debugger/ttd/ttd_external_events.h>
#include <debugger/ttd/ttd_probe.h>
#include <fstream>
#include <string>

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
    /// endregion </Fields>

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

        // Memory access (isExecution=false for data reads)
        lua.set_function("mem_read", [this](uint16_t addr) -> uint8_t {
            if (!_emulator) return 0;
            Memory* mem = _emulator->GetMemory();
            return mem ? mem->MemoryReadFast(addr, false) : 0;
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
            return mem->MemoryReadFast(addr, false) | (mem->MemoryReadFast(addr + 1, false) << 8);
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
                data[i + 1] = mem->MemoryReadFast(addr + i, false);
            }
            return data;
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
        lua.set_function("feature_list", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table features = lua_view.create_table();
            if (_emulator) {
                FeatureManager* fm = _emulator->GetFeatureManager();
                if (fm) {
                    features["sound"] = fm->isEnabled("sound");
                    features["sharedmemory"] = fm->isEnabled("sharedmemory");
                    features["calltrace"] = fm->isEnabled("calltrace");
                    features["breakpoints"] = fm->isEnabled("breakpoints");
                    features["memorytracking"] = fm->isEnabled("memorytracking");
                }
            }
            return features;
        });

        lua.set_function("feature_get", [this](const std::string& name) -> bool {
            if (!_emulator) return false;
            FeatureManager* fm = _emulator->GetFeatureManager();
            return fm ? fm->isEnabled(name) : false;
        });

        lua.set_function("feature_set", [this](const std::string& name, bool enabled) -> bool {
            if (!_emulator) return false;
            FeatureManager* fm = _emulator->GetFeatureManager();
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
            if (!_emulator) return false;
            return _emulator->LoadTape(path);
        });

        lua.set_function("tape_is_inserted", [this]() -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            return ctx && ctx->pTape && !ctx->coreState.tapeFilePath.empty();
        });

        lua.set_function("tape_get_path", [this]() -> std::string {
            if (!_emulator) return "";
            auto* ctx = _emulator->GetContext();
            return ctx ? ctx->coreState.tapeFilePath : "";
        });

        lua.set_function("tape_play", [this]() -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (ctx && ctx->pTape) {
                ctx->pTape->startTape();
                return true;
            }
            return false;
        });

        lua.set_function("tape_stop", [this]() -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (ctx && ctx->pTape) {
                ctx->pTape->stopTape();
                return true;
            }
            return false;
        });

        lua.set_function("tape_rewind", [this]() -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (ctx && ctx->pTape) {
                ctx->pTape->reset();
                return true;
            }
            return false;
        });

        lua.set_function("tape_eject", [this]() -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (ctx && ctx->pTape) {
                ctx->pTape->reset();
                ctx->coreState.tapeFilePath = "";
                return true;
            }
            return false;
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

        // Disassembly
        lua.set_function("disasm", [this](sol::optional<int> address, sol::optional<int> count) -> sol::table {
            sol::table result = _lua->create_table();
            if (!_emulator) return result;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager || !ctx->pDebugManager->GetDisassembler()) return result;
            
            Z80Disassembler* disasm = ctx->pDebugManager->GetDisassembler().get();
            Memory* memory = ctx->pMemory;
            
            uint16_t addr = address.value_or(-1) < 0 ? ctx->pCore->GetZ80()->pc : static_cast<uint16_t>(address.value_or(0));
            int cnt = count.value_or(10);
            if (cnt < 1) cnt = 10;
            if (cnt > 100) cnt = 100;
            
            int idx = 1;
            for (int i = 0; i < cnt; ++i) {
                std::vector<uint8_t> buffer;
                for (int j = 0; j < 4; ++j) {
                    buffer.push_back(memory->MemoryReadFast(static_cast<uint16_t>(addr + j), false));
                }
                
                uint8_t cmdLen = 0;
                DecodedInstruction decoded;
                std::string mnemonic = disasm->disassembleSingleCommand(buffer, addr, &cmdLen, &decoded);
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
                if (decoded.hasJump || decoded.hasRelativeJump) {
                    instr["target"] = decoded.hasRelativeJump ? decoded.relJumpAddr : decoded.jumpAddr;
                }
                result[idx++] = instr;
                addr += cmdLen;
            }
            return result;
        });

        // Physical page disassembly
        lua.set_function("disasm_page", [this](const std::string& type, int page, sol::optional<int> offset, sol::optional<int> count) -> sol::table {
            sol::table result = _lua->create_table();
            if (!_emulator) return result;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager || !ctx->pDebugManager->GetDisassembler()) return result;
            
            Z80Disassembler* disasm = ctx->pDebugManager->GetDisassembler().get();
            Memory* memory = ctx->pMemory;
            
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
                if (decoded.hasJump || decoded.hasRelativeJump) {
                    instr["target"] = decoded.hasRelativeJump ? decoded.relJumpAddr : decoded.jumpAddr;
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
            info["sectors_per_track"] = 16;
            info["sector_size"] = 256;
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
            auto* sec = track->getSector(sector);
            if (!sec) return data;
            for (int i = 0; i < 256; i++) {
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
            sol::state_view lua_view(*_lua);
            sol::table info = lua_view.create_table();
            if (!_emulator) { info["ttd_available"] = false; return info; }
            auto* ctx = _emulator->GetContext();
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
            info["write_journal_enabled"]    = si.writeJournalEnabled;
            info["ttd_available"]            = true;
            return info;
        });

        // ttd_start([mode]) - start recording
        // mode: "gaming" (smaller files, no journal) or "development" (default, full journal)
        lua.set_function("ttd_start", [this](sol::optional<std::string> modeOpt) -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
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
            if (!_emulator) return;
            auto* ctx = _emulator->GetContext();
            if (ctx && ctx->pTimeTravelManager)
                ctx->pTimeTravelManager->SetEnableWriteJournal(enabled);
        });

        lua.set_function("ttd_get_journal_enabled", [this]() -> bool {
            if (!_emulator) return true;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return true;
            return ctx->pTimeTravelManager->GetEnableWriteJournal();
        });

        lua.set_function("ttd_stop", [this]() {
            if (!_emulator) return;
            auto* ctx = _emulator->GetContext();
            if (ctx && ctx->pTimeTravelManager)
                ctx->pTimeTravelManager->StopRecording();
        });

        lua.set_function("ttd_invalidate", [this](sol::optional<std::string> reason) {
            if (!_emulator) return;
            auto* ctx = _emulator->GetContext();
            if (ctx && ctx->pTimeTravelManager)
                ctx->pTimeTravelManager->InvalidateSession(
                    reason.value_or("lua invalidate").c_str());
        });

        lua.set_function("ttd_seek", [this](uint64_t frame, sol::optional<uint32_t> tInFrameOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!_emulator) { result["reached"] = false; return result; }
            auto* ctx = _emulator->GetContext();
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
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            return ctx->pTimeTravelManager->StepBackFrame();
        });

        lua.set_function("ttd_step_forward", [this]() -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            return ctx->pTimeTravelManager->StepForwardFrame();
        });

        lua.set_function("ttd_resume", [this](sol::optional<uint64_t> frameOpt, sol::optional<uint32_t> tInFrameOpt) -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            ttd::TTDTimePoint from = ctx->pTimeTravelManager->CurrentPosition();
            if (frameOpt)
                from.frame = *frameOpt;
            from.tInFrame = tInFrameOpt.value_or(0);
            return ctx->pTimeTravelManager->ResumeRecordingFrom(from);
        });

        lua.set_function("ttd_position", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!_emulator) { result["error"] = "no emulator"; return result; }
            auto* ctx = _emulator->GetContext();
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
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!_emulator) return result;
            auto* ctx = _emulator->GetContext();
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
        // Phase 4 — Reverse search + dump + instruction step
        // -----------------------------------------------------------------

        lua.set_function("ttd_dump", [this](const std::string& path) -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
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
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            result["ok"] = false;
            if (!_emulator) { result["error"] = "no emulator"; return result; }
            auto* ctx = _emulator->GetContext();
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

        lua.set_function("ttd_find_last", [this](uint16_t addr,
                                                   sol::optional<std::string> accessOpt,
                                                   sol::optional<uint8_t> valueOpt,
                                                   sol::optional<uint16_t> pcFromOpt,
                                                   sol::optional<uint16_t> pcToOpt,
                                                   sol::optional<uint64_t> beforeFrameOpt,
                                                   sol::optional<uint32_t> beforeTinOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!_emulator) { result["found"] = false; return result; }
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) { result["found"] = false; return result; }

            ttd::TTDSearchQuery q;
            q.addrFrom = q.addrTo = addr;
            q.access = ttd::TTDAccessTypeFromString(
                accessOpt.value_or("write").c_str());
            if (valueOpt) { q.hasValueFilter = true; q.value = *valueOpt; }
            if (pcFromOpt) { q.hasPcFilter = true; q.pcFrom = *pcFromOpt; q.pcTo = pcToOpt.value_or(0xFFFF); }
            const uint32_t frameT = ctx->config.frame;
            if (beforeFrameOpt)
                q.beforeGlobalT = static_cast<uint64_t>(*beforeFrameOpt) * frameT
                                  + beforeTinOpt.value_or(0);

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
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            return ctx->pTimeTravelManager->StepBackInstruction();
        });

        lua.set_function("ttd_step_instruction_forward", [this]() -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            return ctx->pTimeTravelManager->StepForwardInstruction();
        });

        // -----------------------------------------------------------------
        // Phase 4 — Reverse execution (multi-step)
        // -----------------------------------------------------------------

        lua.set_function("ttd_reverse_step", [this](sol::optional<uint32_t> countOpt) -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            return ctx->pTimeTravelManager->ReverseStepInstructions(
                countOpt.value_or(1));
        });

        lua.set_function("ttd_reverse_step_tstates", [this](uint64_t tstates) -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pTimeTravelManager) return false;
            return ctx->pTimeTravelManager->ReverseStepTStates(tstates);
        });

        lua.set_function("ttd_reverse_continue", [this](sol::table pcsTable) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!_emulator) { result["matched"] = false; return result; }
            auto* ctx = _emulator->GetContext();
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
    }

    void setEmulator(Emulator* emulator) { _emulator = emulator; }
    void setLuaState(sol::state* lua) { _lua = lua; }

    void unregisterType(sol::state& lua)
    {
        // No specific cleanup needed
    }
    /// endregion </Lua SOL lifecycle>
};