#pragma once

#include <sol/sol.hpp>
#include <emulator/emulator.h>
#include <emulator/memory/memory.h>
#include <emulator/io/fdc/fdd.h>
#include <emulator/io/fdc/diskimage.h>
#include <debugger/analyzers/basic-lang/basicencoder.h>
#include <debugger/analyzers/basic-lang/basicextractor.h>

class LuaEmulator
{
    /// region <Fields>
protected:
    Emulator* _emulator = nullptr;
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
        // Register the Emulator class with Sol2
        lua.new_usertype<Emulator>(
          "Emulator",
          "start", &Emulator::Start,
          "stop", &Emulator::Stop,
          "pause", &Emulator::Pause,
          "resume", &Emulator::Resume,
          "is_running", &Emulator::IsRunning,
          "is_paused", &Emulator::IsPaused,
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

        // Disk inspection functions (simplified - complex operations via WebAPI)
        // disk_is_inserted(drive) -> bool
        lua.set_function("disk_is_inserted", [this](int drive) -> bool {
            if (!_emulator || drive < 0 || drive > 3) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->coreState.diskDrives[drive]) return false;
            return ctx->coreState.diskDrives[drive]->isDiskInserted();
        });

        // disk_get_path(drive) -> string
        lua.set_function("disk_get_path", [this](int drive) -> std::string {
            if (!_emulator || drive < 0 || drive > 3) return "";
            auto* ctx = _emulator->GetContext();
            if (!ctx) return "";
            return ctx->coreState.diskFilePaths[drive];
        });

        // disk_create(drive, [cylinders], [sides]) -> bool
        // Creates a blank unformatted disk and inserts it into the specified drive
        lua.set_function("disk_create", [this](int drive, sol::optional<int> cyl, sol::optional<int> sides) -> bool {
            if (!_emulator || drive < 0 || drive > 3) return false;
            auto* ctx = _emulator->GetContext();
            if (!ctx || !ctx->coreState.diskDrives[drive]) return false;
            
            uint8_t cylinders = cyl.value_or(80);
            uint8_t numSides = sides.value_or(2);
            
            // Validate parameters
            if (cylinders != 40 && cylinders != 80) return false;
            if (numSides != 1 && numSides != 2) return false;
            
            // Create blank disk image
            DiskImage* diskImage = new DiskImage(cylinders, numSides);
            
            // Insert into drive
            FDD* fdd = ctx->coreState.diskDrives[drive];
            fdd->insertDisk(diskImage);
            
            // Update path tracking
            ctx->coreState.diskFilePaths[drive] = "<blank>";
            
            return true;
        });

        // BASIC control functions
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
            
            Memory* memory = _emulator->GetMemory();
            if (!memory) {
                result["success"] = false;
                result["message"] = "Memory subsystem not available";
                return result;
            }
            
            std::string command = cmd.value_or("RUN");
            
            // Check if we're on 128K menu
            auto state = BasicEncoder::detectState(memory);
            if (state == BasicEncoder::BasicState::Menu128K) {
                BasicEncoder::navigateToBasic128K(memory);
                BasicEncoder::injectEnter(memory);
                result["success"] = true;
                result["message"] = "Detected 128K menu, navigating to BASIC. Retry command after transition.";
                result["state"] = "menu_transition";
                return result;
            }
            
            // Inject and execute command
            std::string injResult = BasicEncoder::injectCommand(memory, command);
            BasicEncoder::injectEnter(memory);
            
            result["success"] = true;
            result["command"] = command;
            result["message"] = "Command injected and executed";
            result["basic_mode"] = (state == BasicEncoder::BasicState::Basic48K) ? "48K" : "128K";
            
            return result;
        });

        // basic_inject(program) -> bool
        // Inject a BASIC program into memory without executing
        lua.set_function("basic_inject", [this](const std::string& program) -> bool {
            if (!_emulator) return false;
            Memory* memory = _emulator->GetMemory();
            if (!memory) return false;
            
            BasicEncoder encoder;
            return encoder.loadProgram(memory, program);
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

        // Set the emulator instance in the Lua environment
        lua["emulator"] = _emulator;
    }

    void unregisterType(sol::state& lua)
    {
        // No specific cleanup needed
    }
    /// endregion </Lua SOL lifecycle>

};