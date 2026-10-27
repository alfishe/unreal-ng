#pragma once

/// @file luahookhost.h
/// @brief The Lua hook state of one emulator (scripthookhost.h, docs/features/script-callbacks.md "Hook state"):
/// a sol::state of its own, separate from the script's, that runs the frame and sync callbacks on the emulation
/// thread. It has no io, os or package library; it reads and writes the machine through a small API (peek, poke,
/// reg, set_reg, frame, tstate) and shares values with the scripts through shared_get / shared_set. A count hook
/// every kInstructionStep VM instructions stops a call over its instruction budget.

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <sol/sol.hpp>

#include "debugger/callbacks/scripthookhost.h"

class LuaHookHost : public IScriptHookHost
{
public:
    static constexpr int kInstructionStep = 1000;

    explicit LuaHookHost(EmulatorContext* context);
    ~LuaHookHost() override;

    bool Compile(uint32_t id, const std::string& chunk, std::string& error) override;
    HookCallResult Call(uint32_t id, const CallbackHit& hit, const std::string& event,
                        uint64_t instructionBudget) override;
    void Forget(uint32_t id) override;

    static std::unique_ptr<IScriptHookHost> Create(EmulatorContext* context);

private:
    void RegisterApi();
    /// Why the machine may not be changed now (a TTD session records or is browsed); empty: it may
    std::string MutationRefusal() const;
    static void CountHook(lua_State* L, lua_Debug* debug);

    EmulatorContext* _context = nullptr;
    std::mutex _mutex;  ///< the state: Compile and Forget from any thread, Call on the emulation thread
    std::unique_ptr<sol::state> _lua;
    std::map<uint32_t, sol::protected_function> _functions;
    // The running call (the count hook reads them)
    uint64_t _instructions = 0;
    uint64_t _budget = 0;
    bool _aborted = false;
};
