#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/memory/memory.h>
#include <emulator/memory/memoryaccesstracker.h>
#include <emulator/cpu/z80.h>
#include <emulator/io/fdc/fdd.h>
#include <emulator/io/fdc/diskimage.h>
#include <emulator/video/screen.h>
#include <emulator/sound/soundmanager.h>
#include <emulator/sound/chips/soundchip_ay8910.h>
#include <base/featuremanager.h>
#include <debugger/debugmanager.h>
#include <debugger/breakpoints/breakpointmanager.h>

namespace py = pybind11;

/// @brief Python bindings for Emulator class and related functionality
/// Provides comprehensive emulator control matching CLI and WebAPI interfaces
namespace PythonBindings
{
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

        m.def("emu_select", [](const std::string& id) -> bool {
            auto* mgr = EmulatorManager::GetInstance();
            return mgr->SetSelectedEmulatorId(id);
        }, "Select an emulator by ID");

        // Emulator class bindings
        py::class_<Emulator>(m, "Emulator")
            // Lifecycle control
            .def("start", &Emulator::Start, "Start emulator execution")
            .def("start_async", &Emulator::StartAsync, "Start emulator asynchronously")
            .def("stop", &Emulator::Stop, "Stop emulator")
            .def("pause", [](Emulator& self) { self.Pause(true); }, "Pause emulator")
            .def("resume", [](Emulator& self) { self.Resume(true); }, "Resume emulator")
            .def("reset", &Emulator::Reset, "Reset emulator")
            
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
                    regs["r"] = (z80->r_hi << 7) | (z80->r_low & 0x7F);
                }
                return regs;
            }, "Get all registers as dictionary")
            
            // Memory access (isExecution=false for data reads)
            .def("mem_read", [](Emulator& self, uint16_t addr) -> uint8_t {
                Memory* mem = self.GetMemory();
                return mem ? mem->MemoryReadFast(addr, false) : 0;
            }, "Read byte from memory")
            .def("mem_write", [](Emulator& self, uint16_t addr, uint8_t value) {
                Memory* mem = self.GetMemory();
                if (mem) mem->MemoryWriteFast(addr, value);
            }, "Write byte to memory")
            .def("mem_read_word", [](Emulator& self, uint16_t addr) -> uint16_t {
                Memory* mem = self.GetMemory();
                if (!mem) return 0;
                return mem->MemoryReadFast(addr, false) | (mem->MemoryReadFast(addr + 1, false) << 8);
            }, "Read 16-bit word from memory")
            .def("mem_write_word", [](Emulator& self, uint16_t addr, uint16_t value) {
                Memory* mem = self.GetMemory();
                if (!mem) return;
                mem->MemoryWriteFast(addr, value & 0xFF);
                mem->MemoryWriteFast(addr + 1, (value >> 8) & 0xFF);
            }, "Write 16-bit word to memory")
            .def("mem_read_block", [](Emulator& self, uint16_t addr, uint16_t len) -> py::bytes {
                Memory* mem = self.GetMemory();
                if (!mem) return py::bytes("");
                std::string data;
                data.reserve(len);
                for (uint16_t i = 0; i < len; i++) {
                    data.push_back(static_cast<char>(mem->MemoryReadFast(addr + i, false)));
                }
                return py::bytes(data);
            }, "Read block of bytes from memory")
            
            // Feature management (using correct FeatureManager API)
            .def("feature_get", [](Emulator& self, const std::string& name) -> bool {
                FeatureManager* fm = self.GetFeatureManager();
                return fm ? fm->isEnabled(name) : false;
            }, "Get feature state")
            .def("feature_set", [](Emulator& self, const std::string& name, bool enabled) -> bool {
                FeatureManager* fm = self.GetFeatureManager();
                return fm ? fm->setFeature(name, enabled) : false;
            }, "Set feature state")
            .def("feature_list", [](Emulator& self) -> py::dict {
                py::dict features;
                FeatureManager* fm = self.GetFeatureManager();
                if (fm) {
                    features["sound"] = fm->isEnabled("sound");
                    features["sharedmemory"] = fm->isEnabled("sharedmemory");
                    features["calltrace"] = fm->isEnabled("calltrace");
                    features["breakpoints"] = fm->isEnabled("breakpoints");
                    features["memorytracking"] = fm->isEnabled("memorytracking");
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
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->coreState.diskDrives[drive]) return false;
                ctx->coreState.diskDrives[drive]->ejectDisk();
                ctx->coreState.diskFilePaths[drive] = "";
                return true;
            }, "Eject disk from drive")
            .def("disk_create", [](Emulator& self, int drive, int cylinders, int sides) -> bool {
                if (drive < 0 || drive > 3) return false;
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->coreState.diskDrives[drive]) return false;
                if (cylinders != 40 && cylinders != 80) return false;
                if (sides != 1 && sides != 2) return false;
                DiskImage* diskImage = new DiskImage(cylinders, sides);
                ctx->coreState.diskDrives[drive]->insertDisk(diskImage);
                ctx->coreState.diskFilePaths[drive] = "<blank>";
                return true;
            }, "Create blank disk", py::arg("drive"), py::arg("cylinders") = 80, py::arg("sides") = 2)
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
            .def("step", [](Emulator& self, bool skipBreakpoints) {
                self.RunSingleCPUCycle(skipBreakpoints);
            }, "Execute single CPU instruction", py::arg("skip_breakpoints") = true)
            .def("steps", [](Emulator& self, unsigned count, bool skipBreakpoints) {
                self.RunNCPUCycles(count, skipBreakpoints);
            }, "Execute N CPU instructions", py::arg("count"), py::arg("skip_breakpoints") = false)
            .def("stepover", &Emulator::StepOver, "Step over call instructions")
            
            // Tape operations
            .def("tape_load", &Emulator::LoadTape, "Load tape file", py::arg("path"))
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
                if (ctx && ctx->pTape) {
                    ctx->pTape->startTape();
                    return true;
                }
                return false;
            }, "Start tape playback")
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
                if (ctx && ctx->pTape) {
                    ctx->pTape->reset();
                    return true;
                }
                return false;
            }, "Rewind tape to beginning")
            .def("tape_eject", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (ctx && ctx->pTape) {
                    ctx->pTape->reset();
                    ctx->coreState.tapeFilePath = "";
                    return true;
                }
                return false;
            }, "Eject tape")
            
            // Snapshot operations
            .def("snapshot_load", &Emulator::LoadSnapshot, "Load snapshot file", py::arg("path"))
            .def("snapshot_save", &Emulator::SaveSnapshot, "Save snapshot file", py::arg("path"))
            
            // Breakpoint management
            .def("bp", [](Emulator& self, uint16_t addr) -> int {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return -1;
                BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
                return bpm ? static_cast<int>(bpm->AddExecutionBreakpoint(addr)) : -1;
            }, "Add execution breakpoint", py::arg("addr"))
            .def("bp_read", [](Emulator& self, uint16_t addr) -> int {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return -1;
                BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
                return bpm ? static_cast<int>(bpm->AddMemReadBreakpoint(addr)) : -1;
            }, "Add memory read breakpoint (watchpoint)", py::arg("addr"))
            .def("bp_write", [](Emulator& self, uint16_t addr) -> int {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return -1;
                BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
                return bpm ? static_cast<int>(bpm->AddMemWriteBreakpoint(addr)) : -1;
            }, "Add memory write breakpoint (watchpoint)", py::arg("addr"))
            .def("bp_port_in", [](Emulator& self, uint16_t port) -> int {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return -1;
                BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
                return bpm ? static_cast<int>(bpm->AddPortInBreakpoint(port)) : -1;
            }, "Add port IN breakpoint", py::arg("port"))
            .def("bp_port_out", [](Emulator& self, uint16_t port) -> int {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pDebugManager) return -1;
                BreakpointManager* bpm = ctx->pDebugManager->GetBreakpointsManager();
                return bpm ? static_cast<int>(bpm->AddPortOutBreakpoint(port)) : -1;
            }, "Add port OUT breakpoint", py::arg("port"))
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
                info["sectors_per_track"] = 16;
                info["sector_size"] = 256;
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
                auto* sec = track->getSector(sector);
                if (!sec) return py::bytes();
                return py::bytes(reinterpret_cast<char*>(sec->data), 256);
            }, "Read sector data (256 bytes)", py::arg("drive"), py::arg("cyl"), py::arg("side"), py::arg("sector"))
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
            }, "Check if call trace is enabled");
    }
}
