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
#include <emulator/io/tape/tape.h>
#include <tapeaudio/tapeaudioimporter.h>
#include <tapeaudio/tapeaudiorenderer.h>
#include <emulator/video/screen.h>
#include <emulator/sound/soundmanager.h>
#include <emulator/sound/chips/soundchip_ay8910.h>
#include <base/featuremanager.h>
#include <debugger/disassembler/z80disasm.h>
#include <debugger/debugmanager.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <debugger/labels/labelmanager.h>
#include <debugger/analyzers/analyzermanager.h>
#include <debugger/analyzers/trdos/trdosanalyzer.h>
#include <debugger/analyzers/rom-print/screenocr.h>
#include <emulator/video/screencapture.h>
#include <emulator/cpu/opcode_profiler.h>
#include <debugger/keyboard/debugkeyboardmanager.h>
#include <debugger/ttd/timetravelmanager.h>
#include <debugger/ttd/ttd_probe.h>

#include <chrono>
#include <fstream>
#include <optional>
#include <thread>
#include <debugger/ttd/ttd_external_events.h>
#include "../../../automation.h"
#include "../bindings/python_porttrace.h"

namespace py = pybind11;

/// @brief Python bindings for Emulator class and related functionality
/// Provides comprehensive emulator control matching CLI and WebAPI interfaces
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
                return Z80::SetRegisterValue(z80, name, value);
            }, "Set register value by name", py::arg("name"), py::arg("value"))

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
            }, "Read block of bytes from memory", py::arg("addr"), py::arg("len"))
            .def("mem_write_block", [](Emulator& self, uint16_t addr, py::bytes data) {
                Memory* mem = self.GetMemory();
                if (!mem) return;
                std::string bytes = data;
                for (size_t i = 0; i < bytes.size(); i++) {
                    mem->MemoryWriteFast((addr + i) & 0xFFFF, static_cast<uint8_t>(bytes[i]));
                }
            }, "Write block of bytes to memory", py::arg("addr"), py::arg("data"))
            
            // Physical page access (ram/rom/cache/misc)
            .def("page_read", [](Emulator& self, const std::string& type, int page, int offset) -> int {
                Memory* mem = self.GetMemory();
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
            }, "Read byte from physical page", py::arg("type"), py::arg("page"), py::arg("offset"))
            .def("page_write", [](Emulator& self, const std::string& type, int page, int offset, uint8_t value) {
                Memory* mem = self.GetMemory();
                if (!mem) return;
                uint8_t* pagePtr = nullptr;
                if (type == "ram" && page < MAX_RAM_PAGES)
                    pagePtr = mem->RAMPageAddress(page);
                else if (type == "rom" && page < MAX_ROM_PAGES)
                    pagePtr = mem->ROMPageHostAddress(page);  // Allows ROM patching
                else if (type == "cache" && page < MAX_CACHE_PAGES)
                    pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
                else if (type == "misc" && page < MAX_MISC_PAGES)
                    pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
                if (pagePtr && offset >= 0 && offset < PAGE_SIZE) {
                    pagePtr[offset] = value;
                }
            }, "Write byte to physical page", py::arg("type"), py::arg("page"), py::arg("offset"), py::arg("value"))
            .def("page_read_block", [](Emulator& self, const std::string& type, int page, int offset, int len) -> py::bytes {
                Memory* mem = self.GetMemory();
                if (!mem) return py::bytes("");
                uint8_t* pagePtr = nullptr;
                if (type == "ram" && page < MAX_RAM_PAGES)
                    pagePtr = mem->RAMPageAddress(page);
                else if (type == "rom" && page < MAX_ROM_PAGES)
                    pagePtr = mem->ROMPageHostAddress(page);
                else if (type == "cache" && page < MAX_CACHE_PAGES)
                    pagePtr = mem->CacheBase() + (page * PAGE_SIZE);
                else if (type == "misc" && page < MAX_MISC_PAGES)
                    pagePtr = mem->MiscBase() + (page * PAGE_SIZE);
                if (!pagePtr) return py::bytes("");
                // Clamp to page boundary
                if (offset < 0) offset = 0;
                if (offset >= PAGE_SIZE) return py::bytes("");
                if (offset + len > PAGE_SIZE) len = PAGE_SIZE - offset;
                return py::bytes(reinterpret_cast<char*>(pagePtr + offset), len);
            }, "Read block from physical page", py::arg("type"), py::arg("page"), py::arg("offset"), py::arg("len"))
            .def("page_write_block", [](Emulator& self, const std::string& type, int page, int offset, py::bytes data) {
                Memory* mem = self.GetMemory();
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
                if (!pagePtr) return;
                std::string bytes = data;
                if (offset < 0 || offset >= PAGE_SIZE) return;
                // Clamp to page boundary
                size_t maxLen = PAGE_SIZE - offset;
                size_t writeLen = std::min(bytes.size(), maxLen);
                std::memcpy(pagePtr + offset, bytes.data(), writeLen);
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
                return fm ? fm->setFeature(name, enabled) : false;
            }, "Set feature state")
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
                auto* ctx = self.GetContext();
                if (ctx && ctx->pTape) {
                    ctx->pTape->reset();
                    ctx->coreState.tapeFilePath = "";
                    return true;
                }
                return false;
            }, "Eject tape")
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
                
                uint16_t addr = address < 0 ? ctx->pCore->GetZ80()->pc : static_cast<uint16_t>(address);
                if (count < 1) count = 10;
                if (count > 100) count = 100;
                
                for (int i = 0; i < count; ++i) {
                    std::vector<uint8_t> buffer;
                    for (int j = 0; j < 4; ++j) {
                        buffer.push_back(memory->MemoryReadFast(static_cast<uint16_t>(addr + j), false));
                    }
                    
                    uint8_t cmdLen = 0;
                    DecodedInstruction decoded;
                    std::string mnemonic = disasm->disassembleSingleCommand(buffer, addr, &cmdLen, &decoded);
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
                    if (decoded.hasJump || decoded.hasRelativeJump) {
                        instr["target"] = decoded.hasRelativeJump ? decoded.relJumpAddr : decoded.jumpAddr;
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
                    if (decoded.hasJump || decoded.hasRelativeJump) {
                        instr["target"] = decoded.hasRelativeJump ? decoded.relJumpAddr : decoded.jumpAddr;
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
            
            // Capture operations
            .def("capture_ocr", [](Emulator& self) -> std::string {
                return ScreenOCR::ocrScreen(self.GetId());
            }, "OCR text from screen (32x24 chars)")
            .def("capture_screen", [](Emulator& self, const std::string& format, bool fullFramebuffer) -> py::dict {
                py::dict result;
                CaptureMode mode = fullFramebuffer ? CaptureMode::FullFramebuffer : CaptureMode::ScreenOnly;
                auto capture = ScreenCapture::captureScreen(self.GetId(), format, mode);
                result["success"] = capture.success;
                result["format"] = capture.format;
                result["width"] = capture.width;
                result["height"] = capture.height;
                result["size"] = capture.originalSize;
                result["data"] = capture.base64Data;
                if (!capture.success) {
                    result["error"] = capture.errorMessage;
                }
                return result;
            }, "Capture screen as image", py::arg("format") = "gif", py::arg("full") = false)
            
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
                ttd::TTDSessionInfo si = mgr->GetSessionInfo();
                info["state"]                    = ttd::TTDSessionStateToString(si.state);
                info["session_start_frame"]      = py::cast(si.sessionStartFrame);
                info["current_end_frame"]        = py::cast(si.currentEndFrame);
                info["checkpoint_count"]         = py::cast(si.checkpointCount);
                info["page_store_bytes"]         = py::cast(si.pageStoreBytes);
                info["page_store_used_bytes"]    = py::cast(si.pageStoreUsedBytes);
                info["baseline_frames_captured"] = py::cast(si.baselineFramesCaptured);
                info["session_heap_bytes"]       = py::cast(si.sessionHeapBytes);
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
                info["ttd_available"]            = true;
                return info;
            }, "Get TTD session status")

            .def("ttd_start", [](Emulator& self) -> bool {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager) return false;
                return ctx->pTimeTravelManager->StartRecording();
            }, "Start TTD recording")

            .def("ttd_stop", [](Emulator& self) {
                auto* ctx = self.GetContext();
                if (ctx && ctx->pTimeTravelManager)
                    ctx->pTimeTravelManager->StopRecording();
            }, "Stop TTD recording (history retained)")

            .def("ttd_invalidate", [](Emulator& self, const std::string& reason) {
                auto* ctx = self.GetContext();
                if (ctx && ctx->pTimeTravelManager)
                    ctx->pTimeTravelManager->InvalidateSession(reason.c_str());
            }, "Drop all TTD history", py::arg("reason") = "python invalidate")

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
                ttd::TTDTimePoint from = ctx->pTimeTravelManager->CurrentPosition();
                if (!frameObj.is_none())
                    from.frame = frameObj.cast<uint64_t>();
                from.tInFrame = tInFrame;
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
                for (const auto& e : journal.Events())
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
            }, "Load a .ttd session for playback (seek to position the emulator)", py::arg("path"))

            .def("ttd_find_last", [](Emulator& self, uint16_t addr,
                                      const std::string& access,
                                      py::object valueObj,
                                      py::object pcFromObj,
                                      py::object pcToObj,
                                      py::object beforeFrameObj,
                                      uint32_t beforeTin,
                                      py::object physPageObj) -> py::object {
                auto* ctx = self.GetContext();
                if (!ctx || !ctx->pTimeTravelManager) return py::none();

                ttd::TTDSearchQuery q;
                q.addrFrom = q.addrTo = addr;
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
                    q.physPage = static_cast<uint8_t>(physPageObj.cast<int>());
                    q.hasPhysPageFilter = true;
                }

                const uint32_t frameT = ctx->config.frame;
                if (!beforeFrameObj.is_none())
                {
                    uint64_t f = beforeFrameObj.cast<uint64_t>();
                    q.beforeGlobalT = f * frameT + beforeTin;
                }

                ttd::TTDExternalEvent marker;
                auto result = ctx->pTimeTravelManager->FindLastAccess(q, &marker);
                if (!result)
                    return py::none();

                py::dict r;
                r["frame"]     = py::cast(result->time.frame);
                r["tinframe"]  = py::cast(result->time.tInFrame);
                r["pc"]        = py::cast(result->pc);
                r["value"]     = py::cast(result->value);
                r["phys_page"] = py::cast(result->physPage);
                r["access"]    = ttd::TTDAccessTypeToString(result->access);
                return r;
            }, "Reverse search: find last access at address",
               py::arg("addr"),
               py::arg("access") = "write",
               py::arg("value") = py::none(),
               py::arg("pc_from") = py::none(),
               py::arg("pc_to") = py::none(),
               py::arg("before_frame") = py::none(),
               py::arg("before_tin") = 0,
               py::arg("phys_page") = py::none())

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
                if (!r.matched)
                    return py::none();
                py::dict d;
                d["matched"]  = true;
                d["pc"]       = r.pc;
                d["frame"]    = r.arrivedAt.frame;
                d["tinframe"] = r.arrivedAt.tInFrame;
                return d;
            }, "Run backward until any PC matches; returns dict or None",
               py::arg("pcs"));

        // Port trace (PDR) bindings — runtime feature "porttrace"
        registerPortTraceBindings(emulatorClass);
    }
}
