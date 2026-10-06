#pragma once

// The neutral intermediate representation of assembler sources (dialect-conversion.md §2, decision D-7): what every
// dialect's frontend parses into and every backend writes from. It copies no dialect's spelling: operators are
// operations (ALASM's "!" and sjasmplus' "^" are both Xor), directives are kinds, numbers keep their value and the
// spelling they were written in.

#include <cstdint>
#include <string>
#include <vector>

namespace unrealasm::ir
{
enum class NumberSpelling : uint8_t
{
    Decimal,
    Hex,        ///< #C000, $C000, 0xC000, C000h: the backend chooses its own form
    Binary,
    Character,  ///< "A" or 'A': the value is the character code(s)
};

enum class Op : uint8_t
{
    // binary
    Add, Sub, Mul, Div, Mod, And, Or, Xor, Shl, Shr,
    ShrUnsigned,                   ///< sjasmplus ">>>": shift right filling with zeros
    RotateLeft16, RotateRight16,   ///< ALASM's "<" / ">": cyclic shifts of a 16-bit word
    Equal, NotEqual, Less, Greater, LessEqual, GreaterEqual, LogicalAnd, LogicalOr,
    // unary
    Negate, Plus, Not, High, Low,
    LogicalNot,
    Exists,                        ///< sjasmplus "exist name": 1 when the label is defined anywhere in the source
    SwapBytes,                     ///< TASM's postfix "^": the high and low bytes of a 16-bit word exchanged
};

struct Expr
{
    enum class Kind : uint8_t
    {
        Number,
        Symbol,        ///< text = the name
        Current,       ///< $: the current (displaced) address
        CurrentPage,   ///< $$: the current page
        Unary,         ///< op, args[0]
        Binary,        ///< op, args[0] op args[1]
        Group,         ///< (args[0]): parentheses the source wrote
        Memory,        ///< {args[0]}: the word at that address while assembling
        Defined,       ///< ALASM ?label: 0 when defined
        Raw,           ///< text the frontend could not parse
    };
    Kind kind = Kind::Number;
    int64_t value = 0;
    NumberSpelling spelling = NumberSpelling::Decimal;
    int digits = 0;                 ///< the number of digits written (hex #05 = 2), kept for the spelling
    Op op = Op::Add;
    std::string text;
    std::vector<Expr> args;

    static Expr Number(int64_t value, NumberSpelling spelling = NumberSpelling::Decimal, int digits = 0);
    static Expr Symbol(std::string name);
    static Expr Make(Kind kind);
    static Expr Unary(Op op, Expr a);
    static Expr Binary(Op op, Expr a, Expr b);
};

struct Operand
{
    enum class Kind : uint8_t
    {
        Register,    ///< text: a, b, hl, ix, ixh, i, r, af, af' (lower case, normalized)
        Condition,   ///< text: nz, z, nc, c, po, pe, p, m
        Indirect,    ///< (text): (hl), (bc), (de), (sp), (c), (ix), (iy)
        Indexed,     ///< (text + expr): (ix+d), (iy+d)
        Memory,      ///< (expr)
        Immediate,   ///< expr
        String,      ///< text: the characters of a string (DB "..."), UTF-8
    };
    Kind kind = Kind::Immediate;
    std::string text;
    Expr expr;
};

enum class DirectiveKind : uint8_t
{
    Org,          ///< args: address [, page]
    Equ,          ///< the label = args[0]
    Defl,         ///< the label = args[0], redefinable
    Db,           ///< operands: bytes and strings
    Dw,
    Ds,           ///< args: count [, fill bytes...]; operands: a fill sequence repeated count times; params "cyclic": count bytes of the sequence repeated and cut (STORM)
    Include,      ///< text: file; args: [page]
    Incbin,       ///< text: file; args: [offset [, length]]; params "sector-slack": the rest of the file's last disk sector is written after it without moving the address (TASM)
    If,           ///< args: condition (true when not zero)
    Else,
    EndIf,
    Macro,        ///< text: name; params: the parameter names (none: the body uses ALASM's \0..\9)
    EndMacro,
    Repeat,       ///< args: count
    EndRepeat,
    While,        ///< args: condition; the block repeats while it is not zero
    EndWhile,
    RepeatUntil,  ///< ALASM REPEAT: the block runs until UntilZero's expression is 0
    UntilZero,    ///< args: expression
    Disp,         ///< args: address
    Ent,          ///< text "if-displaced": only when a displacement is active (Program::displacementAcrossFiles)
    LocalBlock,   ///< ALASM LOCAL ... ENDL: labels inside are local to the block
    EndLocalBlock,
    Display,      ///< operands: strings and expressions; params: per-item format keys
    Main,         ///< ALASM MAIN "file": the project's main source
    Run,          ///< ALASM RUN address: call code while assembling
    End,
    Other,        ///< text: the directive as written (a backend of the same dialect writes it back, others report it)
};

struct Statement
{
    enum class Kind : uint8_t
    {
        Instruction,   ///< mnemonic (lower case Z80 name), operands
        Directive,     ///< directive, args / operands / text / params
        MacroCall,     ///< mnemonic = the macro name, params = the argument texts
        Raw,           ///< text = what the frontend could not parse
    };
    Kind kind = Kind::Instruction;
    std::string mnemonic;
    std::vector<Operand> operands;
    DirectiveKind directive = DirectiveKind::Other;
    std::vector<Expr> args;
    std::string text;
    std::vector<std::string> params;
};

struct Line
{
    std::string label;             ///< "" = none
    bool labelGlobal = false;      ///< ALASM @label inside a LOCAL block
    std::vector<Statement> statements;
    std::string comment;           ///< without the comment character
    bool hasComment = false;
    uint32_t sourceLine = 0;       ///< 1-based line of the source document
};

struct Program
{
    std::string dialect;           ///< the frontend's dialect
    std::vector<Line> lines;
    /// How the source's assembler computes: 0 = like the target; 16 = in 16-bit words (ALASM), which a wider target
    /// reproduces by masking where it matters (division)
    int expressionBits = 0;
    bool unsignedArithmetic = false;   ///< division and shifts on unsigned values
    /// The displacement (PHASE / DISP) may continue across INCLUDE: a file cannot know whether one is active where it
    /// starts (TASM). An `Ent` statement with text "if-displaced" ends one only when it is active
    bool displacementAcrossFiles = false;
    /// What a comparison or a logical not gives when true: 0 = what the target gives, 1 (STORM), -1 (sjasmplus)
    int trueValue = 0;
};
}  // namespace unrealasm::ir
