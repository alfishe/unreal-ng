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
//
// Exit code 0 = all self-checks passed.

#include "Vtbtop.h"
#include "verilated.h"

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

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s sanity | all <results-dir> | line <zx|txt|16c|256c> <gx> [vconf-hex]\n", argv[0]);
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
    bool ok = RunSanity();
    if (cmd == "all" && argc >= 3)
    {
        ok &= RunContentionCheck();
        if (!ok) { fprintf(stderr, "self-checks failed; results not written\n"); return 4; }
        WriteResults(argv[2]);
    }
    return ok ? 0 : 4;
}
