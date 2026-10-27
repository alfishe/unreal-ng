/// POC 038 (../README.md): the cost of a condition on the hit path (fast predicates, an RPN VM) and of a script
/// callback per hit (Lua in the calling thread's own state, with an instruction budget, a round trip to a worker, an
/// async queue; Python with the GIL free or held by a busy thread). Prints one line per measurement.
///
///   poc038 [rounds]     rounds: interleaved repetitions of the ns-scale measurements (default 7)

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

extern "C"
{
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#ifdef POC038_PYTHON
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#endif

namespace
{
using Clock = std::chrono::steady_clock;

double NsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::nano>(Clock::now() - start).count();
}

/// region <Harness>

/// ns per call of @p f over @p calls, the lower quartile and the median of @p rounds batches
struct Batched
{
    double q1 = 0, median = 0;
};

template <class F>
Batched MeasureBatched(F&& f, size_t calls, int rounds)
{
    std::vector<double> per;
    for (int round = 0; round < rounds; round++)
    {
        const auto start = Clock::now();
        f(calls);
        per.push_back(NsSince(start) / static_cast<double>(calls));
    }
    std::sort(per.begin(), per.end());
    return {per[per.size() / 4], per[per.size() / 2]};
}

/// Per-call distribution in ns (each call timed alone: for µs-scale work, the clock's resolution is ~42 ns here)
struct Distribution
{
    double p50 = 0, p99 = 0, max = 0, mean = 0;
};

template <class F>
Distribution MeasurePerCall(F&& f, size_t calls)
{
    std::vector<double> ns;
    ns.reserve(calls);
    for (size_t i = 0; i < calls; i++)
    {
        const auto start = Clock::now();
        f(i);
        ns.push_back(NsSince(start));
    }
    double sum = 0;
    for (double v : ns)
        sum += v;
    std::sort(ns.begin(), ns.end());
    return {ns[ns.size() / 2], ns[std::min(ns.size() - 1, ns.size() * 99 / 100)], ns.back(), sum / ns.size()};
}

void Print(const char* name, const Batched& b)
{
    std::printf("%-58s q1 %8.2f ns   median %8.2f ns\n", name, b.q1, b.median);
}

void Print(const char* name, const Distribution& d)
{
    std::printf("%-58s p50 %9.0f ns  p99 %9.0f ns  max %10.0f ns  mean %9.0f ns\n", name, d.p50, d.p99, d.max, d.mean);
}

/// endregion </Harness>

/// region <G1: conditions>

/// The registers as the Z80 keeps them (little-endian pairs: A is the high byte of AF)
struct Cpu
{
    uint16_t af, bc, de, hl, ix, iy, sp, pc;
    uint8_t pg[4];  ///< the RAM page in each slot (PG0-PG3)
};
constexpr uint8_t kA = offsetof(Cpu, af) + 1, kF = offsetof(Cpu, af), kB = offsetof(Cpu, bc) + 1;
constexpr uint8_t kHL = offsetof(Cpu, hl), kIX = offsetof(Cpu, ix), kSP = offsetof(Cpu, sp);
constexpr uint8_t kPG3 = offsetof(Cpu, pg) + 3;

struct Ctx
{
    const Cpu* cpu;
    const uint8_t* mem;
    uint16_t addr;  ///< the access (MWA / MRA)
    uint8_t value;  ///< MWV / MRV
};

inline uint8_t R8(const Ctx& x, uint8_t off)
{
    return reinterpret_cast<const uint8_t*>(x.cpu)[off];
}
inline uint16_t R16(const Ctx& x, uint8_t off)
{
    uint16_t v;
    std::memcpy(&v, reinterpret_cast<const uint8_t*>(x.cpu) + off, 2);
    return v;
}

enum class Cmp : uint8_t { Eq, Ne, Lt, Gt, Le, Ge };
inline bool Compare(Cmp cmp, int64_t a, int64_t b)
{
    switch (cmp)
    {
        case Cmp::Eq: return a == b;
        case Cmp::Ne: return a != b;
        case Cmp::Lt: return a < b;
        case Cmp::Gt: return a > b;
        case Cmp::Le: return a <= b;
        case Cmp::Ge: return a >= b;
    }
    return false;
}

/// S4: a tagged shape, one switch and a compare
struct Fast
{
    enum class Kind : uint8_t { Reg8, Reg16, Mask8, Val, Mem8 } kind;
    Cmp cmp;
    uint8_t off = 0;
    uint8_t mask = 0xFF;
    uint16_t addr = 0;
    int32_t k = 0;
};

inline bool EvalFast(const Fast& p, const Ctx& x)
{
    int64_t v = 0;
    switch (p.kind)
    {
        case Fast::Kind::Reg8: v = R8(x, p.off); break;
        case Fast::Kind::Reg16: v = R16(x, p.off); break;
        case Fast::Kind::Mask8: v = R8(x, p.off) & p.mask; break;
        case Fast::Kind::Val: v = x.value & p.mask; break;
        case Fast::Kind::Mem8: v = x.mem[p.addr]; break;
    }
    return Compare(p.cmp, v, p.k);
}

/// S2: RPN over int64 with a fixed stack; JZ / JNZ short-circuit (keep the value when jumping, pop when not)
enum class Op : uint8_t { K, Reg8, Reg16, Val, Addr, Deref8, Deref16, And, Or, Add, Eq, Ne, Lt, Gt, Le, Ge, Jz, Jnz, End };
struct BpOp
{
    Op op;
    uint8_t off = 0;
    uint16_t target = 0;
    int32_t k = 0;
};

inline int64_t RunRpn(const BpOp* code, const Ctx& x)
{
    int64_t stack[32];
    int sp = 0;
    for (size_t pc = 0;; pc++)
    {
        const BpOp& o = code[pc];
        switch (o.op)
        {
            case Op::K: stack[sp++] = o.k; break;
            case Op::Reg8: stack[sp++] = R8(x, o.off); break;
            case Op::Reg16: stack[sp++] = R16(x, o.off); break;
            case Op::Val: stack[sp++] = x.value; break;
            case Op::Addr: stack[sp++] = x.addr; break;
            case Op::Deref8: stack[sp - 1] = x.mem[static_cast<uint16_t>(stack[sp - 1])]; break;
            case Op::Deref16:
            {
                const auto a = static_cast<uint16_t>(stack[sp - 1]);
                stack[sp - 1] = x.mem[a] | (x.mem[static_cast<uint16_t>(a + 1)] << 8);
                break;
            }
#define POC_BIN(OPNAME, EXPR)              \
    case Op::OPNAME:                       \
    {                                      \
        const int64_t b = stack[--sp];     \
        const int64_t a = stack[sp - 1];   \
        stack[sp - 1] = (EXPR);            \
        break;                             \
    }
            POC_BIN(And, a & b)
            POC_BIN(Or, a | b)
            POC_BIN(Add, a + b)
            POC_BIN(Eq, a == b)
            POC_BIN(Ne, a != b)
            POC_BIN(Lt, a < b)
            POC_BIN(Gt, a > b)
            POC_BIN(Le, a <= b)
            POC_BIN(Ge, a >= b)
#undef POC_BIN
            case Op::Jz:
                if (stack[sp - 1] == 0)
                    pc = o.target - 1u;
                else
                    sp--;
                break;
            case Op::Jnz:
                if (stack[sp - 1] != 0)
                    pc = o.target - 1u;
                else
                    sp--;
                break;
            case Op::End: return sp > 0 ? stack[sp - 1] : 1;
        }
    }
}

struct Condition
{
    const char* text;
    std::vector<BpOp> rpn;
    bool hasFast = false;
    Fast fast{};
};

std::vector<Condition> Conditions()
{
    constexpr uint16_t kScore = 0x5C00;
    std::vector<Condition> all;
    all.push_back({"A==0", {{Op::Reg8, kA}, {Op::K, 0, 0, 0}, {Op::Eq}, {Op::End}}, true,
                   {Fast::Kind::Reg8, Cmp::Eq, kA, 0xFF, 0, 0}});
    all.push_back({"HL>=$4000", {{Op::Reg16, kHL}, {Op::K, 0, 0, 0x4000}, {Op::Ge}, {Op::End}}, true,
                   {Fast::Kind::Reg16, Cmp::Ge, kHL, 0xFF, 0, 0x4000}});
    all.push_back({"Z  (F & $40) != 0", {{Op::Reg8, kF}, {Op::K, 0, 0, 0x40}, {Op::And}, {Op::K, 0, 0, 0}, {Op::Ne}, {Op::End}},
                   true, {Fast::Kind::Mask8, Cmp::Ne, kF, 0x40, 0, 0}});
    all.push_back({"MWV==0", {{Op::Val}, {Op::K, 0, 0, 0}, {Op::Eq}, {Op::End}}, true,
                   {Fast::Kind::Val, Cmp::Eq, 0, 0xFF, 0, 0}});
    all.push_back({"($5C78)==13", {{Op::K, 0, 0, 0x5C78}, {Op::Deref8}, {Op::K, 0, 0, 13}, {Op::Eq}, {Op::End}}, true,
                   {Fast::Kind::Mem8, Cmp::Eq, 0, 0xFF, 0x5C78, 13}});
    // A==0 && (HL)!=$FF
    all.push_back({"A==0 && (HL)!=$FF",
                   {{Op::Reg8, kA}, {Op::K, 0, 0, 0}, {Op::Eq}, {Op::Jz, 0, 9}, {Op::Reg16, kHL}, {Op::Deref8},
                    {Op::K, 0, 0, 0xFF}, {Op::Ne}, {Op::End}, {Op::End}}});
    // (score).w >= 1000
    all.push_back({"(score).w >= 1000", {{Op::K, 0, 0, kScore}, {Op::Deref16}, {Op::K, 0, 0, 1000}, {Op::Ge}, {Op::End}}});
    // PG3==7 && MWV>=$80
    all.push_back({"PG3==7 && MWV>=$80",
                   {{Op::Reg8, kPG3}, {Op::K, 0, 0, 7}, {Op::Eq}, {Op::Jz, 0, 8}, {Op::Val}, {Op::K, 0, 0, 0x80},
                    {Op::Ge}, {Op::End}, {Op::End}}});
    // (A & $0F)==3 && (B > 5 || (IX+5)==$C9) && SP < $8000   (19 ops)
    all.push_back({"(A&$0F)==3 && (B>5 || (IX+5)==$C9) && SP<$8000",
                   {{Op::Reg8, kA}, {Op::K, 0, 0, 0x0F}, {Op::And}, {Op::K, 0, 0, 3}, {Op::Eq},  // 0-4
                    {Op::Jz, 0, 19},                                                         // 5
                    {Op::Reg8, kB}, {Op::K, 0, 0, 5}, {Op::Gt},                               // 6-8
                    {Op::Jnz, 0, 15},                                                        // 9
                    {Op::Reg16, kIX}, {Op::K, 0, 0, 5}, {Op::Add}, {Op::Deref8}, {Op::K, 0, 0, 0xC9}, {Op::Eq}, // 10-15?
                    {Op::Jz, 0, 21},                                                         // 16
                    {Op::Reg16, kSP}, {Op::K, 0, 0, 0x8000}, {Op::Lt},                        // 17-19
                    {Op::End}, {Op::End}}});
    // Fix the jump targets of the long one (indices above are approximate in the comments): recompute
    std::vector<BpOp>& c = all.back().rpn;
    // layout: 0 A,1 K,2 And,3 K,4 Eq,5 Jz->END,6 B,7 K,8 Gt,9 Jnz->M,10 IX,11 K,12 Add,13 Deref8,14 K,15 Eq,
    //         16 M: Jz->END,17 SP,18 K,19 Lt,20 END,21 END
    c[5].target = 20;
    c[9].target = 16;
    c[16].target = 20;
    return all;
}

/// States where every condition is mostly false (the real case: a tap's condition rarely holds), and random ones
std::vector<Cpu> States(bool mostlyFalse, std::vector<uint8_t>& values, std::mt19937& rng)
{
    std::vector<Cpu> states(4096);
    values.resize(states.size());
    for (size_t i = 0; i < states.size(); i++)
    {
        Cpu& s = states[i];
        auto r16 = [&] { return static_cast<uint16_t>(rng()); };
        s = Cpu{r16(), r16(), r16(), r16(), r16(), r16(), r16(), r16(), {}};
        for (uint8_t& p : s.pg)
            p = static_cast<uint8_t>(rng() & 7);
        values[i] = static_cast<uint8_t>(rng());
        if (mostlyFalse && (rng() % 100) != 0)
        {
            s.af = static_cast<uint16_t>((s.af & 0x00BF) | 0x0100);  // A != 0, Z clear, (A&F)!=3
            s.hl &= 0x3FFF;
            s.pg[3] = 0;
            values[i] |= 1;
        }
    }
    return states;
}

void MeasureConditions(int rounds)
{
    std::printf("\n## G1 conditions (ns per evaluation; 4096 states cycled)\n");
    std::mt19937 rng(38);
    std::vector<uint8_t> mem(65536);
    for (uint8_t& b : mem)
        b = static_cast<uint8_t>(rng());
    const std::vector<Condition> conditions = Conditions();
    for (bool mostlyFalse : {true, false})
    {
        std::vector<uint8_t> values;
        const std::vector<Cpu> states = States(mostlyFalse, values, rng);
        std::printf("%s\n", mostlyFalse ? "### mostly false (99 %)" : "### random states");
        uint64_t sink = 0;
        // The empty loop over the states: what the harness itself costs per evaluation
        const Batched empty = MeasureBatched(
            [&](size_t calls) {
                for (size_t i = 0; i < calls; i++)
                {
                    const Ctx x{&states[i & 4095], mem.data(), static_cast<uint16_t>(i), values[i & 4095]};
                    sink += x.value;
                }
            },
            1u << 20, rounds);
        Print("  loop only (subtract)", empty);
        for (const Condition& condition : conditions)
        {
            if (condition.hasFast)
            {
                const Batched fast = MeasureBatched(
                    [&](size_t calls) {
                        for (size_t i = 0; i < calls; i++)
                        {
                            const Ctx x{&states[i & 4095], mem.data(), static_cast<uint16_t>(i), values[i & 4095]};
                            sink += EvalFast(condition.fast, x);
                        }
                    },
                    1u << 20, rounds);
                Print((std::string("  fast  ") + condition.text).c_str(), fast);
            }
            const Batched vm = MeasureBatched(
                [&](size_t calls) {
                    for (size_t i = 0; i < calls; i++)
                    {
                        const Ctx x{&states[i & 4095], mem.data(), static_cast<uint16_t>(i), values[i & 4095]};
                        sink += static_cast<uint64_t>(RunRpn(condition.rpn.data(), x) != 0);
                    }
                },
                1u << 20, rounds);
            Print((std::string("  rpn   ") + condition.text + " (" + std::to_string(condition.rpn.size()) + " ops)").c_str(),
                  vm);
        }
        // Differential check: the fast shapes against the VM
        for (const Condition& condition : conditions)
            if (condition.hasFast)
                for (size_t i = 0; i < states.size(); i++)
                {
                    const Ctx x{&states[i], mem.data(), static_cast<uint16_t>(i), values[i]};
                    if (EvalFast(condition.fast, x) != (RunRpn(condition.rpn.data(), x) != 0))
                        std::printf("  MISMATCH %s at state %zu\n", condition.text, i);
                }
        std::printf("  (sink %llu)\n", static_cast<unsigned long long>(sink & 1));
    }
}

/// endregion </G1: conditions>

/// region <G2: Lua>

const char* const kLuaScript = R"(
count = 0
last = 0
function trivial(a, v) end
function real(a, v)
  count = count + 1
  if v == 0 then last = a end
end
function alloc(a, v)
  last = { addr = a, value = v, frame = count }
  count = count + 1
end
function runaway(a, v)
  local n = 0
  while true do n = n + 1 end
end
)";

struct Budget
{
    int64_t left = 0;
};
Budget* g_budget = nullptr;
constexpr int kHookEvery = 1000;

void BudgetHook(lua_State* L, lua_Debug*)
{
    g_budget->left -= kHookEvery;
    if (g_budget->left < 0)
        luaL_error(L, "instruction budget exceeded");
}

void CallLua(lua_State* L, int ref, lua_Integer a, lua_Integer v)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    lua_pushinteger(L, a);
    lua_pushinteger(L, v);
    if (lua_pcall(L, 2, 0, 0) != LUA_OK)
        lua_pop(L, 1);
}

int Ref(lua_State* L, const char* name)
{
    lua_getglobal(L, name);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

void MeasureLua(int rounds)
{
    std::printf("\n## G2 Lua 5.4.7 callbacks\n");
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    luaL_dostring(L, kLuaScript);
    const int trivial = Ref(L, "trivial"), real = Ref(L, "real"), alloc = Ref(L, "alloc"), runaway = Ref(L, "runaway");

    Print("  own state, raw API, trivial (batched)", MeasureBatched([&](size_t n) {
              for (size_t i = 0; i < n; i++) CallLua(L, trivial, static_cast<lua_Integer>(i), 1);
          }, 1u << 18, rounds));
    Print("  own state, raw API, realistic (batched)", MeasureBatched([&](size_t n) {
              for (size_t i = 0; i < n; i++) CallLua(L, real, static_cast<lua_Integer>(i), static_cast<lua_Integer>(i & 255));
          }, 1u << 18, rounds));
    Print("  own state, raw API, realistic (per call)", MeasurePerCall([&](size_t i) {
              CallLua(L, real, static_cast<lua_Integer>(i), static_cast<lua_Integer>(i & 255));
          }, 200000));
    Print("  own state, allocating body, incremental GC (per call)", MeasurePerCall([&](size_t i) {
              CallLua(L, alloc, static_cast<lua_Integer>(i), 1);
          }, 200000));
    luaL_dostring(L, "collectgarbage('generational')");
    Print("  own state, allocating body, generational GC (per call)", MeasurePerCall([&](size_t i) {
              CallLua(L, alloc, static_cast<lua_Integer>(i), 1);
          }, 200000));
    luaL_dostring(L, "collectgarbage('incremental')");

    // The instruction budget: a count hook every kHookEvery VM instructions, the budget reset per call
    Budget budget;
    g_budget = &budget;
    lua_sethook(L, BudgetHook, LUA_MASKCOUNT, kHookEvery);
    Print("  count hook on, realistic (batched)", MeasureBatched([&](size_t n) {
              for (size_t i = 0; i < n; i++)
              {
                  budget.left = 100000;
                  CallLua(L, real, static_cast<lua_Integer>(i), static_cast<lua_Integer>(i & 255));
              }
          }, 1u << 18, rounds));
    for (int64_t limit : {int64_t(10000), int64_t(100000), int64_t(1000000)})
    {
        const Distribution stop = MeasurePerCall([&](size_t) {
            budget.left = limit;
            CallLua(L, runaway, 0, 0);
        }, 50);
        const std::string name = "  runaway stopped by a budget of " + std::to_string(limit) + " instructions";
        Print(name.c_str(), stop);
    }
    lua_sethook(L, nullptr, 0, 0);

    // sol2, as the bindings call Lua today
    {
        sol::state lua;
        lua.open_libraries(sol::lib::base);
        lua.script(kLuaScript);
        sol::protected_function f = lua["real"];
        Print("  sol2 protected_function, realistic (batched)", MeasureBatched([&](size_t n) {
                  for (size_t i = 0; i < n; i++) f(static_cast<int>(i), static_cast<int>(i & 255));
              }, 1u << 18, rounds));
    }

    // A synchronous round trip to a worker thread that owns the state (the AutomationLua model): condvar, then spin
    for (bool spin : {false, true})
    {
        std::mutex m;
        std::condition_variable cv;
        std::atomic<int> state{0};  // 0 idle, 1 request, 2 done, 3 quit
        lua_Integer arg = 0;
        std::thread worker([&] {
            for (;;)
            {
                if (spin)
                {
                    int s;
                    while ((s = state.load(std::memory_order_acquire)) != 1 && s != 3) {}
                    if (s == 3) return;
                }
                else
                {
                    std::unique_lock<std::mutex> lock(m);
                    cv.wait(lock, [&] { return state.load() == 1 || state.load() == 3; });
                    if (state.load() == 3) return;
                }
                CallLua(L, real, arg, 1);
                if (spin)
                    state.store(2, std::memory_order_release);
                else
                {
                    std::lock_guard<std::mutex> lock(m);
                    state.store(2);
                    cv.notify_all();
                }
            }
        });
        const Distribution trip = MeasurePerCall([&](size_t i) {
            arg = static_cast<lua_Integer>(i);
            if (spin)
            {
                state.store(1, std::memory_order_release);
                while (state.load(std::memory_order_acquire) != 2) {}
                state.store(0, std::memory_order_relaxed);
            }
            else
            {
                std::unique_lock<std::mutex> lock(m);
                state.store(1);
                cv.notify_all();
                cv.wait(lock, [&] { return state.load() == 2; });
                state.store(0);
            }
        }, 20000);
        Print(spin ? "  round trip to a worker, spinning worker (per call)" : "  round trip to a worker, condvar (per call)",
              trip);
        if (spin)
            state.store(3, std::memory_order_release);
        else
        {
            std::lock_guard<std::mutex> lock(m);
            state.store(3);
            cv.notify_all();
        }
        worker.join();
    }

    // Async: the hit pushes {addr, value, frame} into a single-producer ring the worker drains later
    {
        struct Event
        {
            uint32_t frame;
            uint16_t addr;
            uint8_t value, kind;
        };
        constexpr size_t kRing = 4096;
        std::array<Event, kRing> ring{};
        std::atomic<size_t> head{0}, tail{0};
        uint64_t dropped = 0;
        std::atomic<bool> quit{false};
        std::thread consumer([&] {
            while (!quit.load(std::memory_order_acquire))
            {
                size_t t = tail.load(std::memory_order_relaxed);
                const size_t h = head.load(std::memory_order_acquire);
                while (t != h)
                {
                    CallLua(L, real, ring[t % kRing].addr, ring[t % kRing].value);
                    t++;
                }
                tail.store(t, std::memory_order_release);
                std::this_thread::yield();
            }
        });
        Print("  async push to a ring (batched, consumer draining)", MeasureBatched([&](size_t n) {
                  for (size_t i = 0; i < n; i++)
                  {
                      const size_t h = head.load(std::memory_order_relaxed);
                      if (h - tail.load(std::memory_order_acquire) >= kRing)
                      {
                          dropped++;
                          continue;
                      }
                      ring[h % kRing] = Event{static_cast<uint32_t>(i), static_cast<uint16_t>(i), 1, 0};
                      head.store(h + 1, std::memory_order_release);
                  }
              }, 1u << 20, rounds));
        quit.store(true, std::memory_order_release);
        consumer.join();
        std::printf("  (ring full, dropped %llu of %llu)\n", static_cast<unsigned long long>(dropped),
                    static_cast<unsigned long long>((1ull << 20) * rounds));
    }
    lua_close(L);
}

/// endregion </G2: Lua>

/// region <G3: Python>

#ifdef POC038_PYTHON
void MeasurePython()
{
    std::printf("\n## G3 Python %s callbacks\n", PY_VERSION);
    Py_InitializeEx(0);
    PyRun_SimpleString(R"(
count = 0
last = 0
def real(a, v):
    global count, last
    count += 1
    if v == 0:
        last = a
stop = False
def spin():
    x = 0
    while not stop:
        x += 1
)");
    PyObject* module = PyImport_AddModule("__main__");
    PyObject* real = PyObject_GetAttrString(module, "real");

    auto call = [real](size_t i) {
        PyObject* a = PyLong_FromSize_t(i);
        PyObject* v = PyLong_FromLong(static_cast<long>(i & 255));
        PyObject* args[] = {a, v};
        PyObject* result = PyObject_Vectorcall(real, args, 2, nullptr);
        Py_XDECREF(result);
        Py_DECREF(a);
        Py_DECREF(v);
    };
    Print("  GIL held by the caller (per call)", MeasurePerCall(call, 200000));

    // The emulation thread calls with the GIL free: PyGILState_Ensure / Release around each call
    PyThreadState* main = PyEval_SaveThread();
    {
        Distribution d;
        std::thread emu([&] {
            d = MeasurePerCall([&](size_t i) {
                const PyGILState_STATE g = PyGILState_Ensure();
                call(i);
                PyGILState_Release(g);
            }, 100000);
        });
        emu.join();
        Print("  foreign thread, GIL free: Ensure + call + Release", d);
    }
    // The same with a thread state kept for the emulation thread (Release no longer destroys it per call)
    {
        Distribution d;
        std::thread emu([&] {
            const PyGILState_STATE outer = PyGILState_Ensure();
            PyThreadState* own = PyEval_SaveThread();
            d = MeasurePerCall([&](size_t i) {
                PyEval_RestoreThread(own);
                call(i);
                own = PyEval_SaveThread();
            }, 100000);
            PyEval_RestoreThread(own);
            PyGILState_Release(outer);
        });
        emu.join();
        Print("  foreign thread, GIL free, kept thread state", d);
    }

    // A script thread busy in Python (an automation script's loop): the GIL is handed over at the switch interval
    for (double interval : {0.005, 0.0005})
    {
        {
            const PyGILState_STATE g = PyGILState_Ensure();
            PyRun_SimpleString(("import sys, threading\nsys.setswitchinterval(" + std::to_string(interval) +
                                ")\nstop = False\nt = threading.Thread(target=spin)\nt.start()\n").c_str());
            PyGILState_Release(g);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));  // POC only: let the spinner take the GIL
        Distribution d;
        std::thread emu([&] {
            d = MeasurePerCall([&](size_t i) {
                const PyGILState_STATE g = PyGILState_Ensure();
                call(i);
                PyGILState_Release(g);
            }, 200);
        });
        emu.join();
        const std::string name = "  foreign thread, a busy script, switch interval " +
                                 std::to_string(static_cast<int>(interval * 1e6)) + " us";
        Print(name.c_str(), d);
        const PyGILState_STATE g = PyGILState_Ensure();
        PyRun_SimpleString("stop = True\nt.join()\n");
        PyGILState_Release(g);
    }
    PyEval_RestoreThread(main);
    Py_DECREF(real);
    Py_FinalizeEx();
}
#endif

/// endregion </G3: Python>

void MeasureClock(int rounds)
{
    std::printf("\n## G4 measuring a callback\n");
    int64_t sink = 0;
    Print("  two steady_clock reads (batched)", MeasureBatched([&](size_t n) {
              for (size_t i = 0; i < n; i++)
              {
                  const auto a = Clock::now();
                  const auto b = Clock::now();
                  sink += (b - a).count();
              }
          }, 1u << 20, rounds));
    std::printf("  (sink %lld)\n", static_cast<long long>(sink & 1));
}
}  // namespace

int main(int argc, char** argv)
{
    const int rounds = argc > 1 ? std::max(1, std::atoi(argv[1])) : 7;
    std::printf("# POC 038, %d rounds per batched measurement\n", rounds);
    MeasureConditions(rounds);
    MeasureLua(rounds);
#ifdef POC038_PYTHON
    MeasurePython();
#else
    std::printf("\n## G3 Python: not built (no Python3 Development.Embed)\n");
#endif
    MeasureClock(rounds);
    return 0;
}
