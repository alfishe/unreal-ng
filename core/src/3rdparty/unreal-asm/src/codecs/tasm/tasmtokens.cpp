#include "codecs/tasm/tasmtokens.h"

namespace unrealasm::codecs::tasm
{
namespace
{
// TASM 3.x, #80-#F0. Confirmed on the TASM 3.2 disk's sources (testdata/tasm3): every line of them decodes to valid
// Z80 source with this table
constexpr TokenTable kTasm3 = {
    "a",     "adc ",   "add ",     "af'",     "af",     "and ",   "b",      "bc",        // 80-87
    "bit ",  "c",      "call ",    "ccf",     "cp ",    "cpd",    "cpdr",   "cpi",       // 88-8F
    "cpir",  "cpl",    "d",        "daa",     "de",     "dec ",   "defb ",  "defm ",     // 90-97
    "defs ", "defw ",  "di",       "phase ",  "djnz ",  "e",      "ei",     "unphase",   // 98-9F
    "equ ",  "ex ",    "exx",      "h",       "halt",   "hl",     "i",      "im ",       // A0-A7
    "in ",   "inc ",   "ind",      "indr",    "ini",    "inir",   "ix",     "iy",        // A8-AF
    "jp ",   "jr ",    "l",        "ld ",     "ldd",    "lddr",   "ldi",    "ldir",      // B0-B7
    "m",     "nc",     "neg",      "nop",     "nv",     "nz",     "or ",    "org ",      // B8-BF
    "otdr",  "otir",   "out ",     "outd",    "outi",   "p",      "pe",     "po",        // C0-C7
    "pop ",  "push ",  "r",        "res ",    "ret",    "reti",   "retn",   "rl ",       // C8-CF
    "rla",   "rlc ",   "rlca",     "rld",     "rr ",    "rra",    "rrc ",   "rrca",      // D0-D7
    "rrd",   "rst ",   "sbc ",     "scf",     "set ",   "sla ",   "sp",     "sra ",      // D8-DF
    "srl ",  "sub ",   "v",        "xor ",    "z",      "include ", "incbin ", "sli ",   // E0-E7
    "inf",   "lx",     "hx",       "ly",      "hy",     "db ",    "dm ",    "ds ",       // E8-EF
    "dw ",                                                                               // F0
};

TokenTable MakeTasm4()
{
    TokenTable table = kTasm3;
    table[0x97 - kFirstToken] = "defmac ";
    table[0x9B - kFirstToken] = "display ";
    table[0x9F - kFirstToken] = "endmac ";
    return table;
}
}  // namespace

const TokenTable& Tasm3Tokens()
{
    return kTasm3;
}

const TokenTable& Tasm4Tokens()
{
    static const TokenTable table = MakeTasm4();
    return table;
}
}  // namespace unrealasm::codecs::tasm
