// keymap.cpp - focus traversal, arrow navigation, and in-place editing implementation.
#include "ui/keymap.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace dbg {

const RegFieldLayout kRegLayout[26] = {
    // 0: A
    {IDebuggerBackend::RegField::A, 8, 3, 0, 0, 1, 0, 2},
    // 1: F
    {IDebuggerBackend::RegField::F, 8, 5, 0, 0, 5, 1, 2},
    // 2: BC
    {IDebuggerBackend::RegField::Bc, 16, 3, 1, 2, 6, 0, 3},
    // 3: DE
    {IDebuggerBackend::RegField::De, 16, 3, 2, 3, 7, 2, 4},
    // 4: HL
    {IDebuggerBackend::RegField::Hl, 16, 3, 3, 4, 8, 3, 4},
    // 5: AF'
    {IDebuggerBackend::RegField::Af2, 16, 11, 0, 1, 9, 5, 6},
    // 6: BC'
    {IDebuggerBackend::RegField::Bc2, 16, 11, 1, 2, 10, 5, 7},
    // 7: DE'
    {IDebuggerBackend::RegField::De2, 16, 11, 2, 3, 11, 6, 8},
    // 8: HL'
    {IDebuggerBackend::RegField::Hl2, 16, 11, 3, 4, 12, 7, 8},
    // 9: SP
    {IDebuggerBackend::RegField::Sp, 16, 19, 0, 5, 13, 9, 10},
    // 10: PC
    {IDebuggerBackend::RegField::Pc, 16, 19, 1, 6, 10, 9, 11},
    // 11: IX
    {IDebuggerBackend::RegField::Ix, 16, 19, 2, 7, 15, 10, 12},
    // 12: IY
    {IDebuggerBackend::RegField::Iy, 16, 19, 3, 8, 18, 11, 12},
    // 13: I
    {IDebuggerBackend::RegField::I, 8, 28, 0, 9, 14, 13, 16},
    // 14: R
    {IDebuggerBackend::RegField::R, 8, 30, 0, 13, 14, 14, 17},
    // 15: IM
    {IDebuggerBackend::RegField::Im, 2, 26, 2, 11, 16, 13, 20},
    // 16: IFF1
    {IDebuggerBackend::RegField::Iff1, 1, 30, 2, 15, 17, 13, 24},
    // 17: IFF2
    {IDebuggerBackend::RegField::Iff2, 1, 31, 2, 16, 17, 14, 25},
    // Flags SF..CF: width = 30..37 (bit = width - 30)
    // 18: SF (bit 7)
    {IDebuggerBackend::RegField::F, 37, 24, 3, 12, 19, 15, 18},
    // 19: ZF (bit 6)
    {IDebuggerBackend::RegField::F, 36, 25, 3, 18, 20, 15, 19},
    // 20: YF (bit 5)
    {IDebuggerBackend::RegField::F, 35, 26, 3, 19, 21, 15, 20},
    // 21: HF (bit 4)
    {IDebuggerBackend::RegField::F, 34, 27, 3, 20, 22, 15, 21},
    // 22: XF (bit 3)
    {IDebuggerBackend::RegField::F, 33, 28, 3, 21, 23, 15, 22},
    // 23: PF (bit 2)
    {IDebuggerBackend::RegField::F, 32, 29, 3, 22, 24, 16, 23},
    // 24: NF (bit 1)
    {IDebuggerBackend::RegField::F, 31, 30, 3, 23, 25, 16, 24},
    // 25: CF (bit 0)
    {IDebuggerBackend::RegField::F, 30, 31, 3, 24, 25, 17, 25}
};

const char* const kTsControlNames[16] = {
    // Column 0
    "vconfig", "tsconfig", "sysconfig", "cacheconfig", "memconfig",
    // Column 1
    "bitmap", "tiles0", "tiles1", "palsel", "misc", "fmaddr", "mempages",
    // Column 2
    "sprites", "dma", "interrupt", "intmask"
};

void FocusNextWidget(UiState& ui, bool /*isTsconf*/) {
    ui.isEditing = false;
    ui.editBuf.clear();
    constexpr int total = 4;
    const int next = (static_cast<int>(ui.activeWidget) + 1) % total;
    ui.activeWidget = static_cast<WidgetId>(next);
    ui.activeWindow = next;
    ui.showBank = (ui.activeWidget == WidgetId::Pages);
    ui.selBank = ui.pagesCurs;
}

void FocusPrevWidget(UiState& ui, bool /*isTsconf*/) {
    ui.isEditing = false;
    ui.editBuf.clear();
    constexpr int total = 4;
    const int prev = (static_cast<int>(ui.activeWidget) + total - 1) % total;
    ui.activeWidget = static_cast<WidgetId>(prev);
    ui.activeWindow = prev;
    ui.showBank = (ui.activeWidget == WidgetId::Pages);
    ui.selBank = ui.pagesCurs;
}

void HandleArrowKey(IDebuggerBackend& be, UiState& ui, NavKey key, bool /*isTsconf*/) {
    if (ui.isEditing) {
        HandleEnterKey(be, ui);
    }

    switch (ui.activeWidget) {
        case WidgetId::Regs: {
            if (ui.regsCurs < 0 || ui.regsCurs >= 26) ui.regsCurs = 0;
            switch (key) {
                case NavKey::Left: ui.regsCurs = kRegLayout[ui.regsCurs].lf; break;
                case NavKey::Right: ui.regsCurs = kRegLayout[ui.regsCurs].rt; break;
                case NavKey::Up: ui.regsCurs = kRegLayout[ui.regsCurs].up; break;
                case NavKey::Down: ui.regsCurs = kRegLayout[ui.regsCurs].dn; break;
                default: break;
            }
            break;
        }
        case WidgetId::Trace: {
            switch (key) {
                case NavKey::Up:
                    ui.traceCurs = be.PrevInstruction(0, ui.traceCurs);
                    break;
                case NavKey::Down: {
                    const auto lines = be.Disassemble(0, ui.traceCurs, 1, false);
                    if (!lines.empty()) {
                        ui.traceCurs = static_cast<uint16_t>(ui.traceCurs + lines[0].len);
                    }
                    break;
                }
                case NavKey::PgUp: {
                    for (int i = 0; i < 21; ++i) {
                        ui.traceCurs = be.PrevInstruction(0, ui.traceCurs);
                    }
                    break;
                }
                case NavKey::PgDn: {
                    const auto lines = be.Disassemble(0, ui.traceCurs, 21, false);
                    if (!lines.empty()) {
                        ui.traceCurs = lines.back().addr;
                    }
                    break;
                }
                default: break;
            }
            break;
        }
        case WidgetId::Memory: {
            switch (key) {
                case NavKey::Left: ui.memCurs--; break;
                case NavKey::Right: ui.memCurs++; break;
                case NavKey::Up: ui.memCurs -= 8; break;
                case NavKey::Down: ui.memCurs += 8; break;
                case NavKey::PgUp: ui.memCurs -= 96; break;
                case NavKey::PgDn: ui.memCurs += 96; break;
            }
            if (ui.memCurs < ui.memTop) {
                ui.memTop = static_cast<uint16_t>(ui.memCurs & ~7);
            } else if (ui.memCurs >= ui.memTop + 96) {
                ui.memTop = static_cast<uint16_t>((ui.memCurs - 88) & ~7);
            }
            break;
        }
        case WidgetId::Pages: {
            // Strictly Read-Only: cursor navigation only
            switch (key) {
                case NavKey::Up:
                    ui.pagesCurs = (ui.pagesCurs > 0) ? ui.pagesCurs - 1 : 3;
                    ui.selBank = ui.pagesCurs;
                    break;
                case NavKey::Down:
                    ui.pagesCurs = (ui.pagesCurs < 3) ? ui.pagesCurs + 1 : 0;
                    ui.selBank = ui.pagesCurs;
                    break;
                default: break;
            }
            break;
        }
    }
}

bool HandleCharInput(IDebuggerBackend& be, UiState& ui, char ch) {
    switch (ui.activeWidget) {
        case WidgetId::Regs: {
            if (ui.regsCurs >= 18 && ui.regsCurs <= 25) {
                // SF..CF flag bits
                if (ch == '0' || ch == '1') {
                    const int bit = 7 - (ui.regsCurs - 18);
                    uint8_t f = be.GetRegs(0).f;
                    if (ch == '1') f |= static_cast<uint8_t>(1 << bit);
                    else f &= static_cast<uint8_t>(~(1 << bit));
                    be.WriteReg(0, IDebuggerBackend::RegField::F, f);
                    return true;
                }
                return false;
            }
            if (ui.regsCurs == 16 || ui.regsCurs == 17) {
                // IFF1 / IFF2
                if (ch == '0' || ch == '1') {
                    be.WriteReg(0, kRegLayout[ui.regsCurs].regField, static_cast<unsigned>(ch - '0'));
                    return true;
                }
                return false;
            }
            if (ui.regsCurs == 15) {
                // IM mode: 0, 1, 2
                if (ch >= '0' && ch <= '2') {
                    be.WriteReg(0, IDebuggerBackend::RegField::Im, static_cast<unsigned>(ch - '0'));
                    return true;
                }
                return false;
            }
            // Hex registers
            if (std::isxdigit(static_cast<unsigned char>(ch)) != 0) {
                const int maxLen = (kRegLayout[ui.regsCurs].width == 16) ? 4 : 2;
                if (!ui.isEditing) {
                    ui.isEditing = true;
                    ui.editBuf.clear();
                }
                if (static_cast<int>(ui.editBuf.size()) < maxLen) {
                    ui.editBuf += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                }
                return true;
            }
            return false;
        }
        case WidgetId::Trace: {
            if (std::isxdigit(static_cast<unsigned char>(ch)) != 0) {
                if (!ui.isEditing) {
                    ui.isEditing = true;
                    ui.editBuf.clear();
                }
                if (ui.editBuf.size() < 4) {
                    ui.editBuf += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                }
                return true;
            }
            if (ch == 'g' || ch == 'G') {
                ui.isEditing = true;
                ui.editBuf.clear();
                return true;
            }
            return false;
        }

        case WidgetId::Memory: {
            if (ui.memAscii) {
                const unsigned char uch = static_cast<unsigned char>(ch);
                if (uch >= 0x20 && uch <= 0x7E) {
                    be.WriteMemory(0, ui.memCurs, {static_cast<uint8_t>(uch)});
                    ui.memCurs++;
                    if (ui.memCurs >= ui.memTop + 96) {
                        ui.memTop = static_cast<uint16_t>((ui.memCurs - 88) & ~7);
                    }
                    return true;
                }
                return false;
            }
            // Hex mode
            if (std::isxdigit(static_cast<unsigned char>(ch)) != 0) {
                if (!ui.isEditing) {
                    ui.isEditing = true;
                    ui.editBuf.clear();
                }
                ui.editBuf += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (ui.editBuf.size() >= 2) {
                    unsigned long val = 0;
                    try {
                        val = std::stoul(ui.editBuf, nullptr, 16);
                    } catch (...) {
                        ui.isEditing = false;
                        ui.editBuf.clear();
                        return false;
                    }
                    be.WriteMemory(0, ui.memCurs, {static_cast<uint8_t>(val & 0xFF)});
                    ui.memCurs++;
                    if (ui.memCurs >= ui.memTop + 96) {
                        ui.memTop = static_cast<uint16_t>((ui.memCurs - 88) & ~7);
                    }
                    ui.isEditing = false;
                    ui.editBuf.clear();
                }
                return true;
            }
            return false;
        }
        case WidgetId::Pages:
            // Strictly Read-Only: typing is rejected!
            return false;
    }
    return false;
}

bool HandleEnterKey(IDebuggerBackend& be, UiState& ui) {
    switch (ui.activeWidget) {
        case WidgetId::Regs: {
            if (ui.isEditing && !ui.editBuf.empty()) {
                unsigned long val = 0;
                try {
                    val = std::stoul(ui.editBuf, nullptr, 16);
                } catch (...) {
                    ui.isEditing = false;
                    ui.editBuf.clear();
                    return false;
                }
                const int width = kRegLayout[ui.regsCurs].width;
                if (width == 8 && val <= 0xFF) {
                    be.WriteReg(0, kRegLayout[ui.regsCurs].regField, static_cast<unsigned>(val));
                } else if (width == 16 && val <= 0xFFFF) {
                    be.WriteReg(0, kRegLayout[ui.regsCurs].regField, static_cast<unsigned>(val));
                }
                ui.isEditing = false;
                ui.editBuf.clear();
                return true;
            }
            if (ui.regsCurs >= 18 && ui.regsCurs <= 25) {
                // Toggle flag bit
                const int bit = 7 - (ui.regsCurs - 18);
                const uint8_t f = static_cast<uint8_t>(be.GetRegs(0).f ^ (1 << bit));
                be.WriteReg(0, IDebuggerBackend::RegField::F, f);
                return true;
            }
            if (ui.regsCurs == 16) {
                be.WriteReg(0, IDebuggerBackend::RegField::Iff1, static_cast<unsigned>(be.GetRegs(0).iff1 ^ 1));
                return true;
            }
            if (ui.regsCurs == 17) {
                be.WriteReg(0, IDebuggerBackend::RegField::Iff2, static_cast<unsigned>(be.GetRegs(0).iff2 ^ 1));
                return true;
            }
            if (ui.regsCurs == 15) {
                be.WriteReg(0, IDebuggerBackend::RegField::Im, static_cast<unsigned>((be.GetRegs(0).im + 1) % 3));
                return true;
            }
            return false;
        }
        case WidgetId::Trace: {
            if (ui.isEditing && !ui.editBuf.empty()) {
                unsigned long val = 0;
                try {
                    val = std::stoul(ui.editBuf, nullptr, 16);
                } catch (...) {
                    ui.isEditing = false;
                    ui.editBuf.clear();
                    return false;
                }
                if (val <= 0xFFFF) {
                    ui.traceCurs = static_cast<uint16_t>(val);
                    ui.traceTop = ui.traceCurs;
                }
                ui.isEditing = false;
                ui.editBuf.clear();
                return true;
            }
            return false;
        }
        case WidgetId::Memory: {
            if (ui.isEditing && !ui.editBuf.empty()) {
                unsigned long val = 0;
                try {
                    val = std::stoul(ui.editBuf, nullptr, 16);
                } catch (...) {
                    ui.isEditing = false;
                    ui.editBuf.clear();
                    return false;
                }
                if (val <= 0xFF) {
                    be.WriteMemory(0, ui.memCurs, {static_cast<uint8_t>(val)});
                    ui.memCurs++;
                    if (ui.memCurs >= ui.memTop + 96) {
                        ui.memTop = static_cast<uint16_t>((ui.memCurs - 88) & ~7);
                    }
                }
                ui.isEditing = false;
                ui.editBuf.clear();
                return true;
            }
            return false;
        }
        case WidgetId::Pages:
            return false;
    }
    return false;
}

bool HandleBackspaceKey(UiState& ui) {
    if (ui.isEditing && !ui.editBuf.empty()) {
        ui.editBuf.pop_back();
        return true;
    }
    return false;
}

bool HandleEscapeKey(UiState& ui) {
    if (ui.isEditing) {
        ui.isEditing = false;
        ui.editBuf.clear();
        return true;
    }
    return false;
}

bool HandleSpaceKey(IDebuggerBackend& be, UiState& ui) {
    switch (ui.activeWidget) {
        case WidgetId::Regs: {
            if (ui.regsCurs >= 18 && ui.regsCurs <= 25) {
                const int bit = 7 - (ui.regsCurs - 18);
                const uint8_t f = static_cast<uint8_t>(be.GetRegs(0).f ^ (1 << bit));
                be.WriteReg(0, IDebuggerBackend::RegField::F, f);
                return true;
            }
            if (ui.regsCurs == 16) {
                be.WriteReg(0, IDebuggerBackend::RegField::Iff1, static_cast<unsigned>(be.GetRegs(0).iff1 ^ 1));
                return true;
            }
            if (ui.regsCurs == 17) {
                be.WriteReg(0, IDebuggerBackend::RegField::Iff2, static_cast<unsigned>(be.GetRegs(0).iff2 ^ 1));
                return true;
            }
            if (ui.regsCurs == 15) {
                be.WriteReg(0, IDebuggerBackend::RegField::Im, static_cast<unsigned>((be.GetRegs(0).im + 1) % 3));
                return true;
            }
            break;
        }
        case WidgetId::Trace: {
            const uint8_t bits = be.BpBitsAt(ui.traceCurs);
            be.SetBpBits(ui.traceCurs, static_cast<uint8_t>(bits ^ 1));
            return true;
        }
        default: break;
    }
    return false;
}

bool HandleMouseClick(IDebuggerBackend& be, UiState& ui, int cellX, int cellY, bool isTsconf) {
    if (cellX < 0 || cellY < 0) return false;
    const int maxCols = isTsconf ? 157 : 80;
    if (cellX >= maxCols || cellY >= 30) return false;

    // If currently editing, commit if valid, else cancel
    if (ui.isEditing) {
        if (!HandleEnterKey(be, ui)) {
            ui.isEditing = false;
            ui.editBuf.clear();
        }
    }

    // Only 4 focusable widgets: Registers, Disassembly (Trace), Memory, and Pages.

    // 1. Pages (y22..27, x in [71, 79])
    if (cellX >= 71 && cellX <= 79 && cellY >= 22 && cellY <= 27) {
        ui.activeWidget = WidgetId::Pages;
        ui.activeWindow = 3;
        ui.showBank = true;
        if (cellY >= 23 && cellY <= 26) {
            ui.pagesCurs = std::clamp(cellY - 23, 0, 3);
            ui.selBank = ui.pagesCurs;
        }
        return true;
    }

    // 2. Memory (y14..27, x in [34, 70])
    if (cellX >= 34 && cellX <= 70 && cellY >= 14 && cellY <= 27) {
        ui.activeWidget = WidgetId::Memory;
        ui.activeWindow = 2;
        ui.showBank = false;
        if (cellY >= 15 && cellY <= 26) {
            const int row = cellY - 15;
            const uint16_t rowBase = static_cast<uint16_t>(ui.memTop + row * 8);
            if (cellX >= 63 && cellX <= 70) {
                const int b = std::clamp(cellX - 63, 0, 7);
                ui.memCurs = static_cast<uint16_t>(rowBase + b);
                ui.memAscii = true;
            } else if (cellX >= 39 && cellX < 63) {
                const int b = std::clamp((cellX - 39) / 3, 0, 7);
                ui.memCurs = static_cast<uint16_t>(rowBase + b);
                ui.memAscii = false;
            } else {
                ui.memCurs = rowBase;
            }
        }
        return true;
    }

    // 3. Left column (x <= 33)
    if (cellX <= 33) {
        // Regs (y0..4)
        if (cellY <= 4) {
            ui.activeWidget = WidgetId::Regs;
            ui.activeWindow = 0;
            ui.showBank = false;
            if (cellY >= 1 && cellY <= 4) {
                const int targetRelY = cellY - 1;
                int bestIdx = -1;
                int bestDist = 999;
                for (int i = 0; i < 26; ++i) {
                    if (kRegLayout[i].relY == targetRelY) {
                        const int px = 1 + kRegLayout[i].relX;
                        const int len = (kRegLayout[i].width == 16) ? 4 : (kRegLayout[i].width == 8 ? 2 : 1);
                        if (cellX >= px && cellX < px + len) {
                            bestIdx = i;
                            break;
                        }
                        const int dist = std::min(std::abs(cellX - px), std::abs(cellX - (px + len - 1)));
                        if (dist < bestDist) {
                            bestDist = dist;
                            bestIdx = i;
                        }
                    }
                }
                if (bestIdx >= 0) {
                    ui.regsCurs = bestIdx;
                }
            }
            return true;
        }

        // Trace (y5..27)
        if (cellY >= 5 && cellY <= 27) {
            ui.activeWidget = WidgetId::Trace;
            ui.activeWindow = 1;
            ui.showBank = false;
            if (cellY >= 6 && cellY <= 26) {
                const int row = cellY - 6;
                const auto disasm = be.Disassemble(0, ui.traceTop, 21, true);
                if (static_cast<size_t>(row) < disasm.size()) {
                    ui.traceCurs = disasm[static_cast<size_t>(row)].addr;
                }
            }
            return true;
        }
    }

    // Clicks on non-focusable widgets (Watches, Ports, Beta128, Stack, Bottom, TSBoard) return false
    return false;
}

}  // namespace dbg
