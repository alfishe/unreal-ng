// asm-subset.cpp - see asm-subset.h.
//
// Pipeline: split the mnemonic off the raw line, normalise the operand text
// (lower-case outside 'c' quotes, drop spaces, cut ";" comments), split on
// top-level commas, classify each operand, then emit bytes per mnemonic.
// Anything unrecognised fails with a non-empty error so the assemble dialog
// re-opens (§4.2). Relative jr/djnz targets are computed against "addr".
#include "backend/asm-subset.h"

#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace dbg {
namespace {

// Register codes as used by the opcode tables: b=0 c=1 d=2 e=3 h=4 l=5
// (hl)=6 a=7; pairs bc=0 de=1 hl=2 sp=3; conditions nz=0 z=1 nc=2 c=3
// po=4 pe=5 p=6 m=7. "(hl)" is a memory operand, never a plain Reg8.
int ParseReg8(const std::string& s) {
    if (s == "b") return 0;
    if (s == "c") return 1;
    if (s == "d") return 2;
    if (s == "e") return 3;
    if (s == "h") return 4;
    if (s == "l") return 5;
    if (s == "a") return 7;
    return -1;
}

int ParseReg16(const std::string& s) {
    if (s == "bc") return 0;
    if (s == "de") return 1;
    if (s == "hl") return 2;
    if (s == "sp") return 3;
    return -1;
}

int ParseIdx(const std::string& s) {  // ix=0 iy=1
    if (s == "ix") return 0;
    if (s == "iy") return 1;
    return -1;
}

int ParseCc(const std::string& s) {
    if (s == "nz") return 0;
    if (s == "z") return 1;
    if (s == "nc") return 2;
    if (s == "c") return 3;
    if (s == "po") return 4;
    if (s == "pe") return 5;
    if (s == "p") return 6;
    if (s == "m") return 7;
    return -1;
}

bool HexDigit(char c, int* v) {
    if (c >= '0' && c <= '9') { *v = c - '0'; return true; }
    if (c >= 'a' && c <= 'f') { *v = c - 'a' + 10; return true; }
    if (c >= 'A' && c <= 'F') { *v = c - 'A' + 10; return true; }
    return false;
}

// Accepts "5A" (hex, no prefix), "0x5A"/"0X5A", 'c' character constants,
// "''" (a quote character) and the signed "+3A"/"-3A" displacement forms.
bool ParseNum(const std::string& s, int* out) {
    size_t i = 0;
    int sign = 1;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        sign = (s[i] == '-') ? -1 : 1;
        ++i;
    }
    if (i >= s.size()) return false;
    if (s[i] == '\'') {  // exactly one character between the quotes
        if (s.size() - i != 3 || s[i + 2] != '\'') return false;
        *out = sign * static_cast<int>(static_cast<unsigned char>(s[i + 1]));
        return true;
    }
    if (s.compare(i, 2, "0x") == 0 || s.compare(i, 2, "0X") == 0) i += 2;
    if (i >= s.size()) return false;
    int v = 0;
    for (; i < s.size(); ++i) {
        int d = 0;
        if (!HexDigit(s[i], &d)) return false;
        v = v * 16 + d;
        if (v > 0xFFFFF) return false;  // overflow guard
    }
    *out = sign * v;
    return true;
}

// Lower-case outside quotes, drop spaces/tabs, cut at the first ";".
std::string Normalise(std::string_view in) {
    std::string out;
    bool inQuote = false;
    for (char ch : in) {
        if (ch == '\'') {
            inQuote = !inQuote;
            out += ch;
            continue;
        }
        if (inQuote) {
            out += ch;
            continue;
        }
        if (ch == ';' || ch == '\r' || ch == '\n') break;
        if (ch == ' ' || ch == '\t') continue;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

// "ld a,07" -> "ld" + "a,07" (no space: mnemonic ends at the first
// non-letter; "jp(hl)" is accepted as "jp" + "(hl)").
void SplitMnemonic(const std::string& text, std::string* mnem, std::string* rest) {
    size_t i = 0;
    while (i < text.size() && std::isalpha(static_cast<unsigned char>(text[i])) != 0) ++i;
    mnem->assign(text, 0, i);
    for (char& c : *mnem) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    size_t j = i;
    while (j < text.size() && (text[j] == ' ' || text[j] == '\t')) ++j;
    rest->assign(text, j, std::string::npos);
}

std::vector<std::string> SplitOps(const std::string& s) {
    std::vector<std::string> out;
    if (s.empty()) return out;  // no operands at all ("nop", "ldir", ...)
    std::string cur;
    bool inQuote = false;
    for (char ch : s) {
        if (ch == '\'') inQuote = !inQuote;
        if (ch == ',' && !inQuote) {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur += ch;
    }
    out.push_back(cur);
    return out;
}

struct Operand {
    enum Kind {
        Bad, Reg8, Reg16, Idx, IReg, AfReg,                        // plain
        MemHl, MemBcDe, MemSp, MemC, MemIdx, MemNn,                // (...) forms
        Imm, Cc,
    };
    Kind kind = Bad;
    int reg = 0;   // Reg8/Reg16/Idx code, MemBcDe 0=bc 1=de, MemIdx 0=ix 1=iy, Cc code
    int num = 0;   // Imm value, MemNn address, MemIdx displacement
};

// "ccFirst" resolves the one ambiguous token "c": as a condition in
// jp/jr/call/ret operand position, as the register everywhere else.
Operand ParseOperand(const std::string& s, bool ccFirst) {
    Operand o;
    if (s.empty()) return o;
    if (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
        const std::string inner = s.substr(1, s.size() - 2);
        if (inner == "hl") { o.kind = Operand::MemHl; return o; }
        if (inner == "bc") { o.kind = Operand::MemBcDe; o.reg = 0; return o; }
        if (inner == "de") { o.kind = Operand::MemBcDe; o.reg = 1; return o; }
        if (inner == "sp") { o.kind = Operand::MemSp; return o; }
        if (inner == "c") { o.kind = Operand::MemC; return o; }
        for (int k = 0; k < 2; ++k) {  // (ix) (iy) (ix+d) (ix-d)
            const char* name = (k == 0) ? "ix" : "iy";
            if (inner.compare(0, 2, name) != 0) continue;
            int disp = 0;
            if (inner.size() > 2 && !ParseNum(inner.substr(2), &disp)) return o;
            if (disp < -128 || disp > 127) return o;
            o.kind = Operand::MemIdx;
            o.reg = k;
            o.num = disp;
            return o;
        }
        int v = 0;
        if (ParseNum(inner, &v) && v >= 0 && v <= 0xFFFF) {
            o.kind = Operand::MemNn;
            o.num = v;
            return o;
        }
        return o;
    }
    if (ccFirst) {
        const int cc = ParseCc(s);
        if (cc >= 0) { o.kind = Operand::Cc; o.reg = cc; return o; }
    }
    const int r8 = ParseReg8(s);
    if (r8 >= 0) { o.kind = Operand::Reg8; o.reg = r8; return o; }
    const int r16 = ParseReg16(s);
    if (r16 >= 0) { o.kind = Operand::Reg16; o.reg = r16; return o; }
    const int ix = ParseIdx(s);
    if (ix >= 0) { o.kind = Operand::Idx; o.reg = ix; return o; }
    if (s == "i") { o.kind = Operand::IReg; o.reg = 0; return o; }
    if (s == "r") { o.kind = Operand::IReg; o.reg = 1; return o; }
    if (s == "af" || s == "af'") { o.kind = Operand::AfReg; return o; }
    if (!ccFirst) {
        const int cc = ParseCc(s);
        if (cc >= 0) { o.kind = Operand::Cc; o.reg = cc; return o; }
    }
    int v = 0;
    if (ParseNum(s, &v)) { o.kind = Operand::Imm; o.num = v; return o; }
    return o;
}

void Emit(std::vector<uint8_t>* code, std::initializer_list<uint8_t> bytes) {
    code->insert(code->end(), bytes);
}

bool EmitByte(std::vector<uint8_t>* code, int v, std::string* error) {
    if (v < -128 || v > 255) { *error = "operand out of range"; return false; }
    code->push_back(static_cast<uint8_t>(v & 0xFF));
    return true;
}

bool EmitWord(std::vector<uint8_t>* code, int v, std::string* error) {
    if (v < -32768 || v > 65535) { *error = "operand out of range"; return false; }
    code->push_back(static_cast<uint8_t>(v & 0xFF));
    code->push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    return true;
}

bool EmitRelative(std::vector<uint8_t>* code, uint16_t at, int target, std::string* error) {
    const int rel = target - static_cast<int>(at) - 2;
    if (rel < -128 || rel > 127) { *error = "relative jump out of range"; return false; }
    code->push_back(static_cast<uint8_t>(rel & 0xFF));
    return true;
}

}  // namespace

namespace {

struct FixedOp {
    const char* name;
    uint8_t b0;
    uint8_t b1;
    bool two;
};

const FixedOp kFixedOps[] = {
    {"nop", 0x00, 0x00, false},   {"halt", 0x76, 0x00, false},
    {"di", 0xF3, 0x00, false},    {"ei", 0xFB, 0x00, false},
    {"exx", 0xD9, 0x00, false},   {"daa", 0x27, 0x00, false},
    {"cpl", 0x2F, 0x00, false},   {"ccf", 0x3F, 0x00, false},
    {"scf", 0x37, 0x00, false},   {"neg", 0xED, 0x44, true},
    {"rld", 0xED, 0x6F, true},    {"rrd", 0xED, 0x67, true},
    {"reti", 0xED, 0x4D, true},   {"retn", 0xED, 0x45, true},
    {"ldi", 0xED, 0xA0, true},    {"ldd", 0xED, 0xA8, true},
    {"cpi", 0xED, 0xA1, true},    {"cpd", 0xED, 0xA9, true},
    {"ini", 0xED, 0xA2, true},    {"ind", 0xED, 0xAA, true},
    {"outi", 0xED, 0xA3, true},   {"outd", 0xED, 0xAB, true},
    {"ldir", 0xED, 0xB0, true},   {"lddr", 0xED, 0xB8, true},
    {"cpir", 0xED, 0xB1, true},   {"cpdr", 0xED, 0xB9, true},
    {"inir", 0xED, 0xB2, true},   {"indr", 0xED, 0xBA, true},
    {"otir", 0xED, 0xB3, true},   {"otdr", 0xED, 0xBB, true},
};

bool AssembleOne(uint16_t addr, const std::string& text, std::vector<uint8_t>* code,
                 std::string* error) {
    std::string mnem;
    std::string rest;
    SplitMnemonic(text, &mnem, &rest);
    if (mnem.empty()) {
        *error = "syntax error";
        return false;
    }
    const std::vector<std::string> opTexts = SplitOps(Normalise(rest));
    // operand position 0 of these mnemonics is a condition code, so "c" wins
    // over the register there ("jp c,8020" vs "ld c,07")
    const bool ccFirst0 = (mnem == "jp" || mnem == "jr" || mnem == "call" || mnem == "ret");
    std::vector<Operand> ops;
    ops.reserve(opTexts.size());
    for (size_t k = 0; k < opTexts.size(); ++k) {
        Operand o = ParseOperand(opTexts[k], ccFirst0 && k == 0);
        if (o.kind == Operand::Bad) {
            *error = opTexts[k].empty() ? "missing operand" : "unknown operand: " + opTexts[k];
            return false;
        }
        ops.push_back(o);
    }
    auto need = [&ops, error](size_t n) -> bool {
        if (ops.size() != n) {
            *error = "wrong operand count";
            return false;
        }
        return true;
    };

    for (const FixedOp& f : kFixedOps) {
        if (mnem != f.name) continue;
        if (!ops.empty()) {
            *error = "unexpected operand";
            return false;
        }
        if (f.two) Emit(code, {f.b0, f.b1});
        else code->push_back(f.b0);
        return true;
    }

    if (mnem == "ld") {
        if (!need(2)) return false;
        const Operand& d = ops[0];
        const Operand& s = ops[1];
        if (d.kind == Operand::Reg16 && d.reg == 3) {  // ld sp,hl / ld sp,ix / ld sp,iy
            if (s.kind == Operand::Reg16 && s.reg == 2) { code->push_back(0xF9); return true; }
            if (s.kind == Operand::Idx) {
                Emit(code, {static_cast<uint8_t>(s.reg ? 0xFD : 0xDD), 0xF9});
                return true;
            }
        }
        if (d.kind == Operand::IReg && s.kind == Operand::Reg8 && s.reg == 7) {
            Emit(code, {0xED, static_cast<uint8_t>(d.reg ? 0x4F : 0x47)});  // ld i,a / ld r,a
            return true;
        }
        if (d.kind == Operand::Reg8 && d.reg == 7 && s.kind == Operand::IReg) {
            Emit(code, {0xED, static_cast<uint8_t>(s.reg ? 0x5F : 0x57)});  // ld a,i / ld a,r
            return true;
        }
        if (d.kind == Operand::Reg8 && d.reg == 7 && s.kind == Operand::MemBcDe) {
            code->push_back(static_cast<uint8_t>(s.reg ? 0x1A : 0x0A));
            return true;
        }
        if (d.kind == Operand::MemBcDe && s.kind == Operand::Reg8 && s.reg == 7) {
            code->push_back(static_cast<uint8_t>(s.reg ? 0x12 : 0x02));
            return true;
        }
        if (d.kind == Operand::Reg8) {
            if (s.kind == Operand::Reg8) {
                code->push_back(static_cast<uint8_t>(0x40 | (d.reg << 3) | s.reg));
                return true;
            }
            if (s.kind == Operand::MemHl) {
                code->push_back(static_cast<uint8_t>(0x40 | (d.reg << 3) | 0x06));
                return true;
            }
            if (s.kind == Operand::MemIdx) {
                Emit(code, {static_cast<uint8_t>(s.reg ? 0xFD : 0xDD),
                            static_cast<uint8_t>(0x40 | (d.reg << 3) | 0x06),
                            static_cast<uint8_t>(s.num & 0xFF)});
                return true;
            }
            if (s.kind == Operand::Imm) {
                code->push_back(static_cast<uint8_t>(0x06 | (d.reg << 3)));
                return EmitByte(code, s.num, error);
            }
            if (s.kind == Operand::MemNn && d.reg == 7) {  // ld a,(nn)
                code->push_back(0x3A);
                return EmitWord(code, s.num, error);
            }
        }
        if (d.kind == Operand::MemHl) {
            if (s.kind == Operand::Reg8) { code->push_back(static_cast<uint8_t>(0x70 | s.reg)); return true; }
            if (s.kind == Operand::Imm) { code->push_back(0x36); return EmitByte(code, s.num, error); }
        }
        if (d.kind == Operand::MemIdx) {
            if (s.kind == Operand::Reg8) {
                Emit(code, {static_cast<uint8_t>(d.reg ? 0xFD : 0xDD),
                            static_cast<uint8_t>(0x70 | s.reg),
                            static_cast<uint8_t>(d.num & 0xFF)});
                return true;
            }
            if (s.kind == Operand::Imm) {
                Emit(code, {static_cast<uint8_t>(d.reg ? 0xFD : 0xDD), 0x36,
                            static_cast<uint8_t>(d.num & 0xFF)});
                return EmitByte(code, s.num, error);
            }
        }
        if (d.kind == Operand::Reg16 && s.kind == Operand::Imm) {
            code->push_back(static_cast<uint8_t>(0x01 | (d.reg << 4)));
            return EmitWord(code, s.num, error);
        }
        if (d.kind == Operand::Idx && s.kind == Operand::Imm) {
            Emit(code, {static_cast<uint8_t>(d.reg ? 0xFD : 0xDD), 0x21});
            return EmitWord(code, s.num, error);
        }
        if (d.kind == Operand::MemNn && s.kind == Operand::Reg8 && s.reg == 7) {  // ld (nn),a
            code->push_back(0x32);
            return EmitWord(code, d.num, error);
        }
        if (d.kind == Operand::MemNn && s.kind == Operand::Reg16 && s.reg == 2) {  // ld (nn),hl
            code->push_back(0x22);
            return EmitWord(code, d.num, error);
        }
        if (d.kind == Operand::Reg16 && d.reg == 2 && s.kind == Operand::MemNn) {  // ld hl,(nn)
            code->push_back(0x2A);
            return EmitWord(code, s.num, error);
        }
        *error = "invalid ld operands";
        return false;
    }

    const struct AluOp { const char* name; int code; } kAlu[] = {
        {"add", 0}, {"adc", 1}, {"sub", 2}, {"sbc", 3},
        {"and", 4}, {"xor", 5}, {"or", 6}, {"cp", 7},
    };
    for (const AluOp& alu : kAlu) {
        if (mnem != alu.name) continue;
        if (ops.empty()) { *error = "missing operand"; return false; }
        if (ops.size() > 2) { *error = "wrong operand count"; return false; }
        // 16-bit accumulators: add/adc/sbc hl,rr and add ix,rr / add iy,rr
        if (mnem != "sub" && mnem != "and" && mnem != "xor" && mnem != "or" && mnem != "cp" &&
            ops.size() == 2 && ops[0].kind == Operand::Reg16 && ops[0].reg == 2) {
            const Operand& s = ops[1];
            if (s.kind != Operand::Reg16) { *error = "invalid operands"; return false; }
            if (mnem == "add") { code->push_back(static_cast<uint8_t>(0x09 | (s.reg << 4))); return true; }
            Emit(code, {0xED, static_cast<uint8_t>((mnem == "adc" ? 0x4A : 0x42) | (s.reg << 3))});
            return true;
        }
        if (mnem == "add" && ops.size() == 2 && ops[0].kind == Operand::Idx) {
            const Operand& s = ops[1];
            int pp = -1;
            if (s.kind == Operand::Reg16) pp = s.reg;
            else if (s.kind == Operand::Idx && s.reg == ops[0].reg) pp = 2;  // hl slot
            if (pp < 0) { *error = "invalid operands"; return false; }
            Emit(code, {static_cast<uint8_t>(ops[0].reg ? 0xFD : 0xDD),
                        static_cast<uint8_t>(0x09 | (pp << 4))});
            return true;
        }
        const Operand* s = &ops[0];
        if (ops.size() == 2) {  // optional leading "a,"
            if (ops[0].kind != Operand::Reg8 || ops[0].reg != 7) {
                *error = "invalid operands";
                return false;
            }
            s = &ops[1];
        }
        if (s->kind == Operand::Reg8) {
            code->push_back(static_cast<uint8_t>(0x80 | (alu.code << 3) | s->reg));
            return true;
        }
        if (s->kind == Operand::MemHl) {
            code->push_back(static_cast<uint8_t>(0x80 | (alu.code << 3) | 0x06));
            return true;
        }
        if (s->kind == Operand::MemIdx) {
            Emit(code, {static_cast<uint8_t>(s->reg ? 0xFD : 0xDD),
                        static_cast<uint8_t>(0x80 | (alu.code << 3) | 0x06),
                        static_cast<uint8_t>(s->num & 0xFF)});
            return true;
        }
        if (s->kind == Operand::Imm) {
            code->push_back(static_cast<uint8_t>(0xC6 | (alu.code << 3)));
            return EmitByte(code, s->num, error);
        }
        *error = "invalid operands";
        return false;
    }

    if (mnem == "inc" || mnem == "dec") {
        if (!need(1)) return false;
        const Operand& o = ops[0];
        const bool isDec = (mnem == "dec");
        switch (o.kind) {
        case Operand::Reg8:
            code->push_back(static_cast<uint8_t>((isDec ? 0x05 : 0x04) | (o.reg << 3)));
            return true;
        case Operand::MemHl:
            code->push_back(static_cast<uint8_t>(isDec ? 0x35 : 0x34));
            return true;
        case Operand::MemIdx:
            Emit(code, {static_cast<uint8_t>(o.reg ? 0xFD : 0xDD),
                        static_cast<uint8_t>(isDec ? 0x35 : 0x34),
                        static_cast<uint8_t>(o.num & 0xFF)});
            return true;
        case Operand::Reg16:
            code->push_back(static_cast<uint8_t>(0x03 | (o.reg << 3) | (isDec ? 0x08 : 0)));
            return true;
        case Operand::Idx:
            Emit(code, {static_cast<uint8_t>(o.reg ? 0xFD : 0xDD),
                        static_cast<uint8_t>(0x23 | (isDec ? 0x08 : 0))});
            return true;
        default:
            *error = "invalid operand";
            return false;
        }
    }

    const struct RotOp { const char* name; int code; } kRot[] = {
        {"rlc", 0}, {"rrc", 1}, {"rl", 2}, {"rr", 3},
        {"sla", 4}, {"sra", 5}, {"sll", 6}, {"srl", 7},
    };
    for (const RotOp& rot : kRot) {
        if (mnem != rot.name) continue;
        if (!need(1)) return false;
        const Operand& o = ops[0];
        if (o.kind == Operand::Reg8 || o.kind == Operand::MemHl) {
            const int t = (o.kind == Operand::MemHl) ? 6 : o.reg;
            Emit(code, {0xCB, static_cast<uint8_t>((rot.code << 3) | t)});
            return true;
        }
        if (o.kind == Operand::MemIdx) {
            Emit(code, {static_cast<uint8_t>(o.reg ? 0xFD : 0xDD), 0xCB,
                        static_cast<uint8_t>(o.num & 0xFF),
                        static_cast<uint8_t>((rot.code << 3) | 0x06)});
            return true;
        }
        *error = "invalid operand";
        return false;
    }

    if (mnem == "bit" || mnem == "res" || mnem == "set") {
        if (!need(2)) return false;
        const Operand& b = ops[0];
        const Operand& o = ops[1];
        if (b.kind != Operand::Imm || b.num < 0 || b.num > 7) {
            *error = "bit number 0..7 expected";
            return false;
        }
        const int base = (mnem == "bit") ? 0x40 : (mnem == "res") ? 0x80 : 0xC0;
        if (o.kind == Operand::Reg8 || o.kind == Operand::MemHl) {
            const int t = (o.kind == Operand::MemHl) ? 6 : o.reg;
            Emit(code, {0xCB, static_cast<uint8_t>(base | (b.num << 3) | t)});
            return true;
        }
        if (o.kind == Operand::MemIdx) {
            Emit(code, {static_cast<uint8_t>(o.reg ? 0xFD : 0xDD), 0xCB,
                        static_cast<uint8_t>(o.num & 0xFF),
                        static_cast<uint8_t>(base | (b.num << 3) | 0x06)});
            return true;
        }
        *error = "invalid operand";
        return false;
    }

    if (mnem == "push" || mnem == "pop") {
        if (!need(1)) return false;
        const Operand& o = ops[0];
        const uint8_t base = (mnem == "push") ? 0xC5 : 0xC1;
        if (o.kind == Operand::Reg16 && o.reg != 3) {  // bc de hl
            code->push_back(static_cast<uint8_t>(base | (o.reg << 4)));
            return true;
        }
        if (o.kind == Operand::AfReg) {
            code->push_back(static_cast<uint8_t>(base | 0x30));
            return true;
        }
        if (o.kind == Operand::Idx) {
            Emit(code, {static_cast<uint8_t>(o.reg ? 0xFD : 0xDD),
                        static_cast<uint8_t>(base | 0x20)});
            return true;
        }
        *error = "invalid operand";
        return false;
    }

    if (mnem == "jp" || mnem == "call") {
        if (ops.size() == 2) {
            if (ops[0].kind != Operand::Cc) { *error = "condition expected"; return false; }
            if (ops[1].kind != Operand::Imm) { *error = "target address expected"; return false; }
            code->push_back(static_cast<uint8_t>((mnem == "jp" ? 0xC2 : 0xC4) | (ops[0].reg << 3)));
            return EmitWord(code, ops[1].num, error);
        }
        if (!need(1)) return false;
        if (mnem == "jp" && ops[0].kind == Operand::MemHl) { code->push_back(0xE9); return true; }
        if (mnem == "jp" && ops[0].kind == Operand::MemIdx && ops[0].num == 0) {
            Emit(code, {static_cast<uint8_t>(ops[0].reg ? 0xFD : 0xDD), 0xE9});
            return true;
        }
        if (ops[0].kind != Operand::Imm) { *error = "target address expected"; return false; }
        code->push_back(mnem == "jp" ? 0xC3 : 0xCD);
        return EmitWord(code, ops[0].num, error);
    }

    if (mnem == "jr" || mnem == "djnz") {
        if (ops.empty() || ops.size() > 2) { *error = "wrong operand count"; return false; }
        const Operand* target = &ops[0];
        uint8_t op = (mnem == "jr") ? 0x18 : 0x10;
        if (ops.size() == 2) {
            if (mnem == "djnz" || ops[0].kind != Operand::Cc || ops[0].reg > 3) {
                *error = "condition expected";
                return false;
            }
            op = static_cast<uint8_t>(0x20 | (ops[0].reg << 3));
            target = &ops[1];
        }
        if (target->kind != Operand::Imm) { *error = "target address expected"; return false; }
        code->push_back(op);
        return EmitRelative(code, addr, target->num, error);
    }

    if (mnem == "ret") {
        if (ops.size() > 1) { *error = "wrong operand count"; return false; }
        if (ops.empty()) { code->push_back(0xC9); return true; }
        if (ops[0].kind != Operand::Cc) { *error = "condition expected"; return false; }
        code->push_back(static_cast<uint8_t>(0xC0 | (ops[0].reg << 3)));
        return true;
    }

    if (mnem == "rst") {
        if (!need(1)) return false;
        const Operand& o = ops[0];
        if (o.kind != Operand::Imm) { *error = "target expected"; return false; }
        if (o.num < 0 || (o.num & 0x38) != o.num) { *error = "invalid rst target"; return false; }
        code->push_back(static_cast<uint8_t>(0xC7 | o.num));
        return true;
    }

    if (mnem == "im") {
        if (!need(1)) return false;
        if (ops[0].kind != Operand::Imm) { *error = "0, 1 or 2 expected"; return false; }
        switch (ops[0].num) {
        case 0: Emit(code, {0xED, 0x46}); return true;
        case 1: Emit(code, {0xED, 0x56}); return true;
        case 2: Emit(code, {0xED, 0x5E}); return true;
        default: *error = "0, 1 or 2 expected"; return false;
        }
    }

    if (mnem == "ex") {
        if (!need(2)) return false;
        if (ops[0].kind == Operand::AfReg && ops[1].kind == Operand::AfReg) {
            code->push_back(0x08);  // ex af,af'
            return true;
        }
        if (ops[0].kind == Operand::Reg16 && ops[0].reg == 1 &&
            ops[1].kind == Operand::Reg16 && ops[1].reg == 2) {
            code->push_back(0xEB);  // ex de,hl
            return true;
        }
        if (ops[0].kind == Operand::MemSp) {
            if (ops[1].kind == Operand::Reg16 && ops[1].reg == 2) { code->push_back(0xE3); return true; }
            if (ops[1].kind == Operand::Idx) {
                Emit(code, {static_cast<uint8_t>(ops[1].reg ? 0xFD : 0xDD), 0xE3});
                return true;
            }
        }
        *error = "invalid operands";
        return false;
    }

    if (mnem == "in") {
        if (!need(2)) return false;
        if (ops[0].kind == Operand::Reg8 && ops[0].reg == 7 && ops[1].kind == Operand::MemNn) {
            code->push_back(0xDB);  // in a,(n)
            return EmitByte(code, ops[1].num, error);
        }
        if (ops[0].kind == Operand::Reg8 && ops[1].kind == Operand::MemC) {  // in r,(c)
            Emit(code, {0xED, static_cast<uint8_t>(0x40 | (ops[0].reg << 3))});
            return true;
        }
        *error = "invalid operands";
        return false;
    }

    if (mnem == "out") {
        if (!need(2)) return false;
        if (ops[0].kind == Operand::MemNn && ops[1].kind == Operand::Reg8 && ops[1].reg == 7) {
            code->push_back(0xD3);  // out (n),a
            return EmitByte(code, ops[0].num, error);
        }
        if (ops[0].kind == Operand::MemC) {  // out (c),r / undocumented out (c),0
            if (ops[1].kind == Operand::Reg8) {
                Emit(code, {0xED, static_cast<uint8_t>(0x41 | (ops[1].reg << 3))});
                return true;
            }
            if (ops[1].kind == Operand::Imm && ops[1].num == 0) {
                Emit(code, {0xED, 0x71});
                return true;
            }
        }
        *error = "invalid operands";
        return false;
    }

    *error = "unknown mnemonic: " + mnem;
    return false;
}

}  // namespace

AssembleResult AssembleSubset(uint16_t addr, const std::string& text) {
    AssembleResult res;
    if (AssembleOne(addr, text, &res.bytes, &res.error)) {
        res.ok = true;
        res.error.clear();
    } else {
        res.bytes.clear();
        if (res.error.empty()) res.error = "syntax error";
        res.ok = false;
    }
    return res;
}

}  // namespace dbg
