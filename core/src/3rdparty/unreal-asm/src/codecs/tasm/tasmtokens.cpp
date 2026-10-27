#include "codecs/tasm/tasmtokens.h"

namespace unrealasm::codecs::tasm
{
namespace
{
// TASM 4.0 XLD / 4.4 KVA, #80-#F0 (the owner's 2012 converter's table; #80-#E6 are the table in the TASM 3.0 and 3.5
// binaries, word for word). Confirmed on 91 real 4.0 / 4.4 sources, which use #E7-#F0 too. Upper case: TASM shows
// its keywords in capitals (seen in the emulator on 3.0, 4.0 and 4.12), the tables in the binaries are upper case
constexpr TokenTable kTasm40 = {
    "A",     "ADC ",   "ADD ",     "AF'",     "AF",     "AND ",   "B",      "BC",        // 80-87
    "BIT ",  "C",      "CALL ",    "CCF",     "CP ",    "CPD",    "CPDR",   "CPI",       // 88-8F
    "CPIR",  "CPL",    "D",        "DAA",     "DE",     "DEC ",   "DEFB ",  "DEFM ",     // 90-97
    "DEFS ", "DEFW ",  "DI",       "PHASE ",  "DJNZ ",  "E",      "EI",     "UNPHASE",   // 98-9F
    "EQU ",  "EX ",    "EXX",      "H",       "HALT",   "HL",     "I",      "IM ",       // A0-A7
    "IN ",   "INC ",   "IND",      "INDR",    "INI",    "INIR",   "IX",     "IY",        // A8-AF
    "JP ",   "JR ",    "L",        "LD ",     "LDD",    "LDDR",   "LDI",    "LDIR",      // B0-B7
    "M",     "NC",     "NEG",      "NOP",     "NV",     "NZ",     "OR ",    "ORG ",      // B8-BF
    "OTDR",  "OTIR",   "OUT ",     "OUTD",    "OUTI",   "P",      "PE",     "PO",        // C0-C7
    "POP ",  "PUSH ",  "R",        "RES ",    "RET",    "RETI",   "RETN",   "RL ",       // C8-CF
    "RLA",   "RLC ",   "RLCA",     "RLD",     "RR ",    "RRA",    "RRC ",   "RRCA",      // D0-D7
    "RRD",   "RST ",   "SBC ",     "SCF",     "SET ",   "SLA ",   "SP",     "SRA ",      // D8-DF
    "SRL ",  "SUB ",   "V",        "XOR ",    "Z",      "INCLUDE ", "INCBIN ", "SLI ",   // E0-E7
    "INF",   "LX",     "HX",       "LY",      "HY",     "DB ",    "DM ",    "DS ",       // E8-EF
    "DW ",                                                                               // F0
};

// TASM 5.5 beta, #80-#F7: the table in its binary, in the binary's order (confirmed on files the editor saved)
constexpr TokenTable kTasm55 = {
    "A", "ADC ", "ADD ", "AF'", "AF", "AND ", "B", "BC",                                  // 80-87
    "BIT ", "C", "C", "CALL ", "CCF", "CP ", "CPD", "CPDR",                                 // 88-8F
    "CPI", "CPIR", "CPL", "D", "DAA", "DB ", "DE", "DEC ",                                  // 90-97
    "DI", "DJNZ ", "DM ", "DS ", "DW ", "E", "EI", "ELSE",                                  // 98-9F
    "ENDIF", "ENDM", "ENDR", "EQU ", "EX ", "EXX", "H", "HALT",                             // A0-A7
    "HL", "HX", "HY", "I", "IF ", "IFDEF ", "IM ", "IN ",                                    // A8-AF
    "INC ", "INCLUDE ", "INCBIN ", "INCSEC ", "IND", "INDR", "INF", "INI",                  // B0-B7
    "INIR", "IX", "IY", "JP ", "JR ", "L", "LD ", "LDD",                                    // B8-BF
    "LDDR", "LDI", "LDIR", "LX", "LY", "M", "MACRO", "NC",                                  // C0-C7
    "NEG", "NOP", "NV", "NZ", "OR ", "ORG ", "OTDR", "OTIR",                                // C8-CF
    "OUT ", "OUTD", "OUTI", "P", "PE", "PHASE ", "PO", "POP ",                              // D0-D7
    "PRINTF ", "PUSH ", "R", "REPT ", "RES ", "RET", "RETI", "RETN",                        // D8-DF
    "RL ", "RLA", "RLC ", "RLCA", "RLD", "RR ", "RRA", "RRC ",                              // E0-E7
    "RRCA", "RRD", "RST ", "SBC ", "SCF", "SET ", "SLA ", "SLI ",                           // E8-EF
    "SP", "SRA ", "SRL ", "SUB ", "UNPHASE", "V", "XOR ", "Z",                              // F0-F7
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
    table[0x97 - kFirstToken] = "DEFMAC ";
    table[0x9B - kFirstToken] = "DISPLAY ";
    table[0x9F - kFirstToken] = "ENDMAC";
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

const TokenTable& Tasm55Tokens()
{
    return kTasm55;
}

bool IsOperandToken(std::string_view name)
{
    static const std::string_view kOperands[] = {"A",  "B",  "C",  "D",  "E",  "H",  "L",  "I",  "R",  "AF", "AF'", "BC", "DE", "HL", "SP",
                                                 "IX", "IY", "HX", "LX", "HY", "LY", "NZ", "Z",  "NC", "PO", "PE",  "P",  "M",  "NV", "V"};
    for (const std::string_view operand : kOperands)
        if (name == operand)
            return true;
    return false;
}
}  // namespace unrealasm::codecs::tasm
