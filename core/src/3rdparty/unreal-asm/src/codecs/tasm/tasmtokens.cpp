#include "codecs/tasm/tasmtokens.h"

namespace unrealasm::codecs::tasm
{
namespace
{
// TASM 4.0 XLD / 4.4 KVA, #80-#F0 (the owner's 2012 converter's table; #80-#E6 are the table in the TASM 3.0 and 3.5
// binaries, word for word). Confirmed on 91 real 4.0 / 4.4 sources, which use #E7-#F0 too
constexpr TokenTable kTasm40 = {
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

void Clear(TokenTable& table, uint8_t from, uint8_t to)
{
    for (int code = from; code <= to; ++code)
        table[code - kFirstToken] = {};
}

// TASM 3.0 / 3.5: the binary's table ends after incbin (#E6)
TokenTable MakeTasm3()
{
    TokenTable table = kTasm40;
    Clear(table, 0xE7, kLastToken);
    return table;
}

// TASM 4.12: the binary's table ends after z (#E4) and renames three codes
TokenTable MakeTasm412()
{
    TokenTable table = kTasm40;
    Clear(table, 0xE5, kLastToken);
    table[0x97 - kFirstToken] = "defmac ";
    table[0x9B - kFirstToken] = "display ";
    table[0x9F - kFirstToken] = "endmac";
    return table;
}
}  // namespace

const TokenTable& Tasm3Tokens()
{
    static const TokenTable table = MakeTasm3();
    return table;
}

const TokenTable& Tasm40Tokens()
{
    return kTasm40;
}

const TokenTable& Tasm412Tokens()
{
    static const TokenTable table = MakeTasm412();
    return table;
}
}  // namespace unrealasm::codecs::tasm
