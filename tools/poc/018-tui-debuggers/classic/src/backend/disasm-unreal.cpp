// disasm-unreal.cpp - see disasm-unreal.h.
#include "backend/disasm-unreal.h"

#include <cctype>
#include <cstdio>

#include "z80ex_dasm.h"

namespace dbg {
namespace {

Z80EX_BYTE MemReadByte(Z80EX_WORD addr, void* user) {
    const uint8_t* mem = static_cast<const uint8_t*>(user);
    return mem[addr];
}

bool IsUpperHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
}

unsigned HexVal(char c) {
    return (c <= '9') ? static_cast<unsigned>(c - '0')
                      : static_cast<unsigned>(c - 'A' + 10);
}

}  // namespace

std::string ToUnrealText(const char* dasm) {
    const std::string s(dasm);
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '#') {
            // substituted value: hex run, keep case
            ++i;
            const size_t start = i;
            while (i < s.size() && IsUpperHex(s[i])) ++i;
            const std::string val = s.substr(start, i - start);
            if (val.size() == 8 && val.rfind("FFFFFF", 0) == 0) {
                // negative ($ ) displacement: printf("%02X", int) rendered
                // the two's complement as 8 digits; fold back to -NN
                const unsigned byte = HexVal(val[6]) * 16u + HexVal(val[7]);
                if (byte >= 0x80u) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "%02X", 0x100u - byte);
                    if (!out.empty() && out.back() == '+') {
                        out.back() = '-';  // "(ix+-05" prints as "(ix-05"
                        out += buf;
                    } else {
                        out += '-';
                        out += buf;
                    }
                    continue;
                }
            }
            out += val;
        } else if (s[i] == '0' && i + 1 < s.size() && (s[i + 1] == 'X' || s[i + 1] == 'x')) {
            i += 2;  // literal "0xNN" (RST templates) prints as NN
        } else {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
            ++i;
        }
    }
    // mnemonic padding: pad the first token to 5 columns when an operand
    // follows; bare mnemonics stay unpadded ("ldir", "di")
    const size_t sp = out.find(' ');
    if (sp != std::string::npos && sp < 5) {
        const size_t op = out.find_first_not_of(' ', sp);
        if (op != std::string::npos) {
            out = out.substr(0, sp) + std::string(5 - sp, ' ') + out.substr(op);
        } else {
            out = out.substr(0, sp);
        }
    }
    return out;
}

UnrealInstr UnrealDisasm(const uint8_t* mem, uint16_t addr) {
    char buf[64];
    int t1 = 0, t2 = 0;
    const int len = z80ex_dasm(buf, static_cast<int>(sizeof buf), 0, &t1, &t2,
                               MemReadByte, addr, const_cast<uint8_t*>(mem));
    UnrealInstr ins;
    ins.len = (len > 0) ? len : 1;
    ins.text = ToUnrealText(buf);
    if (ins.text == "nop*") ins.text = "nop";  // undocumented prefixes
    return ins;
}

uint16_t UnrealPrevInstruction(const uint8_t* mem, uint16_t addr) {
    const uint16_t start = (addr > 16) ? static_cast<uint16_t>(addr - 16) : 0;
    uint16_t best = start;
    uint16_t pc = start;
    while (pc < addr) {
        best = pc;
        const UnrealInstr ins = UnrealDisasm(mem, pc);
        pc = static_cast<uint16_t>(pc + ins.len);
    }
    return best;
}

BranchInfo UnrealBranchInfo(const uint8_t* mem, uint16_t pc, const Z80Regs& regs) {
    const UnrealInstr ins = UnrealDisasm(mem, pc);
    BranchInfo bi;
    bi.nextPc = static_cast<uint16_t>(pc + ins.len);

    const std::string t = ins.text;
    const size_t sp0 = t.find(' ');
    const std::string mnem = (sp0 == std::string::npos) ? t : t.substr(0, sp0);
    std::string operand = (sp0 == std::string::npos) ? "" : t.substr(sp0 + 1);
    // strip the condition prefix: "jp nz,0123" -> "0123"
    auto conditionOf = [&](const std::string& cc) -> int {
        if (cc == "nz") return (regs.f & 0x40) == 0;
        if (cc == "z") return (regs.f & 0x40) != 0;
        if (cc == "nc") return (regs.f & 0x01) == 0;
        if (cc == "c") return (regs.f & 0x01) != 0;
        if (cc == "po") return (regs.f & 0x04) == 0;
        if (cc == "pe") return (regs.f & 0x04) != 0;
        if (cc == "p") return (regs.f & 0x80) == 0;
        if (cc == "m") return (regs.f & 0x80) != 0;
        return -1;  // not a condition
    };
    int taken = -1;  // -1 unconditional, 0/1 condition result
    if (operand.size() >= 3 && operand[2] == ',') {
        taken = conditionOf(operand.substr(0, 2));
        if (taken >= 0) operand = operand.substr(3);
    }
    auto hexOf = [&](const std::string& s) -> int {
        int v = 0;
        for (char c : s) {
            if (c >= '0' && c <= '9') v = v * 16 + (c - '0');
            else if (c >= 'A' && c <= 'F') v = v * 16 + (c - 'A' + 10);
            else return -1;
        }
        return v;
    };

    if (mnem == "halt") {
        bi.flags = kBranchHalt;
        bi.target = (regs.im == 2)
                        ? static_cast<uint16_t>((regs.i << 8) | (regs.intVector & 0xFF))
                        : regs.intVector;
        bi.nextPc = bi.target;
        return bi;
    }
    const bool isBlock = mnem.size() == 4 && mnem[3] == 'r' &&
                         (mnem.compare(0, 3, "ldi") == 0 || mnem.compare(0, 3, "ldd") == 0 ||
                          mnem.compare(0, 3, "cpi") == 0 || mnem.compare(0, 3, "cpd") == 0 ||
                          mnem.compare(0, 3, "ini") == 0 || mnem.compare(0, 3, "ind") == 0 ||
                          mnem == "otir" || mnem == "otdr");
    const bool isBlockOne = mnem == "ldi" || mnem == "ldd" || mnem == "cpi" || mnem == "cpd" ||
                            mnem == "ini" || mnem == "ind" || mnem == "outi" || mnem == "outd";
    if (isBlock || isBlockOne) {
        bi.flags = kBranchBlk | (isBlock ? kBranchLoop : 0);
        if (isBlock) {
            bi.flags |= kBranchTaken;
            bi.target = pc;  // repeats itself until BC == 0
        }
        return bi;
    }
    if (mnem == "djnz") {
        const int target = hexOf(operand);
        bi.flags = kBranchLoop;
        if (regs.bc >> 8) {
            bi.flags |= kBranchTaken;
            if (target >= 0) bi.target = static_cast<uint16_t>(target);
        }
        return bi;
    }
    if (mnem == "jp" || mnem == "jr" || mnem == "call") {
        if (operand == "(hl)" || operand == "(ix)" || operand == "(iy)") {
            bi.flags = kBranchTaken | kBranchAddr;
            bi.target = (operand == "(hl)") ? regs.hl : (operand == "(ix)") ? regs.ix : regs.iy;
            return bi;
        }
        const int target = hexOf(operand);
        const bool condOk = (taken < 0) || taken != 0;
        if (condOk) {
            bi.flags |= kBranchTaken;
            if (target >= 0) bi.target = static_cast<uint16_t>(target);
        }
        if (mnem == "call") bi.flags |= kBranchCall;
        return bi;
    }
    if (mnem == "ret" || mnem == "reti" || mnem == "retn") {
        const bool condOk = (taken < 0) || taken != 0;
        if (condOk) {
            bi.flags = kBranchTaken | kBranchAddr;
            bi.target = static_cast<uint16_t>(mem[regs.sp] | (mem[static_cast<uint16_t>(regs.sp + 1)] << 8));
        }
        return bi;
    }
    if (mnem == "rst") {
        const int target = hexOf(operand);
        bi.flags = kBranchTaken | kBranchCall;
        if (target >= 0) bi.target = static_cast<uint16_t>(target);
        return bi;
    }
    return bi;  // kBranchNone
}

}  // namespace dbg
