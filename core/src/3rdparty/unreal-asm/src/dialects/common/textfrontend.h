#pragma once

// The line engine of the text-source frontends: a label, a command and its operands per line, the block stack that
// pairs MACRO / REPT / IF / PROC with their ends, and the mapping of a dialect's commands to the IR's directives.
// A dialect (pasmo, z80asm, zasm, FantASM ...) is a TextDialect: its expression rules, its command table and the few
// switches where the line syntax differs.

#include <functional>
#include <map>
#include <set>
#include <string>

#include "dialects/common/textexpr.h"
#include "unrealasm/dialect.h"

namespace unrealasm::dialects::text
{
/// What a command needs beyond its directive kind
enum class Special : uint8_t
{
    None,
    IfDef,       ///< IFDEF name -> If(exist name)
    IfNdef,
    Macro,       ///< NAME MACRO params, or MACRO NAME,params (per the dialect's switches)
    Repeat,      ///< REPT n [,var,init,step]
    EndBlock,    ///< ENDM: ends a macro / REPT block and the IFs open inside it
    Proc,        ///< PROC: a local block
    EndProc,
    Local,       ///< LOCAL names: the enclosing macro / REPT block's labels are local
    DefineList,  ///< DEFC a=1, b=2: one Equ per NAME=expr item
    ElseIf,      ///< ELIF expr: ELSE and a nested IF closed by the same ENDIF
    DefineSymbols,   ///< DEFINE a, b: each symbol is 1 (z80asm)
    Group,       ///< DEFGROUP { a, b = 10, c }: consecutive constants (z80asm)
    Vars,        ///< DEFVARS addr { name ds.b n ... }: constants stepping through a block of memory (z80asm)
    Z80n,        ///< .Z80N: the Z80N mnemonics are instructions from here on
    Ignore,      ///< debug or listing information: dropped
    IfNot,       ///< IFNOT expr (rasm)
    IfEqual,     ///< IFEQ a,b: IF a == b (zmac); IfNotEqual, IfLess, IfGreater likewise
    IfNotEqual,
    IfLess,
    IfGreater,
    HexBytes,    ///< DH "12FF": the bytes of a string of hexadecimal digits (FantASM)
    RawBlock,    ///< STRUCT / ENUM ... END: the lines up to END are kept as text (FantASM)
    AsciiZ,      ///< DEFB-like data with a terminating zero byte (zasm .asciz)
    Segment,     ///< #CODE name, start, size: an ORG at the start address when it is given (zasm)
    Text,        ///< kept as text, reported by the backend
};

struct Command
{
    ir::DirectiveKind kind = ir::DirectiveKind::Other;
    Special special = Special::None;
};

enum class LabelStyle : uint8_t
{
    ColumnZero,   ///< a name in column 0, a colon optional (pasmo)
    ColonOrDot,   ///< NAME: or .NAME, anywhere on the line (z80asm: a bare name in column 0 is an opcode or macro)
    ColonOrColumnZero,   ///< NAME: anywhere, or a bare name in column 0
};

struct TextDialect
{
    std::string id;
    ExprRules rules;
    std::map<std::string, Command> commands;   ///< lower-case name (a leading '.' included when the dialect has it)
    std::set<std::string> reserved;            ///< words besides mnemonics, registers, conditions and commands that no label may use
    LabelStyle labelStyle = LabelStyle::ColumnZero;
    bool lineNumbers = false;                  ///< a decimal number before the blanks is ignored
    bool macroNameFirst = true;                ///< NAME MACRO params
    bool macroKeywordFirst = true;             ///< MACRO NAME,params
    bool hashPrefixedCommands = false;         ///< commands may be written #COMMAND (z80asm's preprocessor, zasm)
    std::string macroParamTags;                ///< characters that may precede a macro parameter, in the definition and the body (zasm: # and &)
    bool slashComments = false;                ///< // starts a comment as well as ; (FantASM)
    bool numericLocals = false;                ///< N$ labels (1$:, jr nz,1$): local to the stretch between two ordinary labels (zasm)
    bool shebangLine = false;                  ///< a first line starting #! carries command line options (zasm): a comment
    bool setIsDefl = false;                    ///< NAME SET expr (with a label and no comma) is DEFL, not the bit instruction (zasm)
    bool dotCommands = false;                  ///< every command may also be written .COMMAND (zasm)
    char statementSeparator = 0;               ///< several statements on a line (z80asm's backslash), 0 = one
    bool macroParenParams = false;             ///< MACRO name (a, b): the parameters in parentheses (rasm)
    bool macroLocalLabels = false;             ///< every label of a macro body is local to the expansion (rasm's @labels)
    bool macroParamsSpaceSeparated = false;    ///< MACRO name a b c: parameters separated by blanks as well as commas (z80asm)
    bool z80nAlways = false;                   ///< the Z80N mnemonics are instructions without a switch (FantASM -N, zasm --z80n are options)
    int expressionBits = 16;
    bool unsignedArithmetic = true;
    int trueValue = -1;
    /// An instruction the dialect writes as a convenience form (zasm's compound instructions): the Z80 instructions it stands
    /// for, or false to keep it as it is
    std::function<bool(const ir::Statement&, std::vector<ir::Statement>&)> expandInstruction;
    std::function<std::string(const std::string&)> fileName;   ///< the file name of INCLUDE / INCBIN from the rest of the line
    std::function<std::string(std::string)> nameFilter;        ///< applied to labels and symbols (pasmo's '$' stripping); may be empty
};

/// Parses a document of the dialect
FrontendResult ParseText(const SourceDocument& source, const TextDialect& dialect);

/// The file name of a directive: "quoted", 'quoted' or everything up to a blank
std::string DefaultFileName(const std::string& rest);
}  // namespace unrealasm::dialects::text
