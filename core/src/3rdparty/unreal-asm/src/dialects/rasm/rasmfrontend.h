#pragma once

// Roudoudou's rasm source -> IR. The source is assembled by the rasm engine (dialects/rasm/engine/, a port of rasm.c
// v3.3.3) and the IR is written from what it put down, as assembled (rasm-engine.md §3): macros and loops expanded, IF /
// SWITCH reduced to what was assembled, instructions with their operands (a label kept where it gives the value rasm
// wrote, else the number), everything else (data, crunched sections, INCBIN transforms, floats, checksums) as DB of the
// bytes rasm wrote. Labels are written where rasm put them, the others (crunched sections, NOCODE, EQU) as EQU.
//
// Outputs: the plain binary (sequential ORGs, as rasm's .bin); BUILDZX as DEVICE ZXSPECTRUM48 + SAVESNA (sjasmplus
// words); SAVE as SAVEBIN regions. CPC media (snapshot, cartridge, ROM, CDT, EDSK, AMSDOS / HOBETA headers) are reported.
//
// A source the engine cannot assemble (a file of a project read alone, an error) is converted line by line with the
// rules of rasm's documentation, as before the engine: labels with a colon or at the start of a line, .name proximity
// and @name local labels, #FF $FF 0xFF FFh %101 0b101 101b @17 numbers, C-like priorities with AND OR XOR MOD, EQU,
// NAME=expr, ORG, DEFB / DEFW / DEFS, REPEAT ... REND, WHILE ... WEND, IF / IFNOT / IFDEF / IFNDEF / ELSE / ENDIF,
// MACRO name (a, b) ... MEND, INCLUDE, INCBIN; the other directives as text.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class RasmFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "rasm"; }
    FrontendResult Parse(const SourceDocument& source) const override;
    FrontendResult ParseInProject(const SourceDocument& source, const std::vector<const SourceDocument*>& project) const override;
    FrontendResult ParseWithData(const SourceDocument& source, const std::vector<const SourceDocument*>& project,
                                 const DataFileReader& dataFiles) const override;
};
}  // namespace unrealasm::dialects
