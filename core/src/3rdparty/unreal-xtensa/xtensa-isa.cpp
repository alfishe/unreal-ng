// xtensa-isa.cpp - decoder and disassembler, see xtensa-isa.h.

#include "xtensa-isa.h"

#include <cstdio>

namespace xtensa
{

const int32_t kB4Const[16] = {-1, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 32, 64, 128, 256};
const uint32_t kB4ConstU[16] = {32768, 65536, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 32, 64, 128, 256};

namespace
{

struct OpInfo
{
    const char* mnemonic;
    const char* format;
};

const OpInfo kOpInfo[] = {
#define XTENSA_OP_INFO(name, mnemonic, format) {mnemonic, format},
    XTENSA_OPS(XTENSA_OP_INFO)
#undef XTENSA_OP_INFO
};
static_assert(sizeof(kOpInfo) / sizeof(kOpInfo[0]) == static_cast<size_t>(Op::Count), "op table size");

const char* const kSpecialRegisterNames[256] = {
    // clang-format off
    /*   0 */ "lbeg", "lend", "lcount", "sar", "br", "litbase", nullptr, nullptr,
    /*   8 */ nullptr, nullptr, nullptr, nullptr, "scompare1", nullptr, nullptr, nullptr,
    /*  16 */ "acclo", "acchi", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /*  24 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /*  32 */ "m0", "m1", "m2", "m3", nullptr, nullptr, nullptr, nullptr,
    /*  40 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /*  48 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /*  56 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /*  64 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /*  72 */ "windowbase", "windowstart", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /*  80 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /*  88 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, "eraccess",
    /*  96 */ "ibreakenable", "memctl", nullptr, "atomctl", nullptr, nullptr, nullptr, nullptr,
    /* 104 */ "ddr", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 112 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 120 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 128 */ "ibreaka0", "ibreaka1", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 136 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 144 */ "dbreaka0", "dbreaka1", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 152 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 160 */ "dbreakc0", "dbreakc1", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 168 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 176 */ "configid0", "epc1", "epc2", "epc3", "epc4", "epc5", "epc6", "epc7",
    /* 184 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 192 */ "depc", nullptr, "eps2", "eps3", "eps4", "eps5", "eps6", "eps7",
    /* 200 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 208 */ "configid1", "excsave1", "excsave2", "excsave3", "excsave4", "excsave5", "excsave6", "excsave7",
    /* 216 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    /* 224 */ "cpenable", nullptr, "interrupt", nullptr, "intenable", nullptr, "ps", "vecbase",
    /* 232 */ "exccause", "debugcause", "ccount", "prid", "icount", "icountlevel", "excvaddr", nullptr,
    /* 240 */ "ccompare0", "ccompare1", "ccompare2", nullptr, "misc0", "misc1", "misc2", "misc3",
    /* 248 */ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    // clang-format on
};

const char* const kUserRegisterLow[19] = {
    "accx_0",   "accx_1",   "qacc_h_0", "qacc_h_1",      "qacc_h_2",   "qacc_h_3",   "qacc_h_4",
    "qacc_l_0", "qacc_l_1", "qacc_l_2", "qacc_l_3",      "qacc_l_4",   "gpio_out",   "sar_byte",
    "fft_bit_width",        "ua_state_0", "ua_state_1", "ua_state_2", "ua_state_3",
};

inline int32_t SignExtend(uint32_t value, int bits)
{
    const uint32_t m = 1u << (bits - 1);
    value &= (bits == 32) ? 0xFFFFFFFFu : ((1u << bits) - 1);
    return static_cast<int32_t>((value ^ m) - m);
}

/// Sets maxGroup from the AR operands named in the format string.
void ComputeMaxGroup(Insn& insn)
{
    const char* f = kOpInfo[static_cast<size_t>(insn.op)].format;
    int maxGroup = 0;
    for (const char* p = f; *p; ++p)
    {
        if (*p == 'r' && (p == f || p[-1] == ' '))
        {
            const uint8_t field = p[1] == 'a' ? insn.a : (p[1] == 'b' ? insn.b : insn.c);
            if (field / 4 > maxGroup)
                maxGroup = field / 4;
        }
    }
    insn.maxGroup = static_cast<uint8_t>(maxGroup);
}

void Set(Insn& d, Op op, int a = 0, int b = 0, int c = 0, int32_t imm = 0)
{
    d.op = op;
    d.a = static_cast<uint8_t>(a);
    d.b = static_cast<uint8_t>(b);
    d.c = static_cast<uint8_t>(c);
    d.imm = imm;
}

void DecodeNarrow(uint32_t pc, uint32_t w, Insn& d)
{
    const int op0 = static_cast<int>(w & 15);
    const int t = static_cast<int>((w >> 4) & 15);
    const int s = static_cast<int>((w >> 8) & 15);
    const int r = static_cast<int>((w >> 12) & 15);
    switch (op0)
    {
        case 8: Set(d, Op::L32iN, t, s, 0, r * 4); break;
        case 9: Set(d, Op::S32iN, t, s, 0, r * 4); break;
        case 10: Set(d, Op::AddN, r, s, t); break;
        case 11: Set(d, Op::AddiN, r, s, 0, t == 0 ? -1 : t); break;
        case 12:
            if ((t & 8) == 0)
            {
                int v = ((t & 7) << 4) | r;
                if (v >= 96)
                    v -= 128;
                Set(d, Op::MoviN, s, 0, 0, v);
            }
            else
            {
                const uint32_t target = pc + 4 + static_cast<uint32_t>(((t & 3) << 4) | r);
                Set(d, (t & 4) ? Op::BnezN : Op::BeqzN, 0, s, 0, static_cast<int32_t>(target));
            }
            break;
        case 13:
            if (r == 0)
                Set(d, Op::MovN, t, s);
            else if (r == 15)
            {
                switch (t)
                {
                    case 0: Set(d, Op::RetN); break;
                    case 1: Set(d, Op::RetwN); break;
                    case 2: Set(d, Op::BreakN, 0, 0, 0, s); break;
                    case 3: Set(d, Op::NopN); break;
                    case 6: Set(d, Op::IllN); break;
                    default: break;
                }
            }
            break;
        default: break;
    }
}

void DecodeRst0(int op2, int r, int s, int t, Insn& d)
{
    switch (op2)
    {
        case 0: // ST0
            switch (r)
            {
                case 0: // SNM0
                {
                    const int m = t >> 2, n = t & 3;
                    if (m == 0 && n == 0 && s == 0)
                        Set(d, Op::Ill);
                    else if (m == 2)
                    {
                        if (n == 0 && s == 0)
                            Set(d, Op::Ret);
                        else if (n == 1 && s == 0)
                            Set(d, Op::Retw);
                        else if (n == 2)
                            Set(d, Op::Jx, 0, s);
                    }
                    else if (m == 3)
                    {
                        static const Op kCallx[4] = {Op::Callx0, Op::Callx4, Op::Callx8, Op::Callx12};
                        Set(d, kCallx[n], 0, s);
                    }
                    break;
                }
                case 1: Set(d, Op::Movsp, t, s); break;
                case 2:
                    if (s == 0)
                    {
                        switch (t)
                        {
                            case 0: Set(d, Op::Isync); break;
                            case 1: Set(d, Op::Rsync); break;
                            case 2: Set(d, Op::Esync); break;
                            case 3: Set(d, Op::Dsync); break;
                            case 8: Set(d, Op::Excw); break;
                            case 12: Set(d, Op::Memw); break;
                            case 13: Set(d, Op::Extw); break;
                            case 15: Set(d, Op::Nop); break;
                            default: break;
                        }
                    }
                    break;
                case 3:
                    if (t == 0)
                    {
                        switch (s)
                        {
                            case 0: Set(d, Op::Rfe); break;
                            case 1: Set(d, Op::Rfue); break;
                            case 2: Set(d, Op::Rfde); break;
                            case 4: Set(d, Op::Rfwo); break;
                            case 5: Set(d, Op::Rfwu); break;
                            default: break;
                        }
                    }
                    else if (t == 1)
                        Set(d, Op::Rfi, 0, 0, 0, s);
                    break;
                case 4: Set(d, Op::Break, 0, s, t); break;
                case 5:
                    if (t == 0 && s == 0)
                        Set(d, Op::Syscall);
                    else if (t == 0 && s == 1)
                        Set(d, Op::Simcall);
                    break;
                case 6: Set(d, Op::Rsil, t, 0, 0, s); break;
                case 7:
                    if (t == 0)
                        Set(d, Op::Waiti, 0, 0, 0, s);
                    break;
                case 8:
                    if ((s & 3) == 0)
                        Set(d, Op::Any4, t, s);
                    break;
                case 9:
                    if ((s & 3) == 0)
                        Set(d, Op::All4, t, s);
                    break;
                case 10:
                    if ((s & 7) == 0)
                        Set(d, Op::Any8, t, s);
                    break;
                case 11:
                    if ((s & 7) == 0)
                        Set(d, Op::All8, t, s);
                    break;
                default: break;
            }
            break;
        case 1: Set(d, Op::And, r, s, t); break;
        case 2: Set(d, Op::Or, r, s, t); break;
        case 3: Set(d, Op::Xor, r, s, t); break;
        case 4: // ST1
            switch (r)
            {
                case 0:
                    if (t == 0)
                        Set(d, Op::Ssr, 0, s);
                    break;
                case 1:
                    if (t == 0)
                        Set(d, Op::Ssl, 0, s);
                    break;
                case 2:
                    if (t == 0)
                        Set(d, Op::Ssa8l, 0, s);
                    break;
                case 3:
                    if (t == 0)
                        Set(d, Op::Ssa8b, 0, s);
                    break;
                case 4:
                    if ((t & 14) == 0)
                        Set(d, Op::Ssai, 0, 0, 0, s | ((t & 1) << 4));
                    break;
                case 6: Set(d, Op::Rer, t, s); break;
                case 7: Set(d, Op::Wer, t, s); break;
                case 8:
                    if (s == 0)
                        Set(d, Op::Rotw, 0, 0, 0, SignExtend(static_cast<uint32_t>(t), 4));
                    break;
                case 14: Set(d, Op::Nsa, t, s); break;
                case 15: Set(d, Op::Nsau, t, s); break;
                default: break;
            }
            break;
        case 6:
            if (s == 0)
                Set(d, Op::Neg, r, 0, t);
            else if (s == 1)
                Set(d, Op::Abs, r, 0, t);
            break;
        case 8: Set(d, Op::Add, r, s, t); break;
        case 9: Set(d, Op::Addx2, r, s, t); break;
        case 10: Set(d, Op::Addx4, r, s, t); break;
        case 11: Set(d, Op::Addx8, r, s, t); break;
        case 12: Set(d, Op::Sub, r, s, t); break;
        case 13: Set(d, Op::Subx2, r, s, t); break;
        case 14: Set(d, Op::Subx4, r, s, t); break;
        case 15: Set(d, Op::Subx8, r, s, t); break;
        default: break;
    }
}

void DecodeRst1(int op2, int r, int s, int t, Insn& d)
{
    switch (op2)
    {
        case 0:
        case 1: Set(d, Op::Slli, r, s, 0, 32 - (((op2 & 1) << 4) | t)); break;
        case 2:
        case 3: Set(d, Op::Srai, r, t, 0, ((op2 & 1) << 4) | s); break;
        case 4: Set(d, Op::Srli, r, t, 0, s); break;
        case 6:
        {
            const int sr = (r << 4) | s;
            if (SpecialRegisterName(static_cast<uint32_t>(sr)))
                Set(d, Op::Xsr, t, 0, sr);
            break;
        }
        case 8: Set(d, Op::Src, r, s, t); break;
        case 9:
            if (s == 0)
                Set(d, Op::Srl, r, 0, t);
            break;
        case 10:
            if (t == 0)
                Set(d, Op::Sll, r, s);
            break;
        case 11:
            if (s == 0)
                Set(d, Op::Sra, r, 0, t);
            break;
        case 12: Set(d, Op::Mul16u, r, s, t); break;
        case 13: Set(d, Op::Mul16s, r, s, t); break;
        default: break;
    }
}

void DecodeRst2(int op2, int r, int s, int t, Insn& d)
{
    switch (op2)
    {
        case 0: Set(d, Op::Andb, r, s, t); break;
        case 1: Set(d, Op::Andbc, r, s, t); break;
        case 2: Set(d, Op::Orb, r, s, t); break;
        case 3: Set(d, Op::Orbc, r, s, t); break;
        case 4: Set(d, Op::Xorb, r, s, t); break;
        case 6: Set(d, Op::Saltu, r, s, t); break;
        case 7: Set(d, Op::Salt, r, s, t); break;
        case 8: Set(d, Op::Mull, r, s, t); break;
        case 10: Set(d, Op::Muluh, r, s, t); break;
        case 11: Set(d, Op::Mulsh, r, s, t); break;
        case 12: Set(d, Op::Quou, r, s, t); break;
        case 13: Set(d, Op::Quos, r, s, t); break;
        case 14: Set(d, Op::Remu, r, s, t); break;
        case 15: Set(d, Op::Rems, r, s, t); break;
        default: break;
    }
}

void DecodeRst3(int op2, int r, int s, int t, Insn& d)
{
    switch (op2)
    {
        case 0:
        case 1:
        {
            const int sr = (r << 4) | s;
            if (SpecialRegisterName(static_cast<uint32_t>(sr)))
                Set(d, op2 == 0 ? Op::Rsr : Op::Wsr, t, 0, sr);
            break;
        }
        case 2: Set(d, Op::Sext, r, s, 0, t + 7); break;
        case 3: Set(d, Op::Clamps, r, s, 0, t + 7); break;
        case 4: Set(d, Op::Min, r, s, t); break;
        case 5: Set(d, Op::Max, r, s, t); break;
        case 6: Set(d, Op::Minu, r, s, t); break;
        case 7: Set(d, Op::Maxu, r, s, t); break;
        case 8: Set(d, Op::Moveqz, r, s, t); break;
        case 9: Set(d, Op::Movnez, r, s, t); break;
        case 10: Set(d, Op::Movltz, r, s, t); break;
        case 11: Set(d, Op::Movgez, r, s, t); break;
        case 12: Set(d, Op::Movf, r, s, t); break;
        case 13: Set(d, Op::Movt, r, s, t); break;
        case 14:
        {
            const int ur = (s << 4) | t;
            if (UserRegisterName(static_cast<uint32_t>(ur)))
                Set(d, Op::Rur, r, 0, ur);
            break;
        }
        case 15:
        {
            const int ur = (r << 4) | s;
            if (UserRegisterName(static_cast<uint32_t>(ur)))
                Set(d, Op::Wur, t, 0, ur);
            break;
        }
        default: break;
    }
}

void DecodeFp0(int op2, int r, int s, int t, Insn& d)
{
    switch (op2)
    {
        case 0: Set(d, Op::AddS, r, s, t); break;
        case 1: Set(d, Op::SubS, r, s, t); break;
        case 2: Set(d, Op::MulS, r, s, t); break;
        case 4: Set(d, Op::MaddS, r, s, t); break;
        case 5: Set(d, Op::MsubS, r, s, t); break;
        case 6: Set(d, Op::MaddnS, r, s, t); break;
        case 7: Set(d, Op::DivnS, r, s, t); break;
        case 8: Set(d, Op::RoundS, r, s, 0, t); break;
        case 9: Set(d, Op::TruncS, r, s, 0, t); break;
        case 10: Set(d, Op::FloorS, r, s, 0, t); break;
        case 11: Set(d, Op::CeilS, r, s, 0, t); break;
        case 12: Set(d, Op::FloatS, r, s, 0, t); break;
        case 13: Set(d, Op::UfloatS, r, s, 0, t); break;
        case 14: Set(d, Op::UtruncS, r, s, 0, t); break;
        case 15:
            switch (t)
            {
                case 0: Set(d, Op::MovS, r, s); break;
                case 1: Set(d, Op::AbsS, r, s); break;
                case 3: Set(d, Op::ConstS, r, 0, 0, s); break;
                case 4: Set(d, Op::Rfr, r, s); break;
                case 5: Set(d, Op::Wfr, r, s); break;
                case 6: Set(d, Op::NegS, r, s); break;
                case 7: Set(d, Op::Div0S, r, s); break;
                case 8: Set(d, Op::Recip0S, r, s); break;
                case 9: Set(d, Op::Sqrt0S, r, s); break;
                case 10: Set(d, Op::Rsqrt0S, r, s); break;
                case 11: Set(d, Op::Nexp01S, r, s); break;
                case 12: Set(d, Op::MksadjS, r, s); break;
                case 13: Set(d, Op::MkdadjS, r, s); break;
                case 14: Set(d, Op::AddexpS, r, s); break;
                case 15: Set(d, Op::AddexpmS, r, s); break;
                default: break;
            }
            break;
        default: break;
    }
}

void DecodeFp1(int op2, int r, int s, int t, Insn& d)
{
    static const Op kOps[16] = {Op::Illegal, Op::UnS,     Op::OeqS,    Op::UeqS,    Op::OltS,    Op::UltS,
                                Op::OleS,    Op::UleS,    Op::MoveqzS, Op::MovnezS, Op::MovltzS, Op::MovgezS,
                                Op::MovfS,   Op::MovtS,   Op::Illegal, Op::Illegal};
    if (kOps[op2] != Op::Illegal)
        Set(d, kOps[op2], r, s, t);
}

void DecodeQrst(uint32_t w, Insn& d)
{
    const int t = static_cast<int>((w >> 4) & 15);
    const int s = static_cast<int>((w >> 8) & 15);
    const int r = static_cast<int>((w >> 12) & 15);
    const int op1 = static_cast<int>((w >> 16) & 15);
    const int op2 = static_cast<int>((w >> 20) & 15);
    switch (op1)
    {
        case 0: DecodeRst0(op2, r, s, t, d); break;
        case 1: DecodeRst1(op2, r, s, t, d); break;
        case 2: DecodeRst2(op2, r, s, t, d); break;
        case 3: DecodeRst3(op2, r, s, t, d); break;
        case 4:
        case 5: Set(d, Op::Extui, r, t, ((op1 & 1) << 4) | s, op2 + 1); break;
        case 8:
            switch (op2)
            {
                case 0: Set(d, Op::Lsx, r, s, t); break;
                case 1: Set(d, Op::Lsxp, r, s, t); break;
                case 4: Set(d, Op::Ssx, r, s, t); break;
                case 5: Set(d, Op::Ssxp, r, s, t); break;
                default: break;
            }
            break;
        case 9:
            if (op2 == 0)
                Set(d, Op::L32e, t, s, 0, r * 4 - 64);
            else if (op2 == 4)
                Set(d, Op::S32e, t, s, 0, r * 4 - 64);
            else if (op2 == 5)
                Set(d, Op::S32nb, t, s, 0, r * 4);
            break;
        case 10: DecodeFp0(op2, r, s, t, d); break;
        case 11: DecodeFp1(op2, r, s, t, d); break;
        default: break;
    }
}

void DecodeWide(uint32_t pc, uint32_t w, Insn& d)
{
    const int op0 = static_cast<int>(w & 15);
    const int t = static_cast<int>((w >> 4) & 15);
    const int s = static_cast<int>((w >> 8) & 15);
    const int r = static_cast<int>((w >> 12) & 15);
    const uint32_t imm8 = (w >> 16) & 0xFF;
    switch (op0)
    {
        case 0: DecodeQrst(w, d); break;
        case 1:
        {
            const uint32_t imm16 = (w >> 8) & 0xFFFF;
            const uint32_t target = ((pc + 3) & ~3u) + ((0xFFFF0000u | imm16) << 2);
            Set(d, Op::L32r, t, 0, 0, static_cast<int32_t>(target));
            break;
        }
        case 2: // LSAI
            switch (r)
            {
                case 0: Set(d, Op::L8ui, t, s, 0, static_cast<int32_t>(imm8)); break;
                case 1: Set(d, Op::L16ui, t, s, 0, static_cast<int32_t>(imm8 << 1)); break;
                case 2: Set(d, Op::L32i, t, s, 0, static_cast<int32_t>(imm8 << 2)); break;
                case 4: Set(d, Op::S8i, t, s, 0, static_cast<int32_t>(imm8)); break;
                case 5: Set(d, Op::S16i, t, s, 0, static_cast<int32_t>(imm8 << 1)); break;
                case 6: Set(d, Op::S32i, t, s, 0, static_cast<int32_t>(imm8 << 2)); break;
                case 9: Set(d, Op::L16si, t, s, 0, static_cast<int32_t>(imm8 << 1)); break;
                case 10: Set(d, Op::Movi, t, 0, 0, SignExtend((static_cast<uint32_t>(s) << 8) | imm8, 12)); break;
                case 11: Set(d, Op::L32ai, t, s, 0, static_cast<int32_t>(imm8 << 2)); break;
                case 12: Set(d, Op::Addi, t, s, 0, SignExtend(imm8, 8)); break;
                case 13: Set(d, Op::Addmi, t, s, 0, SignExtend(imm8, 8) * 256); break;
                case 14: Set(d, Op::S32c1i, t, s, 0, static_cast<int32_t>(imm8 << 2)); break;
                case 15: Set(d, Op::S32ri, t, s, 0, static_cast<int32_t>(imm8 << 2)); break;
                default: break; // r = 7: CACHE group, the ESP32-S3 has no caches the ISA can name
            }
            break;
        case 3: // LSCI
            switch (r)
            {
                case 0: Set(d, Op::Lsi, t, s, 0, static_cast<int32_t>(imm8 << 2)); break;
                case 4: Set(d, Op::Ssi, t, s, 0, static_cast<int32_t>(imm8 << 2)); break;
                case 8: Set(d, Op::Lsip, t, s, 0, static_cast<int32_t>(imm8 << 2)); break;
                case 12: Set(d, Op::Ssip, t, s, 0, static_cast<int32_t>(imm8 << 2)); break;
                default: break;
            }
            break;
        case 4: Set(d, Op::Mac16); break;
        case 5: // CALLN
        {
            static const Op kCall[4] = {Op::Call0, Op::Call4, Op::Call8, Op::Call12};
            const int32_t offset = SignExtend(w >> 6, 18);
            const uint32_t target = (pc & ~3u) + static_cast<uint32_t>(offset << 2) + 4;
            Set(d, kCall[(w >> 4) & 3], 0, 0, 0, static_cast<int32_t>(target));
            break;
        }
        case 6: // SI
        {
            const int n = (w >> 4) & 3;
            const int m = (w >> 6) & 3;
            if (n == 0)
            {
                const uint32_t target = pc + 4 + static_cast<uint32_t>(SignExtend(w >> 6, 18));
                Set(d, Op::J, 0, 0, 0, static_cast<int32_t>(target));
            }
            else if (n == 1)
            {
                static const Op kBz[4] = {Op::Beqz, Op::Bnez, Op::Bltz, Op::Bgez};
                const uint32_t target = pc + 4 + static_cast<uint32_t>(SignExtend(w >> 12, 12));
                Set(d, kBz[m], 0, s, 0, static_cast<int32_t>(target));
            }
            else
            {
                const uint32_t target8 = pc + 4 + static_cast<uint32_t>(SignExtend(imm8, 8));
                if (n == 2)
                {
                    static const Op kBi0[4] = {Op::Beqi, Op::Bnei, Op::Blti, Op::Bgei};
                    Set(d, kBi0[m], 0, s, r, static_cast<int32_t>(target8));
                }
                else if (m == 0)
                    Set(d, Op::Entry, 0, s, 0, static_cast<int32_t>(((w >> 12) & 0xFFF) << 3));
                else if (m == 1)
                {
                    const uint32_t targetU = pc + 4 + imm8;
                    switch (r)
                    {
                        case 0: Set(d, Op::Bf, 0, s, 0, static_cast<int32_t>(target8)); break;
                        case 1: Set(d, Op::Bt, 0, s, 0, static_cast<int32_t>(target8)); break;
                        case 8: Set(d, Op::Loop, 0, s, 0, static_cast<int32_t>(targetU)); break;
                        case 9: Set(d, Op::Loopnez, 0, s, 0, static_cast<int32_t>(targetU)); break;
                        case 10: Set(d, Op::Loopgtz, 0, s, 0, static_cast<int32_t>(targetU)); break;
                        default: break;
                    }
                }
                else
                    Set(d, m == 2 ? Op::Bltui : Op::Bgeui, 0, s, r, static_cast<int32_t>(target8));
            }
            break;
        }
        case 7: // B
        {
            static const Op kB[16] = {Op::Bnone, Op::Beq,  Op::Blt,   Op::Bltu, Op::Ball, Op::Bbc,  Op::Bbci, Op::Bbci,
                                      Op::Bany,  Op::Bne,  Op::Bge,   Op::Bgeu, Op::Bnall, Op::Bbs, Op::Bbsi, Op::Bbsi};
            const uint32_t target = pc + 4 + static_cast<uint32_t>(SignExtend(imm8, 8));
            const Op op = kB[r];
            const int c = (op == Op::Bbci || op == Op::Bbsi) ? (((r & 1) << 4) | t) : t;
            Set(d, op, 0, s, c, static_cast<int32_t>(target));
            break;
        }
        default: break;
    }
}

bool EndsBlock(Op op)
{
    switch (op)
    {
        case Op::Add: case Op::AddN: case Op::Addx2: case Op::Addx4: case Op::Addx8: case Op::Sub: case Op::Subx2:
        case Op::Subx4: case Op::Subx8: case Op::And: case Op::Or: case Op::Xor: case Op::Neg: case Op::Abs:
        case Op::Addi: case Op::AddiN: case Op::Addmi: case Op::Movi: case Op::MoviN: case Op::MovN: case Op::Salt:
        case Op::Saltu: case Op::Min: case Op::Max: case Op::Minu: case Op::Maxu: case Op::Moveqz: case Op::Movnez:
        case Op::Movltz: case Op::Movgez: case Op::Movf: case Op::Movt: case Op::Sext: case Op::Clamps: case Op::Nsa:
        case Op::Nsau: case Op::Mul16u: case Op::Mul16s: case Op::Mull: case Op::Muluh: case Op::Mulsh: case Op::Quou:
        case Op::Quos: case Op::Remu: case Op::Rems: case Op::Slli: case Op::Srai: case Op::Srli: case Op::Src:
        case Op::Srl: case Op::Sll: case Op::Sra: case Op::Ssr: case Op::Ssl: case Op::Ssa8l: case Op::Ssa8b:
        case Op::Ssai: case Op::Extui: case Op::Andb: case Op::Andbc: case Op::Orb: case Op::Orbc: case Op::Xorb:
        case Op::Any4: case Op::All4: case Op::Any8: case Op::All8: case Op::L8ui: case Op::L16ui: case Op::L16si:
        case Op::L32i: case Op::L32iN: case Op::L32ai: case Op::S8i: case Op::S16i: case Op::S32i: case Op::S32iN:
        case Op::S32ri: case Op::S32nb: case Op::S32c1i: case Op::L32e: case Op::S32e: case Op::L32r: case Op::Rsr:
        case Op::Rur: case Op::Wur: case Op::Isync: case Op::Rsync: case Op::Esync: case Op::Dsync: case Op::Excw:
        case Op::Memw: case Op::Extw: case Op::Nop: case Op::NopN: case Op::AddS: case Op::SubS: case Op::MulS:
        case Op::MaddS: case Op::MsubS: case Op::MaddnS: case Op::RoundS: case Op::TruncS: case Op::FloorS:
        case Op::CeilS: case Op::UtruncS: case Op::FloatS: case Op::UfloatS: case Op::MovS: case Op::AbsS:
        case Op::NegS: case Op::Rfr: case Op::Wfr: case Op::UnS: case Op::OeqS: case Op::UeqS: case Op::OltS:
        case Op::UltS: case Op::OleS: case Op::UleS: case Op::MoveqzS: case Op::MovnezS: case Op::MovltzS:
        case Op::MovgezS: case Op::MovfS: case Op::MovtS: case Op::Lsi: case Op::Lsip: case Op::Ssi: case Op::Ssip:
        case Op::Lsx: case Op::Lsxp: case Op::Ssx: case Op::Ssxp:
            return false;
        default:
            return true;
    }
}

} // namespace

const char* Mnemonic(Op op)
{
    return kOpInfo[static_cast<size_t>(op)].mnemonic;
}

const char* SpecialRegisterName(uint32_t sr)
{
    return sr < 256 ? kSpecialRegisterNames[sr] : nullptr;
}

const char* UserRegisterName(uint32_t ur)
{
    if (ur < 19)
        return kUserRegisterLow[ur];
    switch (ur)
    {
        case UrThreadPtr: return "threadptr";
        case UrFcr: return "fcr";
        case UrFsr: return "fsr";
        default: return nullptr;
    }
}

bool Decode(uint32_t pc, const uint8_t* bytes, size_t avail, Insn& out)
{
    if (avail < 1)
        return false;
    const int len = InsnLength(bytes[0]);
    if (avail < static_cast<size_t>(len))
        return false;
    out = Insn{};
    out.pc = pc;
    out.len = static_cast<uint8_t>(len);
    if (len == 4)
        out.op = Op::EeInsn;
    else if (len == 2)
        DecodeNarrow(pc, static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8), out);
    else
        DecodeWide(pc, static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
                           (static_cast<uint32_t>(bytes[2]) << 16),
                   out);

    ComputeMaxGroup(out);
    switch (out.op)
    {
        case Op::Call4: out.maxGroup = 1; break;
        case Op::Call8: out.maxGroup = 2; break;
        case Op::Call12: out.maxGroup = 3; break;
        case Op::Callx4: out.maxGroup = static_cast<uint8_t>(out.maxGroup > 1 ? out.maxGroup : 1); break;
        case Op::Callx8: out.maxGroup = static_cast<uint8_t>(out.maxGroup > 2 ? out.maxGroup : 2); break;
        case Op::Callx12: out.maxGroup = 3; break;
        case Op::Entry: out.maxGroup = 0; break; // ENTRY checks the window itself
        default: break;
    }
    if (EndsBlock(out.op))
        out.flags |= InsnEndsBlock;
    return true;
}

namespace
{

void AppendImm(std::string& s, int64_t v)
{
    char buf[24];
    if (v > -256 && v < 256)
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(v));
    else
        std::snprintf(buf, sizeof(buf), "0x%x", static_cast<uint32_t>(v));
    s += buf;
}

} // namespace

std::string Disassemble(const Insn& insn)
{
    const OpInfo& info = kOpInfo[static_cast<size_t>(insn.op)];
    std::string s = info.mnemonic;
    if (insn.op == Op::Rsr || insn.op == Op::Wsr || insn.op == Op::Xsr)
    {
        s += '.';
        const char* name = SpecialRegisterName(insn.c);
        s += name ? name : "?";
    }
    else if (insn.op == Op::Rur || insn.op == Op::Wur)
    {
        s += '.';
        const char* name = UserRegisterName(insn.c);
        s += name ? name : "?";
    }
    const char* f = info.format;
    bool first = true;
    char buf[64];
    while (*f)
    {
        while (*f == ' ')
            ++f;
        if (!*f)
            break;
        s += first ? "\t" : ", ";
        first = false;
        const char kind = *f++;
        auto field = [&](char which) -> uint8_t { return which == 'a' ? insn.a : (which == 'b' ? insn.b : insn.c); };
        switch (kind)
        {
            case 'r':
            case 'f':
            case 'b':
                std::snprintf(buf, sizeof(buf), "%c%d", kind == 'r' ? 'a' : kind, field(*f++));
                s += buf;
                break;
            case 'i': AppendImm(s, insn.imm); break;
            case 't':
                std::snprintf(buf, sizeof(buf), "%x", static_cast<uint32_t>(insn.imm));
                s += buf;
                break;
            case 'n': AppendImm(s, field(*f++)); break;
            case 'k': AppendImm(s, kB4Const[field(*f++) & 15]); break;
            case 'K': AppendImm(s, kB4ConstU[field(*f++) & 15]); break;
            case 'g':
            {
                const int count = *f++ - '0';
                const int base = field(*f++);
                for (int i = 0; i < count; ++i)
                {
                    std::snprintf(buf, sizeof(buf), "%sb%d", i ? ":" : "", base + i);
                    s += buf;
                }
                break;
            }
            default: break;
        }
    }
    return s;
}

} // namespace xtensa
