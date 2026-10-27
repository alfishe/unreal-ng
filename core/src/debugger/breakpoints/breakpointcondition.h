#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "emulator/cpu/z80.h"

class LabelManager;
class Memory;
struct EmulatorState;

/// The condition of a breakpoint or a tap (conditional-breakpoints design F1, breakpoint-taps
/// conditions-and-callbacks.md): an expression compiled once into a program, evaluated on the emulation thread only
/// after the breakpoint's trigger matched. The canonical language is Unreal's (debugger-model rules.md §5.3):
///
///   precedence (high to low): ! ~ - (unary), M(x) W(x) a->b | * / % | + - | << >> | comparisons | & | ^ | | | && | ||
///
/// Numbers are hex unless written as decimal (`123d`); `#FF`, `$FF`, `0xFF`, `FFh` are hex; `'c'` is a character.
/// Names are case-insensitive. A false condition costs no store; an error is reported once and the breakpoint is
/// marked, never stopped on.

/// Which language a text is in. The printed (canonical) form is always Unreal
enum class BreakpointConditionDialect : uint8_t
{
    Unreal = 0,  ///< comparisons bind tighter than & ^ |, numbers hex by default
    C            ///< C precedence (& ^ | tighter than && ||, looser than comparisons), numbers decimal by default
};

/// The accesses a breakpoint can be triggered by (BRK_MEM_* and BRK_IO_* folded together): a condition may only
/// use the access operands its trigger can supply
enum BreakpointConditionTrigger : uint8_t
{
    BRK_COND_EXEC = 0x01,
    BRK_COND_READ = 0x02,
    BRK_COND_WRITE = 0x04,
    BRK_COND_PORT_IN = 0x08,
    BRK_COND_PORT_OUT = 0x10,
    BRK_COND_ANY = 0x1F  ///< no checks (a tool evaluating an expression on its own)
};

/// The access that triggered the evaluation. kNone where the access has no such part (Unreal: FFFFFFFF)
struct BreakpointConditionAccess
{
    static constexpr uint32_t kNone = 0xFFFF'FFFFu;
    uint32_t readAddress = kNone;   ///< RD / MRA
    uint32_t writeAddress = kNone;  ///< WR / MWA
    uint32_t memoryValue = kNone;   ///< MDT / MRV / MWV
    uint32_t portIn = kNone;        ///< IN / PRA
    uint32_t portOut = kNone;       ///< OUT / PWA
    uint32_t portValue = kNone;     ///< VAL (port) / PWV
};

/// What an evaluation reads. The registers and the memory of the breakpoint's CPU; the state for the timing and the
/// ports. Any pointer may be null in a test: the operands that need it then evaluate to an error
struct BreakpointConditionContext
{
    const Z80Registers* cpu = nullptr;
    Memory* memory = nullptr;              ///< M(x) / W(x): DirectReadFromZ80Memory, no side effects
    const EmulatorState* state = nullptr;  ///< FRAME, FD, DOS
    BreakpointConditionAccess access;
    uint32_t hits = 0;                     ///< HITS: the condition-true hits before this one
};

enum class BreakpointConditionResult : uint8_t
{
    False = 0,
    True,
    Error
};

/// A compiled condition: immutable, shared by the descriptor that owns it and every copy of it
class BreakpointConditionProgram
{
public:
    enum class Op : uint8_t
    {
        Const, Reg8, Reg16, Var, Mem8, Mem16,
        Neg, Not, BitNot,
        Mul, Div, Mod, Add, Sub, Shl, Shr,
        Lt, Gt, Le, Ge, Eq, Ne,
        And, Xor, Or,
        JumpIfZero, JumpIfNotZero, Bool,
        End
    };
    struct Instruction
    {
        Op op = Op::End;
        uint8_t arg = 0;      ///< the register or the variable
        uint16_t target = 0;  ///< a jump's destination
        int64_t value = 0;    ///< a constant
    };

    /// A shape the evaluator answers without the stack machine (performance.md S4): `operand <cmp> constant`
    struct Fast
    {
        enum class Kind : uint8_t { None, Reg8, Reg16, Var, Mem8 } kind = Kind::None;
        Op compare = Op::Eq;  ///< Lt .. Ne
        uint8_t arg = 0;      ///< the register or the variable
        uint8_t mask = 0xFF;  ///< Reg8 only: (reg & mask)
        uint16_t address = 0; ///< Mem8 only
        int64_t constant = 0;
    };

    /// The text as written, and the canonical (Unreal, fully parenthesised) form
    const std::string& Source() const { return _source; }
    const std::string& Canonical() const { return _canonical; }
    /// Whether the condition names a label (it must be compiled again when the labels change)
    bool UsesLabels() const { return _usesLabels; }
    bool IsFast() const { return _fast.kind != Fast::Kind::None; }
    const std::vector<Instruction>& Code() const { return _code; }

    /// One evaluation. `error`, when the result is Error, says why ("address 1FFFF outside memory")
    BreakpointConditionResult Evaluate(const BreakpointConditionContext& context, std::string* error = nullptr) const noexcept;
    /// The stack machine alone (the reference the fast shapes are tested against)
    BreakpointConditionResult EvaluateCode(const BreakpointConditionContext& context, std::string* error = nullptr) const noexcept;

    /// Compiles `text`. Null with the reason (and the 1-based column) in `error`. `trigger` refuses access operands
    /// the breakpoint can never supply (RD on an execution breakpoint, VAL on a port-in breakpoint); `labels` may be
    /// null (a name then has to be an operand). The result is checked by a dry evaluation on `dryRun` when given
    static std::shared_ptr<const BreakpointConditionProgram> Compile(const std::string& text, uint8_t trigger,
                                                                     const LabelManager* labels, std::string& error,
                                                                     BreakpointConditionDialect dialect = BreakpointConditionDialect::Unreal,
                                                                     const BreakpointConditionContext* dryRun = nullptr);

    /// A condition dialect by name ("unreal", "c"); false for anything else
    static bool ParseDialect(const std::string& text, BreakpointConditionDialect& dialect);

private:
    friend class BreakpointConditionCompiler;
    std::string _source;
    std::string _canonical;
    std::vector<Instruction> _code;
    Fast _fast;
    bool _usesLabels = false;
};

typedef std::shared_ptr<const BreakpointConditionProgram> BreakpointConditionPtr;
