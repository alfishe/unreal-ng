#pragma once

// Character constants in the source's code page, shared by the frontends

#include "unrealasm/encoding.h"
#include "unrealasm/ir.h"

namespace unrealasm::dialects
{
/// The frontends read a source as UTF-8 text, so a character constant outside ASCII ('Ц' of a CP866 source) got the
/// value of its UTF-8 bytes. Its assembler reads the byte of the code page: every such constant gets the value of its
/// bytes there, combined as the frontend combined the UTF-8 bytes (one character: its byte; several: the frontend's
/// order and width). A code page of several bytes a character (UTF-8) keeps the values
void CharactersInCodePage(ir::Program& program, encoding::CodePage codePage);
}  // namespace unrealasm::dialects
