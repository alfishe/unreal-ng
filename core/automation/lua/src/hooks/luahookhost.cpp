#include "luahookhost.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

#include "../bindings/lua_callbackhit.h"
#include "debugger/callbacks/scriptcallbacks.h"
#include "debugger/debugmanager.h"
#include "debugger/ttd/timetravelhooks.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{
LuaHookHost*& HostOf(lua_State* L)
{
    return *static_cast<LuaHookHost**>(lua_getextraspace(L));
}
}  // namespace

LuaHookHost::LuaHookHost(EmulatorContext* context) : _context(context)
{
    _lua = std::make_unique<sol::state>();
    // No io, os, package, debug: a hook reads and writes the machine through its API only. No coroutine and no
    // pcall / xpcall either: they would catch the budget's error and let a runaway call go on. No metatables: Lua
    // runs __gc and __close with the hooks off, out of the budget's reach
    _lua->open_libraries(sol::lib::base, sol::lib::string, sol::lib::math, sol::lib::table, sol::lib::utf8);
    for (const char* name : {"pcall", "xpcall", "dofile", "loadfile", "load", "collectgarbage", "print", "setmetatable",
                             "getmetatable"})
        (*_lua)[name] = sol::lua_nil;
    HostOf(_lua->lua_state()) = this;
    // The same numbers on every run and every host; a reseed names its seed
    sol::table math = (*_lua)["math"];
    sol::protected_function seed = math["randomseed"];
    seed(kRandomSeed);
    math["randomseed"] = [seed](sol::optional<lua_Integer> value) {
        if (!value)
            throw std::runtime_error("math.randomseed: a hook names its seed (no host randomness)");
        seed(*value);
    };
    RegisterApi();
}

LuaHookHost::~LuaHookHost()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _functions.clear();
    _lua.reset();
}

std::unique_ptr<IScriptHookHost> LuaHookHost::Create(EmulatorContext* context)
{
    return std::make_unique<LuaHookHost>(context);
}

std::string LuaHookHost::EditRefusal() const
{
    return ScriptCallbackManager::EditRefusal(_context, _edits != nullptr ? CallbackRole::Logic : CallbackRole::View);
}

void LuaHookHost::RegisterApi()
{
    sol::state& lua = *_lua;
    EmulatorContext* context = _context;
    auto z80 = [context]() -> Z80* {
        return context != nullptr && context->pCore != nullptr ? context->pCore->GetZ80() : nullptr;
    };
    auto callbacks = [context]() -> ScriptCallbackManager* {
        return context != nullptr && context->pDebugManager != nullptr ? context->pDebugManager->GetScriptCallbacks()
                                                                       : nullptr;
    };

    lua["peek"] = [context](int address) -> int {
        if (context == nullptr || context->pMemory == nullptr)
            return 0;
        return context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(address));
    };
    lua["peek16"] = [context](int address) -> int {
        if (context == nullptr || context->pMemory == nullptr)
            return 0;
        const auto at = static_cast<uint16_t>(address);
        return context->pMemory->DirectReadFromZ80Memory(at) |
               (context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(at + 1)) << 8);
    };
    lua["poke"] = [this, context](int address, int value) {
        if (const std::string refusal = EditRefusal(); !refusal.empty())
            throw std::runtime_error("poke: " + refusal);
        if (_edits != nullptr)
        {
            if (_edits->pokes.size() >= HookEdits::kMaxPokes)
                throw std::runtime_error("poke: at most " + std::to_string(HookEdits::kMaxPokes) + " writes per call");
            _edits->pokes.emplace_back(static_cast<uint16_t>(address), static_cast<uint8_t>(value));
            return;
        }
        if (context != nullptr && context->pMemory != nullptr)
            context->pMemory->ToolWriteToZ80Memory(static_cast<uint16_t>(address), static_cast<uint8_t>(value));
    };
    lua["reg"] = [z80](const std::string& name) -> int {
        uint16_t value = 0;
        bool wide = false;
        Z80* cpu = z80();
        if (cpu == nullptr || !Z80::GetRegisterValue(cpu, name, value, wide))
            throw std::runtime_error("reg: unknown register '" + name + "'");
        return value;
    };
    lua["set_reg"] = [this, z80](const std::string& name, int value) {
        if (const std::string refusal = EditRefusal(); !refusal.empty())
            throw std::runtime_error("set_reg: " + refusal);
        if (_edits != nullptr)
        {
            const Z80::RegisterInfo* info = Z80::FindRegister(name);
            if (info == nullptr || (info->maxValue != 0 && static_cast<unsigned>(value) > info->maxValue))
                throw std::runtime_error("set_reg: unknown register '" + name + "' or a value out of its range");
            _edits->regs.emplace_back(name, static_cast<uint16_t>(value));
            return;
        }
        Z80* cpu = z80();
        if (cpu == nullptr || !Z80::SetRegisterValue(cpu, name, static_cast<uint16_t>(value)))
            throw std::runtime_error("set_reg: unknown register '" + name + "'");
    };
    lua["frame"] = [context]() -> uint64_t { return context != nullptr ? context->emulatorState.frame_counter : 0; };
    lua["tstate"] = [z80]() -> uint32_t {
        const Z80* cpu = z80();
        return cpu != nullptr ? cpu->t : 0;
    };
    lua["shared_get"] = [callbacks](sol::this_state L, const std::string& key) -> sol::object {
        ScriptCallbackManager* manager = callbacks();
        return LuaFromCallbackValue(L, manager != nullptr ? manager->SharedGet(key) : CallbackValue{});
    };
    lua["shared_set"] = [callbacks](const std::string& key, sol::object value) {
        CallbackValue shared;
        if (!LuaToCallbackValue(value, shared))
            throw std::runtime_error("shared_set: '" + key + "' takes nil, a boolean, a number or a string");
        if (ScriptCallbackManager* manager = callbacks())
            manager->SharedSet(key, shared);
    };
}

void LuaHookHost::CountHook(lua_State* L, lua_Debug* /*debug*/)
{
    LuaHookHost* host = HostOf(L);
    if (host == nullptr)
        return;
    host->_instructions += kInstructionStep;
    if (host->_instructions <= host->_budget)
        return;
    host->_aborted = true;
    // Only C frames between here and the call's pcall: the long jump skips no destructor
    char text[64];
    std::snprintf(text, sizeof(text), "instruction budget of %llu exceeded", static_cast<unsigned long long>(host->_budget));
    luaL_error(L, "%s", text);
}

bool LuaHookHost::Compile(uint32_t id, const std::string& chunk, std::string& error)
{
    std::lock_guard<std::mutex> lock(_mutex);
    // The chunk runs once here (it returns the function), with the same budget a call gets by default
    _instructions = 0;
    _budget = 100000;
    _aborted = false;
    lua_sethook(_lua->lua_state(), &LuaHookHost::CountHook, LUA_MASKCOUNT, kInstructionStep);
    sol::protected_function_result result = _lua->safe_script(chunk, sol::script_pass_on_error, "=chunk");
    lua_sethook(_lua->lua_state(), nullptr, 0, 0);
    if (!result.valid())
    {
        sol::error raised = result;
        error = raised.what();
        return false;
    }
    sol::object returned = result;
    if (returned.get_type() != sol::type::function)
    {
        error = "a hook chunk returns its function (return function(hit) ... end)";
        return false;
    }
    _functions[id] = returned.as<sol::protected_function>();
    return true;
}

HookCallResult LuaHookHost::Call(uint32_t id, const CallbackHit& hit, const std::string& event,
                                 uint64_t instructionBudget, HookEdits* edits)
{
    HookCallResult outcome;
    std::lock_guard<std::mutex> lock(_mutex);
    auto found = _functions.find(id);
    if (found == _functions.end())
    {
        outcome.ok = false;
        outcome.error = "no hook " + std::to_string(id);
        return outcome;
    }
    _instructions = 0;
    _budget = std::max<uint64_t>(instructionBudget, kInstructionStep);
    _aborted = false;
    _edits = edits;
    lua_State* L = _lua->lua_state();
    lua_sethook(L, &LuaHookHost::CountHook, LUA_MASKCOUNT, kInstructionStep);
    {
        sol::protected_function_result result = found->second(LuaCallbackHitTable(*_lua, hit, event));
        outcome.instructions = _instructions;
        outcome.aborted = _aborted;
        if (!result.valid())
        {
            sol::error raised = result;
            outcome.ok = false;
            outcome.error = raised.what();
        }
    }
    lua_sethook(L, nullptr, 0, 0);
    _edits = nullptr;
    // A hook's garbage is the hook state's: a step per call keeps it small without a pause anywhere
    lua_gc(L, LUA_GCSTEP, 0);
    return outcome;
}

void LuaHookHost::Forget(uint32_t id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _functions.erase(id);
}
