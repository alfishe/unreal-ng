// Behavioral Z80 bus model driving the BaseConf zclock/zmem/arbiter RTL.
#include "Vtb.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>

enum Kind { M1, MR, MW, IOR, IOW, INTL };
struct Cyc { Kind k; uint16_t addr; int n; };
struct Instr { std::string name; std::vector<Cyc> cyc; };

static int cycLen(const Cyc& c) {
    switch (c.k) { case M1: return 4; case MR: case MW: return 3; case IOR: case IOW: return 4; case INTL: return c.n; }
    return 0;
}

struct Bus { uint16_t a = 0; int mreq = 1, iorq = 1, m1 = 1, rfsh = 1, rd = 1, wr = 1; };

struct Sim {
    Vtb* t;
    Bus bus, pend; bool hasPend = false;
    std::vector<Instr> prog;
    size_t ii = 0, ci = 0; int T = 0; // T=0 before start
    bool started = false;
    long n = 0;
    uint16_t ir = 0x3F00;
    // log
    std::vector<long> instrStart; std::vector<size_t> instrIdx;
    std::vector<long> riseTimes; // all rising edges
    long lastIntStart = -1;
    std::vector<long> intTimes;
    int prevZclk = 0;
    struct CS { long t1; int kind; int ph; long len; int dbeg; int dbegPh; int next0; int tIdx; };
    std::vector<CS> cstats; long curT1 = -1; int curPh = 0; int curKind = -1; int curDbeg = 0; int curDbegPh = -1; int curNext0 = 0;
    int ccPhase() { return t->cbeg_o ? 0 : t->post_cbeg_o ? 1 : t->pre_cend_o ? 2 : 3; }

    void applyBus() {
        t->a = bus.a; t->mreq_n = bus.mreq; t->iorq_n = bus.iorq; t->m1_n = bus.m1;
        t->rfsh_n = bus.rfsh; t->rd_n = bus.rd; t->wr_n = bus.wr;
    }
    void onRise() {
        // advance T state
        if (!started) { started = true; ii = 0; ci = 0; T = 1; }
        else {
            T++;
            if (T > cycLen(prog[ii].cyc[ci])) {
                T = 1; ci++;
                if (ci >= prog[ii].cyc.size()) { ci = 0; ii = (ii + 1) % prog.size(); }
            }
        }
        const Cyc& c = prog[ii].cyc[ci];
        if (T == 1) {
            if (curT1 >= 0) cstats.push_back({curT1, curKind, curPh, n - curT1, curDbeg, curDbegPh, curNext0, 0});
            curT1 = n; curPh = ccPhase(); curKind = c.k; curDbeg = 0; curDbegPh = -1; curNext0 = 0;
        }
        if (T == 1 && ci == 0) { instrStart.push_back(n); instrIdx.push_back(ii); }
        riseTimes.push_back(n);
        pend = bus;
        if (T == 1) { pend.a = c.addr; pend.rfsh = 1; if (c.k == M1) pend.m1 = 0; }
        if (c.k == M1 && T == 3) { pend.mreq = 1; pend.rd = 1; pend.m1 = 1; pend.a = ir; pend.rfsh = 0; ir = (ir & 0xFF80) | ((ir + 1) & 0x7F); }
        if ((c.k == IOR || c.k == IOW) && T == 2) { pend.iorq = 0; if (c.k == IOR) pend.rd = 0; else pend.wr = 0; }
        hasPend = true;
    }
    void onFall() {
        if (!started) return;
        const Cyc& c = prog[ii].cyc[ci];
        pend = bus;
        switch (c.k) {
        case M1: if (T == 1) { pend.mreq = 0; pend.rd = 0; } if (T == 3) pend.mreq = 0; if (T == 4) pend.mreq = 1; break;
        case MR: if (T == 1) { pend.mreq = 0; pend.rd = 0; } if (T == 3) { pend.mreq = 1; pend.rd = 1; } break;
        case MW: if (T == 1) pend.mreq = 0; if (T == 2) pend.wr = 0; if (T == 3) { pend.mreq = 1; pend.wr = 1; } break;
        case IOR: case IOW: if (T == 4) { pend.iorq = 1; pend.rd = 1; pend.wr = 1; } break;
        default: break;
        }
        hasPend = true;
    }
    void step() {
        t->fclk = 1; t->eval();
        if (hasPend) { bus = pend; hasPend = false; applyBus(); t->eval(); }
        if (t->dram_beg_o) { curDbeg = 1; curDbegPh = ccPhase(); }
        if (t->dram_beg_o && !t->cpu_next_o) curNext0 = 1;
        if (!t->int_n) { if (lastIntStart != n - 1) intTimes.push_back(n); lastIntStart = n; }
        t->fclk = 0; t->eval();
        int z = t->zclk_out;
        if (prevZclk == 1 && z == 0) onRise();
        else if (prevZclk == 0 && z == 1) onFall();
        prevZclk = z;
        n++;
    }
};

static std::vector<Instr> P;
static void add(const char* nm, std::vector<Cyc> c) { P.push_back({nm, c}); }

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    std::string test = argc > 1 ? argv[1] : "nop14";
    int turbo = 2, raster = 0, go = 0, bw = 0, rom0 = 0, p7ffd = 0, ctype = 0;
    long cycles = 200000;
    for (int i = 2; i < argc; i++) {
        if (!strncmp(argv[i], "turbo=", 6)) turbo = atoi(argv[i] + 6);
        if (!strncmp(argv[i], "raster=", 7)) raster = atoi(argv[i] + 7);
        if (!strncmp(argv[i], "go=", 3)) go = atoi(argv[i] + 3);
        if (!strncmp(argv[i], "bw=", 3)) bw = atoi(argv[i] + 3);
        if (!strncmp(argv[i], "rom0=", 5)) rom0 = atoi(argv[i] + 5);
        if (!strncmp(argv[i], "p7ffd=", 6)) p7ffd = atoi(argv[i] + 6);
        if (!strncmp(argv[i], "ctype=", 6)) ctype = atoi(argv[i] + 6);
        if (!strncmp(argv[i], "cycles=", 7)) cycles = atol(argv[i] + 7);
    }
    uint16_t base = 0x8000;
    // programs
    if (test == "nop") { for (int i = 0; i < 64; i++) add("NOP", {{M1, (uint16_t)(base + i), 0}}); }
    else if (test == "nopodd") { for (int i = 0; i < 64; i++) add("NOP", {{M1, (uint16_t)(base + 1 + i), 0}}); }
    else if (test == "nopsame") { for (int i = 0; i < 64; i++) add("NOP", {{M1, base, 0}}); } // JR $-like same addr
    else if (test == "ldahl") { // LD A,(HL) ; INC HL  (HL walks 0xC000..)
        for (int i = 0; i < 32; i++) {
            uint16_t pc = base + 2 * i;
            add("LD A,(HL)", {{M1, pc, 0}, {MR, (uint16_t)(0xC000 + i), 0}});
            add("INC HL", {{M1, (uint16_t)(pc + 1), 0}, {INTL, 0x3F00, 2}});
        }
    }
    else if (test == "ldahl_seq") { // only LD A,(HL) with walking PC and HL (idealized)
        for (int i = 0; i < 64; i++) add("LD A,(HL)", {{M1, (uint16_t)(base + i), 0}, {MR, (uint16_t)(0xC000 + 2 * i), 0}});
    }
    else if (test == "ldhla") {
        for (int i = 0; i < 32; i++) {
            uint16_t pc = base + 2 * i;
            add("LD (HL),A", {{M1, pc, 0}, {MW, (uint16_t)(0xC000 + i), 0}});
            add("INC HL", {{M1, (uint16_t)(pc + 1), 0}, {INTL, 0x3F00, 2}});
        }
    }
    else if (test == "outfe") { // OUT (#FE),A = M1, MR(n), IOW
        for (int i = 0; i < 32; i++) { uint16_t pc = base + 2 * i;
            add("OUT (FE),A", {{M1, pc, 0}, {MR, (uint16_t)(pc + 1), 0}, {IOW, 0x00FE, 0}}); }
    }
    else if (test == "outfffd") { // OUT (C),A with BC=#FFFD: ED M1, M1, IOW
        for (int i = 0; i < 32; i++) { uint16_t pc = base + 2 * i;
            add("OUT (C),A", {{M1, pc, 0}, {M1, (uint16_t)(pc + 1), 0}, {IOW, 0xFFFD, 0}}); }
    }
    else if (test == "infffd") {
        for (int i = 0; i < 32; i++) { uint16_t pc = base + 2 * i;
            add("IN A,(C)", {{M1, pc, 0}, {M1, (uint16_t)(pc + 1), 0}, {IOR, 0xFFFD, 0}}); }
    }
    else if (test == "c_nop") { // NOPs in contended memory 0x4000..
        for (int i = 0; i < 4096; i++) add("NOP", {{M1, (uint16_t)(0x4000 + i), 0}});
    }
    else if (test == "c_probe") {
        std::string pk = getenv("PROBE") ? getenv("PROBE") : "m1";
        for (int i = 0; i < 2000; i++) {
            add("X", {{M1, 0x8000, 0}, {INTL, 0x8000, 1 + (i * 7) % 13}});
            if (pk == "m1") add("C", {{M1, 0x4000, 0}});
            else if (pk == "mr") add("C", {{M1, 0x8001, 0}, {MR, 0x4000, 0}});
            else if (pk == "mw") add("C", {{M1, 0x8001, 0}, {MW, 0x4000, 0}});
            else if (pk == "io_fe") add("C", {{M1, 0x8001, 0}, {IOW, 0x00FE, 0}});
            else if (pk == "io_40ff") add("C", {{M1, 0x8001, 0}, {IOW, 0x40FF, 0}});
            else if (pk == "io_40fe") add("C", {{M1, 0x8001, 0}, {IOW, 0x40FE, 0}});
            else if (pk == "int") add("C", {{M1, 0x8001, 0}, {INTL, 0x4000, 1}});
            else if (pk == "int2") add("C", {{M1, 0x8001, 0}, {INTL, 0x4000, 2}});
            else if (pk == "c000") add("C", {{M1, 0x8001, 0}, {MR, 0xC000, 0}});
        }
    }
    else if (test == "c_nop_unc") { for (int i = 0; i < 4096; i++) add("NOP", {{M1, (uint16_t)(0x8000 + i), 0}}); }
    else if (test == "rand") {
        srand(12345);
        uint16_t pc = 0x8000;
        for (int i = 0; i < 4000; i++) {
            int r = rand() % (getenv("WITHIO") ? 11 : 6); uint16_t hl = 0xC000 + (rand() % 64);
            switch (r) {
            case 0: add("NOP", {{M1, pc, 0}}); pc += 1; break;
            case 1: add("LD A,(HL)", {{M1, pc, 0}, {MR, hl, 0}}); pc += 1; break;
            case 2: add("LD (HL),A", {{M1, pc, 0}, {MW, hl, 0}}); pc += 1; break;
            case 3: add("INC HL", {{M1, pc, 0}, {INTL, 0x3F00, 2}}); pc += 1; break;
            case 4: add("LD A,n", {{M1, pc, 0}, {MR, (uint16_t)(pc + 1), 0}}); pc += 2; break;
            case 6: add("OUT (FE),A", {{M1, pc, 0}, {MR, (uint16_t)(pc + 1), 0}, {IOW, 0x00FE, 0}}); pc += 2; break;
            case 7: add("OUT (C),A", {{M1, pc, 0}, {M1, (uint16_t)(pc + 1), 0}, {IOW, 0xFFFD, 0}}); pc += 2; break;
            case 8: add("IN A,(C)", {{M1, pc, 0}, {M1, (uint16_t)(pc + 1), 0}, {IOR, 0xBFFD, 0}}); pc += 2; break;
            case 9: add("OUT (FD),A", {{M1, pc, 0}, {MR, (uint16_t)(pc + 1), 0}, {IOW, 0xFFFD, 0}}); pc += 2; break;
            case 10: add("IN A,(FE)", {{M1, pc, 0}, {MR, (uint16_t)(pc + 1), 0}, {IOR, 0x7FFE, 0}}); pc += 2; break;
            case 5: add("INC (HL)", {{M1, pc, 0}, {MR, hl, 0}, {INTL, hl, 1}, {MW, hl, 0}}); pc += 1; break;
            }
            if (pc > 0xBF00) pc = 0x8000;
        }
    }
    else { fprintf(stderr, "unknown test\n"); return 1; }
    Vtb* t = new Vtb;
    Sim s; s.t = t; s.prog = P;
    t->turbo = turbo; t->modes_raster = raster; t->go_mode = go; t->bw = bw; t->win0_rom = rom0; t->p7ffd = p7ffd; t->contend_type = ctype;
    t->rst_n = 0; s.applyBus();
    for (int i = 0; i < 64; i++) s.step();
    t->rst_n = 1;
    std::string mode = argc > 2 ? "" : "";
    bool trace = getenv("TRACE") != nullptr;
    long traceFrom = getenv("TRACE_FROM") ? atol(getenv("TRACE_FROM")) : 0;
    long switchAt = getenv("SWITCH_AT") ? atol(getenv("SWITCH_AT")) : -1;
    int switchTo = getenv("SWITCH_TO") ? atoi(getenv("SWITCH_TO")) : 2;
    for (long i = 0; i < cycles; i++) {
        if (i == switchAt) { t->turbo = switchTo; s.cstats.clear(); }
        if (trace && s.n >= traceFrom && s.n < traceFrom + 400)
            printf("n=%ld cc=%d%d%d%d zp=%d zn=%d zclk=%d stall=%d cpu_stall=%d next=%d strobe=%d dbeg=%d a=%04x mreq=%d rd=%d m1=%d rfsh=%d iorq=%d h=%d v=%d cont=%d dbg=%x\n",
                s.n, t->cbeg_o, t->post_cbeg_o, t->pre_cend_o, t->cend_o, t->zpos, t->zneg, t->zclk_out, t->stall_total, t->cpu_stall,
                t->cpu_next_o, t->cpu_strobe_o, t->dram_beg_o, t->a, t->mreq_n, t->rd_n, t->m1_n, t->rfsh_n, t->iorq_n, t->hcount_o, t->vcount_o, t->contend, t->dbg_o);
        s.step();
    }
    // instruction lengths histogram (skip first 20 instructions)
    if (test.rfind("c_", 0) == 0 && !getenv("CSTATS")) {
        // contention report: print each instruction start relative to INT, and length
        long intT = -1;
        for (long x : s.intTimes) { intT = x; break; }
        printf("int_start first at n=%ld (all:", intT);
        for (size_t k = 0; k < s.intTimes.size() && k < 4; k++) printf(" %ld", s.intTimes[k]);
        printf(")\n");
        // print rise-edge fclk phase relative to int (mod 8)
        size_t lim = s.instrStart.size();
        // find instructions whose length != nominal (32 fclk at 3.5MHz)
        int shown = 0;
        long firstInt = s.intTimes.size() > 0 ? s.intTimes[0] : 0;
        for (size_t k = 1; k < lim; k++) {
            long len = s.instrStart[k] - s.instrStart[k - 1];
            if (s.instrStart[k - 1] < firstInt) continue;
            if (len != 32 && shown < 40) {
                printf("instr start n=%ld  rel_int_fclk=%ld  (T35=%.3f)  len_fclk=%ld (T35=%.2f)\n", s.instrStart[k - 1], s.instrStart[k - 1] - firstInt,
                    (s.instrStart[k - 1] - firstInt) / 8.0, len, len / 8.0);
                shown++;
            }
        }
        if (test == "c_probe") {
            long T0 = firstInt + 15; // Fuse-convention frame origin (see report)
            int base = getenv("PROBE") && std::string(getenv("PROBE")) != "m1" ? 4 : 0; // T of the M1 before the probed cycle
            std::map<long, long> m; // T of probed cycle -> extra
            for (size_t k = 0; k + 1 < s.instrStart.size(); k++) {
                if (P[s.instrIdx[k]].name != "C") continue;
                long st = s.instrStart[k] - T0; if (st % 8) { printf("odd start %ld\n", st); }
                long T = st / 8 + base; // T at which the probed cycle begins (for m1: the M1 itself)
                long len = s.instrStart[k + 1] - s.instrStart[k];
                long nominal = 0; std::string pk = getenv("PROBE") ? getenv("PROBE") : "m1";
                if (pk == "m1") nominal = 32; else if (pk == "mr" || pk == "mw" || pk == "c000") nominal = 56; else if (pk.rfind("io", 0) == 0) nominal = 64; else if (pk == "int") nominal = 40; else nominal = 48;
                long line = (T - 14335 + 224 * 64) / 224 - 64; long off = (T - 14335) - line * 224;
                if (getenv("RASTER128")) { line = (T - 14361 + 228 * 64) / 228 - 64; off = (T - 14361) - line * 228; }
                if (line >= 0 && line < 192 && off >= -6 && off < 134) { long key = off + 100; long v = (len - nominal) / 8; if (m.count(key) && m[key] != v) m[key] = 99; else m[key] = v; }
            }
            for (auto& e : m) printf("%+ld:%ld ", e.first - 100, e.second); printf("\n");
        }
        // first rising edge after int
        for (long r : s.riseTimes) if (r >= firstInt) { printf("first rise after int_start at n=%ld (delta %ld fclk)\n", r, r - firstInt); break; }
        return 0;
    }
    if (getenv("CSTATS")) {
        static const char* kn[] = {"M1","MR","MW","IOR","IOW","INT"};
        std::map<std::string, long> h;
        for (size_t k = 10; k < s.cstats.size(); k++) {
            auto& c = s.cstats[k];
            long T0 = s.intTimes.empty() ? 0 : s.intTimes[0] + 15;
            char key[160]; snprintf(key, sizeof key, "%s t14par=%ld T1ph=%d dbeg=%d dbegPh=%d next0=%d len=%ld", kn[c.kind], (((c.t1 - T0) / 2) % 2 + 2) % 2, c.ph, c.dbeg, c.dbegPh, c.next0, c.len);
            h[key]++;
        }
        for (auto& e : h) printf("%s  x%ld\n", e.first.c_str(), e.second);
    }
    std::map<std::string, std::map<long, long>> hist;
    for (size_t k = 20; k + 1 < s.instrStart.size(); k++) {
        long len = s.instrStart[k + 1] - s.instrStart[k];
        hist[P[s.instrIdx[k]].name][len]++;
    }
    for (auto& h : hist) {
        printf("%-12s", h.first.c_str());
        for (auto& e : h.second) printf("  %ld fclk (%.1f T14) x%ld", e.first, e.first / 2.0, e.second);
        printf("\n");
    }
    // sequence print of first 24 instr after warmup
    printf("seq:");
    for (size_t k = 20; k < 44 && k + 1 < s.instrStart.size(); k++) printf(" %s=%ld", P[s.instrIdx[k]].name.c_str(), s.instrStart[k + 1] - s.instrStart[k]);
    printf("\n");
    return 0;
}
