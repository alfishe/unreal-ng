// xtensa-jit-x64.inc - POC 053: the x86_64 backend of the region JIT (xtensa-jit.inc includes it). System V calling
// convention (Linux, macOS); on Windows x64 a prologue moves the arguments there and saves rsi / rdi.
//
// Host registers: r14 st, r15 the remaining budget; rax, rcx, rdx, r11 scratch (r11 holds a computed exit pc);
// xmm0-xmm3 floating-point scratch. Guest registers live in rbx, rbp, rsi, rdi, r8, r9, r10, r12, r13 (the most used
// ones of the region) and in frame slots past those nine. The frame keeps ar, l1, wb4 and the budget at entry.

namespace xtensa
{
namespace
{

// ---- x86_64 encoder ----------------------------------------------------------------------------------------------

enum Gpr : uint32_t
{
    RAX = 0, RCX = 1, RDX = 2, RBX = 3, RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8 = 8, R9 = 9, R10 = 10, R11 = 11, R12 = 12, R13 = 13, R14 = 14, R15 = 15
};

enum Cc : uint8_t
{
    CO = 0, CB = 2, CAE = 3, CE = 4, CNE = 5, CBE = 6, CA = 7, CS = 8, CNS = 9, CP = 10, CNP = 11, CL = 12, CGE = 13,
    CLE = 14, CG = 15
};

constexpr uint32_t kNoIndex = 0xFF;

struct Mem
{
    uint32_t base;
    int32_t disp = 0;
    uint32_t index = kNoIndex;
    uint32_t scale = 0; ///< log2
};

class X64
{
public:
    std::vector<uint8_t> code;

    size_t Pos() const { return code.size(); }
    void B(uint32_t b) { code.push_back(static_cast<uint8_t>(b)); }
    void D32(uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            B((v >> (8 * i)) & 0xFF);
    }
    void D64(uint64_t v)
    {
        D32(static_cast<uint32_t>(v));
        D32(static_cast<uint32_t>(v >> 32));
    }

    void Rex(bool w, uint32_t r, uint32_t x, uint32_t b, bool force)
    {
        const uint32_t v = 0x40u | (w ? 8u : 0u) | (((r >> 3) & 1) << 2) | (((x >> 3) & 1) << 1) | ((b >> 3) & 1);
        if (v != 0x40u || force)
            B(v);
    }

    /// prefix (0 = none), REX, opcode bytes, ModRM register form: reg field `reg`, r/m `rm`
    void Op(uint32_t pfx, bool w, std::initializer_list<uint8_t> op, uint32_t reg, uint32_t rm, bool byteRegs = false)
    {
        if (pfx)
            B(pfx);
        Rex(w, reg, 0, rm, byteRegs);
        for (uint8_t o : op)
            B(o);
        B(0xC0u | ((reg & 7) << 3) | (rm & 7));
    }
    /// the same with a memory operand
    void Op(uint32_t pfx, bool w, std::initializer_list<uint8_t> op, uint32_t reg, const Mem& m, bool byteRegs = false)
    {
        if (pfx)
            B(pfx);
        Rex(w, reg, m.index == kNoIndex ? 0 : m.index, m.base, byteRegs);
        for (uint8_t o : op)
            B(o);
        const bool sib = m.index != kNoIndex || (m.base & 7) == 4;
        const uint32_t mod = (m.disp == 0 && (m.base & 7) != 5) ? 0u : (m.disp >= -128 && m.disp <= 127) ? 1u : 2u;
        B((mod << 6) | ((reg & 7) << 3) | (sib ? 4u : (m.base & 7)));
        if (sib)
            B((m.scale << 6) | (((m.index == kNoIndex ? 4u : m.index) & 7) << 3) | (m.base & 7));
        if (mod == 1)
            B(static_cast<uint32_t>(m.disp) & 0xFF);
        else if (mod == 2)
            D32(static_cast<uint32_t>(m.disp));
    }

    // integer, 32-bit unless noted
    void Mov(uint32_t d, uint32_t s) { if (d != s) Op(0, false, {0x8B}, d, s); }
    void Mov64(uint32_t d, uint32_t s) { if (d != s) Op(0, true, {0x8B}, d, s); }
    void MovImm(uint32_t d, uint32_t v)
    {
        if (v == 0)
        {
            Op(0, false, {0x31}, d, d); // xor (flags!)
            return;
        }
        Rex(false, 0, 0, d, false);
        B(0xB8u + (d & 7));
        D32(v);
    }
    /// mov without touching the flags
    void MovImmKeep(uint32_t d, uint32_t v)
    {
        Rex(false, 0, 0, d, false);
        B(0xB8u + (d & 7));
        D32(v);
    }
    void MovImm64(uint32_t d, uint64_t v)
    {
        Rex(true, 0, 0, d, false);
        B(0xB8u + (d & 7));
        D64(v);
    }
    /// mov r64, sign-extended imm32
    void MovImm64S(uint32_t d, int32_t v) { Op(0, true, {0xC7}, 0, d); D32(static_cast<uint32_t>(v)); }
    void Load(uint32_t d, const Mem& m) { Op(0, false, {0x8B}, d, m); }
    void Store(const Mem& m, uint32_t s) { Op(0, false, {0x89}, s, m); }
    void Load64(uint32_t d, const Mem& m) { Op(0, true, {0x8B}, d, m); }
    void Store64(const Mem& m, uint32_t s) { Op(0, true, {0x89}, s, m); }
    void StoreImm(const Mem& m, uint32_t v) { Op(0, false, {0xC7}, 0, m); D32(v); }
    void Load8(uint32_t d, const Mem& m) { Op(0, false, {0x0F, 0xB6}, d, m); }
    void Load16(uint32_t d, const Mem& m) { Op(0, false, {0x0F, 0xB7}, d, m); }
    void Load16S(uint32_t d, const Mem& m) { Op(0, false, {0x0F, 0xBF}, d, m); }
    void Store8(const Mem& m, uint32_t s) { Op(0, false, {0x88}, s, m, true); }
    void Store16(const Mem& m, uint32_t s) { Op(0x66, false, {0x89}, s, m); }
    void Lea(uint32_t d, const Mem& m) { Op(0, false, {0x8D}, d, m); }
    void Lea64(uint32_t d, const Mem& m) { Op(0, true, {0x8D}, d, m); }

    /// op r/m32, r32: 0x01 add, 0x29 sub, 0x21 and, 0x09 or, 0x31 xor, 0x39 cmp, 0x85 test
    void Alu(uint8_t op, uint32_t d, uint32_t s, bool w = false) { Op(0, w, {op}, s, d); }
    /// op r/m32, imm: /0 add, /5 sub, /4 and, /1 or, /6 xor, /7 cmp
    void AluImm(uint32_t ext, uint32_t d, uint32_t imm, bool w = false)
    {
        const int32_t v = static_cast<int32_t>(imm);
        if (v >= -128 && v <= 127)
        {
            Op(0, w, {0x83}, ext, d);
            B(imm & 0xFF);
        }
        else
        {
            Op(0, w, {0x81}, ext, d);
            D32(imm);
        }
    }
    void AluMemImm(uint32_t ext, const Mem& m, uint32_t imm) { Op(0, false, {0x81}, ext, m); D32(imm); }
    void Cmp64MemZero(const Mem& m) { Op(0, true, {0x83}, 7, m); B(0); }
    void Test(uint32_t a, uint32_t b, bool w = false) { Op(0, w, {0x85}, b, a); }
    void TestImm(uint32_t d, uint32_t imm) { Op(0, false, {0xF7}, 0, d); D32(imm); }
    void TestMemImm(const Mem& m, uint32_t imm) { Op(0, false, {0xF7}, 0, m); D32(imm); }
    void Neg(uint32_t d) { Op(0, false, {0xF7}, 3, d); }
    void Not(uint32_t d) { Op(0, false, {0xF7}, 2, d); }
    void Imul(uint32_t d, uint32_t s, bool w = false) { Op(0, w, {0x0F, 0xAF}, d, s); }
    /// /4 shl, /5 shr, /7 sar
    void Shift(uint32_t ext, uint32_t d, uint32_t n, bool w = false) { Op(0, w, {0xC1}, ext, d); B(n); }
    void ShiftCl(uint32_t ext, uint32_t d, bool w = false) { Op(0, w, {0xD3}, ext, d); }
    void Movzx8(uint32_t d, uint32_t s) { Op(0, false, {0x0F, 0xB6}, d, s, true); }
    void Movzx16(uint32_t d, uint32_t s) { Op(0, false, {0x0F, 0xB7}, d, s); }
    void Movsx16(uint32_t d, uint32_t s) { Op(0, false, {0x0F, 0xBF}, d, s); }
    void Movsxd(uint32_t d, uint32_t s) { Op(0, true, {0x63}, d, s); }
    void Cmov(Cc c, uint32_t d, uint32_t s, bool w = false) { Op(0, w, {0x0F, static_cast<uint8_t>(0x40 + c)}, d, s); }
    void Setcc(Cc c, uint32_t d) { Op(0, false, {0x0F, static_cast<uint8_t>(0x90 + c)}, 0, d, true); }
    void And8(uint32_t d, uint32_t s) { Op(0, false, {0x20}, s, d, true); }
    void Or8(uint32_t d, uint32_t s) { Op(0, false, {0x08}, s, d, true); }
    void Bt(uint32_t rm, uint32_t reg) { Op(0, false, {0x0F, 0xA3}, reg, rm); }
    void Push(uint32_t r) { Rex(false, 0, 0, r, false); B(0x50u + (r & 7)); }
    void Pop(uint32_t r) { Rex(false, 0, 0, r, false); B(0x58u + (r & 7)); }
    void Ret() { B(0xC3); }

    // SSE scalar
    void MovssLoad(uint32_t x, const Mem& m) { Op(0xF3, false, {0x0F, 0x10}, x, m); }
    void Addss(uint32_t d, uint32_t s) { Op(0xF3, false, {0x0F, 0x58}, d, s); }
    void Mulss(uint32_t d, uint32_t s) { Op(0xF3, false, {0x0F, 0x59}, d, s); }
    void Subss(uint32_t d, uint32_t s) { Op(0xF3, false, {0x0F, 0x5C}, d, s); }
    void Ucomiss(uint32_t a, uint32_t b) { Op(0, false, {0x0F, 0x2E}, a, b); }
    void Ucomisd(uint32_t a, uint32_t b) { Op(0x66, false, {0x0F, 0x2E}, a, b); }
    void MovdXR(uint32_t x, uint32_t r) { Op(0x66, false, {0x0F, 0x6E}, x, r); }
    void MovdRX(uint32_t r, uint32_t x) { Op(0x66, false, {0x0F, 0x7E}, x, r); }
    void MovqXR(uint32_t x, uint32_t r) { Op(0x66, true, {0x0F, 0x6E}, x, r); }
    void Cvtss2sd(uint32_t d, uint32_t s) { Op(0xF3, false, {0x0F, 0x5A}, d, s); }
    void Mulsd(uint32_t d, uint32_t s) { Op(0xF2, false, {0x0F, 0x59}, d, s); }
    void Maxsd(uint32_t d, uint32_t s) { Op(0xF2, false, {0x0F, 0x5F}, d, s); }
    void Minsd(uint32_t d, uint32_t s) { Op(0xF2, false, {0x0F, 0x5D}, d, s); }
    void Cvtsi2ss(uint32_t x, uint32_t r, bool w) { Op(0xF3, w, {0x0F, 0x2A}, x, r); }
    void Cvtsi2sd64(uint32_t x, uint32_t r) { Op(0xF2, true, {0x0F, 0x2A}, x, r); }
    void Cvttsd2si64(uint32_t r, uint32_t x) { Op(0xF2, true, {0x0F, 0x2C}, r, x); }
    void Cvtsd2si64(uint32_t r, uint32_t x) { Op(0xF2, true, {0x0F, 0x2D}, r, x); }
    /// VEX.LIG.66.0F38.W0 op /r for xmm0-xmm7: 0xB9 vfmadd231ss d = s1 * s2 + d, 0xBD vfnmadd231ss d = -(s1 * s2) + d
    void Vfma231ss(uint8_t op, uint32_t d, uint32_t s1, uint32_t s2)
    {
        B(0xC4);
        B(0xE2);
        B(((~s1 & 15u) << 3) | 1u);
        B(op);
        B(0xC0u | ((d & 7) << 3) | (s2 & 7));
    }

    // control: rel32 fixups bound by Bind
    size_t Jcc(Cc c)
    {
        B(0x0F);
        B(0x80u + c);
        D32(0);
        return Pos() - 4;
    }
    size_t Jmp()
    {
        B(0xE9);
        D32(0);
        return Pos() - 4;
    }
    void Bind(size_t at, size_t target)
    {
        const uint32_t rel = static_cast<uint32_t>(static_cast<int64_t>(target) - static_cast<int64_t>(at + 4));
        for (int i = 0; i < 4; ++i)
            code[at + static_cast<size_t>(i)] = static_cast<uint8_t>(rel >> (8 * i));
    }
};

constexpr uint32_t XST = R14, XREM = R15;
constexpr uint32_t T0 = RAX, T1 = RCX, T2 = RDX, T3 = R11;
constexpr uint32_t kPool[9] = {RBX, RBP, RSI, RDI, R8, R9, R10, R12, R13};
constexpr uint32_t kSpilled = 0x100; ///< _host[g] >= kSpilled: frame slot (_host[g] - kSpilled)

// frame (rsp-relative)
constexpr int32_t kFrAr = 0, kFrL1 = 8, kFrWb = 16, kFrB0 = 20, kFrSlots = 24;
constexpr int32_t kFrameSize = kFrSlots + 16 * 4 + 8; // 96; 88 after an even number of pushes (16-byte alignment)

bool HostHasFma()
{
    static const bool fma = [] {
#if defined(_MSC_VER) && !defined(__clang__)
        // CPUID.1:ECX FMA (12), OSXSAVE (27), AVX (28); XCR0 has the XMM and YMM state enabled
        int r[4] = {};
        __cpuid(r, 1);
        const uint32_t ecx = static_cast<uint32_t>(r[2]);
        constexpr uint32_t kNeed = (1u << 12) | (1u << 27) | (1u << 28);
        return (ecx & kNeed) == kNeed && (_xgetbv(0) & 6) == 6;
#else
        __builtin_cpu_init();
        return __builtin_cpu_supports("fma") != 0;
#endif
    }();
    return fma;
}

bool BackendOk(const Insn& in)
{
    switch (in.op)
    {
        case Op::MaddS: case Op::MaddnS: case Op::MsubS: return HostHasFma();
        default: return true;
    }
}

Mem St(uint32_t off)
{
    return Mem{XST, static_cast<int32_t>(off)};
}

Mem Fr(uint32_t n)
{
    return St(FR(n));
}

Mem Frame(int32_t off)
{
    return Mem{RSP, off};
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

    std::vector<uint8_t> Compile()
    {
        uint32_t uses[16] = {};
        for (const RegionBlock& rb : _blocks)
            for (uint32_t k = 0; k < rb.native; ++k)
            {
                const Use u = UseOf(rb.insns[k]);
                _used = static_cast<uint16_t>(_used | u.read | u.write);
                _written = static_cast<uint16_t>(_written | u.write);
                for (uint32_t g = 0; g < 16; ++g)
                    if ((u.read | u.write) & (1u << g))
                        ++uses[g];
            }
        // the most used guest registers get host registers
        uint32_t order[16];
        uint32_t n = 0;
        for (uint32_t g = 0; g < 16; ++g)
            if (_used & (1u << g))
                order[n++] = g;
        std::stable_sort(order, order + n, [&](uint32_t x, uint32_t y) { return uses[x] > uses[y]; });
        for (uint32_t i = 0; i < n; ++i)
            _host[order[i]] = i < 9 ? kPool[i] : kSpilled + order[i];
        for (uint32_t i = 0; i < n && i < 9; ++i)
            _hostUsed |= 1u << kPool[i];
        Prologue();

        _labels.assign(_blocks.size(), 0);
        for (size_t i = 0; i < _blocks.size(); ++i)
        {
            const RegionBlock& rb = _blocks[i];
            _labels[i] = _a.Pos();
            _cur = &rb;
            _curIndex = i;
            // the budget check: a block runs whole or not at all (the interpreter cuts it)
            _a.AluImm(7, XREM, rb.count);
            ExitTo(_a.Jcc(CB), rb.start, 0);
            _a.AluImm(5, XREM, rb.count);
            for (uint32_t k = 0; k < rb.native; ++k)
            {
                _k = k;
                if (!EmitInsn(rb.insns[k]))
                    return std::vector<uint8_t>{};
            }
            if (rb.native < rb.count)
                ExitTo(_a.Jmp(), rb.insns[rb.native].pc, rb.count - rb.native);
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
            EmitExit(s.pc, s.adjust, false);
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

    static constexpr uint32_t kSaved[6] = {RBX, RBP, R12, R13, R14, R15};

    bool Spilled(uint8_t g) const { return _host[g & 15] >= kSpilled; }
    Mem Slot(uint8_t g) const { return Frame(kFrSlots + 4 * static_cast<int32_t>(g & 15)); }

    /// The host register holding guest g: its own, or `tmp` loaded from its slot
    uint32_t Src(uint8_t g, uint32_t tmp)
    {
        if (!Spilled(g))
            return _host[g & 15];
        _a.Load(tmp, Slot(g));
        return tmp;
    }
    /// The host register to compute guest g into: its own, or `tmp` (then Put)
    uint32_t Dst(uint8_t g, uint32_t tmp) const { return Spilled(g) ? tmp : _host[g & 15]; }
    void Put(uint8_t g, uint32_t r)
    {
        if (Spilled(g))
            _a.Store(Slot(g), r);
        else
            _a.Mov(_host[g & 15], r);
    }

    /// guest index (wb4 + g) & 63 into `r`
    void GuestIndex(uint32_t r, uint32_t g)
    {
        _a.Load(r, Frame(kFrWb));
        _a.AluImm(0, r, g);
        _a.AluImm(4, r, 63);
    }

    void Prologue()
    {
#if defined(_WIN32)
        // Windows x64: ar rcx, wb4 rdx, st r8, l1 r9, budget on the stack above the 32-byte home area; rsi and rdi
        // are callee-saved there. Save them and move the arguments to the System V registers the body expects.
        _a.Push(RDI);
        _a.Push(RSI);
        _a.Mov64(RDI, RCX);
        _a.Mov64(RSI, RDX);
        _a.Mov64(RDX, R8);
        _a.Mov64(RCX, R9);
        _a.Load(R8, Mem{RSP, 16 + 8 + 32}); // two pushes, the return address, the home area
#endif
        _pushes = 0;
        for (uint32_t r : kSaved)
            if ((_hostUsed & (1u << r)) || r == XST || r == XREM)
            {
                _a.Push(r);
                ++_pushes;
            }
        // pushes + return address: keep rsp 16-aligned (no calls are made, but stay ABI-clean)
        _frame = (_pushes & 1) ? kFrameSize : kFrameSize - 8;
        _a.AluImm(5, RSP, static_cast<uint32_t>(_frame), true);
        _a.Store64(Frame(kFrAr), RDI);
        _a.Store64(Frame(kFrL1), RCX);
        _a.Store(Frame(kFrWb), RSI);
        _a.Store(Frame(kFrB0), R8);
        _a.Mov64(XST, RDX);
        _a.Mov(XREM, R8);
        _a.Load64(T2, Frame(kFrAr));
        for (uint32_t g = 0; g < 16; ++g)
            if (_used & (1u << g))
            {
                GuestIndex(T1, g);
                const Mem m{T2, 0, T1, 2};
                if (Spilled(static_cast<uint8_t>(g)))
                {
                    _a.Load(T0, m);
                    _a.Store(Slot(static_cast<uint8_t>(g)), T0);
                }
                else
                    _a.Load(_host[g], m);
            }
    }

    void Epilogue()
    {
        _a.AluImm(0, RSP, static_cast<uint32_t>(_frame), true);
        for (int i = 5; i >= 0; --i)
        {
            const uint32_t r = kSaved[i];
            if ((_hostUsed & (1u << r)) || r == XST || r == XREM)
                _a.Pop(r);
        }
#if defined(_WIN32)
        _a.Pop(RSI);
        _a.Pop(RDI);
#endif
        _a.Ret();
    }

    /// Writes back the registers, returns (pc << 32) | executed; executed = consumed budget - adjust. The pc is
    /// `pc`, or T3 when pcInT3.
    void EmitExit(uint32_t pc, uint32_t adjust, bool pcInT3)
    {
        if (_written)
            _a.Load64(T2, Frame(kFrAr));
        for (uint32_t g = 0; g < 16; ++g)
            if (_written & (1u << g))
            {
                GuestIndex(T1, g);
                const uint32_t v = Src(static_cast<uint8_t>(g), T0);
                _a.Store(Mem{T2, 0, T1, 2}, v);
            }
        _a.Load(T1, Frame(kFrB0));
        _a.Alu(0x29, T1, XREM);
        if (adjust)
            _a.AluImm(5, T1, adjust);
        if (pcInT3)
            _a.Mov(T0, T3);
        else
            _a.MovImm(T0, pc);
        _a.Shift(4, T0, 32, true);
        _a.Alu(0x09, T0, T1, true);
        Epilogue();
    }

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

    void Side(size_t fixup) { ExitTo(fixup, _cur->insns[_k].pc, _cur->count - _k); }

    void BranchTo(size_t fixup, uint32_t pc)
    {
        auto it = _index.find(pc);
        if (it != _index.end())
            _toLabel.push_back({fixup, it->second});
        else
            ExitTo(fixup, pc, 0);
    }

    void SequentialEnd(uint32_t pc)
    {
        if (_loop.has && pc == _loop.end)
        {
            // if (lend == pc && lcount != 0) { lcount--; pc = lbeg }
            _a.AluMemImm(7, St(offsetof(CpuState, lend)), pc);
            const size_t notEnd = _a.Jcc(CNE);
            _a.Load(T0, St(offsetof(CpuState, lcount)));
            _a.Test(T0, T0);
            const size_t done = _a.Jcc(CE);
            _a.AluImm(5, T0, 1);
            _a.Store(St(offsetof(CpuState, lcount)), T0);
            _a.Load(T3, St(offsetof(CpuState, lbeg)));
            _a.AluImm(7, T3, _loop.beg);
            BranchTo(_a.Jcc(CE), _loop.beg);
            EmitExit(0, 0, true); // LBEG changed under the region (a host write): continue there in the interpreter
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
            return;
        BranchTo(_a.Jmp(), pc);
    }

    /// Fast-path address translation of T0 (the guest address); T1 = host bias, or a side exit. Uses T2.
    void Translate(uint32_t size, uint32_t flag, bool store)
    {
        _a.Mov(T1, T0);
        _a.AluImm(4, T1, 0xFFF);
        _a.AluImm(7, T1, 0x1000 - size);
        Side(_a.Jcc(CA));
        _a.Mov(T1, T0);
        _a.Shift(5, T1, 22);
        _a.Load64(T2, Frame(kFrL1));
        _a.Load64(T1, Mem{T2, 0, T1, 3});
        _a.Test(T1, T1, true);
        Side(_a.Jcc(CE));
        _a.Mov(T2, T0);
        _a.Shift(5, T2, 12);
        _a.AluImm(4, T2, 0x3FF);
        _a.Lea64(T2, Mem{T2, 0, T2, 1});                // * 3
        _a.Lea64(T1, Mem{T1, 0, T2, 3});                // + 24 * index
        _a.TestMemImm(Mem{T1, static_cast<int32_t>(offsetof(PageEntryLayout, flags))}, flag);
        Side(_a.Jcc(CE));
        if (store)
        {
            _a.Cmp64MemZero(Mem{T1, static_cast<int32_t>(offsetof(PageEntryLayout, code))});
            Side(_a.Jcc(CNE));
        }
        _a.Load64(T1, Mem{T1, static_cast<int32_t>(offsetof(PageEntryLayout, bias))});
    }

    /// xmm0 -> fr[a], any NaN as the default quiet NaN (Result in xtensa-cpu.cpp)
    void StoreFloatResult(uint32_t a)
    {
        _a.MovdRX(T0, 0);
        _a.MovImm(T1, 0x7FC00000u);
        _a.Ucomiss(0, 0);
        _a.Cmov(CP, T0, T1);
        _a.Store(Fr(a), T0);
    }

    /// xmm0 = fr[b], xmm1 = double(fr[b]) * 2^scale
    void ScaledDouble(uint32_t b, uint32_t scale)
    {
        _a.MovssLoad(0, Fr(b));
        _a.Cvtss2sd(1, 0);
        if (scale & 15)
        {
            _a.MovImm64(T0, static_cast<uint64_t>(1023 + (scale & 15)) << 52);
            _a.MovqXR(2, T0);
            _a.Mulsd(1, 2);
        }
    }

    void ClampDouble(double lo, double hi)
    {
        uint64_t bits;
        std::memcpy(&bits, &lo, 8);
        _a.MovImm64(T0, bits);
        _a.MovqXR(2, T0);
        _a.Maxsd(1, 2);
        std::memcpy(&bits, &hi, 8);
        _a.MovImm64(T0, bits);
        _a.MovqXR(2, T0);
        _a.Minsd(1, 2);
    }

    /// T0 = xmm1 (not NaN) rounded as `op` and saturated to int32 (FloatToInt in xtensa-cpu.cpp). Uses T1, xmm2.
    void ToInt32(Op op)
    {
        ClampDouble(-2147483650.0, 2147483649.0); // integers: the rounding of what stays is unchanged
        if (op == Op::RoundS)
            _a.Cvtsd2si64(T0, 1); // MXCSR: round to nearest, ties to even
        else
            _a.Cvttsd2si64(T0, 1);
        if (op == Op::FloorS || op == Op::CeilS)
        {
            _a.Cvtsi2sd64(2, T0);
            if (op == Op::FloorS)
            {
                _a.Lea64(T1, Mem{T0, -1});
                _a.Ucomisd(2, 1); // trunc > d: one down
            }
            else
            {
                _a.Lea64(T1, Mem{T0, 1});
                _a.Ucomisd(1, 2); // d > trunc: one up
            }
            _a.Cmov(CA, T0, T1, true);
        }
        _a.MovImm64S(T1, 0x7FFFFFFF);
        _a.Alu(0x39, T0, T1, true);
        _a.Cmov(CG, T0, T1, true);
        _a.MovImm64S(T1, static_cast<int32_t>(0x80000000u));
        _a.Alu(0x39, T0, T1, true);
        _a.Cmov(CL, T0, T1, true);
    }

    void EmitBranch(Cc c, uint32_t target) { BranchTo(_a.Jcc(c), target); }

    /// a = b op c for a commutative op
    void Commutative(uint8_t op, const Insn& in)
    {
        const uint32_t b = Src(in.b, T0);
        const uint32_t c = Src(in.c, T1);
        const uint32_t d = Dst(in.a, T2);
        if (d == b)
            _a.Alu(op, d, c);
        else if (d == c)
            _a.Alu(op, d, b);
        else
        {
            _a.Mov(d, b);
            _a.Alu(op, d, c);
        }
        Put(in.a, d);
    }

    bool EmitInsn(const Insn& in)
    {
        const uint32_t imm = static_cast<uint32_t>(in.imm);
        switch (in.op)
        {
            case Op::Add: case Op::AddN: case Op::Addx2: case Op::Addx4: case Op::Addx8:
            {
                const uint32_t sh = in.op == Op::Addx2 ? 1 : in.op == Op::Addx4 ? 2 : in.op == Op::Addx8 ? 3 : 0;
                const uint32_t b = Src(in.b, T0);
                const uint32_t c = Src(in.c, T1);
                const uint32_t d = Dst(in.a, T2);
                _a.Lea(d, Mem{c, 0, b, sh});
                Put(in.a, d);
                return true;
            }
            case Op::Sub: case Op::Subx2: case Op::Subx4: case Op::Subx8:
            {
                const uint32_t sh = in.op == Op::Subx2 ? 1 : in.op == Op::Subx4 ? 2 : in.op == Op::Subx8 ? 3 : 0;
                const uint32_t b = Src(in.b, T0);
                _a.Mov(T0, b);
                if (sh)
                    _a.Shift(4, T0, sh);
                const uint32_t c = Src(in.c, T1);
                _a.Alu(0x29, T0, c);
                Put(in.a, T0);
                return true;
            }
            case Op::And: Commutative(0x21, in); return true;
            case Op::Or: Commutative(0x09, in); return true;
            case Op::Xor: Commutative(0x31, in); return true;
            case Op::Neg: _a.Mov(T0, Src(in.c, T0)); _a.Neg(T0); Put(in.a, T0); return true;
            case Op::Abs:
            {
                const uint32_t c = Src(in.c, T1);
                _a.Mov(T0, c);
                _a.Neg(T0);
                _a.Cmov(CS, T0, c); // -c negative: c was positive (or INT_MIN, unchanged)
                Put(in.a, T0);
                return true;
            }
            case Op::Addi: case Op::AddiN: case Op::Addmi:
            {
                const uint32_t b = Src(in.b, T0);
                const uint32_t d = Dst(in.a, T2);
                _a.Lea(d, Mem{b, static_cast<int32_t>(imm)});
                Put(in.a, d);
                return true;
            }
            case Op::Movi: case Op::MoviN:
            {
                const uint32_t d = Dst(in.a, T0);
                _a.MovImmKeep(d, imm);
                Put(in.a, d);
                return true;
            }
            case Op::MovN: Put(in.a, Src(in.b, T0)); return true;
            case Op::Salt: case Op::Saltu:
                _a.Alu(0x39, Src(in.b, T0), Src(in.c, T1));
                _a.Setcc(in.op == Op::Salt ? CL : CB, T2);
                _a.Movzx8(T2, T2);
                Put(in.a, T2);
                return true;
            case Op::Min: case Op::Max: case Op::Minu: case Op::Maxu:
            {
                const uint32_t b = Src(in.b, T0);
                const uint32_t c = Src(in.c, T1);
                const bool isMin = in.op == Op::Min || in.op == Op::Minu;
                const Cc lt = (in.op == Op::Min || in.op == Op::Max) ? CL : CB;
                _a.Alu(0x39, b, c); // flags of b - c
                _a.Mov(T2, isMin ? c : b);
                _a.Cmov(lt, T2, isMin ? b : c);
                Put(in.a, T2);
                return true;
            }
            case Op::Moveqz: case Op::Movnez: case Op::Movltz: case Op::Movgez:
            {
                const uint32_t c = Src(in.c, T1);
                _a.Test(c, c);
                const uint32_t b = Src(in.b, T0);
                _a.Mov(T2, Src(in.a, T2));
                const Cc cc = in.op == Op::Moveqz ? CE : in.op == Op::Movnez ? CNE : in.op == Op::Movltz ? CS : CNS;
                _a.Cmov(cc, T2, b);
                Put(in.a, T2);
                return true;
            }
            case Op::Movf: case Op::Movt:
            {
                const uint32_t b = Src(in.b, T0);
                _a.Mov(T2, Src(in.a, T2));
                _a.TestMemImm(St(offsetof(CpuState, br)), 1u << (in.c & 15));
                _a.Cmov(in.op == Op::Movf ? CE : CNE, T2, b);
                Put(in.a, T2);
                return true;
            }
            case Op::Sext:
            {
                const uint32_t width = imm + 1;
                _a.Mov(T0, Src(in.b, T0));
                if (width < 32)
                {
                    _a.Shift(4, T0, 32 - width);
                    _a.Shift(7, T0, 32 - width);
                }
                Put(in.a, T0);
                return true;
            }
            case Op::Mul16u: case Op::Mul16s:
            {
                const uint32_t b = Src(in.b, T0);
                const uint32_t c = Src(in.c, T1);
                if (in.op == Op::Mul16u)
                {
                    _a.Movzx16(T0, b);
                    _a.Movzx16(T1, c);
                }
                else
                {
                    _a.Movsx16(T0, b);
                    _a.Movsx16(T1, c);
                }
                _a.Imul(T0, T1);
                Put(in.a, T0);
                return true;
            }
            case Op::Mull:
                _a.Mov(T0, Src(in.b, T0));
                _a.Imul(T0, Src(in.c, T1));
                Put(in.a, T0);
                return true;
            // a zero divisor exits before the instruction: the interpreter stops there (DivideByZero). IDIV faults on
            // INT_MIN / -1: a divisor of -1 takes 0 - n (quotient) and 0 (remainder), as the interpreter
            case Op::Quou: case Op::Remu: case Op::Quos: case Op::Rems:
            {
                _a.Mov(T1, Src(in.c, T1));
                _a.Test(T1, T1);
                Side(_a.Jcc(CE));
                _a.Mov(T0, Src(in.b, T0));
                const bool rem = in.op == Op::Remu || in.op == Op::Rems;
                size_t minusOne = 0, done = 0;
                if (in.op == Op::Quou || in.op == Op::Remu)
                {
                    _a.Alu(0x31, T2, T2); // xor edx, edx
                    _a.Op(0, false, {0xF7}, 6, T1); // div ecx
                }
                else
                {
                    _a.AluImm(7, T1, 0xFFFFFFFFu);
                    minusOne = _a.Jcc(CE);
                    _a.B(0x99); // cdq
                    _a.Op(0, false, {0xF7}, 7, T1); // idiv ecx
                    done = _a.Jmp();
                    _a.Bind(minusOne, _a.Pos());
                    if (rem)
                        _a.Alu(0x31, T2, T2);
                    else
                        _a.Neg(T0);
                    _a.Bind(done, _a.Pos());
                }
                Put(in.a, rem ? T2 : T0);
                return true;
            }
            // BSR leaves ZF set and its destination undefined for 0: -1 then, so 31 - (-1) = 32 and 30 - (-1) = 31
            case Op::Nsau: case Op::Nsa:
            {
                _a.Mov(T0, Src(in.b, T0));
                if (in.op == Op::Nsa)
                {
                    _a.Mov(T2, T0);
                    _a.Shift(7, T2, 31); // sar edx, 31
                    _a.Alu(0x31, T0, T2); // x = v < 0 ? ~v : v
                }
                _a.Op(0, false, {0x0F, 0xBD}, T0, T0); // bsr eax, eax
                _a.MovImm(T2, 0xFFFFFFFFu);
                _a.Cmov(CE, T0, T2);
                _a.Neg(T0);
                _a.AluImm(0, T0, in.op == Op::Nsau ? 31u : 30u);
                Put(in.a, T0);
                return true;
            }
            case Op::Muluh: case Op::Mulsh:
            {
                const uint32_t b = Src(in.b, T0);
                const uint32_t c = Src(in.c, T1);
                if (in.op == Op::Muluh)
                {
                    _a.Mov(T0, b); // zero-extends
                    _a.Mov(T1, c);
                    // movs of a register onto itself are skipped: make sure the upper halves are zero
                    if (b == T0)
                        _a.Op(0, false, {0x89}, T0, T0);
                    if (c == T1)
                        _a.Op(0, false, {0x89}, T1, T1);
                }
                else
                {
                    _a.Movsxd(T0, b);
                    _a.Movsxd(T1, c);
                }
                _a.Imul(T0, T1, true);
                _a.Shift(5, T0, 32, true);
                Put(in.a, T0);
                return true;
            }
            case Op::Slli:
            {
                const uint32_t d = Dst(in.a, T0);
                if (imm >= 32)
                    _a.MovImmKeep(d, 0);
                else
                {
                    _a.Mov(d, Src(in.b, T0));
                    _a.Shift(4, d, imm);
                }
                Put(in.a, d);
                return true;
            }
            case Op::Srai: case Op::Srli:
            {
                _a.Mov(T0, Src(in.b, T0));
                if (imm & 31)
                    _a.Shift(in.op == Op::Srai ? 7 : 5, T0, imm & 31);
                Put(in.a, T0);
                return true;
            }
            case Op::Extui:
            {
                const uint32_t lsb = in.c & 31;
                uint32_t width = imm;
                if (lsb + width > 32)
                    width = 32 - lsb;
                _a.Mov(T0, Src(in.b, T0));
                if (lsb)
                    _a.Shift(5, T0, lsb);
                if (width < 32)
                    _a.AluImm(4, T0, (1u << width) - 1);
                Put(in.a, T0);
                return true;
            }
            case Op::Src:
                // ((b << 32) | c) >> sar
                _a.Mov(T2, Src(in.b, T2));
                _a.Shift(4, T2, 32, true);
                _a.Mov(T0, Src(in.c, T0));
                if (!Spilled(in.c) && H(in.c) == T0)
                    _a.Op(0, false, {0x89}, T0, T0);
                _a.Alu(0x09, T0, T2, true);
                _a.Load(T1, St(offsetof(CpuState, sar)));
                _a.ShiftCl(5, T0, true);
                Put(in.a, T0);
                return true;
            case Op::Srl:
                _a.Op(0, false, {0x89}, Src(in.c, T0), T0); // mov eax, c (zero-extends)
                _a.Load(T1, St(offsetof(CpuState, sar)));
                _a.ShiftCl(5, T0, true);
                Put(in.a, T0);
                return true;
            case Op::Sll:
                // (b << 32) >> sar, low half
                _a.Op(0, false, {0x89}, Src(in.b, T0), T0);
                _a.Shift(4, T0, 32, true);
                _a.Load(T1, St(offsetof(CpuState, sar)));
                _a.ShiftCl(5, T0, true);
                Put(in.a, T0);
                return true;
            case Op::Sra:
                _a.Movsxd(T0, Src(in.c, T0));
                _a.Load(T1, St(offsetof(CpuState, sar)));
                _a.ShiftCl(7, T0, true);
                Put(in.a, T0);
                return true;
            case Op::Ssr: case Op::Ssl:
                _a.Mov(T0, Src(in.b, T0));
                _a.AluImm(4, T0, 31);
                if (in.op == Op::Ssl)
                {
                    _a.Neg(T0);
                    _a.AluImm(0, T0, 32);
                }
                _a.Store(St(offsetof(CpuState, sar)), T0);
                return true;
            case Op::Ssa8l: case Op::Ssa8b:
                _a.Mov(T0, Src(in.b, T0));
                _a.AluImm(4, T0, 3);
                _a.Shift(4, T0, 3);
                if (in.op == Op::Ssa8b)
                {
                    _a.Neg(T0);
                    _a.AluImm(0, T0, 32);
                }
                _a.Store(St(offsetof(CpuState, sar)), T0);
                return true;
            case Op::Ssai: _a.StoreImm(St(offsetof(CpuState, sar)), imm & 0xFFFF); return true;
            case Op::Isync: case Op::Rsync: case Op::Esync: case Op::Dsync: case Op::Excw: case Op::Memw:
            case Op::Extw: case Op::Nop: case Op::NopN:
                return true;

            case Op::L8ui: case Op::L16ui: case Op::L16si: case Op::L32i: case Op::L32iN:
            {
                const uint32_t size = in.op == Op::L8ui ? 1 : (in.op == Op::L16ui || in.op == Op::L16si) ? 2 : 4;
                _a.Lea(T0, Mem{Src(in.b, T0), static_cast<int32_t>(imm)});
                Translate(size, RegionRead, false);
                const Mem m{T1, 0, T0, 0};
                const uint32_t d = Dst(in.a, T2);
                if (size == 1)
                    _a.Load8(d, m);
                else if (size == 2)
                {
                    if (in.op == Op::L16si)
                        _a.Load16S(d, m);
                    else
                        _a.Load16(d, m);
                }
                else
                    _a.Load(d, m);
                Put(in.a, d);
                return true;
            }
            case Op::L32r:
            {
                const uint8_t* p = (imm & 0xFFF) <= 0x1000 - 4 ? _xlate(_cpu, imm) : nullptr;
                if (!p)
                {
                    Side(_a.Jmp());
                    return true;
                }
                _a.MovImm64(T0, reinterpret_cast<uintptr_t>(p));
                const uint32_t d = Dst(in.a, T1);
                _a.Load(d, Mem{T0, 0});
                Put(in.a, d);
                return true;
            }
            case Op::S8i: case Op::S16i: case Op::S32i: case Op::S32iN: case Op::S32nb:
            {
                const uint32_t size = in.op == Op::S8i ? 1 : in.op == Op::S16i ? 2 : 4;
                _a.Lea(T0, Mem{Src(in.b, T0), static_cast<int32_t>(imm)});
                Translate(size, RegionWrite, true);
                const uint32_t v = Src(in.a, T2);
                const Mem m{T1, 0, T0, 0};
                if (size == 1)
                    _a.Store8(m, v);
                else if (size == 2)
                    _a.Store16(m, v);
                else
                    _a.Store(m, v);
                return true;
            }

            // ---- floating point ----
            case Op::Lsi:
                _a.Lea(T0, Mem{Src(in.b, T0), static_cast<int32_t>(imm)});
                Translate(4, RegionRead, false);
                _a.Load(T2, Mem{T1, 0, T0, 0});
                _a.Store(Fr(in.a), T2);
                return true;
            case Op::Ssi:
                _a.Lea(T0, Mem{Src(in.b, T0), static_cast<int32_t>(imm)});
                Translate(4, RegionWrite, true);
                _a.Load(T2, Fr(in.a));
                _a.Store(Mem{T1, 0, T0, 0}, T2);
                return true;
            case Op::Wfr: _a.Store(Fr(in.a), Src(in.b, T0)); return true;
            case Op::Rfr:
            {
                const uint32_t d = Dst(in.a, T0);
                _a.Load(d, Fr(in.b));
                Put(in.a, d);
                return true;
            }
            case Op::MovS: _a.Load(T0, Fr(in.b)); _a.Store(Fr(in.a), T0); return true;
            case Op::MovfS: case Op::MovtS:
                _a.Load(T0, Fr(in.a));
                _a.Load(T1, Fr(in.b));
                _a.TestMemImm(St(offsetof(CpuState, br)), 1u << (in.c & 15));
                _a.Cmov(in.op == Op::MovtS ? CNE : CE, T0, T1);
                _a.Store(Fr(in.a), T0);
                return true;
            case Op::AbsS:
                _a.Load(T0, Fr(in.b));
                _a.AluImm(4, T0, 0x7FFFFFFFu);
                _a.Store(Fr(in.a), T0);
                return true;
            case Op::NegS:
                _a.Load(T0, Fr(in.b));
                _a.AluImm(6, T0, 0x80000000u);
                _a.Store(Fr(in.a), T0);
                return true;
            case Op::AddS: case Op::SubS: case Op::MulS:
                _a.MovssLoad(0, Fr(in.b));
                _a.MovssLoad(1, Fr(in.c));
                if (in.op == Op::AddS)
                    _a.Addss(0, 1);
                else if (in.op == Op::SubS)
                    _a.Subss(0, 1);
                else
                    _a.Mulss(0, 1);
                StoreFloatResult(in.a);
                return true;
            case Op::MaddS: case Op::MaddnS: case Op::MsubS:
                if (!HostHasFma())
                    return false;
                _a.MovssLoad(0, Fr(in.a));
                _a.MovssLoad(1, Fr(in.b));
                _a.MovssLoad(2, Fr(in.c));
                _a.Vfma231ss(in.op == Op::MsubS ? 0xBD : 0xB9, 0, 1, 2); // one rounding, as std::fma
                StoreFloatResult(in.a);
                return true;
            case Op::FloatS: case Op::UfloatS:
                if (in.op == Op::FloatS)
                    _a.Cvtsi2ss(0, Src(in.b, T0), false);
                else
                {
                    _a.Op(0, false, {0x89}, Src(in.b, T0), T0); // zero-extended to 64 bits
                    _a.Cvtsi2ss(0, T0, true);
                }
                if (imm & 15)
                {
                    // * 2^-scale: exact, the conversion rounded once as in the interpreter
                    _a.MovImm(T0, static_cast<uint32_t>(127 - (imm & 15)) << 23);
                    _a.MovdXR(1, T0);
                    _a.Mulss(0, 1);
                }
                _a.MovdRX(T0, 0);
                _a.Store(Fr(in.a), T0);
                return true;
            case Op::RoundS: case Op::TruncS: case Op::FloorS: case Op::CeilS:
                ScaledDouble(in.b, imm);
                ToInt32(in.op);
                _a.MovImm(T1, 0x7FFFFFFFu);
                _a.Ucomiss(0, 0);
                _a.Cmov(CP, T0, T1); // NaN
                Put(in.a, T0);
                return true;
            case Op::UtruncS:
                ScaledDouble(in.b, imm);
                // positive: trunc, >= 2^32 -> 0xFFFFFFFF (into T2)
                _a.Op(0xF2, false, {0x0F, 0x10}, 3, 1); // movsd xmm3, xmm1
                _a.MovImm64(T0, 0x41F0000000000000ull); // 2^32
                _a.MovqXR(2, T0);
                _a.Minsd(3, 2);
                _a.Cvttsd2si64(T2, 3);
                _a.MovImm(T1, 0xFFFFFFFFu);
                _a.Alu(0x39, T2, T1, true);
                _a.Cmov(CA, T2, T1, true);
                // negative values convert as TRUNC.S (into T0)
                ToInt32(Op::TruncS);
                _a.MovdRX(T1, 0);
                _a.Test(T1, T1);
                _a.Cmov(CNS, T0, T2);
                _a.MovImm(T1, 0xFFFFFFFFu);
                _a.Ucomiss(0, 0);
                _a.Cmov(CP, T0, T1); // NaN
                Put(in.a, T0);
                return true;
            case Op::UnS: case Op::OeqS: case Op::UeqS: case Op::OltS: case Op::UltS: case Op::OleS: case Op::UleS:
            {
                _a.MovssLoad(1, Fr(in.b));
                _a.MovssLoad(2, Fr(in.c));
                switch (in.op)
                {
                    case Op::UnS: _a.Ucomiss(1, 2); _a.Setcc(CP, T0); break;
                    case Op::OeqS: _a.Ucomiss(1, 2); _a.Setcc(CE, T0); _a.Setcc(CNP, T1); _a.And8(T0, T1); break;
                    case Op::UeqS: _a.Ucomiss(1, 2); _a.Setcc(CE, T0); break;
                    case Op::OltS: _a.Ucomiss(2, 1); _a.Setcc(CA, T0); break;
                    case Op::UltS: _a.Ucomiss(1, 2); _a.Setcc(CB, T0); break;
                    case Op::OleS: _a.Ucomiss(2, 1); _a.Setcc(CAE, T0); break;
                    default: _a.Ucomiss(1, 2); _a.Setcc(CBE, T0); break;
                }
                _a.Movzx8(T0, T0);
                if (in.a & 15)
                    _a.Shift(4, T0, in.a & 15);
                _a.Load(T1, St(offsetof(CpuState, br)));
                _a.AluImm(4, T1, ~(1u << (in.a & 15)));
                _a.Alu(0x09, T1, T0);
                _a.Store(St(offsetof(CpuState, br)), T1);
                return true;
            }

            // ---- control transfers (the block's last instruction) ----
            case Op::Beqz: case Op::BeqzN: case Op::Bnez: case Op::BnezN: case Op::Bltz: case Op::Bgez:
            {
                const uint32_t b = Src(in.b, T0);
                _a.Test(b, b);
                const Cc c = (in.op == Op::Beqz || in.op == Op::BeqzN) ? CE
                           : (in.op == Op::Bnez || in.op == Op::BnezN) ? CNE : in.op == Op::Bltz ? CS : CNS;
                EmitBranch(c, imm);
                return true;
            }
            case Op::Beqi: case Op::Bnei: case Op::Blti: case Op::Bgei:
                _a.AluImm(7, Src(in.b, T0), static_cast<uint32_t>(kB4Const[in.c & 15]));
                EmitBranch(in.op == Op::Beqi ? CE : in.op == Op::Bnei ? CNE : in.op == Op::Blti ? CL : CGE, imm);
                return true;
            case Op::Bltui: case Op::Bgeui:
                _a.AluImm(7, Src(in.b, T0), kB4ConstU[in.c & 15]);
                EmitBranch(in.op == Op::Bltui ? CB : CAE, imm);
                return true;
            case Op::Bbci: case Op::Bbsi:
                _a.TestImm(Src(in.b, T0), 1u << (in.c & 31));
                EmitBranch(in.op == Op::Bbci ? CE : CNE, imm);
                return true;
            case Op::Bbc: case Op::Bbs:
                _a.Bt(Src(in.b, T0), Src(in.c, T1)); // bit c mod 32
                EmitBranch(in.op == Op::Bbc ? CAE : CB, imm);
                return true;
            case Op::Bnone: case Op::Bany:
                _a.Test(Src(in.b, T0), Src(in.c, T1));
                EmitBranch(in.op == Op::Bnone ? CE : CNE, imm);
                return true;
            case Op::Ball: case Op::Bnall:
                _a.Mov(T0, Src(in.b, T0));
                _a.Not(T0);
                _a.Test(T0, Src(in.c, T1));
                EmitBranch(in.op == Op::Ball ? CE : CNE, imm);
                return true;
            case Op::Beq: case Op::Bne: case Op::Blt: case Op::Bge: case Op::Bltu: case Op::Bgeu:
            {
                _a.Alu(0x39, Src(in.b, T0), Src(in.c, T1));
                const Cc c = in.op == Op::Beq ? CE : in.op == Op::Bne ? CNE : in.op == Op::Blt ? CL
                           : in.op == Op::Bge ? CGE : in.op == Op::Bltu ? CB : CAE;
                EmitBranch(c, imm);
                return true;
            }
            case Op::Bt: case Op::Bf:
                _a.TestMemImm(St(offsetof(CpuState, br)), 1u << (in.b & 15));
                EmitBranch(in.op == Op::Bt ? CNE : CE, imm);
                return true;
            case Op::Loop: case Op::Loopnez: case Op::Loopgtz:
            {
                const uint32_t b = Src(in.b, T1);
                _a.Lea(T0, Mem{b, -1});
                _a.Store(St(offsetof(CpuState, lcount)), T0);
                _a.StoreImm(St(offsetof(CpuState, lbeg)), in.pc + 3);
                _a.StoreImm(St(offsetof(CpuState, lend)), imm);
                if (in.op != Op::Loop)
                {
                    _a.Test(b, b);
                    EmitBranch(in.op == Op::Loopnez ? CE : CLE, imm);
                }
                return true;
            }
            case Op::J: BranchTo(_a.Jmp(), imm); return true;
            case Op::Jx: _a.Mov(T3, Src(in.b, T3)); EmitExit(0, 0, true); return true;
            case Op::Ret: case Op::RetN: _a.Mov(T3, Src(0, T3)); EmitExit(0, 0, true); return true;
            case Op::Call0: case Op::Callx0:
                if (in.op == Op::Callx0)
                    _a.Mov(T3, Src(in.b, T3));
                SetGuest(0, in.pc + 3);
                _a.StoreImm(St(offsetof(CpuState, callKind)), 0);
                EmitExit(imm, 0, in.op == Op::Callx0);
                return true;
            case Op::Call4: case Op::Call8: case Op::Call12: case Op::Callx4: case Op::Callx8: case Op::Callx12:
            {
                const uint32_t nn = (in.op == Op::Call4 || in.op == Op::Callx4) ? 1 : (in.op == Op::Call8 || in.op == Op::Callx8) ? 2 : 3;
                const bool indirect = in.op == Op::Callx4 || in.op == Op::Callx8 || in.op == Op::Callx12;
                if (indirect)
                    _a.Mov(T3, Src(in.b, T3));
                SetGuest(static_cast<uint8_t>(nn * 4), (nn << 30) | ((in.pc + 3) & 0x3FFFFFFFu));
                _a.AluMemImm(4, St(offsetof(CpuState, ps)), ~0x30000u);
                _a.AluMemImm(1, St(offsetof(CpuState, ps)), nn << 16);
                _a.StoreImm(St(offsetof(CpuState, callKind)), nn);
                EmitExit(imm, 0, indirect);
                return true;
            }
            default:
                return false;
        }
    }

    uint32_t H(uint8_t g) const { return _host[g & 15]; }

    void SetGuest(uint8_t g, uint32_t v)
    {
        if (Spilled(g))
            _a.StoreImm(Slot(g), v);
        else
            _a.MovImmKeep(H(g), v);
    }

    const std::vector<RegionBlock>& _blocks;
    RegionLoop _loop;
    Cpu& _cpu;
    uint8_t* (*_xlate)(Cpu&, uint32_t);
    std::map<uint32_t, size_t> _index;
    X64 _a;
    uint32_t _host[16] = {};
    uint32_t _hostUsed = 0;
    uint32_t _pushes = 0;
    int32_t _frame = 0;
    uint16_t _used = 0;
    uint16_t _written = 0;
    std::vector<size_t> _labels;
    std::vector<Fixup> _toLabel;
    std::vector<Stub> _stubs;
    const RegionBlock* _cur = nullptr;
    size_t _curIndex = 0;
    uint32_t _k = 0;
};

} // namespace
} // namespace xtensa
