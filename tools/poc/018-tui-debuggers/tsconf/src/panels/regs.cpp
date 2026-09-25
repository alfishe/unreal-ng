// regs.cpp - registers block (left column top) + bottom info bar.
#include "panels/paint.h"

#include <cstdio>
#include <string>

#include "model/hexfmt.h"
#include "model/model.h"
#include "ui/keymap.h"

namespace dbg {


void PaintRegs(TextScreen& s, IDebuggerBackend& be, const UiState& ui) {
    const Z80Regs r = be.GetRegs(0);
    const MachineInfo machine = be.GetMachineInfo();
    const bool act = (ui.activeWidget == WidgetId::Regs);
    const uint8_t attr = act ? kWSel : kWNorm;

    s.Tprint(1, 0, "regs", kWTitle);
    s.Tprint(1, 1, "af:**** af'**** sp:**** ir: ****", attr);
    s.Tprint(1, 2, "bc:**** bc'**** pc:**** t:******", attr);
    s.Tprint(1, 3, "de:**** de'**** ix:**** im?,i:**", attr);
    s.Tprint(1, 4, "hl:**** hl'**** iy:**** ########", attr);

    // DiHALT or T counter
    if (r.halted && !r.iff1) {
        s.Tprint(27, 2, "DiHALT", act ? kWDihalt1 : kWDihalt2);
    } else {
        const uint32_t tVal = be.GetAbsoluteT(0) % machine.frameLength;
        char tBuf[16];
        std::snprintf(tBuf, sizeof(tBuf), "%6u", tVal);
        s.Tprint(27, 2, tBuf, attr);
    }
    s.Tprint(31, 3, Hex2(0), attr);

    // 26 layout fields
    for (int i = 0; i < 26; ++i) {
        const auto& l = kRegLayout[i];
        const int px = 1 + l.relX;
        const int py = 1 + l.relY;
        const bool isCurs = act && (ui.regsCurs == i);
        const uint8_t fieldAttr = isCurs ? kWCurs : attr;

        std::string valStr;
        if (isCurs && ui.isEditing) {
            const int len = (l.width == 16) ? 4 : (l.width == 8 ? 2 : 1);
            valStr = StrFit(ui.editBuf, len);
        } else {
            switch (i) {
                case 0: valStr = Hex2(r.a); break;
                case 1: valStr = Hex2(r.f); break;
                case 2: valStr = Hex4(r.bc); break;
                case 3: valStr = Hex4(r.de); break;
                case 4: valStr = Hex4(r.hl); break;
                case 5: valStr = Hex4(r.af2); break;
                case 6: valStr = Hex4(r.bc2); break;
                case 7: valStr = Hex4(r.de2); break;
                case 8: valStr = Hex4(r.hl2); break;
                case 9: valStr = Hex4(r.sp); break;
                case 10: valStr = Hex4(r.pc); break;
                case 11: valStr = Hex4(r.ix); break;
                case 12: valStr = Hex4(r.iy); break;
                case 13: valStr = Hex2(r.i); break;
                case 14: valStr = Hex2(r.r); break;
                case 15: valStr = HexDigit(r.im); break;
                case 16: valStr = HexDigit(r.iff1); break;
                case 17: valStr = HexDigit(r.iff2); break;
                case 18: valStr = (r.f & 0x80) ? "S" : "s"; break;
                case 19: valStr = (r.f & 0x40) ? "Z" : "z"; break;
                case 20: valStr = (r.f & 0x20) ? "5" : "."; break;
                case 21: valStr = (r.f & 0x10) ? "H" : "h"; break;
                case 22: valStr = (r.f & 0x08) ? "3" : "."; break;
                case 23: valStr = (r.f & 0x04) ? "P" : "p"; break;
                case 24: valStr = (r.f & 0x02) ? "N" : "n"; break;
                case 25: valStr = (r.f & 0x01) ? "C" : "c"; break;
                default: break;
            }
        }
        s.Tprint(px, py, valStr, fieldAttr);
    }

    s.Frame(1, 1, 32, 4, kFrameColor);
}

void PaintBottomBar(TextScreen& s, IDebuggerBackend& be, const UiState& /*ui*/) {
    // Time delta panel:
    s.Tprint(1, 28, "time delta:", kWOtheroff);
    const int delta = static_cast<int>(be.GetTimeDelta(0));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%14d", delta);
    s.Tprint(12, 28, buf, kWOther);
    s.Tprint(26, 28, "t", kWOtheroff);
    s.Frame(1, 28, 26, 1, kFrameColor);

    // AY:
    const AyState ay = be.GetAy();
    s.Tprint(28, 28, ay.ChipLabel(), kWTitle);
    const int activeReg = ay.latchedReg[static_cast<size_t>(ay.activeChip & 1)];
    for (int reg = 0; reg < 16; ++reg) {
        const char digit = "0123456789ABCDEF"[reg];
        s.Tprint(31 + reg * 3, 28, std::string(1, digit), kWAynum);

        const uint8_t regAttr = (reg == activeReg) ? kWAyon : kWAyoff;
        std::string valStr = Hex2(ay.regs[static_cast<size_t>(ay.activeChip & 1)][reg]);
        s.Tprint(31 + reg * 3 + 1, 28, valStr, regAttr);
    }
    s.Frame(31, 28, 48, 1, kFrameColor);
}

}  // namespace dbg
