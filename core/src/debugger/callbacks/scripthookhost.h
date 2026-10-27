#pragma once

/// @file scripthookhost.h
/// @brief The hook state of an emulator (conditions-and-callbacks.md §3, docs/features/script-callbacks.md): a script
/// state owned by the emulation thread that runs the frame and sync callbacks. A callback of these modes is source
/// (a chunk that returns the function), not a closure of the script's own state: the hook state compiles it once and
/// calls it on the emulation thread, inside the access (sync) or at the frame end (frame), with an instruction budget
/// that stops a runaway call at the same point on every host.
///
/// The core knows only this interface; the Lua automation module provides the implementation and installs its
/// factory (SetScriptHookHostFactory). Without one, frame and sync callbacks are refused.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class EmulatorContext;
struct CallbackHit;

/// One call of a hook
struct HookCallResult
{
    bool ok = true;              ///< false: the function raised (error) or ran out of instructions (aborted)
    bool aborted = false;        ///< stopped by the instruction budget
    uint64_t instructions = 0;   ///< VM instructions the call ran (counted per 1000)
    std::string error;
};

/// A logic callback's writes of one call (CallbackRole::Logic): kept, not made, so they land at the next instruction
/// boundary and a TTD recording keeps them as an edit there
struct HookEdits
{
    static constexpr size_t kMaxPokes = 65536;
    std::vector<std::pair<uint16_t, uint8_t>> pokes;      ///< in order
    std::vector<std::pair<std::string, uint16_t>> regs;   ///< register name, value, in order
    bool Empty() const { return pokes.empty() && regs.empty(); }
};

class IScriptHookHost
{
public:
    virtual ~IScriptHookHost() = default;

    /// Compiles @p chunk for callback @p id: the chunk returns the function. False with the reason. Any thread
    virtual bool Compile(uint32_t id, const std::string& chunk, std::string& error) = 0;
    /// Emulation thread: runs callback @p id on @p hit (a game event's name in @p event); @p instructionBudget VM
    /// instructions at most. @p edits: a logic callback's call, its writes go there instead of the machine
    virtual HookCallResult Call(uint32_t id, const CallbackHit& hit, const std::string& event,
                                uint64_t instructionBudget, HookEdits* edits) = 0;
    /// The callback went: its function with it. Any thread
    virtual void Forget(uint32_t id) = 0;
};

using ScriptHookHostFactory = std::unique_ptr<IScriptHookHost> (*)(EmulatorContext* context);

/// The Lua automation module installs its factory when it starts (nullptr: none)
void SetScriptHookHostFactory(ScriptHookHostFactory factory);
ScriptHookHostFactory GetScriptHookHostFactory();
