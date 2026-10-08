#include "codecs/alasm/alasmtokens.h"

namespace unrealasm::codecs::alasm
{
namespace
{
// ALASM 5.07-5.09: the `mnemtkn` table of alTOKENS.H (the ALASM 5.09 sources); the same bytes in the binaries of
// 5.07, 5.08, 5.09 and the ZX Evolution 5.09 edition
constexpr MnemonicTable kAlasm5 = {
    "INCLUDE", "INCBIN", "MACRO",  "LOCAL",   "RLCA",   "RRCA",   "HALT",  "CALL",   // 80-87
    "PUSH",    "RETN",   "RETI",   "DJNZ",    "OUTI",   "OUTD",   "LDIR",  "CPIR",   // 88-8F
    "INIR",    "OTIR",   "LDDR",   "CPDR",    "INDR",   "OTDR",   "DD",    "DEFB",   // 90-97
    "DEFW",    "DEFS",   "DISP",   "ENDM",    "EDUP",   "ENDL",   "MAIN",  "ELSE",   // 98-9F
    "DISPLAY", "EXA",    "DB",     "DW",      "DS",     "NOP",    "INC",   "DEC",    // A0-A7
    "RLA",     "RRA",    "DAA",    "CPL",     "SCF",    "CCF",    "ADD",   "ADC",    // A8-AF
    "SUB",     "SBC",    "AND",    "XOR",     "RET",    "POP",    "RST",   "EXX",    // B0-B7
    "RLC",     "RRC",    "SLA",    "SRA",     "SLI",    "SRL",    "BIT",   "RES",    // B8-BF
    "SET",     "OUT",    "NEG",    "RRD",     "RLD",    "LDI",    "CPI",   "INI",    // C0-C7
    "LDD",     "CPD",    "IND",    "ORG",     "EQU",    "ENT",    "INF",   "DUP",    // C8-CF
    "IFN",     "REPEAT", "UNTIL0", "IF0",     "LD",     "JR",     "JP",    "OR",     // D0-D7
    "CP",      "EX",     "DI",     "EI",      "IN",     "RL",     "RR",    "IM",     // D8-DF
    "ENDIF",   "EXD",    "JNZ",    "JZ",      "JNC",    "JC",     "RUN",             // E0-E6
};

// `regstkn` of alTOKENS.H; the same in every version from 3.8 on
constexpr RegisterTable kRegisters = {
    "(BC)", "(DE)", "(HL)", "(SP)", "(IX)", "(IY)",                                     // 9F-A4
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",  // A5-B9
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",  // BA-CE
    "",                                                                                // CF
    "(C)", "(IX", "(IY", "AF'",                                                        // D0-D3
    "", "", "", "", "", "", "", "", "", "", "", "",                                    // D4-DF
    "BC", "DE", "HL", "AF", "IX", "IY", "SP", "NZ", "NC", "PO", "PE", "HX", "LX", "HY", "LY",   // E0-EE
    "B", "C", "D", "E", "H", "L", "A", "P", "M", "Z", "R", "I",                        // EF-FA
};

struct Change
{
    uint8_t code;
    std::string_view name;   ///< "" removes the keyword
};

MnemonicTable Derive(std::initializer_list<Change> changes)
{
    MnemonicTable table = kAlasm5;
    for (const Change& c : changes)
        table[c.code - kFirstMnemonic] = c.name;
    return table;
}

// The older tables, read from each version's binary (research-alasm.md §3): 3.8 alasm4x8.C, 4.2 alasm48.C,
// 4.42 alasm442.C, 4.5 alasm4.5.C, 4.44 al64_444.C / al42_444.C, 5.0 alasm_64.C / alasm_42.C, 5.05 alasm_64.C /
// alasm_42.C (and its alTOKENS.H). Other builds share one of them: 4.3 has 4.2's table, 4.43 4.5's, 4.45 / 4.46 4.44's
std::vector<Version> Build()
{
    std::vector<Version> versions;
    versions.push_back({"3.8", "ALASM 3.8",
                        Derive({{0x83, "ERASE"}, {0x96, "DEFM"}, {0x9D, "STOP"}, {0x9F, ""}, {0xA0, ""}, {0xA1, ""}, {0xA2, ""},
                                {0xA3, ""}, {0xA4, ""}, {0xD0, ""}, {0xD1, ""}, {0xD2, ""}, {0xD3, ""}, {0xE0, ""}, {0xE1, ""},
                                {0xE2, ""}, {0xE3, ""}, {0xE4, ""}, {0xE5, ""}, {0xE6, ""}})});
    versions.push_back({"4.2", "ALASM 4.2 / 4.3",
                        Derive({{0x96, "DEFM"}, {0xA0, ""}, {0xA1, ""}, {0xD1, ""}, {0xD2, ""}, {0xD3, "IF"}, {0xE1, ""}, {0xE2, ""},
                                {0xE3, ""}, {0xE4, ""}, {0xE5, ""}, {0xE6, ""}})});
    versions.push_back({"4.42", "ALASM 4.42",
                        Derive({{0x96, "DEFM"}, {0xA1, ""}, {0xD1, ""}, {0xD2, ""}, {0xD3, "IF"}, {0xE1, ""}, {0xE2, ""}, {0xE3, ""},
                                {0xE4, ""}, {0xE5, ""}, {0xE6, ""}})});
    versions.push_back({"4.5", "ALASM 4.43 / 4.5",
                        Derive({{0x96, "DEFM"}, {0xA1, ""}, {0xD2, "UNTIL"}, {0xD3, "IF"}, {0xE1, ""}, {0xE2, ""}, {0xE3, ""}, {0xE4, ""},
                                {0xE5, ""}, {0xE6, ""}})});
    versions.push_back({"4.44", "ALASM 4.44-4.46",
                        Derive({{0xD2, "UNTIL"}, {0xD3, "IF"}, {0xE1, ""}, {0xE2, ""}, {0xE3, ""}, {0xE4, ""}, {0xE5, ""}, {0xE6, ""}})});
    versions.push_back({"5.0", "ALASM 5.0", Derive({{0xD2, "UNTIL"}, {0xD3, "IF"}, {0xE6, ""}})});
    versions.push_back({"5.05", "ALASM 5.05", Derive({{0xD2, "UNTIL"}})});
    versions.push_back({"5.07", "ALASM 5.07-5.09", kAlasm5});
    return versions;
}
}  // namespace

const std::vector<Version>& Versions()
{
    static const std::vector<Version> versions = Build();
    return versions;
}

const Version* FindVersion(std::string_view id)
{
    for (const Version& v : Versions())
        if (v.id == id)
            return &v;
    return nullptr;
}

const RegisterTable& Registers()
{
    return kRegisters;
}
}  // namespace unrealasm::codecs::alasm
