#pragma once

// The core the backends of the one-pass 128K assemblers share (XAS, STORM; asm-synchronizer.md §15.2, part B, Z3):
// targets without macros, WHILE or (mostly) conditions, with short names and their own expression rules. The source is
// walked with its macros expanded, WHILE and the repeats the target cannot hold unrolled, the conditions whose value is
// known decided and the names assigned with DEFL / "=" written as their values; every label gets a name the target keeps
// apart. A dialect gives the expression text, the instructions and directives it spells its own way, its data keywords,
// its repeat and condition blocks and the layout of a line.

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class RetroWriter
{
public:
    RetroWriter(const ir::Program& program, const BackendOptions& options, BackendResult& result);
    virtual ~RetroWriter() = default;
    RetroWriter(const RetroWriter&) = delete;
    RetroWriter& operator=(const RetroWriter&) = delete;

    /// Names given, macros found, the program written into the result
    void Run();

protected:
    using Expr = ir::Expr;
    using Op = ir::Op;
    using Operand = ir::Operand;
    using Statement = ir::Statement;

    // ---- the dialect ----

    virtual std::string Title() const = 0;                          ///< "XAS": in diagnostics
    virtual size_t Significant() const = 0;                         ///< characters of a name the target compares
    virtual bool Reserved(const std::string& upper) const = 0;      ///< a word a label must not be
    virtual std::string Shown(const std::string& name) const { return name; }   ///< a name as the target's editor shows it
    virtual std::string Sanitized(const std::string& name) const;   ///< letters, digits, "_" (a letter first), upper case
    virtual std::string Key(const std::string& name) const;         ///< what makes two names the same for the target
    /// The target's text of an expression computing the source's value; nullopt when it cannot be written so (Problem()
    /// may say why). Expression() falls back to the value
    virtual std::optional<std::string> Text(const Expr& e) = 0;
    virtual std::string Number(int64_t value) const;                ///< a value as a number of the target
    virtual std::string HexByte(int value) const;                   ///< a byte as a hex number (#DD)
    virtual std::string RegisterText(const std::string& reg) const;
    virtual std::string Db() const { return "DB"; }
    virtual size_t DataWidth() const { return 40; }
    virtual bool CharactersInData() const { return true; }          ///< a DB item may be a character constant ("A")                 ///< the characters of DB / DW items on a line
    virtual std::string CommentAfter() const { return "   ;"; }     ///< what puts a comment after a statement
    virtual std::string Dw() const { return "DW"; }
    /// An item of DB / DW (an operand that is no string)
    virtual std::string DataItem(const Operand& item, bool /*words*/) { return OperandText(item); }
    /// The target's $ in EQU is the address of the line (PASM: 0, an EQU with $ is written as its value)
    virtual bool CurrentInEqu() const { return true; }
    /// A string of DB as items (and lines of its own where the target needs them), `flush` ends the items so far
    virtual void PutString(const std::string& text, std::vector<std::string>& items, std::vector<std::string>& out,
                           const std::function<void()>& flush) = 0;
    /// The instructions the target spells its own way, or cannot assemble (nullopt: the common rules)
    virtual std::optional<std::vector<std::string>> OwnInstruction(const Statement&) { return std::nullopt; }
    /// A directive the source's frontend keeps as text (sjasmplus SAVEBIN ...) the target has its own form of
    virtual std::optional<std::vector<std::string>> OtherText(const std::string&, const std::string&) { return std::nullopt; }
    /// The directives that depend on the target (ORG, DISP, ENT, DS, INCLUDE, INCBIN, DUP, END ...)
    virtual std::vector<std::string> DirectiveText(const Statement& s, const std::string& label) = 0;
    /// A condition left to the target (its value unknown here): the lines that open it, or nullopt when the target has
    /// none (then the block is written as taken, with a warning)
    struct Condition
    {
        bool parentEmits = true;
        bool decided = true;      ///< known while converting: only the taken branch is written
        bool taken = true;
        bool native = false;      ///< left to the target
        bool whenZero = false;
        std::string test;
        bool Emits() const { return parentEmits && (!decided || taken); }
    };
    virtual std::optional<std::vector<std::string>> OpenNative(const Expr&, Condition&) { return std::nullopt; }
    virtual std::vector<std::string> ElseNative(Condition&) { return {}; }
    virtual std::vector<std::string> CloseNative(const Condition&) { return {}; }
    /// How a DUP is written: unrolled, kept as the target's block (its DUP / EDUP go through DirectiveText), or as one
    /// line the target repeats (RepeatedLabel gives that line's label field)
    enum class RepeatForm { Unrolled, Block, Line };
    virtual RepeatForm Repeat(const std::vector<ir::Line>& lines, size_t begin, size_t end) const = 0;
    virtual std::string RepeatedLabel(int64_t) const { return {}; }
    /// The lines of one source line: its label, its statements' texts (";..." a comment line), its comment
    /// (`bare`: the source line has no statements)
    virtual std::vector<std::string> Layout(const std::string& label, const std::vector<std::string>& texts, bool bare, bool hasComment,
                                            const std::string& comment) = 0;

    // ---- helpers ----

    static bool IsTemporary(const std::string& label);
    /// The number of a temporary label's reference: 1 of 1B, 1F, 1_B, 1_F
    static std::string TemporaryNumber(const std::string& reference);
    static bool IsDirective(const Statement& s, ir::DirectiveKind kind);
    static const Expr& Unwrapped(const Expr& e);
    static bool UsesCurrent(const Expr& e);
    static Expr ShiftCurrent(const Expr& e, int64_t offset);   ///< $ as $-offset (an item of a DB / DW list)

    // ---- names ----

    std::string FullName(const std::string& text) const;
    std::optional<std::string> Temporary(const std::string& reference) const;
    std::string Name(const std::string& text) const;   ///< the target's spelling of a source name
    bool InScope(const std::string& text) const;
    std::string Give(const std::string& wanted);       ///< a new name, unique for the target and no keyword

    // ---- values (the source's arithmetic) ----

    int64_t Wrap(int64_t v) const;
    int64_t True() const { return program.trueValue ? program.trueValue : -1; }
    const SourceLabel* Lookup(const std::string& text) const;
    std::optional<int64_t> Value(const Expr& e) const;
    std::string LineLabel(bool withFile) const;
    static std::string Blanked(std::string text);   // a source text in a comment: its tabs as blanks
    std::string BodyLineLabel(bool withFile) const;   // ... of a line of a macro expanded on the line
    bool Later(const std::string& text) const;   ///< defined further down: unknown when a one-pass target reaches the line
    bool InWord(const Expr& e) const;            ///< in 0..#FFFF, where a 16-bit unsigned target agrees

    // ---- expressions and operands ----

    void Problem(const std::string& why);
    /// A name or $ or a temporary label reference: the target's text (a name assigned again and again as its value)
    std::optional<std::string> SymbolText(const Expr& u);
    std::string Expression(const Expr& e);   ///< the target's text, else the value with a note, else 0 with a warning
    std::string Known(const Expr& e);        ///< one that must be known when the line is assembled (ORG, EQU, DS ...)
    virtual std::string OperandText(const Operand& o);
    std::string Operands(const std::vector<Operand>& operands);
    std::string NotConverted(const std::string& what);
    std::vector<std::string> DataLines(const std::string& keyword, const std::vector<std::string>& items, size_t width = 40) const;
    /// INCBIN for a target without it (or without its part): the file's bytes as DB lines, when the project's data files
    /// hold it (options.dataFiles); else the line not converted, `why`
    std::vector<std::string> IncbinAsData(const Statement& s, const std::string& why);
    std::vector<std::string> Data(const Statement& s, bool words);
    std::vector<std::string> InstructionText(const Statement& s);
    /// The bytes of an instruction the target lacks, as a DB line with the instruction as its comment
    std::string Bytes(const std::vector<std::string>& items, const std::string& instruction) const;
    /// An instruction on IXH / IXL / IYH / IYL (a target without them: GENS) as its bytes: the H / L form's opcode
    /// (PROMETHEUS' instruction table) after DD / FD; nullopt when it uses none
    std::optional<std::vector<std::string>> HalfRegisterBytes(const Statement& s);
    std::vector<std::string> StatementText(const Statement& s, const std::string& label);

    // ---- conditions ----

    bool Emitting() const { return conditions.empty() || conditions.back().Emits(); }
    std::vector<std::string> OpenCondition(const Expr& condition);
    std::vector<std::string> ElseCondition();
    std::vector<std::string> CloseCondition();

    // ---- the walk ----

    struct MacroDef
    {
        size_t begin = 0;   ///< the MACRO line
        size_t end = 0;     ///< the ENDM line
        std::vector<std::string> params;
    };
    static size_t Matching(const std::vector<ir::Line>& lines, size_t from, ir::DirectiveKind open, ir::DirectiveKind close);
    void Range(const std::vector<ir::Line>& lines, size_t begin, size_t end, size_t index);
    /// The lines between `begin` and `end` (a repeated block's body) once more, its temporary labels ("1" ... JR 1b) of
    /// this pass's own (the references by their place in the body)
    void RepeatPass(const std::vector<ir::Line>& lines, size_t begin, size_t end, size_t index);
    Operand Argument(const std::string& text) const;
    void Expand(const MacroDef& def, const Statement& call, size_t index, uint32_t callLine);
    void Emit(const std::vector<std::string>& texts, uint32_t origin);
    void WriteLine(const ir::Line& l, size_t k, const std::string* repeatLabel = nullptr);

    const ir::Program lowered;                           // sjasmplus' MODULE, STRUCT ... lowered (sjasmpluslower.h)
    const ir::Program& program;
    const BackendOptions& options;
    BackendResult& result;
    Diagnostics& diagnostics;
    uint32_t line = 0;
    size_t lineIndex = 0;
    std::string parent{};                                // sjasmplus: the last label without a dot (".x" hangs on it)
    std::vector<std::string> modules{};                  // sjasmplus: the MODULEs open where the walk is (their marks)
    std::map<std::string, std::string> names{};          // the source's full names -> the target's
    std::set<std::string> redefinable{};                 // names assigned with DEFL / "=": their values are written
    std::map<std::string, int64_t> deflValues{};         // ... as they stand at the line being written
    std::map<std::string, std::string> deflNames{};      // ... or, where the value is not known, the name of the definition in force
    std::string assignedName{};                          // the name the statement being written defines that way
    std::vector<std::map<std::string, std::string>> scopes{};   // the labels of each macro expansion being written
    std::map<std::string, std::vector<std::pair<size_t, std::string>>> temporaries{};   // sjasmplus "1": its definitions
    bool displaced = false;
    std::optional<int64_t> here{};                       // $ on the line being written, when its label gives it
    std::optional<int64_t> lineAddress{};                // ... as it stands at the line's start (the statements move here)
    std::optional<int64_t> statementAddress{};           // ... and at the statement being written (several a line)
    std::string problem{};                               // why the target cannot compute the expression being written
    bool strict = false;                                 // every label must be known when the line is assembled (ORG, DS ...)
    std::vector<Condition> conditions{};
    bool nativeBlock = false;                            // a block of the target (a repeat or a condition) is open
    std::map<std::string, MacroDef> macros{};
    int expansions = 0;                                  // macro expansions written (their labels' names count it)
    int nesting = 0;                                     // expansions open (a limit against a macro calling itself)
    uint32_t bodyLine = 0;                               // in an expansion of a macro called on a line: the body's line (1-based)
    std::set<std::string> taken{};                       // the keys of the names given out
    std::vector<bool> repeats{};
    std::set<std::string> defines{};
};

}  // namespace unrealasm::dialects
