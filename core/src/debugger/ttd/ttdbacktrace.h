#pragma once

/// @file ttdbacktrace.h
/// @brief The call stack at a moment of the history (docs/inprogress/2026-11-07-ttd-backtrace/README.md).
/// Each stack word above SP is named by the instruction that wrote it last: a CALL / RST pushed a return address, an
/// interrupt acknowledge the interrupted PC, anything else (PUSH, a store) data. With the usual stack discipline every
/// word between SP and the stack's base was written since the stack last grew past it, so its last writer is what the
/// word holds now: the return addresses are the live frames, innermost first.

#include <cstdint>
#include <string>
#include <vector>

#include "debugger/ttd/ttdphyspage.h"
#include "debugger/ttd/ttdcheckpoint.h"   // TTDTimePoint

namespace ttd
{

struct TTDBacktraceQuery
{
    bool hasTime = false;        ///< at: this moment (else the current position)
    TTDTimePoint at;
    uint32_t maxDepth = 16;      ///< frames listed
    uint32_t maxSlots = 256;     ///< stack words looked at

    static constexpr uint32_t kMaxDepth = 64;
    static constexpr uint32_t kMaxSlots = 4096;
};

enum class TTDBacktraceKind : uint8_t
{
    Call,        ///< CALL nn / CALL cc,nn
    Rst,         ///< RST n
    Interrupt,   ///< an interrupt acknowledge (IM 0 / 1 / 2, NMI)
};
const char* TTDBacktraceKindName(TTDBacktraceKind kind);

/// One frame: a return address on the stack and the instruction that pushed it
struct TTDBacktraceFrame
{
    TTDBacktraceKind kind = TTDBacktraceKind::Call;
    uint16_t slot = 0;               ///< the stack address of the word (its low byte)
    uint16_t returnAddress = 0;      ///< the word: where the routine returns to (the interrupted PC for an interrupt)
    uint16_t callerPc = 0;           ///< the CALL / RST (an interrupt: the interrupted PC)
    PhysPage callerPage = kPhysPageNone;
    std::string text;                ///< the instruction ("CALL #8160"), or "interrupt"
    TTDTimePoint calledAt;           ///< when the word was pushed
    bool hasCallee = false;
    uint16_t callee = 0;             ///< the routine entered: the first instruction after the push
    uint32_t dataWords = 0;          ///< data words between this frame's word and the one below it (locals, PUSHes)
};

struct TTDBacktraceResult
{
    std::string error;               ///< non-empty: no answer
    TTDTimePoint at;                 ///< the moment asked about
    uint16_t pc = 0;
    PhysPage codePage = kPhysPageNone;
    uint16_t sp = 0;
    std::vector<TTDBacktraceFrame> frames;   ///< innermost first
    uint32_t slotsScanned = 0;
    uint32_t dataWords = 0;          ///< words that are not return addresses, over the slots scanned
    /// Why the walk ended: max_depth, max_slots, top_of_memory (the word at #FFFE was looked at), rom (a stack word
    /// in ROM), before_session (a word not written in the recorded history: the stack's base, or older frames than
    /// the session), barrier (a replay barrier before the word's last write)
    std::string stop;
    uint16_t stopSlot = 0;
    uint32_t framesTraced = 0;
    double ms = 0.0;
};

/// The answer as text, one line per frame (the MCP and CLI form)
std::string TTDBacktraceText(const TTDBacktraceResult& result);

}  // namespace ttd
