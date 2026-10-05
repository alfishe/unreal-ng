// TS-Conf video RTL simulation harness (Verilator testbench).
//
// Drives tbtop.v (clock + arbiter + video_top of the ZX-Evo TS-Conf firmware) with a
// behavioral DRAM backed by a 4 MB memory image, writes the video registers through
// the same strobes top.v uses, runs to a chosen raster line and captures the palette
// index that enters the CRAM (video_out.vdata) for every pixel of that line.
//
// Usage:
//   tsconf-video-sim sanity                      run the self-checks only
//   tsconf-video-sim all <results-dir>           self-checks, then write zx-gxoffs.txt / txt-gxoffs.txt
//   tsconf-video-sim line <mode> <gx> [vconf]    print one captured line (mode: zx, txt, 16c, 256c)
//   tsconf-video-sim tsulatch [results-dir]      TSU latch cases: print the traces, write tsu-latch.txt
//
// Exit code 0 = all self-checks passed.

#include "Vtbtop.h"
#include "verilated.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// DRAM image: 4 MB, 2M words of 16 bits, word address as the RTL uses it
// (byte address = word * 2, low byte first).

static std::vector<uint8_t> g_mem(4u * 1024u * 1024u, 0);

extern "C" int dram_read(int addr)
{
    uint32_t w = static_cast<uint32_t>(addr) & 0x1FFFFFu;
    return g_mem[w * 2] | (g_mem[w * 2 + 1] << 8);
}

static void Poke(uint32_t byteAddr, uint8_t v) { g_mem[byteAddr & 0x3FFFFFu] = v; }

// ---------------------------------------------------------------------------
// Simulation

struct RunConfig
{
    uint8_t vconf = 0;
    uint8_t vpage = 5;
    uint16_t gxOffs = 0;
    uint16_t gyOffs = 0;
    uint8_t palsel = 0;
    uint8_t border = 0xEE;   // index no graphics mode here can produce with PAL_SEL = 0
    int windowLine = 9;      // 0-based line inside the graphics window
    bool cpuReq = false;     // CPU requesting DRAM every cycle (contention check)
};

// One captured line: 'samples' holds one palette index per output pixel.
// Lo-res modes: one pixel per 7 MHz dot. Hi-res (TXT): two pixels per dot.
struct Line
{
    std::vector<uint8_t> samples;
    int pixelsPerDot = 1;
};

// Window geometry per V_CONFIG[7:6] (video_mode.v hp_beg / hp_end / vp_beg, 50 Hz, 320-line frame).
static const int kHpBeg[4] = {140, 108, 108, 88};
static const int kHpEnd[4] = {396, 428, 428, 448};
static const int kVpBeg[4] = {80, 76, 56, 32};

// Border dots captured on each side of the window.
static const int kBorderDots = 8;
// Pipeline delay (in dots) between the raster counter and the index at the CRAM address.
// Found by the G_X_OFFS = 0 self-checks; kept fixed for all runs.
static int g_pipeDelay = -1;

static Line RunLine(const RunConfig& cfg, int pipeDelay)
{
    auto ctx = std::make_unique<VerilatedContext>();
    ctx->randReset(0);
    auto top = std::make_unique<Vtbtop>(ctx.get());

    auto tick = [&]() {
        top->clk = 0; top->eval();
        top->clk = 1; top->eval();
    };

    top->clk = 0;
    top->res = 1;
    top->cpu_req = cfg.cpuReq ? 1 : 0;
    top->eval();
    for (int i = 0; i < 8; i++) tick();
    top->res = 0;
    tick();

    auto write = [&](CData& strobe, uint8_t value) {
        top->xt_wr_data = value;
        strobe = 1;
        tick();
        strobe = 0;
        tick();
    };
    write(top->vconf_wr, cfg.vconf);
    write(top->vpage_wr, cfg.vpage);
    write(top->gx_offsl_wr, cfg.gxOffs & 0xFF);
    write(top->gx_offsh_wr, (cfg.gxOffs >> 8) & 1);
    write(top->gy_offsl_wr, cfg.gyOffs & 0xFF);
    write(top->gy_offsh_wr, (cfg.gyOffs >> 8) & 1);
    write(top->palsel_wr, cfg.palsel);
    write(top->border_wr, cfg.border);
    write(top->tsconf_wr, 0);

    int rres = cfg.vconf >> 6;
    int targetV = kVpBeg[rres] + cfg.windowLine;
    int firstDot = kHpBeg[rres] - kBorderDots;
    int lastDot = kHpEnd[rres] + kBorderDots;  // exclusive
    bool hires = (cfg.vconf & 3) == 3;

    Line line;
    line.pixelsPerDot = hires ? 2 : 1;
    line.samples.assign((lastDot - firstDot) * line.pixelsPerDot, 0);

    // Run until the target line has been fully emitted (+ pipeline slack).
    // Sampling: before each rising edge (clk low, settled) vdata_o is what the CRAM
    // address register takes on that edge. A 7 MHz dot spans 4 edges (c0..c3).
    // Lo-res: vdata is constant over the dot, take the c0 edge. Hi-res: the first
    // pixel of a pair is on the c0/c1 edges, the second on the c2/c3 edges.
    long guard = 0;
    while (true)
    {
        top->clk = 0;
        top->eval();
        int v = top->ray_y;
        int h = top->ray_x;
        // Captured dot index refers to the raster dot whose pixel arrives now: h - pipeDelay.
        if (v == targetV)
        {
            int dot = h - pipeDelay;
            if (dot >= firstDot && dot < lastDot)
            {
                int base = (dot - firstDot) * line.pixelsPerDot;
                if (top->c0_o) line.samples[base] = top->vdata_o;
                if (hires && top->c2_o) line.samples[base + 1] = top->vdata_o;
            }
        }
        if (v == targetV + 1 && h > pipeDelay + 4) break;
        top->clk = 1;
        top->eval();
        if (++guard > 4000000) { fprintf(stderr, "error: raster never reached line %d\n", targetV); exit(2); }
    }
    top->final();
    return line;
}

// ---------------------------------------------------------------------------
// Memory images

static const uint8_t kZxPage = 0x05;
static const uint8_t kTxtPage = 0x10;

static uint8_t ZxPixelByte(int col, int y) { return static_cast<uint8_t>(0x81 | ((col & 0x1F) << 1) | ((y & 1) << 6)); }
static uint8_t ZxAttrByte(int col, int row)
{
    int ink = col & 7;
    int paper = (ink + 1 + (col >> 3)) & 7;
    return static_cast<uint8_t>(((row & 1) << 6) | (paper << 3) | ink);
}

static void FillZx()
{
    uint32_t base = kZxPage * 16384u;
    for (int y = 0; y < 192; y++)
        for (int c = 0; c < 32; c++)
        {
            uint32_t off = ((y & 0xC0) << 5) | ((y & 7) << 8) | ((y & 0x38) << 2) | c;
            Poke(base + off, ZxPixelByte(c, y));
        }
    for (int r = 0; r < 24; r++)
        for (int c = 0; c < 32; c++)
            Poke(base + 0x1800 + r * 32 + c, ZxAttrByte(c, r));
}

static uint8_t TxtCode(int col) { return static_cast<uint8_t>(0x80 | (col & 0x7F)); }
static uint8_t TxtAttr(int col)
{
    int ink = col & 15;
    int paper = (ink + 1 + ((col >> 4) & 7)) & 15;
    return static_cast<uint8_t>((paper << 4) | ink);
}

// Glyph line l of character ch. A bijection of ch for every line, different from ch
// itself, so a raw character code shown as pixels can be told from its glyph.
static uint8_t TxtGlyph(int ch, int l) { return static_cast<uint8_t>(ch * 0x1D + 0x35 + l * 0x40); }

static void FillTxt()
{
    uint32_t base = kTxtPage * 16384u;
    for (int r = 0; r < 64; r++)
        for (int c = 0; c < 128; c++)
        {
            Poke(base + r * 256 + c, TxtCode(c));
            Poke(base + r * 256 + 128 + c, TxtAttr(c));
        }
    // Font at page V_PAGE ^ 1, 8 bytes per character.
    uint32_t font = (kTxtPage ^ 1) * 16384u;
    for (int ch = 0; ch < 256; ch++)
        for (int l = 0; l < 8; l++)
            Poke(font + ch * 8 + l, TxtGlyph(ch, l));
}

static const uint8_t kHcPage = 0x40;  // 16c: page[7:3], 8 pages (0x40..0x47)
static const uint8_t kXcPage = 0x60;  // 256c: page[7:4], 16 pages (0x60..0x6F)
static uint8_t HcPixel(int x) { return static_cast<uint8_t>((x ^ (x >> 4) ^ ((x >> 8) * 3)) & 15); }
static uint8_t XcPixel(int x) { return static_cast<uint8_t>((x * 37 + (x >> 8) * 11 + 5) & 255); }

static void FillHc(int y)
{
    uint32_t base = (kHcPage & 0xF8) * 16384u + y * 256u;
    for (int x = 0; x < 512; x += 2)
        Poke(base + x / 2, static_cast<uint8_t>((HcPixel(x) << 4) | HcPixel(x + 1)));
}

static void FillXc(int y)
{
    uint32_t base = (kXcPage & 0xF0) * 16384u + y * 512u;
    for (int x = 0; x < 512; x++) Poke(base + x, XcPixel(x));
}

// ---------------------------------------------------------------------------
// Expected pictures (plain, unscrolled) for the self-checks

static std::vector<uint8_t> ExpectZx(int y)
{
    std::vector<uint8_t> out;
    for (int d = 0; d < 256; d++)
    {
        int c = d >> 3;
        uint8_t px = ZxPixelByte(c, y);
        uint8_t at = ZxAttrByte(c, y >> 3);
        bool ink = (px >> (7 - (d & 7))) & 1;
        out.push_back(static_cast<uint8_t>(((at >> 6) & 1) << 3 | (ink ? (at & 7) : ((at >> 3) & 7))));
    }
    return out;
}

static std::vector<uint8_t> ExpectTxt(int y, int pixels)
{
    std::vector<uint8_t> out;
    for (int p = 0; p < pixels; p++)
    {
        int c = p >> 3;
        uint8_t glyph = TxtGlyph(TxtCode(c), y & 7);
        uint8_t at = TxtAttr(c);
        bool ink = (glyph >> (7 - (p & 7))) & 1;
        out.push_back(ink ? (at & 15) : (at >> 4));
    }
    return out;
}

static int WindowDots(uint8_t vconf) { return kHpEnd[vconf >> 6] - kHpBeg[vconf >> 6]; }

static std::vector<uint8_t> WindowOf(const Line& l, uint8_t vconf)
{
    int n = WindowDots(vconf) * l.pixelsPerDot;
    int b = kBorderDots * l.pixelsPerDot;
    return std::vector<uint8_t>(l.samples.begin() + b, l.samples.begin() + b + n);
}

static int CountMismatch(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    int n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); i++) n += a[i] != b[i];
    return n;
}

// In hi-res (TXT) the plex carries 4-bit pixels and video_out prefixes PAL_SEL[3:0],
// so the border shows as {PAL_SEL[3:0], BORDER[3:0]} (PAL_SEL = 0 here).
static bool BorderClean(const Line& l, uint8_t vconf, uint8_t border)
{
    if ((vconf & 3) == 3) border &= 0x0F;
    int b = kBorderDots * l.pixelsPerDot;
    int n = WindowDots(vconf) * l.pixelsPerDot;
    for (int i = 0; i < b; i++)
        if (l.samples[i] != border || l.samples[b + n + i] != border) return false;
    return true;
}

static void PrintLine(const Line& l)
{
    for (size_t i = 0; i < l.samples.size(); i++) printf("%02X%c", l.samples[i], (i + 1) % 32 ? ' ' : '\n');
    printf("\n");
}

// Pipeline delay search: the delay for which the G_X_OFFS = 0 ZX line matches the plain picture.
static int FindPipeDelay()
{
    RunConfig cfg;
    cfg.vconf = 0x00;
    cfg.vpage = kZxPage;
    for (int d = 0; d < 32; d++)
    {
        Line l = RunLine(cfg, d);
        if (CountMismatch(WindowOf(l, cfg.vconf), ExpectZx(cfg.windowLine)) == 0 && BorderClean(l, cfg.vconf, cfg.border))
            return d;
    }
    return -1;
}

static bool Check(const char* name, bool ok)
{
    printf("  %-58s %s\n", name, ok ? "PASS" : "FAIL");
    return ok;
}

static bool RunSanity()
{
    bool all = true;
    printf("self-checks (pipeline delay %d dots):\n", g_pipeDelay);
    {
        RunConfig cfg; cfg.vconf = 0x00; cfg.vpage = kZxPage;
        Line l = RunLine(cfg, g_pipeDelay);
        all &= Check("ZX 256x192, G_X_OFFS=0 = plain ZX screen", CountMismatch(WindowOf(l, cfg.vconf), ExpectZx(cfg.windowLine)) == 0 && BorderClean(l, cfg.vconf, cfg.border));
    }
    for (uint8_t vc : {uint8_t(0x03), uint8_t(0x83)})
    {
        RunConfig cfg; cfg.vconf = vc; cfg.vpage = kTxtPage;
        Line l = RunLine(cfg, g_pipeDelay);
        char name[96];
        snprintf(name, sizeof name, "TXT V_CONFIG=%02X, G_X_OFFS=0 = plain text screen", vc);
        all &= Check(name, CountMismatch(WindowOf(l, cfg.vconf), ExpectTxt(cfg.windowLine, WindowDots(vc) * 2)) == 0 && BorderClean(l, cfg.vconf, cfg.border));
    }
    for (uint8_t vc : {uint8_t(0x01), uint8_t(0x02), uint8_t(0x81), uint8_t(0x82)})
    {
        bool okAll = true;
        for (int gx = 0; gx < 512; gx += (gx < 40 ? 1 : 37))
        {
            RunConfig cfg; cfg.vconf = vc; cfg.vpage = (vc & 3) == 1 ? kHcPage : kXcPage; cfg.gxOffs = static_cast<uint16_t>(gx);
            Line l = RunLine(cfg, g_pipeDelay);
            std::vector<uint8_t> exp;
            for (int d = 0; d < WindowDots(vc); d++)
            {
                int x = (d + gx) & 511;
                exp.push_back((vc & 3) == 1 ? HcPixel(x) : XcPixel(x));
            }
            bool ok = CountMismatch(WindowOf(l, vc), exp) == 0 && BorderClean(l, vc, cfg.border);
            if (!ok) printf("    mismatch at G_X_OFFS=%d\n", gx);
            okAll &= ok;
        }
        char name[96];
        snprintf(name, sizeof name, "%s V_CONFIG=%02X: G_X_OFFS = linear scroll by G_X_OFFS dots", (vc & 3) == 1 ? "16C" : "256C", vc);
        all &= Check(name, okAll);
    }
    return all;
}

static std::vector<int> OffsetList();

// DRAM contention must not change the picture: every run of the result set is
// repeated with the CPU requesting every DRAM cycle and compared.
static bool RunContentionCheck()
{
    int diffs = 0;
    for (uint8_t vc : {uint8_t(0x00), uint8_t(0x03), uint8_t(0x83)})
        for (int gx : OffsetList())
        {
            RunConfig a; a.vconf = vc; a.vpage = vc == 0 ? kZxPage : kTxtPage; a.gxOffs = static_cast<uint16_t>(gx);
            RunConfig b = a; b.cpuReq = true;
            if (RunLine(a, g_pipeDelay).samples != RunLine(b, g_pipeDelay).samples)
            {
                printf("    V_CONFIG=%02X G_X_OFFS=%d differs with CPU contention\n", vc, gx);
                diffs++;
            }
        }
    return Check("ZX/TXT results identical with CPU requesting every cycle", diffs == 0);
}

static std::vector<int> OffsetList()
{
    std::vector<int> v;
    for (int i = 0; i < 32; i++) v.push_back(i);
    for (int i : {32, 33, 34, 35, 36, 37, 38, 39, 40, 64, 65, 66, 67, 68, 128, 129, 130, 131, 255, 256, 257, 300, 508, 509, 510, 511}) v.push_back(i);
    return v;
}

static void WriteResults(const std::string& dir)
{
    struct Job { const char* file; std::vector<uint8_t> vconfs; uint8_t vpage; };
    std::vector<Job> jobs = {
        {"zx-gxoffs.txt", {0x00}, kZxPage},
        {"txt-gxoffs.txt", {0x03, 0x83}, kTxtPage},
    };
    for (const Job& job : jobs)
    {
        std::string path = dir + "/" + job.file;
        FILE* f = fopen(path.c_str(), "w");
        if (!f) { perror(path.c_str()); exit(3); }
        fprintf(f, "# Generated by tools/machines/tsconf/rtl-sim (tsconf-video-sim all). Format: see README.md.\n");
        fprintf(f, "# border_dots=%d border=EE (shows as 0E in hi-res) window_line=9 pal_sel=00 v_page=%02X\n", kBorderDots, job.vpage);
        for (uint8_t vc : job.vconfs)
            for (int gx : OffsetList())
            {
                RunConfig cfg; cfg.vconf = vc; cfg.vpage = job.vpage; cfg.gxOffs = static_cast<uint16_t>(gx);
                Line l = RunLine(cfg, g_pipeDelay);
                fprintf(f, "%02X %d %d", vc, gx, l.pixelsPerDot);
                for (uint8_t s : l.samples) fprintf(f, " %02X", s);
                fprintf(f, "\n");
            }
        fclose(f);
        printf("wrote %s\n", path.c_str());
    }
}

// ---------------------------------------------------------------------------
// TSU latch timing (tsulatch): the TSU draws line L during line L - 1 from ts_start; the tile registers
// T0/T1 G_PAGE, T0/T1 X_OFFS and PAL_SEL are latched at line_start (video_ports.v:153-165) and read by the
// TSU when it hands an object to the renderer (video_ts.v:162-171). A pass that runs past line_start draws
// its late tiles with line L's latch. Each case sets the "before" values, then writes the "after" values
// during line L - 1 (dot 420: after ts_start, before line_start), and captures TS line `line` (the window).
// TSU_CYCLES=1 in the environment prints the owner of each DRAM cycle at the start of the pass.

static const uint8_t kTsMapPage = 0x30;   // T_MAP_PAGE
static const uint8_t kTsVideoPage = 0xC0; // V_PAGE: zero memory, the graphics layer is index 0
static const uint8_t kTsSpritePage = 0xA0;
// Graphics bitmaps (tiles and sprites) at pages 80h..AFh: one byte per physical address
static uint8_t TsGfxByte(uint32_t a) { return static_cast<uint8_t>(a * 7 + (a >> 8) * 13 + (a >> 17) * 101); }

struct TsuRegs
{
    uint8_t palsel, t0gpage, t1gpage;
    uint16_t t0x, t1x;
};

struct TsuCase
{
    const char* name;
    uint8_t vconf;
    uint8_t tsconf;
    int s0;          // sprites in S0 (64x8 each, all on the line)
    int s1;          // sprites in S1
    TsuRegs before, after;
    int line = 20;   // captured TS line (window line)
};

// Sprite d (0-based over all layers): x, tile and palette
static uint16_t TsSpriteR1(int d) { return static_cast<uint16_t>(((d * 23) & 0x1FF) | (7 << 9)); }
static uint16_t TsSpriteR2(int d) { return static_cast<uint16_t>(((d * 8) & 0x3F) | ((d & 7) << 6) | ((d & 15) << 12)); }

struct TsuGo
{
    bool sprite;
    int x, page, pal, slots;
    bool late;
};

struct TsuRun
{
    std::vector<uint8_t> window;
    std::vector<TsuGo> gos;
    int split = -1;       // TSU DRAM cycles (tilemap + renderer) from ts_start to line_start
    int dotsToLine = 0;   // DRAM cycles from ts_start to line_start
    int videoBefore = 0;  // video DRAM cycles in that span
    int freeBefore = 0;   // free DRAM cycles in that span
    int tsuTotal = 0;     // TSU DRAM cycles of the whole pass
};

static void FillTsuMemory()
{
    std::fill(g_mem.begin(), g_mem.end(), 0);
    for (uint32_t a = 0x80u * 16384u; a < 0xB0u * 16384u; a++) g_mem[a] = TsGfxByte(a);
    // Tile map, every row the same: layer l column c = tile (c & 63) of bitmap row 1 + l, palette c & 3
    for (int row = 0; row < 64; row++)
        for (int l = 0; l < 2; l++)
            for (int c = 0; c < 64; c++)
            {
                uint16_t e = static_cast<uint16_t>((c & 63) | ((1 + l) << 6) | ((c & 3) << 12));
                uint32_t a = kTsMapPage * 16384u + row * 256 + l * 128 + c * 2;
                g_mem[a] = e & 0xFF;
                g_mem[a + 1] = e >> 8;
            }
}

static TsuRun RunTsu(const TsuCase& tc, int pipeDelay)
{
    auto ctx = std::make_unique<VerilatedContext>();
    ctx->randReset(0);
    auto top = std::make_unique<Vtbtop>(ctx.get());
    auto tick = [&]() {
        top->clk = 0; top->eval();
        top->clk = 1; top->eval();
    };
    top->clk = 0;
    top->res = 1;
    top->cpu_req = 0;
    top->eval();
    for (int i = 0; i < 8; i++) tick();
    top->res = 0;
    tick();

    auto write = [&](CData& strobe, uint8_t value) {
        top->xt_wr_data = value;
        strobe = 1;
        tick();
        strobe = 0;
        tick();
    };
    auto writeTileRegs = [&](const TsuRegs& r) {
        write(top->palsel_wr, r.palsel);
        write(top->t0gpage_wr, r.t0gpage);
        write(top->t1gpage_wr, r.t1gpage);
        write(top->t0x_offsl_wr, r.t0x & 0xFF);
        write(top->t0x_offsh_wr, (r.t0x >> 8) & 1);
        write(top->t1x_offsl_wr, r.t1x & 0xFF);
        write(top->t1x_offsh_wr, (r.t1x >> 8) & 1);
    };
    write(top->vconf_wr, tc.vconf);
    write(top->vpage_wr, kTsVideoPage);
    write(top->border_wr, 0xEE);
    write(top->tmpage_wr, kTsMapPage);
    write(top->sgpage_wr, kTsSpritePage);
    write(top->t0y_offsl_wr, 0);
    write(top->t0y_offsh_wr, 0);
    write(top->t1y_offsl_wr, 0);
    write(top->t1y_offsh_wr, 0);
    writeTileRegs(tc.before);
    write(top->tsconf_wr, tc.tsconf);

    // SFILE: S0 = s0 sprites (LEAP on the last), S1 = s1 sprites (LEAP on the last, or an inactive LEAP
    // descriptor when s1 = 0), then an inactive LEAP descriptor that ends S2
    std::vector<uint16_t> sfile(256, 0);
    int d = 0;
    auto sprite = [&](bool leap) {
        sfile[d * 3] = static_cast<uint16_t>(tc.line | 0x2000 | (leap ? 0x4000 : 0));
        sfile[d * 3 + 1] = TsSpriteR1(d);
        sfile[d * 3 + 2] = TsSpriteR2(d);
        d++;
    };
    auto leapOnly = [&]() { sfile[d * 3] = 0x4000; d++; };
    for (int i = 0; i < tc.s0; i++) sprite(i == tc.s0 - 1);
    if (tc.s0 == 0) leapOnly();
    for (int i = 0; i < tc.s1; i++) sprite(i == tc.s1 - 1);
    if (tc.s1 == 0) leapOnly();
    leapOnly();
    for (int i = 0; i < 256; i++)
    {
        top->zma = static_cast<uint8_t>(i);
        top->zmd = sfile[i];
        top->sfile_we = 1;
        tick();
        top->sfile_we = 0;
        tick();
    }

    int rres = tc.vconf >> 6;
    int targetV = kVpBeg[rres] + tc.line;
    int firstDot = kHpBeg[rres];
    int lastDot = kHpEnd[rres];
    TsuRun run;
    run.window.assign(lastDot - firstDot, 0);
    // The "after" writes, one strobe per 28 MHz clock inside the observed loop (nothing is missed)
    const std::vector<std::pair<CData*, uint8_t>> late = {
        {&top->palsel_wr, tc.after.palsel},
        {&top->t0gpage_wr, tc.after.t0gpage},
        {&top->t1gpage_wr, tc.after.t1gpage},
        {&top->t0x_offsl_wr, static_cast<uint8_t>(tc.after.t0x & 0xFF)},
        {&top->t0x_offsh_wr, static_cast<uint8_t>((tc.after.t0x >> 8) & 1)},
        {&top->t1x_offsl_wr, static_cast<uint8_t>(tc.after.t1x & 0xFF)},
        {&top->t1x_offsh_wr, static_cast<uint8_t>((tc.after.t1x >> 8) & 1)},
    };
    size_t lateNext = 0;
    bool inPass = false, afterLine = false;
    int slots = 0;
    long guard = 0;
    while (true)
    {
        top->clk = 0;
        for (const auto& w : late) *w.first = 0;
        if (lateNext > 0 && lateNext < late.size())
        {
            top->xt_wr_data = late[lateNext].second;
            *late[lateNext].first = 1;
            lateNext++;
        }
        top->eval();
        int v = top->ray_y;
        int h = top->ray_x;
        if (lateNext == 0 && v == targetV - 1 && h == 420)
        {
            top->xt_wr_data = late[0].second;
            *late[0].first = 1;
            lateNext = 1;
            top->eval();
        }
        if (top->ts_start_o)
        {
            if (v == targetV - 1) inPass = true;
            else if (v == targetV) inPass = false;
        }
        if (inPass)
        {
            if (top->c2_o && getenv("TSU_CYCLES") && h < 448 && (afterLine ? 0 : 1) && slots < 40)
                printf("    dot %3d cycle %02X%s\n", h, top->curr_cycle_o, top->tsr_go_o ? " go" : "");
            if (top->c2_o)
            {
                if (top->ts_next_o || top->tm_next_o)
                    slots++;
                else if (!afterLine && (top->curr_cycle_o & 2))
                    run.videoBefore++;
                else if (!afterLine && top->curr_cycle_o == 0)
                    run.freeBefore++;
                if (!afterLine) run.dotsToLine++;
            }
            if (top->tsr_go_o)
                run.gos.push_back({top->tsr_sprite_o != 0, static_cast<int>(top->tsr_x_o), top->tsr_page_o, top->tsr_pal_o, slots, afterLine});
            if (top->line_start_o && !afterLine)
            {
                afterLine = true;
                run.split = slots;
            }
        }
        if (v == targetV || v == targetV + 1)
        {
            // A 360-wide window ends at dot 447: its last dot arrives after the raster moved to the next line
            int dot = h - pipeDelay + (v == targetV + 1 ? 448 : 0);
            if (dot >= firstDot && dot < lastDot && top->c0_o) run.window[dot - firstDot] = top->vdata_o;
        }
        if (v == targetV + 1 && h > pipeDelay + 4) break;
        if (v == targetV && !inPass && run.tsuTotal == 0) run.tsuTotal = slots;
        top->clk = 1;
        top->eval();
        if (++guard > 4000000) { fprintf(stderr, "error: raster never reached line %d\n", targetV); exit(2); }
    }
    top->final();
    return run;
}

static std::vector<TsuCase> TsuCases()
{
    // PAL_SEL keeps [3:0] = 0, so the graphics layer (zero memory) is index 0 in every mode
    const TsuRegs a = {0x00, 0x80, 0x90, 0, 0};
    const TsuRegs b = {0xE0, 0x88, 0x98, 13, 267};
    return {
        {"cross-t0-360-16c", 0xC1, 0xA0, 15, 2, a, b},
        {"cross-t1-360-16c", 0xC1, 0xE0, 6, 0, a, b},
        {"control-360-16c", 0xC1, 0xE0, 0, 0, a, b},
        {"cross-t0-320-16c", 0x41, 0xA0, 14, 1, a, b},
        {"cross-t0-256-16c", 0x01, 0xA0, 13, 1, a, b},
        {"cross-t0-360-256c", 0xC2, 0xA0, 8, 1, a, b},
        {"cross-t0-256-zx", 0x00, 0xA0, 16, 1, a, b},
        {"control-256-zx", 0x00, 0xE0, 2, 0, a, b},
        {"cross-t0-256-txt", 0x03, 0xA0, 8, 1, a, b},
        {"cross-t0-320-txt", 0x83, 0xA0, 8, 1, a, b},
        // The last 8 window lines have no tilemap prefetch (v_pf ends at vpix_end_ts - 9)
        {"noprefetch-t0-360-16c", 0xC1, 0xA0, 16, 1, a, b, 284},
        {"noprefetch-t1-320-16c", 0x41, 0xE0, 8, 0, a, b, 196},
    };
}

static int RunTsuLatch(const char* dir)
{
    FillTsuMemory();
    FILE* f = nullptr;
    if (dir)
    {
        std::string path = std::string(dir) + "/tsu-latch.txt";
        f = fopen(path.c_str(), "w");
        if (!f) { perror(path.c_str()); return 3; }
        fprintf(f, "# Generated by tools/machines/tsconf/rtl-sim (tsconf-video-sim tsulatch). Format: see README.md.\n");
        fprintf(f, "# v_page=%02X t_map_page=%02X sg_page=%02X gfx=pages 80..AF byte(a) = a*7 + (a>>8)*13 + (a>>17)*101\n",
                kTsVideoPage, kTsMapPage, kTsSpritePage);
    }
    for (const TsuCase& tc : TsuCases())
    {
        TsuRun r = RunTsu(tc, g_pipeDelay);
        int late = 0;
        for (const TsuGo& g : r.gos) late += g.late;
        printf("%-20s split=%d dots=%d video=%d free=%d tsu=%d objects=%zu late=%d\n", tc.name, r.split, r.dotsToLine,
               r.videoBefore, r.freeBefore, r.tsuTotal, r.gos.size(), late);
        std::string gos;
        for (const TsuGo& g : r.gos)
        {
            char b[48];
            snprintf(b, sizeof b, " %c%d:%02X:%X:%d:%c", g.sprite ? 'S' : 'T', g.x, g.page, g.pal, g.slots, g.late ? 'L' : 'E');
            gos += b;
        }
        printf("  go%s\n", gos.c_str());
        if (f)
        {
            fprintf(f, "case %s vconf=%02X tsconf=%02X line=%d s0=%d s1=%d before=%02X,%02X,%02X,%d,%d after=%02X,%02X,%02X,%d,%d split=%d\n",
                    tc.name, tc.vconf, tc.tsconf, tc.line, tc.s0, tc.s1, tc.before.palsel, tc.before.t0gpage, tc.before.t1gpage,
                    tc.before.t0x, tc.before.t1x, tc.after.palsel, tc.after.t0gpage, tc.after.t1gpage, tc.after.t0x,
                    tc.after.t1x, r.split);
            fprintf(f, "go%s\n", gos.c_str());
            fprintf(f, "idx");
            for (uint8_t s : r.window) fprintf(f, " %02X", s);
            fprintf(f, "\n");
        }
    }
    if (f) fclose(f);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s sanity | all <results-dir> | line <zx|txt|16c|256c> <gx> [vconf-hex] | tsulatch [results-dir]\n", argv[0]);
        return 1;
    }
    FillZx();
    FillTxt();
    for (int y = 0; y < 512; y++) { FillHc(y); FillXc(y); }

    g_pipeDelay = FindPipeDelay();
    if (g_pipeDelay < 0)
    {
        fprintf(stderr, "error: no pipeline delay makes the G_X_OFFS=0 ZX line match the plain screen\n");
        return 2;
    }

    std::string cmd = argv[1];
    if (cmd == "line" && argc >= 4)
    {
        std::string m = argv[2];
        RunConfig cfg;
        cfg.gxOffs = static_cast<uint16_t>(atoi(argv[3]));
        if (m == "zx") { cfg.vconf = 0x00; cfg.vpage = kZxPage; }
        else if (m == "txt") { cfg.vconf = 0x03; cfg.vpage = kTxtPage; }
        else if (m == "16c") { cfg.vconf = 0x01; cfg.vpage = kHcPage; }
        else { cfg.vconf = 0x02; cfg.vpage = kXcPage; }
        if (argc >= 5) cfg.vconf = static_cast<uint8_t>(strtol(argv[4], nullptr, 16));
        PrintLine(RunLine(cfg, g_pipeDelay));
        return 0;
    }
    if (cmd == "tsulatch")
        return RunTsuLatch(argc >= 3 ? argv[2] : nullptr);
    bool ok = RunSanity();
    if (cmd == "all" && argc >= 3)
    {
        ok &= RunContentionCheck();
        if (!ok) { fprintf(stderr, "self-checks failed; results not written\n"); return 4; }
        WriteResults(argv[2]);
    }
    return ok ? 0 : 4;
}
