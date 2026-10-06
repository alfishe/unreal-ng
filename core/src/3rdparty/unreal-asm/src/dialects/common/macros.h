#pragma once

// Macro expansion shared by the dialects whose macros a target cannot always express: ALASM (\0..\9, \C \N \S \P \R)
// and TASM 4.12 (\0..\9, \c \n \s \r, the same operators in lower case). A macro that glues a parameter to a name or
// walks its parameter text is expanded at its calls; the others stay macros.

#include <string>
#include <vector>

namespace unrealasm::dialects
{
/// A macro body gluing a parameter to a name (ax\0, TEXTURER\0MAX) or walking its parameter text (\C \N \S \P \R in
/// either case)
bool NeedsExpansion(const std::vector<std::string>& body);

/// The parameter text of one macro call and the pointer into it (ALASM help "MACRO"): \0..\9 count comma-separated
/// parameters from the pointer, \P returns parameter 0 and moves the pointer to parameter 1, \C is the symbol at the
/// pointer, \N moves it one symbol, \S<char> is the text from the pointer up to <char>, \R puts the pointer back
struct MacroArguments
{
    std::string text;
    size_t pointer = 0;

    /// Offset of the end of the parameter starting at `from` (a comma outside quotes, or the end)
    size_t FieldEnd(size_t from) const;
    std::string Field(size_t index) const;
};

/// One body line with the arguments put in (the operators move `args.pointer`)
std::string Substitute(const std::string& line, MacroArguments& args);
}  // namespace unrealasm::dialects
