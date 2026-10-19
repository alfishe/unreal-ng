#pragma once

// sjasmplus' structure words in the IR the targets without them take (the retro backends, asm-synchronizer.md §15.3):
// MODULE as the full names of its labels (main.loop) and of the names used inside it, STRUCT as EQU of its fields'
// offsets and of its size (its name), an instance as its data and EQU of label.field, a LUA block left out with a
// warning, ALIGN as DS, DZ / DC / ABYTE / ABYTEC / ABYTEZ / D24 / DD / DWORD as DB / DW, SLOT / PAGE / MMU dropped
// with a note. The layout (layout::Layout) gives the same names their values.

#include <string>

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
/// The marks LowerSjasmplus leaves where a MODULE opens ("UNREALASM_MODULE name") and closes: the writer finds the
/// names a macro uses in the module it is expanded in (sjasmplus expands macros as text)
inline const std::string kModuleMark = "UNREALASM_MODULE";
inline const std::string kEndModuleMark = "UNREALASM_ENDMODULE";

/// `expandMixedMacros`: a macro whose body has both local labels (.name, temporary ones) and global ones (a label, an
/// assignment) is put in at its calls (ALASM: its LOCAL block would make the global ones local too); `expandAllMacros`:
/// every macro (ALASM 3.8 has none: MACRO is a syntax error there, live)
ir::Program LowerSjasmplus(const ir::Program& program, Diagnostics& diagnostics, bool expandMixedMacros = false, bool expandAllMacros = false);
}  // namespace unrealasm::dialects
