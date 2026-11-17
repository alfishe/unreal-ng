#pragma once

// rasm's assembler (rasm.c v3.3.3, commit a5796b9, MIT) ported to C++: the state of one assembly (rasm's s_assenv) and
// the functions that work on it, under the names rasm gives them. One Engine assembles one source; no globals, no exit().
//
// - rasmexpr.cpp: expressions (ComputeExpressionCore, ComputeExpression, ExpressionFastTranslate, TranslateTag,
//   MakeLocalLabel), variables, aliases, labels (PushLabel), deferred expressions (PushExpression, PopAllExpression);
// - rasmassemble.cpp: the main loop, output, ORG zones, banks, crunched sections, the end of the assembly;
// - rasmdirectives.cpp: the directives (IF / SWITCH / REPEAT / WHILE / MACRO / STRUCT / DEFB ... / SAVE / BUILDZX ...).
//
// Behaviour notes kept from rasm on purpose (the bytes depend on them):
// - variables are doubles; a value is rounded to an integer as floor(v + rough) (rough 0.5, 0.0 with -m);
// - an expression with a symbol not known yet is deferred: pushed with the address it writes to, variables substituted
//   at push time as text ("%lf" trimmed), resolved after the whole source is assembled;
// - words are mutated in place the way rasm mutates them (quoted chars become #nn, `$` / variables are substituted in
//   the word of an expression pushed outside loops and macros).

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dialects/rasm/engine/rasmdata.h"
#include "dialects/rasm/engine/rasminstructions.h"
#include "dialects/rasm/engine/rasmprepro.h"
#include "unrealasm/compress.h"
#include "unrealasm/diagnostics.h"

namespace unrealasm::rasm
{
inline constexpr int kBankMaxNumber = 260;   ///< rasm's BANK_MAX_NUMBER: banks 0..259, 260 = the default workspace

/// rasm's e_compute_operation_type (the order matters: functions come after kGreaterEq)
enum class Op : uint8_t
{
    PushData = 0, Open, Close, Comma, Add, Sub, Div, IntDiv, Mul, And, Or, Mod, Xor, Not, Shl, Shr, BAnd, BOr, Lower,
    Greater, Equal, NotEqual, LowerEq, GreaterEq,
    Sin, Cos, Int, Floor, Abs, Ln, Log10, Sqrt, Asin, Acos, Atan, Exp, Low, High, Psg, Rnd, Frac, Ceil, GetR, GetV, GetB,
    SetR, SetV, SetB, Soft2Hard, Hard2Soft, Peek, Pow2, Pow, Fmod, Atan2, Hypot, Ldexp, Fdim, Fstep, Fmax, Fmin, Clamp,
    Lerp, IsGreater, IsLess,
    GetNop, GetTick, Duration, FileSize, GetSize, IsRegister, FileByte, CountNops,
    End
};

/// rasm's e_expression: how a deferred expression is written
enum class ExprKind : uint8_t
{
    J8, V8At0, V8, J16, V16, V16At0, V32At0, RealAmsdos, RealMicrosoft, Float24, IV8, IV81, V8At3, IV16, Rst,
    RstConditional, Im, Run, ZxRun, ZxStack, BitResSet
};

struct ComputeElement
{
    Op op = Op::End;
    double value = 0;
    int priority = 0;
    std::optional<std::string> string;
};

struct DicoVar   ///< s_expr_dico
{
    std::string name;
    double v = 0;
    bool used = false;
    int iw = 0;
    bool autoriseExport = true;
};

struct Alias     ///< s_alias
{
    std::string alias;
    std::string translation;
    int iw = 0;
    int lz = 0;
    int ptr = 0;
    bool used = false;
    bool fromStruct = false;
    bool autoriseExport = true;
};

struct Label     ///< s_label
{
    std::string name;
    int ptr = 0;
    int lz = -1;
    int iorgzone = 0;
    int ibank = 0;
    bool local = false;
    unsigned nop = 0;
    int fileidx = 0;
    int fileline = 0;
    bool autoriseExport = true;
    bool localExport = false;
    bool makeAlias = false;
    bool used = false;
    bool relocatable = false;   ///< defined inside RELOCATE_START ... RELOCATE_END
    int backidx = 0;
};

struct Expression   ///< s_expression
{
    std::optional<std::string> reference;   ///< the translated text when pushed inside a loop / macro; else the word
    int iw = 0;
    int ptr = 0;     ///< `$` of the expression
    int wptr = 0;    ///< where the value is written (physical address in the bank)
    ExprKind kind = ExprKind::V8;
    int lz = -1;
    int ibank = 0;
    int iorgzone = 0;
    std::optional<std::string> module;
    int wcodeadr = 0;          ///< codeadr of the value's field (RELOCATE_TABLE entry)
    bool relocblock = false;   ///< pushed inside RELOCATE_START ... RELOCATE_END
    bool relocdollar = false;  ///< the word held `$` before ExpressionFastTranslate
};

struct Repeat    ///< s_repeat
{
    int start = 0;
    int cpt = 0;
    int value = 0;
    int maxim = 0;
    int repeatCounter = 0;
    std::string repeatVar;
    double varIncrement = 1;
};

struct WhileWend ///< s_whilewend
{
    int start = 0;
    int cpt = 0;
    int value = 0;
    int maxim = 0;
    int whileCounter = 0;
    std::string whileVar;   ///< WHILE expr,var: the counter variable (empty: none)
};

enum class IfType : uint8_t { If, IfNot, IfDef, IfNDef, IfUsed, IfNUsed, Else, ElseIf, ElseIfNot, Case, Default, Break };

struct IfThen    ///< s_ifthen
{
    std::string filename;
    int line = 0;
    int v = 0;
    int first = 0;
    IfType type = IfType::If;
};

struct SwitchCase   ///< s_switchcase
{
    double refval = 0;
    int execute = 0;
    int casematch = 0;
};

struct Macro     ///< s_macro
{
    std::string mnemo;
    std::vector<Word> wc;
    std::vector<std::string> param;   ///< "{NAME}" (plain NAME with -ass)
    std::vector<int> replaceIdx;      ///< body words holding a '{'
    bool multiArg = false;
    int maxArg = 0;
};

struct MacroPosition   ///< s_macro_position
{
    int start = 0;
    int end = 0;
    int value = 0;
    int level = 0;
    int pushed = 0;
};

struct LzSection    ///< s_lz_section
{
    int iw = 0;
    int memstart = 0;
    int memend = 0;
    int lzversion = 0;     ///< 0: intermediate (LZCLOSE), else the crunch code (4, 7, 8, 17, 18, 48, 49, 70, 71)
    int version = 0;
    int minmatch = 0;
    int iorgzone = 0;
    int ibank = 0;
    int iexpr = 0;
    int ilabel = 0;
    int iendexpr = 0;
    int iendlabel = 0;
    int crunchedSize = -1;   ///< after the end of the assembly: the crunched bytes (-1 = not crunched)
};

/// A run of bytes one statement put down (the frontend's view of the assembly): physical position, `$`, origin
struct TraceItem
{
    int serial = 0;          ///< the statement executed (one per main-loop step: a repeated line gets one per pass)
    int iw = 0;              ///< its first word
    int file = 0;            ///< its source file (index in the file names) and 1-based line
    int line = 0;
    int bank = 0;
    int orgzone = 0;
    int lz = -1;
    int outputStart = 0;     ///< physical address in the bank
    int outputEnd = 0;
    int codeStart = 0;       ///< `$` of the first byte
    bool instruction = false;
    std::string mnemonic;               ///< instructions: rasm's word, upper case
    std::vector<std::string> operands;  ///< instructions: the operand words
    std::vector<uint8_t> encoded;       ///< instructions: the bytes before the deferred values were written
    std::vector<Fixup> fixups;          ///< instructions: offsets relative to outputStart
    struct Value
    {
        int offset = 0;                 ///< relative to outputStart
        int size = 1;                   ///< 1 (DEFB) or 2 (DEFW)
        std::string expression;         ///< the text rasm evaluates (translated)
    };
    std::vector<Value> values;          ///< data: the bytes of DEFB / DEFW values computed from an expression
};

struct OrgZone   ///< s_orgzone
{
    int ibank = 0;
    int protect = 0;
    int extended = 0;
    int memstart = 0;
    int memend = 0;
    int ifile = 0;
    int iline = 0;
    int nocode = 0;
    int inplace = 0;
};

struct RasmStructField   ///< s_rasmstructfield
{
    std::string fullname;
    std::string name;
    int offset = 0;
    int size = 0;
    std::vector<uint8_t> data;
    int zetype = 0;
};

struct RasmStruct   ///< s_rasmstruct (prototype or alias)
{
    std::string name;
    int size = 0;
    int ptr = 0;
    int nbelem = 0;
    std::vector<RasmStructField> fields;
};

struct GlobalLabelStack
{
    std::string label;
};

struct Ticker
{
    std::string varname;
    long nopstart = 0;
    long tickerstart = 0;
};

struct Save      ///< s_save
{
    int ibank = 0;
    int ioffset = 0;
    int isize = 0;
    int iw = 0;
    int irun = 0;
    std::string filename;
    int amsdos = 0;
    int hobeta = 0;
    int tape = 0;
    int dsk = 0;
};

enum class PokerMethod : uint8_t { Xor8, Sum8, Cipher001, Cipher002, Cipher003, Cipher004, Sum16 };

struct Poker
{
    PokerMethod method = PokerMethod::Xor8;
    int istart = 0;
    int iend = 0;
    int outputadr = 0;
    int ibank = 0;
    int istring = 0;
    int ipoker = 0;
};

/// File access of an assembly (INCLUDE is resolved by the preprocessor; INCBIN / FILESIZE / FILEBYTE read here)
using FileReader = std::function<std::optional<std::vector<uint8_t>>(const std::string& path)>;

struct EngineOptions
{
    PreproOptions prepro;
    bool maxam = false;          ///< -m: Maxam style calculations (rough 0.0, 16-bit masked operators, Maxam priorities)
    bool as80 = false;
    bool noWarning = false;
    std::vector<std::string> includePaths;
    std::vector<std::pair<std::string, double>> defines;   ///< -D name=value
    /// TIMESTAMP's clock (seconds since the epoch, local time); < 0: the current time
    int64_t timestamp = -1;
};

enum class LoopStyle : uint8_t { RepeatN, RepeatUntil, While };

/// TranslateTag option: remove the blanks
inline constexpr int kTagRemoveSpace = 1;

/// rasm's StringIsQuote: the quote character when the word is one quoted string, else 0
int StringIsQuote(const std::string& w);

class Engine
{
public:
    using Directive = void (Engine::*)();
    Engine(const FileSystem& fs, const EngineOptions& options);

    // ---- expressions (rasmexpr.cpp) ----
    double ComputeExpressionCore(std::string& expression, int ptr, int didx);
    int RoundComputeExpressionCore(std::string& expression, int ptr, int didx);
    double ComputeExpression(std::string& expr, int ptr, int didx, int expectedEval);
    int RoundComputeExpression(std::string& expr, int ptr, int didx, int expectedEval);
    void ExpressionFastTranslate(std::string& expr, int fullreplace);
    std::string TranslateTag(std::string varbuffer, int* touched, int tagoption);
    std::string MakeLocalLabel(const std::string& varbuffer, int* retdek);
    void ExpressionSetDicoVar(const std::string& name, double v);
    DicoVar* SearchDico(const std::string& name);
    Alias* SearchAlias(const std::string& name);
    Label* SearchLabel(const std::string& name);
    void InsertUsedToTree(const std::string& name) { usedTree.insert(name); }
    bool SearchUsed(const std::string& name) const { return usedTree.count(name) != 0; }
    bool InsertLabelToTree(const Label& label);
    void PushLabel();
    void PushLabelLight(Label label);
    void PushExpression(int iw, ExprKind kind);
    void PopAllExpression(int crunchedZone);

    // ---- diagnostics ----
    void MakeError(int iw, const std::string& message);
    void MakeWarning(int iw, const std::string& message);
    void ExpError(int didx, const std::string& message);
    void ExpWarning(int didx, const std::string& message);
    int GetExpIdx(int didx) const;

    // ---- state (rasm's s_assenv, fields kept under rasm's names in camelCase) ----
    FileSystem fs;
    EngineOptions options;
    Diagnostics diagnostics;
    int nberr = 0;
    int stop = 0;

    std::vector<Word> wl;
    std::vector<std::string> filename;
    std::vector<HexBin> hexbin;
    std::unordered_set<std::string> macroFast;
    int idx = 0;
    int stage = 0;

    // memory
    std::vector<std::vector<uint8_t>> mem;
    std::vector<int> memsize;
    int maxptr = 65536;
    int extended = 0;
    int extendedLimit = 0;
    int activebank = kBankMaxNumber;
    int lastbank = 0;
    std::array<int, kBankMaxNumber / 4 + 1> bankset{};
    std::array<int, kBankMaxNumber + 1> bankused{};
    std::array<int, kBankMaxNumber + 1> bankgate{};
    std::array<int, kBankMaxNumber + 1> setgate{};
    std::array<int, 257> rombank{};
    int forcetape = 0, forcezx = 0, forcecpr = 0, forcesnapshot = 0, forceROM = 0, bankmode = 0;
    int extendedCPR = 0;
    // RELOCATE_START / RELOCATE_END / RELOCATE_TABLE (WinAPE / SymbOS inline relocation table)
    int relocblockactive = 0;   ///< 0 inactive, 1 regular, 2 HIGH
    int relocmode = 0;
    int relocblockidx = 0;
    int relocbank = 0;
    bool relocbankset = false;

    // output position
    int codeadr = 0;
    int outputadr = 0;
    int nocode = 0;
    int codeadrbackup = 0;
    int outputadrbackup = 0;
    std::vector<OrgZone> orgzone;
    int deadend = 0;
    int insideORG = 0;
    int orgDone = 0;
    enum class OutputMode : uint8_t { Normal, NoCode, Disabled };
    OutputMode outputMode = OutputMode::Normal;
    void Output(uint8_t v);

    // struct
    std::vector<RasmStruct> rasmstruct;
    int getstruct = 0;
    int backupOutputadr = 0;
    int backupCodeadr = 0;
    std::vector<RasmStruct> rasmstructalias;

    // expressions
    std::vector<Expression> expression;
    int maxam = 0;
    int as80 = 0;
    int maxamAs80 = 0;
    float rough = 0.5f;
    std::array<char, 256> automateExpressionValidCharExtended{};
    std::array<char, 256> automateExpressionValidCharFirst{};
    std::array<char, 256> automateExpressionValidChar{};
    std::array<char, 256> automateExpressionDecision{};
    std::array<char, 256> automateValidVarFirst{};
    std::array<char, 256> automateValidLabelFirst{};
    std::array<char, 256> automateValidLabel{};
    std::array<char, 256> automateDigit{};
    std::array<char, 256> automateHexa{};
    std::array<ComputeElement, 256> automateElement{};
    std::array<uint8_t, 256> charset{};
    std::array<uint8_t, 256> psgfine{};

    // labels, variables, aliases
    std::deque<Label> label;                                    ///< ae->label (declaration order); pointers stable
    std::unordered_map<std::string, size_t> labelTree;
    std::string module;
    bool hasModule = false;
    std::string moduleSeparator = "_";
    std::string lastgloballabel;
    int curProxIndex = -1;
    std::vector<GlobalLabelStack> globalLabelStack;
    std::unordered_map<std::string, DicoVar> dicoTree;
    std::vector<std::string> dicoOrder;
    std::deque<Alias> alias;
    std::unordered_map<std::string, size_t> aliasTree;
    std::unordered_set<std::string> usedTree;
    int autoriseExport = 1;
    int localExport = 0;

    // loops, conditions, macros
    std::vector<Repeat> repeat;
    double repeatStart = 1;
    double repeatIncrement = 1;
    std::vector<WhileWend> whilewend;
    std::vector<IfThen> ifthen;
    std::vector<SwitchCase> switchcase;
    std::vector<Macro> macro;
    int imacro = 0;
    int repeatcounter = 0, whilecounter = 0, macrocounter = 0;
    std::vector<MacroPosition> macropos;
    std::vector<Ticker> ticker;
    unsigned tick = 0;
    unsigned nop = 0;

    // crunched sections
    std::vector<LzSection> lzsection;
    int lz = -1;
    int curlz = -1;

    std::vector<Poker> poker;
    std::vector<Save> save;
    int nowarning = 0;
    int erronwarn = 0;
    int dams = 0;
    int pasmo = 0;
    int extendedError = 0;

    // ZX outputs
    struct ZxSnapshotState
    {
        int run = 0;
        int stack = 0;
    } zxsnapshot;
    int snapshotRun = 0;

    // ---- assembly (rasmassemble.cpp) ----
    void Assemble();
    void CrunchAndResolve();
    bool PlainBinary(std::vector<uint8_t>& out, int& start, int* bank = nullptr) const;
    void OutputSetLimit(int zelimit, int ext);
    void OverWriteCheck();
    void OrgClose();
    void OrgNew(int nocodeFlag);
    void NewMemorySpace();
    void GetBackToMemorySpace();
    void PushGlobal();
    void PopGlobal();
    Macro* SearchMacro(const std::string& name);
    void MacroExecute(size_t imacroIndex);
    void PushFixup(const Fixup& fixup, int dollar);
    void ExecuteInstruction(const std::string& mnemonic);
    void ApplyPokers();
    bool BankIsCrunched() const;
    Directive FindDirective(const std::string& name) const;
    bool InsertAlias(const Alias& a);
    void DelDico(const std::string& name);

    // ---- directives (rasmdirectives.cpp, rasmdirectives2.cpp) ----
    Word& W(int offset);
    std::string CurrentFile() const;
    void OutputString(const std::string& quoted, StringDirective directive, bool markEnd);
    void DataDirective(ExprKind kind, int zeros, const char* name);
    void RealStruct(int which);
    void SkipLoopBlock(LoopStyle style);
    void SetLoopVar(const std::string& name, double v);
    void PushIf(int v, IfType type);
    bool OneParameter(const char* error);
    bool SymbolDefined(const std::string& word);
    void ElseIfCommon(bool light, bool negate, const char* name);
    void DEFB(); void DEFBStruct(); void DEFW(); void DEFI(); void DEFR(); void DEFF(); void DF24(); void DEFS(); void STR();
    void ALIGN(); void CONFINE();
    void WHILE(); void WEND(); void STARTINGINDEX(); void REPEAT(); void REND(); void UNTIL();
    void IF(); void IFLight(); void IFNOT(); void IFNOTLight(); void IFDEF(); void IFDEFLight(); void IFNDEF();
    void IFNDEFLight(); void IFUSED(); void IFUSEDLight(); void IFNUSED(); void IFNUSEDLight(); void UNDEF();
    void ELSEIF(); void ELSEIFLight(); void ELSEIFNOT(); void ELSEIFNOTLight(); void ELSE(); void ENDIF();
    void SWITCH(); void SWITCHLight(); void CASE(); void CASELight(); void DEFAULT(); void DEFAULTLight();
    void BREAK(); void BREAKLight(); void ENDSWITCH();
    void ASSERT(); void PROTECT(); void ORG(); void NOCODE(); void CODE(); void ENUM(); void STRUCT(); void ENDSTRUCT();
    void MACRO();
    void PRINT(); void STOP();
    // rasmdirectives2.cpp
    void DELAYEDPRINT(); void FailDirective(); void LIMIT(); void BANK(); void REBANK(); void ROMBANK(); void BANKSET();  // FailDirective, not FAIL: gtest defines FAIL()
    void LzOpen(int lzversion, int version, bool takesMinMatch, const char* name);
    void LZ4(); void LZ48(); void LZ49(); void LZX7(); void LZX0(); void LZX0B(); void LZEXO(); void LZAPU(); void LZSA1();
    void LZSA2(); void LZCLOSE();
    void CHARSET(); void UTF8REMAP(); void MODULE(); void ENDMODULE(); void LABEL(); void NOEXPORT(); void ENOEXPORT();
    void ExportFlags(int exportval); void LocalFlags(int localval);
    void LET(); void TICKER(); void RUN(); void SAVE(); void PokerDirective(PokerMethod method, int bytes, const char* name);
    void SUMMEM(); void SUM16(); void XORMEM(); void CIPHERMEM(); void TIMESTAMP(); void BRK(); void REDEFINEBRK();
    void LIST(); void NOLIST(); void HEXBIN(); void ERRRD(); void AMSDOS(); void BUILDZX(); void BUILDSNA(); void BUILDCPR();
    void BUILDROM(); void BUILDTAPE(); void BUILDOBJ(); void NAMEROM(); void COMZ(); void NotConvertedDirective(); void RELOCATESTART(); void RELOCATEEND(); void RELOCATETABLE();
       int RelocCountAtoms(const std::string& expr, bool dollarAtom); bool RelocNetZero(const std::string& expr);
       void RelocSetSymbol(const std::string& name, int value);
    void NotConverted(const std::string& what);
    void Print(const std::string& text);
    void PopAllSave();
    void BuildOutputs();
    std::string UpperTags(std::string text) const;

    // ---- state added for the assembler loop ----
    int imacropos = 0;                                  ///< logical length of macropos (rasm's imacropos)
    std::unordered_map<std::string, size_t> macroIndex; ///< first macro of each name
    std::vector<Utf8Remap> utf8Remap;
    std::vector<int> delayedPrintIdx;                   ///< DELAYED_PRINT statements, printed at the end
    std::string printLine;                              ///< PRINT text of the current line (rasm's stdout)
    int currentRunIdx = 0;                              ///< the RUN statement's address word (SAVE's run address)
    int rundefined = 0;
    int amsdos = 0;                                     ///< AMSDOS: the plain binary gets an AMSDOS header
    int bankRemu = 0;                                   ///< rasm's remu (BREAKPOINT bank style), set by BUILDSNA
    std::vector<int> winapeBRK;                         ///< REDEFINE_BRK bytes
    AudioFilterState audioFilter;                       ///< INCBIN DMA filter state, kept across imports (rasm statics)
    bool binaryOutput = true;                           ///< the plain .bin is written (no SAVE, no ROM/SNA/CPR/tape)
    struct OutputFile
    {
        std::string name;                               ///< the file name as written in the source; "" = the .sna
        std::vector<uint8_t> bytes;
        int bank = -1;                                  ///< SAVE: the memory saved (bank, offset, size) and its format
        int offset = 0;
        int size = 0;
        SaveFormat format = SaveFormat::Plain;
    };
    std::vector<TraceItem> trace;                       ///< every run of bytes put down, in order
    int traceSerial = 0;
    void TraceBytes(int start, int count, int code);
    std::vector<OutputFile> outputFiles;                ///< SAVE files and the BUILDZX snapshot
    std::vector<std::string> notConverted;              ///< rasm outputs this port does not produce (CPC media ...)
    std::string backupFilename;
    int backupLine = 0;

private:
    unsigned fastRandSeed = 0x12345678;
    unsigned FastRand();
    double StringFunction(Op op, const std::string& arg, const std::string* arg2, double number, int didx);
};

/// The cruncher of rasm's legacy crunch code (4 LZ4, 7 ZX7, 8 Exomizer, 17 AP-Ultra, 18 LZSA, 48 / 49, 70 / 71 ZX0)
std::optional<compress::Algorithm> CrunchAlgorithm(int code, int version);
/// rasm's IsRegister: a Z80 register name (upper case)
bool IsRegister(const std::string& word);
/// A name of rasm's function table (SIN, LO, GETNOP ...)
bool IsMath(const std::string& word);
/// rasm's TrimFloatingPointString: "%lf" text without a ".000000" fraction
std::string FormatDouble(double v);
}  // namespace unrealasm::rasm
