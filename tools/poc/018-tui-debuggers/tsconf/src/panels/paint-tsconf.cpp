// paint-tsconf.cpp - PC history + TSConf board painters, cell-exact port of
// unreal_dbg_render.py (show_pc_history / show_tsconf) which is the normative
// oracle for TDD-DBG-02 Appendix A/B.
#include "panels/paint-tsconf.h"

#include <cstdio>
#include <string>

#include "model/hexfmt.h"
#include "model/model.h"
#include "panels/paint.h"

namespace dbg {

namespace {

// ---------------------------------------------------------------------------
// Widget engine (port of dbg_canvas / python Canvas)
// ---------------------------------------------------------------------------

class TsCanvas {
public:
    TsCanvas(TextScreen& s, int bx, int by) : s_(s), bx_(bx), by_(by) {}

    TsCanvas& Pr(const std::string& str, uint8_t attr) {
        const std::string fit = str.size() > 31 ? str.substr(0, 31) : str;
        s_.Tprint(bx_ + x_, by_ + y_, fit, attr);
        last_ = static_cast<int>(fit.size());
        return *this;
    }
    TsCanvas& SetXy(int x, int y) {
        x_ = x;
        y_ = y;
        return *this;
    }
    TsCanvas& SetCols(int c0, int c1, int c2, int c3) {
        cols_[0] = c0;
        cols_[1] = c1;
        cols_[2] = c2;
        cols_[3] = c3;
        return *this;
    }
    TsCanvas& NextRow() {
        cur_ = 0;
        if (cols_[0] != -1) SetXy(cols_[0], y_ + 1);
        return *this;
    }
    TsCanvas& NextCol() {
        cur_ = (cur_ + 1) & 3;
        if (cols_[cur_] != -1) SetXy(cols_[cur_], y_);
        return *this;
    }
    TsCanvas& Mx() {
        x_ += last_;
        return *this;
    }
    TsCanvas& Move(int dx, int dy) {
        x_ += dx;
        y_ += dy;
        return *this;
    }

    TextScreen& Screen() { return s_; }
    int Bx() const { return bx_; }
    int By() const { return by_; }
    int X() const { return x_; }
    int Y() const { return y_; }

private:
    TextScreen& s_;
    int bx_ = 0;
    int by_ = 0;
    int x_ = 0;
    int y_ = 0;
    int cols_[4] = {-1, -1, -1, -1};
    int cur_ = 0;
    int last_ = 0;
};

// ---------------------------------------------------------------------------
// Widget engine (port of dbg_control / python Control)
// ---------------------------------------------------------------------------

class TsControl {
public:
    TsControl(TsCanvas& cv, int colx, int colw, int y, int h, bool active)
        : cv_(cv), colx_(colx), colw_(colw), y_(y), h_(h), active_(active) {}

    uint8_t N() const { return active_ ? kWSel : kWNorm; }
    uint8_t Eq() const { return active_ ? kWEqActive : kWEq; }
    uint8_t BitsC() const { return active_ ? kWBitsActive : kWBits; }

    void RegFrame(const std::string& title) {
        cv_.SetCols(colx_, colx_ + 3, colx_ + 12, colx_ + 14).SetXy(colx_, y_);
        cv_.Pr(title, kWTitle).Mx();
        cv_.NextRow();
        FrameAndFill();
    }
    void RegFrameVal(const std::string& title, uint8_t regval) {
        cv_.SetCols(colx_, colx_ + 3, colx_ + 12, colx_ + 14).SetXy(colx_, y_);
        cv_.Pr(title, kWTitle).Mx();
        cv_.Pr("=", kWTitle).Mx();
        cv_.Pr(Hex2(regval), kWRegval);
        cv_.NextRow();
        FrameAndFill();
    }

    void BitsRange(int bits) {
        if (bits >= 0) {
            const char l0 = bits >= 10 ? static_cast<char>('0' + bits / 10) : ' ';
            const char l1 = bits % 10 == 0 ? '0' : static_cast<char>('0' + bits % 10);
            cv_.Pr(std::string{l0, l1}, BitsC()).Mx().Pr(":", BitsC());
        } else {
            cv_.Pr("   ", BitsC());
        }
        cv_.NextCol();
    }
    void Lhs(const std::string& title, int bits) {
        BitsRange(bits);
        cv_.Pr(title, N()).NextCol().Pr("=", Eq()).NextCol();
    }

    void BitSet(const std::string& title, int bits, const char* const* vals, int count,
                int v) {
        Lhs(title, bits);
        cv_.Pr(vals[In0(v, count)], N());
        cv_.NextRow();
    }
    void Bit(const std::string& title, int bits, unsigned v) {
        Lhs(title, bits);
        cv_.Pr((v & 1) != 0 ? "ena" : "dis", N());
        cv_.NextRow();
    }
    void BitH(const std::string& title, int bits, unsigned v) {
        Lhs(title, bits);
        cv_.Pr(Hex2(static_cast<uint8_t>(v & 0xFF)), N());
        cv_.NextRow();
    }
    void BitD(const std::string& title, int bits, unsigned v) {
        Lhs(title, bits);
        char buf[8];
        std::snprintf(buf, sizeof buf, "%03u", v);
        cv_.Pr(buf, N());
        cv_.NextRow();
    }
    void Hex16(const std::string& title, int bits, unsigned v) {
        Lhs(title, bits);
        cv_.Pr(Hex4(static_cast<uint16_t>(v & 0xFFFF)), N());
        cv_.NextRow();
    }
    void Hex24(const std::string& title, uint32_t v) {
        cv_.Pr(title, N()).NextCol().NextCol().Pr("=", Eq()).NextCol();
        char hi[4];
        std::snprintf(hi, sizeof hi, "%02X", v >> 14);
        char lo[6];
        std::snprintf(lo, sizeof lo, "%04X", v & 0x3FFF);
        // the ':' separator carries the "=" attribute (python: pr(":", eq))
        cv_.Pr(hi, N()).Mx().Pr(":", Eq()).Mx().Pr(lo, N());
        cv_.NextRow();
    }
    void Port(const std::string& title, uint8_t v) {
        cv_.Pr(title, N()).NextCol().NextCol().Pr("=", Eq()).NextCol();
        cv_.Pr(Hex2(v), N());
        cv_.NextRow();
    }
    void Hex8Inline(const std::string& title, uint8_t v) {
        cv_.Pr(title, N()).Mx().Pr("= ", Eq()).Mx().Pr(Hex2(v), N()).Mx().Pr(" ", N()).Mx();
    }
    void DecInline(const std::string& title, unsigned v) {
        char buf[8];
        std::snprintf(buf, sizeof buf, "%03u", v);
        cv_.Pr(title, N()).Mx().Pr("= ", Eq()).Mx().Pr(buf, N()).Mx().Pr(" ", N()).Mx();
    }
    void HlPort(const char* p, uint8_t hv, uint8_t lv, uint16_t v) {
        // "XH= 00 XL= 00 X= 0000 " - H/L are %02X, the word is %04X
        cv_.Pr(std::string(p) + "H", N()).Mx().Pr("= ", Eq()).Mx().Pr(Hex2(hv), N()).Mx()
            .Pr(" ", N()).Mx();
        cv_.Pr(std::string(p) + "L", N()).Mx().Pr("= ", Eq()).Mx().Pr(Hex2(lv), N()).Mx()
            .Pr(" ", N()).Mx();
        cv_.Pr(p, N()).Mx().Pr("= ", Eq()).Mx().Pr(Hex4(v), N()).Mx().Pr(" ", N()).Mx();
        cv_.NextRow();
    }
    void Led(const std::string& title, unsigned on) {
        cv_.Move(1, 0);
        if ((on & 1) != 0) {
            cv_.Pr(title, kWLedon).Mx();
        } else {
            cv_.Move(static_cast<int>(title.size()), 0);
        }
    }
    void Cxy(int x, int y) { cv_.SetXy(colx_ + x, y_ + y); }

    TsCanvas& Canvas() { return cv_; }

private:
    void FrameAndFill() {
        cv_.Screen().Frame(cv_.Bx() + cv_.X(), cv_.By() + cv_.Y(), colw_ - 1, h_, kFrameColor);
        cv_.Screen().FillRect(cv_.Bx() + cv_.X(), cv_.By() + cv_.Y(), colw_ - 1, h_, N());
    }
    static int In0(int v, int count) { return v < 0 ? 0 : (v >= count ? count - 1 : v); }

    TsCanvas& cv_;
    int colx_;
    int colw_;
    int y_;
    int h_;
    bool active_;
};

// ---------------------------------------------------------------------------
// Decoder tables (D_VMODE / D_RRES / D_CLK / D_DMA)
// ---------------------------------------------------------------------------

const char* const kDVmode[4] = {"ZX  ", "16c ", "256c", "text"};
const char* const kDRres[4] = {"256x192", "320x200", "320x240", "360x288"};
const char* const kDClk[4] = {"3.5M", "7M", "14M", "unk"};
const char* const kDDma[16] = {"unk    ", "unk    ", "RAM-RAM", "BLT-RAM", "SPI-RAM",
                               "RAM-SPI", "IDE-RAM", "RAM-IDE", "FIL-RAM", "RAM-CRM",
                               "unk    ", "RAM-SFL", "unk    ", "unk    ", "unk    ",
                               "unk    "};

// ---------------------------------------------------------------------------
// The 16 control painters, in init_regs_page() column order
// ---------------------------------------------------------------------------

using TsPainter = void (*)(TsControl&, const TsConfState&);

void PaintVconfig(TsControl& c, const TsConfState& t) {
    c.RegFrameVal("VConfig", t.vconf);
    c.BitSet("RRES", 76, kDRres, 4, t.rres);
    c.Bit("NOGFX", 5, t.nogfx);
    c.Bit("NOTSU", 4, t.notsu);
    c.Bit("GFXOVR", 3, t.gfxovr);
    c.Bit("FT_EN", 2, t.ftEn);
    c.BitSet("VMODE", 10, kDVmode, 4, t.vmode);
}

void PaintTsconfig(TsControl& c, const TsConfState& t) {
    c.RegFrameVal("TSConfig", t.tsconf);
    c.Bit("S_EN", 7, t.sEn);
    c.Bit("T1_EN", 6, t.t1En);
    c.Bit("T0_EN", 5, t.t0En);
    c.Bit("T1Z_EN", 3, t.t1zEn);
    c.Bit("T0Z_EN", 2, t.t0zEn);
    c.Bit("TS_EXT", 0, t.tsconf);  // whole-register bit 0
}

void PaintSysconfig(TsControl& c, const TsConfState& t) {
    c.RegFrameVal("SysConfig", t.sysconf);
    c.Bit("CACHE_EN", 2, t.sysconf >> 2);
    c.BitSet("ZCLK", 10, kDClk, 4, t.zclk);
}

void PaintCacheconfig(TsControl& c, const TsConfState& t) {
    c.RegFrameVal("CacheConfig", t.cacheconf);
    struct Row {
        const char* name;
        int sh;
    };
    const Row rows[4] = {{"EN_C000", 3}, {"EN_8000", 2}, {"EN_4000", 1}, {"EN_0000", 0}};
    for (const Row& r : rows) {
        c.Bit(r.name, 4, t.cacheconf >> r.sh);  // NB: every row labelled "4:"
    }
}

void PaintMemconfig(TsControl& c, const TsConfState& t) {
    c.RegFrameVal("MemConfig", t.memconf);
    c.Bit("LCK128", 76, t.sEn);             // NB: original shows S_EN here (bug)
    c.Bit("W0_RAM", 3, t.w0Ram ? 0u : 1u);  // shown inverted
    c.Bit("W0_MAP", 2, t.w0MapN ? 0u : 1u);  // shown inverted
    c.Bit("W0_WE", 1, t.w0We);
    c.Bit("ROM128", 0, t.rom128);
}

void PaintBitmap(TsControl& c, const TsConfState& t) {
    c.Cxy(19, 0);
    c.Led("EN", t.nogfx ? 0u : 1u);
    c.RegFrame("Bitmap");
    c.Port("VPage", t.vpage);
    c.HlPort("X", static_cast<uint8_t>(t.gXoffs >> 8), static_cast<uint8_t>(t.gXoffs & 0xFF),
             t.gXoffs);
    c.HlPort("Y", static_cast<uint8_t>(t.gYoffs >> 8), static_cast<uint8_t>(t.gYoffs & 0xFF),
             t.gYoffs);
}

void PaintTiles(TsControl& c, const TsConfState& t, int n) {
    char title[16];
    std::snprintf(title, sizeof title, "Tiles%d", n);
    c.RegFrame(title);
    c.Cxy(14, 0);
    c.Led("Z_EN", (n == 0 ? t.t0zEn : t.t1zEn) ? 1u : 0u);
    c.Led("EN", (n == 0 ? t.t0En : t.t1En) ? 1u : 0u);
    c.Canvas().NextRow();
    char gpage[16];
    std::snprintf(gpage, sizeof gpage, "T%dGPage", n);
    c.Port(gpage, n == 0 ? t.t0gpage : t.t1gpage);
    c.Hex16("X", 16, n == 0 ? t.t0Xoffs : t.t1Xoffs);
    c.Hex16("Y", 16, n == 0 ? t.t0Yoffs : t.t1Yoffs);
}

void PaintTiles0(TsControl& c, const TsConfState& t) { PaintTiles(c, t, 0); }
void PaintTiles1(TsControl& c, const TsConfState& t) { PaintTiles(c, t, 1); }

void PaintPalsel(TsControl& c, const TsConfState& t) {
    c.RegFrameVal("PalSel", t.palsel);
    c.BitH("T1PAL", 76, t.t1pal << 2);
    c.BitH("T0PAL", 54, t.t0pal << 2);
    c.BitH("GPAL", 30, t.gpal);
}

void PaintMisc(TsControl& c, const TsConfState& t) {
    c.RegFrame("Misc");
    c.Cxy(6, 0);
    c.Led("FDD", t.fddvirt >> 3);
    c.Led("FDC", t.fddvirt >> 2);
    c.Led("FDB", t.fddvirt >> 1);
    c.Led("FDA", t.fddvirt);
    c.Canvas().NextRow();
    c.Port("   TMPage", t.tmpage);
    c.Port("   Border", t.border);
    c.Port("   FDDVirt", t.fddvirt);
}

void PaintFmaddr(TsControl& c, const TsConfState& t) {
    c.RegFrameVal("FMAddr", t.fmaddr);
    c.Bit("FM_EN", 4, t.fmEn);
    c.Hex16("FM_MAPS", 30, t.fmAddr << 12);
}

void PaintMempages(TsControl& c, const TsConfState& t) {
    c.RegFrame("MemPages");
    for (int i = 0; i < 4; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, "   Page%d", i);
        c.Port(name, t.page[i]);
    }
}

void PaintSprites(TsControl& c, const TsConfState& t) {
    c.RegFrame("Sprites");
    c.Port("SGPage", t.sgpage);
}

void PaintDma(TsControl& c, const TsConfState& t) {
    const TsDmaState& d = t.dma;
    c.RegFrame("DMA");
    c.Cxy(15, 0);
    c.Led("ACTIVE", d.active ? 1u : 0u);
    c.Canvas().NextRow();
    c.Hex24("     SRC", t.saddr);
    c.Hex24("CURR SRC", d.saddr);
    c.Canvas().NextRow();
    c.Hex24("     DST", t.daddr);
    c.Hex24("CURR DST", d.daddr);
    c.Canvas().NextRow();
    c.Hex8Inline("     NUM", t.dmanum);
    c.Hex8Inline(" LEN", t.dmalen);
    c.Canvas().NextRow();
    c.Hex8Inline("CURR NUM", static_cast<uint8_t>(d.num & 0xFF));
    c.Hex8Inline(" LEN", static_cast<uint8_t>(d.len & 0xFF));
    c.Canvas().NextRow();
    c.Canvas().NextRow();
    c.Port("CTRL", d.ctrl);
    c.Bit("OPT", 6, d.opt);
    c.Bit("S_ALIGN", 5, d.sAlgn);
    c.Bit("D_ALIGN", 4, d.dAlgn);
    c.Bit("A_SZ", 3, d.asz);
    c.BitSet("DDEV", 20, kDDma, 16, (d.dev << 1) + d.rw);
}

void PaintInterrupt(TsControl& c, const TsConfState& t) {
    c.RegFrame("Interrupt");
    c.Port("HSINT", t.hsint);
    c.DecInline(" h", static_cast<unsigned>(t.hsint) << 1);
    c.Canvas().NextRow();
    c.Port("VSINTH", static_cast<uint8_t>(t.vsint >> 8));
    c.Port("VSINTL", static_cast<uint8_t>(t.vsint & 0xFF));
    c.DecInline(" v", t.vsint);
    c.DecInline(" inc", t.vsint >> 4);
}

void PaintIntmask(TsControl& c, const TsConfState& t) {
    c.RegFrame("IntMask");
    c.Bit("DMA", 2, t.intDma);
    c.Bit("LINE", 1, t.intLine);
    c.Bit("FRAME", 0, t.intFrame);
}

struct TsEntry {
    TsPainter paint;
    const char* name;
    int h;
};

const TsEntry kCol0[] = {
    {PaintVconfig, "vconfig", 6},   {PaintTsconfig, "tsconfig", 6},
    {PaintSysconfig, "sysconfig", 2}, {PaintCacheconfig, "cacheconfig", 4},
    {PaintMemconfig, "memconfig", 5},
};
const TsEntry kCol1[] = {
    {PaintBitmap, "bitmap", 3}, {PaintTiles0, "tiles0", 3}, {PaintTiles1, "tiles1", 3},
    {PaintPalsel, "palsel", 3}, {PaintMisc, "misc", 3},     {PaintFmaddr, "fmaddr", 2},
    {PaintMempages, "mempages", 4},
};
const TsEntry kCol2[] = {
    {PaintSprites, "sprites", 1}, {PaintDma, "dma", 15},
    {PaintInterrupt, "interrupt", 5}, {PaintIntmask, "intmask", 3},
};

}  // namespace

void PaintPcHistory(TextScreen& s, IDebuggerBackend& be) {
    const std::vector<PcHistEntry> hist = be.GetPcHistory(0);
    const size_t n = hist.size() < 28 ? hist.size() : 28;
    for (size_t i = 0; i < n; ++i) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%02X:%04X", hist[i].page, hist[i].addr);
        s.Tprint(80, static_cast<int>(i) + 1, buf, kWOther);
    }
    s.Tprint(80, 0, "PC hist", kWTitle);
    s.Frame(80, 1, 7, 28, kFrameColor);
}

std::vector<TsControlRect> PaintTsconfBoard(TextScreen& s, IDebuggerBackend& be,
                                            const std::string& activeControl) {
    std::vector<TsControlRect> placed;
    TsConfState t;
    if (!be.GetTsConf(t)) return placed;

    const TsEntry* const cols[3] = {kCol0, kCol1, kCol2};
    constexpr int kColLens[3] = {5, 7, 4};
    TsCanvas cv(s, 88, 0);
    for (int ci = 0; ci < 3; ++ci) {
        int y = 0;
        for (int i = 0; i < kColLens[ci]; ++i) {
            const TsEntry& e = cols[ci][i];
            TsControl ctl(cv, ci * 23, 23, y, e.h, activeControl == e.name);
            e.paint(ctl, t);
            TsControlRect rect;
            rect.name = e.name;
            rect.x = ci * 23 + 88;
            rect.y = y;
            rect.h = e.h;
            placed.push_back(rect);
            y += e.h + 1;
        }
    }
    return placed;
}

}  // namespace dbg
