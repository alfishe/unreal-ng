# Segmentation Accuracy Analysis

## What Runs in the Default Idle Loop

### Spectrum 48K Idle Loop
The Z80 executes a tight loop between these key ROM regions:

| Address | Label | Description |
|---------|-------|-------------|
| `0x0038` | `MASK-INT` | IM1 interrupt vector: PUSH AF/HL, increment FRAMES, call `KEYBOARD` |
| `0x02BF` | `KEYBOARD` | Called from ISR: scans 8 keyboard rows via `IN (C)` on port `$FE` |
| `0x028E` | `KEY-SCAN` | Inner keyboard loop |
| `0x1303` | `MAIN-4` | **HALT** (waits for interrupt), resets flags, prints report |
| `0x12AC` | `MAIN-2` | Opens channel K, calls EDITOR, calls LINE-SCAN |
| `0x0F2C` | `EDITOR` | Main input editor |
| `0x12CF` | `MAIN-3` | Checks line number / carriage-return |
| `0x12A2` | `MAIN-EXEC` | Sets DF-SZ, calls AUTO-LIST |

**Key insight**: The idle loop visits scattered ROM addresses. Without full R/W/X coverage, many operand bytes remain `UNKNOWN`, creating false gaps.

### Spectrum 128K Idle Loop
The 128K ROM0 interrupt handler at `0x0038` is different — it pushes return addresses and jumps to SWAP at `$5B00` (a RAM-resident paging routine) to call the ROM1 interrupt handler, then returns to `$0048`.

| Address | Description |
|---------|-------------|
| `0x0038` | Push HL, push return+SWAP addresses, JP to `$5B00` |
| `$5B00–$5B13` | **SWAP** — executable code in RAM (paging subroutine) |
| `$5B14–$5B1C` | **YOUNGER** — return paging subroutine |
| `$5B1D–$5B2E` | **ONERR** — error handler paging subroutine |
| `0x0048` | POP HL / RET (after ROM1 executes) |

## Key Bugs in GenericTagClassifier

### Bug 1: Wrong system variable range end
```cpp
// Current (WRONG):
for (uint32_t addr = 0x5B00; addr <= 0x5CBF; ++addr)
    tagMap.addTag(addr, MemoryTag::SystemVariables);
```
- **48K**: System variables go from `$5C00` to `$5CB5` (not starting at `$5B00`)
  - `$5B00–$5BFF` = ZX printer buffer (repurposed as system variables by 128K)
- **128K**: `$5B00–$5B2E` = executable code (SWAP/YOUNGER/ONERR RAM routines)
  - `$5B2F–$5BFF` = other 128K system variables (PIN, POUT, etc.)
  - `$5C00–$5CB5` = standard system variables

### Bug 2: The classifier doesn't know the machine model
The classifier doesn't detect whether it's running on a 48K or 128K machine.

## Correct Ranges by Model

### 48K Spectrum
- `$4000–$57FF` → ScreenBitmap
- `$5800–$5AFF` → ScreenAttributes  
- `$5C00–$5CB5` → SystemVariables (correct end!)
- `$5B00–$5BFF` → ZX Printer buffer (GenericData if written)
- `$0000–$3FFF` → ROM (executed, never written)

### 128K Spectrum (ROM0 mode)
- `$4000–$57FF` → ScreenBitmap
- `$5800–$5AFF` → ScreenAttributes
- **`$5B00–$5B13`** → SWAP (executed RAM code — must be CODE or SMC)
- **`$5B14–$5B1C`** → YOUNGER (executed RAM code)
- **`$5B1D–$5B2E`** → ONERR (executed RAM code)
- `$5B2F–$5BFF` → 128K system variables
- `$5C00–$5CB5` → standard system variables

## Additional Issues in the Classifier Pipeline

### ISR region detection (InterruptClassifier)
The RETI-based detection works for user-space ISRs, but the ROM ISR at `0x0038–0x004F` returns via `RET` (not `RETI` in IM1). On 128K it uses a custom SWAP mechanism. The ISR is at ROM addresses < `0x4000` so it gets `BlockType::CODE` naturally (if executed).

### The operand-fill problem
Without the fill-forward pass, bytes like the many 2-byte and 3-byte instructions in the ISR paths leave `UNKNOWN` gaps. The fix (already merged) resolves this for executed regions.

### What's still not right
1. **128K RAM routines in `$5B00–$5B2E` tagged as SystemVariables** — they should be recognized as executable code (the memory tracker will show high execute counts there)
2. **System variables end address** — `$5CBF` is wrong; correct end is `$5CB5` for 48K standard vars

## Required Fix

In `GenericTagClassifier::classify()`:
1. Read `ctx.emulatorContext->config.mem_model` to detect 48K vs 128K
2. Apply model-specific ranges:
   - All models: `$5C00–$5CB5` → SystemVariables (not `$5CBF`)
   - **128K models only**: `$5B00–$5B2E` → do NOT override as SystemVariables (leave to type-based classification which will see execute counters and mark it CODE)
   - **48K**: `$5B00–$5BFF` → SystemVariables / GenericData (ZX printer buffer)
3. The models that use 128K paging: `MM_SPECTRUM128`, `MM_PLUS3`, `MM_PENTAGON`, `MM_SCORP`, etc.
