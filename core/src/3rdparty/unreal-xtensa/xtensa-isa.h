// xtensa-isa.h - instruction decoder and disassembler of the Xtensa LX7 core as configured in the ESP32-S3.
//
// Part of unreal-xtensa (namespace xtensa), a dependency-free user-mode interpreter. See xtensa-cpu.h for the
// execution engine and its API.
//
// Decode() turns one instruction (2, 3 or 4 bytes, little endian) into an Insn: an operation code plus operand fields
// whose meaning depends on the operation (listed in the X-macro table below). Disassemble() prints an Insn in the
// syntax of binutils objdump (xtensa-esp32s3-elf-objdump -d), without the "<symbol>" and "(literal)" annotations.
//
// Encodings covered: the core ISA, code density, windowed registers, MUL16 / MUL32 / MUL32_HIGH / DIV32, loops,
// booleans, MIN/MAX, SEXT, CLAMPS, NSA, S32C1I, SALT/SALTU, the single precision FPU with LSIP/LSXP post-increment
// forms and the division/square root step instructions, the special and user registers the core has, and the system
// instructions (RFE, RSIL, ...). MAC16 and the ESP32-S3 PIE extension (EE.*, op0 = 0xE/0xF, 4 bytes) decode to
// Op::Mac16 / Op::EeInsn with their correct length and no operands; the interpreter treats them as unimplemented.

#ifndef UNREAL_XTENSA_ISA_H
#define UNREAL_XTENSA_ISA_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace xtensa
{

// Operand format tokens (space separated, used by the disassembler and to find the AR registers an instruction
// touches): r = AR register, f = FR register, b = boolean register, followed by the field (a, b or c).
// i = Insn::imm (decimal when -256 < v < 256, else 0x hex, as objdump), t = Insn::imm as an absolute target,
// n<f> = field as a number, k<f> / K<f> = B4CONST / B4CONSTU table entry of the field, g4<f> / g8<f> = boolean group.
// clang-format off
#define XTENSA_OPS(X) \
    X(Illegal,   "(illegal)", "")          \
    X(Ill,       "ill",       "")          \
    X(IllN,      "ill.n",     "")          \
    /* ALU, a = ar, b = as, c = at */      \
    X(Add,       "add",       "ra rb rc")  \
    X(AddN,      "add.n",     "ra rb rc")  \
    X(Addx2,     "addx2",     "ra rb rc")  \
    X(Addx4,     "addx4",     "ra rb rc")  \
    X(Addx8,     "addx8",     "ra rb rc")  \
    X(Sub,       "sub",       "ra rb rc")  \
    X(Subx2,     "subx2",     "ra rb rc")  \
    X(Subx4,     "subx4",     "ra rb rc")  \
    X(Subx8,     "subx8",     "ra rb rc")  \
    X(And,       "and",       "ra rb rc")  \
    X(Or,        "or",        "ra rb rc")  \
    X(Xor,       "xor",       "ra rb rc")  \
    X(Neg,       "neg",       "ra rc")     \
    X(Abs,       "abs",       "ra rc")     \
    X(Addi,      "addi",      "ra rb i")   \
    X(AddiN,     "addi.n",    "ra rb i")   \
    X(Addmi,     "addmi",     "ra rb i")   \
    X(Movi,      "movi",      "ra i")      \
    X(MoviN,     "movi.n",    "ra i")      \
    X(MovN,      "mov.n",     "ra rb")     \
    X(Salt,      "salt",      "ra rb rc")  \
    X(Saltu,     "saltu",     "ra rb rc")  \
    X(Min,       "min",       "ra rb rc")  \
    X(Max,       "max",       "ra rb rc")  \
    X(Minu,      "minu",      "ra rb rc")  \
    X(Maxu,      "maxu",      "ra rb rc")  \
    X(Moveqz,    "moveqz",    "ra rb rc")  \
    X(Movnez,    "movnez",    "ra rb rc")  \
    X(Movltz,    "movltz",    "ra rb rc")  \
    X(Movgez,    "movgez",    "ra rb rc")  \
    X(Movf,      "movf",      "ra rb bc")  \
    X(Movt,      "movt",      "ra rb bc")  \
    X(Sext,      "sext",      "ra rb i")   \
    X(Clamps,    "clamps",    "ra rb i")   \
    X(Nsa,       "nsa",       "ra rb")     \
    X(Nsau,      "nsau",      "ra rb")     \
    X(Mul16u,    "mul16u",    "ra rb rc")  \
    X(Mul16s,    "mul16s",    "ra rb rc")  \
    X(Mull,      "mull",      "ra rb rc")  \
    X(Muluh,     "muluh",     "ra rb rc")  \
    X(Mulsh,     "mulsh",     "ra rb rc")  \
    X(Quou,      "quou",      "ra rb rc")  \
    X(Quos,      "quos",      "ra rb rc")  \
    X(Remu,      "remu",      "ra rb rc")  \
    X(Rems,      "rems",      "ra rb rc")  \
    /* shifts; slli a = ar, b = as, imm = amount; srai / srli b = at */ \
    X(Slli,      "slli",      "ra rb i")   \
    X(Srai,      "srai",      "ra rb i")   \
    X(Srli,      "srli",      "ra rb i")   \
    X(Src,       "src",       "ra rb rc")  \
    X(Srl,       "srl",       "ra rc")     \
    X(Sll,       "sll",       "ra rb")     \
    X(Sra,       "sra",       "ra rc")     \
    X(Ssr,       "ssr",       "rb")        \
    X(Ssl,       "ssl",       "rb")        \
    X(Ssa8l,     "ssa8l",     "rb")        \
    X(Ssa8b,     "ssa8b",     "rb")        \
    X(Ssai,      "ssai",      "i")         \
    X(Extui,     "extui",     "ra rb nc i") \
    /* booleans: a = br, b = bs, c = bt; anyN / allN a = bt, b = bs */ \
    X(Andb,      "andb",      "ba bb bc")  \
    X(Andbc,     "andbc",     "ba bb bc")  \
    X(Orb,       "orb",       "ba bb bc")  \
    X(Orbc,      "orbc",      "ba bb bc")  \
    X(Xorb,      "xorb",      "ba bb bc")  \
    X(Any4,      "any4",      "ba g4b")    \
    X(All4,      "all4",      "ba g4b")    \
    X(Any8,      "any8",      "ba g8b")    \
    X(All8,      "all8",      "ba g8b")    \
    /* loads / stores: a = at, b = as, imm = byte offset */ \
    X(L8ui,      "l8ui",      "ra rb i")   \
    X(L16ui,     "l16ui",     "ra rb i")   \
    X(L16si,     "l16si",     "ra rb i")   \
    X(L32i,      "l32i",      "ra rb i")   \
    X(L32iN,     "l32i.n",    "ra rb i")   \
    X(L32ai,     "l32ai",     "ra rb i")   \
    X(S8i,       "s8i",       "ra rb i")   \
    X(S16i,      "s16i",      "ra rb i")   \
    X(S32i,      "s32i",      "ra rb i")   \
    X(S32iN,     "s32i.n",    "ra rb i")   \
    X(S32ri,     "s32ri",     "ra rb i")   \
    X(S32nb,     "s32nb",     "ra rb i")   \
    X(S32c1i,    "s32c1i",    "ra rb i")   \
    X(L32e,      "l32e",      "ra rb i")   \
    X(S32e,      "s32e",      "ra rb i")   \
    X(L32r,      "l32r",      "ra t")      \
    /* control: imm = absolute target */   \
    X(Call0,     "call0",     "t")         \
    X(Call4,     "call4",     "t")         \
    X(Call8,     "call8",     "t")         \
    X(Call12,    "call12",    "t")         \
    X(Callx0,    "callx0",    "rb")        \
    X(Callx4,    "callx4",    "rb")        \
    X(Callx8,    "callx8",    "rb")        \
    X(Callx12,   "callx12",   "rb")        \
    X(J,         "j",         "t")         \
    X(Jx,        "jx",        "rb")        \
    X(Ret,       "ret",       "")          \
    X(RetN,      "ret.n",     "")          \
    X(Retw,      "retw",      "")          \
    X(RetwN,     "retw.n",    "")          \
    X(Entry,     "entry",     "rb i")      \
    X(Movsp,     "movsp",     "ra rb")     \
    X(Rotw,      "rotw",      "i")         \
    /* branches: b = as, c = at / bit / const index */ \
    X(Beqz,      "beqz",      "rb t")      \
    X(Bnez,      "bnez",      "rb t")      \
    X(Bltz,      "bltz",      "rb t")      \
    X(Bgez,      "bgez",      "rb t")      \
    X(BeqzN,     "beqz.n",    "rb t")      \
    X(BnezN,     "bnez.n",    "rb t")      \
    X(Beqi,      "beqi",      "rb kc t")   \
    X(Bnei,      "bnei",      "rb kc t")   \
    X(Blti,      "blti",      "rb kc t")   \
    X(Bgei,      "bgei",      "rb kc t")   \
    X(Bltui,     "bltui",     "rb Kc t")   \
    X(Bgeui,     "bgeui",     "rb Kc t")   \
    X(Bnone,     "bnone",     "rb rc t")   \
    X(Beq,       "beq",       "rb rc t")   \
    X(Blt,       "blt",       "rb rc t")   \
    X(Bltu,      "bltu",      "rb rc t")   \
    X(Ball,      "ball",      "rb rc t")   \
    X(Bbc,       "bbc",       "rb rc t")   \
    X(Bbci,      "bbci",      "rb nc t")   \
    X(Bany,      "bany",      "rb rc t")   \
    X(Bne,       "bne",       "rb rc t")   \
    X(Bge,       "bge",       "rb rc t")   \
    X(Bgeu,      "bgeu",      "rb rc t")   \
    X(Bnall,     "bnall",     "rb rc t")   \
    X(Bbs,       "bbs",       "rb rc t")   \
    X(Bbsi,      "bbsi",      "rb nc t")   \
    X(Bf,        "bf",        "bb t")      \
    X(Bt,        "bt",        "bb t")      \
    X(Loop,      "loop",      "rb t")      \
    X(Loopnez,   "loopnez",   "rb t")      \
    X(Loopgtz,   "loopgtz",   "rb t")      \
    /* special / user registers: a = at (rur: ar), c = register number (Insn::c holds 8 bits) */ \
    X(Rsr,       "rsr",       "ra")        \
    X(Wsr,       "wsr",       "ra")        \
    X(Xsr,       "xsr",       "ra")        \
    X(Rur,       "rur",       "ra")        \
    X(Wur,       "wur",       "ra")        \
    /* synchronisation, no operation */    \
    X(Isync,     "isync",     "")          \
    X(Rsync,     "rsync",     "")          \
    X(Esync,     "esync",     "")          \
    X(Dsync,     "dsync",     "")          \
    X(Excw,      "excw",      "")          \
    X(Memw,      "memw",      "")          \
    X(Extw,      "extw",      "")          \
    X(Nop,       "nop",       "")          \
    X(NopN,      "nop.n",     "")          \
    /* stops */                            \
    X(Break,     "break",     "nb nc")     \
    X(BreakN,    "break.n",   "i")         \
    X(Syscall,   "syscall",   "")          \
    X(Simcall,   "simcall",   "")          \
    /* privileged system instructions (decoded for diagnostics, stop when executed) */ \
    X(Rsil,      "rsil",      "ra i")      \
    X(Waiti,     "waiti",     "i")         \
    X(Rfe,       "rfe",       "")          \
    X(Rfue,      "rfue",      "")          \
    X(Rfde,      "rfde",      "")          \
    X(Rfwo,      "rfwo",      "")          \
    X(Rfwu,      "rfwu",      "")          \
    X(Rfi,       "rfi",       "i")         \
    X(Rer,       "rer",       "ra rb")     \
    X(Wer,       "wer",       "ra rb")     \
    /* FPU: a = fr / ar / br, b = fs / as, c = ft / at / bt */ \
    X(AddS,      "add.s",     "fa fb fc")  \
    X(SubS,      "sub.s",     "fa fb fc")  \
    X(MulS,      "mul.s",     "fa fb fc")  \
    X(MaddS,     "madd.s",    "fa fb fc")  \
    X(MsubS,     "msub.s",    "fa fb fc")  \
    X(MaddnS,    "maddn.s",   "fa fb fc")  \
    X(DivnS,     "divn.s",    "fa fb fc")  \
    X(RoundS,    "round.s",   "ra fb i")   \
    X(TruncS,    "trunc.s",   "ra fb i")   \
    X(FloorS,    "floor.s",   "ra fb i")   \
    X(CeilS,     "ceil.s",    "ra fb i")   \
    X(UtruncS,   "utrunc.s",  "ra fb i")   \
    X(FloatS,    "float.s",   "fa rb i")   \
    X(UfloatS,   "ufloat.s",  "fa rb i")   \
    X(MovS,      "mov.s",     "fa fb")     \
    X(AbsS,      "abs.s",     "fa fb")     \
    X(NegS,      "neg.s",     "fa fb")     \
    X(Rfr,       "rfr",       "ra fb")     \
    X(Wfr,       "wfr",       "fa rb")     \
    X(ConstS,    "const.s",   "fa i")      \
    X(Div0S,     "div0.s",    "fa fb")     \
    X(Recip0S,   "recip0.s",  "fa fb")     \
    X(Sqrt0S,    "sqrt0.s",   "fa fb")     \
    X(Rsqrt0S,   "rsqrt0.s",  "fa fb")     \
    X(Nexp01S,   "nexp01.s",  "fa fb")     \
    X(MksadjS,   "mksadj.s",  "fa fb")     \
    X(MkdadjS,   "mkdadj.s",  "fa fb")     \
    X(AddexpS,   "addexp.s",  "fa fb")     \
    X(AddexpmS,  "addexpm.s", "fa fb")     \
    X(UnS,       "un.s",      "ba fb fc")  \
    X(OeqS,      "oeq.s",     "ba fb fc")  \
    X(UeqS,      "ueq.s",     "ba fb fc")  \
    X(OltS,      "olt.s",     "ba fb fc")  \
    X(UltS,      "ult.s",     "ba fb fc")  \
    X(OleS,      "ole.s",     "ba fb fc")  \
    X(UleS,      "ule.s",     "ba fb fc")  \
    X(MoveqzS,   "moveqz.s",  "fa fb rc")  \
    X(MovnezS,   "movnez.s",  "fa fb rc")  \
    X(MovltzS,   "movltz.s",  "fa fb rc")  \
    X(MovgezS,   "movgez.s",  "fa fb rc")  \
    X(MovfS,     "movf.s",    "fa fb bc")  \
    X(MovtS,     "movt.s",    "fa fb bc")  \
    /* FPU loads / stores: a = ft / fr, b = as, c = at (indexed), imm = offset */ \
    X(Lsi,       "lsi",       "fa rb i")   \
    X(Lsip,      "lsip",      "fa rb i")   \
    X(Ssi,       "ssi",       "fa rb i")   \
    X(Ssip,      "ssip",      "fa rb i")   \
    X(Lsx,       "lsx",       "fa rb rc")  \
    X(Lsxp,      "lsxp",      "fa rb rc")  \
    X(Ssx,       "ssx",       "fa rb rc")  \
    X(Ssxp,      "ssxp",      "fa rb rc")  \
    /* not implemented by the interpreter, decoded for length only */ \
    X(Mac16,     "(mac16)",   "")          \
    X(EeInsn,    "(ee)",      "")          \
    /* interpreter pseudo-op closing a predecoded block, never produced by Decode() */ \
    X(BlockEnd,  "(end)",     "")
// clang-format on

enum class Op : uint16_t
{
#define XTENSA_OP_ENUM(name, mnemonic, format) name,
    XTENSA_OPS(XTENSA_OP_ENUM)
#undef XTENSA_OP_ENUM
        Count
};

/// One decoded instruction. 16 bytes; also the element of the interpreter's predecoded blocks.
struct Insn
{
    Op op = Op::Illegal;
    uint8_t len = 0;      ///< 2, 3 or 4 bytes
    uint8_t a = 0;        ///< operand fields, meaning per operation (table above)
    uint8_t b = 0;
    uint8_t c = 0;
    uint8_t maxGroup = 0; ///< highest AR register group (reg / 4) the instruction touches, for the window check
    uint8_t flags = 0;    ///< InsnFlag bits
    int32_t imm = 0;      ///< immediate, byte offset or absolute target
    uint32_t pc = 0;      ///< address of the instruction
};
static_assert(sizeof(Insn) == 16, "Insn is meant to stay 16 bytes");

enum InsnFlag : uint8_t
{
    InsnEndsBlock = 1, ///< control transfer, window or loop register change, or a stop: last instruction of a block
};

/// Special register numbers used by the interpreter.
enum SpecialRegister : uint8_t
{
    SrLbeg = 0,
    SrLend = 1,
    SrLcount = 2,
    SrSar = 3,
    SrBr = 4,
    SrScompare1 = 12,
    SrAtomctl = 99,
    SrWindowBase = 72,
    SrWindowStart = 73,
    SrEps2 = 194,
    SrCpEnable = 224,
    SrPs = 230,
    SrCcount = 234,
};

enum UserRegister : uint8_t
{
    UrThreadPtr = 231,
    UrFcr = 232,
    UrFsr = 233,
};

/// Decodes the instruction at `pc` from `bytes` (at least `avail` readable bytes; 4 are enough for any
/// instruction). Returns false when `avail` is shorter than the instruction. Unknown encodings give Op::Illegal
/// with the length from op0.
bool Decode(uint32_t pc, const uint8_t* bytes, size_t avail, Insn& out);

/// Instruction length from its first byte (2, 3 or 4).
inline int InsnLength(uint8_t firstByte)
{
    const int op0 = firstByte & 15;
    return op0 >= 14 ? 4 : (op0 >= 8 ? 2 : 3);
}

/// objdump-style text: mnemonic, a tab, operands ("l32i.n\ta2, a1, 4"); targets as bare hex.
std::string Disassemble(const Insn& insn);

const char* Mnemonic(Op op);
const char* SpecialRegisterName(uint32_t sr); ///< nullptr when the ESP32-S3 has no such register
const char* UserRegisterName(uint32_t ur);    ///< nullptr when the ESP32-S3 has no such register

extern const int32_t kB4Const[16];
extern const uint32_t kB4ConstU[16];

} // namespace xtensa

#endif // UNREAL_XTENSA_ISA_H
