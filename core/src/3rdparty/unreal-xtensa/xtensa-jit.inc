// xtensa-jit.inc - POC 051 (tools/poc/051-xtensa-jit), included at the end of xtensa-cpu.cpp: a region JIT for
// arm64 (xtensa-jit-a64.inc) and x86_64 (xtensa-jit-x64.inc) hosts. Never lands as is.
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
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

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

/// Kinds the backend compiles beyond the shared list (xtensa-jit-a64.inc / xtensa-jit-x64.inc)
bool BackendOk(const Insn& in);

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
    while (n < count && UseOf(insns[n]).ok && BackendOk(insns[n]) &&
           !(IsLoopOp(insns[n].op) &&
             (!loop.has || insns[n].pc + 3 != loop.beg || static_cast<uint32_t>(insns[n].imm) != loop.end)))
        ++n;
    return n;
}


} // namespace

} // namespace xtensa

#if defined(__aarch64__)
#include "xtensa-jit-a64.inc"
#else
#include "xtensa-jit-x64.inc"
#endif

namespace xtensa
{

// ---- arena -------------------------------------------------------------------------------------------------------

struct Cpu::JitArena
{
    static constexpr size_t kSize = 64u << 20;
    uint8_t* base = nullptr;
    size_t used = 0;

    JitArena()
    {
#if defined(_WIN32)
        base = static_cast<uint8_t*>(VirtualAlloc(nullptr, kSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
#else
        int flags = MAP_PRIVATE | MAP_ANON;
#if defined(__APPLE__)
        flags |= MAP_JIT;
#endif
        void* p = mmap(nullptr, kSize, PROT_READ | PROT_WRITE | PROT_EXEC, flags, -1, 0);
        base = p == MAP_FAILED ? nullptr : static_cast<uint8_t*>(p);
#endif
    }
    ~JitArena()
    {
        if (!base)
            return;
#if defined(_WIN32)
        VirtualFree(base, 0, MEM_RELEASE);
#else
        munmap(base, kSize);
#endif
    }
};

void Cpu::JitArenaDelete::operator()(JitArena* a) const
{
    delete a;
}

void Cpu::SetJit(bool on, uint32_t hot)
{
    jitOn_ = on;
    if (hot != 0)
        jitHot_ = hot;
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
    const std::vector<uint8_t> code = c.Compile();
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
    const size_t bytes = code.size();
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
#elif defined(_WIN32)
    FlushInstructionCache(GetCurrentProcess(), at, bytes);
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
