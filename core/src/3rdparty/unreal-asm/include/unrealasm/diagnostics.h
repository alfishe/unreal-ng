#pragma once

// Diagnostics every operation of the library returns: what went wrong or was changed, with the line (1-based) and
// the byte offset in the input when known.

#include <cstdint>
#include <string>
#include <vector>

namespace unrealasm
{
enum class Severity : uint8_t
{
    Info,
    Warning,
    Error,
};

struct Diagnostic
{
    Severity severity = Severity::Info;
    uint32_t line = 0;          ///< 1-based; 0 = the whole input
    uint64_t byteOffset = 0;    ///< in the input bytes, when it applies
    std::string message;
};

using Diagnostics = std::vector<Diagnostic>;

/// True when any diagnostic is an error
bool HasErrors(const Diagnostics& diagnostics);
}  // namespace unrealasm
