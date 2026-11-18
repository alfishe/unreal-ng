// xtensa-cpu.cpp - execution engine of unreal-xtensa, see xtensa-cpu.h.

#include "xtensa-cpu.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace xtensa
{

// Instruction counts of the ESP-IDF window vectors (xtensa_vectors.S), including RFWO / RFWU.
constexpr uint32_t kOverflowCycles[4] = {0, 5, 10, 14};
constexpr uint32_t kUnderflowCycles[4] = {0, 5, 10, 14};
constexpr uint32_t kAllocaCycles = 12; ///< _xt_alloca_exc up to the underflow handler

struct Cpu::PageEntry
{
    uintptr_t bias = 0;      ///< host address = bias + guest address
    uint32_t flags = 0;
    CodePage* code = nullptr; ///< decoded-code bitmap of the backing host page, nullptr if none
};

struct Cpu::CodePage
{
    uint64_t bits[64] = {}; ///< one bit per byte of the 4 KB host page that belongs to a decoded instruction
};

struct Cpu::Block
{
    uint32_t start = 0;
    uint32_t end = 0;      ///< address after the last instruction
    uint32_t count = 0;    ///< instructions, not counting the BlockEnd pseudo-op at insns[count]
    uint8_t maxGroup = 0;
    std::vector<Insn> insns;
#if UNREAL_XTENSA_JIT
    void* jit = nullptr;   ///< compiled region headed by this block (POC 051)
    uint32_t jitLo = 0;    ///< the region's address range (blocks' starts to ends)
    uint32_t jitHi = 0;
    uint8_t jitGroup = 0;  ///< highest register group the region touches
    bool jitHasLoop = false;
    uint32_t jitLoopEnd = 0; ///< LEND of the zero-overhead loop the region runs
    uint32_t hot = 0;
    bool jitTried = false;
#endif
#ifdef UNREAL_XTENSA_PROFILE
    uint64_t profileEntries = 0;
    uint64_t profileInsns = 0;
#endif
};

const char* StopReasonName(StopReason reason)
{
    switch (reason)
    {
        case StopReason::None: return "None";
        case StopReason::Budget: return "Budget";
        case StopReason::Trap: return "Trap";
        case StopReason::Break: return "Break";
        case StopReason::IllegalInstruction: return "IllegalInstruction";
        case StopReason::Unimplemented: return "Unimplemented";
        case StopReason::MemoryFault: return "MemoryFault";
        case StopReason::Privileged: return "Privileged";
        case StopReason::DivideByZero: return "DivideByZero";
        case StopReason::Syscall: return "Syscall";
    }
    return "?";
}

namespace
{

inline float ToF(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

inline uint32_t ToB(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

inline bool IsNan(uint32_t b)
{
    return (b & 0x7FFFFFFFu) > 0x7F800000u;
}

/// Arithmetic result: any NaN becomes the default quiet NaN.
inline uint32_t Result(float f)
{
    const uint32_t b = ToB(f);
    return IsNan(b) ? 0x7FC00000u : b;
}

/// a * b + c rounded once, the same on every host. The C library's fmaf is not exact everywhere (MinGW-w64 rounds
/// twice: one result in ten differs), so off arm64 it is computed here: a * b is exact in double, the double sum is
/// rounded once more to float, which only matters when it lands exactly halfway between two floats and is inexact;
/// then the last bit moves towards the true value (musl's fmaf). Round to nearest even, no exception flags.
inline float FusedMulAdd(float a, float b, float c)
{
#if defined(__aarch64__) || defined(_M_ARM64)
    return std::fma(a, b, c); // FMADD
#else
    const double xy = static_cast<double>(a) * static_cast<double>(b);
    const double result = xy + static_cast<double>(c);
    uint64_t u;
    std::memcpy(&u, &result, sizeof(u));
    const uint32_t e = static_cast<uint32_t>(u >> 52) & 0x7FFu;
    const bool halfway = (u & 0x1FFFFFFFu) == 0x10000000u;
    const bool exact = result - xy == static_cast<double>(c) && result - static_cast<double>(c) == xy;
    if (!halfway || e == 0x7FFu || exact)
        return static_cast<float>(result);
    const bool negative = (u >> 63) != 0;
    const double err = negative == (static_cast<double>(c) > xy) ? xy - result + static_cast<double>(c)
                                                                : static_cast<double>(c) - result + xy;
    if (negative == (err < 0))
        ++u;
    else
        --u;
    double adjusted;
    std::memcpy(&adjusted, &u, sizeof(adjusted));
    return static_cast<float>(adjusted);
#endif
}

constexpr float kInvPow2[16] = {1.0f,          0.5f,          0.25f,          0.125f,          0.0625f,       0.03125f,
                                0.015625f,     0.0078125f,    0.00390625f,    0.001953125f,    0.0009765625f, 0.00048828125f,
                                0.000244140625f, 0.0001220703125f, 0.00006103515625f, 0.000030517578125f};

enum class Rounding
{
    Nearest,
    Zero,
    Down,
    Up
};

/// ROUND.S / TRUNC.S / FLOOR.S / CEIL.S: fs * 2^scale to int32 with saturation; NaN -> 0x7FFFFFFF.
uint32_t FloatToInt(uint32_t bits, int scale, Rounding mode)
{
    if (IsNan(bits))
        return 0x7FFFFFFFu;
    const double d = static_cast<double>(ToF(bits)) * static_cast<double>(1u << scale); // exact
    double r;
    switch (mode)
    {
        case Rounding::Zero: r = std::trunc(d); break;
        case Rounding::Down: r = std::floor(d); break;
        case Rounding::Up: r = std::ceil(d); break;
        default:
        {
            r = std::floor(d);
            const double diff = d - r; // exact for |d| < 2^52
            if (diff > 0.5 || (diff == 0.5 && std::fmod(r, 2.0) != 0.0))
                r += 1.0;
            break;
        }
    }
    if (r >= 2147483648.0)
        return 0x7FFFFFFFu;
    if (r < -2147483648.0)
        return 0x80000000u;
    return static_cast<uint32_t>(static_cast<int32_t>(r));
}

/// UTRUNC.S: NaN and values >= 2^32 -> 0xFFFFFFFF; negative values convert as TRUNC.S.
uint32_t FloatToUint(uint32_t bits, int scale)
{
    if (IsNan(bits))
        return 0xFFFFFFFFu;
    if (bits & 0x80000000u)
        return FloatToInt(bits, scale, Rounding::Zero);
    const double r = std::trunc(static_cast<double>(ToF(bits)) * static_cast<double>(1u << scale));
    if (r >= 4294967296.0)
        return 0xFFFFFFFFu;
    return static_cast<uint32_t>(r);
}

inline uint32_t Nsa(uint32_t v)
{
    const uint32_t x = (v & 0x80000000u) ? ~v : v;
    if (x == 0)
        return 31;
    uint32_t n = 0;
    while (!(x & (0x40000000u >> n)))
        ++n;
    return n;
}

inline uint32_t Nsau(uint32_t v)
{
    if (v == 0)
        return 32;
    uint32_t n = 0;
    while (!(v & (0x80000000u >> n)))
        ++n;
    return n;
}

inline uint32_t Rotr16(uint32_t ws, uint32_t n)
{
    n &= 15;
    return ((ws >> n) | (ws << (16 - n))) & 0xFFFFu;
}

} // namespace

Cpu::Cpu()
{
    UpdateSafeGroups();
#if UNREAL_XTENSA_JIT
    JitInitFromEnv();
#endif
}

Cpu::~Cpu() = default;

void Cpu::Reset()
{
    state_ = CpuState{};
    UpdateSafeGroups();
}

void Cpu::SetState(const CpuState& in)
{
    state_ = in;
    UpdateSafeGroups();
}

float Cpu::Fr(int n) const
{
    return ToF(state_.fr[n & 15]);
}

void Cpu::SetFr(int n, float v)
{
    state_.fr[n & 15] = ToB(v);
}

// ---------------------------------------------------------------------------------------------------------------------
// Memory

bool Cpu::AddRegion(uint32_t guestBase, uint32_t size, uint8_t* host, uint32_t flags)
{
    if ((guestBase & 0xFFF) || (size & 0xFFF) || size == 0 || !host ||
        static_cast<uint64_t>(guestBase) + size > 0x100000000ull)
        return false;
    regions_.push_back({guestBase, size, host, flags});
    const uintptr_t bias = reinterpret_cast<uintptr_t>(host) - guestBase;
    for (uint64_t a = guestBase; a < static_cast<uint64_t>(guestBase) + size; a += 4096)
    {
        const uint32_t addr = static_cast<uint32_t>(a);
        auto& table = l1_[addr >> 22];
        if (!table)
            table.reset(new PageEntry[1024]);
        PageEntry& e = table[(addr >> 12) & 1023];
        e.bias = bias;
        e.flags = flags;
        e.code = nullptr;
    }
    InvalidateCode();
    return true;
}

void Cpu::ClearRegions()
{
    InvalidateCode();
    regions_.clear();
    for (auto& t : l1_)
        t.reset();
}

Cpu::PageEntry* Cpu::Entry(uint32_t addr) const
{
    PageEntry* t = l1_[addr >> 22].get();
    return t ? &t[(addr >> 12) & 1023] : nullptr;
}

inline uint8_t* Cpu::Xlate(uint32_t addr, uint32_t flag) const
{
    PageEntry* t = l1_[addr >> 22].get();
    if (!t)
        return nullptr;
    const PageEntry& e = t[(addr >> 12) & 1023];
    if (!(e.flags & flag))
        return nullptr;
    return reinterpret_cast<uint8_t*>(e.bias + addr);
}

inline bool Cpu::Load(uint32_t addr, int size, uint32_t& value)
{
    if ((addr & 0xFFF) <= static_cast<uint32_t>(0x1000 - size))
    {
        const uint8_t* p = Xlate(addr, RegionRead);
        if (!p)
            return false;
        if (size == 4)
        {
            uint32_t v;
            std::memcpy(&v, p, 4);
            value = v;
        }
        else if (size == 2)
        {
            uint16_t v;
            std::memcpy(&v, p, 2);
            value = v;
        }
        else
            value = *p;
        return true;
    }
    uint32_t v = 0;
    for (int i = 0; i < size; ++i)
    {
        const uint8_t* p = Xlate(addr + static_cast<uint32_t>(i), RegionRead);
        if (!p)
            return false;
        v |= static_cast<uint32_t>(*p) << (8 * i);
    }
    value = v;
    return true;
}

inline bool Cpu::Store(uint32_t addr, int size, uint32_t value)
{
    if ((addr & 0xFFF) <= static_cast<uint32_t>(0x1000 - size))
    {
        PageEntry* t = l1_[addr >> 22].get();
        if (!t)
            return false;
        PageEntry& e = t[(addr >> 12) & 1023];
        if (!(e.flags & RegionWrite))
            return false;
        uint8_t* p = reinterpret_cast<uint8_t*>(e.bias + addr);
        if (size == 4)
            std::memcpy(p, &value, 4);
        else if (size == 2)
        {
            const uint16_t v = static_cast<uint16_t>(value);
            std::memcpy(p, &v, 2);
        }
        else
            *p = static_cast<uint8_t>(value);
        if (e.code)
            CodeWriteCheck(e, addr, size);
        return true;
    }
    for (int i = 0; i < size; ++i) // crosses a page: check both pages before writing anything
    {
        PageEntry* e = Entry(addr + static_cast<uint32_t>(i));
        if (!e || !(e->flags & RegionWrite))
            return false;
    }
    for (int i = 0; i < size; ++i)
    {
        const uint32_t a = addr + static_cast<uint32_t>(i);
        PageEntry* e = Entry(a);
        *reinterpret_cast<uint8_t*>(e->bias + a) = static_cast<uint8_t>(value >> (8 * i));
        if (e->code)
            CodeWriteCheck(*e, a, 1);
    }
    return true;
}

void Cpu::CodeWriteCheck(PageEntry& e, uint32_t addr, int size)
{
    const uint32_t off = addr & 0xFFF;
    for (int i = 0; i < size; ++i)
    {
        const uint32_t o = off + static_cast<uint32_t>(i);
        if (o < 4096 && (e.code->bits[o >> 6] >> (o & 63)) & 1)
        {
#ifdef UNREAL_XTENSA_PROFILE
            // POC 051: which guest stores hit decoded code (each one drops every block)
            static uint64_t hits = 0;
            if (++hits <= 40 || (hits & (hits - 1)) == 0)
                std::fprintf(stderr, "xtensa: code write #%llu at %08x pc %08x blocks %zu\n",
                             static_cast<unsigned long long>(hits), addr + static_cast<uint32_t>(i), state_.pc,
                             blockMap_.size());
#endif
            InvalidateCode();
            codeModified_ = true;
            return;
        }
    }
}

bool Cpu::Read8(uint32_t addr, uint8_t& value)
{
    uint32_t v;
    if (!Load(addr, 1, v))
        return false;
    value = static_cast<uint8_t>(v);
    return true;
}

bool Cpu::Read16(uint32_t addr, uint16_t& value)
{
    uint32_t v;
    if (!Load(addr, 2, v))
        return false;
    value = static_cast<uint16_t>(v);
    return true;
}

bool Cpu::Read32(uint32_t addr, uint32_t& value)
{
    return Load(addr, 4, value);
}

bool Cpu::Write8(uint32_t addr, uint8_t value)
{
    return Store(addr, 1, value);
}

bool Cpu::Write16(uint32_t addr, uint16_t value)
{
    return Store(addr, 2, value);
}

bool Cpu::Write32(uint32_t addr, uint32_t value)
{
    return Store(addr, 4, value);
}

bool Cpu::ReadBlock(uint32_t addr, void* dst, size_t size)
{
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (size_t i = 0; i < size; ++i)
    {
        const uint8_t* p = Xlate(addr + static_cast<uint32_t>(i), RegionRead);
        if (!p)
            return false;
        d[i] = *p;
    }
    return true;
}

bool Cpu::WriteBlock(uint32_t addr, const void* src, size_t size)
{
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (size_t i = 0; i < size; ++i)
        if (!Store(addr + static_cast<uint32_t>(i), 1, s[i]))
            return false;
    return true;
}

uint8_t* Cpu::HostPointer(uint32_t addr, uint32_t size, uint32_t flags)
{
    uint8_t* p = Xlate(addr, flags);
    if (!p || size == 0)
        return p;
    for (uint64_t a = (addr & ~0xFFFull) + 4096; a < static_cast<uint64_t>(addr) + size; a += 4096)
        if (Xlate(static_cast<uint32_t>(a), flags) != p + (a - addr))
            return nullptr;
    return p;
}

#ifdef UNREAL_XTENSA_PROFILE
void Cpu::ProfileFold(const Block& b)
{
    if (b.profileEntries == 0)
        return;
    ProfileEntry& e = profileRetired_[b.start];
    e.len = b.count;
    e.entries += b.profileEntries;
    e.insns += b.profileInsns;
    if (e.ops.empty())
        for (uint32_t i = 0; i < b.count; ++i)
            e.ops.push_back(static_cast<uint16_t>(b.insns[i].op));
}

void Cpu::DumpProfile(const char* path) const
{
    std::unordered_map<uint32_t, ProfileEntry> all = profileRetired_;
    for (const auto& kv : blockMap_)
    {
        const Block& b = *kv.second;
        if (b.profileEntries == 0)
            continue;
        ProfileEntry& e = all[b.start];
        e.len = b.count;
        e.entries += b.profileEntries;
        e.insns += b.profileInsns;
        if (e.ops.empty())
            for (uint32_t i = 0; i < b.count; ++i)
                e.ops.push_back(static_cast<uint16_t>(b.insns[i].op));
    }
    FILE* f = std::fopen(path, "w");
    if (!f)
        return;
    for (const auto& kv : all)
    {
        const ProfileEntry& e = kv.second;
        std::fprintf(f, "%08x %u %llu %llu", kv.first, e.len, static_cast<unsigned long long>(e.entries),
                     static_cast<unsigned long long>(e.insns));
        for (uint16_t op : e.ops)
            std::fprintf(f, " %s", Mnemonic(static_cast<Op>(op)));
        std::fprintf(f, "\n");
    }
    std::fclose(f);
}
#endif

void Cpu::InvalidateCode()
{
#ifdef UNREAL_XTENSA_PROFILE
    profileLast_ = nullptr;
    for (auto& kv : blockMap_)
        ProfileFold(*kv.second);
#endif
    for (auto& kv : blockMap_)
        retired_.push_back(std::move(kv.second));
    blockMap_.clear();
    std::memset(lookup_, 0, sizeof(lookup_));
    for (PageEntry* e : codeEntries_)
        e->code = nullptr;
    codeEntries_.clear();
    codePages_.clear();
}

void Cpu::FreeRetired()
{
    retired_.clear();
}

void Cpu::MarkCode(uint32_t addr, int len)
{
    for (int i = 0; i < len; ++i)
    {
        const uint32_t a = addr + static_cast<uint32_t>(i);
        PageEntry* e = Entry(a);
        if (!e)
            continue;
        if (!e->code)
        {
            // The bitmap belongs to the host page: attach it to every guest alias of that page.
            const uintptr_t hostPage = (e->bias + (a & ~0xFFFu));
            auto& cp = codePages_[hostPage];
            if (!cp)
                cp.reset(new CodePage);
            for (const Region& r : regions_)
            {
                const uintptr_t h0 = reinterpret_cast<uintptr_t>(r.host);
                if (hostPage >= h0 && hostPage < h0 + r.size)
                {
                    PageEntry* alias = Entry(r.base + static_cast<uint32_t>(hostPage - h0));
                    if (alias && alias->bias == reinterpret_cast<uintptr_t>(r.host) - r.base && !alias->code)
                    {
                        alias->code = cp.get();
                        codeEntries_.push_back(alias);
                    }
                }
            }
            if (!e->code) // mapping replaced since: keep this entry consistent anyway
            {
                e->code = cp.get();
                codeEntries_.push_back(e);
            }
        }
        const uint32_t o = a & 0xFFF;
        e->code->bits[o >> 6] |= 1ull << (o & 63);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Traps and host calls

void Cpu::SetTrapRange(uint32_t base, uint32_t size)
{
    trapBase_ = base;
    trapSize_ = size;
    if (sentinel_ == 0)
        sentinel_ = base;
    InvalidateCode(); // blocks stop before trap addresses
}

void Cpu::SetCallSentinel(uint32_t pc)
{
    // Hosts set the sentinel before every call into the guest, almost always the same one: the decoded blocks already
    // stop before it, dropping them would decode the whole working set again at every call
    if (pc == sentinel_)
        return;
    sentinel_ = pc;
    InvalidateCode();
}

uint32_t Cpu::Arg(int i)
{
    if (i < 0)
        return 0;
    const uint32_t n = state_.callKind & 3;
    if (n == 0)
    {
        if (i < 6)
            return Ar(2 + i);
        uint32_t v = 0;
        Load(Ar(1) + 4u * static_cast<uint32_t>(i - 6), 4, v);
        return v;
    }
    if (i < 6)
    {
        if (state_.trapEntered)
            return Ar(2 + i);
        return Phys(state_.windowBase, n * 4 + 2 + static_cast<uint32_t>(i));
    }
    const uint32_t sp = state_.trapEntered ? state_.trapArgSp : Ar(1);
    uint32_t v = 0;
    Load(sp + 4u * static_cast<uint32_t>(i - 6), 4, v);
    return v;
}

void Cpu::ReturnFromTrap(uint32_t a2, uint32_t a3)
{
    const uint32_t n = state_.callKind & 3;
    if (n == 0)
    {
        Ar(2) = a2;
        Ar(3) = a3;
        state_.pc = Ar(0);
        return;
    }
    if (!state_.trapEntered)
    {
        const uint32_t wb = state_.windowBase;
        Phys(wb, n * 4 + 2) = a2;
        Phys(wb, n * 4 + 3) = a3;
        state_.pc = (state_.pc & 0xC0000000u) | (Phys(wb, n * 4) & 0x3FFFFFFFu);
        return;
    }
    // The host entered the callee's frame: do its RETW (with the underflow of the caller if it was spilled).
    Ar(2) = a2;
    Ar(3) = a3;
    state_.trapEntered = 0;
    const uint32_t ra = Ar(0);
    const uint32_t callN = ra >> 30;
    const uint32_t c = state_.windowBase;
    const uint32_t m = (c - callN) & 15;
    if (!((state_.windowStart >> m) & 1))
    {
        uint32_t fault = 0;
        FillFrame(m, callN, fault);
    }
    state_.windowStart &= ~(1u << c);
    state_.windowBase = m;
    UpdateSafeGroups();
    state_.pc = (state_.pc & 0xC0000000u) | (ra & 0x3FFFFFFFu);
}

void Cpu::EnterTrapFrame(uint32_t sp)
{
    const uint32_t n = state_.callKind & 3;
    state_.trapArgSp = Ar(1);
    const uint32_t c = (state_.windowBase + n) & 15;
    Phys(c, 1) = sp;
    state_.windowBase = c;
    state_.windowStart |= 1u << c;
    state_.trapEntered = 1;
    UpdateSafeGroups();
}

bool Cpu::BeginCall(uint32_t pc, const uint32_t* args, int n, uint32_t stackPointer)
{
    if (state_.hostDepth >= static_cast<uint32_t>(kMaxHostFrames))
        return false;
    HostFrame& hf = state_.hostFrames[state_.hostDepth];
    hf = HostFrame{};
    hf.savedPc = state_.pc;
    hf.savedCallKind = state_.callKind;
    const bool atTrap = InTrapRange(state_.pc);
    if (atTrap && (state_.callKind & 3) == 0)
    {
        hf.kind = 2;
        for (int i = 0; i < 16; ++i)
            hf.savedWindow[i] = Ar(i);
        Ar(1) = stackPointer;
    }
    else if (atTrap)
    {
        hf.kind = 1;
        if (!state_.trapEntered)
            EnterTrapFrame(stackPointer);
    }
    else
    {
        hf.kind = 0;
        if (!((state_.windowStart >> state_.windowBase) & 1))
            state_.windowStart |= 1u << state_.windowBase;
        UpdateSafeGroups();
        Ar(1) = stackPointer;
    }
    hf.savedTrapEntered = state_.trapEntered;
    hf.savedTrapArgSp = state_.trapArgSp;
    state_.trapEntered = 0;
    state_.hostDepth++;

    uint32_t fault = 0;
    if (!WindowCheck(2, fault))
        return false;
    const uint32_t sp = Ar(1);
    Ar(4) = (1u << 30) | (sentinel_ & 0x3FFFFFFFu);
    for (int i = 0; i < n; ++i)
    {
        if (i < 6)
            Ar(6 + i) = args[i];
        else if (!Store(sp + 4u * static_cast<uint32_t>(i - 6), 4, args[i]))
            return false;
    }
    state_.ps = (state_.ps & ~0x30000u) | (1u << 16);
    state_.callKind = 1;
    state_.pc = pc;
    return true;
}

uint32_t Cpu::EndCall(uint32_t* a3)
{
    const uint32_t r2 = Ar(6);
    if (a3)
        *a3 = Ar(7);
    if (state_.hostDepth == 0)
        return r2;
    const HostFrame& hf = state_.hostFrames[--state_.hostDepth];
    if (hf.kind == 2)
        for (int i = 0; i < 16; ++i)
            Ar(i) = hf.savedWindow[i];
    state_.pc = hf.savedPc;
    state_.callKind = hf.savedCallKind;
    state_.trapEntered = hf.savedTrapEntered;
    state_.trapArgSp = hf.savedTrapArgSp;
    state_.ps = (state_.ps & ~0x30000u) | ((hf.savedCallKind & 3) << 16);
    return r2;
}

bool Cpu::SpillAllWindows()
{
    if (InTrapRange(state_.pc) && (state_.callKind & 3) != 0 && !state_.trapEntered)
        EnterTrapFrame(Ar(1) - 16);
    uint32_t fault = 0;
    return SpillAll(fault);
}

// ---------------------------------------------------------------------------------------------------------------------
// Register windows

void Cpu::UpdateSafeGroups()
{
    const uint32_t rot = Rotr16(state_.windowStart, state_.windowBase);
    safeGroups_ = (rot & 2) ? 0 : (rot & 4) ? 1 : (rot & 8) ? 2 : 3;
}

/// Hardware WindowCheck for an instruction touching register group `group` (1..3) of the current window.
bool Cpu::WindowCheck(int group, uint32_t& faultAddress)
{
    while (static_cast<uint32_t>(group) > safeGroups_)
    {
        const uint32_t m = (state_.windowBase + safeGroups_ + 1) & 15;
        if (!SpillFrame(m, faultAddress))
            return false;
    }
    return true;
}

/// _WindowOverflow4/8/12 for the frame at window base m (oldest live frame).
bool Cpu::SpillFrame(uint32_t m, uint32_t& faultAddress)
{
    const uint32_t ws = state_.windowStart;
    auto R = [&](uint32_t n) -> uint32_t& { return Phys(m, n); };
    auto S = [&](uint32_t value, uint32_t addr) -> bool {
        if (Store(addr, 4, value))
            return true;
        faultAddress = addr;
        return false;
    };
    auto L = [&](uint32_t addr, uint32_t& value) -> bool {
        if (Load(addr, 4, value))
            return true;
        faultAddress = addr;
        return false;
    };
    uint32_t size;
    if ((ws >> ((m + 1) & 15)) & 1)
    {
        size = 1;
        const uint32_t sp = R(5);
        if (!S(R(0), sp - 16) || !S(R(1), sp - 12) || !S(R(2), sp - 8) || !S(R(3), sp - 4))
            return false;
    }
    else if ((ws >> ((m + 2) & 15)) & 1)
    {
        size = 2;
        if (!S(R(0), R(9) - 16))
            return false;
        uint32_t t;
        if (!L(R(1) - 12, t))
            return false;
        R(0) = t;
        if (!S(R(1), R(9) - 12) || !S(R(2), R(9) - 8) || !S(R(3), R(9) - 4))
            return false;
        for (uint32_t i = 0; i < 4; ++i)
            if (!S(R(4 + i), R(0) - 32 + 4 * i))
                return false;
    }
    else
    {
        size = 3;
        if (!S(R(0), R(13) - 16))
            return false;
        uint32_t t;
        if (!L(R(1) - 12, t))
            return false;
        R(0) = t;
        if (!S(R(1), R(13) - 12) || !S(R(2), R(13) - 8) || !S(R(3), R(13) - 4))
            return false;
        for (uint32_t i = 0; i < 8; ++i)
            if (!S(R(4 + i), R(0) - 48 + 4 * i))
                return false;
    }
    state_.windowStart &= ~(1u << m);
    handlerCycles_ += kOverflowCycles[size];
    UpdateSafeGroups();
    return true;
}

/// _WindowUnderflow4/8/12: restores the frame at window base m, called with CALL(4 * n) by the current frame.
bool Cpu::FillFrame(uint32_t m, uint32_t n, uint32_t& faultAddress)
{
    auto R = [&](uint32_t k) -> uint32_t& { return Phys(m, k); };
    auto L = [&](uint32_t addr, uint32_t& value) -> bool {
        uint32_t v;
        if (Load(addr, 4, v))
        {
            value = v;
            return true;
        }
        faultAddress = addr;
        return false;
    };
    if (n <= 1)
    {
        const uint32_t sp = R(5);
        if (!L(sp - 16, R(0)) || !L(sp - 12, R(1)) || !L(sp - 8, R(2)) || !L(sp - 4, R(3)))
            return false;
        n = 1;
    }
    else if (n == 2)
    {
        const uint32_t sp = R(9);
        if (!L(sp - 16, R(0)) || !L(sp - 12, R(1)) || !L(sp - 8, R(2)) || !L(R(1) - 12, R(7)) || !L(sp - 4, R(3)))
            return false;
        const uint32_t base = R(7);
        for (uint32_t i = 0; i < 4; ++i)
            if (!L(base - 32 + 4 * i, R(4 + i)))
                return false;
    }
    else
    {
        const uint32_t sp = R(13);
        if (!L(sp - 16, R(0)) || !L(sp - 12, R(1)) || !L(sp - 8, R(2)) || !L(R(1) - 12, R(11)) || !L(sp - 4, R(3)))
            return false;
        const uint32_t base = R(11);
        for (uint32_t i = 0; i < 8; ++i)
            if (!L(base - 48 + 4 * i, R(4 + i)))
                return false;
    }
    state_.windowStart |= 1u << m;
    handlerCycles_ += kUnderflowCycles[n];
    UpdateSafeGroups();
    return true;
}

bool Cpu::SpillAll(uint32_t& faultAddress)
{
    for (;;)
    {
        const uint32_t rot = Rotr16(state_.windowStart, state_.windowBase) & ~1u;
        if (!rot)
            return true;
        uint32_t k = 1;
        while (!((rot >> k) & 1))
            ++k;
        if (!SpillFrame((state_.windowBase + k) & 15, faultAddress))
            return false;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Blocks

Cpu::Block* Cpu::Lookup(uint32_t pc)
{
    Block*& slot = lookup_[(pc >> 1) & (kLookupSize - 1)];
    if (slot && slot->start == pc)
        return slot;
    auto it = blockMap_.find(pc);
    if (it == blockMap_.end())
        return nullptr;
    slot = it->second.get();
    return slot;
}

Cpu::Block* Cpu::Build(uint32_t pc)
{
    std::unique_ptr<Block> b(new Block);
    b->start = pc;
    uint32_t a = pc;
    int maxGroup = 0;
    b->insns.reserve(16);
    while (b->insns.size() < kMaxBlockInsns)
    {
        if (!b->insns.empty() && (InTrapRange(a) || ((a ^ sentinel_) & 0x3FFFFFFFu) == 0))
            break;
        uint8_t bytes[4];
        size_t got = 0;
        while (got < 4)
        {
            const uint8_t* p = Xlate(a + static_cast<uint32_t>(got), RegionExec);
            if (!p)
                break;
            bytes[got++] = *p;
        }
        Insn in;
        if (!Decode(a, bytes, got, in))
            break;
        MarkCode(a, in.len);
        b->insns.push_back(in);
        if (in.maxGroup > maxGroup)
            maxGroup = in.maxGroup;
        a += in.len;
        if (in.flags & InsnEndsBlock)
            break;
        if (a == state_.lend)
            break;
    }
    if (b->insns.empty())
        return nullptr;
    b->count = static_cast<uint32_t>(b->insns.size());
    Insn endMark;
    endMark.op = Op::BlockEnd;
    endMark.pc = a;
    b->insns.push_back(endMark);
    b->end = a;
    b->maxGroup = static_cast<uint8_t>(maxGroup);
    Block* raw = b.get();
    blockMap_[pc] = std::move(b);
    lookup_[(pc >> 1) & (kLookupSize - 1)] = raw;
    return raw;
}

// ---------------------------------------------------------------------------------------------------------------------
// Run

RunResult Cpu::Run(uint64_t maxInstructions)
{
    RunResult res;
    uint64_t executed = 0;
    handlerCycles_ = 0;
    for (;;)
    {
        if (!retired_.empty())
            FreeRetired();
        if (executed + handlerCycles_ >= maxInstructions)
        {
            res.reason = StopReason::Budget;
            break;
        }
        const uint32_t pc = state_.pc;
        if (InTrapRange(pc) || ((pc ^ sentinel_) & 0x3FFFFFFFu) == 0)
        {
            res.reason = StopReason::Trap;
            break;
        }
        Block* b = Lookup(pc);
        if (!b)
        {
            b = Build(pc);
            if (!b)
            {
                res.reason = StopReason::MemoryFault;
                res.faultAddress = pc;
                break;
            }
        }
#ifdef UNREAL_XTENSA_PROFILE
        profileLast_ = nullptr;
#endif
        const ExecOut out = Exec(b, maxInstructions, executed);
#ifdef UNREAL_XTENSA_PROFILE
        if (profileLast_)
            profileLast_->profileInsns += executed - profileMark_;
        profileLast_ = nullptr;
#endif
        if (out.stop != StopReason::None)
        {
            res.reason = out.stop;
            res.faultAddress = out.faultAddress;
            break;
        }
    }
    executed += handlerCycles_;
    state_.ccount += executed;
#if UNREAL_XTENSA_JIT
    if (jitOn_)
    {
        jitStats_.total += executed;
        static const bool print = std::getenv("UNREAL_XTENSA_JIT_STATS") != nullptr;
        if (print && (++jitStats_.runs % 100000) == 0)
            std::fprintf(stderr,
                         "xtensa-jit: native %.1f %% of %llu; regions %llu (rejected %llu) blocks %llu; entries: "
                         "native %llu, no region %llu, window %llu, loop %llu, budget %llu\n",
                         100.0 * static_cast<double>(jitStats_.insns) / static_cast<double>(jitStats_.total),
                         static_cast<unsigned long long>(jitStats_.total),
                         static_cast<unsigned long long>(jitStats_.compiles),
                         static_cast<unsigned long long>(jitStats_.rejected),
                         static_cast<unsigned long long>(jitStats_.regionBlocks),
                         static_cast<unsigned long long>(jitStats_.calls),
                         static_cast<unsigned long long>(jitStats_.noRegion),
                         static_cast<unsigned long long>(jitStats_.declineGroup),
                         static_cast<unsigned long long>(jitStats_.declineLoop),
                         static_cast<unsigned long long>(jitStats_.declineBudget));
    }
#endif
    res.pc = state_.pc;
    res.executed = executed;
    return res;
}

// Exec runs block `b` and the blocks that follow it as long as they are in the lookup cache, the budget allows a whole
// block and no loop end lies inside one. Dispatch: computed goto (one indirect jump per handler, so the host's branch
// predictor sees the guest's instruction sequence) on GCC / Clang, a switch elsewhere. The handlers are written once
// with CASE / NEXT / TAKEN / STOP. Every block ends with the BlockEnd pseudo-op, so the handlers never test for the end.
// Blocks touching more register groups than are free run through the window-check stub first.

#if defined(__GNUC__) || defined(__clang__)
#define UNREAL_XTENSA_THREADED 1
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic" // labels as values
#else
#define UNREAL_XTENSA_THREADED 0
#endif

Cpu::ExecOut Cpu::Exec(Block* b, uint64_t maxInstructions, uint64_t& executed)
{
    ExecOut out;
    CpuState& st = state_;
    uint32_t* const ar = st.ar;
    uint32_t wb4 = 0;
    const Insn* begin = nullptr;
    const Insn* ip = nullptr;
    uint32_t nextPc = 0;
    uint32_t v = 0;

#if UNREAL_XTENSA_THREADED
#define XTENSA_LABEL(name, mnemonic, format) &&L_##name,
#define XTENSA_CHECK_LABEL(name, mnemonic, format) &&L_WindowCheck,
    static const void* const kHandlers[] = {XTENSA_OPS(XTENSA_LABEL)};
    static const void* const kCheckHandlers[] = {XTENSA_OPS(XTENSA_CHECK_LABEL)};
#undef XTENSA_LABEL
#undef XTENSA_CHECK_LABEL
    const void* const* table = kHandlers;
#define CASE(name) L_##name:
    // The empty asm with a unique operand keeps every handler's indirect jump its own: without it the compilers merge
    // the identical dispatch tails into one jump and the host's predictor loses the guest's sequence (-40 % speed).
#define DISPATCH() \
    do \
    { \
        const void* target_ = table[static_cast<size_t>(ip->op)]; \
        __asm__ volatile("" : "+r"(target_) : "i"(__COUNTER__)); \
        goto* target_; \
    } while (0)
#else
    bool checkMode = false;
#define CASE(name) case Op::name:
#define DISPATCH() goto dispatch
#endif
#define NEXT \
    do \
    { \
        ++ip; \
        DISPATCH(); \
    } while (0)

#define AR(n) ar[(wb4 + (n)) & 63]
#define RA AR(ip->a)
#define RB AR(ip->b)
#define RC AR(ip->c)
#define FA st.fr[ip->a]
#define FB st.fr[ip->b]
#define FC st.fr[ip->c]
#define BIT(n) ((st.br >> (n)) & 1u)
#define STOP(reason) \
    do \
    { \
        out.stop = (reason); \
        goto stopped; \
    } while (0)
#define FAULT(addr) \
    do \
    { \
        out.faultAddress = (addr); \
        STOP(StopReason::MemoryFault); \
    } while (0)
#define LOAD(addr, size, dst) \
    do \
    { \
        const uint32_t ea_ = (addr); \
        if (!Load(ea_, (size), (dst))) \
            FAULT(ea_); \
    } while (0)
#define STORE(addr, size, val) \
    do \
    { \
        const uint32_t ea_ = (addr); \
        if (!Store(ea_, (size), (val))) \
            FAULT(ea_); \
        if (codeModified_) \
        { \
            codeModified_ = false; \
            ++ip; \
            nextPc = ip[-1].pc + ip[-1].len; \
            goto modified; \
        } \
    } while (0)
#define TAKEN(target) \
    do \
    { \
        nextPc = static_cast<uint32_t>(target); \
        goto taken; \
    } while (0)
#define BRANCH(cond) \
    do \
    { \
        if (cond) \
            TAKEN(ip->imm); \
    } while (0)

enterBlock:
{
#ifdef UNREAL_XTENSA_PROFILE
    if (profileLast_)
        profileLast_->profileInsns += executed - profileMark_;
    profileLast_ = b;
    profileMark_ = executed;
    ++b->profileEntries;
#endif
    wb4 = st.windowBase * 4;
#if UNREAL_XTENSA_JIT
    // POC 051: the compiled region headed by this block, where it holds as a whole (xtensa-jit-a64.inc)
    if (jitOn_)
    {
        if (!b->jit && !b->jitTried && ++b->hot >= jitHot_)
        {
            b->jitTried = true;
            JitCompile(*b);
        }
        if (!b->jit)
            ++jitStats_.noRegion;
        else if (b->jitGroup > safeGroups_)
            ++jitStats_.declineGroup;
        else if (!(st.lcount == 0 || st.lend < b->jitLo || st.lend > b->jitHi || (b->jitHasLoop && st.lend == b->jitLoopEnd)))
            ++jitStats_.declineLoop;
        else if (maxInstructions - executed - handlerCycles_ < b->count)
            ++jitStats_.declineBudget;
        if (b->jit && b->jitGroup <= safeGroups_ && (st.lcount == 0 || st.lend < b->jitLo || st.lend > b->jitHi || (b->jitHasLoop && st.lend == b->jitLoopEnd)))
        {
            const uint64_t rem = maxInstructions - executed - handlerCycles_;
            if (rem >= b->count)
            {
                using JitFn = uint64_t (*)(uint32_t*, uint32_t, CpuState*, const void*, uint32_t);
                const uint64_t r = reinterpret_cast<JitFn>(b->jit)(ar, wb4, &st, l1_,
                                                                   rem > 0x7FFFFFFFu ? 0x7FFFFFFFu : static_cast<uint32_t>(rem));
                const uint32_t ran = static_cast<uint32_t>(r);
                ++jitStats_.calls;
                if (ran != 0) // else nothing ran (its first instruction left at once): the interpreter runs the block
                {
                    jitStats_.insns += ran;
                    executed += ran;
                    nextPc = static_cast<uint32_t>(r >> 32);
                    goto chain;
                }
            }
        }
    }
#endif
    uint32_t count = b->count;
    bool cut = false;
    // a loop end inside the block (built under another LEND) must see the loop-back check
    if (st.lcount != 0 && st.lend - b->start - 1 < b->end - b->start - 1)
    {
        for (uint32_t i = 1; i < count; ++i)
            if (b->insns[i].pc == st.lend)
            {
                count = i;
                cut = true;
                break;
            }
    }
    const uint64_t remaining = maxInstructions - executed - handlerCycles_;
    if (count > remaining)
    {
        count = static_cast<uint32_t>(remaining);
        cut = true;
    }
    if (cut)
    {
        std::memcpy(partial_, b->insns.data(), count * sizeof(Insn));
        partial_[count] = Insn{};
        partial_[count].op = Op::BlockEnd;
        partial_[count].pc = b->insns[count].pc;
        begin = partial_;
    }
    else
        begin = b->insns.data();
    ip = begin;
#if UNREAL_XTENSA_THREADED
    table = b->maxGroup > safeGroups_ ? kCheckHandlers : kHandlers;
#else
    checkMode = b->maxGroup > safeGroups_;
#endif
    DISPATCH();
}

#if UNREAL_XTENSA_THREADED
L_WindowCheck:
    if (ip->maxGroup > safeGroups_ && !WindowCheck(ip->maxGroup, out.faultAddress))
        STOP(StopReason::MemoryFault);
    goto* kHandlers[static_cast<size_t>(ip->op)];
#else
dispatch:
    if (checkMode && ip->maxGroup > safeGroups_ && !WindowCheck(ip->maxGroup, out.faultAddress))
        STOP(StopReason::MemoryFault);
    switch (ip->op)
    {
#endif
            // ---- ALU ----
            CASE(Add)
            CASE(AddN) RA = RB + RC; NEXT;
            CASE(Addx2) RA = (RB << 1) + RC; NEXT;
            CASE(Addx4) RA = (RB << 2) + RC; NEXT;
            CASE(Addx8) RA = (RB << 3) + RC; NEXT;
            CASE(Sub) RA = RB - RC; NEXT;
            CASE(Subx2) RA = (RB << 1) - RC; NEXT;
            CASE(Subx4) RA = (RB << 2) - RC; NEXT;
            CASE(Subx8) RA = (RB << 3) - RC; NEXT;
            CASE(And) RA = RB & RC; NEXT;
            CASE(Or) RA = RB | RC; NEXT;
            CASE(Xor) RA = RB ^ RC; NEXT;
            CASE(Neg) RA = 0u - RC; NEXT;
            CASE(Abs)
                v = RC;
                RA = (v & 0x80000000u) ? 0u - v : v;
                NEXT;
            CASE(Addi)
            CASE(AddiN)
            CASE(Addmi) RA = RB + static_cast<uint32_t>(ip->imm); NEXT;
            CASE(Movi)
            CASE(MoviN) RA = static_cast<uint32_t>(ip->imm); NEXT;
            CASE(MovN) RA = RB; NEXT;
            CASE(Salt) RA = static_cast<int32_t>(RB) < static_cast<int32_t>(RC) ? 1u : 0u; NEXT;
            CASE(Saltu) RA = RB < RC ? 1u : 0u; NEXT;
            CASE(Min) RA = static_cast<int32_t>(RB) < static_cast<int32_t>(RC) ? RB : RC; NEXT;
            CASE(Max) RA = static_cast<int32_t>(RB) < static_cast<int32_t>(RC) ? RC : RB; NEXT;
            CASE(Minu) RA = RB < RC ? RB : RC; NEXT;
            CASE(Maxu) RA = RB < RC ? RC : RB; NEXT;
            CASE(Moveqz)
                if (RC == 0)
                    RA = RB;
                NEXT;
            CASE(Movnez)
                if (RC != 0)
                    RA = RB;
                NEXT;
            CASE(Movltz)
                if (static_cast<int32_t>(RC) < 0)
                    RA = RB;
                NEXT;
            CASE(Movgez)
                if (static_cast<int32_t>(RC) >= 0)
                    RA = RB;
                NEXT;
            CASE(Movf)
                if (!BIT(ip->c))
                    RA = RB;
                NEXT;
            CASE(Movt)
                if (BIT(ip->c))
                    RA = RB;
                NEXT;
            CASE(Sext)
            {
                const uint32_t sh = 31u - static_cast<uint32_t>(ip->imm);
                RA = static_cast<uint32_t>(static_cast<int32_t>(RB << sh) >> sh);
                NEXT;
            }
            CASE(Clamps)
            {
                const int32_t x = static_cast<int32_t>(RB);
                const int32_t hi = static_cast<int32_t>((1u << ip->imm) - 1u);
                const int32_t lo = -hi - 1;
                RA = static_cast<uint32_t>(x < lo ? lo : (x > hi ? hi : x));
                NEXT;
            }
            CASE(Nsa) RA = Nsa(RB); NEXT;
            CASE(Nsau) RA = Nsau(RB); NEXT;
            CASE(Mul16u) RA = (RB & 0xFFFFu) * (RC & 0xFFFFu); NEXT;
            CASE(Mul16s)
                RA = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(RB)) *
                                           static_cast<int32_t>(static_cast<int16_t>(RC)));
                NEXT;
            CASE(Mull) RA = RB * RC; NEXT;
            CASE(Muluh) RA = static_cast<uint32_t>((static_cast<uint64_t>(RB) * RC) >> 32); NEXT;
            CASE(Mulsh)
                RA = static_cast<uint32_t>(static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(RB)) *
                                                                 static_cast<int32_t>(RC)) >>
                                           32);
                NEXT;
            CASE(Quou)
                if (RC == 0)
                    STOP(StopReason::DivideByZero);
                RA = RB / RC;
                NEXT;
            CASE(Remu)
                if (RC == 0)
                    STOP(StopReason::DivideByZero);
                RA = RB % RC;
                NEXT;
            CASE(Quos)
            {
                const int32_t d = static_cast<int32_t>(RC);
                const int32_t n = static_cast<int32_t>(RB);
                if (d == 0)
                    STOP(StopReason::DivideByZero);
                RA = (d == -1) ? 0u - static_cast<uint32_t>(n) : static_cast<uint32_t>(n / d);
                NEXT;
            }
            CASE(Rems)
            {
                const int32_t d = static_cast<int32_t>(RC);
                const int32_t n = static_cast<int32_t>(RB);
                if (d == 0)
                    STOP(StopReason::DivideByZero);
                RA = (d == -1) ? 0u : static_cast<uint32_t>(n % d);
                NEXT;
            }

            // ---- shifts ----
            CASE(Slli) RA = static_cast<uint32_t>(static_cast<uint64_t>(RB) << ip->imm); NEXT;
            CASE(Srai) RA = static_cast<uint32_t>(static_cast<int32_t>(RB) >> ip->imm); NEXT;
            CASE(Srli) RA = RB >> ip->imm; NEXT;
            CASE(Src)
                RA = static_cast<uint32_t>((((static_cast<uint64_t>(RB)) << 32) | RC) >> (st.sar & 63));
                NEXT;
            CASE(Srl) RA = static_cast<uint32_t>(static_cast<uint64_t>(RC) >> (st.sar & 63)); NEXT;
            CASE(Sll) RA = static_cast<uint32_t>((static_cast<uint64_t>(RB) << 32) >> (st.sar & 63)); NEXT;
            CASE(Sra)
                RA = static_cast<uint32_t>(static_cast<int64_t>(static_cast<int32_t>(RC)) >> (st.sar & 63));
                NEXT;
            CASE(Ssr) st.sar = RB & 31; NEXT;
            CASE(Ssl) st.sar = 32 - (RB & 31); NEXT;
            CASE(Ssa8l) st.sar = (RB & 3) * 8; NEXT;
            CASE(Ssa8b) st.sar = 32 - (RB & 3) * 8; NEXT;
            CASE(Ssai) st.sar = static_cast<uint32_t>(ip->imm); NEXT;
            CASE(Extui)
                RA = (RB >> ip->c) & static_cast<uint32_t>((1ull << ip->imm) - 1);
                NEXT;

            // ---- booleans ----
            CASE(Andb) st.br = (st.br & ~(1u << ip->a)) | ((BIT(ip->b) & BIT(ip->c)) << ip->a); NEXT;
            CASE(Andbc) st.br = (st.br & ~(1u << ip->a)) | ((BIT(ip->b) & (BIT(ip->c) ^ 1u)) << ip->a); NEXT;
            CASE(Orb) st.br = (st.br & ~(1u << ip->a)) | ((BIT(ip->b) | BIT(ip->c)) << ip->a); NEXT;
            CASE(Orbc) st.br = (st.br & ~(1u << ip->a)) | ((BIT(ip->b) | (BIT(ip->c) ^ 1u)) << ip->a); NEXT;
            CASE(Xorb) st.br = (st.br & ~(1u << ip->a)) | ((BIT(ip->b) ^ BIT(ip->c)) << ip->a); NEXT;
            CASE(Any4) st.br = (st.br & ~(1u << ip->a)) | ((((st.br >> ip->b) & 0xFu) != 0 ? 1u : 0u) << ip->a); NEXT;
            CASE(All4) st.br = (st.br & ~(1u << ip->a)) | ((((st.br >> ip->b) & 0xFu) == 0xFu ? 1u : 0u) << ip->a); NEXT;
            CASE(Any8) st.br = (st.br & ~(1u << ip->a)) | ((((st.br >> ip->b) & 0xFFu) != 0 ? 1u : 0u) << ip->a); NEXT;
            CASE(All8)
                st.br = (st.br & ~(1u << ip->a)) | ((((st.br >> ip->b) & 0xFFu) == 0xFFu ? 1u : 0u) << ip->a);
                NEXT;

            // ---- loads / stores ----
            CASE(L8ui) LOAD(RB + static_cast<uint32_t>(ip->imm), 1, v); RA = v; NEXT;
            CASE(L16ui) LOAD(RB + static_cast<uint32_t>(ip->imm), 2, v); RA = v; NEXT;
            CASE(L16si)
                LOAD(RB + static_cast<uint32_t>(ip->imm), 2, v);
                RA = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(v)));
                NEXT;
            CASE(L32i)
            CASE(L32iN)
            CASE(L32ai)
            CASE(L32e) LOAD(RB + static_cast<uint32_t>(ip->imm), 4, v); RA = v; NEXT;
            CASE(L32r) LOAD(static_cast<uint32_t>(ip->imm), 4, v); RA = v; NEXT;
            CASE(S8i) STORE(RB + static_cast<uint32_t>(ip->imm), 1, RA); NEXT;
            CASE(S16i) STORE(RB + static_cast<uint32_t>(ip->imm), 2, RA); NEXT;
            CASE(S32i)
            CASE(S32iN)
            CASE(S32ri)
            CASE(S32nb)
            CASE(S32e) STORE(RB + static_cast<uint32_t>(ip->imm), 4, RA); NEXT;
            CASE(S32c1i)
            {
                const uint32_t ea = RB + static_cast<uint32_t>(ip->imm);
                if (ea & 3)
                    FAULT(ea);
                LOAD(ea, 4, v);
                const uint32_t old = v;
                if (old == st.scompare1 && !Store(ea, 4, RA))
                    FAULT(ea);
                RA = old;
                if (codeModified_)
                {
                    codeModified_ = false;
                    nextPc = ip->pc + ip->len;
                    ++ip;
                    goto modified;
                }
                NEXT;
            }

            // ---- FPU loads / stores ----
            CASE(Lsi) LOAD(RB + static_cast<uint32_t>(ip->imm), 4, v); FA = v; NEXT;
            CASE(Lsx) LOAD(RB + RC, 4, v); FA = v; NEXT;
            CASE(Lsip) LOAD(RB, 4, v); FA = v; RB += static_cast<uint32_t>(ip->imm); NEXT;
            CASE(Lsxp) LOAD(RB, 4, v); FA = v; RB += RC; NEXT;
            CASE(Ssi) STORE(RB + static_cast<uint32_t>(ip->imm), 4, FA); NEXT;
            CASE(Ssx) STORE(RB + RC, 4, FA); NEXT;
            CASE(Ssip)
            {
                const uint32_t ea = RB;
                RB = ea + static_cast<uint32_t>(ip->imm); // after a successful store (STORE may leave the block)
                if (!Store(ea, 4, FA))
                {
                    RB = ea;
                    FAULT(ea);
                }
                if (codeModified_)
                {
                    codeModified_ = false;
                    nextPc = ip->pc + ip->len;
                    ++ip;
                    goto modified;
                }
                NEXT;
            }
            CASE(Ssxp)
            {
                const uint32_t ea = RB;
                const uint32_t inc = RC;
                if (!Store(ea, 4, FA))
                    FAULT(ea);
                RB = ea + inc;
                if (codeModified_)
                {
                    codeModified_ = false;
                    nextPc = ip->pc + ip->len;
                    ++ip;
                    goto modified;
                }
                NEXT;
            }

            // ---- FPU ----
            CASE(AddS) FA = Result(ToF(FB) + ToF(FC)); NEXT;
            CASE(SubS) FA = Result(ToF(FB) - ToF(FC)); NEXT;
            CASE(MulS) FA = Result(ToF(FB) * ToF(FC)); NEXT;
            CASE(MaddS)
            CASE(MaddnS) FA = Result(FusedMulAdd(ToF(FB), ToF(FC), ToF(FA))); NEXT;
            CASE(MsubS) FA = Result(FusedMulAdd(-ToF(FB), ToF(FC), ToF(FA))); NEXT;
            CASE(RoundS) RA = FloatToInt(FB, ip->imm, Rounding::Nearest); NEXT;
            CASE(TruncS) RA = FloatToInt(FB, ip->imm, Rounding::Zero); NEXT;
            CASE(FloorS) RA = FloatToInt(FB, ip->imm, Rounding::Down); NEXT;
            CASE(CeilS) RA = FloatToInt(FB, ip->imm, Rounding::Up); NEXT;
            CASE(UtruncS) RA = FloatToUint(FB, ip->imm); NEXT;
            CASE(FloatS) FA = ToB(static_cast<float>(static_cast<int32_t>(RB)) * kInvPow2[ip->imm & 15]); NEXT;
            CASE(UfloatS) FA = ToB(static_cast<float>(RB) * kInvPow2[ip->imm & 15]); NEXT;
            CASE(MovS) FA = FB; NEXT;
            CASE(AbsS) FA = FB & 0x7FFFFFFFu; NEXT;
            CASE(NegS) FA = FB ^ 0x80000000u; NEXT;
            CASE(Rfr) RA = FB; NEXT;
            CASE(Wfr) FA = RB; NEXT;
            CASE(UnS)
            CASE(OeqS)
            CASE(UeqS)
            CASE(OltS)
            CASE(UltS)
            CASE(OleS)
            CASE(UleS)
            {
                const float x = ToF(FB), y = ToF(FC);
                const bool un = IsNan(FB) || IsNan(FC);
                bool r = false;
                switch (ip->op)
                {
                    case Op::UnS: r = un; break;
                    case Op::OeqS: r = !un && x == y; break;
                    case Op::UeqS: r = un || x == y; break;
                    case Op::OltS: r = !un && x < y; break;
                    case Op::UltS: r = un || x < y; break;
                    case Op::OleS: r = !un && x <= y; break;
                    default: r = un || x <= y; break;
                }
                st.br = (st.br & ~(1u << ip->a)) | ((r ? 1u : 0u) << ip->a);
                NEXT;
            }
            CASE(MoveqzS)
                if (RC == 0)
                    FA = FB;
                NEXT;
            CASE(MovnezS)
                if (RC != 0)
                    FA = FB;
                NEXT;
            CASE(MovltzS)
                if (static_cast<int32_t>(RC) < 0)
                    FA = FB;
                NEXT;
            CASE(MovgezS)
                if (static_cast<int32_t>(RC) >= 0)
                    FA = FB;
                NEXT;
            CASE(MovfS)
                if (!BIT(ip->c))
                    FA = FB;
                NEXT;
            CASE(MovtS)
                if (BIT(ip->c))
                    FA = FB;
                NEXT;

            // ---- special / user registers ----
            CASE(Rsr)
                switch (ip->c)
                {
                    case SrLbeg: v = st.lbeg; break;
                    case SrLend: v = st.lend; break;
                    case SrLcount: v = st.lcount; break;
                    case SrSar: v = st.sar; break;
                    case SrBr: v = st.br; break;
                    case SrScompare1: v = st.scompare1; break;
                    case SrAtomctl: v = st.atomctl; break;
                    case SrWindowBase: v = st.windowBase; break;
                    case SrWindowStart: v = st.windowStart; break;
                    case SrPs: v = st.ps; break;
                    case SrCpEnable: v = st.cpEnable; break;
                    case 235: v = st.prid; break; // PRID
                    case SrCcount: v = static_cast<uint32_t>(st.ccount + executed + static_cast<uint64_t>(ip - begin) + handlerCycles_); break;
                    case 16: case 17: case 32: case 33: case 34: case 35: STOP(StopReason::Unimplemented); // MAC16
                    default: STOP(StopReason::Privileged);
                }
                RA = v;
                NEXT;
            CASE(Wsr)
            CASE(Xsr)
            {
                const uint32_t nv = RA;
                switch (ip->c)
                {
                    case SrLbeg: v = st.lbeg; st.lbeg = nv; break;
                    case SrLend: v = st.lend; st.lend = nv; break;
                    case SrLcount: v = st.lcount; st.lcount = nv; break;
                    case SrSar: v = st.sar; st.sar = nv & 63; break;
                    case SrBr: v = st.br; st.br = nv & 0xFFFFu; break;
                    case SrScompare1: v = st.scompare1; st.scompare1 = nv; break;
                    case SrAtomctl: v = st.atomctl; st.atomctl = nv & 0x3F; break;
                    case SrWindowBase: v = st.windowBase; st.windowBase = nv & 15; break;
                    case SrWindowStart: v = st.windowStart; st.windowStart = nv & 0xFFFFu; break;
                    case SrPs: v = st.ps; st.ps = nv; break;
                    case SrCpEnable: v = st.cpEnable; st.cpEnable = nv & 0xFF; break;
                    case SrCcount:
                        v = static_cast<uint32_t>(st.ccount + executed + static_cast<uint64_t>(ip - begin) + handlerCycles_);
                        st.ccount = static_cast<uint64_t>(nv) - executed - static_cast<uint64_t>(ip - begin) - handlerCycles_; // the WSR reads nv
                        break;
                    case 16: case 17: case 32: case 33: case 34: case 35: STOP(StopReason::Unimplemented);
                    default: STOP(StopReason::Privileged);
                }
                if (ip->op == Op::Xsr)
                    RA = v;
                UpdateSafeGroups();
                NEXT;
            }
            CASE(Rur)
                switch (ip->c)
                {
                    case UrThreadPtr: RA = st.threadPtr; break;
                    case UrFcr: RA = st.fcr; break;
                    case UrFsr: RA = st.fsr; break;
                    default: STOP(StopReason::Unimplemented);
                }
                NEXT;
            CASE(Wur)
                switch (ip->c)
                {
                    case UrThreadPtr: st.threadPtr = RA; break;
                    case UrFcr: st.fcr = RA & 0xFFFFF07Fu; break;
                    case UrFsr: st.fsr = RA & 0x00000F80u; break;
                    default: STOP(StopReason::Unimplemented);
                }
                NEXT;

            CASE(Isync)
            CASE(Rsync)
            CASE(Esync)
            CASE(Dsync)
            CASE(Excw)
            CASE(Memw)
            CASE(Extw)
            CASE(Nop)
            CASE(NopN) NEXT;

            // ---- branches ----
            CASE(Beqz)
            CASE(BeqzN) BRANCH(RB == 0); NEXT;
            CASE(Bnez)
            CASE(BnezN) BRANCH(RB != 0); NEXT;
            CASE(Bltz) BRANCH(static_cast<int32_t>(RB) < 0); NEXT;
            CASE(Bgez) BRANCH(static_cast<int32_t>(RB) >= 0); NEXT;
            CASE(Beqi) BRANCH(static_cast<int32_t>(RB) == kB4Const[ip->c]); NEXT;
            CASE(Bnei) BRANCH(static_cast<int32_t>(RB) != kB4Const[ip->c]); NEXT;
            CASE(Blti) BRANCH(static_cast<int32_t>(RB) < kB4Const[ip->c]); NEXT;
            CASE(Bgei) BRANCH(static_cast<int32_t>(RB) >= kB4Const[ip->c]); NEXT;
            CASE(Bltui) BRANCH(RB < kB4ConstU[ip->c]); NEXT;
            CASE(Bgeui) BRANCH(RB >= kB4ConstU[ip->c]); NEXT;
            CASE(Bnone) BRANCH((RB & RC) == 0); NEXT;
            CASE(Beq) BRANCH(RB == RC); NEXT;
            CASE(Blt) BRANCH(static_cast<int32_t>(RB) < static_cast<int32_t>(RC)); NEXT;
            CASE(Bltu) BRANCH(RB < RC); NEXT;
            CASE(Ball) BRANCH((~RB & RC) == 0); NEXT;
            CASE(Bbc) BRANCH(((RB >> (RC & 31)) & 1) == 0); NEXT;
            CASE(Bbci) BRANCH(((RB >> ip->c) & 1) == 0); NEXT;
            CASE(Bany) BRANCH((RB & RC) != 0); NEXT;
            CASE(Bne) BRANCH(RB != RC); NEXT;
            CASE(Bge) BRANCH(static_cast<int32_t>(RB) >= static_cast<int32_t>(RC)); NEXT;
            CASE(Bgeu) BRANCH(RB >= RC); NEXT;
            CASE(Bnall) BRANCH((~RB & RC) != 0); NEXT;
            CASE(Bbs) BRANCH(((RB >> (RC & 31)) & 1) != 0); NEXT;
            CASE(Bbsi) BRANCH(((RB >> ip->c) & 1) != 0); NEXT;
            CASE(Bf) BRANCH(!BIT(ip->b)); NEXT;
            CASE(Bt) BRANCH(BIT(ip->b)); NEXT;

            // ---- loops ----
            CASE(Loop)
            CASE(Loopnez)
            CASE(Loopgtz)
                v = RB;
                st.lcount = v - 1;
                st.lbeg = ip->pc + 3;
                st.lend = static_cast<uint32_t>(ip->imm);
                if ((ip->op == Op::Loopnez && v == 0) || (ip->op == Op::Loopgtz && static_cast<int32_t>(v) <= 0))
                    TAKEN(ip->imm);
                NEXT;

            // ---- calls, jumps, returns ----
            CASE(J) TAKEN(ip->imm);
            CASE(Jx) TAKEN(RB);
            CASE(Call0)
                AR(0) = ip->pc + 3;
                st.callKind = 0;
                TAKEN(ip->imm);
            CASE(Callx0)
                v = RB;
                AR(0) = ip->pc + 3;
                st.callKind = 0;
                TAKEN(v);
            CASE(Call4)
            CASE(Call8)
            CASE(Call12)
            CASE(Callx4)
            CASE(Callx8)
            CASE(Callx12)
            {
                uint32_t n, target;
                switch (ip->op)
                {
                    case Op::Call4: n = 1; target = static_cast<uint32_t>(ip->imm); break;
                    case Op::Call8: n = 2; target = static_cast<uint32_t>(ip->imm); break;
                    case Op::Call12: n = 3; target = static_cast<uint32_t>(ip->imm); break;
                    case Op::Callx4: n = 1; target = RB; break;
                    case Op::Callx8: n = 2; target = RB; break;
                    default: n = 3; target = RB; break;
                }
                AR(n * 4) = (n << 30) | ((ip->pc + 3) & 0x3FFFFFFFu);
                st.ps = (st.ps & ~0x30000u) | (n << 16);
                st.callKind = n;
                TAKEN(target);
            }
            CASE(Ret)
            CASE(RetN) TAKEN(AR(0));
            CASE(Retw)
            CASE(RetwN)
            {
                const uint32_t a0 = AR(0);
                const uint32_t n = a0 >> 30;
                const uint32_t c = st.windowBase;
                const uint32_t rot = Rotr16(st.windowStart, c); // bit k = WS[c + k]; WS[c - k] = bit 16 - k
                const uint32_t mm = ((rot >> 15) & 1) ? 1u : ((rot >> 14) & 1) ? 2u : ((rot >> 13) & 1) ? 3u : 0u;
                if (n == 0 || (mm != 0 && mm != n))
                    STOP(StopReason::IllegalInstruction);
                const uint32_t m = (c - n) & 15;
                if (!((st.windowStart >> m) & 1))
                {
                    if (!FillFrame(m, n, out.faultAddress))
                        STOP(StopReason::MemoryFault);
                }
                st.windowStart &= ~(1u << c);
                st.windowBase = m;
                UpdateSafeGroups();
                TAKEN((ip->pc & 0xC0000000u) | (a0 & 0x3FFFFFFFu));
            }
            CASE(Entry)
            {
                const uint32_t callInc = (st.ps >> 16) & 3;
                if (ip->b > 3)
                    STOP(StopReason::IllegalInstruction);
                if (callInc && !WindowCheck(static_cast<int>(callInc), out.faultAddress))
                    STOP(StopReason::MemoryFault);
                const uint32_t sp = AR(ip->b) - static_cast<uint32_t>(ip->imm);
                AR(callInc * 4 + ip->b) = sp;
                st.windowBase = (st.windowBase + callInc) & 15;
                st.windowStart |= 1u << st.windowBase;
                UpdateSafeGroups();
                NEXT; // an ender: the block-end pseudo-op follows
            }
            CASE(Movsp)
            {
                const uint32_t c = st.windowBase;
                const uint32_t rot = Rotr16(st.windowStart, c);
                if (!(rot & 0xE000u))
                {
                    // Alloca exception: the caller's frame is spilled; the handler underflows it back.
                    uint32_t n = AR(0) >> 30;
                    n = (n == 0) ? 1 : n;
                    if (!FillFrame((c - n) & 15, n, out.faultAddress))
                        STOP(StopReason::MemoryFault);
                    handlerCycles_ += kAllocaCycles;
                }
                RA = RB;
                NEXT;
            }
            CASE(Rotw)
                st.windowBase = (st.windowBase + static_cast<uint32_t>(ip->imm)) & 15;
                UpdateSafeGroups();
                NEXT; // an ender: the block-end pseudo-op follows

            // ---- stops ----
            CASE(Syscall)
                if (AR(2) != 0)
                    STOP(StopReason::Syscall);
                if (!SpillAll(out.faultAddress))
                    STOP(StopReason::MemoryFault);
                NEXT;
            CASE(Simcall) STOP(StopReason::Syscall);
            CASE(Break)
            CASE(BreakN) STOP(StopReason::Break);
            CASE(Ill)
            CASE(IllN)
            CASE(Illegal) STOP(StopReason::IllegalInstruction);
            CASE(Rsil)
            CASE(Waiti)
            CASE(Rfe)
            CASE(Rfue)
            CASE(Rfde)
            CASE(Rfwo)
            CASE(Rfwu)
            CASE(Rfi)
            CASE(Rer)
            CASE(Wer) STOP(StopReason::Privileged);
            CASE(DivnS)
            CASE(ConstS)
            CASE(Div0S)
            CASE(Recip0S)
            CASE(Sqrt0S)
            CASE(Rsqrt0S)
            CASE(Nexp01S)
            CASE(MksadjS)
            CASE(MkdadjS)
            CASE(AddexpS)
            CASE(AddexpmS)
            CASE(Mac16)
            CASE(EeInsn) STOP(StopReason::Unimplemented);
            CASE(BlockEnd) goto blockEnd;
#if !UNREAL_XTENSA_THREADED
            default: STOP(StopReason::Unimplemented);
#endif
#if !UNREAL_XTENSA_THREADED
    }
#endif

blockEnd: // sequential end of the block: loop-back check
    nextPc = ip->pc;
    if (nextPc == st.lend && st.lcount != 0)
    {
        st.lcount--;
        nextPc = st.lbeg;
    }
    executed += static_cast<uint64_t>(ip - begin);
    goto chain;

taken:
    executed += static_cast<uint64_t>(ip - begin) + 1;
    goto chain;

chain:
    st.pc = nextPc;
    if (executed + handlerCycles_ >= maxInstructions)
        return out;
    b = lookup_[(nextPc >> 1) & (kLookupSize - 1)];
    if (!b || b->start != nextPc)
        return out; // Run looks it up / builds it (and checks traps: no block is ever built at a trap address)
    goto enterBlock;

modified: // a store hit decoded code: leave so Run frees the retired blocks
    if (nextPc == st.lend && st.lcount != 0)
    {
        st.lcount--;
        nextPc = st.lbeg;
    }
    st.pc = nextPc;
    executed += static_cast<uint64_t>(ip - begin);
    return out;

stopped:
    st.pc = ip->pc;
    executed += static_cast<uint64_t>(ip - begin);
    return out;

#undef CASE
#undef DISPATCH
#undef NEXT
#undef AR
#undef RA
#undef RB
#undef RC
#undef FA
#undef FB
#undef FC
#undef BIT
#undef STOP
#undef FAULT
#undef LOAD
#undef STORE
#undef TAKEN
#undef BRANCH
}

#if UNREAL_XTENSA_THREADED
#pragma GCC diagnostic pop
#endif

} // namespace xtensa

#include "xtensa-jit-a64.inc"
