// side.cpp - mid column (watches + memory dump) + right column (ports,
// beta128, stack, pages). Field geometry measured from the golden dump:
//   row = label/addr x34..37, ' ' x38, 8 byte pairs x39..61, ' ' x62,
//         ascii x63..70;  right column x72..78 right-aligned to 7.
#include "panels/paint.h"

#include <cstdint>
#include <string>
#include <vector>

#include "model/hexfmt.h"
#include "model/model.h"

namespace dbg {

namespace {

std::string BytesText(const std::vector<uint8_t>& bytes) {
    std::string out;
    for (size_t k = 0; k < bytes.size(); ++k) {
        if (k != 0) out += ' ';
        out += Hex2(bytes[k]);
    }
    return out;
}

std::string AsciiText(const std::vector<uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

uint16_t WordAt(IDebuggerBackend& be, uint16_t addr) {
    const std::vector<uint8_t> b = be.ReadMemory(0, addr, 2);
    return static_cast<uint16_t>(b[0] | (b[1] << 8));
}

void WatchRow(TextScreen& s, IDebuggerBackend& be, const std::string& label,
              uint16_t addr, int y, uint8_t baseAttr, bool isCurs, bool isEditing,
              const std::string& editBuf) {
    const std::vector<uint8_t> bytes = be.ReadMemory(0, addr, 8);
    const uint8_t labelAttr = isCurs ? kWCurs : baseAttr;
    std::string lbl = label;
    if (isCurs && isEditing) {
        lbl = StrFit(editBuf, 4);
    }
    if (lbl.size() > 3) {
        s.Tprint(34, y, StrFit(lbl, 4) + " ", labelAttr);
    } else {
        s.Tprint(34, y, StrRight(lbl, 3) + ": ", labelAttr);
    }
    s.Tprint(39, y, BytesText(bytes) + " " + AsciiText(bytes), baseAttr);
}

void DumpRow(TextScreen& s, IDebuggerBackend& be, uint16_t addr, int y, uint8_t attr,
             uint16_t memCurs, bool actMem, bool isEditing, bool memAscii,
             const std::string& editBuf) {
    const std::vector<uint8_t> bytes = be.ReadMemory(0, addr, 8);
    s.Tprint(34, y, Hex4(addr) + " ", attr);
    s.Tprint(39, y, BytesText(bytes) + " " + AsciiText(bytes), attr);
    if (actMem && memCurs >= addr && memCurs < addr + 8) {
        const int dx = static_cast<int>(memCurs - addr);
        s.SetAttr(39 + dx * 3, y, kWCurs);
        s.SetAttr(39 + dx * 3 + 1, y, kWCurs);
        s.SetAttr(63 + dx, y, kWCurs);
        if (isEditing && !memAscii) {
            s.Tprint(39 + dx * 3, y, StrFit(editBuf, 2), kWCurs);
        }
    }
}

void Right(TextScreen& s, int y, const std::string& text, uint8_t attr, bool isCurs) {
    const uint8_t rowAttr = isCurs ? kWCurs : attr;
    s.Tprint(72, y, StrRight(text, 7), rowAttr);
}

}  // namespace

void PaintSide(TextScreen& s, IDebuggerBackend& be, const UiState& ui) {
    const Z80Regs r = be.GetRegs(0);
    const PortsState ports = be.GetPorts();
    const Beta128State beta = be.GetBeta128();
    const MachineInfo machine = be.GetMachineInfo();

    const bool actMem = (ui.activeWidget == WidgetId::Memory);
    const bool actPages = (ui.activeWidget == WidgetId::Pages);

    const uint8_t memAttr = actMem ? kWSel : kWNorm;
    const uint8_t betaAttr = (machine.trdosPresent || beta.present) ? kWOther : kWOtheroff;

    // watches panel (y1..13, Passive)
    s.Tprint(34, 0, "watches", kWTitle);
    const struct { const char* name; uint16_t addr; } kFixedWatches[10] = {
        {"PC", r.pc}, {"SP", r.sp}, {"BC", r.bc}, {"DE", r.de}, {"HL", r.hl},
        {"IX", r.ix}, {"IY", r.iy}, {"BC'", r.bc2}, {"DE'", r.de2}, {"HL'", r.hl2}
    };
    for (int i = 0; i < 10; ++i) {
        WatchRow(s, be, kFixedWatches[i].name, kFixedWatches[i].addr, 1 + i, kWOther, false, false, "");
    }
    for (int i = 0; i < 3; ++i) {
        const uint16_t addr = ui.userWatches[static_cast<size_t>(i)];
        WatchRow(s, be, Hex4(addr), addr, 11 + i, kWOther, false, false, "");
    }
    s.Frame(34, 1, 37, 13, kFrameColor);

    // memory panel (y14..26, Focusable)
    s.Tprint(34, 14, "memory: " + Hex4(ui.memCurs) + " gsdma: " + Hex6(machine.gsDmaAddr) +
                     (ui.memAscii ? " [ASC]" : " [HEX]"),
             kWTitle);
    for (int row = 0; row < 12; ++row) {
        DumpRow(s, be, static_cast<uint16_t>(ui.memTop + row * 8), 15 + row, memAttr,
                ui.memCurs, actMem, ui.isEditing, ui.memAscii, ui.editBuf);
    }
    s.Frame(34, 15, 37, 12, kFrameColor);

    // ports panel (y1..4, Passive)
    s.Tprint(72, 0, "ports", kWTitle);
    Right(s, 1, "FE:" + Hex2(ports.fe), kWOther, false);
    Right(s, 2, "7FFD:" + Hex2(ports.p7ffd), ports.lock48 ? kW48k : kWOther, false);
    Right(s, 3, "cmos:" + Hex2(ports.cmosAddr), kWOther, false);
    Right(s, 4, "EFF7:" + Hex2(ports.eff7), kWOther, false);
    s.Frame(72, 1, 7, 4, kFrameColor);

    // beta128 panel (y6..10, Passive)
    s.Tprint(72, 5, "beta128", kWTitle);
    Right(s, 6, "CD:" + Hex4((beta.cmd << 8) | beta.data), betaAttr, false);
    Right(s, 7, "STAT:" + Hex2(beta.statusRead), betaAttr, false);
    Right(s, 8, "SECT:" + Hex2(beta.sector), betaAttr, false);
    Right(s, 9, "T:" + Hex2(beta.track) + "/" + Hex2(beta.headTrack), betaAttr, false);
    Right(s, 10, "S:" + Hex2(beta.system) + "/" + Hex2(beta.rqs), betaAttr, false);
    s.Frame(72, 6, 7, 5, kFrameColor);

    // stack panel (y12..21, Passive)
    s.Tprint(72, 11, "stack", kWTitle);
    for (int k = 0; k < 10; ++k) {
        const int y = 12 + k;
        const int offset = (k == 0) ? -2 : (k == 1 ? 0 : (k - 1) * 2);
        std::string tag;
        if (k == 0) tag = "-2:";
        else if (k == 1) tag = "SP:";
        else if (offset == 16) tag = "10:";
        else tag = "+" + HexDigit(offset) + ":";

        const std::string valStr = Hex4(WordAt(be, static_cast<uint16_t>(r.sp + offset)));
        Right(s, y, tag + valStr, kWOther, false);
    }
    s.Frame(72, 12, 7, 10, kFrameColor);

    // pages panel (y23..26, Focusable, Strictly Read-Only)
    s.Tprint(72, 22, "pages", kWTitle);
    for (int p = 0; p < 4 && p < static_cast<int>(machine.pages.size()); ++p) {
        const bool isCurs = actPages && (ui.pagesCurs == p);
        const uint8_t rowAttr = isCurs ? kWCurs : (actPages ? kWSel : kWOtheroff);
        const uint8_t nameAttr = isCurs ? kWCurs : (actPages ? kWSel : (machine.pages[p].readOnly ? kWBankro : kWBank));
        s.Tprint(72, 23 + p, HexDigit(p) + ":", rowAttr);
        s.Tprint(74, 23 + p, machine.pages[p].name, nameAttr);
    }
    s.Frame(72, 23, 7, 4, kFrameColor);
}

void PaintClassicFrame(TextScreen& s, IDebuggerBackend& be, const UiState& ui) {
    PaintRegs(s, be, ui);
    PaintTrace(s, be, ui);
    PaintSide(s, be, ui);
    PaintBottomBar(s, be, ui);
}

}  // namespace dbg
