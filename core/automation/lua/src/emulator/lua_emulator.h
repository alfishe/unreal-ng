#pragma once

#include <sol/sol.hpp>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <debugger/breakpoints/breakpointmanager.h>
#include <base/featuremanager.h>

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
        // Register EmulatorManager as a global singleton accessor
        lua["EmulatorManager"] = lua.create_table_with(
            "getInstance", []() { return EmulatorManager::GetInstance(); },
            "create", [](sol::optional<std::string> symbolicId) -> Emulator* {
                auto* mgr = EmulatorManager::GetInstance();
                auto shared = mgr->CreateEmulator(symbolicId.value_or(""));
                return shared.get();
            },
            "get", [](const std::string& id) -> Emulator* {
                auto* mgr = EmulatorManager::GetInstance();
                auto shared = mgr->GetEmulator(id);
                return shared.get();
            },
            "getMostRecent", []() -> Emulator* {
                auto* mgr = EmulatorManager::GetInstance();
                auto shared = mgr->GetMostRecentEmulator();
                return shared.get();
            },
            "list", []() -> std::vector<std::string> {
                auto* mgr = EmulatorManager::GetInstance();
                return mgr->GetEmulatorIds();
            },
            "remove", [](const std::string& id) -> bool {
                auto* mgr = EmulatorManager::GetInstance();
                return mgr->RemoveEmulator(id);
            },
            "shutdown", []() {
                auto* mgr = EmulatorManager::GetInstance();
                mgr->ShutdownAllEmulators();
            }
        );
        
        // Register the Emulator class with comprehensive API
        lua.new_usertype<Emulator>(
          "Emulator",
          
          // === Lifecycle Control ===
          "init", &Emulator::Init,
          "reset", &Emulator::Reset,
          "start", &Emulator::Start,
          "startAsync", &Emulator::StartAsync,
          "pause", &Emulator::Pause,
          "resume", &Emulator::Resume,
          "stop", &Emulator::Stop,
          
          // === Execution Control ===
          "runSingleCPUCycle", &Emulator::RunSingleCPUCycle,
          "runNCPUCycles", &Emulator::RunNCPUCycles,
          "runUntilInterrupt", &Emulator::RunUntilInterrupt,
          "stepOver", &Emulator::StepOver,
          
          // === State Query ===
          "isRunning", &Emulator::IsRunning,
          "isPaused", &Emulator::IsPaused,
          "isDebug", &Emulator::IsDebug,
          "getState", &Emulator::GetState,
          "setState", &Emulator::SetState,
          
          // === Identity ===
          "getId", &Emulator::GetId,
          "getUUID", &Emulator::GetUUID,
          "getSymbolicId", &Emulator::GetSymbolicId,
          "setSymbolicId", &Emulator::SetSymbolicId,
          
          // === Info & Statistics ===
          "getStatistics", &Emulator::GetStatistics,
          "getInstanceInfo", &Emulator::GetInstanceInfo,
          "getUptimeString", &Emulator::GetUptimeString,
          
          // === Performance ===
          "getSpeed", &Emulator::GetSpeed,
          "setSpeed", &Emulator::SetSpeed,
          "setSpeedMultiplier", &Emulator::SetSpeedMultiplier,
          "enableTurboMode", &Emulator::EnableTurboMode,
          "disableTurboMode", &Emulator::DisableTurboMode,
          "isTurboMode", &Emulator::IsTurboMode,
          
          // === File Operations ===
          "loadSnapshot", &Emulator::LoadSnapshot,
          "loadTape", &Emulator::LoadTape,
          "loadDisk", &Emulator::LoadDisk,
          "loadROM", &Emulator::LoadROM,
          
          // === Debug Control ===
          "debugOn", &Emulator::DebugOn,
          "debugOff", &Emulator::DebugOff,
          
          // === Subsystem Access ===
          "getBreakpointManager", &Emulator::GetBreakpointManager,
          "getFeatureManager", &Emulator::GetFeatureManager,
          
          // === Memory Access (high-level helpers) ===
          "readMemory", [](Emulator& emu, uint16_t addr) -> uint8_t {
              return emu.GetMemory()->DirectReadFromZ80Memory(addr);
          },
          "writeMemory", [](Emulator& emu, uint16_t addr, uint8_t value) {
              emu.GetMemory()->DirectWriteToZ80Memory(addr, value);
          },
          
          // === CPU State Access (high-level helpers) ===
          "getPC", [](Emulator& emu) -> uint16_t {
              auto* state = emu.GetZ80State();
              return state ? state->pc : 0;
          },
          "setPC", [](Emulator& emu, uint16_t value) {
              auto* state = emu.GetZ80State();
              if (state) state->pc = value;
          },
          "getSP", [](Emulator& emu) -> uint16_t {
              auto* state = emu.GetZ80State();
              return state ? state->sp : 0;
          },
          "getAF", [](Emulator& emu) -> uint16_t {
              auto* state = emu.GetZ80State();
              return state ? state->af : 0;
          },
          "getBC", [](Emulator& emu) -> uint16_t {
              auto* state = emu.GetZ80State();
              return state ? state->bc : 0;
          },
          "getDE", [](Emulator& emu) -> uint16_t {
              auto* state = emu.GetZ80State();
              return state ? state->de : 0;
          },
          "getHL", [](Emulator& emu) -> uint16_t {
              auto* state = emu.GetZ80State();
              return state ? state->hl : 0;
          }
        );
        
        // Register BreakpointManager for debugging control
        lua.new_usertype<BreakpointManager>(
            "BreakpointManager",
            // Add breakpoints
            "addExecutionBreakpoint", &BreakpointManager::AddExecutionBreakpoint,
            "addMemReadBreakpoint", &BreakpointManager::AddMemReadBreakpoint,
            "addMemWriteBreakpoint", &BreakpointManager::AddMemWriteBreakpoint,
            "addPortInBreakpoint", &BreakpointManager::AddPortInBreakpoint,
            "addPortOutBreakpoint", &BreakpointManager::AddPortOutBreakpoint,
            // Remove breakpoints
            "removeBreakpointByID", &BreakpointManager::RemoveBreakpointByID,
            "removeBreakpointByAddress", &BreakpointManager::RemoveBreakpointByAddress,
            "clearBreakpoints", &BreakpointManager::ClearBreakpoints,
            // Activate/deactivate
            "activateBreakpoint", &BreakpointManager::ActivateBreakpoint,
            "deactivateBreakpoint", &BreakpointManager::DeactivateBreakpoint,
            "activateAll", &BreakpointManager::ActivateAllBreakpoints,
            "deactivateAll", &BreakpointManager::DeactivateAllBreakpoints,
            // Groups
            "activateGroup", &BreakpointManager::ActivateBreakpointGroup,
            "deactivateGroup", &BreakpointManager::DeactivateBreakpointGroup,
            "removeGroup", &BreakpointManager::RemoveBreakpointGroup
        );
        
        // Register FeatureManager for runtime feature control
        lua.new_usertype<FeatureManager>(
            "FeatureManager",
            "setFeature", &FeatureManager::setFeature,
            "setMode", &FeatureManager::setMode,
            "getMode", &FeatureManager::getMode,
            "isEnabled", &FeatureManager::isEnabled,
            "listFeatures", &FeatureManager::listFeatures
        );

        // Set the emulator instance in the Lua environment (backward compatibility)
        lua["emulator"] = _emulator;
    }

    void unregisterType(sol::state& lua)
    {
        // No specific cleanup needed
    }
    /// endregion </Lua SOL lifecycle>

};