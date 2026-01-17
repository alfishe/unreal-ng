#pragma once

#include <sol/sol.hpp>
#include <emulator/emulator.h>
#include <emulator/memory/memory.h>
#include <emulator/io/fdc/fdd.h>
#include <emulator/io/fdc/diskimage.h>

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

        // Set the emulator instance in the Lua environment
        lua["emulator"] = _emulator;
    }

    void unregisterType(sol::state& lua)
    {
        // No specific cleanup needed
    }
    /// endregion </Lua SOL lifecycle>

};