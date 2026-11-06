#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// GDB breakpoint conditions evaluated by the target (qSupported ConditionalBreakpoints+): the agent expressions of
/// `Z0,addr,kind;X len,bytes[;X ...]` written in the core breakpoint-condition language (breakpointcondition.h).
///
/// The bytecode (gdb/doc/agentexpr.texi, gdbsupport/ax.def) is executed symbolically: every value becomes a core
/// expression, every `if_goto` splits the run into two paths, and the condition is the || of each path's
/// (branch conditions && result). gdb evaluates the condition again on its side when the target stops (breakpoint.c
/// bpstat_check_breakpoint_conditions), so the core form only spares the stops where it is false and has to agree
/// with the bytecode wherever that is true.
///
/// Refused (the breakpoint is then set without a condition and gdb alone decides): a backward jump (a loop), more
/// than kMaxPaths paths, trace / printf / state variables / floats / ref64, registers the core has no operand for
/// (p1ffd, pfe), a division or remainder by anything but a nonzero constant (an error in the agent), a shift by a
/// count that is not a constant 0..63, an unsigned compare or division and a signed right shift of a value that can
/// be negative, and a constant beyond 15 hex digits
class GDBAgentExpression
{
public:
    static constexpr size_t kMaxPaths = 64;

    /// One agent expression: true with its core form, false with the reason
    static bool toCondition(const std::vector<uint8_t>& bytecode, std::string& out, std::string& reason);

    /// The condition list of a Z packet (the text after the first ';': `X len,hex[;X len,hex]...`): the || of the
    /// expressions. False with the reason when one does not parse or translate
    static bool listToCondition(const std::string& list, std::string& out, std::string& reason);
};
