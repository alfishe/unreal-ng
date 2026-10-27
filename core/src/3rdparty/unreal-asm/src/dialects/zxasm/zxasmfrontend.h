#pragma once

// ZX-ASM / ZAsm -> IR (research-zxasm.md for the format; ZX-ASM 3.10's manual and the 3.3 ReadMe for the syntax):
// statements separated by ":", a comment after ";" or from the first non-Latin character (Russian text needs no ";"),
// expressions left to right without priorities on 16-bit unsigned words, postfix functions on the operand before them
// (.b .h .e .l .r .L .R .c .n .s .m), jrz / callnz / retc forms, PUSH / POP / INC / DEC lists, macros with =1..=n
// parameters that keep the previous call's values, labels local to each macro expansion and REPT pass, nested PHASE,
// IFUSED libraries, INCLUDE / INSERT with several names, SAVEOBJ, ENDA, ~text~ through the XLAT table (ZAsm's own or
// the one LOADTAB loads).

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class ZxasmFrontend : public IFrontend
{
public:
    std::string_view Dialect() const override { return "zxasm"; }
    FrontendResult Parse(const SourceDocument& source) const override;
    FrontendResult ParseInProject(const SourceDocument& source, const std::vector<const SourceDocument*>& project) const override;
    /// LOADTAB reads its table from the data files
    FrontendResult ParseWithData(const SourceDocument& source, const std::vector<const SourceDocument*>& project,
                                 const DataFileReader& dataFiles) const override;
};
}  // namespace unrealasm::dialects
