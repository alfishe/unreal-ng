#pragma once

// ZEsarUX breakpoint-condition dialect (the subset DeZog emits).
//
// DeZog (zesaruxremote.ts setBreakpointZesarux) always builds conditions of
// the form:
//   PC=0XXXXh [ and ROM=<0|1>] [ and RAM=<bank>] [ and (<user condition>)]
// stepOut instead sends a raw condition with no address part:
//   PC=PEEKW(SP-2) AND SP>=<decimal>
//
// Supported grammar (case-insensitive throughout; ZEsarUX expression_parser.c precedence):
//   expr    := or
//   or      := and ((OR | XOR) and)*
//   and     := not (AND not)*
//   not     := NOT not | cmp
//   cmp     := sum ((= | <> | != | < | > | <= | >=) sum)?
//   sum     := term ((+|-) term)*
//   term    := factor ((* | / | & | \| | ^) factor)*
//   factor  := number | name | func(factor) | ( expr ) | - factor | + factor
//   number  := hexDigits[h|H] | 0x hexDigits | decimal
//   name    : PC SP AF BC DE HL IX IY AF' BC' HL' DE' I R IM A F B C D E H L
//             IXH IXL IYH IYL, the flags FS FZ FP FV FH FN FC (0/1), ROM RAM (any case)
//   func    : PEEK(expr) PEEKB(expr) PEEKW(expr)
//
// ROM and RAM follow ZEsarUX: on a 128K-style machine (four 16K slots) RAM is the bank paged at C000h and ROM the
// ROM number in slot 0 (DZRP bank 8 -> 0, 9 -> 1); on the 48K and the Next both are 0.
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
    const std::vector<uint8_t>* slots = nullptr;  // DZRP slots (getSlots): 4 on 128K-style machines, 8=ROM0, 9=ROM1
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

// Writes the condition in the core breakpoint-condition language (breakpointcondition.h, every operation in
// brackets) so the core skips the false hits without stopping the machine. The evaluator above stays the reference
// and still runs on every stop: the core form only has to be true whenever the evaluator would be. False, with the
// reason, for what the core answers differently: ROM / RAM (the paging), a division by
// anything but a nonzero number (the evaluator stops on a zero divisor), and text the evaluator can not parse
bool translateToCore(const std::string& condition, std::string& coreOut, std::string& reasonOut);

// Finds the "PC=<hex>h" literal of an address breakpoint (the address the breakpoint is set at). Returns false
// when the condition has none (stepOut-style). restOut receives what is checked on every hit: the rest after
// DeZog's leading "PC=<hex>h [AND ...]", or the whole condition when the literal is elsewhere ("B=5 AND PC=8003h").
bool extractPcLiteral(const std::string& condition, uint16_t& addrOut, std::string& restOut);
} // namespace zrcp
