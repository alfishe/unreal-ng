#pragma once

// IR -> sjasmplus (the first output target, decision D-10): sjasmplus' operator priorities (parentheses added where the
// source's left-to-right order differs), LOCAL blocks as renamed labels, macro parameters as named arguments,
// REPEAT / UNTIL0 as WHILE, multi-byte DS fills as DUP; what sjasmplus cannot express becomes a comment and a warning.

#include "unrealasm/dialect.h"

namespace unrealasm::dialects
{
class SjasmplusBackend : public IBackend
{
public:
    std::string_view Dialect() const override { return "sjasmplus"; }
    BackendResult Write(const ir::Program& program, const BackendOptions& options) const override;
};
}  // namespace unrealasm::dialects
