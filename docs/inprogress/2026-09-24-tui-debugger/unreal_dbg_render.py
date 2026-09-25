#!/usr/bin/env python3
"""
Reference renderer for the Unreal Speccy debugger (monitor) screen.

Faithful port of the text-screen layout code of:
  * UnrealSpeccy 0.39.0 (SMT)                 -> layout "classic"  (80x30 cells)
  * zx-evo/pentevo Unreal (TSConf fork)       -> layout "tsconf"   (157x30 cells, 88 visible without TSConf)

It produces a golden text dump, an attribute dump and a PNG (8x16 font, 1px frames) so that
any re-implementation can be diffed cell-by-cell and pixel-by-pixel.

The emulator state is a plain dict (see sample_state()).  Disassembly is supplied by the
state (a tiny table) because the renderer is about layout, not about decoding Z80.

Usage: python3 unreal_dbg_render.py <font16.bin|font16.cpp> <outdir>
"""
import re, sys, os

W_SEL, W_NORM, W_CURS = 0x17, 0x07, 0x30
BACKGR, BACKGR_CH = 0x50, 0xB1
W_TITLE, W_OTHER, W_OTHEROFF = 0x59, 0x40, 0x47
W_AYNUM, W_AYON, W_AYOFF = 0x4F, 0x41, 0x40
W_BANK, W_BANKRO = 0x40, 0x41
W_DIHALT1, W_DIHALT2 = 0x1A, 0x0A
W_TRACEPOS = 0x70
W_48K, W_DOS = 0x20, 0x20
W_TRACE_JINFO_CURS_FG, W_TRACE_JINFO_NOCURS_FG, W_TRACE_JARROW_FOREGR = 0x0D, 0x02, 0x0D
FRAME = 0x01
# tsconf widget colours
W_REGVAL, W_BITS, W_EQ, W_BITS_ACTIVE, W_EQ_ACTIVE, W_LEDON = 0x51, 0x04, 0x04, 0x14, 0x14, 0x50


class Screen:
    def __init__(self, w, h=30):
        self.w, self.h = w, h
        self.ch = [BACKGR_CH] * (w * h)
        self.at = [BACKGR] * (w * h)
        self.frames = []

    def tprint(self, x, y, s, attr):
        p = y * self.w + x
        for c in s:
            b = c if isinstance(c, int) else ord(c)
            if 0 <= p < len(self.ch):
                self.ch[p] = b
                self.at[p] = attr
            p += 1

    def tprint_fg(self, x, y, s, fg):
        p = y * self.w + x
        for c in s:
            b = c if isinstance(c, int) else ord(c)
            self.ch[p] = b
            self.at[p] = (self.at[p] & 0xF0) + fg
            p += 1

    def setattr(self, x, y, attr):
        self.at[y * self.w + x] = attr

    def fillrect(self, x, y, dx, dy, attr):
        for yy in range(y, y + dy):
            for xx in range(x, x + dx):
                self.ch[yy * self.w + xx] = 0x20
                self.at[yy * self.w + xx] = attr

    def frame(self, x, y, dx, dy, attr):
        self.frames.append((x, y, dx, dy, attr))


# --------------------------------------------------------------------------------------------
# classic panels (identical positions in both forks)
# --------------------------------------------------------------------------------------------
REGS_LAYOUT = [  # name, width, x, y, lf, rt, up, dn
    ("a", 8, 3, 0, 0, 1, 0, 2), ("f", 8, 5, 0, 0, 5, 1, 2), ("bc", 16, 3, 1, 2, 6, 0, 3),
    ("de", 16, 3, 2, 3, 7, 2, 4), ("hl", 16, 3, 3, 4, 8, 3, 4), ("af'", 16, 11, 0, 1, 9, 5, 6),
    ("bc'", 16, 11, 1, 2, 10, 5, 7), ("de'", 16, 11, 2, 3, 11, 6, 8), ("hl'", 16, 11, 3, 4, 12, 7, 8),
    ("sp", 16, 19, 0, 5, 13, 9, 10), ("pc", 16, 19, 1, 6, 10, 9, 11), ("ix", 16, 19, 2, 7, 15, 10, 12),
    ("iy", 16, 19, 3, 8, 18, 11, 12), ("i", 8, 28, 0, 9, 14, 13, 16), ("r", 8, 30, 0, 13, 14, 14, 17),
    ("im", 2, 26, 2, 11, 16, 13, 20), ("iff1", 1, 30, 2, 15, 17, 13, 24), ("iff2", 1, 31, 2, 16, 17, 14, 25),
]


def show_regs(S, st):
    cpu, prev = st["cpu"], st["prev"]
    act = st["active"] == "regs"
    atr = W_SEL if act else W_NORM
    x0, y0 = 1, 1
    S.tprint(x0, y0 + 0, "af:**** af'**** sp:**** ir: ****", atr)
    S.tprint(x0, y0 + 1, "bc:**** bc'**** pc:**** t:******", atr)
    S.tprint(x0, y0 + 2, "de:**** de'**** ix:**** im?,i:**", atr)
    S.tprint(x0, y0 + 3, "hl:**** hl'**** iy:**** ########", atr)
    if cpu["halted"] and not cpu["iff1"]:
        S.tprint(x0 + 26, y0 + 1, "DiHALT", W_DIHALT1 if act else W_DIHALT2)
    else:
        S.tprint(x0 + 26, y0 + 1, "%6u" % cpu["t"], atr)
    for i, (name, wd, x, y, *_nav) in enumerate(REGS_LAYOUT):
        v, pv = cpu[name], prev[name]
        a1 = W_CURS if (act and i == st["regs_curs"]) else atr
        if v != pv:
            a1 |= 0x08
        s = {8: "%02X", 16: "%04X", 1: "%X", 2: "%X"}[wd] % v
        S.tprint(x0 + x, y0 + y, s, a1)
    flg = "SZ5H3PNCsz.h.pnc"
    for q in range(8):
        a1 = W_CURS if (act and st["regs_curs"] == q + 18) else atr
        bit = 0x80 >> q
        c = flg[q + (0 if cpu["f"] & bit else 8)]
        if bit & (cpu["f"] ^ prev["f"]):
            a1 |= 0x08
        S.tprint(x0 + 24 + q, y0 + 3, c, a1)
    S.tprint(x0, y0 - 1, "regs", W_TITLE)
    S.frame(x0, y0, 32, 4, FRAME)


def show_trace(S, st):
    cpu = st["cpu"]
    x0, y0, n = 1, 6, 21
    act = st["active"] == "trace"
    atr0 = W_SEL if act else W_NORM
    cs = [(0, 4), (5, 10), (16, 16)]
    pc = st["trace_top"]
    jf = st["pc_jump"]  # None or dict(target, braddr)
    for ii in range(n):
        addr, ln, text = st["disasm"](pc)
        line = text.ljust(32)[:32]
        a = W_TRACEPOS if pc == cpu["pc"] else atr0
        if pc in st["bpx"]:
            a = (a & ~7) | 2
        S.tprint(x0, y0 + ii, line, a)
        if pc == st["trace_curs"] and act:
            c0, cl = cs[st["trace_mode"]]
            for q in range(cl):
                S.setattr(x0 + c0 + q, y0 + ii, W_CURS)
        if jf:
            if pc == cpu["pc"]:
                arr = 0x18 if jf["target"] <= cpu["pc"] else 0x19
                col = W_TRACE_JINFO_CURS_FG if (pc == st["trace_curs"] and act and st["trace_mode"] == 2) else W_TRACE_JINFO_NOCURS_FG
                if jf["braddr"]:
                    S.tprint_fg(x0 + 27, y0 + ii, [*b"%04X" % jf["target"], arr], col)
                else:
                    S.tprint_fg(x0 + 31, y0 + ii, [arr], col)
            if pc == jf["target"]:
                S.tprint_fg(x0 + 31, y0 + ii, [0x11], W_TRACE_JARROW_FOREGR)
        pc = (pc + ln) & 0xFFFF
    S.tprint(x0, y0 - 1, "Z80(%u)" % st["cpu_index"], W_TITLE)
    S.tprint(x0 + 8, y0 - 1, "%04X" % cpu["last_branch"], W_TITLE)
    S.frame(x0, y0, 32, n, FRAME)


def hexline(mem, ptr, head):
    s = list(head.ljust(5)[:5]) + [" "] * 32
    for dx in range(8):
        c = mem[(ptr + dx) & 0xFFFF]
        s[5 + 3 * dx:7 + 3 * dx] = list("%02X" % c)
        s[7 + 3 * dx] = " "
        s[29 + dx] = c if c else ord(".")
    return [x if isinstance(x, int) else ord(x) for x in s[:37]]


def show_mem(S, st):
    x0, y0, n = 34, 15, 12
    act = st["active"] == "mem"
    mem = st["mem"]
    top, curs = st["mem_top"], st["mem_curs"]
    for ii in range(n):
        ptr = (top + ii * 8) & 0xFFFF
        S.tprint(x0, y0 + ii, hexline(mem, ptr, "%04X " % ptr), W_SEL if act else W_NORM)
        for dx in range(8):
            if (ptr + dx) & 0xFFFF == curs and act:
                cx = dx + 29 if st["mem_ascii"] else dx * 3 + 5 + st["mem_second"]
                S.setattr(x0 + cx, y0 + ii, W_CURS)
    S.tprint(x0, y0 - 1, "memory: %04X gsdma: %06X" % (curs, st["gsdmaaddr"]), W_TITLE)
    S.frame(x0, y0, 37, n, FRAME)


def show_watch(S, st, alt_title=None):
    x0, y0, n = 34, 1, 13
    cpu, mem = st["cpu"], st["mem"]
    if st["show_scrshot"]:
        for y in range(n):
            for x in range(37):
                S.setattr(x0 + x, y0 + y, 0xFF)
    else:
        rows = [("PC", cpu["pc"]), ("SP", cpu["sp"]), ("BC", cpu["bc"]), ("DE", cpu["de"]), ("HL", cpu["hl"]),
                ("IX", cpu["ix"]), ("IY", cpu["iy"]), ("BC'", cpu["bc'"]), ("DE'", cpu["de'"]), ("HL'", cpu["hl'"])]
        for i, (nm, v) in enumerate(rows):
            S.tprint(x0, y0 + i, hexline(mem, v, "%3s: " % nm), W_OTHER)
        for i, v in enumerate(st["user_watches"]):
            S.tprint(x0, y0 + 10 + i, hexline(mem, v, "%04X " % v), W_OTHER)
    title = "watches"
    if st["show_scrshot"] == 1:
        title = alt_title or "screen memory"
    if st["show_scrshot"] == 2:
        title = "ray-painted"
    S.tprint(x0, y0 - 1, title, W_TITLE)
    if st["dosports"]:
        S.tprint(x0 + 34, y0 - 1, "DOS", W_DOS)
    S.frame(x0, y0, 37, n, FRAME)


def show_stack(S, st):
    x0, y0 = 72, 12
    sp, mem = st["cpu"]["sp"], st["mem"]
    for i in range(10):
        lab = "-2" if i == 0 else "SP" if i == 1 else ("%X" if i > 8 else "+%X") % ((i - 1) * 2)
        a = (sp + (i - 1) * 2) & 0xFFFF
        S.tprint(x0, y0 + i, "%s:%02X%02X" % (lab, mem[(a + 1) & 0xFFFF], mem[a]), W_OTHER)
    S.tprint(x0, y0 - 1, "stack", W_TITLE)
    S.frame(x0, y0, 7, 10, FRAME)


def show_ay(S, st):
    if not st["ay_scheme"]:
        return
    x0, y0 = 31, 28
    S.tprint(x0 - 3, y0, st["ay_name"], W_TITLE)
    for i in range(16):
        S.tprint(x0 + i * 3, y0, "0123456789ABCDEF"[i], W_AYNUM)
        S.tprint(x0 + i * 3 + 1, y0, "%02X" % st["ay_regs"][i], W_AYON if i == st["ay_active_reg"] else W_AYOFF)
    S.frame(x0, y0, 48, 1, FRAME)


def show_banks(S, st, tsconf=False):
    x0, y0 = 72, 22
    for i in range(4):
        name, ro = st["banks"][i]
        if tsconf:
            sel = st.get("selbank") == i and st.get("showbank")
            actb = st["active"] == "banks"
            S.tprint(x0, y0 + i + 1, "%d:" % i, ((W_OTHEROFF & 0xF) | W_CURS) if sel else (W_OTHEROFF | (0x10 if actb else 0)))
            S.tprint(x0 + 2, y0 + i + 1, name, W_CURS if sel else ((W_BANKRO if ro else W_BANK) | (0x10 if actb else 0)))
        else:
            S.tprint(x0, y0 + i + 1, "%d:" % i, W_OTHEROFF)
            S.tprint(x0 + 2, y0 + i + 1, name, W_BANKRO if ro else W_BANK)
    S.frame(x0, y0 + 1, 7, 4, FRAME)
    S.tprint(x0, y0, "pages", W_TITLE)


def show_ports(S, st):
    x0, y0 = 72, 1
    S.tprint(x0, y0, "  FE:%02X" % st["pFE"], W_OTHER)
    S.tprint(x0, y0 + 1, "7FFD:%02X" % st["p7FFD"], W_48K if st["lock48"] else W_OTHER)
    if st["extport"] is not None:
        S.tprint(x0, y0 + 2, "%04X:%02X" % st["extport"], W_OTHER)
    else:
        S.tprint(x0, y0 + 2, "cmos:%02X" % st["cmos_addr"], W_OTHER)
    S.tprint(x0, y0 + 3, "EFF7:%02X" % st["pEFF7"], W_OTHER)
    S.frame(x0, y0, 7, 4, FRAME)
    S.tprint(x0, y0 - 1, "ports", W_TITLE)


def show_dos(S, st):
    x0, y0 = 72, 6
    wd = st["wd"]
    a = W_OTHER if st["trdos_present"] else W_OTHEROFF
    S.tprint(x0, y0 + 0, "CD:%02X%02X" % (wd["cmd"], wd["data"]), a)
    S.tprint(x0, y0 + 1, "STAT:%02X" % wd["status"], a)
    S.tprint(x0, y0 + 2, "SECT:%02X" % wd["sector"], a)
    S.tprint(x0, y0 + 3, "T:%02X/%02X" % (wd["drive_track"], wd["track"]), a)
    S.tprint(x0, y0 + 4, "S:%02X/%02X" % (wd["system"], wd["rqs"]), a)
    S.frame(x0, y0, 7, 5, FRAME)
    S.tprint(x0, y0 - 1, "beta128", W_TITLE)


def show_time(S, st):
    x0, y0 = 1, 28
    S.tprint(x0, y0, "time delta:", W_OTHEROFF)
    S.tprint(x0 + 11, y0, "%14d" % st["time_delta"], W_OTHER)
    S.tprint(x0 + 25, y0, "t", W_OTHEROFF)
    S.frame(x0, y0, 26, 1, FRAME)


def show_pc_history(S, st):
    x0, y0 = 80, 0
    for i, (page, addr) in enumerate(st["pc_hist"][:28]):
        S.tprint(x0, y0 + i + 1, "%02X:%04X" % (page, addr), W_OTHER)
    S.tprint(x0, y0, "PC hist", W_TITLE)
    S.frame(x0, y0 + 1, 7, 28, FRAME)


# --------------------------------------------------------------------------------------------
# TSConf widget engine (port of dbg_canvas / dbg_column / dbg_control)
# --------------------------------------------------------------------------------------------
class Canvas:
    def __init__(self, S, bx, by):
        self.S, self.bx, self.by = S, bx, by
        self.x = self.y = 0
        self.cols = [-1] * 4
        self.cur = 0
        self.last = 0

    def pr(self, s, attr):
        s = s[:31]
        self.S.tprint(self.bx + self.x, self.by + self.y, s, attr)
        self.last = len(s)
        return self

    def set_xy(self, x, y): self.x, self.y = x, y; return self
    def set_cols(self, *c): self.cols = list(c); return self

    def next_row(self):
        self.cur = 0
        if self.cols[0] != -1:
            self.set_xy(self.cols[0], self.y + 1)
        return self

    def next_col(self):
        self.cur = (self.cur + 1) & 3
        if self.cols[self.cur] != -1:
            self.set_xy(self.cols[self.cur], self.y)
        return self

    def mx(self): self.x += self.last; return self
    def move(self, dx, dy): self.x += dx; self.y += dy; return self


class Control:
    def __init__(self, cv, colx, colw, y, h, active=False):
        self.cv, self.colx, self.colw, self.y, self.h, self.active = cv, colx, colw, y, h, active

    # colour helpers
    @property
    def n(self): return W_SEL if self.active else W_NORM
    @property
    def eq(self): return W_BITS_ACTIVE if self.active else W_EQ
    @property
    def bits_c(self): return W_BITS_ACTIVE if self.active else W_BITS

    def reg_frame(self, title, regval=None):
        cv, x = self.cv, self.colx
        cv.set_cols(x, x + 3, x + 12, x + 14).set_xy(x, self.y)
        cv.pr(title, W_TITLE).mx()
        if regval is not None:
            cv.pr("=", W_TITLE).mx()
            cv.pr("%02X" % regval, W_REGVAL)
        cv.next_row()
        cv.S.frame(cv.bx + cv.x, cv.by + cv.y, self.colw - 1, self.h, FRAME)
        cv.S.fillrect(cv.bx + cv.x, cv.by + cv.y, self.colw - 1, self.h, self.n)

    def bits_range(self, bits):
        cv = self.cv
        if bits >= 0:
            l0 = chr(48 + bits // 10) if bits >= 10 else " "
            l1 = "0" if bits % 10 == 0 else chr(48 + bits % 10)
            cv.pr(l0 + l1, self.bits_c).mx().pr(":", self.bits_c)
        else:
            cv.pr("   ", self.bits_c)
        cv.next_col()

    def _lhs(self, title, bits):
        self.bits_range(bits)
        self.cv.pr(title, self.n).next_col().pr("=", self.eq).next_col()

    def bit_set(self, title, bits, vals, v):
        self._lhs(title, bits); self.cv.pr(vals[v], self.n); self.cv.next_row()

    def bit(self, title, bits, v):
        self._lhs(title, bits); self.cv.pr("ena" if v & 1 else "dis", self.n); self.cv.next_row()

    def bit_h(self, title, bits, v):
        self._lhs(title, bits); self.cv.pr("%02X" % (v & 0xFF), self.n); self.cv.next_row()

    def bit_d(self, title, bits, v):
        self._lhs(title, bits); self.cv.pr("%03d" % v, self.n); self.cv.next_row()

    def hex16(self, title, bits, v):
        self._lhs(title, bits); self.cv.pr("%04X" % (v & 0xFFFF), self.n); self.cv.next_row()

    def hex24(self, title, v):
        cv = self.cv
        cv.pr(title, self.n).next_col().next_col().pr("=", self.eq).next_col()
        cv.pr("%02X" % (v >> 14), self.n).mx().pr(":", self.eq).mx().pr("%04X" % (v & 0x3FFF), self.n)
        cv.next_row()

    def port(self, title, v):
        cv = self.cv
        cv.pr(title, self.n).next_col().next_col().pr("=", self.eq).next_col()
        cv.pr("%02X" % v, self.n); cv.next_row()

    def hex8_inline(self, title, v):
        self.cv.pr(title, self.n).mx().pr("= ", self.eq).mx().pr("%02X" % v, self.n).mx().pr(" ", self.n).mx()

    def dec_inline(self, title, v):
        self.cv.pr(title, self.n).mx().pr("= ", self.eq).mx().pr("%03d" % v, self.n).mx().pr(" ", self.n).mx()

    def hl_port(self, p, hv, lv, v):
        cv = self.cv
        for t, val, fmt in ((p + "H", hv, "%02X"), (p + "L", lv, "%02X"), (p, v, "%04X")):
            cv.pr(t, self.n).mx().pr("= ", self.eq).mx().pr(fmt % val, self.n).mx().pr(" ", self.n).mx()
        cv.next_row()

    def led(self, title, on):
        self.cv.move(1, 0)
        if on & 1:
            self.cv.pr(title, W_LEDON).mx()
        else:
            self.cv.move(len(title), 0)

    def cxy(self, x, y): self.cv.set_xy(self.colx + x, self.y + y)


D_VMODE = ["ZX  ", "16c ", "256c", "text"]
D_RRES = ["256x192", "320x200", "320x240", "360x288"]
D_CLK = ["3.5M", "7M", "14M", "unk"]
D_DMA = ["unk    ", "unk    ", "RAM-RAM", "BLT-RAM", "SPI-RAM", "RAM-SPI", "IDE-RAM", "RAM-IDE",
         "FIL-RAM", "RAM-CRM", "unk    ", "RAM-SFL", "unk    ", "unk    ", "unk    ", "unk    "]


def show_tsconf(S, st):
    t = st["ts"]
    cv = Canvas(S, 88, 0)
    cols = [[], [], []]
    # (painter, height) in column order exactly as init_regs_page()

    def vconfig(c):
        c.reg_frame("VConfig", t["vconf"])
        c.bit_set("RRES", 76, D_RRES, t["rres"]); c.bit("NOGFX", 5, t["nogfx"]); c.bit("NOTSU", 4, t["notsu"])
        c.bit("GFXOVR", 3, t["gfxovr"]); c.bit("FT_EN", 2, t["ft_en"]); c.bit_set("VMODE", 10, D_VMODE, t["vmode"])

    def tsconfig(c):
        c.reg_frame("TSConfig", t["tsconf"])
        c.bit("S_EN", 7, t["s_en"]); c.bit("T1_EN", 6, t["t1_en"]); c.bit("T0_EN", 5, t["t0_en"])
        c.bit("T1Z_EN", 3, t["t1z_en"]); c.bit("T0Z_EN", 2, t["t0z_en"]); c.bit("TS_EXT", 0, t["tsconf"])

    def sysconfig(c):
        c.reg_frame("SysConfig", t["sysconf"])
        c.bit("CACHE_EN", 2, t["sysconf"] >> 2); c.bit_set("ZCLK", 10, D_CLK, t["zclk"])

    def cacheconfig(c):
        c.reg_frame("CacheConfig", t["cacheconf"])
        for k, sh in (("EN_C000", 3), ("EN_8000", 2), ("EN_4000", 1), ("EN_0000", 0)):
            c.bit(k, 4, t["cacheconf"] >> sh)          # NB: original prints bit number 4 for all rows

    def memconfig(c):
        c.reg_frame("MemConfig", t["memconf"])
        c.bit("LCK128", 76, t["s_en"])                  # NB: original shows S_EN here (bug)
        c.bit("W0_RAM", 3, t["w0_ram"] ^ 1); c.bit("W0_MAP", 2, t["w0_map_n"] ^ 1)
        c.bit("W0_WE", 1, t["w0_we"]); c.bit("ROM128", 0, t["rom128"])

    def bitmap(c):
        c.cxy(19, 0); c.led("EN", 0 if t["nogfx"] else 1)
        c.reg_frame("Bitmap")
        c.port("VPage", t["vpage"])
        c.hl_port("X", t["g_xoffs"] >> 8, t["g_xoffs"] & 0xFF, t["g_xoffs"])
        c.hl_port("Y", t["g_yoffs"] >> 8, t["g_yoffs"] & 0xFF, t["g_yoffs"])

    def tiles(n):
        def f(c):
            c.reg_frame("Tiles%d" % n)
            c.cxy(14, 0); c.led("Z_EN", t["t%dz_en" % n]); c.led("EN", t["t%d_en" % n]); c.cv.next_row()
            c.port("T%dGPage" % n, t["t%dgpage" % n])
            c.hex16("X", 16, t["t%d_xoffs" % n]); c.hex16("Y", 16, t["t%d_yoffs" % n])
        f.__name__ = "tiles%d" % n
        return f

    def palsel(c):
        c.reg_frame("PalSel", t["palsel"])
        c.bit_h("T1PAL", 76, t["t1pal"] << 2); c.bit_h("T0PAL", 54, t["t0pal"] << 2); c.bit_h("GPAL", 30, t["gpal"])

    def misc(c):
        c.reg_frame("Misc")
        c.cxy(6, 0)
        for k, sh in (("FDD", 3), ("FDC", 2), ("FDB", 1), ("FDA", 0)):
            c.led(k, t["fddvirt"] >> sh)
        c.cv.next_row()
        c.port("   TMPage", t["tmpage"]); c.port("   Border", t["border"]); c.port("   FDDVirt", t["fddvirt"])

    def fmaddr(c):
        c.reg_frame("FMAddr", t["fmaddr"])
        c.bit("FM_EN", 4, t["fm_en"]); c.hex16("FM_MAPS", 30, t["fm_addr"] << 12)

    def mempages(c):
        c.reg_frame("MemPages")
        for i in range(4):
            c.port("   Page%d" % i, t["page"][i])

    def sprites(c):
        c.reg_frame("Sprites"); c.port("SGPage", t["sgpage"])

    def dma(c):
        d = t["dma"]
        c.reg_frame("DMA")
        c.cxy(15, 0); c.led("ACTIVE", 1 if d["active"] else 0); c.cv.next_row()
        c.hex24("     SRC", t["saddr"]); c.hex24("CURR SRC", d["saddr"]); c.cv.next_row()
        c.hex24("     DST", t["daddr"]); c.hex24("CURR DST", d["daddr"]); c.cv.next_row()
        c.hex8_inline("     NUM", t["dmanum"]); c.hex8_inline(" LEN", t["dmalen"]); c.cv.next_row()
        c.hex8_inline("CURR NUM", d["num"] & 0xFF); c.hex8_inline(" LEN", d["len"] & 0xFF); c.cv.next_row()
        c.cv.next_row()
        c.port("CTRL", d["ctrl"])
        c.bit("OPT", 6, d["opt"]); c.bit("S_ALIGN", 5, d["s_algn"]); c.bit("D_ALIGN", 4, d["d_algn"])
        c.bit("A_SZ", 3, d["asz"]); c.bit_set("DDEV", 20, D_DMA, (d["dev"] << 1) + d["rw"])

    def interrupt(c):
        c.reg_frame("Interrupt")
        c.port("HSINT", t["hsint"]); c.dec_inline(" h", t["hsint"] << 1); c.cv.next_row()
        c.port("VSINTH", t["vsint"] >> 8); c.port("VSINTL", t["vsint"] & 0xFF)
        c.dec_inline(" v", t["vsint"]); c.dec_inline(" inc", t["vsint"] >> 4)

    def intmask(c):
        c.reg_frame("IntMask")
        c.bit("DMA", 2, t["intdma"]); c.bit("LINE", 1, t["intline"]); c.bit("FRAME", 0, t["intframe"])

    layout = [
        [(vconfig, 6), (tsconfig, 6), (sysconfig, 2), (cacheconfig, 4), (memconfig, 5)],
        [(bitmap, 3), (tiles(0), 3), (tiles(1), 3), (palsel, 3), (misc, 3), (fmaddr, 2), (mempages, 4)],
        [(sprites, 1), (dma, 15), (interrupt, 5), (intmask, 3)],
    ]
    placed = []
    for ci, col in enumerate(layout):
        colx, y = ci * 23, 0
        for painter, h in col:
            placed.append((painter, Control(cv, colx, 23, y, h, active=(painter.__name__ == st.get("ts_active")))))
            y += h + 1
    for painter, ctl in placed:
        painter(ctl)
    return [(p.__name__, c.colx + 88, c.y, c.h) for p, c in placed]


# --------------------------------------------------------------------------------------------
def debugscr(st, variant):
    S = Screen(157 if variant == "tsconf" else 80)
    show_regs(S, st); show_trace(S, st); show_mem(S, st)
    show_watch(S, st, "screen memory (alt)" if (variant == "tsconf" and st.get("scrshot_alt")) else None)
    show_stack(S, st); show_ay(S, st); show_banks(S, st, tsconf=(variant == "tsconf"))
    show_ports(S, st); show_dos(S, st)
    placed = None
    if variant == "tsconf":
        show_pc_history(S, st)
        placed = show_tsconf(S, st)
    show_time(S, st)
    return S, placed


# --------------------------------------------------------------------------------------------
CP437_LOW = dict(zip(range(32), " ☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼"))


def glyph(b):
    if b < 0x20:
        return CP437_LOW[b]
    if b == 0x7F:
        return "⌂"
    if b < 0x80:
        return chr(b)
    return bytes([b]).decode("cp866")


def dump_text(S, visible_w=None):
    w = visible_w or S.w
    out = ["     " + "".join(str((x // 10) % 10) if x % 10 == 0 else " " for x in range(w)),
           "     " + "".join(str(x % 10) for x in range(w))]
    for y in range(S.h):
        out.append("%3d  " % y + "".join(glyph(S.ch[y * S.w + x]) for x in range(w)))
    return "\n".join(out)


def dump_attr(S, visible_w=None):
    w = visible_w or S.w
    out = []
    for y in range(S.h):
        out.append("%3d  " % y + " ".join("%02X" % S.at[y * S.w + x] for x in range(w)))
    return "\n".join(out)


def palette(level_norm=0xC0, level_bright=0xFF):
    pal = []
    for i in range(16):
        y = level_bright if i & 8 else level_norm
        pal.append(((y if i & 2 else 0), (y if i & 4 else 0), (y if i & 1 else 0)))
    return pal


def render_png(S, font, path, st, visible_w=None, variant="classic"):
    from PIL import Image
    w = visible_w or S.w
    img = Image.new("RGB", (w * 8, S.h * 16))
    px = img.load()
    pal = palette()
    # screen preview (pentevo style) inside the watch box
    if st["show_scrshot"]:
        border = st["ts"]["border"] & 7 if variant == "tsconf" else 0
        for y in range(13 * 16):
            for x in range(37 * 8):
                px[34 * 8 + x, 16 + y] = pal[border]
    for y in range(S.h):
        for x in range(w):
            a = S.at[y * S.w + x]
            if a == 0xFF:
                continue
            ch = S.ch[y * S.w + x]
            for by in range(16):
                f = font[ch * 16 + by]
                for bx in range(8):
                    px[x * 8 + bx, y * 16 + by] = pal[(a & 0xF) if f & (0x80 >> bx) else (a >> 4)]
    for (x, y, dx, dy, c) in S.frames:
        col = pal[(c | 8) & 0xF]
        def put(xx, yy):
            if 0 <= xx < w * 8 and 0 <= yy < S.h * 16:
                px[xx, yy] = col
        for xx in range(8 * x - 1, (x + dx) * 8):
            put(xx, y * 16 - 1); put(xx, (y + dy) * 16)
        for yy in range(16 * y, (y + dy) * 16):
            put(x * 8 - 1, yy); put((x + dx) * 8, yy)
    img = img.resize((img.width * 2, img.height * 2), Image.NEAREST)
    img.save(path)


def load_font(path):
    data = open(path, "rb").read()
    if path.endswith(".cpp"):
        s = data.decode("latin1")
        s = s[s.index("{"):]
        return [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", s)]
    return list(data)


# --------------------------------------------------------------------------------------------
def sample_state():
    mem = bytearray(0x10000)
    prog = {0x8000: ([0xF3], "di"), 0x8001: ([0x31, 0x00, 0xC0], "ld   sp,C000"),
            0x8004: ([0x21, 0x00, 0x40], "ld   hl,4000"), 0x8007: ([0x11, 0x01, 0x40], "ld   de,4001"),
            0x800A: ([0x01, 0xFF, 0x17], "ld   bc,17FF"), 0x800D: ([0x36, 0x00], "ld   (hl),00"),
            0x800F: ([0xED, 0xB0], "ldir"), 0x8011: ([0xCD, 0x20, 0x80], "call 8020"),
            0x8014: ([0x18, 0xFE], "jr   8014"), 0x8020: ([0x3E, 0x07], "ld   a,07"),
            0x8022: ([0xD3, 0xFE], "out  (FE),a"), 0x8024: ([0xDD, 0x36, 0x05, 0x10], "ld   (ix+05),10"),
            0x8028: ([0xFD, 0xCB, 0x01, 0xC6], "set  0,(iy+01)"), 0x802C: ([0xC9], "ret")}
    for a, (b, _) in prog.items():
        mem[a:a + len(b)] = bytes(b)
    msg = b"UNREAL SPECCY DEBUGGER"
    mem[0xC000:0xC000 + len(msg)] = msg
    mem[0xBFFE] = 0x14; mem[0xBFFF] = 0x80  # return address on stack
    for i in range(0x4000, 0x4010):
        mem[i] = i & 0xFF

    def disasm(pc):
        if pc in prog:
            b, t = prog[pc]
        else:
            b, t = [mem[pc]], "nop"
        hx = "".join("%02X" % x for x in b[-4:])
        if len(b) > 4:
            hx = ".." + hx
        return pc, len(b), ("%04X " % pc + hx).ljust(16) + t

    cpu = dict(a=0x07, f=0x44, bc=0x17FF, de=0x4001, hl=0x4000, **{"af'": 0x0000, "bc'": 0x0000, "de'": 0x0000,
               "hl'": 0x0000}, sp=0xBFFE, pc=0x8011, ix=0x5C3A, iy=0x5C3A, i=0x3F, r=0x12, im=1, iff1=0, iff2=0,
               t=17023, halted=False, last_branch=0x800F)
    prev = dict(cpu); prev.update(bc=0x0000, pc=0x800F, f=0x40, r=0x10)
    return dict(
        cpu=cpu, prev=prev, active="trace", regs_curs=0, cpu_index=0,
        trace_top=0x8000, trace_curs=0x8011, trace_mode=2, bpx={0x8022},
        pc_jump=dict(target=0x8020, braddr=False), disasm=disasm,
        mem=mem, mem_top=0xC000, mem_curs=0xC003, mem_ascii=0, mem_second=0, gsdmaaddr=0,
        show_scrshot=0, dosports=False, user_watches=[0x4000, 0x8000, 0xC000],
        ay_scheme=1, ay_name="AY:", ay_regs=[0x1C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x0F, 0, 0, 0, 0, 0, 0xFF, 0xBF],
        ay_active_reg=7,
        banks=[("BASIC", True), ("RAM 5", False), ("RAM 2", False), ("RAM 0", False)],
        pFE=0x07, p7FFD=0x10, lock48=False, extport=None, cmos_addr=0x00, pEFF7=0x00,
        trdos_present=True, wd=dict(cmd=0x80, data=0x00, status=0x20, sector=0x01, drive_track=0, track=0, system=0x3C, rqs=0x80),
        time_delta=12, pc_hist=[([0x00, 0x05, 0x02, 0x00][((0x800F - i) >> 14) & 3], 0x800F - i) for i in range(28)],
        ts=dict(vconf=0x00, rres=0, nogfx=0, notsu=0, gfxovr=0, ft_en=0, vmode=0, tsconf=0x00, s_en=0, t1_en=0, t0_en=0,
                t1z_en=0, t0z_en=0, sysconf=0x02, zclk=2, cacheconf=0x00, memconf=0x04, w0_ram=0, w0_map_n=1, w0_we=0, rom128=0,
                vpage=0x05, g_xoffs=0x000, g_yoffs=0x000, t0gpage=0x00, t1gpage=0x00, t0_xoffs=0, t0_yoffs=0, t1_xoffs=0, t1_yoffs=0,
                palsel=0x0F, t1pal=0, t0pal=0, gpal=0xF, tmpage=0x00, border=0x07, fddvirt=0x00, fmaddr=0x00, fm_en=0, fm_addr=0,
                page=[0x00, 0x05, 0x02, 0x00], sgpage=0x00, saddr=0, daddr=0, dmanum=0, dmalen=0,
                dma=dict(active=False, saddr=0, daddr=0, num=0, len=0, ctrl=0, opt=0, s_algn=0, d_algn=0, asz=0, dev=0, rw=0),
                hsint=0x00, vsint=0x000, intdma=0, intline=0, intframe=1),
    )


if __name__ == "__main__":
    font = load_font(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    st = sample_state()
    S, _ = debugscr(st, "classic")
    open(os.path.join(out, "classic.txt"), "w").write(dump_text(S))
    open(os.path.join(out, "classic_attr.txt"), "w").write(dump_attr(S))
    render_png(S, font, os.path.join(out, "classic.png"), st)
    st2 = sample_state()
    st2["banks"] = [("ROM 0", True), ("RAM 5", False), ("RAM 2", False), ("RAM 0", False)]
    S2, placed = debugscr(st2, "tsconf")
    open(os.path.join(out, "tsconf.txt"), "w").write(dump_text(S2))
    open(os.path.join(out, "tsconf_attr.txt"), "w").write(dump_attr(S2))
    render_png(S2, font, os.path.join(out, "tsconf.png"), st2, variant="tsconf")
    for p in placed:
        print(p)
