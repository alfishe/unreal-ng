#pragma once

// The last step of a conversion to sjasmplus, done on the whole written project: what needs the project's layout or
// sees across files.
// - A byte operand the source's assembler truncates without a word (ALASM: LD (HL),IMER with IMER = #BFBF, DB of an
//   address in a DUP) gets `low`: sjasmplus builds the same byte but warns, and that warning cannot be silenced. The
//   operands come from the layout of the converted project (layout::LayoutResult::wideBytes).
// - Alone Coder's object saver (SAVEOBJ*: an ObjTab of name, address, length, page, start; RUN in ALASM saves them):
//   the same objects as SLOT 3 / PAGE / SAVEBIN at the end of the unit.
// - ORG address,page with an address that may be outside #C000-#FFFF: ALASM and sjasmplus both map the page at #C000,
//   sjasmplus warns (orgpage); the line says it is meant.

#include <vector>

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
/// files: the written sjasmplus project, as INCLUDE names them; included[k]: some file INCLUDEs file k (not a main)
void FinishSjasmplusProject(std::vector<ProjectFile>& files, const std::vector<bool>& included, const BackendOptions& options,
                            Diagnostics& diagnostics);
}  // namespace unrealasm::dialects
