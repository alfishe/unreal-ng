// xtensa-jit-a64.inc - POC 051 (tools/poc/051-xtensa-jit), included at the end of xtensa-cpu.cpp: a region JIT for
// arm64 hosts. Never lands as is.
//
// A region is compiled from a hot block (its head) over every block reachable from it by direct branches and jumps,
// up to calls, returns, window changes and kinds the JIT does not know: the body of a function between its calls,
// with its loops. Inside the region branches between its blocks are native jumps, the guest registers it uses live in
// host registers (the window base cannot change inside a region) and the instruction budget is a host register:
//
//     uint64_t fn(uint32_t* ar, uint32_t wb4, CpuState* st, PageEntry* const* l1, uint32_t budget)
//     -> bits 0..31: instructions executed, bits 32..63: the guest pc to continue at (interpreter: chain)
//
// Every block of the region starts with the budget check: if the remaining budget cannot hold the whole block the
// region exits before it and the interpreter runs the cut block itself. Anything not certain on the fast path - a page
// miss or crossing, a store to a page with decoded code, an unknown kind - exits *before* that instruction, so the
// interpreter runs it with the same result it always gets. Exits write back every guest register the region writes.
//
// The interpreter enters a region only where it holds as a whole: no loop end with a live count inside its address
// range and no register group beyond the free ones (Exec in xtensa-cpu.cpp); neither can change inside a region.

#include "xtensa-cpu.h"

#if UNREAL_XTENSA_JIT

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <sys/mman.h>
#include <vector>

#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#endif

namespace xtensa
{

/// The one zero-overhead loop a region compiles: LBEG / LEND it was compiled for
struct RegionLoop
{
    bool has = false;
    uint32_t beg = 0;
    uint32_t end = 0;
};

struct RegionBlock
{
    uint32_t start;
    uint32_t end;
    uint32_t count;
    uint32_t native; ///< NativePrefix
    const Insn* insns;
};

namespace
{

// ---- arm64 encoder -----------------------------------------------------------------------------------------------

enum Cond : uint32_t
{
    EQ = 0, NE = 1, HS = 2, LO = 3, MI = 4, PL = 5, HI = 8, LS = 9, GE = 10, LT = 11, GT = 12, LE = 13
};

constexpr uint32_t ZR = 31;

/// Logical immediate (N:immr:imms) for a 32-bit pattern, or -1 if it has none.
int64_t LogicalImm32(uint32_t value)
{
    if (value == 0 || value == 0xFFFFFFFFu)
        return -1;
    for (uint32_t size = 2; size <= 32; size *= 2)
    {
        const uint32_t mask = size == 32 ? 0xFFFFFFFFu : ((1u << size) - 1);
        const uint32_t elem = value & mask;
        bool repeats = true;
        for (uint32_t i = size; i < 32; i += size)
            if (((value >> i) & mask) != elem)
                repeats = false;
        if (!repeats)
            continue;
        // elem must be a rotated run of ones
        for (uint32_t rot = 0; rot < size; ++rot)
        {
            const uint32_t r = size == 32 ? ((elem >> rot) | (elem << ((32 - rot) & 31))) & mask
                                          : (((elem >> rot) | (elem << (size - rot))) & mask);
            // r = 0...01...1 ?
            if ((r & (r + 1)) == 0 && r != 0)
            {
                uint32_t ones = 0;
                while ((r >> ones) & 1)
                    ++ones;
                // value = ROR(ones run, immr) within size: elem = ROL(r, rot) => immr = (size - rot) % size
                const uint32_t immr = (size - rot) % size;
                const uint32_t imms = ((~(size * 2 - 1)) & 0x3F) | (ones - 1);
                return static_cast<int64_t>((immr << 6) | (imms & 0x3F));
            }
        }
        return -1;
    }
    return -1;
}

class A64
{
public:
    std::vector<uint32_t> code;

    size_t Pos() const { return code.size(); }
    void Emit(uint32_t insn) { code.push_back(insn); }

    // data processing, 32-bit unless noted
    void AddReg(uint32_t d, uint32_t n, uint32_t m, uint32_t lsl = 0) { Emit(0x0B000000u | (m << 16) | (lsl << 10) | (n << 5) | d); }
    void SubReg(uint32_t d, uint32_t n, uint32_t m, uint32_t lsl = 0) { Emit(0x4B000000u | (m << 16) | (lsl << 10) | (n << 5) | d); }
    void CmpReg(uint32_t n, uint32_t m) { Emit(0x6B000000u | (m << 16) | (n << 5) | ZR); }
    void AndReg(uint32_t d, uint32_t n, uint32_t m) { Emit(0x0A000000u | (m << 16) | (n << 5) | d); }
    void OrrReg(uint32_t d, uint32_t n, uint32_t m) { Emit(0x2A000000u | (m << 16) | (n << 5) | d); }
    void EorReg(uint32_t d, uint32_t n, uint32_t m) { Emit(0x4A000000u | (m << 16) | (n << 5) | d); }
    void TstReg(uint32_t n, uint32_t m) { Emit(0x6A000000u | (m << 16) | (n << 5) | ZR); }
    void BicsZr(uint32_t n, uint32_t m) { Emit(0x6A200000u | (m << 16) | (n << 5) | ZR); } // flags of n & ~m
    void Mov(uint32_t d, uint32_t m) { if (d != m) Emit(0x2A0003E0u | (m << 16) | d); }
    void Neg(uint32_t d, uint32_t m) { Emit(0x4B0003E0u | (m << 16) | d); }
    void AddImm12(uint32_t d, uint32_t n, uint32_t imm, uint32_t sh = 0) { Emit(0x11000000u | (sh << 22) | (imm << 10) | (n << 5) | d); }
    void SubImm12(uint32_t d, uint32_t n, uint32_t imm, uint32_t sh = 0) { Emit(0x51000000u | (sh << 22) | (imm << 10) | (n << 5) | d); }
    void CmpImm12(uint32_t n, uint32_t imm) { Emit(0x71000000u | (imm << 10) | (n << 5) | ZR); }
    void Movz(uint32_t d, uint32_t imm16, uint32_t hw = 0) { Emit(0x52800000u | (hw << 21) | (imm16 << 5) | d); }
    void Movk(uint32_t d, uint32_t imm16, uint32_t hw) { Emit(0x72800000u | (hw << 21) | (imm16 << 5) | d); }
    void MovzX(uint32_t d, uint32_t imm16, uint32_t hw = 0) { Emit(0xD2800000u | (hw << 21) | (imm16 << 5) | d); }
    void MovkX(uint32_t d, uint32_t imm16, uint32_t hw) { Emit(0xF2800000u | (hw << 21) | (imm16 << 5) | d); }
    void Csel(uint32_t d, uint32_t n, uint32_t m, Cond c) { Emit(0x1A800000u | (m << 16) | (c << 12) | (n << 5) | d); }
    void Cset(uint32_t d, Cond c) { Emit(0x1A800400u | (ZR << 16) | ((c ^ 1u) << 12) | (ZR << 5) | d); }
    void Cneg(uint32_t d, uint32_t n, Cond c) { Emit(0x5A800400u | (n << 16) | ((c ^ 1u) << 12) | (n << 5) | d); }
    void Ubfm(uint32_t d, uint32_t n, uint32_t immr, uint32_t imms) { Emit(0x53000000u | (immr << 16) | (imms << 10) | (n << 5) | d); }
    void Sbfm(uint32_t d, uint32_t n, uint32_t immr, uint32_t imms) { Emit(0x13000000u | (immr << 16) | (imms << 10) | (n << 5) | d); }
    void UbfmX(uint32_t d, uint32_t n, uint32_t immr, uint32_t imms) { Emit(0xD3400000u | (immr << 16) | (imms << 10) | (n << 5) | d); }
    void SbfmX(uint32_t d, uint32_t n, uint32_t immr, uint32_t imms) { Emit(0x93400000u | (immr << 16) | (imms << 10) | (n << 5) | d); }
    void LslImm(uint32_t d, uint32_t n, uint32_t s) { Ubfm(d, n, (32 - s) & 31, 31 - s); }
    void LsrImm(uint32_t d, uint32_t n, uint32_t s) { Ubfm(d, n, s, 31); }
    void AsrImm(uint32_t d, uint32_t n, uint32_t s) { Sbfm(d, n, s, 31); }
    void Ubfx(uint32_t d, uint32_t n, uint32_t lsb, uint32_t width) { Ubfm(d, n, lsb, lsb + width - 1); }
    void Sbfx(uint32_t d, uint32_t n, uint32_t lsb, uint32_t width) { Sbfm(d, n, lsb, lsb + width - 1); }
    void Lslv(uint32_t d, uint32_t n, uint32_t m) { Emit(0x1AC02000u | (m << 16) | (n << 5) | d); }
    void Lsrv(uint32_t d, uint32_t n, uint32_t m) { Emit(0x1AC02400u | (m << 16) | (n << 5) | d); }
    void LsrvX(uint32_t d, uint32_t n, uint32_t m) { Emit(0x9AC02400u | (m << 16) | (n << 5) | d); }
    void AsrvX(uint32_t d, uint32_t n, uint32_t m) { Emit(0x9AC02800u | (m << 16) | (n << 5) | d); }
    void OrrRegX(uint32_t d, uint32_t n, uint32_t m, uint32_t lsl = 0) { Emit(0xAA000000u | (m << 16) | (lsl << 10) | (n << 5) | d); }
    void Mul(uint32_t d, uint32_t n, uint32_t m) { Emit(0x1B000000u | (m << 16) | (ZR << 10) | (n << 5) | d); }
    void Umull(uint32_t d, uint32_t n, uint32_t m) { Emit(0x9BA07C00u | (m << 16) | (n << 5) | d); }
    void Smull(uint32_t d, uint32_t n, uint32_t m) { Emit(0x9B207C00u | (m << 16) | (n << 5) | d); }
    void AndImm(uint32_t d, uint32_t n, uint32_t nimm) { Emit(0x12000000u | (nimm << 10) | (n << 5) | d); } // N:immr:imms (N = 0)

    // memory
    void LdrWReg(uint32_t t, uint32_t n, uint32_t m, bool scale) { Emit((scale ? 0xB8607800u : 0xB8606800u) | (m << 16) | (n << 5) | t); }
    void StrWReg(uint32_t t, uint32_t n, uint32_t m, bool scale) { Emit((scale ? 0xB8207800u : 0xB8206800u) | (m << 16) | (n << 5) | t); }
    void LdrhReg(uint32_t t, uint32_t n, uint32_t m) { Emit(0x78606800u | (m << 16) | (n << 5) | t); }
    void StrhReg(uint32_t t, uint32_t n, uint32_t m) { Emit(0x78206800u | (m << 16) | (n << 5) | t); }
    void LdrbReg(uint32_t t, uint32_t n, uint32_t m) { Emit(0x38606800u | (m << 16) | (n << 5) | t); }
    void StrbReg(uint32_t t, uint32_t n, uint32_t m) { Emit(0x38206800u | (m << 16) | (n << 5) | t); }
    void LdrXRegScaled(uint32_t t, uint32_t n, uint32_t m) { Emit(0xF8607800u | (m << 16) | (n << 5) | t); }
    void LdrWImm(uint32_t t, uint32_t n, uint32_t off) { Emit(0xB9400000u | ((off / 4) << 10) | (n << 5) | t); }
    void StrWImm(uint32_t t, uint32_t n, uint32_t off) { Emit(0xB9000000u | ((off / 4) << 10) | (n << 5) | t); }
    void LdrXImm(uint32_t t, uint32_t n, uint32_t off) { Emit(0xF9400000u | ((off / 8) << 10) | (n << 5) | t); }
    void StpPre(uint32_t t1, uint32_t t2, int32_t off) { Emit(0xA9800000u | ((static_cast<uint32_t>(off / 8) & 0x7F) << 15) | (t2 << 10) | (31u << 5) | t1); }
    void LdpPost(uint32_t t1, uint32_t t2, int32_t off) { Emit(0xA8C00000u | ((static_cast<uint32_t>(off / 8) & 0x7F) << 15) | (t2 << 10) | (31u << 5) | t1); }

    // floating point (S = single, D = double) and bit tests
    void FmovSW(uint32_t s, uint32_t w) { Emit(0x1E270000u | (w << 5) | s); }
    void FmovWS(uint32_t w, uint32_t s) { Emit(0x1E260000u | (s << 5) | w); }
    void FmovDX(uint32_t d, uint32_t x) { Emit(0x9E670000u | (x << 5) | d); }
    void LdrSImm(uint32_t s, uint32_t n, uint32_t off) { Emit(0xBD400000u | ((off / 4) << 10) | (n << 5) | s); }
    void FaddS(uint32_t d, uint32_t n, uint32_t m) { Emit(0x1E202800u | (m << 16) | (n << 5) | d); }
    void FsubS(uint32_t d, uint32_t n, uint32_t m) { Emit(0x1E203800u | (m << 16) | (n << 5) | d); }
    void FmulS(uint32_t d, uint32_t n, uint32_t m) { Emit(0x1E200800u | (m << 16) | (n << 5) | d); }
    void FmaddS(uint32_t d, uint32_t n, uint32_t m, uint32_t a) { Emit(0x1F000000u | (m << 16) | (a << 10) | (n << 5) | d); } // a + n * m
    void FmsubS(uint32_t d, uint32_t n, uint32_t m, uint32_t a) { Emit(0x1F008000u | (m << 16) | (a << 10) | (n << 5) | d); } // a - n * m
    void FcmpS(uint32_t n, uint32_t m) { Emit(0x1E202000u | (m << 16) | (n << 5)); }
    void FcmpD(uint32_t n, uint32_t m) { Emit(0x1E602000u | (m << 16) | (n << 5)); }
    void ScvtfSW(uint32_t s, uint32_t w) { Emit(0x1E220000u | (w << 5) | s); }
    void UcvtfSW(uint32_t s, uint32_t w) { Emit(0x1E230000u | (w << 5) | s); }
    void FcvtDS(uint32_t d, uint32_t s) { Emit(0x1E22C000u | (s << 5) | d); }
    void FmulD(uint32_t d, uint32_t n, uint32_t m) { Emit(0x1E600800u | (m << 16) | (n << 5) | d); }
    /// FCVTNS 0x1E600000, FCVTPS 0x1E680000, FCVTMS 0x1E700000, FCVTZS 0x1E780000, FCVTZU 0x1E790000 (w from d)
    void FcvtWD(uint32_t base, uint32_t w, uint32_t d) { Emit(base | (d << 5) | w); }
    void TstImm(uint32_t n, uint32_t mask) { Emit(0x72000000u | (static_cast<uint32_t>(LogicalImm32(mask)) << 10) | (n << 5) | ZR); }
    void Bfi(uint32_t d, uint32_t n, uint32_t lsb, uint32_t width) { Emit(0x33000000u | (((32 - lsb) & 31) << 16) | ((width - 1) << 10) | (n << 5) | d); }

    // control; the branch offsets are patched by Bind
    size_t B() { Emit(0x14000000u); return Pos() - 1; }
    size_t BCond(Cond c) { Emit(0x54000000u | c); return Pos() - 1; }
    size_t Cbz(uint32_t t) { Emit(0x34000000u | t); return Pos() - 1; }
    size_t Cbnz(uint32_t t) { Emit(0x35000000u | t); return Pos() - 1; }
    size_t CbzX(uint32_t t) { Emit(0xB4000000u | t); return Pos() - 1; }
    size_t CbnzX(uint32_t t) { Emit(0xB5000000u | t); return Pos() - 1; }
    size_t Tbz(uint32_t t, uint32_t bit) { Emit(0x36000000u | ((bit >> 5) << 31) | ((bit & 31) << 19) | t); return Pos() - 1; }
    size_t Tbnz(uint32_t t, uint32_t bit) { Emit(0x37000000u | ((bit >> 5) << 31) | ((bit & 31) << 19) | t); return Pos() - 1; }
    void Ret() { Emit(0xD65F03C0u); }

    void Bind(size_t at, size_t target)
    {
        const int32_t delta = static_cast<int32_t>(target) - static_cast<int32_t>(at);
        uint32_t& insn = code[at];
        if ((insn & 0xFC000000u) == 0x14000000u)
            insn |= static_cast<uint32_t>(delta) & 0x03FFFFFFu;
        else if ((insn & 0x7E000000u) == 0x36000000u) // tbz / tbnz
            insn |= (static_cast<uint32_t>(delta) & 0x3FFFu) << 5;
        else // b.cond, cbz, cbnz
            insn |= (static_cast<uint32_t>(delta) & 0x7FFFFu) << 5;
    }

    void MovImm32(uint32_t d, uint32_t v)
    {
        Movz(d, v & 0xFFFF);
        if (v >> 16)
            Movk(d, v >> 16, 1);
    }
    void MovImm64(uint32_t d, uint64_t v)
    {
        MovzX(d, static_cast<uint32_t>(v & 0xFFFF));
        for (uint32_t hw = 1; hw < 4; ++hw)
            if ((v >> (16 * hw)) & 0xFFFF)
                MovkX(d, static_cast<uint32_t>((v >> (16 * hw)) & 0xFFFF), hw);
    }
    /// d = n + imm (32-bit wrap)
    void AddConst(uint32_t d, uint32_t n, uint32_t imm, uint32_t scratch)
    {
        if (imm == 0)
            Mov(d, n);
        else if (imm < 4096)
            AddImm12(d, n, imm);
        else if ((0u - imm) < 4096)
            SubImm12(d, n, 0u - imm);
        else if ((imm & 0xFFF) == 0 && imm < (1u << 24))
            AddImm12(d, n, imm >> 12, 1);
        else
        {
            MovImm32(scratch, imm);
            AddReg(d, n, scratch);
        }
    }
};

// Host registers: x0 ar, x1 wb4, x2 st, x3 l1, x4 remaining budget, x8 budget at entry; x5-x7, x16, x17 scratch;
// s0-s3 floating-point scratch; guest registers in kPool (x19.. are saved when used).
constexpr uint32_t XAR = 0, XWB = 1, XST = 2, XL1 = 3, XREM = 4, XB0 = 8;
constexpr uint32_t T0 = 16, T1 = 17, T2 = 5, T3 = 6, T5 = 7;
constexpr uint32_t kPool[16] = {9, 10, 11, 12, 13, 14, 15, 19, 20, 21, 22, 23, 24, 25, 26, 27};

struct PageEntryLayout // Cpu::PageEntry, checked against the real one in Cpu::JitCompile
{
    uintptr_t bias;
    uint32_t flags;
    void* code;
};
static_assert(sizeof(PageEntryLayout) == 24, "PageEntry layout");

constexpr uint32_t FR(uint32_t n)
{
    return static_cast<uint32_t>(offsetof(CpuState, fr)) + 4 * (n & 15);
}

struct Use
{
    uint16_t read = 0;
    uint16_t write = 0;
    bool ok = false;
    bool ender = false;       ///< a control transfer the JIT compiles (the block's last instruction)
    bool conditional = false; ///< falls through when not taken
};

Use UseOf(const Insn& in)
{
    Use u;
    auto R = [&](uint8_t f) { u.read = static_cast<uint16_t>(u.read | (1u << (f & 15))); };
    auto W = [&](uint8_t f) { u.write = static_cast<uint16_t>(u.write | (1u << (f & 15))); };
    u.ok = true;
    switch (in.op)
    {
        case Op::Add: case Op::AddN: case Op::Addx2: case Op::Addx4: case Op::Addx8: case Op::Sub: case Op::Subx2:
        case Op::Subx4: case Op::Subx8: case Op::And: case Op::Or: case Op::Xor: case Op::Salt: case Op::Saltu:
        case Op::Min: case Op::Max: case Op::Minu: case Op::Maxu: case Op::Mul16u: case Op::Mul16s: case Op::Mull:
        case Op::Muluh: case Op::Mulsh: case Op::Src:
            R(in.b); R(in.c); W(in.a); break;
        case Op::Moveqz: case Op::Movnez: case Op::Movltz: case Op::Movgez:
            R(in.a); R(in.b); R(in.c); W(in.a); break;
        case Op::Movf: case Op::Movt:
            R(in.a); R(in.b); W(in.a); break;
        case Op::Neg: case Op::Abs: case Op::Srl: case Op::Sra:
            R(in.c); W(in.a); break;
        case Op::Addi: case Op::AddiN: case Op::Addmi: case Op::MovN: case Op::Sext: case Op::Slli: case Op::Srai:
        case Op::Srli: case Op::Extui: case Op::Sll:
        case Op::L8ui: case Op::L16ui: case Op::L16si: case Op::L32i: case Op::L32iN:
            R(in.b); W(in.a); break;
        case Op::Movi: case Op::MoviN: case Op::L32r:
            W(in.a); break;
        case Op::Ssr: case Op::Ssl: case Op::Ssa8l: case Op::Ssa8b:
            R(in.b); break;
        case Op::Ssai: case Op::Isync: case Op::Rsync: case Op::Esync: case Op::Dsync: case Op::Excw: case Op::Memw:
        case Op::Extw: case Op::Nop: case Op::NopN:
            break;
        case Op::S8i: case Op::S16i: case Op::S32i: case Op::S32iN: case Op::S32nb:
            R(in.a); R(in.b); break;
        // floating point: FA / FB / FC live in CpuState::fr
        case Op::Lsi: case Op::Ssi: case Op::Wfr: case Op::FloatS: case Op::UfloatS:
            R(in.b); break;
        case Op::Rfr: case Op::RoundS: case Op::TruncS: case Op::FloorS: case Op::CeilS: case Op::UtruncS:
            W(in.a); break;
        case Op::AddS: case Op::SubS: case Op::MulS: case Op::MaddS: case Op::MaddnS: case Op::MsubS: case Op::MovS:
        case Op::AbsS: case Op::NegS: case Op::UnS: case Op::OeqS: case Op::UeqS: case Op::OltS: case Op::UltS:
        case Op::OleS: case Op::UleS:
            break;
        case Op::Beqz: case Op::BeqzN: case Op::Bnez: case Op::BnezN: case Op::Bltz: case Op::Bgez: case Op::Beqi:
        case Op::Bnei: case Op::Blti: case Op::Bgei: case Op::Bltui: case Op::Bgeui: case Op::Bbci: case Op::Bbsi:
            R(in.b); u.ender = true; u.conditional = true; break;
        case Op::Bnone: case Op::Beq: case Op::Blt: case Op::Bltu: case Op::Ball: case Op::Bbc: case Op::Bany:
        case Op::Bne: case Op::Bge: case Op::Bgeu: case Op::Bnall: case Op::Bbs:
            R(in.b); R(in.c); u.ender = true; u.conditional = true; break;
        case Op::Bt: case Op::Bf:
            u.ender = true; u.conditional = true; break;
        case Op::Loop: case Op::Loopnez: case Op::Loopgtz: // fall through into the body (or branch past it)
            R(in.b); u.ender = true; u.conditional = true; break;
        case Op::J:
            u.ender = true; break;
        case Op::Jx:
            R(in.b); u.ender = true; break;
        case Op::Call0:
            W(0); u.ender = true; break;
        case Op::Callx0:
            R(in.b); W(0); u.ender = true; break;
        case Op::Call4: W(4); u.ender = true; break;
        case Op::Call8: W(8); u.ender = true; break;
        case Op::Call12: W(12); u.ender = true; break;
        case Op::Callx4: R(in.b); W(4); u.ender = true; break;
        case Op::Callx8: R(in.b); W(8); u.ender = true; break;
        case Op::Callx12: R(in.b); W(12); u.ender = true; break;
        case Op::Ret: case Op::RetN:
            R(0); u.ender = true; break;
        default:
            u.ok = false;
            break;
    }
    return u;
}

bool IsLoopOp(Op op)
{
    return op == Op::Loop || op == Op::Loopnez || op == Op::Loopgtz;
}

/// Instructions of a block the JIT compiles, from the first one (all of them = the block ends in the region). A LOOP
/// for another loop than the region's is left to the interpreter.
uint32_t NativePrefix(const Insn* insns, uint32_t count, const RegionLoop& loop)
{
    uint32_t n = 0;
    while (n < count && UseOf(insns[n]).ok &&
           !(IsLoopOp(insns[n].op) &&
             (!loop.has || insns[n].pc + 3 != loop.beg || static_cast<uint32_t>(insns[n].imm) != loop.end)))
        ++n;
    return n;
}


class RegionCompiler
{
public:
    RegionCompiler(const std::vector<RegionBlock>& blocks, const RegionLoop& loop, Cpu& cpu,
                   uint8_t* (*xlate)(Cpu&, uint32_t)) :
        _blocks(blocks), _loop(loop), _cpu(cpu), _xlate(xlate)
    {
        for (size_t i = 0; i < blocks.size(); ++i)
            _index[blocks[i].start] = i;
    }

    std::vector<uint32_t> Compile()
    {
        uint16_t used = 0;
        for (const RegionBlock& rb : _blocks)
            for (uint32_t k = 0; k < rb.native; ++k)
            {
                const Use u = UseOf(rb.insns[k]);
                used = static_cast<uint16_t>(used | u.read | u.write);
                _written = static_cast<uint16_t>(_written | u.write);
            }
        uint32_t slot = 0;
        for (uint32_t g = 0; g < 16; ++g)
            if (used & (1u << g))
                _host[g] = kPool[slot++];
        _saved = slot > 7 ? slot - 7 : 0;
        Prologue(used);

        _labels.assign(_blocks.size(), 0);
        for (size_t i = 0; i < _blocks.size(); ++i)
        {
            const RegionBlock& rb = _blocks[i];
            _labels[i] = _a.Pos();
            _cur = &rb;
            _curIndex = i;
            // the budget check: a block runs whole or not at all (the interpreter cuts it)
            _a.CmpImm12(XREM, rb.count);
            ExitTo(_a.BCond(LO), rb.start, 0);
            _a.SubImm12(XREM, XREM, rb.count);
            for (uint32_t k = 0; k < rb.native; ++k)
            {
                _k = k;
                if (!EmitInsn(rb.insns[k]))
                    return {};
            }
            if (rb.native < rb.count)
                ExitTo(_a.B(), rb.insns[rb.native].pc, rb.count - rb.native);
            else
            {
                const Use last = UseOf(rb.insns[rb.count - 1]);
                if (!last.ender || last.conditional)
                    SequentialEnd(rb.end);
            }
        }
        for (const Fixup& f : _toLabel)
            _a.Bind(f.at, _labels[f.block]);
        for (Stub& s : _stubs)
        {
            const size_t at = _a.Pos();
            for (size_t f : s.fixups)
                _a.Bind(f, at);
            EmitExit(s.pc, s.adjust, ZR);
        }
        return std::move(_a.code);
    }

private:
    struct Fixup
    {
        size_t at;
        size_t block;
    };
    struct Stub
    {
        uint32_t pc;
        uint32_t adjust;
        std::vector<size_t> fixups;
    };

    uint32_t H(uint8_t g) const { return _host[g & 15]; }

    void Prologue(uint16_t used)
    {
        for (uint32_t i = 0; i < _saved; i += 2)
            _a.StpPre(19 + i, 20 + i, -16);
        _a.Mov(XB0, XREM);
        for (uint32_t g = 0; g < 16; ++g)
            if (used & (1u << g))
            {
                _a.AddImm12(T3, XWB, g);
                _a.AndImm(T3, T3, static_cast<uint32_t>(LogicalImm32(63)));
                _a.LdrWReg(_host[g], XAR, T3, true);
            }
    }

    void Epilogue()
    {
        for (uint32_t i = (_saved + 1) & ~1u; i >= 2; i -= 2)
            _a.LdpPost(19 + i - 2, 20 + i - 2, 16);
        _a.Ret();
    }

    /// Writes back the registers, returns (pc << 32) | executed; executed = consumed budget - adjust.
    void EmitExit(uint32_t pc, uint32_t adjust, uint32_t pcReg)
    {
        for (uint32_t g = 0; g < 16; ++g)
            if (_written & (1u << g))
            {
                _a.AddImm12(T3, XWB, g);
                _a.AndImm(T3, T3, static_cast<uint32_t>(LogicalImm32(63)));
                _a.StrWReg(_host[g], XAR, T3, true);
            }
        _a.SubReg(T1, XB0, XREM);
        if (adjust)
            _a.SubImm12(T1, T1, adjust);
        if (pcReg == ZR)
        {
            _a.MovImm32(T0, pc);
            pcReg = T0;
        }
        _a.OrrRegX(XAR, T1, pcReg, 32);
        Epilogue();
    }

    /// A branch (fixup) to the exit continuing at `pc` (not running the `adjust` last counted instructions)
    void ExitTo(size_t fixup, uint32_t pc, uint32_t adjust)
    {
        for (Stub& s : _stubs)
            if (s.pc == pc && s.adjust == adjust)
            {
                s.fixups.push_back(fixup);
                return;
            }
        _stubs.push_back({pc, adjust, {fixup}});
    }

    /// Exit before the current instruction
    void Side(size_t fixup)
    {
        ExitTo(fixup, _cur->insns[_k].pc, _cur->count - _k);
    }

    /// A branch (fixup) to guest `pc` after the whole current block: a block of the region or an exit
    void BranchTo(size_t fixup, uint32_t pc)
    {
        auto it = _index.find(pc);
        if (it != _index.end())
            _toLabel.push_back({fixup, it->second});
        else
            ExitTo(fixup, pc, 0);
    }

    /// The sequential end of a block: at the region's loop end the loop-back check (blockEnd in Exec), then on
    void SequentialEnd(uint32_t pc)
    {
        if (_loop.has && pc == _loop.end)
        {
            // if (lend == pc && lcount != 0) { lcount--; pc = lbeg }
            _a.LdrWImm(T0, XST, offsetof(CpuState, lend));
            _a.MovImm32(T1, pc);
            _a.CmpReg(T0, T1);
            const size_t notEnd = _a.BCond(NE);
            _a.LdrWImm(T0, XST, offsetof(CpuState, lcount));
            const size_t done = _a.Cbz(T0);
            _a.SubImm12(T0, T0, 1);
            _a.StrWImm(T0, XST, offsetof(CpuState, lcount));
            _a.LdrWImm(T5, XST, offsetof(CpuState, lbeg));
            _a.MovImm32(T1, _loop.beg);
            _a.CmpReg(T5, T1);
            BranchTo(_a.BCond(EQ), _loop.beg);
            EmitExit(0, 0, T5); // LBEG changed under the region (a host write): continue there in the interpreter
            const size_t on = _a.Pos();
            _a.Bind(notEnd, on);
            _a.Bind(done, on);
        }
        Goto(pc);
    }

    void Goto(uint32_t pc)
    {
        auto it = _index.find(pc);
        if (it != _index.end() && it->second == _curIndex + 1)
            return; // the next block follows
        BranchTo(_a.B(), pc);
    }

    /// Fast-path address translation of T0 (the guest address); T1 = host bias, or a side exit.
    void Translate(uint32_t size, uint32_t flag, bool store)
    {
        _a.AndImm(T1, T0, static_cast<uint32_t>(LogicalImm32(0xFFF)));
        _a.CmpImm12(T1, 0x1000 - size);
        Side(_a.BCond(HI));
        _a.LsrImm(T1, T0, 22);
        _a.LdrXRegScaled(T1, XL1, T1);
        Side(_a.CbzX(T1));
        _a.Ubfx(T2, T0, 12, 10);
        _a.Emit(0x8B000000u | (T2 << 16) | (4u << 10) | (T1 << 5) | T1); // add x1, x1, x2, lsl #4
        _a.Emit(0x8B000000u | (T2 << 16) | (3u << 10) | (T1 << 5) | T1); // add x1, x1, x2, lsl #3 (24-byte entries)
        _a.LdrWImm(T2, T1, offsetof(PageEntryLayout, flags));
        _a.TstImm(T2, flag);
        Side(_a.BCond(EQ));
        if (store)
        {
            _a.LdrXImm(T2, T1, offsetof(PageEntryLayout, code));
            Side(_a.CbnzX(T2));
        }
        _a.LdrXImm(T1, T1, offsetof(PageEntryLayout, bias));
    }

    /// s-register result -> fr[a], any NaN as the default quiet NaN (Result in xtensa-cpu.cpp)
    void StoreFloatResult(uint32_t s, uint32_t a)
    {
        _a.FmovWS(T0, s);
        _a.FcmpS(s, s);
        _a.MovImm32(T1, 0x7FC00000u);
        _a.Csel(T0, T1, T0, static_cast<Cond>(6)); // VS
        _a.StrWImm(T0, XST, FR(a));
    }

    /// d1 = double(fr[b]) * 2^scale
    void ScaledDouble(uint32_t b, uint32_t scale)
    {
        _a.LdrSImm(0, XST, FR(b));
        _a.FcvtDS(1, 0);
        if (scale & 15)
        {
            _a.MovImm64(T0, static_cast<uint64_t>(1023 + (scale & 15)) << 52);
            _a.FmovDX(2, T0);
            _a.FmulD(1, 1, 2);
        }
    }

    bool EmitInsn(const Insn& in)
    {
        const uint32_t A = H(in.a), B = H(in.b), C = H(in.c);
        const uint32_t imm = static_cast<uint32_t>(in.imm);
        constexpr Cond VS = static_cast<Cond>(6);
        switch (in.op)
        {
            case Op::Add: case Op::AddN: _a.AddReg(A, B, C); return true;
            case Op::Addx2: _a.AddReg(A, C, B, 1); return true;
            case Op::Addx4: _a.AddReg(A, C, B, 2); return true;
            case Op::Addx8: _a.AddReg(A, C, B, 3); return true;
            case Op::Sub: _a.SubReg(A, B, C); return true;
            case Op::Subx2: _a.LslImm(T0, B, 1); _a.SubReg(A, T0, C); return true;
            case Op::Subx4: _a.LslImm(T0, B, 2); _a.SubReg(A, T0, C); return true;
            case Op::Subx8: _a.LslImm(T0, B, 3); _a.SubReg(A, T0, C); return true;
            case Op::And: _a.AndReg(A, B, C); return true;
            case Op::Or: _a.OrrReg(A, B, C); return true;
            case Op::Xor: _a.EorReg(A, B, C); return true;
            case Op::Neg: _a.Neg(A, C); return true;
            case Op::Abs: _a.CmpImm12(C, 0); _a.Cneg(A, C, LT); return true;
            case Op::Addi: case Op::AddiN: case Op::Addmi: _a.AddConst(A, B, imm, T0); return true;
            case Op::Movi: case Op::MoviN: _a.MovImm32(A, imm); return true;
            case Op::MovN: _a.Mov(A, B); return true;
            case Op::Salt: _a.CmpReg(B, C); _a.Cset(A, LT); return true;
            case Op::Saltu: _a.CmpReg(B, C); _a.Cset(A, LO); return true;
            case Op::Min: _a.CmpReg(B, C); _a.Csel(A, B, C, LT); return true;
            case Op::Max: _a.CmpReg(B, C); _a.Csel(A, C, B, LT); return true;
            case Op::Minu: _a.CmpReg(B, C); _a.Csel(A, B, C, LO); return true;
            case Op::Maxu: _a.CmpReg(B, C); _a.Csel(A, C, B, LO); return true;
            case Op::Moveqz: _a.CmpImm12(C, 0); _a.Csel(A, B, A, EQ); return true;
            case Op::Movnez: _a.CmpImm12(C, 0); _a.Csel(A, B, A, NE); return true;
            case Op::Movltz: _a.CmpImm12(C, 0); _a.Csel(A, B, A, LT); return true;
            case Op::Movgez: _a.CmpImm12(C, 0); _a.Csel(A, B, A, GE); return true;
            case Op::Movf:
                _a.LdrWImm(T0, XST, offsetof(CpuState, br));
                _a.TstImm(T0, 1u << (in.c & 15));
                _a.Csel(A, B, A, EQ);
                return true;
            case Op::Movt:
                _a.LdrWImm(T0, XST, offsetof(CpuState, br));
                _a.TstImm(T0, 1u << (in.c & 15));
                _a.Csel(A, B, A, NE);
                return true;
            case Op::Sext: _a.Sbfx(A, B, 0, imm + 1); return true;
            case Op::Mul16u: _a.Ubfx(T0, B, 0, 16); _a.Ubfx(T1, C, 0, 16); _a.Mul(A, T0, T1); return true;
            case Op::Mul16s: _a.Sbfx(T0, B, 0, 16); _a.Sbfx(T1, C, 0, 16); _a.Mul(A, T0, T1); return true;
            case Op::Mull: _a.Mul(A, B, C); return true;
            case Op::Muluh: _a.Umull(T0, B, C); _a.UbfmX(T0, T0, 32, 63); _a.Mov(A, T0); return true;
            case Op::Mulsh: _a.Smull(T0, B, C); _a.UbfmX(T0, T0, 32, 63); _a.Mov(A, T0); return true;
            case Op::Slli:
                if (imm >= 32)
                    _a.Movz(A, 0);
                else
                    _a.LslImm(A, B, imm);
                return true;
            case Op::Srai: _a.AsrImm(A, B, imm & 31); return true;
            case Op::Srli: _a.LsrImm(A, B, imm & 31); return true;
            case Op::Extui:
            {
                const uint32_t lsb = in.c & 31;
                uint32_t width = imm;
                if (lsb + width > 32)
                    width = 32 - lsb;
                _a.Ubfx(A, B, lsb, width);
                return true;
            }
            case Op::Src:
                _a.UbfmX(T0, B, 32, 31);
                _a.OrrRegX(T0, T0, C);
                _a.LdrWImm(T1, XST, offsetof(CpuState, sar));
                _a.LsrvX(T0, T0, T1);
                _a.Mov(A, T0);
                return true;
            case Op::Srl:
                _a.LdrWImm(T1, XST, offsetof(CpuState, sar));
                _a.Mov(T0, C);
                _a.LsrvX(T0, T0, T1);
                _a.Mov(A, T0);
                return true;
            case Op::Sll:
                _a.UbfmX(T0, B, 32, 31);
                _a.LdrWImm(T1, XST, offsetof(CpuState, sar));
                _a.LsrvX(T0, T0, T1);
                _a.Mov(A, T0);
                return true;
            case Op::Sra:
                _a.SbfmX(T0, C, 0, 31);
                _a.LdrWImm(T1, XST, offsetof(CpuState, sar));
                _a.AsrvX(T0, T0, T1);
                _a.Mov(A, T0);
                return true;
            case Op::Ssr: _a.AndImm(T0, B, static_cast<uint32_t>(LogicalImm32(31))); _a.StrWImm(T0, XST, offsetof(CpuState, sar)); return true;
            case Op::Ssl:
                _a.AndImm(T0, B, static_cast<uint32_t>(LogicalImm32(31)));
                _a.Movz(T1, 32);
                _a.SubReg(T0, T1, T0);
                _a.StrWImm(T0, XST, offsetof(CpuState, sar));
                return true;
            case Op::Ssa8l: _a.Ubfm(T0, B, 29, 1); _a.StrWImm(T0, XST, offsetof(CpuState, sar)); return true;
            case Op::Ssa8b:
                _a.Ubfm(T0, B, 29, 1);
                _a.Movz(T1, 32);
                _a.SubReg(T0, T1, T0);
                _a.StrWImm(T0, XST, offsetof(CpuState, sar));
                return true;
            case Op::Ssai: _a.Movz(T0, imm & 0xFFFF); _a.StrWImm(T0, XST, offsetof(CpuState, sar)); return true;
            case Op::Isync: case Op::Rsync: case Op::Esync: case Op::Dsync: case Op::Excw: case Op::Memw:
            case Op::Extw: case Op::Nop: case Op::NopN:
                return true;

            case Op::L8ui: case Op::L16ui: case Op::L16si: case Op::L32i: case Op::L32iN:
            {
                const uint32_t size = in.op == Op::L8ui ? 1 : (in.op == Op::L16ui || in.op == Op::L16si) ? 2 : 4;
                _a.AddConst(T0, B, imm, T1);
                Translate(size, RegionRead, false);
                if (size == 1)
                    _a.LdrbReg(A, T1, T0);
                else if (size == 2)
                {
                    _a.LdrhReg(A, T1, T0);
                    if (in.op == Op::L16si)
                        _a.Sbfx(A, A, 0, 16);
                }
                else
                    _a.LdrWReg(A, T1, T0, false);
                return true;
            }
            case Op::L32r:
            {
                const uint8_t* p = (imm & 0xFFF) <= 0x1000 - 4 ? _xlate(_cpu, imm) : nullptr;
                if (!p)
                {
                    Side(_a.B());
                    return true;
                }
                _a.MovImm64(T0, reinterpret_cast<uintptr_t>(p));
                _a.LdrWImm(A, T0, 0);
                return true;
            }
            case Op::S8i: case Op::S16i: case Op::S32i: case Op::S32iN: case Op::S32nb:
            {
                const uint32_t size = in.op == Op::S8i ? 1 : in.op == Op::S16i ? 2 : 4;
                _a.AddConst(T0, B, imm, T1);
                Translate(size, RegionWrite, true);
                if (size == 1)
                    _a.StrbReg(A, T1, T0);
                else if (size == 2)
                    _a.StrhReg(A, T1, T0);
                else
                    _a.StrWReg(A, T1, T0, false);
                return true;
            }

            // ---- floating point ----
            case Op::Lsi:
                _a.AddConst(T0, B, imm, T1);
                Translate(4, RegionRead, false);
                _a.LdrWReg(T2, T1, T0, false);
                _a.StrWImm(T2, XST, FR(in.a));
                return true;
            case Op::Ssi:
                _a.AddConst(T0, B, imm, T1);
                Translate(4, RegionWrite, true);
                _a.LdrWImm(T2, XST, FR(in.a));
                _a.StrWReg(T2, T1, T0, false);
                return true;
            case Op::Wfr: _a.StrWImm(B, XST, FR(in.a)); return true;
            case Op::Rfr: _a.LdrWImm(A, XST, FR(in.b)); return true;
            case Op::MovS: _a.LdrWImm(T0, XST, FR(in.b)); _a.StrWImm(T0, XST, FR(in.a)); return true;
            case Op::AbsS:
                _a.LdrWImm(T0, XST, FR(in.b));
                _a.AndImm(T0, T0, static_cast<uint32_t>(LogicalImm32(0x7FFFFFFFu)));
                _a.StrWImm(T0, XST, FR(in.a));
                return true;
            case Op::NegS:
                _a.LdrWImm(T0, XST, FR(in.b));
                _a.MovImm32(T1, 0x80000000u);
                _a.EorReg(T0, T0, T1);
                _a.StrWImm(T0, XST, FR(in.a));
                return true;
            case Op::AddS: case Op::SubS: case Op::MulS:
                _a.LdrSImm(1, XST, FR(in.b));
                _a.LdrSImm(2, XST, FR(in.c));
                if (in.op == Op::AddS)
                    _a.FaddS(0, 1, 2);
                else if (in.op == Op::SubS)
                    _a.FsubS(0, 1, 2);
                else
                    _a.FmulS(0, 1, 2);
                StoreFloatResult(0, in.a);
                return true;
            case Op::MaddS: case Op::MaddnS: case Op::MsubS:
                _a.LdrSImm(1, XST, FR(in.b));
                _a.LdrSImm(2, XST, FR(in.c));
                _a.LdrSImm(3, XST, FR(in.a));
                if (in.op == Op::MsubS)
                    _a.FmsubS(0, 1, 2, 3); // fa - fb * fc, one rounding = fma(-fb, fc, fa)
                else
                    _a.FmaddS(0, 1, 2, 3);
                StoreFloatResult(0, in.a);
                return true;
            case Op::FloatS: case Op::UfloatS:
                if (in.op == Op::FloatS)
                    _a.ScvtfSW(0, B);
                else
                    _a.UcvtfSW(0, B);
                if (imm & 15)
                {
                    // * 2^-scale: exact, the conversion rounded once as in the interpreter
                    _a.MovImm32(T0, static_cast<uint32_t>(127 - (imm & 15)) << 23);
                    _a.FmovSW(1, T0);
                    _a.FmulS(0, 0, 1);
                }
                _a.FmovWS(T0, 0);
                _a.StrWImm(T0, XST, FR(in.a));
                return true;
            case Op::RoundS: case Op::TruncS: case Op::FloorS: case Op::CeilS:
            {
                const uint32_t base = in.op == Op::RoundS ? 0x1E600000u : in.op == Op::TruncS ? 0x1E780000u
                                    : in.op == Op::FloorS ? 0x1E700000u : 0x1E680000u;
                ScaledDouble(in.b, imm);
                _a.FcvtWD(base, A, 1); // saturating, ties to even for ROUND.S
                _a.FcmpD(1, 1);
                _a.MovImm32(T0, 0x7FFFFFFFu);
                _a.Csel(A, T0, A, VS); // NaN
                return true;
            }
            case Op::UtruncS:
                ScaledDouble(in.b, imm);
                _a.FcvtWD(0x1E780000u, T0, 1);
                _a.FcvtWD(0x1E790000u, T2, 1);
                _a.LdrWImm(T1, XST, FR(in.b));
                _a.TstImm(T1, 0x80000000u);
                _a.Csel(A, T0, T2, NE); // negative values convert as TRUNC.S
                _a.FcmpD(1, 1);
                _a.MovImm32(T0, 0xFFFFFFFFu);
                _a.Csel(A, T0, A, VS);
                return true;
            case Op::UnS: case Op::OeqS: case Op::UeqS: case Op::OltS: case Op::UltS: case Op::OleS: case Op::UleS:
            {
                _a.LdrSImm(1, XST, FR(in.b));
                _a.LdrSImm(2, XST, FR(in.c));
                _a.FcmpS(1, 2);
                switch (in.op)
                {
                    case Op::UnS: _a.Cset(T0, VS); break;
                    case Op::OeqS: _a.Cset(T0, EQ); break;
                    case Op::UeqS: _a.Cset(T0, EQ); _a.Cset(T1, VS); _a.OrrReg(T0, T0, T1); break;
                    case Op::OltS: _a.Cset(T0, MI); break;
                    case Op::UltS: _a.Cset(T0, LT); break;
                    case Op::OleS: _a.Cset(T0, LS); break;
                    default: _a.Cset(T0, LE); break;
                }
                _a.LdrWImm(T1, XST, offsetof(CpuState, br));
                _a.Bfi(T1, T0, in.a & 15, 1);
                _a.StrWImm(T1, XST, offsetof(CpuState, br));
                return true;
            }

            // ---- control transfers (the block's last instruction) ----
            case Op::Beqz: case Op::BeqzN: BranchTo(_a.Cbz(B), imm); return true;
            case Op::Bnez: case Op::BnezN: BranchTo(_a.Cbnz(B), imm); return true;
            case Op::Bltz: _a.CmpImm12(B, 0); BranchTo(_a.BCond(LT), imm); return true;
            case Op::Bgez: _a.CmpImm12(B, 0); BranchTo(_a.BCond(GE), imm); return true;
            case Op::Beqi: case Op::Bnei: case Op::Blti: case Op::Bgei:
                _a.MovImm32(T0, static_cast<uint32_t>(kB4Const[in.c & 15]));
                _a.CmpReg(B, T0);
                BranchTo(_a.BCond(in.op == Op::Beqi ? EQ : in.op == Op::Bnei ? NE : in.op == Op::Blti ? LT : GE), imm);
                return true;
            case Op::Bltui: case Op::Bgeui:
                _a.MovImm32(T0, kB4ConstU[in.c & 15]);
                _a.CmpReg(B, T0);
                BranchTo(_a.BCond(in.op == Op::Bltui ? LO : HS), imm);
                return true;
            case Op::Bbci: _a.TstImm(B, 1u << (in.c & 31)); BranchTo(_a.BCond(EQ), imm); return true;
            case Op::Bbsi: _a.TstImm(B, 1u << (in.c & 31)); BranchTo(_a.BCond(NE), imm); return true;
            case Op::Bbc: _a.Lsrv(T0, B, C); _a.TstImm(T0, 1); BranchTo(_a.BCond(EQ), imm); return true;
            case Op::Bbs: _a.Lsrv(T0, B, C); _a.TstImm(T0, 1); BranchTo(_a.BCond(NE), imm); return true;
            case Op::Bnone: _a.TstReg(B, C); BranchTo(_a.BCond(EQ), imm); return true;
            case Op::Bany: _a.TstReg(B, C); BranchTo(_a.BCond(NE), imm); return true;
            case Op::Ball: _a.BicsZr(C, B); BranchTo(_a.BCond(EQ), imm); return true;
            case Op::Bnall: _a.BicsZr(C, B); BranchTo(_a.BCond(NE), imm); return true;
            case Op::Beq: _a.CmpReg(B, C); BranchTo(_a.BCond(EQ), imm); return true;
            case Op::Bne: _a.CmpReg(B, C); BranchTo(_a.BCond(NE), imm); return true;
            case Op::Blt: _a.CmpReg(B, C); BranchTo(_a.BCond(LT), imm); return true;
            case Op::Bge: _a.CmpReg(B, C); BranchTo(_a.BCond(GE), imm); return true;
            case Op::Bltu: _a.CmpReg(B, C); BranchTo(_a.BCond(LO), imm); return true;
            case Op::Bgeu: _a.CmpReg(B, C); BranchTo(_a.BCond(HS), imm); return true;
            case Op::Bt: case Op::Bf:
                _a.LdrWImm(T0, XST, offsetof(CpuState, br));
                _a.TstImm(T0, 1u << (in.b & 15));
                BranchTo(_a.BCond(in.op == Op::Bt ? NE : EQ), imm);
                return true;
            case Op::Loop: case Op::Loopnez: case Op::Loopgtz:
                _a.SubImm12(T0, B, 1);
                _a.StrWImm(T0, XST, offsetof(CpuState, lcount));
                _a.MovImm32(T0, in.pc + 3);
                _a.StrWImm(T0, XST, offsetof(CpuState, lbeg));
                _a.MovImm32(T0, imm);
                _a.StrWImm(T0, XST, offsetof(CpuState, lend));
                if (in.op == Op::Loopnez)
                    BranchTo(_a.Cbz(B), imm);
                else if (in.op == Op::Loopgtz)
                {
                    _a.CmpImm12(B, 0);
                    BranchTo(_a.BCond(LE), imm);
                }
                return true;
            case Op::J: BranchTo(_a.B(), imm); return true;
            case Op::Jx: _a.Mov(T5, B); EmitExit(0, 0, T5); return true;
            case Op::Ret: case Op::RetN: _a.Mov(T5, H(0)); EmitExit(0, 0, T5); return true;
            case Op::Call0:
                _a.MovImm32(H(0), in.pc + 3);
                StoreCallKind(0);
                EmitExit(imm, 0, ZR);
                return true;
            case Op::Callx0:
                _a.Mov(T5, B);
                _a.MovImm32(H(0), in.pc + 3);
                StoreCallKind(0);
                EmitExit(0, 0, T5);
                return true;
            case Op::Call4: case Op::Call8: case Op::Call12: case Op::Callx4: case Op::Callx8: case Op::Callx12:
            {
                const uint32_t nn = (in.op == Op::Call4 || in.op == Op::Callx4) ? 1 : (in.op == Op::Call8 || in.op == Op::Callx8) ? 2 : 3;
                const bool indirect = in.op == Op::Callx4 || in.op == Op::Callx8 || in.op == Op::Callx12;
                if (indirect)
                    _a.Mov(T5, B);
                _a.MovImm32(H(static_cast<uint8_t>(nn * 4)), (nn << 30) | ((in.pc + 3) & 0x3FFFFFFFu));
                _a.LdrWImm(T0, XST, offsetof(CpuState, ps));
                _a.MovImm32(T1, ~0x30000u);
                _a.AndReg(T0, T0, T1);
                _a.MovImm32(T1, nn << 16);
                _a.OrrReg(T0, T0, T1);
                _a.StrWImm(T0, XST, offsetof(CpuState, ps));
                StoreCallKind(nn);
                EmitExit(imm, 0, indirect ? T5 : ZR);
                return true;
            }
            default:
                return false;
        }
    }

    void StoreCallKind(uint32_t n)
    {
        _a.Movz(T0, n);
        _a.StrWImm(T0, XST, offsetof(CpuState, callKind));
    }

    const std::vector<RegionBlock>& _blocks;
    RegionLoop _loop;
    Cpu& _cpu;
    uint8_t* (*_xlate)(Cpu&, uint32_t);
    std::map<uint32_t, size_t> _index;
    A64 _a;
    uint32_t _host[16] = {};
    uint32_t _saved = 0;
    uint16_t _written = 0;
    std::vector<size_t> _labels;
    std::vector<Fixup> _toLabel;
    std::vector<Stub> _stubs;
    const RegionBlock* _cur = nullptr;
    size_t _curIndex = 0;
    uint32_t _k = 0;
};

} // namespace

// ---- arena -------------------------------------------------------------------------------------------------------

struct Cpu::JitArena
{
    static constexpr size_t kSize = 64u << 20;
    uint8_t* base = nullptr;
    size_t used = 0;

    JitArena()
    {
        int flags = MAP_PRIVATE | MAP_ANON;
#if defined(__APPLE__)
        flags |= MAP_JIT;
#endif
        void* p = mmap(nullptr, kSize, PROT_READ | PROT_WRITE | PROT_EXEC, flags, -1, 0);
        base = p == MAP_FAILED ? nullptr : static_cast<uint8_t*>(p);
    }
    ~JitArena()
    {
        if (base)
            munmap(base, kSize);
    }
};

void Cpu::JitArenaDelete::operator()(JitArena* a) const
{
    delete a;
}

void Cpu::SetJit(bool on, uint32_t hot)
{
    jitOn_ = on;
    jitHot_ = hot == 0 ? 1 : hot;
}

void Cpu::JitInitFromEnv()
{
    if (const char* e = std::getenv("UNREAL_XTENSA_JIT"))
        jitOn_ = e[0] == '1';
    if (const char* e = std::getenv("UNREAL_XTENSA_JIT_HOT"))
        jitHot_ = static_cast<uint32_t>(std::max(1, std::atoi(e)));
}

void Cpu::JitCompile(Block& head)
{
    static_assert(sizeof(PageEntry) == sizeof(PageEntryLayout), "PageEntry layout");
    static_assert(offsetof(PageEntry, bias) == offsetof(PageEntryLayout, bias), "PageEntry layout");
    static_assert(offsetof(PageEntry, flags) == offsetof(PageEntryLayout, flags), "PageEntry layout");
    static_assert(offsetof(PageEntry, code) == offsetof(PageEntryLayout, code), "PageEntry layout");
    static_assert(sizeof(l1_[0]) == sizeof(void*), "l1_ entries are plain pointers");
    ++jitStats_.compiles;

    // the region: blocks reachable from the head by direct branches, jumps, fall-throughs and its loop's back edge.
    // Its loop: the live one, else the first LOOP found (a second pass then splits the blocks at its end).
    RegionLoop loop;
    if (state_.lcount != 0)
        loop = {true, state_.lbeg, state_.lend};
    std::vector<RegionBlock> blocks;
    for (int pass = 0; pass < 2; ++pass)
    {
        blocks.clear();
        RegionLoop found;
        if (!Discover(head.start, loop, blocks, found))
            break;
        if (loop.has || !found.has)
            break;
        loop = found;
    }
    if (blocks.empty() || blocks[0].native == 0)
    {
        ++jitStats_.rejected;
        return;
    }

    RegionCompiler c(blocks, loop, *this, [](Cpu& cpu, uint32_t addr) { return cpu.Xlate(addr, RegionRead); });
    const std::vector<uint32_t> code = c.Compile();
    if (code.empty())
    {
        ++jitStats_.rejected;
        return;
    }
    uint32_t lo = 0xFFFFFFFFu, hi = 0;
    uint8_t group = 0;
    for (const RegionBlock& rb : blocks)
    {
        lo = std::min(lo, rb.start);
        hi = std::max(hi, rb.end);
        for (uint32_t k = 0; k < rb.native; ++k)
            group = std::max(group, rb.insns[k].maxGroup);
    }

    if (!jitArena_)
        jitArena_.reset(new JitArena);
    if (!jitArena_->base)
        return;
    const size_t bytes = code.size() * 4;
    if (bytes > JitArena::kSize)
        return;
    if (jitArena_->used + bytes > JitArena::kSize)
    {
        // full: forget every region (none is running: compiling happens between blocks)
        for (auto& kv : blockMap_)
        {
            kv.second->jit = nullptr;
            kv.second->jitTried = false;
            kv.second->hot = 0;
        }
        jitArena_->used = 0;
        ++jitStats_.flushes;
    }
    uint8_t* at = jitArena_->base + jitArena_->used;
#if defined(__APPLE__)
    pthread_jit_write_protect_np(0);
#endif
    std::memcpy(at, code.data(), bytes);
#if defined(__APPLE__)
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(at, bytes);
#else
    __builtin___clear_cache(reinterpret_cast<char*>(at), reinterpret_cast<char*>(at + bytes));
#endif
    jitArena_->used += (bytes + 15) & ~size_t{15};
    jitStats_.bytes += bytes;
    jitStats_.regionBlocks += blocks.size();
    head.jit = at;
    head.jitLo = lo;
    head.jitHi = hi;
    head.jitGroup = group;
    head.jitLoopEnd = loop.has ? loop.end : 0;
    head.jitHasLoop = loop.has;
}

bool Cpu::Discover(uint32_t headPc, const RegionLoop& loop, std::vector<RegionBlock>& blocks, RegionLoop& found)
{
    constexpr size_t kMaxBlocks = 64;
    constexpr uint32_t kMaxInsns = 2048;
    std::map<uint32_t, size_t> seen;
    std::vector<uint32_t> queue{headPc};
    uint32_t total = 0;
    for (size_t q = 0; q < queue.size() && blocks.size() < kMaxBlocks && total < kMaxInsns; ++q)
    {
        const uint32_t pc = queue[q];
        if (seen.count(pc) || InTrapRange(pc) || ((pc ^ sentinel_) & 0x3FFFFFFFu) == 0)
            continue;
        Block* b = Lookup(pc);
        if (!b)
            b = Build(pc);
        if (!b)
            continue;
        // a block running over the loop end ends there in the region (the interpreter cuts it there too)
        uint32_t count = b->count;
        uint32_t end = b->end;
        if (loop.has && loop.end - b->start - 1 < b->end - b->start - 1)
            for (uint32_t i = 1; i < b->count; ++i)
                if (b->insns[i].pc == loop.end)
                {
                    count = i;
                    end = loop.end;
                    break;
                }
        const uint32_t native = NativePrefix(b->insns.data(), count, loop);
        if (native == 0 && !blocks.empty())
            continue; // nothing native: an exit to it costs less than a block that exits at once
        for (uint32_t k = 0; k < count; ++k)
            if (IsLoopOp(b->insns[k].op) && !found.has)
                found = {true, b->insns[k].pc + 3, static_cast<uint32_t>(b->insns[k].imm)};
        seen[pc] = blocks.size();
        blocks.push_back({b->start, end, count, native, b->insns.data()});
        total += count;
        if (native < count)
            continue;
        const Insn& last = b->insns[count - 1];
        const Use u = UseOf(last);
        if (!u.ender || u.conditional)
        {
            queue.push_back(end);
            if (loop.has && end == loop.end)
                queue.push_back(loop.beg);
        }
        if (u.ender && (u.conditional || last.op == Op::J) && !(last.op == Op::Loop))
            queue.push_back(static_cast<uint32_t>(last.imm));
    }
    return !blocks.empty();
}

} // namespace xtensa

#endif // UNREAL_XTENSA_JIT
