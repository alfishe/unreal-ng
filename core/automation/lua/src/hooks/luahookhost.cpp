#include "luahookhost.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <stdexcept>

#include "../bindings/lua_callbackhit.h"
#include "debugger/callbacks/scriptcallbacks.h"
#include "debugger/debugmanager.h"
#include "debugger/targets/gscardtarget.h"
#include "debugger/ttd/timetravelhooks.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"

namespace
{
LuaHookHost*& HostOf(lua_State* L)
{
    return *static_cast<LuaHookHost**>(lua_getextraspace(L));
}

GeneralSoundCard* CardOf(EmulatorContext* context, DebugCpuId cpu)
{
    GSCardTarget* target =
        context != nullptr && context->pDebugManager != nullptr ? context->pDebugManager->GetCardTarget(cpu) : nullptr;
    return target != nullptr ? target->Card() : nullptr;
}

uint8_t PeekCard(EmulatorContext* context, DebugCpuId cpu, uint16_t address)
{
    uint8_t value = 0xFF;
    if (GeneralSoundCard* card = CardOf(context, cpu))
        card->peekCardMemory(address, value);
    return value;
}

/// A register of the card's CPU by the debugger's name (any case); false for an unknown one
bool CardRegister(EmulatorContext* context, DebugCpuId cpu, std::string name, uint16_t& value)
{
    GeneralSoundCard* card = CardOf(context, cpu);
    Z80CpuRegisters r{};
    if (card == nullptr || !card->cardRegisters(r))
        return false;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::pair<const char*, uint16_t> table[] = {
        {"af", r.af}, {"bc", r.bc}, {"de", r.de}, {"hl", r.hl}, {"af'", r.afAlt}, {"bc'", r.bcAlt}, {"de'", r.deAlt},
        {"hl'", r.hlAlt}, {"ix", r.ix}, {"iy", r.iy}, {"sp", r.sp}, {"pc", r.pc}, {"memptr", r.memptr},
        {"a", static_cast<uint16_t>(r.af >> 8)}, {"f", static_cast<uint16_t>(r.af & 0xFF)},
        {"b", static_cast<uint16_t>(r.bc >> 8)}, {"c", static_cast<uint16_t>(r.bc & 0xFF)},
        {"d", static_cast<uint16_t>(r.de >> 8)}, {"e", static_cast<uint16_t>(r.de & 0xFF)},
        {"h", static_cast<uint16_t>(r.hl >> 8)}, {"l", static_cast<uint16_t>(r.hl & 0xFF)},
        {"ixh", static_cast<uint16_t>(r.ix >> 8)}, {"ixl", static_cast<uint16_t>(r.ix & 0xFF)},
        {"iyh", static_cast<uint16_t>(r.iy >> 8)}, {"iyl", static_cast<uint16_t>(r.iy & 0xFF)},
        {"i", r.i}, {"r", r.r}, {"im", r.im}, {"iff1", r.iff1}, {"iff2", r.iff2}};
    for (const auto& [key, v] : table)
        if (name == key)
        {
            value = v;
            return true;
        }
    return false;
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
    return ScriptCallbackManager::EditRefusal(
        _context, _edits != nullptr && !_edits->view ? CallbackRole::Logic : CallbackRole::View);
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

    lua["peek"] = [this, context](int address) -> int {
        if (_card)
            return PeekCard(context, _cardCpu, static_cast<uint16_t>(address));
        if (context == nullptr || context->pMemory == nullptr)
            return 0;
        return context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(address));
    };
    lua["peek16"] = [this, context](int address) -> int {
        const auto at = static_cast<uint16_t>(address);
        if (_card)
            return PeekCard(context, _cardCpu, at) | (PeekCard(context, _cardCpu, static_cast<uint16_t>(at + 1)) << 8);
        if (context == nullptr || context->pMemory == nullptr)
            return 0;
        return context->pMemory->DirectReadFromZ80Memory(at) |
               (context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(at + 1)) << 8);
    };
    lua["poke"] = [this, context](int address, int value) {
        if (_card && _edits == nullptr)
            throw std::runtime_error("poke: the card has no CPU");
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
    lua["reg"] = [this, context, z80](const std::string& name) -> int {
        uint16_t value = 0;
        if (_card)
        {
            if (!CardRegister(context, _cardCpu, name, value))
                throw std::runtime_error("reg: unknown register '" + name + "' of the card CPU");
            return value;
        }
        bool wide = false;
        Z80* cpu = z80();
        if (cpu == nullptr || !Z80::GetRegisterValue(cpu, name, value, wide))
            throw std::runtime_error("reg: unknown register '" + name + "'");
        return value;
    };
    lua["set_reg"] = [this, z80](const std::string& name, int value) {
        if (_card && _edits == nullptr)
            throw std::runtime_error("set_reg: the card has no CPU");
        if (const std::string refusal = EditRefusal(); !refusal.empty())
            throw std::runtime_error("set_reg: " + refusal);
        if (_card)
        {
            // Made at the card's next instruction boundary (GSCardTarget::QueueHookEdits)
            Z80CpuRegisters probe{};
            if (value < 0 || value > 0xFFFF || !GSCardTarget::SetRegister(probe, name, static_cast<uint16_t>(value)))
                throw std::runtime_error("set_reg: unknown register '" + name + "' of the card CPU or a value out of its range");
            _edits->regs.emplace_back(name, static_cast<uint16_t>(value));
            return;
        }
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
    _cardCpu = static_cast<DebugCpuId>(hit.cpu);
    _card = IsCardCpu(_cardCpu);
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
        else if (result.return_count() > 0)
        {
            // An integer the function returned (a card read's byte); anything else is no answer
            sol::object first = result.get<sol::object>(0);
            if (first.is<lua_Integer>())
            {
                const lua_Integer value = first.as<lua_Integer>();
                outcome.returned = value >= 0 && value <= 0xFF ? static_cast<int32_t>(value) : 0x100;  // 0x100: no byte
            }
        }
    }
    lua_sethook(L, nullptr, 0, 0);
    _edits = nullptr;
    _card = false;
    // A hook's garbage is the hook state's: a step per call keeps it small without a pause anywhere
    lua_gc(L, LUA_GCSTEP, 0);
    return outcome;
}

void LuaHookHost::Forget(uint32_t id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _functions.erase(id);
}
