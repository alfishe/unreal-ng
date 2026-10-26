#pragma once

/// @file l1model.h
/// @brief L1 of the analysis layers (docs/emulator/analysis/analysis-layers.md): what the code did over a replayed
/// recording, folded into structures. Everything a PC or an address names is a physical location (PhysKey: the
/// absolute page and the offset in it), so the same Z80 address in two pages is two instructions, two routines; the
/// Z80 address the code ran at is kept next to it for display. All counts are integers and every list is sorted, so
/// two builds of the same recording serialize byte for byte the same (L1Result::ToJson), on every compiler.
///
/// Produced by L1Builder (l1builder.h) from the replay tracer (replaytracer.h):
/// @code
///   analysis::L1Result l1 = ...;                       // ReplayTracer::Build
///   const analysis::Routine* r = l1.RoutineAtZ80(0x8B70);
///   std::string sheet = l1.FactSheet(*r, disasm);       // <= 1 KB
/// @endcode

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "debugger/analyzers/analysis/z80opinfo.h"
#include "emulator/state/statenode.h"

namespace analysis
{

/// (absolute page << 14) | offset in the page
using PhysKey = uint32_t;
constexpr PhysKey kNoKey = 0xFFFFFFFFu;

inline PhysKey MakeKey(uint16_t page, uint16_t offset)
{
    return (static_cast<uint32_t>(page) << 14) | (offset & 0x3FFFu);
}
inline uint16_t KeyPage(PhysKey key)
{
    return static_cast<uint16_t>(key >> 14);
}
inline uint16_t KeyOffset(PhysKey key)
{
    return static_cast<uint16_t>(key & 0x3FFFu);
}

/// A closed range of physical locations in one page, with the Z80 address its first byte was seen at
struct Interval
{
    PhysKey lo = 0;
    PhysKey hi = 0;  ///< inclusive
    uint16_t z80 = 0;
};

enum class EdgeKind : uint8_t
{
    Fallthrough,  ///< the next instruction in memory
    Taken,        ///< a taken JP / JR / DJNZ
    AfterCall,    ///< a CALL / RST site to the instruction its callee returned to
    Call,         ///< a CALL / RST to its target
    Indirect,     ///< JP (HL) / JP (IX) / JP (IY) to a target it took
    Dispatch,     ///< a RET whose address was pushed by the code (PUSH + RET), or a patched return address
    Interrupt,    ///< the instruction an interrupt came after, to the handler
};
const char* EdgeKindName(EdgeKind kind);

/// One instruction location, as executed
struct Instruction
{
    PhysKey key = 0;
    uint16_t z80 = 0;
    uint8_t bytes[4] = {};
    uint8_t length = 1;
    OpClass cls = OpClass::Nop;
    Flow flow = Flow::None;
    uint64_t executions = 0;
    uint64_t tstates = 0;
    bool selfModified = false;  ///< its bytes were seen to change
};

struct Edge
{
    uint32_t to = 0;  ///< block index (Cfg) / routine index (CallGraph)
    EdgeKind kind = EdgeKind::Fallthrough;
    uint64_t count = 0;
};

struct Block
{
    PhysKey start = 0;
    uint16_t z80 = 0;
    uint16_t z80End = 0;  ///< the last byte of the last instruction
    uint32_t routine = 0;
    uint32_t instructions = 0;
    uint64_t executions = 0;  ///< of its first instruction
    std::vector<Edge> successors;  ///< within the routine, sorted by target
    std::vector<std::pair<uint16_t, uint64_t>> indirectTargets;  ///< Z80 target, count (JP (rr) ends)
};

enum class CallKind : uint8_t
{
    Call,
    Rst,
    Tail,        ///< JP / JR to another routine's entry
    FallsInto,   ///< runs on into the next routine's entry
    Dispatch,    ///< PUSH + RET (or a patched return)
    Indirect,    ///< JP (rr)
    Interrupt,   ///< interrupted code to the handler
};
const char* CallKindName(CallKind kind);

struct CallEdge
{
    uint32_t caller = 0;  ///< routine index
    uint32_t callee = 0;
    CallKind kind = CallKind::Call;
    uint64_t count = 0;
    std::vector<uint16_t> sites;  ///< Z80 addresses of the instructions that made it (sorted, at most 8)
};

/// Entry kinds of a routine (bits)
namespace entry
{
constexpr uint16_t kCall = 1u << 0;
constexpr uint16_t kRst = 1u << 1;
constexpr uint16_t kInterrupt = 1u << 2;
constexpr uint16_t kDispatch = 1u << 3;   ///< PUSH + RET target
constexpr uint16_t kIndirect = 1u << 4;   ///< JP (rr) target
constexpr uint16_t kJump = 1u << 5;       ///< a JP target entered from outside the routine it jumped from
constexpr uint16_t kShared = 1u << 6;     ///< code two routines run into
constexpr uint16_t kRoot = 1u << 7;       ///< where the replay started, or code reached by no other entry
constexpr uint16_t kNmi = 1u << 8;
}  // namespace entry
std::string EntryKindNames(uint16_t kinds);

struct RoutineProfile
{
    std::vector<Interval> reads;
    std::vector<Interval> writes;
    std::vector<Interval> executes;
    std::vector<uint16_t> portsIn;   ///< sorted; at most 16
    std::vector<uint16_t> portsOut;
    bool portsInMore = false;        ///< more distinct ports than kept
    bool portsOutMore = false;
    uint64_t tstates = 0;            ///< its own instructions
    uint64_t tstatesInclusive = 0;   ///< entry to return, callees included
    uint64_t entries = 0;            ///< times it was entered (call, interrupt, dispatch, a jump or a fall into it)
    uint32_t activeFrames = 0;       ///< frames it ran in (its busiest instruction's count: a lower bound)
    uint64_t firstFrame = 0;
    uint64_t lastFrame = 0;
    uint16_t liveIn = 0;             ///< registers read before written (reg:: bits)
    uint16_t liveOut = 0;            ///< registers it wrote that a caller read after the return
    uint16_t written = 0;            ///< registers it wrote and did not restore
    uint16_t minDepth = 0xFFFF;      ///< call depth at entry (0: no known caller)
    uint16_t maxDepth = 0;
};

struct Routine
{
    PhysKey entry = 0;
    uint16_t z80 = 0;
    uint16_t kinds = 0;              ///< entry:: bits
    std::vector<uint32_t> blocks;    ///< block indices, in address order
    uint32_t instructions = 0;
    RoutineProfile profile;
};

struct Induction
{
    std::string reg;  ///< "HL", "B", "IX", ...
    int32_t step = 0;
};

struct Stride
{
    PhysKey instruction = 0;
    uint16_t z80 = 0;
    bool write = false;
    int32_t stride = 0;
    bool constant = false;  ///< false: the address moved by different amounts
};

struct Loop
{
    PhysKey header = 0;
    uint16_t z80 = 0;
    PhysKey latch = 0;         ///< the instruction of the back edge (the last one seen)
    uint16_t latchZ80 = 0;
    uint32_t routine = 0;
    uint64_t instances = 0;
    uint64_t passes = 0;
    uint32_t minTrips = 0;
    uint32_t maxTrips = 0;
    std::vector<std::pair<uint32_t, uint64_t>> trips;  ///< trips -> instances, sorted (at most 32; larger trips in
                                                       ///< the last bucket, its key the smallest of them)
    std::vector<Induction> induction;
    std::vector<Stride> strides;
    bool delay = false;        ///< no memory or I/O access in its body: it only burns time
};

enum class BaseReg : uint8_t
{
    IX,
    IY,
    HL,
};
const char* BaseRegName(BaseReg reg);

struct BaseOffsetPattern
{
    uint32_t routine = 0;
    BaseReg base = BaseReg::IX;
    std::vector<int16_t> offsets;  ///< sorted
    std::vector<uint16_t> bases;   ///< distinct base values seen, sorted (at most 64)
    uint32_t basesSeen = 0;        ///< distinct bases counted (may exceed bases.size())
    int32_t step = 0;              ///< the most common distance between consecutive bases (0: one base)
    bool reads = false;
    bool writes = false;
    std::vector<uint16_t> instructions;  ///< Z80 addresses of the accesses (sorted, at most 16)
};

/// DataFlow answers, one block plus one hop
struct ImmediateAddress
{
    PhysKey instruction = 0;  ///< the LD rr,nn
    uint16_t z80 = 0;
    uint16_t value = 0;       ///< the last value seen
    uint16_t useZ80 = 0;      ///< the access that used it (first seen)
    uint64_t uses = 0;
};

enum class SourceKind : uint8_t
{
    None,
    Memory,
    Port,
    Constant,
    Register,
};
const char* SourceKindName(SourceKind kind);

struct ConstantCompare
{
    PhysKey instruction = 0;  ///< the CP n
    uint16_t z80 = 0;
    uint8_t constant = 0;
    SourceKind source = SourceKind::Memory;
    uint16_t address = 0;     ///< memory address / port of the compared value
    uint16_t loaderZ80 = 0;   ///< the instruction that loaded it
    uint64_t count = 0;
};

struct PageTable
{
    PhysKey instruction = 0;  ///< the LD H,n
    uint16_t z80 = 0;
    uint16_t table = 0;       ///< n * 256
    uint16_t readerZ80 = 0;   ///< the (HL) access
    uint64_t count = 0;
};

struct BranchCondition
{
    PhysKey instruction = 0;
    uint16_t z80 = 0;
    uint8_t cond = kAlways;      ///< 0 NZ .. 7 M; 8: DJNZ (B not zero)
    uint64_t taken = 0;
    uint64_t notTaken = 0;
    uint16_t producerZ80 = 0;    ///< the instruction that set the flags (last seen)
    OpClass producerClass = OpClass::Nop;
    SourceKind tested = SourceKind::None;  ///< what the producer looked at
    uint16_t address = 0;        ///< memory address / port (Memory / Port)
    uint8_t constant = 0;        ///< the compared constant (Constant)
    bool varies = false;         ///< more than one producer / source was seen
};

struct ReplaySummary
{
    uint64_t firstFrame = 0;
    uint64_t lastFrame = 0;     ///< exclusive
    uint64_t frames = 0;
    uint64_t instructions = 0;
    uint64_t tstates = 0;
    uint64_t interrupts = 0;
    uint32_t checked = 0;       ///< frame boundaries compared with the recording's checkpoints
    uint32_t diverged = 0;      ///< of them, where the replayed CPU differed
    uint64_t firstDivergence = 0;
    uint64_t hostMicros = 0;    ///< wall time of the replay (not serialized: it differs run to run)
    uint64_t builderBytes = 0;  ///< what L1Builder held at the end (not serialized: hash tables grow by host policy)
};

struct L1Result
{
    ReplaySummary replay;
    std::vector<Instruction> instructions;  ///< sorted by key
    std::vector<Block> blocks;              ///< sorted by start
    std::vector<Routine> routines;          ///< sorted by entry
    std::vector<CallEdge> calls;            ///< sorted by (caller, callee, kind)
    std::vector<Loop> loops;                ///< sorted by header
    std::vector<BaseOffsetPattern> patterns;
    std::vector<ImmediateAddress> immediateAddresses;
    std::vector<ConstantCompare> constantCompares;
    std::vector<PageTable> pageTables;
    std::vector<BranchCondition> branches;
    /// page index -> the Z80 bank it was last seen mapped at (for showing physical ranges as Z80 ones)
    std::vector<std::pair<uint16_t, std::string>> pageNames;

    /// Lookup indexes (BuildIndex; not serialized): the routine of each instruction, instructions by Z80 address
    std::vector<uint32_t> instructionRoutine;
    std::vector<uint32_t> byZ80;
    void BuildIndex();

    const Instruction* InstructionAt(PhysKey key) const;
    /// The routine whose code holds @p key (nullptr: not executed)
    const Routine* RoutineOf(PhysKey key) const;
    /// The routine that owns the instruction last executed at Z80 address @p z80 (any page; the lowest page wins)
    const Routine* RoutineAtZ80(uint16_t z80, int page = -1) const;
    uint32_t IndexOf(const Routine& routine) const { return static_cast<uint32_t>(&routine - routines.data()); }

    /// Everything, deterministic (no host times)
    StateNode ToStateNode() const;
    std::string ToJson() const;
    /// The summary of one routine (explain's body)
    StateNode RoutineNode(const Routine& routine) const;

    /// The fact sheet of @p routine, at most 1 KB: profile, loops, fields, callers, registers, a disassembly excerpt
    /// (at most 24 lines). @p disassemble turns an instruction's bytes into text; @p label names an address
    using Disassemble = std::function<std::string(const Instruction&)>;
    using LabelOf = std::function<std::string(uint16_t)>;
    std::string FactSheet(const Routine& routine, const Disassemble& disassemble, const LabelOf& label) const;

    /// "A F BC HL"
    static std::string RegNames(uint16_t mask);
};

}  // namespace analysis
