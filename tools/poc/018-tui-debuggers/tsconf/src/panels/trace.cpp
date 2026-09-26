// trace.cpp - disassembly list (left column, y6..26).
//
// Line format (golden): "8011 CD2080     call 8020      ↓"
//   x1:  %04X addr   x6: bytes hex, 11 wide (".." prefix when longer than
//   4 bytes, showing the LAST four)   x17: mnemonic, 15 wide   x32: branch
//   marker - "↓" on a taken branch instruction, "◄" on its target line.
#include "panels/paint.h"

#include <string>
#include <vector>

#include "model/hexfmt.h"
#include "model/model.h"

namespace dbg {

namespace {

std::string BytesField(const std::vector<uint8_t>& bytes) {
    std::string hex;
    const size_t start = bytes.size() > 4 ? bytes.size() - 4 : 0;
    if (bytes.size() > 4) hex = "..";
    for (size_t k = start; k < bytes.size(); ++k) hex += Hex2(bytes[k]);
    return StrFit(hex, 11);  // left-aligned, padded to 11 (x6..x16)
}

}  // namespace

void PaintTrace(TextScreen& s, IDebuggerBackend& be, const UiState& ui) {
    const Z80Regs r = be.GetRegs(0);
    const BranchInfo branch = be.GetBranchAtPc(0);
    const bool taken = (branch.flags & kBranchTaken) != 0;
    const bool act = (ui.activeWidget == WidgetId::Trace);
    const uint8_t atr0 = act ? kWSel : kWNorm;

    // Title at row 5 in kWTitle
    s.Tprint(1, 5, "Z80(0)", kWTitle);
    s.Tprint(9, 5, Hex4(r.lastBranch), kWTitle);
    if (act && ui.isEditing) {
        s.Tprint(17, 5, "goto:" + StrFit(ui.editBuf, 4), kWCurs);
    }

    const std::vector<DisasmLine> lines = be.Disassemble(0, ui.traceTop, 21, false);
    for (size_t i = 0; i < lines.size(); ++i) {
        const int y = 6 + static_cast<int>(i);
        const DisasmLine& d = lines[i];
        uint8_t a = (d.addr == r.pc) ? kWTracepos : atr0;
        const uint8_t bpBits = be.BpBitsAt(d.addr);
        if (bpBits != 0) {
            a = BpxLineAttr(a);
        }
        s.Tprint(1, y, Hex4(d.addr) + " " + BytesField(d.bytes) +
                           StrFit(d.mnemonic, 15), a);

        // Highlight cursor line when trace is active
        if (d.addr == ui.traceCurs && act) {
            for (int q = 17; q <= 31; ++q) {
                s.SetAttr(q, y, kWCurs);
            }
        }

        uint8_t markerCode = ' ';
        if (taken && d.addr == r.pc) {
            markerCode = 0x19;  // ↓ at the branch line
        } else if (taken && d.addr == branch.target) {
            markerCode = 0x11;  // ◄ at the target line
        }
        s.SetChar(32, y, markerCode, a);
    }
    s.Frame(1, 6, 32, 21, kFrameColor);
}

}  // namespace dbg
