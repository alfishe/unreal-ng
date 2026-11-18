// xtensa-jit-a64.inc - POC 051 (included at the end of xtensa-cpu.cpp) (tools/poc/051-xtensa-jit): a block JIT for arm64 hosts. Never lands as is.
//
// A compiled block is a native prefix of an interpreter block: it runs the block's instructions from the first one
// while they are of the kinds it knows and hands the rest back to the interpreter at an instruction boundary:
//
//     uint64_t fn(uint32_t* ar, uint32_t wb4, CpuState* st, PageEntry* const* l1)
//     -> bits 0..7: index k of the next instruction of the block to run (interpreter: ip = begin + k)
//        bit 8:     the instruction at k was a taken control transfer; bits 32..63 = its target (interpreter: taken)
//
// Anything not certain on the fast path - a page miss, a page crossing, a store to a page with decoded code, a
// division, a window or loop register - exits *before* that instruction: the interpreter runs it and gets the
// same result it always gets. The window base cannot change inside a block, so the guest registers the prefix uses
// live in host registers between the prologue and an exit; every exit writes back the ones written so far.
//
// The interpreter calls a compiled block only where it would run the whole block itself: not cut by the budget or a
// loop end, the window check not needed (Exec in xtensa-cpu.cpp).

#include "xtensa-cpu.h"

#if UNREAL_XTENSA_JIT

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sys/mman.h>
#include <vector>

#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#endif

namespace xtensa
{

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

// Host registers. x0 ar, x1 wb4, x2 st, x3 l1; x4-x8, x16, x17 scratch; guest registers in kPool.
constexpr uint32_t XAR = 0, XWB = 1, XST = 2, XL1 = 3;
constexpr uint32_t T0 = 16, T1 = 17, T2 = 4, T3 = 5, T4 = 6, T5 = 7;
constexpr uint32_t kPool[16] = {9, 10, 11, 12, 13, 14, 15, 19, 20, 21, 22, 23, 24, 25, 26, 27};

struct PageEntryLayout // Cpu::PageEntry, checked against the real one in Cpu::JitCompile
{
    uintptr_t bias;
    uint32_t flags;
    void* code;
};
static_assert(sizeof(PageEntryLayout) == 24, "PageEntry layout");

struct Use
{
    uint16_t read = 0;
    uint16_t write = 0;
    bool ok = false;
    bool ender = false; ///< a control transfer the JIT compiles (the block's last instruction)
};

Use UseOf(const Insn& in)
{
    Use u;
    auto R = [&](uint8_t f) { u.read = static_cast<uint16_t>(u.read | (1u << f)); };
    auto W = [&](uint8_t f) { u.write = static_cast<uint16_t>(u.write | (1u << f)); };
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
        case Op::Beqz: case Op::BeqzN: case Op::Bnez: case Op::BnezN: case Op::Bltz: case Op::Bgez: case Op::Beqi:
        case Op::Bnei: case Op::Blti: case Op::Bgei: case Op::Bltui: case Op::Bgeui: case Op::Bbci: case Op::Bbsi:
            R(in.b); u.ender = true; break;
        case Op::Bnone: case Op::Beq: case Op::Blt: case Op::Bltu: case Op::Ball: case Op::Bbc: case Op::Bany:
        case Op::Bne: case Op::Bge: case Op::Bgeu: case Op::Bnall: case Op::Bbs:
            R(in.b); R(in.c); u.ender = true; break;
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

class Compiler
{
public:
    Compiler(const Insn* insns, uint32_t count, Cpu& cpu, uint8_t* (*xlate)(Cpu&, uint32_t)) :
        _insns(insns), _count(count), _cpu(cpu), _xlate(xlate)
    {
    }

    /// The native code (empty if the block has no prefix worth compiling).
    std::vector<uint32_t> Compile()
    {
        // the prefix and the guest registers it uses
        uint16_t used = 0;
        uint32_t n = 0;
        bool endsWithTransfer = false;
        for (; n < _count; ++n)
        {
            const Use u = UseOf(_insns[n]);
            if (!u.ok)
                break;
            used = static_cast<uint16_t>(used | u.read | u.write);
            if (u.ender)
            {
                endsWithTransfer = true;
                ++n;
                break;
            }
        }
        if (n == 0 || (n < 2 && !endsWithTransfer))
            return {};
        _prefix = n;

        uint32_t slot = 0;
        for (uint32_t g = 0; g < 16; ++g)
            if (used & (1u << g))
                _host[g] = kPool[slot++];
        _saved = slot > 7 ? slot - 7 : 0; // x19.. used
        Prologue(used);

        uint16_t dirty = 0;
        for (uint32_t i = 0; i < n; ++i)
        {
            const Insn& in = _insns[i];
            const Use u = UseOf(in);
            if (!EmitInsn(in, i, dirty))
                return {};
            dirty = static_cast<uint16_t>(dirty | u.write);
        }
        if (!endsWithTransfer)
            ExitAt(n, dirty); // fall into the rest of the block / its BlockEnd in the interpreter
        else if (UseOf(_insns[n - 1]).ender && IsConditional(_insns[n - 1].op))
            ExitAt(n, dirty); // not taken: the BlockEnd follows

        // out-of-line exits
        for (const Pending& p : _pending)
        {
            const size_t at = _a.Pos();
            for (size_t f : p.fixups)
                _a.Bind(f, at);
            EmitExit(p.index, p.dirty, p.taken, p.targetReg, p.target);
        }
        return std::move(_a.code);
    }

private:
    struct Pending
    {
        uint32_t index;
        uint16_t dirty;
        bool taken;
        uint32_t targetReg; ///< ZR = constant target
        uint32_t target;
        std::vector<size_t> fixups;
    };

    static bool IsConditional(Op op)
    {
        switch (op)
        {
            case Op::J: case Op::Jx: case Op::Call0: case Op::Callx0: case Op::Call4: case Op::Call8:
            case Op::Call12: case Op::Callx4: case Op::Callx8: case Op::Callx12: case Op::Ret: case Op::RetN:
                return false;
            default:
                return true;
        }
    }

    uint32_t H(uint8_t g) const { return _host[g & 15]; }

    void Prologue(uint16_t used)
    {
        for (uint32_t i = 0; i < _saved; i += 2)
            _a.StpPre(19 + i, 20 + i, -16);
        for (uint32_t g = 0; g < 16; ++g)
            if (used & (1u << g))
            {
                _a.AddImm12(T3, XWB, g);
                _a.AndImm(T3, T3, static_cast<uint32_t>(LogicalImm32(63)));
                _a.LdrWReg(_host[g], XAR, T3, true);
            }
    }

    void WriteBack(uint16_t dirty)
    {
        for (uint32_t g = 0; g < 16; ++g)
            if (dirty & (1u << g))
            {
                _a.AddImm12(T3, XWB, g);
                _a.AndImm(T3, T3, static_cast<uint32_t>(LogicalImm32(63)));
                _a.StrWReg(_host[g], XAR, T3, true);
            }
    }

    void Epilogue()
    {
        for (uint32_t i = (_saved + 1) & ~1u; i >= 2; i -= 2)
            _a.LdpPost(19 + i - 2, 20 + i - 2, 16);
        _a.Ret();
    }

    void EmitExit(uint32_t index, uint16_t dirty, bool taken, uint32_t targetReg, uint32_t target)
    {
        // the target register may be a guest register: build the result before the write-back reuses nothing of it
        if (taken)
        {
            if (targetReg == ZR)
                _a.MovImm64(T1, (static_cast<uint64_t>(target) << 32) | 0x100u | index);
            else
            {
                _a.MovImm32(T1, 0x100u | index);
                _a.OrrRegX(T1, T1, targetReg, 32);
            }
        }
        else
            _a.MovImm32(T1, index);
        WriteBack(dirty);
        _a.OrrRegX(XAR, ZR, T1); // mov x0, x17 (the target is in the upper half)
        Epilogue();
    }

    /// Exit before instruction `index` (the interpreter runs it)
    void ExitAt(uint32_t index, uint16_t dirty)
    {
        EmitExit(index, dirty, false, ZR, 0);
    }

    /// A forward branch to an out-of-line exit before `index`
    void Side(size_t fixup, uint32_t index, uint16_t dirty)
    {
        for (Pending& p : _pending)
            if (!p.taken && p.index == index && p.dirty == dirty)
            {
                p.fixups.push_back(fixup);
                return;
            }
        _pending.push_back({index, dirty, false, ZR, 0, {fixup}});
    }

    void TakenSide(size_t fixup, uint32_t index, uint16_t dirty, uint32_t target)
    {
        _pending.push_back({index, dirty, true, ZR, target, {fixup}});
    }

    /// Fast-path address translation: T0 = ea (zero-extended). On success T1 = host page base bias, else side exit.
    void Translate(uint32_t size, uint32_t flag, bool store, uint32_t index, uint16_t dirty)
    {
        _a.AndImm(T1, T0, static_cast<uint32_t>(LogicalImm32(0xFFF)));
        _a.CmpImm12(T1, 0x1000 - size);
        Side(_a.BCond(HI), index, dirty);
        _a.LsrImm(T1, T0, 22);
        _a.LdrXRegScaled(T1, XL1, T1);
        Side(_a.CbzX(T1), index, dirty);
        _a.Ubfx(T2, T0, 12, 10);
        // T1 += T2 * 24
        _a.Emit(0x8B000000u | (T2 << 16) | (4u << 10) | (T1 << 5) | T1); // add x, x, x2, lsl #4
        _a.Emit(0x8B000000u | (T2 << 16) | (3u << 10) | (T1 << 5) | T1); // add x, x, x2, lsl #3
        _a.LdrWImm(T2, T1, offsetof(PageEntryLayout, flags));
        uint32_t bit = 0;
        while (!((flag >> bit) & 1))
            ++bit;
        Side(_a.Tbz(T2, bit), index, dirty);
        if (store)
        {
            _a.LdrXImm(T2, T1, offsetof(PageEntryLayout, code));
            Side(_a.CbnzX(T2), index, dirty);
        }
        _a.LdrXImm(T1, T1, offsetof(PageEntryLayout, bias));
    }

    void Branch(Cond c, const Insn& in, uint32_t i, uint16_t dirty)
    {
        TakenSide(_a.BCond(c), i, dirty, static_cast<uint32_t>(in.imm));
    }

    bool EmitInsn(const Insn& in, uint32_t i, uint16_t dirty)
    {
        const uint32_t A = H(in.a), B = H(in.b), C = H(in.c);
        const uint32_t imm = static_cast<uint32_t>(in.imm);
        switch (in.op)
        {
            case Op::Add: case Op::AddN: _a.AddReg(A, B, C); return true;
            case Op::Addx2: return FixShifted(A, B, C, 1, true);
            case Op::Addx4: return FixShifted(A, B, C, 2, true);
            case Op::Addx8: return FixShifted(A, B, C, 3, true);
            case Op::Sub: _a.SubReg(A, B, C); return true;
            case Op::Subx2: return FixShifted(A, B, C, 1, false);
            case Op::Subx4: return FixShifted(A, B, C, 2, false);
            case Op::Subx8: return FixShifted(A, B, C, 3, false);
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
                _a.UbfmX(T0, B, 32, 31); // x = B << 32
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
                _a.SbfmX(T0, C, 0, 31); // sxtw
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
            case Op::Ssa8l: _a.Ubfm(T0, B, 29, 1); _a.StrWImm(T0, XST, offsetof(CpuState, sar)); return true; // (B & 3) << 3
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
                Translate(size, RegionRead, false, i, dirty);
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
                // the literal's host address is fixed while the block lives (a mapping change drops all blocks)
                const uint8_t* p = (imm & 0xFFF) <= 0x1000 - 4 ? _xlate(_cpu, imm) : nullptr;
                if (!p)
                    return EmitSideAt(i, dirty);
                _a.MovImm64(T0, reinterpret_cast<uintptr_t>(p));
                _a.LdrWImm(A, T0, 0);
                return true;
            }
            case Op::S8i: case Op::S16i: case Op::S32i: case Op::S32iN: case Op::S32nb:
            {
                const uint32_t size = in.op == Op::S8i ? 1 : in.op == Op::S16i ? 2 : 4;
                _a.AddConst(T0, B, imm, T1);
                Translate(size, RegionWrite, true, i, dirty);
                if (size == 1)
                    _a.StrbReg(A, T1, T0);
                else if (size == 2)
                    _a.StrhReg(A, T1, T0);
                else
                    _a.StrWReg(A, T1, T0, false);
                return true;
            }

            // ---- control transfers (the prefix's last instruction) ----
            case Op::Beqz: case Op::BeqzN: TakenSide(_a.Cbz(B), i, dirty, imm); return true;
            case Op::Bnez: case Op::BnezN: TakenSide(_a.Cbnz(B), i, dirty, imm); return true;
            case Op::Bltz: TakenSide(_a.Tbnz(B, 31), i, dirty, imm); return true;
            case Op::Bgez: TakenSide(_a.Tbz(B, 31), i, dirty, imm); return true;
            case Op::Beqi: case Op::Bnei: case Op::Blti: case Op::Bgei:
                _a.MovImm32(T0, static_cast<uint32_t>(kB4Const[in.c & 15]));
                _a.CmpReg(B, T0);
                Branch(in.op == Op::Beqi ? EQ : in.op == Op::Bnei ? NE : in.op == Op::Blti ? LT : GE, in, i, dirty);
                return true;
            case Op::Bltui: case Op::Bgeui:
                _a.MovImm32(T0, kB4ConstU[in.c & 15]);
                _a.CmpReg(B, T0);
                Branch(in.op == Op::Bltui ? LO : HS, in, i, dirty);
                return true;
            case Op::Bbci: TakenSide(_a.Tbz(B, in.c & 31), i, dirty, imm); return true;
            case Op::Bbsi: TakenSide(_a.Tbnz(B, in.c & 31), i, dirty, imm); return true;
            case Op::Bbc: _a.Lsrv(T0, B, C); TakenSide(_a.Tbz(T0, 0), i, dirty, imm); return true;
            case Op::Bbs: _a.Lsrv(T0, B, C); TakenSide(_a.Tbnz(T0, 0), i, dirty, imm); return true;
            case Op::Bnone: _a.TstReg(B, C); Branch(EQ, in, i, dirty); return true;
            case Op::Bany: _a.TstReg(B, C); Branch(NE, in, i, dirty); return true;
            case Op::Ball: _a.BicsZr(C, B); Branch(EQ, in, i, dirty); return true; // (~B & C) == 0
            case Op::Bnall: _a.BicsZr(C, B); Branch(NE, in, i, dirty); return true;
            case Op::Beq: _a.CmpReg(B, C); Branch(EQ, in, i, dirty); return true;
            case Op::Bne: _a.CmpReg(B, C); Branch(NE, in, i, dirty); return true;
            case Op::Blt: _a.CmpReg(B, C); Branch(LT, in, i, dirty); return true;
            case Op::Bge: _a.CmpReg(B, C); Branch(GE, in, i, dirty); return true;
            case Op::Bltu: _a.CmpReg(B, C); Branch(LO, in, i, dirty); return true;
            case Op::Bgeu: _a.CmpReg(B, C); Branch(HS, in, i, dirty); return true;
            case Op::J: EmitExit(i, dirty, true, ZR, imm); return true;
            case Op::Jx: _a.Mov(T5, B); EmitExit(i, dirty, true, T5, 0); return true;
            case Op::Ret: case Op::RetN: _a.Mov(T5, H(0)); EmitExit(i, dirty, true, T5, 0); return true;
            case Op::Call0:
                _a.MovImm32(H(0), in.pc + 3);
                StoreCallKind(0);
                EmitExit(i, static_cast<uint16_t>(dirty | 1u), true, ZR, imm);
                return true;
            case Op::Callx0:
                _a.Mov(T5, B);
                _a.MovImm32(H(0), in.pc + 3);
                StoreCallKind(0);
                EmitExit(i, static_cast<uint16_t>(dirty | 1u), true, T5, 0);
                return true;
            case Op::Call4: case Op::Call8: case Op::Call12: case Op::Callx4: case Op::Callx8: case Op::Callx12:
            {
                const uint32_t nn = (in.op == Op::Call4 || in.op == Op::Callx4) ? 1 : (in.op == Op::Call8 || in.op == Op::Callx8) ? 2 : 3;
                const bool indirect = in.op == Op::Callx4 || in.op == Op::Callx8 || in.op == Op::Callx12;
                if (indirect)
                    _a.Mov(T5, B);
                _a.MovImm32(H(static_cast<uint8_t>(nn * 4)), (nn << 30) | ((in.pc + 3) & 0x3FFFFFFFu));
                // ps = (ps & ~0x30000) | (n << 16)
                _a.LdrWImm(T0, XST, offsetof(CpuState, ps));
                _a.MovImm32(T1, ~0x30000u);
                _a.AndReg(T0, T0, T1);
                _a.MovImm32(T1, nn << 16);
                _a.OrrReg(T0, T0, T1);
                _a.StrWImm(T0, XST, offsetof(CpuState, ps));
                StoreCallKind(nn);
                EmitExit(i, static_cast<uint16_t>(dirty | (1u << (nn * 4))), true, indirect ? T5 : ZR, imm);
                return true;
            }
            default:
                return false;
        }
    }

    bool EmitSideAt(uint32_t i, uint16_t dirty)
    {
        Side(_a.B(), i, dirty);
        return true;
    }

    void StoreCallKind(uint32_t n)
    {
        _a.Movz(T0, n);
        _a.StrWImm(T0, XST, offsetof(CpuState, callKind));
    }

    /// Addx (add = true): A = (B << s) + C; Subx: A = (B << s) - C
    bool FixShifted(uint32_t A, uint32_t B, uint32_t C, uint32_t s, bool add)
    {
        if (add)
            _a.AddReg(A, C, B, s);
        else
        {
            _a.LslImm(T0, B, s);
            _a.SubReg(A, T0, C);
        }
        return true;
    }

    const Insn* _insns;
    uint32_t _count;
    Cpu& _cpu;
    uint8_t* (*_xlate)(Cpu&, uint32_t);
    A64 _a;
    uint32_t _host[16] = {};
    uint32_t _saved = 0;
    uint32_t _prefix = 0;
    std::vector<Pending> _pending;
};

} // namespace

// ---- arena -------------------------------------------------------------------------------------------------------

struct Cpu::JitArena
{
    static constexpr size_t kSize = 32u << 20;
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

void* Cpu::JitCompile(Block& b)
{
    static_assert(sizeof(PageEntry) == sizeof(PageEntryLayout), "PageEntry layout");
    static_assert(offsetof(PageEntry, bias) == offsetof(PageEntryLayout, bias), "PageEntry layout");
    static_assert(offsetof(PageEntry, flags) == offsetof(PageEntryLayout, flags), "PageEntry layout");
    static_assert(offsetof(PageEntry, code) == offsetof(PageEntryLayout, code), "PageEntry layout");
    static_assert(sizeof(l1_[0]) == sizeof(void*), "l1_ entries are plain pointers");

    Compiler c(b.insns.data(), b.count, *this, [](Cpu& cpu, uint32_t addr) { return cpu.Xlate(addr, RegionRead); });
    const std::vector<uint32_t> code = c.Compile();
    ++jitStats_.compiles;
    if (code.empty())
    {
        ++jitStats_.rejected;
        return nullptr;
    }
    if (!jitArena_)
        jitArena_.reset(new JitArena);
    if (!jitArena_->base)
        return nullptr;
    const size_t bytes = code.size() * 4;
    if (jitArena_->used + bytes > JitArena::kSize)
    {
        // full: forget every compiled block (none is running: compiling happens between blocks)
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
    return at;
}

} // namespace xtensa

#endif // UNREAL_XTENSA_JIT
