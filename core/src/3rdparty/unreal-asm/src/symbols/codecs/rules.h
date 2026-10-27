#pragma once

// The name rules of the export targets (symbols/formats.md §3.2) and a CodecInfo builder for the codecs.

#include <string>
#include <utility>
#include <vector>

#include "unrealasm/symbols/codec.h"

namespace unrealasm::symbols::codecs
{
/// Our own line formats: a name is a word
inline NameRules WordRules()
{
    NameRules r;
    r.charset = Charset::NoBlank;
    r.forbidden = ";";
    return r;
}

/// sjasmplus (and sjasm): letters, digits, _ . ? ! # @; '.' joins module, label and local label; instructions and
/// registers are reserved
inline NameRules SjasmplusRules()
{
    NameRules r;
    r.charset = Charset::Identifier;
    r.extra = "_.?!#@";
    r.firstExtra = "_.@";
    r.reserveZ80 = true;
    return r;
}

/// pasmo: letters, digits, _ ? @ . ; mnemonics, registers, operators and directives are reserved
inline NameRules PasmoRules()
{
    NameRules r;
    r.charset = Charset::Identifier;
    r.extra = "_?@.";
    r.firstExtra = "_?@.";
    r.reserveZ80 = true;
    r.reserved = {"AND", "OR", "XOR", "NOT", "MOD", "SHL", "SHR", "HIGH", "LOW", "EQ", "NE", "LT", "LE", "GT", "GE", "NUL",
                  "DEFINED", "EQU", "DEFL", "ORG", "DB", "DW", "DS", "DEFB", "DEFW", "DEFS", "DEFM", "END", "IF", "ELSE",
                  "ENDIF", "MACRO", "ENDM", "REPT", "LOCAL", "PROC", "ENDP", "PUBLIC", "INCLUDE", "INCBIN"};
    return r;
}

/// z88dk z80asm: a C identifier; mnemonics, registers and its own keywords are reserved
inline NameRules Z88dkRules()
{
    NameRules r;
    r.charset = Charset::Identifier;
    r.extra = "_";
    r.firstExtra = "_";
    r.reserveZ80 = true;
    r.reserved = {"ASMPC", "DEFC", "DEFB", "DEFW", "DEFS", "DEFM", "EQU", "PUBLIC", "EXTERN", "GLOBAL", "SECTION"};
    return r;
}

/// VICE: letters, digits and _ (the writer adds the '.')
inline NameRules ViceRules()
{
    NameRules r;
    r.charset = Charset::Identifier;
    r.extra = "_";
    r.firstExtra = "_";
    return r;
}

/// IDA: a C identifier plus @ $ ? . , at most 511 bytes
inline NameRules IdaRules()
{
    NameRules r;
    r.charset = Charset::Identifier;
    r.extra = "_@$?.";
    r.firstExtra = "_@$?.";
    r.maxLength = 511;
    return r;
}

inline CodecInfo MakeInfo(std::string id, std::string title, Family family, std::vector<std::string> extensions, NameRules rules,
                          bool pages, std::string comment)
{
    CodecInfo info;
    info.id = std::move(id);
    info.title = std::move(title);
    info.family = family;
    info.extensions = std::move(extensions);
    info.rules = std::move(rules);
    info.pages = pages;
    info.comment = std::move(comment);
    return info;
}
}  // namespace unrealasm::symbols::codecs
