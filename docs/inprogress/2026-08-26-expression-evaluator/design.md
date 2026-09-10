# Expression Evaluator Design

## Overview

Implement a C-like expression evaluator for conditional breakpoints and memory watching.
Based on xpeccy-plus `xexpr` system but adapted for unreal-qt architecture.

## File Structure

```
core/src/debugger/expression/
├── expression.h           # Public API
├── expression.cpp         # Compiler and evaluator
├── expression_opcodes.h   # RPN opcodes
└── expression_variables.h # Pseudo-variable definitions
```

## API Design

```cpp
// expression.h

#pragma once
#include <string>
#include <vector>
#include <cstdint>

class EmulatorContext;

namespace Expression {

// RPN opcode types
enum class Opcode : uint8_t {
    // Operands (push to stack)
    NUM,        // Push literal number
    REG,        // Push CPU register value
    LABEL,      // Push label address
    VAR,        // Push pseudo-variable
    
    // Unary operators
    NOT,        // Logical NOT
    INV,        // Bitwise NOT
    NEG,        // Unary minus
    MRDB,       // Memory read byte: M(addr)
    MRDW,       // Memory read word: [addr]
    
    // Binary operators
    MUL, DIV, MOD,
    ADD, SUB,
    SHL, SHR,
    LT, GT, LE, GE,
    EQ, NE,
    AND, XOR, OR,
    LAND, LOR,
    
    // Special
    RAYHIT,     // RAY(x, y) beam position check
};

// Pseudo-variables accessible in expressions
enum class Variable : uint8_t {
    RD,         // Last memory read address
    WR,         // Last memory write address
    MDT,        // Last memory data
    IN,         // Last IN port
    OUT,        // Last OUT port
    VAL,        // Last IN/OUT data
    DOS,        // TR-DOS mode active
    SLOT0,      // Page in slot 0
    SLOT1,      // Page in slot 1
    SLOT2,      // Page in slot 2
    SLOT3,      // Page in slot 3
    FRAME,      // Frame counter
    RAYX,       // Beam X position
    RAYY,       // Beam Y position
    HITS,       // Breakpoint hit counter
};

// Single RPN instruction
struct Instruction {
    Opcode opcode;
    uint32_t value;         // For NUM: the number
    int8_t numBase;         // For NUM: original base (10, 16, 8)
    std::string name;       // For REG/LABEL: identifier
};

// Compiled expression
struct CompiledExpression {
    std::vector<Instruction> code;
    std::string source;     // Original text
    std::string error;      // Error message if compilation failed
    int errorPos;           // Position in source where error occurred
    
    bool isValid() const { return error.empty() && !code.empty(); }
};

// Evaluation context for tracking last memory/IO operations
struct EvaluationContext {
    uint16_t lastReadAddr = 0;
    uint16_t lastWriteAddr = 0;
    uint8_t lastMemData = 0;
    uint16_t lastInPort = 0;
    uint16_t lastOutPort = 0;
    uint8_t lastIOData = 0;
    uint32_t frameCount = 0;
    uint16_t rayX = 0;
    uint16_t rayY = 0;
    uint32_t hitCount = 0;
    
    // For RAY(x,y) - beam position at instruction start
    uint16_t rayStartX = 0;
    uint16_t rayStartY = 0;
};

// Compile expression from text
CompiledExpression compile(const char* source, EmulatorContext* ctx = nullptr);

// Evaluate compiled expression, returns result
// Sets *error = true on evaluation failure
uint32_t evaluate(
    const CompiledExpression& expr,
    EmulatorContext* ctx,
    const EvaluationContext* evalCtx,
    bool* error = nullptr
);

// Get human-readable form of compiled expression (for UI display)
std::string decompile(const CompiledExpression& expr);

// Check if expression uses a specific pseudo-variable
bool usesVariable(const CompiledExpression& expr, Variable var);

} // namespace Expression
```

## Compilation Algorithm

Parser structure (recursive descent):

```
expression := binary(1)
binary(minPrio) := unary (op unary)*
unary := ('!' | '~' | '-' | '+')? primary
primary := number | name | '(' expression ')' | '[' expression ']' | 'M(' expression ')'
```

Output is Reverse Polish Notation (RPN) for stack-based evaluation.

### Number Parsing

| Format | Example | Result |
|--------|---------|--------|
| Decimal | `1234` | 1234 |
| Hex (0x) | `0x4AF3` | 0x4AF3 |
| Hex (#) | `#4AF3` | 0x4AF3 |
| Hex ($) | `$4AF3` | 0x4AF3 |
| Octal | `0177` | 127 |
| Char | `'A'` | 65 |

### Operator Precedence (higher = tighter binding)

| Priority | Operators |
|----------|-----------|
| 1 | `\|\|` |
| 2 | `&&` |
| 3 | `\|` |
| 4 | `^` |
| 5 | `&` |
| 6 | `==` `!=` |
| 7 | `<` `>` `<=` `>=` |
| 8 | `<<` `>>` |
| 9 | `+` `-` |
| 10 | `*` `/` `%` |
| 11 | `->` (memory offset) |

### Name Resolution Order

1. CPU registers (A, BC, DE, HL, IX, IY, SP, PC, AF', etc.)
2. Pseudo-variables (RD, WR, MDT, etc.)
3. Labels (from LabelManager)
4. Error if not found

Use `.name` to force register interpretation (e.g., `.BC` when BC is also a label).

## Evaluation Implementation

```cpp
uint32_t evaluate(const CompiledExpression& expr, EmulatorContext* ctx,
                  const EvaluationContext* evalCtx, bool* error) {
    constexpr int STACK_SIZE = 64;
    uint32_t stack[STACK_SIZE];
    int sp = 0;
    
    for (const auto& instr : expr.code) {
        switch (instr.opcode) {
        case Opcode::NUM:
            stack[sp++] = instr.value;
            break;
            
        case Opcode::REG:
            stack[sp++] = getRegisterValue(ctx, instr.name);
            break;
            
        case Opcode::VAR:
            stack[sp++] = getVariableValue(evalCtx, (Variable)instr.value);
            break;
            
        case Opcode::MRDB:
            stack[sp-1] = ctx->memory->readByte(stack[sp-1]);
            break;
            
        case Opcode::ADD:
            stack[sp-2] += stack[sp-1];
            sp--;
            break;
            
        // ... other operators
        }
    }
    
    return stack[0];
}
```

## Integration Points

### BreakpointManager Integration

```cpp
// breakpointmanager.h additions

struct BreakpointDescriptor {
    // ... existing fields ...
    
    // New fields for conditional breakpoints
    std::string conditionSource;              // Expression text
    Expression::CompiledExpression condition; // Compiled form
    uint32_t hitCount = 0;                    // Times triggered
    uint32_t skipCount = 0;                   // Skip first N hits
    bool edgeTrigger = false;                 // Fire on false->true only
    bool lastConditionValue = false;          // For edge detection
    
    enum Action {
        ACTION_BREAK,       // Enter debugger (default)
        ACTION_LOG,         // Log to console only
        ACTION_SCREENSHOT,  // Capture frame
        ACTION_COUNT,       // Increment counter only
    };
    Action action = ACTION_BREAK;
};
```

### Event Tracking

The emulator core must track memory/IO operations for pseudo-variables:

```cpp
// Add to EmulatorContext or similar
struct DebugEventState {
    uint16_t lastReadAddr = 0;
    uint16_t lastWriteAddr = 0;
    uint8_t lastMemData = 0;
    uint16_t lastInPort = 0;
    uint16_t lastOutPort = 0;
    uint8_t lastIOData = 0;
};

// Call from memory read path
void Memory::trackRead(uint16_t addr, uint8_t data) {
    if (debugState) {
        debugState->lastReadAddr = addr;
        debugState->lastMemData = data;
    }
}
```

## Example Expressions

```
// Break when A register equals 5
A == 5

// Break when writing to screen memory
WR >= 0x4000 && WR < 0x5B00

// Break when stack pointer goes below threshold
SP < #8000

// Break when reading from port 0xFE (keyboard)
IN == #FE && (VAL & 0x1F) != 0x1F

// Break when beam reaches specific position
RAY(128, 96)

// Break after 100 hits
HITS >= 100

// Break when page 5 is in slot 1
SLOT1 == 5

// Memory comparison
M(HL) == M(DE)

// Complex condition
(A == 'Z' || A == 'z') && M(HL+1) > 0x20
```

## Testing Strategy

1. **Unit tests** for expression compiler
   - Valid expressions compile correctly
   - Syntax errors reported with position
   - All operators produce correct RPN

2. **Unit tests** for evaluator
   - Arithmetic operations
   - Memory access
   - Register access
   - Pseudo-variables

3. **Integration tests**
   - Breakpoint conditions work end-to-end
   - Edge detection functions correctly
   - Hit counters increment properly
