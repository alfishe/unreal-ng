# Breakpoint System Enhancements

## Current State

unreal-qt's `BreakpointDescriptor` supports:
- Single address (no ranges)
- Memory and IO types
- Active/inactive toggle
- Group and note annotations
- No conditions, no hit counters

## Proposed Enhancements

### 1. Address Ranges

```cpp
struct BreakpointDescriptor {
    // Existing
    uint16_t z80address = 0xFFFF;
    
    // New - range support
    uint16_t z80addressEnd = 0xFFFF;  // End of range (inclusive)
                                       // If == z80address, single address
    
    // For bank-specific breakpoints
    uint16_t bankOffsetEnd = 0xFFFF;
};
```

**Implementation impact:**
- `HandlePCChange()` checks `pc >= adr && pc <= adrEnd`
- UI: address field accepts `0x1234-0x5678` format
- Serialization: save both addresses

### 2. Conditional Breakpoints

```cpp
struct BreakpointDescriptor {
    // Existing fields...
    
    // Condition support
    std::string condition;                        // Expression text (empty = unconditional)
    Expression::CompiledExpression compiledCond;  // Compiled form
    
    // Hit counting
    uint32_t hitCount = 0;       // Times breakpoint address was hit
    uint32_t fireCount = 0;      // Times condition was true
    uint32_t skipCount = 0;      // Skip first N fires
    
    // Edge detection for conditions
    bool edgeTrigger = false;    // Fire only on false->true transition
    bool lastCondValue = false;  // Last condition evaluation result
    
    // Actions
    enum class Action : uint8_t {
        Break,          // Enter debugger (default)
        Log,            // Log to console, continue
        Screenshot,     // Capture frame, continue
        CountOnly,      // Just increment counters
    };
    Action action = Action::Break;
};
```

**Evaluation flow:**
```cpp
uint16_t BreakpointManager::HandlePCChange(uint16_t pc) {
    BreakpointDescriptor* bp = FindAddressBreakpoint(pc);
    if (!bp || !bp->active) return BRK_INVALID;
    
    // Check address type match
    if (!(bp->memoryType & BRK_MEM_EXECUTE)) return BRK_INVALID;
    
    // Check range
    if (pc < bp->z80address || pc > bp->z80addressEnd) return BRK_INVALID;
    
    // Increment hit count (address matched)
    bp->hitCount++;
    
    // Evaluate condition
    if (!bp->condition.empty()) {
        EvaluationContext evalCtx = buildEvalContext();
        evalCtx.hitCount = bp->hitCount;
        
        bool error = false;
        uint32_t result = Expression::evaluate(bp->compiledCond, _context, &evalCtx, &error);
        
        if (error) {
            // Broken condition = always true (fail-safe)
            result = 1;
        }
        
        // Edge detection
        if (bp->edgeTrigger) {
            bool shouldFire = (result != 0) && !bp->lastCondValue;
            bp->lastCondValue = (result != 0);
            if (!shouldFire) return BRK_INVALID;
        } else if (result == 0) {
            bp->lastCondValue = false;
            return BRK_INVALID;
        }
    }
    
    // Increment fire count
    bp->fireCount++;
    
    // Check skip count
    if (bp->fireCount <= bp->skipCount) return BRK_INVALID;
    
    // Perform action
    _lastTriggeredBreakpointID = bp->breakpointID;
    
    switch (bp->action) {
        case Action::Break:
            return bp->breakpointID;
            
        case Action::Log:
            logBreakpoint(bp, pc);
            return BRK_INVALID;
            
        case Action::Screenshot:
            captureScreenshot(bp, pc);
            return BRK_INVALID;
            
        case Action::CountOnly:
            return BRK_INVALID;
    }
    
    return bp->breakpointID;
}
```

### 3. IRQ Breakpoints

Add new breakpoint type for interrupt handling:

```cpp
enum BreakpointTypeEnum : uint8_t {
    BRK_MEMORY = 0,
    BRK_IO,
    BRK_KEYBOARD,
    BRK_IRQ,        // New: interrupt trigger
};

// IRQ sub-types
constexpr uint8_t BRK_IRQ_NONE = 0x00;
constexpr uint8_t BRK_IRQ_INT = 0x01;    // Maskable interrupt
constexpr uint8_t BRK_IRQ_NMI = 0x02;    // Non-maskable interrupt
constexpr uint8_t BRK_IRQ_ALL = 0xFF;
```

**Implementation:**
- Hook into `Z80::handleInterrupt()`
- Check for active IRQ breakpoints
- Fire before interrupt handler executes

### 4. Port Mask Support

For IO breakpoints, support partial port matching:

```cpp
struct BreakpointDescriptor {
    // Existing
    uint16_t z80address;  // Port address for BRK_IO
    
    // New
    uint16_t portMask = 0xFFFF;  // Mask for port matching
                                  // Match if: (actualPort & mask) == (z80address & mask)
};
```

**Use case:** Break on any port matching `XX01` pattern:
```
port = 0x01
mask = 0x00FF
```

### 5. Global Conditions

Breakpoints without address - fire when condition becomes true:

```cpp
struct BreakpointDescriptor {
    // If type == BRK_CONDITION, address is ignored
    // Condition is evaluated every instruction
};

enum BreakpointTypeEnum : uint8_t {
    BRK_MEMORY = 0,
    BRK_IO,
    BRK_KEYBOARD,
    BRK_IRQ,
    BRK_CONDITION,  // Global condition, no address
};
```

**Evaluation:** Checked in main emulation loop:
```cpp
void Emulator::executeInstruction() {
    // ... execute instruction ...
    
    // Check global conditions
    for (auto& bp : _breakpointManager->GetConditionBreakpoints()) {
        if (evaluateCondition(bp)) {
            enterDebugger(bp);
            break;
        }
    }
}
```

**Performance note:** Global conditions are expensive. Limit to small number.

## API Changes

### BreakpointManager New Methods

```cpp
class BreakpointManager {
public:
    // Range breakpoints
    uint16_t AddExecutionBreakpointRange(uint16_t startAddr, uint16_t endAddr,
                                         const std::string& owner = OWNER_INTERACTIVE);
    
    // Conditional breakpoints
    bool SetBreakpointCondition(uint16_t breakpointID, const std::string& condition);
    std::string GetBreakpointCondition(uint16_t breakpointID) const;
    bool ValidateCondition(const std::string& condition, std::string* error = nullptr);
    
    // Hit counters
    void ResetBreakpointCounters(uint16_t breakpointID);
    void ResetAllCounters();
    uint32_t GetHitCount(uint16_t breakpointID) const;
    uint32_t GetFireCount(uint16_t breakpointID) const;
    
    // Skip count
    void SetSkipCount(uint16_t breakpointID, uint32_t count);
    
    // Actions
    void SetBreakpointAction(uint16_t breakpointID, BreakpointDescriptor::Action action);
    
    // Edge trigger
    void SetEdgeTrigger(uint16_t breakpointID, bool enable);
    
    // IRQ breakpoints
    uint16_t AddIRQBreakpoint(uint8_t irqType, const std::string& owner = OWNER_INTERACTIVE);
    
    // Global conditions
    uint16_t AddGlobalCondition(const std::string& condition,
                                const std::string& owner = OWNER_INTERACTIVE);
    const std::vector<BreakpointDescriptor*>& GetConditionBreakpoints() const;
    
    // Event context (for expression evaluation)
    void UpdateLastRead(uint16_t addr, uint8_t data);
    void UpdateLastWrite(uint16_t addr, uint8_t data);
    void UpdateLastIn(uint16_t port, uint8_t data);
    void UpdateLastOut(uint16_t port, uint8_t data);
    const Expression::EvaluationContext& GetEvalContext() const;
};
```

## Serialization

Update breakpoint save/load to handle new fields:

```json
{
  "breakpoints": [
    {
      "id": 1,
      "type": "memory",
      "address": "0x8000",
      "addressEnd": "0x8FFF",
      "memoryType": ["execute"],
      "condition": "A == 5 && M(HL) > 0x20",
      "skipCount": 10,
      "edgeTrigger": false,
      "action": "break",
      "active": true,
      "group": "default",
      "note": "Break in main loop when A=5"
    },
    {
      "id": 2,
      "type": "io",
      "address": "0x00FE",
      "portMask": "0x00FF",
      "ioType": ["in"],
      "condition": "(VAL & 0x1F) != 0x1F",
      "action": "log"
    },
    {
      "id": 3,
      "type": "condition",
      "condition": "SP < #8000",
      "edgeTrigger": true,
      "note": "Stack overflow detection"
    }
  ]
}
```

## Compatibility with Unreal bpx.ini

xpeccy-plus loads Unreal's breakpoint format. Consider supporting:

```ini
; Unreal bpx.ini format
x0=0x80A6        ; Execute at 0x80A6, CPU 0
r0=0x1234        ; Read at 0x1234
w0=0x5678        ; Write at 0x5678
x0=0x8000-0x8FFF ; Execute range
```

This would allow importing breakpoints from classic Unreal sessions.
