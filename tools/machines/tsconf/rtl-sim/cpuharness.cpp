// TS-Conf CPU memory wait simulation (Verilator testbench for tbcpu.v).
//
// The RTL decides when the Z80 clock runs (zclock.v: zpos / zneg strobes, stalled by
// zmem.v through cpu_stall). The Z80 is not in the RTL tree, so this file plays it as a
// bus-cycle model: a scripted list of machine cycles (M1 opcode fetch, memory read,
// memory write, internal T-states) whose pins change on the Z80 clock edges the RTL
// produces, with the Z80's output delay (kPinDelay below). A machine cycle's length in
// fclk (28 MHz clocks) minus its nominal length is its wait.
//
// One simulation runs many tests ("segments"), one or several per raster line: a sync
// pseudo-cycle idles the Z80 until a chosen fclk of the frame (and, at 14 MHz, a chosen
// DRAM phase c0..c3: the Z80 clock is shifted by single fclk through zclock's ide_stall
// input until it matches), then the segment's machine cycles run.
//
// Usage:
//   tsconf-cpu-sim sanity                    self-checks only
//   tsconf-cpu-sim all <results-dir>         self-checks, then write cpu-waits.txt
//   tsconf-cpu-sim trace <mhz> <vconf-hex> <line> <dot> <phase> <prog> [cache-hex]
//                                            fclk-by-fclk trace of one program
//                                            (prog: tokens as in cpu-waits.txt, e.g. M1.8000,RD.C000;
//                                            CPU_SIM_PIN_DELAY=<n> overrides the pin delay
//                                            for any command)
//   tsconf-cpu-sim cache [results-dir]       cache fill / retention scenarios: print them, write
//                                            cache-retention.txt (README "CPU cache fill and retention")
//
// Exit code 0 = all self-checks passed.

#include "Vtbcpu.h"
#include "verilated.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// DRAM image: 4 MB, word address as the RTL uses it (byte address = word * 2, low byte first)

static std::vector<uint8_t> g_mem(4u * 1024u * 1024u, 0);
struct DramWrite { uint32_t byteAddr; uint8_t data; };
static std::vector<DramWrite> g_writes;  // every byte the DRAM model stored, in order

extern "C" int dram_read(int addr)
{
    uint32_t w = static_cast<uint32_t>(addr) & 0x1FFFFFu;
    return g_mem[w * 2] | (g_mem[w * 2 + 1] << 8);
}

extern "C" void dram_write(int addr, int data, int bsel)
{
    uint32_t w = static_cast<uint32_t>(addr) & 0x1FFFFFu;
    if (bsel & 1) { g_mem[w * 2] = static_cast<uint8_t>(data); g_writes.push_back({w * 2, static_cast<uint8_t>(data)}); }
    if (bsel & 2) { g_mem[w * 2 + 1] = static_cast<uint8_t>(data >> 8); g_writes.push_back({w * 2 + 1, static_cast<uint8_t>(data >> 8)}); }
}

// CPU memory map: window 0 = ROM (MEM_CONFIG W0_MAP_N = 1, W0_RAM = 0), windows 1..3 = RAM pages
static const uint8_t kPage[4] = {0x00, 0x20, 0x21, 0x22};
static const uint8_t kMemConf = 0x04;
static uint32_t PhysOf(uint16_t a) { return kPage[a >> 14] * 16384u + (a & 0x3FFFu); }
static bool IsRom(uint16_t a) { return (a >> 14) == 0; }

// Page 0x21 (0x8000-0xBFFF): NOPs (00). Page 0x22 (0xC000-0xFFFF): a pattern below 80h,
// so it never equals the garbage the DRAM model drives outside CAS (D3h / B5h).
static uint8_t DataPattern(uint16_t a) { return static_cast<uint8_t>((a * 7 + 3) & 0x7F); }
static void FillMemory()
{
    std::fill(g_mem.begin(), g_mem.end(), 0);
    for (uint32_t a = 0xC000; a <= 0xFFFF; a++) g_mem[PhysOf(static_cast<uint16_t>(a))] = DataPattern(static_cast<uint16_t>(a));
    g_writes.clear();
}

// ---------------------------------------------------------------------------
// Z80 bus-cycle model

// Cf: a zero-time step between machine cycles that changes the world around the Z80 (cache
// scenarios): CACHE_CONFIG, a DRAM byte changed behind the CPU's back (as a DMA write would),
// the Z80 / zmem reset line
enum class Kind : uint8_t { M1, Rd, Wr, Id, Sy, Cf };
enum class CfOp : uint8_t { CacheEn, Poke, ResetOn, ResetOff };

struct MCycle
{
    Kind kind;
    uint16_t addr = 0;
    uint8_t data = 0;   // write data
    int t = 4;          // T-states
    long target = 0;    // Sy: first fclk of the frame the next cycle may start at
    int phase = -1;     // Sy: required DRAM phase of that T1 (14 MHz; -1 = any)
    CfOp op = CfOp::CacheEn;  // Cf: what changes (addr / data: the poked byte, data: CACHE_CONFIG)
};

static const char* KindName(Kind k)
{
    return k == Kind::M1 ? "M1" : k == Kind::Rd ? "RD" : k == Kind::Wr ? "WR" : k == Kind::Id ? "ID" : k == Kind::Sy ? "SY" : "CF";
}

static MCycle Fetch(uint16_t a, int t = 4) { return {Kind::M1, a, 0, t}; }
static MCycle Read(uint16_t a) { return {Kind::Rd, a, 0, 3}; }
static MCycle Write(uint16_t a, uint8_t d, int t = 3) { return {Kind::Wr, a, d, t}; }
static MCycle Idle(int t = 1) { return {Kind::Id, 0, 0, t}; }
static MCycle Sync(long target, int phase) { MCycle c{Kind::Sy, 0, 0, 0}; c.target = target; c.phase = phase; return c; }
static MCycle Config(CfOp op, uint16_t a = 0, uint8_t d = 0) { MCycle c{Kind::Cf, a, d, 0}; c.op = op; return c; }

// Token format (cpu-waits.txt "prog" lines): M1.aaaa or M1.aaaa.t (t T-states if not 4),
// RD.aaaa, WR.aaaa.dd or WR.aaaa.dd.t, IDt; cache scenarios also CE.x (CACHE_CONFIG = x),
// PK.aaaa.dd (DRAM byte at CPU address aaaa := dd, no CPU cycle), RST1 / RST0 (reset on / off)
static std::string Token(const MCycle& c)
{
    char buf[32];
    if (c.kind == Kind::Cf)
    {
        if (c.op == CfOp::CacheEn) snprintf(buf, sizeof buf, "CE.%X", c.data);
        else if (c.op == CfOp::Poke) snprintf(buf, sizeof buf, "PK.%04X.%02X", c.addr, c.data);
        else snprintf(buf, sizeof buf, c.op == CfOp::ResetOn ? "RST1" : "RST0");
        return buf;
    }
    if (c.kind == Kind::Id) snprintf(buf, sizeof buf, "ID%d", c.t);
    else if (c.kind == Kind::Wr) snprintf(buf, sizeof buf, c.t == 3 ? "WR.%04X.%02X" : "WR.%04X.%02X.%d", c.addr, c.data, c.t);
    else if (c.kind == Kind::M1) snprintf(buf, sizeof buf, c.t == 4 ? "M1.%04X" : "M1.%04X.%d", c.addr, c.t);
    else snprintf(buf, sizeof buf, "RD.%04X", c.addr);
    return buf;
}

static std::vector<MCycle> ParseProg(const std::string& s)
{
    std::vector<MCycle> p;
    size_t i = 0;
    while (i < s.size())
    {
        size_t j = s.find(',', i);
        if (j == std::string::npos) j = s.size();
        std::string t = s.substr(i, j - i);
        std::vector<std::string> f;
        size_t a = 0;
        while (true) { size_t b = t.find('.', a); f.push_back(t.substr(a, b - a)); if (b == std::string::npos) break; a = b + 1; }
        auto hex = [](const std::string& x) { return static_cast<int>(strtol(x.c_str(), nullptr, 16)); };
        if (f[0] == "CE") p.push_back(Config(CfOp::CacheEn, 0, static_cast<uint8_t>(hex(f[1]))));
        else if (f[0] == "PK") p.push_back(Config(CfOp::Poke, static_cast<uint16_t>(hex(f[1])), static_cast<uint8_t>(hex(f[2]))));
        else if (f[0] == "RST1") p.push_back(Config(CfOp::ResetOn));
        else if (f[0] == "RST0") p.push_back(Config(CfOp::ResetOff));
        else if (f[0].rfind("ID", 0) == 0) p.push_back(Idle(atoi(f[0].c_str() + 2)));
        else if (f[0] == "M1") p.push_back(Fetch(static_cast<uint16_t>(hex(f[1])), f.size() > 2 ? atoi(f[2].c_str()) : 4));
        else if (f[0] == "RD") p.push_back(Read(static_cast<uint16_t>(hex(f[1]))));
        else if (f[0] == "WR") p.push_back(Write(static_cast<uint16_t>(hex(f[1])), static_cast<uint8_t>(hex(f[2])), f.size() > 3 ? atoi(f[3].c_str()) : 3));
        i = j + 1;
    }
    return p;
}

struct Pins
{
    uint16_t za = 0xFFFF;
    uint8_t zdo = 0xEE;
    uint8_t mreq_n = 1, rd_n = 1, wr_n = 1, m1_n = 1, rfsh_n = 1;
};

// Pin delay: a pin the Z80 changes at a clock edge reaches the RTL registers this many
// fclk after the posedge that follows the edge. zclock.v puts every Z80 clock edge about
// 6 ns before an fclk posedge (its comment: 5.8 ns lead); Z80 output delays (CLK to
// MREQ / RD / WR / M1 / RFSH / address / data) are longer than that and shorter than
// one more fclk (35.7 ns), so the change is first seen one posedge later: 1.
// The sensitivity pass runs everything again with 2.
static int kPinDelay = 1;

struct RunCfg
{
    int turbo = 2;          // SYS_CONFIG[1:0]: 0 = 3.5, 1 = 7, 2 = 14 MHz
    uint8_t cacheEn = 0;    // CACHE_CONFIG[3:0]
    uint8_t vconf = 0x20;   // V_CONFIG (20h = NOGFX)
    uint8_t vpage = 0x05;   // V_PAGE
};

struct CycleRec
{
    MCycle mc;
    long t1 = -1;        // f of the period whose zpos makes T1's rising edge (Sy: when it released)
    long end = -1;       // f of the next machine cycle's T1 strobe
    long t2f = -1;       // f of T2's falling-edge strobe
    long req = -1;       // f of the dram_beg period (14 MHz) / first cpu_req period (3.5 / 7 MHz)
    int sampleSrc = 0;   // 'L' DRAM bus (cpu_latch), 'C' cache register, 'R' ROM
    int samplePhase = -1;
    uint8_t got = 0;     // the byte the Z80 took
    uint8_t mem = 0;     // the DRAM byte at that moment
    bool dataOk = true;
    bool writeOk = true;
    int stalls = 0;      // fclk with cpu_stall = 1 inside the cycle
    int stall357 = 0;    // fclk with stall357 = 1 at 3.5 / 7 MHz (the stall zmem.v uses there)
};

struct RunResult
{
    std::vector<CycleRec> cyc;
    std::vector<int> decisions;  // video block decision cycles (frame DRAM cycle numbers)
    bool edgesOk = true;         // rising / falling strobes alternate, never both
    bool done = false;
};

static int NominalPerT(int turbo) { return turbo >= 2 ? 2 : (turbo == 1 ? 4 : 8); }

static RunResult RunProg(const RunCfg& cfg, const std::vector<MCycle>& prog, FILE* trace = nullptr)
{
    FillMemory();
    auto ctx = std::make_unique<VerilatedContext>();
    ctx->randReset(0);
    auto top = std::make_unique<Vtbcpu>(ctx.get());
    RunResult rr;
    rr.cyc.resize(prog.size());
    for (size_t i = 0; i < prog.size(); i++) rr.cyc[i].mc = prog[i];

    Pins applied;
    std::deque<std::pair<long, Pins>> pending;  // (posedge index it is first seen at, pins)
    long k = 0;                                 // posedges done: the current period is k

    auto drive = [&](const Pins& p) {
        top->za = p.za; top->zdo = p.zdo; top->mreq_n = p.mreq_n; top->rd_n = p.rd_n;
        top->wr_n = p.wr_n; top->m1_n = p.m1_n; top->rfsh_n = p.rfsh_n;
    };
    // the low half of period k with the pins posedge P[k+1] sees, settled
    auto settle = [&]() {
        while (!pending.empty() && pending.front().first <= k + 1)
        {
            applied = pending.front().second;
            pending.pop_front();
        }
        drive(applied);
        top->clk = 0;
        top->eval();
    };
    auto rise = [&]() {
        top->clk = 1;
        top->eval();
        k++;
    };

    top->clk = 0;
    top->res = 1;
    top->rst_n = 0;
    top->turbo = static_cast<uint8_t>(cfg.turbo);
    top->cache_en = cfg.cacheEn;
    top->memconf = kMemConf;
    top->xt_page = kPage[0] | (kPage[1] << 8) | (kPage[2] << 16) | (static_cast<uint32_t>(kPage[3]) << 24);
    top->ext_stall = 0;
    for (int i = 0; i < 8; i++) { settle(); rise(); }
    top->res = 0;
    top->rst_n = 1;
    settle(); rise();
    auto write = [&](CData& strobe, uint8_t value) {
        top->xt_wr_data = value; strobe = 1; settle(); rise(); strobe = 0; settle(); rise();
    };
    write(top->vconf_wr, cfg.vconf);
    write(top->vpage_wr, cfg.vpage);
    write(top->border_wr, 0x00);
    write(top->tsconf_wr, 0x00);

    const int n = static_cast<int>(prog.size());
    Pins z;            // what the Z80 drives
    int cur = 0;       // machine cycle index
    int T = 0;         // T-state of cycle cur, 0 = not begun
    int lastEdge = 0;  // +1 rising, -1 falling
    bool stallNext = false;
    long prevF = 0;
    long guard = 0;
    bool settled = false;

    // Each iteration: settle the low half of period k with the pins P[k+1] sees, observe
    // (registered outputs = period k, combinational ones = what P[k+1] samples), run the
    // Z80 clock edge that falls at the end of period k, then rise into period k + 1.
    while (!rr.done)
    {
        if (++guard > 4000000) { fprintf(stderr, "error: run did not finish\n"); exit(2); }
        if (settled)
        {
            settle();  // pins a delay of 0 scheduled for P[k+1]
            top->ext_stall = stallNext ? 1 : 0;
            stallNext = false;
            top->eval();
            if (T > 0 && rr.cyc[cur].req < 0 && cfg.turbo >= 2 && top->dram_beg_o) rr.cyc[cur].req = prevF;
            rise();
        }
        top->ext_stall = 0;
        settle();
        settled = true;

        const int phase = top->c0_o ? 0 : top->c1_o ? 1 : top->c2_o ? 2 : 3;
        const long d = static_cast<long>(top->ray_y) * 448 + top->ray_x;
        const long f = 4 * d + phase;
        prevF = f;

        if (top->c3_o && top->video_start_o && top->video_go_o) rr.decisions.push_back(static_cast<int>(d));
        if (T > 0)
        {
            CycleRec& r = rr.cyc[cur];
            if (top->cpu_stall_o) r.stalls++;
            if (top->stall357_o && cfg.turbo < 2) r.stall357++;
            if (r.req < 0 && (cfg.turbo >= 2 ? top->dram_beg_o : top->cpu_req_o)) r.req = f;
        }
        if (trace)
            fprintf(trace, "f=%ld line=%d dot=%d c%d zpos=%d zneg=%d stall=%d dram_beg=%d cpu_req=%d cpu_next=%d curr_cpu=%d curr_vid=%d latch=%d zd=%02X mreq_n=%d rd_n=%d wr_n=%d m1_n=%d rfsh_n=%d za=%04X cyc=%d T=%d\n",
                    f, top->ray_y, top->ray_x, phase, top->zpos_o, top->zneg_o, top->cpu_stall_o, top->dram_beg_o, top->cpu_req_o,
                    top->cpu_next_o, top->curr_cpu_o, top->curr_vid_o, top->cpu_latch_o, top->zd_out_o, applied.mreq_n, applied.rd_n,
                    applied.wr_n, applied.m1_n, applied.rfsh_n, applied.za, cur, T);

        if (top->zpos_o && top->zneg_o) rr.edgesOk = false;
        if (!top->zpos_o && !top->zneg_o) continue;
        const bool rising = top->zpos_o;
        if ((rising ? 1 : -1) == lastEdge) rr.edgesOk = false;
        lastEdge = rising ? 1 : -1;

        // A Z80 clock edge at the end of period k
        if (rising)
        {
            if (T > 0)
            {
                if (T == prog[cur].t) { rr.cyc[cur].end = f; cur++; T = 0; }
                else T++;
            }
            if (T == 0)
            {
                bool wait = false;
                while (cur < n && (prog[cur].kind == Kind::Sy || prog[cur].kind == Kind::Cf))
                {
                    const MCycle& s = prog[cur];
                    if (s.kind == Kind::Cf)
                    {
                        if (s.op == CfOp::CacheEn) top->cache_en = s.data;
                        else if (s.op == CfOp::Poke) g_mem[PhysOf(s.addr)] = s.data;
                        else if (s.op == CfOp::ResetOn) { top->rst_n = 0; top->cache_en = 0; }  // zports.v: cacheconf <= 0 at reset
                        else top->rst_n = 1;
                        rr.cyc[cur].t1 = rr.cyc[cur].end = f;
                        cur++;
                        continue;
                    }
                    if (f >= s.target && (s.phase < 0 || cfg.turbo < 2 || phase == s.phase))
                    {
                        rr.cyc[cur].t1 = rr.cyc[cur].end = f;
                        cur++;
                    }
                    else
                    {
                        // from 16 fclk before the target, shift the Z80 clock by one fclk
                        // until its rising edges have the parity of the wanted phase: then
                        // one lands exactly on the first f >= target with that phase
                        if (s.phase >= 0 && cfg.turbo >= 2 && f >= s.target - 16 && (f & 1) != (s.phase & 1)) stallNext = true;
                        wait = true;
                        break;
                    }
                }
                if (cur == n) { rr.done = true; break; }
                if (wait)
                {
                    z.rfsh_n = 1;  // idle T-states
                    pending.push_back({k + 1 + kPinDelay, z});
                    continue;
                }
                T = 1;
                rr.cyc[cur].t1 = f;
            }
        }
        if (T == 0) continue;

        const MCycle& mc = prog[cur];
        CycleRec& r = rr.cyc[cur];
        if (!rising && T == 2) r.t2f = f;
        // The Z80 drives the data bus in its write cycles; it floats some time after the
        // next T1 rising edge and the next read's memory drives it after RD falls. Model:
        // the write data stays until the next machine cycle's T1 falling edge.
        if (!rising && T == 1 && mc.kind != Kind::Wr) z.zdo = 0xEE;
        auto sample = [&]() {
            if (IsRom(mc.addr)) { r.sampleSrc = 'R'; return; }
            r.sampleSrc = top->cpu_latch_o ? 'L' : 'C';
            r.samplePhase = phase;
            r.got = top->zd_out_o;
            r.mem = g_mem[PhysOf(mc.addr)];
            r.dataOk = top->zd_out_o == g_mem[PhysOf(mc.addr)] && top->zd_ena_o;
        };
        switch (mc.kind)
        {
        case Kind::M1:
            if (rising && T == 1) { z.za = mc.addr; z.m1_n = 0; z.rfsh_n = 1; }
            else if (!rising && T == 1) { z.mreq_n = 0; z.rd_n = 0; }
            else if (rising && T == 3) { sample(); z.mreq_n = 1; z.rd_n = 1; z.m1_n = 1; z.rfsh_n = 0; z.za = 0x3F00; }
            else if (!rising && T == 3) { z.mreq_n = 0; }
            else if (!rising && T == 4) { z.mreq_n = 1; }
            break;
        case Kind::Rd:
            if (rising && T == 1) { z.za = mc.addr; z.rfsh_n = 1; }
            else if (!rising && T == 1) { z.mreq_n = 0; z.rd_n = 0; }
            else if (!rising && T == 3) { sample(); z.mreq_n = 1; z.rd_n = 1; }
            break;
        case Kind::Wr:
            if (rising && T == 1) { z.za = mc.addr; z.rfsh_n = 1; }
            else if (!rising && T == 1) { z.mreq_n = 0; z.zdo = mc.data; }
            else if (!rising && T == 2) { z.wr_n = 0; }
            else if (!rising && T == 3) { z.mreq_n = 1; z.wr_n = 1; }
            break;
        case Kind::Id:
            if (rising && T == 1) z.rfsh_n = 1;
            break;
        case Kind::Sy:
        case Kind::Cf:
            break;
        }
        pending.push_back({k + 1 + kPinDelay, z});
    }
    // let DRAM cycles already granted finish (a write does not wait for its cycle)
    for (int i = 0; i < 32; i++) { settle(); rise(); }
    top->final();

    // every write cycle's byte reached the DRAM, in program order
    size_t w = 0;
    for (CycleRec& r : rr.cyc)
        if (r.mc.kind == Kind::Wr && !IsRom(r.mc.addr))
        {
            const uint32_t a = PhysOf(r.mc.addr);
            size_t j = w;
            while (j < g_writes.size() && !(g_writes[j].byteAddr == a && g_writes[j].data == r.mc.data)) j++;
            if (j == g_writes.size()) r.writeOk = false;
            else w = j + 1;
        }
    return rr;
}

static int Wait(const CycleRec& r, int turbo) { return static_cast<int>(r.end - r.t1) - r.mc.t * NominalPerT(turbo); }

// ---------------------------------------------------------------------------
// Programs. `o` offsets the addresses so successive tests use other cache lines.

static std::vector<MCycle> NopRun(uint16_t at, int n)
{
    std::vector<MCycle> p;
    for (int i = 0; i < n; i++) p.push_back(Fetch(static_cast<uint16_t>(at + i)));
    return p;
}
// LD A,(HL) / LD (HL),A runs: the opcode from page 0x21, the data in page 0x22
static std::vector<MCycle> LoadRun(int n, uint16_t o)
{
    std::vector<MCycle> p;
    for (int i = 0; i < n; i++) { p.push_back(Fetch(static_cast<uint16_t>(0x8000 + o + i))); p.push_back(Read(static_cast<uint16_t>(0xC000 + o + 2 * i))); }
    return p;
}
static std::vector<MCycle> StoreRun(int n, uint16_t o)
{
    std::vector<MCycle> p;
    for (int i = 0; i < n; i++) { p.push_back(Fetch(static_cast<uint16_t>(0x8000 + o + i))); p.push_back(Write(static_cast<uint16_t>(0xE000 + o + i), static_cast<uint8_t>(i * 13 + 1))); }
    return p;
}
// POP rr: M1 + two reads; PUSH rr: M1 (5 T) + two writes; LDI: two M1 + read + write (5 T)
static std::vector<MCycle> PopRun(int n, uint16_t o)
{
    std::vector<MCycle> p;
    for (int i = 0; i < n; i++)
    {
        p.push_back(Fetch(static_cast<uint16_t>(0x8000 + o + i)));
        p.push_back(Read(static_cast<uint16_t>(0xC000 + o + 2 * i)));
        p.push_back(Read(static_cast<uint16_t>(0xC001 + o + 2 * i)));
    }
    return p;
}
static std::vector<MCycle> PushRun(int n, uint16_t o)
{
    std::vector<MCycle> p;
    for (int i = 0; i < n; i++)
    {
        p.push_back(Fetch(static_cast<uint16_t>(0x8000 + o + i), 5));
        p.push_back(Write(static_cast<uint16_t>(0xF000 + o - 2 * i - 1), 0x11));
        p.push_back(Write(static_cast<uint16_t>(0xF000 + o - 2 * i - 2), 0x22));
    }
    return p;
}
static std::vector<MCycle> LdiRun(int n, uint16_t o)
{
    std::vector<MCycle> p;
    for (int i = 0; i < n; i++)
    {
        p.push_back(Fetch(static_cast<uint16_t>(0x8000 + o + 2 * i)));
        p.push_back(Fetch(static_cast<uint16_t>(0x8001 + o + 2 * i)));
        p.push_back(Read(static_cast<uint16_t>(0xC000 + o + i)));
        p.push_back(Write(static_cast<uint16_t>(0xE800 + o + i), static_cast<uint8_t>(0x40 + i), 5));
    }
    return p;
}
// One access, then ROM fetches (no DRAM cycle before it: the arbiter is clean)
static std::vector<MCycle> Isolated(const MCycle& x)
{
    return {x, Fetch(0x0100), Fetch(0x0101), Fetch(0x0102)};
}

// ---------------------------------------------------------------------------
// Jobs: one simulation each, made of segments

struct Segment
{
    std::string name;
    long startF;     // first T1 at the first rising edge with f >= startF ...
    int phase;       // ... and (14 MHz) f % 4 == phase
    std::vector<MCycle> prog;
};

struct Job
{
    std::string group;
    RunCfg cfg;
    std::vector<Segment> segs;
};

static long FAt(int line, int dot) { return 4L * (line * 448L + dot); }
static const char* MhzName(int turbo) { return turbo >= 2 ? "14" : (turbo == 1 ? "7" : "3.5"); }

struct Mode { uint8_t vconf; uint8_t vpage; const char* name; int h0; };
// h0: the first block decision of a window line (hpix_beg - go_offs, G_X_OFFS = 0)
static const Mode kModes[] = {
    {0x00, 0x05, "zx", 140 - 18}, {0x41, 0x40, "16c", 108 - 6}, {0x42, 0x60, "256c", 108 - 4}, {0x83, 0x10, "txt", 108 - 10},
};

static std::vector<Job> BuildJobs()
{
    std::vector<Job> jobs;
    char nm[96];
    auto off = [](size_t i) { return static_cast<uint16_t>((i * 0x200) & 0x1E00); };

    // --- 14 MHz, border: TXT 320x240 is set, lines 8..55 are above its window ---
    for (int cache : {0, 0xF})
    {
        Job j;
        j.group = cache ? "border14c" : "border14";
        j.cfg.turbo = 2; j.cfg.cacheEn = static_cast<uint8_t>(cache); j.cfg.vconf = 0x83; j.cfg.vpage = 0x10;
        int line = 8;
        for (int ph = 0; ph < 4; ph++)
        {
            auto add = [&](const char* what, std::vector<MCycle> p) {
                snprintf(nm, sizeof nm, "%s-p%d", what, ph);
                j.segs.push_back({nm, FAt(line++, 20), ph, std::move(p)});
            };
            uint16_t o = off(j.segs.size());
            add("iso-m1", Isolated(Fetch(static_cast<uint16_t>(0x8000 + o))));
            o = off(j.segs.size());
            add("iso-rd", Isolated(Read(static_cast<uint16_t>(0xC000 + o))));
            o = off(j.segs.size());
            add("iso-wr", Isolated(Write(static_cast<uint16_t>(0xE000 + o), 0x5A)));
            if (!cache)
            {
                add("nop", NopRun(0x8000, 96));
                add("ldahl", LoadRun(48, 0));
                add("ldhla", StoreRun(48, 0));
                add("rom-nop", NopRun(0x0000, 32));
            }
            else
            {
                // a fresh cache line set: NOPs fill word by word, then the same 64 bytes again (hits)
                o = off(j.segs.size());
                std::vector<MCycle> p = NopRun(static_cast<uint16_t>(0x8000 + o), 64), p2 = NopRun(static_cast<uint16_t>(0x8000 + o), 64);
                p.insert(p.end(), p2.begin(), p2.end());
                add("nop-twice", p);
                // a read that misses, then the other byte of the same word (hit)
                o = off(j.segs.size());
                add("rd-miss-hit", {Read(static_cast<uint16_t>(0xC000 + o)), Fetch(0x0100), Fetch(0x0101), Read(static_cast<uint16_t>(0xC001 + o)), Fetch(0x0102), Fetch(0x0103)});
            }
        }
        jobs.push_back(std::move(j));
    }

    // --- 14 MHz, window lines, per mode ---
    for (const Mode& m : kModes)
    {
        Job j;
        j.group = std::string("win14-") + m.name;
        j.cfg.turbo = 2; j.cfg.vconf = m.vconf; j.cfg.vpage = m.vpage;
        int line = 100;
        for (int ph = 0; ph < 4; ph++)
        {
            auto add = [&](const char* what, std::vector<MCycle> p) {
                snprintf(nm, sizeof nm, "%s-p%d", what, ph);
                j.segs.push_back({nm, FAt(line++, m.h0 - 24), ph, std::move(p)});
            };
            add("nop", NopRun(0x8000, 120));
            add("ldahl", LoadRun(60, 0));
            add("ldhla", StoreRun(60, 0));
            add("pop", PopRun(40, 0));
            add("push", PushRun(40, 0));
            add("ldi", LdiRun(30, 0));
        }
        const RunCfg winCfg = j.cfg;
        const std::string winGroup = j.group;
        jobs.push_back(std::move(j));

        // isolated accesses at every fclk of two blocks of 8 DRAM cycles (64 positions),
        // eight tests per line, 32 dots apart (a multiple of every block length)
        Job ji;
        ji.group = winGroup + "-iso";
        ji.cfg = winCfg;
        int slot = 0;
        for (const char* what : {"iso-m1", "iso-rd", "iso-wr", "wwwr"})
            for (int o = 0; o < 64; o++, slot++)
            {
                const long f = FAt(100 + slot / 8, m.h0 + 40 + 32 * (slot % 8)) + o;
                std::vector<MCycle> p;
                if (!strcmp(what, "iso-m1")) p = Isolated(Fetch(0x8000));
                else if (!strcmp(what, "iso-rd")) p = Isolated(Read(0xC000));
                else if (!strcmp(what, "iso-wr")) p = Isolated(Write(0xE000, 0x5A));
                else p = {Write(0xE000, 1), Write(0xE001, 2), Write(0xE002, 3), Read(0xC000), Fetch(0x0100), Fetch(0x0101)};
                snprintf(nm, sizeof nm, "%s-o%d", what, o);
                ji.segs.push_back({nm, f, static_cast<int>(f % 4), p});
            }
        jobs.push_back(std::move(ji));
    }
    {
        Job j;
        j.group = "border14-256c";
        j.cfg.turbo = 2; j.cfg.vconf = 0x42; j.cfg.vpage = 0x60;
        j.segs.push_back({"nop-p0", FAt(20, 80), 0, NopRun(0x8000, 120)});
        j.segs.push_back({"ldhla-p0", FAt(21, 80), 0, StoreRun(60, 0)});
        jobs.push_back(std::move(j));
    }

    // --- 3.5 and 7 MHz: does stall357 ever fire? Heaviest modes, every DRAM-cycle alignment ---
    const Mode slowModes[] = {kModes[2], kModes[3], {0x02, 0x60, "256c-256", 140 - 4}, {0x03, 0x10, "txt-256", 140 - 10}};
    for (int turbo : {1, 0})
        for (const Mode& m : slowModes)
        {
            Job j;
            j.group = std::string(turbo ? "s7-" : "s35-") + m.name;
            j.cfg.turbo = turbo; j.cfg.vconf = m.vconf; j.cfg.vpage = m.vpage;
            const int div = turbo ? 1 : 2;  // half the instructions at 3.5 MHz (one line each)
            int line = 100;
            for (int sh = 0; sh < 8; sh++)
            {
                auto add = [&](const char* what, std::vector<MCycle> p) {
                    snprintf(nm, sizeof nm, "%s-s%d", what, sh);
                    j.segs.push_back({nm, FAt(line++, m.h0 - 8 + sh), -1, std::move(p)});
                };
                add("nop", NopRun(0x8000, 80 / div));
                add("ldahl", LoadRun(40 / div, 0));
                add("ldhla", StoreRun(40 / div, 0));
                add("pop", PopRun(30 / div, 0));
                add("push", PushRun(30 / div, 0));
                add("ldi", LdiRun(20 / div, 0));
            }
            jobs.push_back(std::move(j));
        }
    return jobs;
}

static std::vector<MCycle> FlatProg(const Job& j, std::vector<size_t>& segStart)
{
    std::vector<MCycle> p;
    for (const Segment& s : j.segs)
    {
        p.push_back(Sync(s.startF, s.phase));
        segStart.push_back(p.size());
        p.insert(p.end(), s.prog.begin(), s.prog.end());
    }
    return p;
}

// ---------------------------------------------------------------------------
// Result writer

struct Tally
{
    int segs = 0, cycles = 0, dataBad = 0, writeBad = 0, reqBad = 0, late = 0, stall357Segs = 0, edgeBad = 0;
};

static void Account(const Job& j, const RunResult& rr, const std::vector<size_t>& segStart, Tally& t, FILE* f)
{
    if (!rr.edgesOk || !rr.done) t.edgeBad++;
    for (size_t si = 0; si < j.segs.size(); si++)
    {
        const Segment& s = j.segs[si];
        const size_t a = segStart[si], b = a + s.prog.size();
        const CycleRec& c0 = rr.cyc[a];
        bool ok = true, late = c0.t1 > s.startF + (j.cfg.turbo >= 2 ? 3 : 4 * NominalPerT(j.cfg.turbo));
        long total = 0;
        int stalls = 0, s357 = 0;
        for (size_t i = a; i < b; i++)
        {
            const CycleRec& r = rr.cyc[i];
            t.cycles++;
            if (!r.dataOk) { t.dataBad++; ok = false; }
            if (!r.writeOk) { t.writeBad++; ok = false; }
            if (j.cfg.turbo >= 2 && r.req >= 0 && r.req != r.t2f) { t.reqBad++; ok = false; }
            total += r.end - r.t1;
            stalls += r.stalls;
            s357 += r.stall357;
        }
            t.segs++;
        if (late) t.late++;
        if (s357) t.stall357Segs++;
        if (!f) continue;
        const long line = c0.t1 / (4 * 448);
        int first = -1, last = -1;
        for (int dcy : rr.decisions)
            if (dcy / 448 == line) { if (first < 0) first = dcy % 448; last = dcy % 448; }
        fprintf(f, "run %s %s mhz=%s cache=%X vconf=%02X t0=%ld line=%ld dot=%ld phase=%ld fetch=%d..%d total=%ld stalls=%d stall357=%d ok=%d\n",
                j.group.c_str(), s.name.c_str(), MhzName(j.cfg.turbo), j.cfg.cacheEn, j.cfg.vconf, c0.t1, line, (c0.t1 / 4) % 448,
                c0.t1 % 4, first, last, total, stalls, s357, ok && !late ? 1 : 0);
        std::string pr = "  prog", w = "  wait", q = "  req", sm = "  sample";
        char buf[32];
        for (size_t i = a; i < b; i++)
        {
            const CycleRec& r = rr.cyc[i];
            pr += (i == a ? " " : ",") + Token(r.mc);
            snprintf(buf, sizeof buf, " %d", Wait(r, j.cfg.turbo)); w += buf;
            if (r.req >= 0) snprintf(buf, sizeof buf, " %ld", r.req - r.t1); else snprintf(buf, sizeof buf, " -");
            q += buf;
            if (r.sampleSrc == 'L' || r.sampleSrc == 'C') snprintf(buf, sizeof buf, " %c%d", r.sampleSrc, r.samplePhase); else snprintf(buf, sizeof buf, " -");
            sm += buf;
        }
        fprintf(f, "%s\n%s\n%s\n%s\n", pr.c_str(), w.c_str(), q.c_str(), sm.c_str());
    }
}

// ---------------------------------------------------------------------------
// Self-checks

static bool Check(const char* name, bool ok)
{
    printf("  %-70s %s\n", name, ok ? "PASS" : "FAIL");
    return ok;
}

static RunResult RunOne(const RunCfg& cfg, long startF, int phase, const std::vector<MCycle>& prog)
{
    std::vector<MCycle> p = {Sync(startF, phase)};
    p.insert(p.end(), prog.begin(), prog.end());
    RunResult r = RunProg(cfg, p);
    r.cyc.erase(r.cyc.begin());
    return r;
}

static bool RunSanity()
{
    bool all = true;
    printf("self-checks (pin delay %d fclk):\n", kPinDelay);
    // 1. No DRAM: the Z80 clock runs at its nominal rate, edges alternate, nothing stalls
    for (int turbo : {2, 1, 0})
    {
        RunCfg c; c.turbo = turbo; c.vconf = 0x83; c.vpage = 0x10;
        RunResult r = RunOne(c, FAt(150, 98), -1, NopRun(0x0000, 24));
        bool ok = r.done && r.edgesOk;
        for (const CycleRec& x : r.cyc) ok &= Wait(x, turbo) == 0 && x.req < 0 && x.stalls == 0;
        char nm[96];
        snprintf(nm, sizeof nm, "%s MHz: ROM fetches in a TXT window line take 4 T, no DRAM request", MhzName(turbo));
        all &= Check(nm, ok);
    }
    // 2. The four request phases are reachable; the request is at T2's falling edge, 3 fclk after T1
    {
        bool ok = true;
        int seen = 0;
        for (int ph = 0; ph < 4; ph++)
        {
            RunCfg c; c.turbo = 2; c.vconf = 0x83; c.vpage = 0x10;
            RunResult r = RunOne(c, FAt(10, 20), ph, Isolated(Fetch(0x8000)));
            ok &= r.done && r.cyc[0].t1 % 4 == ph && r.cyc[0].req == r.cyc[0].t1 + 3 && r.cyc[0].req == r.cyc[0].t2f;
            seen |= 1 << (r.cyc[0].req % 4);
        }
        all &= Check("14 MHz: T1 at each of c0..c3, dram_beg at T2 falling = T1 + 3 fclk", ok && seen == 15);
    }
    // 3. The data the Z80 samples is the memory byte, writes land (border, 256C window)
    {
        bool ok = true;
        for (uint8_t vc : {uint8_t(0x83), uint8_t(0x42)})
            for (int ph = 0; ph < 4; ph++)
            {
                RunCfg c; c.turbo = 2; c.vconf = vc; c.vpage = vc == 0x42 ? 0x60 : 0x10;
                for (auto prog : {LoadRun(24, 0), StoreRun(24, 0), LdiRun(12, 0)})
                {
                    RunResult r = RunOne(c, FAt(vc == 0x42 ? 150 : 10, 100), ph, prog);
                    ok &= r.done && r.edgesOk;
                    for (const CycleRec& x : r.cyc) ok &= x.dataOk && x.writeOk;
                }
            }
        all &= Check("14 MHz: sampled bytes = memory, writes land (border, 256C window)", ok);
    }
    // 4. Bus-model control: with the pins seen at the very next posedge (pin delay 0,
    //    faster than any Z80) the request moves to T1's falling edge.
    {
        const int saved = kPinDelay;
        kPinDelay = 0;
        RunCfg c; c.turbo = 2; c.vconf = 0x83; c.vpage = 0x10;
        RunResult r = RunOne(c, FAt(10, 20), 0, Isolated(Fetch(0x8000)));
        kPinDelay = saved;
        all &= Check("control: pin delay 0 puts dram_beg at T1 + 1 (T1 falling edge)", r.done && r.cyc[0].req == r.cyc[0].t1 + 1);
    }
    return all;
}

static bool RunAll(const std::string& dir)
{
    std::vector<Job> jobs = BuildJobs();
    std::string path = dir + "/cpu-waits.txt";
    FILE* f = fopen(path.c_str(), "w");
    if (!f) { perror(path.c_str()); return false; }
    fprintf(f, "# Generated by tools/machines/tsconf/rtl-sim (tsconf-cpu-sim all). Format: README.md,\n");
    fprintf(f, "# section \"CPU DRAM waits at 14 MHz\". pin_delay=%d mem_config=%02X pages=%02X,%02X,%02X,%02X\n", kPinDelay, kMemConf, kPage[0], kPage[1], kPage[2], kPage[3]);
    Tally t;
    std::vector<RunResult> base;
    for (const Job& j : jobs)
    {
        std::vector<size_t> segStart;
        std::vector<MCycle> p = FlatProg(j, segStart);
        base.push_back(RunProg(j.cfg, p));
        Account(j, base.back(), segStart, t, f);
    }
    fclose(f);
    printf("wrote %s (%zu simulations, %d tests, %d machine cycles)\n", path.c_str(), jobs.size(), t.segs, t.cycles);

    // Sensitivity: the same jobs with the Z80 pins one fclk slower (pin delay 2)
    const int saved = kPinDelay;
    kPinDelay = 2;
    int differ = 0;
    Tally t2;
    for (size_t ji = 0; ji < jobs.size(); ji++)
    {
        const Job& j = jobs[ji];
        std::vector<size_t> segStart;
        std::vector<MCycle> p = FlatProg(j, segStart);
        RunResult b = RunProg(j.cfg, p);
        Account(j, b, segStart, t2, nullptr);
        for (size_t si = 0; si < j.segs.size(); si++)
        {
            bool same = true;
            for (size_t i = segStart[si]; same && i < segStart[si] + j.segs[si].prog.size(); i++)
                same = Wait(b.cyc[i], j.cfg.turbo) == Wait(base[ji].cyc[i], j.cfg.turbo);
            if (!same) differ++;
        }
    }
    kPinDelay = saved;

    bool ok = true;
    ok &= Check("all simulations finished, Z80 clock edges alternate", t.edgeBad == 0);
    ok &= Check("every test started at its fclk (sync not late)", t.late == 0);
    ok &= Check("every sampled byte = memory (DRAM bus or cache register)", t.dataBad == 0);
    ok &= Check("every write reached the DRAM", t.writeBad == 0);
    ok &= Check("14 MHz: every dram_beg is at T2's falling-edge strobe", t.reqBad == 0);
    char nm[96];
    snprintf(nm, sizeof nm, "pin delay 2: data and writes still correct");
    ok &= Check(nm, t2.dataBad == 0 && t2.writeBad == 0 && t2.edgeBad == 0);
    // reported, not a check: the refused-cycle race (README) moves with the pin delay
    printf("  pin delay 2 changes the waits of %d tests (refused-cycle race, see README)\n", differ);
    printf("  tests where stall357 fired at 3.5 / 7 MHz: %d\n", t.stall357Segs);
    return ok;
}

// ---------------------------------------------------------------------------
// CPU cache fill and retention (cache): when the cache fills, what clears it, what a CPU
// write does to it. Each scenario is one simulation from FPGA configuration (cache RAM
// zeroed = every entry invalid), at 14 and 3.5 MHz, on border lines (no video load).
// A read's verdict: "stale" = the Z80 got a byte that is not in DRAM at that moment.

struct CacheScenario { const char* name; const char* what; const char* prog; };
static const CacheScenario kCacheScenarios[] = {
    {"fill-while-off", "reads with CACHE_CONFIG = 0 fill entries; enabled later they answer with the old word",
     "CE.0,RD.C010,RD.C011,RD.C021,PK.C010.A1,PK.C011.A2,PK.C021.A3,PK.C030.A4,CE.F,RD.C010,RD.C011,RD.C021,RD.C030"},
    {"off-keeps", "switching the cache off does not clear it",
     "CE.F,RD.C040,CE.0,PK.C040.B1,CE.F,RD.C040"},
    {"uncached-refills", "a read in a window without the cache refills the entry from DRAM",
     "CE.F,RD.C050,PK.C050.B2,CE.0,RD.C050,PK.C050.B3,CE.F,RD.C050"},
    {"other-window-fills", "a window without the enable bit fills entries an enabled window hits",
     "CE.8,RD.8060,PK.8060.B4,CE.4,RD.8060"},
    {"hit-no-refill", "a hit does not refill: the stale word stays until a DRAM read",
     "CE.F,RD.C0C0,PK.C0C0.BB,RD.C0C0,RD.C0C0,CE.0,RD.C0C0"},
    {"write-off-invalidates", "a CPU write with the cache off invalidates the entry it hits",
     "CE.0,RD.C070,PK.C071.B5,WR.C070.55,CE.F,RD.C071,RD.C070"},
    {"write-on-invalidates", "a CPU write with the cache on invalidates the whole word",
     "CE.F,RD.C080,PK.C081.B6,WR.C080.66,RD.C081,RD.C080"},
    {"write-other-tag", "a CPU write to the same index under another tag leaves the entry",
     "CE.F,RD.C090,PK.C090.B7,WR.8090.77,RD.C090,RD.8090"},
    {"reset-keeps-off", "a reset does not clear the cache (filled with the cache off)",
     "CE.0,RD.C0A0,PK.C0A0.B9,RST1,ID8,RST0,CE.F,RD.C0A0"},
    {"reset-keeps-on", "a reset does not clear the cache (filled with the cache on); reset sets CACHE_CONFIG = 0",
     "CE.F,RD.C0B0,PK.C0B0.BA,RST1,ID8,RST0,CE.F,RD.C0B0"},
};

static bool RunCache(const std::string& dir)
{
    FILE* f = nullptr;
    if (!dir.empty())
    {
        std::string path = dir + "/cache-retention.txt";
        f = fopen(path.c_str(), "w");
        if (!f) { perror(path.c_str()); return false; }
        fprintf(f, "# Generated by tools/machines/tsconf/rtl-sim (tsconf-cpu-sim cache). Format: README.md,\n");
        fprintf(f, "# section \"CPU cache fill and retention\". pin_delay=%d mem_config=%02X pages=%02X,%02X,%02X,%02X\n", kPinDelay, kMemConf, kPage[0], kPage[1], kPage[2], kPage[3]);
        fprintf(f, "# DRAM at start: page %02X (8000-BFFF) 00, page %02X (C000-FFFF) byte(a) = (a * 7 + 3) & 7F; cache RAM zeroed (all invalid)\n", kPage[2], kPage[3]);
    }
    bool ok = true;
    for (const CacheScenario& sc : kCacheScenarios)
    {
        const std::vector<MCycle> body = ParseProg(sc.prog);
        std::string results[2];
        for (int turbo : {2, 0})
        {
            RunCfg c; c.turbo = turbo; c.vconf = 0x83; c.vpage = 0x10;
            RunResult r = RunOne(c, FAt(8, 20), turbo >= 2 ? 0 : -1, body);
            ok &= r.done && r.edgesOk;
            std::string line;
            char buf[48];
            for (const CycleRec& x : r.cyc)
            {
                if (x.mc.kind != Kind::Rd) continue;
                // H = hit (no DRAM request), M = miss (a DRAM read); "!" = stale (got != DRAM)
                snprintf(buf, sizeof buf, " %04X=%02X/%02X%c%s", x.mc.addr, x.got, x.mem, x.req >= 0 ? 'M' : 'H',
                         x.got == x.mem ? "" : "!");
                line += buf;
            }
            results[turbo >= 2 ? 0 : 1] = line;
        }
        // the cache does not depend on the clock: the same bytes, hits and misses at 14 and 3.5 MHz
        const bool same = results[0] == results[1];
        ok &= same;
        printf("%-22s %s\n  prog %s\n  14   %s\n  3.5  %s\n", sc.name, sc.what, sc.prog, results[0].c_str(), results[1].c_str());
        if (f) fprintf(f, "case %s\n  prog %s\n  reads%s\n", sc.name, sc.prog, results[0].c_str());
    }
    if (f) fclose(f);
    Check("cache scenarios finished, 14 and 3.5 MHz give the same bytes, hits and misses", ok);
    return ok;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s sanity | all <results-dir> | trace <mhz> <vconf-hex> <line> <dot> <phase> <prog> [cache-hex] | cache [results-dir]\n", argv[0]);
        return 1;
    }
    std::string cmd = argv[1];
    if (const char* e = getenv("CPU_SIM_PIN_DELAY")) kPinDelay = atoi(e);
    if (cmd == "trace" && argc >= 8)
    {
        RunCfg c;
        std::string mhz = argv[2];
        c.turbo = mhz == "14" ? 2 : (mhz == "7" ? 1 : 0);
        c.vconf = static_cast<uint8_t>(strtol(argv[3], nullptr, 16));
        const int mode = c.vconf & 3;
        c.vpage = mode == 0 ? 0x05 : mode == 1 ? 0x40 : mode == 2 ? 0x60 : 0x10;
        if (argc >= 9) c.cacheEn = static_cast<uint8_t>(strtol(argv[8], nullptr, 16));
        std::vector<MCycle> prog = {Sync(FAt(atoi(argv[4]), atoi(argv[5])), atoi(argv[6]))};
        std::vector<MCycle> body = ParseProg(argv[7]);
        prog.insert(prog.end(), body.begin(), body.end());
        FILE* tmp = tmpfile();
        RunResult r = RunProg(c, prog, tmp);
        rewind(tmp);
        char line[512];
        bool on = false;
        while (fgets(line, sizeof line, tmp))
        {
            if (!on && strstr(line, " cyc=1 T=1")) on = true;
            if (on) fputs(line, stdout);
        }
        fclose(tmp);
        for (size_t i = 1; i < r.cyc.size(); i++)
            printf("cycle %zu %s t1=%ld end=%ld wait=%d req=%ld sample=%c%d ok=%d write=%d stalls=%d\n", i, Token(r.cyc[i].mc).c_str(),
                   r.cyc[i].t1, r.cyc[i].end, Wait(r.cyc[i], c.turbo), r.cyc[i].req < 0 ? -1 : r.cyc[i].req - r.cyc[i].t1,
                   r.cyc[i].sampleSrc ? r.cyc[i].sampleSrc : '-', r.cyc[i].samplePhase, r.cyc[i].dataOk, r.cyc[i].writeOk, r.cyc[i].stalls);
        return 0;
    }
    if (cmd == "cache")
        return RunCache(argc >= 3 ? argv[2] : "") ? 0 : 4;
    bool ok = RunSanity();
    if (cmd == "all" && argc >= 3)
    {
        if (!ok) { fprintf(stderr, "self-checks failed; results not written\n"); return 4; }
        ok &= RunAll(argv[2]);
    }
    return ok ? 0 : 4;
}
