#pragma once

#include <sol/sol.hpp>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/memory/memory.h>
#include <emulator/io/fdc/fdd.h>
#include <emulator/io/fdc/diskimage.h>
#include <debugger/analyzers/basic-lang/basicencoder.h>
#include <debugger/analyzers/basic-lang/basicextractor.h>
#include <emulator/io/tape/tape.h>
#include <emulator/cpu/z80.h>
#include <emulator/video/screen.h>
#include <emulator/sound/soundmanager.h>
#include <emulator/sound/chips/soundchip_ay8910.h>
#include <debugger/debugmanager.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/analyzers/analyzermanager.h>
#include <debugger/analyzers/trdos/trdosanalyzer.h>
#include <debugger/disassembler/z80disasm.h>
#include <debugger/analyzers/rom-print/screenocr.h>
#include <emulator/video/screencapture.h>
#include <emulator/cpu/opcode_profiler.h>

class LuaEmulator
{
    /// region <Fields>
protected:
    Emulator* _emulator = nullptr;
    sol::state* _lua = nullptr;
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
            },
            // Memory access
            "read_memory", [](Emulator& emu, uint16_t addr) -> uint8_t {
                auto* memory = emu.GetMemory();
                return memory ? memory->DirectReadFromZ80Memory(addr) : 0;
            },
            "write_memory", [](Emulator& emu, uint16_t addr, uint8_t value) {
                auto* memory = emu.GetMemory();
                if (memory) memory->DirectWriteToZ80Memory(addr, value);
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
            std::string selectedId = mgr->GetSelectedEmulatorId();
            if (selectedId.empty()) return nullptr;
            auto emu = mgr->GetEmulator(selectedId);
            return emu.get();
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

        lua.set_function("get_registers", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table regs = lua_view.create_table();
            if (_emulator) {
                Z80State* z80 = _emulator->GetZ80State();
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


        // basic_run([command]) -> table {success, message, basic_mode}
        // Execute a BASIC command (defaults to "RUN" if no command specified)
        lua.set_function("basic_run", [this](sol::this_state L, sol::optional<std::string> cmd) -> sol::table {
            sol::state_view lua(L);
            sol::table result = lua.create_table();
            if (!_emulator) {
                result["success"] = false;
                result["message"] = "No emulator available";
                return result;
            }
            
            std::string command = cmd.value_or("RUN");
            
            // Use new runCommand API - handles menu navigation automatically
            auto injResult = BasicEncoder::runCommand(_emulator, command);
            
            result["success"] = injResult.success;
            result["command"] = command;
            result["message"] = injResult.message;
            
            switch (injResult.state) {
                case BasicEncoder::BasicState::Basic48K:
                    result["basic_mode"] = "48K";
                    break;
                case BasicEncoder::BasicState::Basic128K:
                    result["basic_mode"] = "128K";
                    break;
                case BasicEncoder::BasicState::TRDOS_Active:
                case BasicEncoder::BasicState::TRDOS_SOS_Call:
                    result["basic_mode"] = "trdos";
                    break;
                default:
                    result["basic_mode"] = "unknown";
                    break;
            }
            
            return result;
        });

        // basic_inject(program) -> table {success, message, state}
        // Inject a BASIC program into memory without executing
        lua.set_function("basic_inject", [this](sol::this_state L, const std::string& program) -> sol::table {
            sol::state_view lua(L);
            sol::table result = lua.create_table();
            
            if (!_emulator) {
                result["success"] = false;
                result["message"] = "No emulator available";
                return result;
            }
            Memory* memory = _emulator->GetMemory();
            if (!memory) {
                result["success"] = false;
                result["message"] = "Memory subsystem not available";
                return result;
            }
            
            // Check state before injection
            auto state = BasicEncoder::detectState(memory);
            if (state == BasicEncoder::BasicState::TRDOS_Active || 
                state == BasicEncoder::BasicState::TRDOS_SOS_Call) {
                result["success"] = false;
                result["message"] = "TR-DOS is active. Please exit to BASIC first.";
                result["state"] = "trdos";
                return result;
            }
            if (state == BasicEncoder::BasicState::Menu128K) {
                result["success"] = false;
                result["message"] = "On 128K menu. Please enter BASIC first.";
                result["state"] = "menu128k";
                return result;
            }
            
            BasicEncoder encoder;
            bool success = encoder.loadProgram(memory, program);
            
            result["success"] = success;
            result["message"] = success ? "BASIC program injected successfully" : "Failed to inject BASIC program";
            result["state"] = (state == BasicEncoder::BasicState::Basic48K) ? "basic48k" : "basic128k";
            
            return result;
        });

        // basic_extract() -> string
        // Extract the current BASIC program from memory
        lua.set_function("basic_extract", [this]() -> std::string {
            if (!_emulator) return "";
            Memory* memory = _emulator->GetMemory();
            if (!memory) return "";
            
            BasicExtractor extractor;
            return extractor.extractFromMemory(memory);
        });

        // basic_clear() -> bool
        // Clear the BASIC program in memory (equivalent to NEW command)
        lua.set_function("basic_clear", [this]() -> bool {
            if (!_emulator) return false;
            Memory* memory = _emulator->GetMemory();
            if (!memory) return false;
            
            BasicEncoder encoder;
            return encoder.loadProgram(memory, "");
        });

        // basic_state() -> table {state, in_editor, ready_for_commands}
        // Get the current BASIC environment state
        lua.set_function("basic_state", [this](sol::this_state L) -> sol::table {
            sol::state_view lua(L);
            sol::table result = lua.create_table();
            if (!_emulator) {
                result["state"] = "unknown";
                result["in_editor"] = false;
                result["ready_for_commands"] = false;
                return result;
            }
            
            Memory* memory = _emulator->GetMemory();
            if (!memory) {
                result["state"] = "unknown";
                result["in_editor"] = false;
                result["ready_for_commands"] = false;
                return result;
            }
            
            auto state = BasicEncoder::detectState(memory);
            bool isInEditor = BasicEncoder::isInBasicEditor(memory);
            
            result["in_editor"] = isInEditor;
            
            switch (state) {
                case BasicEncoder::BasicState::Menu128K:
                    result["state"] = "menu128k";
                    result["ready_for_commands"] = false;
                    break;
                case BasicEncoder::BasicState::Basic128K:
                    result["state"] = "basic128k";
                    result["ready_for_commands"] = true;
                    break;
                case BasicEncoder::BasicState::Basic48K:
                    result["state"] = "basic48k";
                    result["ready_for_commands"] = true;
                    break;
                default:
                    result["state"] = "unknown";
                    result["ready_for_commands"] = false;
                    break;
            }
            
            return result;
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

        // Analyzer management
        lua.set_function("analyzer_list", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table analyzers = lua_view.create_table();
            if (!_emulator) return analyzers;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return analyzers;
            AnalyzerManager* am = ctx->pDebugManager->GetAnalyzerManager();
            if (!am) return analyzers;
            int i = 1;
            for (const auto& name : am->getRegisteredAnalyzers()) {
                analyzers[i++] = name;
            }
            return analyzers;
        });

        lua.set_function("analyzer_enable", [this](const std::string& name) -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return false;
            AnalyzerManager* am = ctx->pDebugManager->GetAnalyzerManager();
            return am ? am->activate(name) : false;
        });

        lua.set_function("analyzer_disable", [this](const std::string& name) -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return false;
            AnalyzerManager* am = ctx->pDebugManager->GetAnalyzerManager();
            return am ? am->deactivate(name) : false;
        });

        lua.set_function("analyzer_status", [this](sol::this_state L, const std::string& name) -> sol::table {
            sol::state_view lua(L);
            sol::table status = lua.create_table();
            if (!_emulator) return status;
            auto* ctx = _emulator->GetContext();
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
                    status["event_count"] = static_cast<int>(trdos->getEventCount());
                }
            }
            return status;
        });

        lua.set_function("analyzer_events", [this](const std::string& name, sol::optional<int> limit) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table events = lua_view.create_table();
            if (!_emulator) return events;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return events;
            AnalyzerManager* am = ctx->pDebugManager->GetAnalyzerManager();
            if (!am) return events;
            
            size_t maxEvents = limit.value_or(50);
            
            if (name == "trdos") {
                TRDOSAnalyzer* trdos = dynamic_cast<TRDOSAnalyzer*>(am->getAnalyzer(name));
                if (trdos) {
                    auto evts = trdos->getEvents();
                    size_t start = (evts.size() > maxEvents) ? evts.size() - maxEvents : 0;
                    int i = 1;
                    for (size_t j = start; j < evts.size(); j++) {
                        events[i++] = evts[j].format();
                    }
                }
            }
            return events;
        });

        lua.set_function("analyzer_clear", [this](const std::string& name) {
            if (!_emulator) return;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pDebugManager) return;
            AnalyzerManager* am = ctx->pDebugManager->GetAnalyzerManager();
            if (!am) return;
            
            if (name == "trdos") {
                TRDOSAnalyzer* trdos = dynamic_cast<TRDOSAnalyzer*>(am->getAnalyzer(name));
                if (trdos) trdos->clear();
            }
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
                std::vector<uint8_t> buffer;
                for (int j = 0; j < 4 && (currentOffset + j) < PAGE_SIZE; ++j) {
                    buffer.push_back(pageBase[currentOffset + j]);
                }
                if (buffer.size() < 4) buffer.resize(4, 0);
                
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

        // Capture operations
        lua.set_function("capture_ocr", [this]() -> std::string {
            if (!_emulator) return "";
            return ScreenOCR::ocrScreen(_emulator->GetId());
        });

        lua.set_function("capture_screen", [this](const std::string& format, bool fullFramebuffer) -> sol::table {
            sol::table result = (*_lua).create_table();
            if (!_emulator) {
                result["success"] = false;
                result["error"] = "No emulator";
                return result;
            }
            CaptureMode mode = fullFramebuffer ? CaptureMode::FullFramebuffer : CaptureMode::ScreenOnly;
            auto capture = ScreenCapture::captureScreen(_emulator->GetId(), format, mode);
            result["success"] = capture.success;
            result["format"] = capture.format;
            result["width"] = capture.width;
            result["height"] = capture.height;
            result["size"] = static_cast<int>(capture.originalSize);
            result["data"] = capture.base64Data;
            if (!capture.success) {
                result["error"] = capture.errorMessage;
            }
            return result;
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

        // Opcode profiler
        lua.set_function("profiler_start", [this]() -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pCore) return false;
            Z80* z80 = ctx->pCore->GetZ80();
            if (!z80) return false;
            OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
            if (!profiler) return false;
            FeatureManager* fm = _emulator->GetFeatureManager();
            if (fm) fm->setFeature("opcode_profiler", true);
            profiler->Start();
            return true;
        });

        lua.set_function("profiler_stop", [this]() -> bool {
            if (!_emulator) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pCore) return false;
            Z80* z80 = ctx->pCore->GetZ80();
            if (!z80) return false;
            OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
            if (!profiler) return false;
            profiler->Stop();
            return true;
        });

        lua.set_function("profiler_clear", [this]() {
            if (!_emulator) return;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pCore) return;
            Z80* z80 = ctx->pCore->GetZ80();
            if (!z80) return;
            OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
            if (profiler) profiler->Clear();
        });

        lua.set_function("profiler_status", [this]() -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!_emulator) return result;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pCore) return result;
            Z80* z80 = ctx->pCore->GetZ80();
            if (!z80) return result;
            OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
            if (!profiler) return result;
            FeatureManager* fm = _emulator->GetFeatureManager();
            auto status = profiler->GetStatus();
            result["feature_enabled"] = fm ? fm->isEnabled("opcode_profiler") : false;
            result["capturing"] = status.capturing;
            result["total_executions"] = status.totalExecutions;
            result["trace_size"] = status.traceSize;
            result["trace_capacity"] = status.traceCapacity;
            return result;
        });

        lua.set_function("profiler_counters", [this](sol::optional<size_t> limitOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!_emulator) return result;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pCore) return result;
            Z80* z80 = ctx->pCore->GetZ80();
            if (!z80) return result;
            OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
            if (!profiler) return result;
            size_t limit = limitOpt.value_or(100);
            auto counters = profiler->GetTopOpcodes(limit);
            int idx = 1;
            for (const auto& counter : counters) {
                sol::table entry = lua_view.create_table();
                entry["prefix"] = counter.prefix;
                entry["opcode"] = counter.opcode;
                entry["count"] = counter.count;
                entry["mnemonic"] = counter.mnemonic;
                result[idx++] = entry;
            }
            return result;
        });

        lua.set_function("profiler_trace", [this](sol::optional<size_t> countOpt) -> sol::table {
            sol::state_view lua_view(*_lua);
            sol::table result = lua_view.create_table();
            if (!_emulator) return result;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->pCore) return result;
            Z80* z80 = ctx->pCore->GetZ80();
            if (!z80) return result;
            OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
            if (!profiler) return result;
            size_t count = countOpt.value_or(100);
            auto trace = profiler->GetRecentTrace(count);
            int idx = 1;
            for (const auto& entry : trace) {
                sol::table item = lua_view.create_table();
                item["pc"] = entry.pc;
                item["prefix"] = entry.prefix;
                item["opcode"] = entry.opcode;
                item["flags"] = entry.flags;
                item["a"] = entry.a;
                item["frame"] = entry.frame;
                item["tstate"] = entry.tState;
                result[idx++] = item;
            }
            return result;
        });

        // Set the emulator instance in the Lua environment
        lua["emulator"] = _emulator;
    }

    void setEmulator(Emulator* emulator) { _emulator = emulator; }
    void setLuaState(sol::state* lua) { _lua = lua; }

    void unregisterType(sol::state& lua)
    {
        // No specific cleanup needed
    }
    /// endregion </Lua SOL lifecycle>
};