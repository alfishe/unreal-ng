#pragma once

// ZEsarUX breakpoint-condition dialect (the subset DeZog emits).
//
// DeZog (zesaruxremote.ts setBreakpointZesarux) always builds conditions of
// the form:
//   PC=0XXXXh [ and ROM=<0|1>] [ and RAM=<bank>] [ and (<user condition>)]
// stepOut instead sends a raw condition with no address part:
//   PC=PEEKW(SP-2) AND SP>=<decimal>
//
// Supported grammar (case-insensitive throughout):
//   expr    := or
//   or      := and (OR and)*
//   and     := not (AND not)*
//   not     := NOT not | cmp
//   cmp     := sum ((= | <> | != | < | > | <= | >=) sum)?
//   sum     := term ((+|-) term)*
//   term    := factor ((*|/) factor)*
//   factor  := number | name | func(factor) | ( expr ) | - factor | + factor
//   number  := hexDigits[h|H] | 0x hexDigits | decimal
//   name    : PC SP AF BC DE HL IX IY AF' BC' HL' DE' I R IM A F B C D E H L
//             IXH IXL IYH IYL ROM RAM
//   func    : PEEK(expr) PEEKB(expr) PEEKW(expr)
//
// ROM evaluates to the ROM index visible at the breakpoint address (0/1) or
// -1 when that address is not ROM-backed; RAM to the RAM bank visible there
// or -1. This makes " and ROM=1" false whenever some other bank is paged in,
// matching ZEsarUX semantics.
//
// Parse failures evaluate to true (conservative: stop) - silently running
// past a user breakpoint would be worse than a spurious stop.

#include "dzrpserver.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace zrcp
{
struct ConditionContext
{
    const dzrp::IDebugInterface::Registers* regs = nullptr;
    const std::vector<uint8_t>* slots = nullptr;  // per 16 KB slot; >=8 means ROM (8=ROM0, 9=ROM1)
    uint16_t breakpointAddr = 0;
    std::function<uint8_t(uint16_t)> readMem;     // byte read from Z80 address space
};

class ConditionEvaluator
{
public:
    // Returns the truth of the condition. *errorOut (when non-null) receives
    // a reason when parsing failed - the result is then true (conservative).
    static bool evaluate(const std::string& condition, const ConditionContext& ctx,
                         std::string* errorOut = nullptr);
};

// Extracts the leading "PC=<hex>h" literal DeZog emits for address breakpoints.
// Returns false when the condition has no PC literal (stepOut-style).
// restOut receives the remaining condition (re-evaluated on every hit).
bool extractPcLiteral(const std::string& condition, uint16_t& addrOut, std::string& restOut);
} // namespace zrcp
