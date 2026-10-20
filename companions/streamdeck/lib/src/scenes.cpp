#include "sdb/scenes.h"

#include <algorithm>
#include <cmath>

#include "sdb/format.h"
#include "sdb/svg.h"

namespace sdb::scenes
{
using namespace draw;
namespace
{
// Diagonal Spectrum flag stripes (red, yellow, green, cyan) clipped to [x, x+w).
void FlagStripes(Rgb& im, int x, int w, int phase, bool reverse = false)
{
    const Color cols[4] = {kBRed, kBYellow, kBGreen, kBCyan};
    const int band = 14;
    for (int j = 0; j < im.h; ++j)
        for (int i = x; i < x + w && i < im.w; ++i)
        {
            const int d = (i + (reverse ? -j : j) + phase);
            const int idx = ((d / band) % 4 + 4) % 4;
            auto* p = im.At(i, j);
            p[0] = cols[idx].r;
            p[1] = cols[idx].g;
            p[2] = cols[idx].b;
        }
}

std::string Clip(std::string s, size_t n)
{
    for (auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (s.size() > n)
        s.resize(n);
    return s;
}
std::string Kfmt(int v)
{
    return v >= 1000 && v % 100 == 0
               ? (v % 1000 == 0 ? sdb::Format("{}K", v / 1000) : sdb::Format("{:.1f}K", v / 1000.0))
               : std::to_string(v);
}
}  // namespace

// ---------------------------------------------------------------- balance
Rgb Balance(Size sz, int value, int frame)
{
    value = std::clamp(value, -100, 100);
    Rgb im(sz.w, sz.h);
    GradV(im, 0, 0, sz.w, sz.h, {10, 10, 30}, {22, 22, 48});
    const int x0 = 130, x1 = sz.w - 130, cx = sz.w / 2, tw = x1 - x0;
    const int ty = 46, th = 24, cy = ty + th / 2;
    RoundRect(im, x0, ty, tw, th, 6, {8, 8, 16});
    const int len = static_cast<int>(std::abs(value) / 100.f * (tw / 2));
    if (value < 0)
        GradH(im, cx - len, ty + 2, len, th - 4, kBBlue, kBCyan);
    else if (value > 0)
        GradH(im, cx, ty + 2, len, th - 4, kBMagenta, kBRed);
    for (int p = -100; p <= 100; p += 10)
    {  // ruler
        const int x = cx + p * (tw / 2) / 100;
        const bool major = p % 50 == 0;
        FillRect(im, x, ty + th + 4, major ? 2 : 1, major ? 12 : 6, major ? kWhite : kGrey);
    }
    FillRect(im, cx - 1, ty - 12, 3, th + 24, kBWhite);  // centre mark
    const int kx = cx + value * (tw / 2) / 100;
    const float pulse = 1.f + 0.08f * std::sin(frame * 0.5f);
    Disc(im, kx, cy, 15 * pulse, {0, 0, 0});
    Disc(im, kx, cy, 13 * pulse, kBWhite);
    Disc(im, kx, cy, 8, value == 0 ? kBGreen : (value < 0 ? kBCyan : kBMagenta));
    TextShadow(im, 38, 30, "L", 7, kBCyan, kBlack);
    TextShadow(im, sz.w - 38 - 30, 30, "R", 7, kBMagenta, kBlack);
    const std::string r = value == 0 ? "CENTER" : sdb::Format("{} {}%", value < 0 ? "LEFT" : "RIGHT", std::abs(value));
    TextCentered(im, cx, 8, r, 3, value == 0 ? kBGreen : kBWhite);
    Text(im, x0, 8, "BALANCE", 2, kGrey);
    return im;
}

// ---------------------------------------------------------------- tape / disk loader
Rgb LoadingScene(Size sz, const Loading& l, int frame)
{
    Rgb im(sz.w, sz.h);
    const Color c1 = l.pilot ? kBRed : kBYellow, c2 = l.pilot ? kBCyan : kBBlue;
    const int band = l.pilot ? 10 : 0;
    // Border stripes that run across the whole strip; data blocks use irregular widths.
    unsigned seed = static_cast<unsigned>(frame) * 2654435761u + 12345u;
    int x = 0, k = frame;
    while (x < sz.w)
    {
        int w = band;
        if (!band)
        {
            seed = seed * 1664525u + 1013904223u;
            w = 3 + (seed >> 24) % 22;
        }
        FillRect(im, x, 0, w, sz.h, (k++ & 1) ? c1 : c2);
        x += w;
    }
    // "Paper" panel, like the Spectrum screen inside the border.
    const int px = 90, py = 9, pw = sz.w - 180, ph = sz.h - 18;
    FillRect(im, px, py, pw, ph, kBlack);
    if (l.iconSvg.empty() || !DrawSvg(im, l.iconSvg, px + 12, py + 6, 68, l.disk ? kBCyan : kBWhite, 0.05f))
        DrawIcon(im, l.disk ? Icon::Disk : Icon::Tape, px + 12, py + 6, 68, l.disk ? kBCyan : kBWhite, kBYellow, frame);
    const int tx = px + 100;
    Text(im, tx, py + 8, sdb::Format("{}: {}", l.bytes ? "BYTES" : "PROGRAM", Clip(l.name, 18)), 3, kBWhite);
    const int bx = tx, by = py + 36, bw = pw - 100 - 100, bh = 20, n = 50;
    const int filled = std::clamp(l.progress, 0, 100) * n / 100;
    for (int i = 0; i < n; ++i)
    {
        const int sx = bx + i * bw / n, ew = bw / n - 2;
        FillRect(im, sx, by, ew, bh,
                 i < filled ? Mix(kBGreen, kBYellow, static_cast<float>(i) / n) : Color{30, 30, 40});
    }
    const std::string pct = sdb::Format("{}%", std::clamp(l.progress, 0, 100));
    Text(im, bx + bw + 12, by, pct, 3, l.progress >= 100 ? kBGreen : kBWhite);
    Text(im, tx, py + 62, l.pilot ? "PILOT TONE..." : (l.progress >= 100 ? "LOADED OK" : "LOADING DATA..."), 2,
         l.progress >= 100 ? kBGreen : kBCyan);
    return im;
}

// ---------------------------------------------------------------- status banners
bool StatusByName(const std::string& n, Status& out)
{
    static const std::pair<const char*, Status> m[] = {{"ok", Status::Ok},           {"error", Status::Error},
                                                       {"warn", Status::Warn},       {"paused", Status::Paused},
                                                       {"resumed", Status::Resumed}, {"muted", Status::Muted},
                                                       {"unmuted", Status::Unmuted}, {"info", Status::Info},
                                                       {"saved", Status::Saved},     {"reset", Status::Reset}};
    for (auto& [name, s] : m)
        if (n == name)
        {
            out = s;
            return true;
        }
    return false;
}

Rgb StatusScene(Size sz, Status st, const std::string& title, const std::string& subtitle, int frame,
                const std::string& iconSvg)
{
    struct Look
    {
        Color a, b, fg;
        Icon icon;
        const char* def;
    };
    Look lk{};
    switch (st)
    {
        case Status::Ok:
            lk = {{0, 120, 20}, {0, 215, 60}, kBWhite, Icon::Check, "OK"};
            break;
        case Status::Error:
            lk = {{120, 0, 0}, {235, 20, 20}, kBWhite, Icon::Cross, "ERROR"};
            break;
        case Status::Warn:
            lk = {{150, 110, 0}, {255, 215, 0}, kBlack, Icon::Warn, "WARNING"};
            break;
        case Status::Paused:
            lk = {{0, 0, 120}, {30, 60, 235}, kBWhite, Icon::Pause, "PAUSED"};
            break;
        case Status::Resumed:
            lk = {{0, 110, 30}, {0, 215, 90}, kBWhite, Icon::Play, "RESUMED"};
            break;
        case Status::Muted:
            lk = {{110, 0, 70}, {225, 20, 130}, kBWhite, Icon::Mute, "MUTED"};
            break;
        case Status::Unmuted:
            lk = {{0, 90, 110}, {0, 200, 215}, kBWhite, Icon::Speaker, "SOUND ON"};
            break;
        case Status::Info:
            lk = {{20, 20, 100}, {60, 90, 230}, kBWhite, Icon::Gear, "INFO"};
            break;
        case Status::Saved:
            lk = {{70, 0, 110}, {170, 40, 230}, kBWhite, Icon::Disk, "SAVED"};
            break;
        case Status::Reset:
            lk = {{110, 40, 0}, {235, 110, 0}, kBWhite, Icon::Reset, "RESET"};
            break;
    }
    Rgb im(sz.w, sz.h);
    GradV(im, 0, 0, sz.w, sz.h, lk.b, lk.a);
    const int side = 120;
    FlagStripes(im, 0, side, frame * 3);
    FlagStripes(im, sz.w - side, side, -frame * 3, true);
    RectAlpha(im, side, 0, 6, sz.h, kBlack, .5f);
    RectAlpha(im, sz.w - side - 6, 0, 6, sz.h, kBlack, .5f);
    const std::string t = Clip(title.empty() ? lk.def : title, 22);
    const bool sub = !subtitle.empty();
    const int scale = sub ? 5 : 7;
    const int isz = sub ? 56 : 70;
    const int tw = TextWidth(t, scale), total = isz + 24 + tw, cx = sz.w / 2;
    const int x = cx - total / 2;
    const float pop = std::min(1.f, frame / 4.f);  // icon grows in over the first frames
    const int s2 = std::max(8, static_cast<int>(isz * (0.6f + 0.4f * pop)));
    if (iconSvg.empty() || !DrawSvg(im, iconSvg, x + (isz - s2) / 2, (sz.h - s2) / 2 - (sub ? 8 : 0), s2, lk.fg))
        DrawIcon(im, lk.icon, x + (isz - s2) / 2, (sz.h - s2) / 2 - (sub ? 8 : 0), s2, lk.fg, lk.fg, frame);
    const int ty = sub ? 14 : (sz.h - 7 * scale) / 2;
    TextShadow(im, x + isz + 24, ty, t, scale, lk.fg, Dim(lk.a, .6f));
    if (sub)
        TextCentered(im, cx, 66, Clip(subtitle, 40), 3, lk.fg);
    return im;
}

// ---------------------------------------------------------------- volume (one tile per audio adapter / dial)
Rgb VolumeTile(int w, int h, const Adapter& a, bool selected, int index, int frame)
{
    Rgb im(w, h);
    GradV(im, 0, 0, w, h, {18, 18, 36}, {8, 8, 18});
    const Color accent = a.muted ? Color{150, 40, 70} : kBCyan;
    Outline(im, 0, 0, w, h, selected ? 3 : 1, selected ? (frame % 8 < 4 ? kBWhite : kBCyan) : kGrey);
    TextCentered(im, w / 2, 8, Clip(a.name, 15), 2, a.muted ? kGrey : kBWhite);
    const int bx = 10, bw = w - 20, by = 30, bh = 18, n = 20;
    const int lit = std::clamp(a.level, 0, 100) * n / 100;
    for (int i = 0; i < n; ++i)
    {
        const float f = static_cast<float>(i) / (n - 1);
        Color c = f < .6f ? Mix(kBGreen, kBYellow, f / .6f) : Mix(kBYellow, kBRed, (f - .6f) / .4f);
        if (a.muted)
            c = Dim(c, .25f);
        FillRect(im, bx + i * bw / n, by, bw / n - 2, bh, i < lit ? c : Color{26, 26, 38});
    }
    Text(im, 12, 58, sdb::Format("{}%", a.muted ? 0 : a.level), 4, a.muted ? kGrey : kBWhite);
    DrawIcon(im, a.muted ? Icon::Mute : Icon::Speaker, w - 54, 54, 40, a.muted ? kBRed : accent,
             a.muted ? kBRed : accent, frame);
    Text(im, 6, h - 10, std::to_string(index + 1), 1, kGrey);
    return im;
}

Rgb Volume(Size sz, const std::vector<Adapter>& ad, int selected, int tileW, int frame)
{
    Rgb im(sz.w, sz.h, 8, 8, 18);
    const int n = std::max(1, sz.w / tileW);
    for (int i = 0; i < n; ++i)
    {
        const Adapter a = i < static_cast<int>(ad.size()) ? ad[i] : Adapter{};
        Blit(im, VolumeTile(tileW, sz.h, a, i == selected, i, frame), i * tileW, 0);
    }
    return im;
}

// ---------------------------------------------------------------- TTD timeline
Rgb TtdScene(Size sz, const Ttd& t, int frame)
{
    Rgb im(sz.w, sz.h);
    GradV(im, 0, 0, sz.w, sz.h, {12, 12, 28}, {5, 5, 14});
    const int total = std::max(1, t.total), pos = std::clamp(t.pos, 0, total);
    const int x0 = 24, x1 = sz.w - 24, tw = x1 - x0, ty = 34, th = 28;
    auto frameX = [&](int f) {
        return x0 + static_cast<int>(static_cast<long long>(std::clamp(f, 0, total)) * tw / total);
    };

    // header
    DrawIcon(im, t.reverse ? Icon::Rewind : Icon::Forward, 18, 3, 22, t.reverse ? kBMagenta : kBGreen, kWhite);
    Text(im, 46, 6, "TTD", 3, kBWhite);
    if (t.absStart >= 0)
    {
        std::string head = sdb::Format("FRAME {}   RANGE {}-{}", t.absStart + pos, t.absStart, t.absStart + total);
        if (t.fps > 0)
            head += "   " + ClockText(pos / t.fps) + " / " + ClockText(total / t.fps);
        TextCentered(im, sz.w / 2, 7, head, 2, kBCyan);
    }
    else
    {
        TextCentered(im, sz.w / 2, 7, sdb::Format("FRAME {} OF {}", pos, total), 2, kBCyan);
    }
    const Color badge = t.live ? kBGreen : kBMagenta;
    RoundRect(im, sz.w - 150, 4, 126, 22, 5, badge);
    TextCentered(im, sz.w - 87, 8, !t.badge.empty() ? t.badge : (t.live ? "LIVE" : (t.reverse ? "REVERSE" : "REPLAY")),
                 2, kBlack);

    // rail: visited (bright) | recorded but ahead (dim)
    RoundRect(im, x0 - 3, ty - 3, tw + 6, th + 6, 8, {30, 30, 52});
    GradH(im, x0, ty, frameX(pos) - x0, th, {20, 40, 140}, kBCyan);
    GradH(im, frameX(pos), ty, x1 - frameX(pos), th, {20, 24, 48}, {28, 34, 70});
    for (int f : t.hits)
        Disc(im, frameX(f), ty + th / 2, 4, kBMagenta);  // watch / exception hits
    for (int f : t.checkpoints)
    {  // snapshot checkpoints
        const float cx = static_cast<float>(frameX(f));
        Tri(im, cx, ty - 12, cx - 6, ty - 3, cx + 6, ty - 3, kBYellow);
        FillRect(im, static_cast<int>(cx), ty - 3, 1, 5, kBYellow);
    }
    for (int f : t.breakpoints)
    {  // breakpoints
        FillRect(im, frameX(f) - 1, ty, 3, th, kBRed);
        Disc(im, frameX(f), ty + th + 8, 4, kBRed);
    }
    // cursor with pulsing glow
    const int cx = frameX(pos);
    const float g = 0.5f + 0.5f * std::sin(frame * 0.6f);
    RectAlpha(im, cx - 7, ty - 8, 15, th + 16, kBWhite, .08f + .1f * g);
    FillRect(im, cx - 1, ty - 10, 3, th + 20, kBWhite);
    Tri(im, static_cast<float>(cx), ty - 8.f, cx - 8.f, ty - 20.f, cx + 8.f, ty - 20.f, kBWhite);
    // moving chevrons show direction
    for (int k = 0; k < 3; ++k)
    {
        const int off = (frame * 3 + k * 14) % 42;
        const int xx = t.reverse ? cx - 14 - off : cx + 14 + off;
        if (xx > x0 && xx < x1 && pos != (t.reverse ? 0 : total))
            Text(im, xx, ty + 7, t.reverse ? "<" : ">", 2, Dim(kBWhite, .7f - off / 100.f));
    }
    // ruler
    for (int i = 0; i <= 20; ++i)
    {
        const int f = total * i / 20, x = frameX(f);
        const bool major = i % 2 == 0;
        FillRect(im, x, ty + th + 6, 1, major ? 8 : 4, major ? kWhite : kGrey);
        if (major && i % 4 == 0)
            TextCentered(im, x, ty + th + 16, t.absStart >= 0 && t.fps > 0 ? ClockText(f / t.fps) : Kfmt(f), 1, kGrey);
    }
    // legend
    Tri(im, 760.f * sz.w / 1200, 90.f, 754.f * sz.w / 1200, 98.f, 766.f * sz.w / 1200, 98.f, kBYellow);
    Text(im, static_cast<int>(772.f * sz.w / 1200), 91, "CHECKPOINT", 1, kGrey);
    FillRect(im, static_cast<int>(870.f * sz.w / 1200), 90, 3, 9, kBRed);
    Text(im, static_cast<int>(880.f * sz.w / 1200), 91, "BREAKPOINT", 1, kGrey);
    Disc(im, 984.f * sz.w / 1200, 94.f, 3.f, kBMagenta);
    Text(im, static_cast<int>(992.f * sz.w / 1200), 91, "HIT", 1, kGrey);
    return im;
}

// ---------------------------------------------------------------- debugger: registers
Rgb Registers(Size sz, const std::vector<Reg>& regs, int flags, int tileW, int frame)
{
    Rgb im(sz.w, sz.h, 8, 8, 18);
    const int n = std::max(1, sz.w / tileW);
    const Color pal[6] = {kBCyan, kBMagenta, kBGreen, kBYellow, {110, 150, 255}, {255, 90, 90}};
    for (int i = 0; i < n; ++i)
    {
        Rgb t(tileW, sz.h);
        GradV(t, 0, 0, tileW, sz.h, {18, 18, 36}, {8, 8, 18});
        const Reg r = i < static_cast<int>(regs.size()) ? regs[i] : Reg{"--", 0, -1};
        const bool changed = r.prev >= 0 && r.prev != r.value;
        if (changed)
        {
            RectAlpha(t, 0, 0, tileW, sz.h, kBYellow, 0.10f + 0.07f * (frame % 6 < 3));
            Outline(t, 0, 0, tileW, sz.h, 3, kBYellow);
        }
        else
            Outline(t, 0, 0, tileW, sz.h, 1, kGrey);
        Text(t, 10, 8, Clip(r.name, 3), 3, pal[i % 6]);
        if (changed)
        {
            RoundRect(t, tileW - 62, 8, 52, 18, 4, kBYellow);
            TextCentered(t, tileW - 36, 12, "NEW", 2, kBlack);
        }
        const std::string hex = sdb::Format("{:04X}", r.value & 0xFFFF);
        TextShadow(t, (tileW - TextWidth(hex, 5)) / 2, 34, hex, 5, changed ? kBYellow : kBWhite, kBlack);
        if (changed)
            TextCentered(t, tileW / 2, 74, sdb::Format("WAS {:04X}", r.prev & 0xFFFF), 2, kGrey);
        else if (i == 0 && flags >= 0)
        {  // flags of AF: S Z - H - P N C
            const char* names = "SZ-H-PNC";
            for (int b = 0; b < 8; ++b)
            {
                const bool on = (flags >> (7 - b)) & 1;
                TextCentered(t, 20 + b * 23, 76, std::string(1, names[b]), 2, on ? kBGreen : Color{50, 50, 66});
            }
        }
        Blit(im, t, i * tileW, 0);
    }
    return im;
}

// ---------------------------------------------------------------- debugger: disassembly
Rgb Disasm(Size sz, const std::vector<AsmLine>& lines, int pc, const std::vector<int>& bps, int tstates, int frame)
{
    Rgb im(sz.w, sz.h);
    GradV(im, 0, 0, sz.w, sz.h, {10, 12, 28}, {4, 4, 12});
    const int rowH = 23, top = 4, textX = 150, panelW = 300, codeW = sz.w - panelW;
    for (int i = 0; i < 4 && i < static_cast<int>(lines.size()); ++i)
    {
        const int y = top + i * rowH;
        const bool cur = i == pc;
        const bool bp = std::find(bps.begin(), bps.end(), lines[i].addr) != bps.end();
        if (cur)
        {
            GradH(im, 0, y, codeW, rowH - 2, {120, 100, 0}, {20, 18, 0});
            Tri(im, 8.f, y + 3.f, 8.f, y + rowH - 5.f, 22.f, y + (rowH - 2) / 2.f, kBYellow);
        }
        if (bp)
            Disc(im, 36, y + (rowH - 2) / 2.f, 6.f, kBRed);
        Text(im, 56, y + 3, sdb::Format("{:04X}", lines[i].addr & 0xFFFF), 2, cur ? kBWhite : kCyan);
        Text(im, textX, y + 3, Clip(lines[i].text, 28), 2, cur ? kBWhite : Dim(kBWhite, .65f));
    }
    FillRect(im, codeW, 0, 3, sz.h, kGrey);
    // right panel: PC + T-states
    const int pcAddr = (pc >= 0 && pc < static_cast<int>(lines.size())) ? lines[pc].addr : 0;
    Text(im, codeW + 18, 8, "PC", 3, kGrey);
    TextShadow(im, codeW + 70, 4, sdb::Format("{:04X}", pcAddr & 0xFFFF), 5, kBYellow, kBlack);
    if (tstates >= 0)
    {
        Text(im, codeW + 18, 50, "T", 2, kGrey);
        Text(im, codeW + 40, 50, std::to_string(tstates), 2, kBCyan);
    }
    const bool atBp = std::find(bps.begin(), bps.end(), pcAddr) != bps.end();
    RoundRect(im, codeW + 18, 72, panelW - 36, 22, 5,
              atBp ? (frame % 8 < 4 ? kBRed : Color{150, 0, 0}) : Color{30, 34, 70});
    TextCentered(im, codeW + panelW / 2, 77, atBp ? "BREAKPOINT" : "STEPPING", 2, kBWhite);
    return im;
}

// ---------------------------------------------------------------- debugger: memory dump
Rgb Memory(Size sz, int addr, const std::vector<int>& bytes, const std::vector<int>& hl, int frame)
{
    Rgb im(sz.w, sz.h);
    GradV(im, 0, 0, sz.w, sz.h, {10, 12, 24}, {4, 4, 10});
    const int per = 20, rows = 4, rowH = 24;
    for (int r = 0; r < rows; ++r)
    {
        const int y = 3 + r * rowH, base = r * per;
        Text(im, 10, y + 5, sdb::Format("{:04X}", (addr + base) & 0xFFFF), 2, kCyan);
        std::string asciiPart;
        for (int c = 0; c < per; ++c)
        {
            const int idx = base + c;
            const bool have = idx < static_cast<int>(bytes.size());
            const bool h = std::find(hl.begin(), hl.end(), idx) != hl.end();
            const int x = 76 + c * 36;
            if (h)
            {
                RoundRect(im, x - 3, y + 1, 34, 20, 3, frame % 6 < 3 ? kBRed : Color{150, 0, 0});
            }
            Text(im, x, y + 5, have ? sdb::Format("{:02X}", bytes[idx] & 0xFF) : "--", 2,
                 h ? kBWhite : (have ? kBWhite : kGrey));
            const int b = have ? bytes[idx] & 0xFF : 0;
            asciiPart +=
                (have && b >= 32 && b < 127 && draw::Renderable(std::string(1, static_cast<char>(b))) && b != ' ')
                    ? static_cast<char>(b)
                    : '.';
        }
        FillRect(im, 76 + per * 36 + 2, y + 2, 2, 18, kGrey);
        Text(im, 76 + per * 36 + 14, y + 5, asciiPart, 2, Dim(kBGreen, .85f));
    }
    return im;
}

// ---------------------------------------------------------------- debugger: call stack
Rgb CallStack(Size sz, const std::vector<Frame>& fr, int cur, int frame)
{
    Rgb im(sz.w, sz.h);
    GradV(im, 0, 0, sz.w, sz.h, {12, 12, 30}, {5, 5, 14});
    Text(im, 14, 8, "CALL STACK", 2, kGrey);
    Text(im, 14, 28, "TOP", 2, kBYellow);
    Text(im, 14, 48, ">", 3, kBYellow);
    const int bw = 170, gap = 50, x0 = 130, y = 14, bh = 72;
    for (int i = 0; i < static_cast<int>(fr.size()) && i < 5; ++i)
    {
        const int x = x0 + i * (bw + gap);
        const bool top = i == 0, is = i == cur;
        RoundRect(im, x, y, bw, bh, 10, is ? kBYellow : (top ? Color{40, 90, 200} : Color{40, 44, 76}));
        RoundRect(im, x + 3, y + 3, bw - 6, bh - 6, 8, is ? Color{70, 60, 0} : Color{14, 16, 34});
        TextCentered(im, x + bw / 2, y + 10, sdb::Format("{:04X}", fr[i].addr & 0xFFFF), 4, is ? kBYellow : kBWhite);
        TextCentered(im, x + bw / 2, y + 46, Clip(fr[i].name, 10), 2, is ? kBWhite : kBCyan);
        if (i + 1 < static_cast<int>(fr.size()) && i < 4)
        {
            const int ax = x + bw + 8;
            Line(im, static_cast<float>(ax), y + bh / 2.f, static_cast<float>(ax + gap - 24), y + bh / 2.f, 4.f, kGrey);
            Tri(im, ax + gap - 8.f, y + bh / 2.f, ax + gap - 24.f, y + bh / 2.f - 9.f, ax + gap - 24.f,
                y + bh / 2.f + 9.f, kGrey);
        }
    }
    (void)frame;
    return im;
}

// ---------------------------------------------------------------- TTD live / recording
std::string ClockText(double seconds)
{
    const long long s = std::max(0LL, static_cast<long long>(seconds));
    return s >= 3600 ? sdb::Format("{}:{:02}:{:02}", s / 3600, (s / 60) % 60, s % 60)
                     : sdb::Format("{}:{:02}", s / 60, s % 60);
}

Rgb TtdLive(Size sz, bool paused, int frame)
{
    Rgb im(sz.w, sz.h);
    const Color base = paused ? Color{150, 110, 0} : Color{0, 150, 50};
    GradV(im, 0, 0, sz.w, sz.h, Dim(base, .35f), Dim(base, .12f));
    // heartbeat trace scrolling to the left (frozen while paused)
    const int y0 = sz.h / 2 + 6, x0 = 330, x1 = sz.w - 40;
    float px = static_cast<float>(x0), py = static_cast<float>(y0);
    for (int x = x0; x <= x1; x += 3)
    {
        const int ph = (x - x0 + (paused ? 0 : frame * 6)) % 180;
        float y = static_cast<float>(y0);
        if (ph > 60 && ph < 70)
            y -= (ph - 60) * 3.2f;
        else if (ph >= 70 && ph < 80)
            y += (ph - 75) * 3.0f - 15;
        else if (ph >= 80 && ph < 88)
            y += (88 - ph) * 1.5f;
        Line(im, px, py, static_cast<float>(x), y, 2.5f,
             Mix(Dim(base, .6f), paused ? kBYellow : kBGreen, static_cast<float>(x - x0) / (x1 - x0)));
        px = static_cast<float>(x);
        py = y;
    }
    const float pulse = paused ? 1.f : 0.6f + 0.4f * std::sin(frame * 0.25f);
    Disc(im, 48, sz.h / 2.f, 16, Dim(paused ? kBYellow : kBGreen, pulse));
    Ring(im, 48, sz.h / 2.f, 22, 2.5f, paused ? kBYellow : kBGreen);
    TextShadow(im, 86, 20, paused ? "PAUSED" : "LIVE", 6, kBWhite, Dim(base, .3f));
    Text(im, 88, 72, "TTD OFF - PRESS TTD TO RECORD", 2, Dim(kBWhite, .75f));
    return im;
}

Rgb TtdRecording(Size sz, long long from, long long to, double fps, int frame)
{
    Rgb im(sz.w, sz.h);
    GradV(im, 0, 0, sz.w, sz.h, {70, 0, 8}, {24, 0, 4});
    // blinking record dot + REC
    if (frame % 16 < 11)
        Disc(im, 40, 30, 14, kBRed);
    Ring(im, 40, 30, 19, 2.f, kBRed);
    TextShadow(im, 66, 15, "REC", 4, kBWhite, {60, 0, 0});
    // counters: start stays put, the end keeps counting
    const std::string fr = sdb::Format("FRAME {} TO {}", from, to);
    Text(im, 200, 12, fr, 3, kBWhite);
    const std::string tm = fps > 0 ? ClockText((to - from) / fps) : "";
    if (!tm.empty())
        Text(im, sz.w - 24 - TextWidth(tm, 4), 10, tm, 4, kBYellow);
    // indeterminate progress: barber-pole stripes sliding right inside the rail, plus a travelling glow
    const int rx = 24, ry = 58, rw = sz.w - 48, rh = 26;
    RoundRect(im, rx - 3.f, ry - 3.f, rw + 6.f, rh + 6.f, 8, {90, 10, 20});
    for (int j = 0; j < rh; ++j)
        for (int i = 0; i < rw; ++i)
        {
            const int d = ((i + j - frame * 4) % 36 + 36) % 36;
            const Color c = d < 18 ? Color{200, 20, 40} : Color{120, 0, 18};
            auto* p = im.At(rx + i, ry + j);
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
        }
    const int gx = rx + (frame * 18) % (rw + 200) - 100;
    for (int i = -100; i < 100; ++i)
    {
        const int x = gx + i;
        if (x < rx || x >= rx + rw)
            continue;
        RectAlpha(im, x, ry, 1, rh, kBWhite, 0.35f * (1.f - std::abs(i) / 100.f));
    }
    return im;
}

// ---------------------------------------------------------------- machine config carousel
Rgb Configs(Size sz, const std::vector<std::string>& names, int selected, int frame)
{
    Rgb im(sz.w, sz.h);
    GradV(im, 0, 0, sz.w, sz.h, {16, 16, 34}, {6, 6, 16});
    const Color pal[6] = {kBBlue, kBRed, kBMagenta, kBGreen, kBCyan, kBYellow};
    const int n = static_cast<int>(names.size());
    if (n == 0)
        return im;
    selected = std::clamp(selected, 0, n - 1);
    const int cx = sz.w / 2, step = sz.w / 4;
    for (int d = -2; d <= 2; ++d)
    {
        const int i = selected + d;
        if (i < 0 || i >= n)
            continue;
        const std::string nm = Clip(names[i], 12);
        const int x = cx + d * step;
        if (d == 0)
        {
            const int bw = std::max(300, TextWidth(nm, 6) + 120);
            RoundRect(im, x - bw / 2.f - 4, 6, bw + 8.f, 88, 14, kBWhite);
            RoundRect(im, x - bw / 2.f, 10, static_cast<float>(bw), 80, 12, pal[i % 6]);
            DrawIcon(im, Icon::Chip, x - bw / 2 + 12, 26, 48, kBWhite, kBWhite, frame);
            TextShadow(im, x - bw / 2 + 70, 29, nm, 6, i % 6 == 5 || i % 6 == 4 ? kBlack : kBWhite,
                       Dim(pal[i % 6], .4f));
            const int ux = (frame * 8) % 80;
            for (int k = -1; k < bw / 80 + 1; ++k)
            {
                const Color rc[4] = {kBRed, kBYellow, kBGreen, kBCyan};
                FillRect(im, x - bw / 2 + 8 + k * 80 + ux, 82, 20, 4, rc[((k % 4) + 4) % 4]);
            }
        }
        else
        {
            const int sc = std::abs(d) == 1 ? 4 : 3;
            if (x + TextWidth(nm, sc) / 2 > sz.w - 56 || x - TextWidth(nm, sc) / 2 < 56)
                continue;  // keep clear of the arrows
            TextCentered(im, x, 50 - 7 * sc / 2, nm, sc, Dim(pal[i % 6], std::abs(d) == 1 ? .75f : .45f));
        }
    }
    if (selected > 0)
        Text(im, 14, 34, "<", 4, kBWhite);
    if (selected < n - 1)
        Text(im, sz.w - 14 - 24, 34, ">", 4, kBWhite);
    return im;
}

// ---------------------------------------------------------------- rainbow marquee
Rgb Marquee(Size sz, const std::string& msg, int frame)
{
    Rgb im(sz.w, sz.h, 0, 0, 0);
    for (int j = 0; j < sz.h; j += 4)
        FillRect(im, 0, j, sz.w, 1, {14, 14, 24});  // scanlines
    const std::string m = Clip(msg, 80);
    const int scale = 7, tw = TextWidth(m, scale), span = sz.w + tw + 40;
    int x = sz.w - (frame * 10) % span;
    for (size_t i = 0; i < m.size(); ++i)
    {
        const Color c = Hsv(frame * 0.02f + i * 0.06f, 1.f, 1.f);
        TextShadow(im, x + static_cast<int>(i) * 6 * scale,
                   (sz.h - 7 * scale) / 2 + static_cast<int>(3 * std::sin(frame * .4f + i * .6f)), std::string(1, m[i]),
                   scale, c, Dim(c, .25f));
    }
    return im;
}

// ---------------------------------------------------------------- splash
Rgb Splash(Size sz, int frame)
{
    Rgb im(sz.w, sz.h);
    GradV(im, 0, 0, sz.w, sz.h, {10, 10, 40}, {0, 0, 0});
    FlagStripes(im, sz.w - 260, 260, -frame * 4, true);
    FlagStripes(im, 0, 260, frame * 4);
    FillRect(im, 260, 0, 6, sz.h, kBlack);
    FillRect(im, sz.w - 266, 0, 6, sz.h, kBlack);
    DrawIcon(im, Icon::Chip, sz.w / 2 - 330, 14, 72, kBWhite, kBWhite, frame);
    TextShadow(im, sz.w / 2 - 240, 8, "UNREAL NG", 8, kBWhite, {40, 40, 120});
    TextCentered(im, sz.w / 2 + 40, 78, "ZX SPECTRUM EMULATOR", 2, kBCyan);
    return im;
}

}  // namespace sdb::scenes
